#include <errno.h>
#include <fcntl.h>
#include <linux/kcov.h>
#include <linux/types.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define KCOV_INIT_TRACE _IOR('c', 1, unsigned long)
#define KCOV_ENABLE _IO('c', 100)
#define KCOV_DISABLE _IO('c', 101)
#define COVER_SIZE (64 << 10)

#define KCOV_TRACE_PC 0
#define KCOV_TRACE_CMP 1

#define KCOV_BUFFER_SIZE (64 * 1024)
#define TEST_DIR "/mnt/nfs"
#define NFS_EXPORT_DIR "/tmp/nfs_share"
#define IMAGE_PATH "/tmp/test.img"
#define LOOP_DEVICE "/dev/loop99"
#define IMAGE_SIZE_MB 400
#define MAX_OPS 100

int kcov_fd = -1;
unsigned long *kcov_cover = NULL;

// Типы операций для фаззинга
typedef enum { OP_CREATE, OP_DELETE, OP_READ, OP_WRITE } fs_operation_t;

// Настройка и запуск NFS
int setup_nfs_server(const char *export_dir, const char *test_dir) {
    // Создание директории для экспорта NFS
    if (mkdir(export_dir, 0777) != 0 && errno != EEXIST) {
        perror("Ошибка создания экспортируемой директории");
        return -1;
    }

    // Добавление записи в /etc/exports
    FILE *exports = fopen("/etc/exports", "a");
    if (!exports) {
        perror("Ошибка открытия /etc/exports");
        return -1;
    }
    fprintf(exports, "%s *(rw,sync,no_subtree_check,no_root_squash)\n", export_dir);
    fclose(exports);

    // Перезапуск службы NFS
    if (system("exportfs -r") != 0) {
        fprintf(stderr, "Ошибка экспорта NFS\n");
        return -1;
    }

    printf("NFS сервер настроен. Экспорт: %s\n", export_dir);
    return 0;
}

// Настройка и монтирование NFS
int mount_nfs(const char *server, const char *export_dir, const char *mount_point) {
    // Создание точки монтирования
    if (mkdir(mount_point, 0777) != 0 && errno != EEXIST) {
        perror("Ошибка создания точки монтирования");
        return -1;
    }

    // Монтирование NFS
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mount -t nfs %s:%s %s", server, export_dir, mount_point);
    printf("Выполнение команды: %s\n", cmd);
    if (system(cmd) != 0) {
        fprintf(stderr, "Ошибка монтирования NFS\n");
        return -1;
    }

    printf("Файловая система NFS смонтирована: %s\n", mount_point);
    return 0;
}

// Инициализация KCOV
void kcov_init() {
    kcov_fd = open("/sys/kernel/debug/kcov", O_RDWR);
    if (kcov_fd == -1) {
        perror("Ошибка открытия kcov");
        exit(1);
    }

    if (ioctl(kcov_fd, KCOV_INIT_TRACE, COVER_SIZE)) {
        perror("Ошибка инициализации kcov");
        close(kcov_fd);
        exit(1);
    }

    kcov_cover = (unsigned long *)mmap(NULL, COVER_SIZE * sizeof(unsigned long),
                                       PROT_READ | PROT_WRITE, MAP_SHARED, kcov_fd, 0);
    if (kcov_cover == MAP_FAILED) {
        perror("Ошибка маппинга буфера kcov");
        close(kcov_fd);
        exit(1);
    }

    if (ioctl(kcov_fd, KCOV_ENABLE, KCOV_TRACE_PC)) {
        perror("Ошибка включения kcov");
        munmap(kcov_cover, COVER_SIZE * sizeof(unsigned long));
        close(kcov_fd);
        exit(1);
    }

    // Сброс покрытия
    __atomic_store_n(&kcov_cover[0], 0, __ATOMIC_RELAXED);
}

// Отключение kcov
void kcov_close() {
  int n = __atomic_load_n(&kcov_cover[0], __ATOMIC_RELAXED);

  FILE *file = fopen("output.txt", "w");
  if (file) {
    for (int i = 0; i < n; i++) {
      fprintf(file, "0x%lx\n", kcov_cover[i + 1]);
    }
    fclose(file);
  } else {
    perror("Ошибка открытия файла для записи kcov_output");
  }

  if (ioctl(kcov_fd, KCOV_DISABLE, 0)) {
    perror("Ошибка отключения kcov");
  }

  if (munmap(kcov_cover, COVER_SIZE * sizeof(unsigned long))) {
    perror("Ошибка при освобождении памяти kcov");
  }

  if (close(kcov_fd)) {
    perror("Ошибка закрытия дескриптора kcov");
  }
}

// Выполнение операций
void perform_operation(fs_operation_t op, const char *test_dir) {
    char path[256];
    snprintf(path, sizeof(path), "%s/file_%d", test_dir, rand() % 10);

    switch (op) {
    case OP_CREATE: {
        int fd = creat(path, S_IRUSR | S_IWUSR);
        if (fd >= 0) {
            close(fd);
            printf("Создан файл: %s\n", path);
        } else {
            perror("Ошибка создания файла");
        }
        break;
    }
    case OP_DELETE: {
        if (unlink(path) == 0) {
            printf("Удален файл: %s\n", path);
        } else {
            perror("Ошибка удаления файла");
        }
        break;
    }
    case OP_READ: {
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            char buf[128];
            read(fd, buf, sizeof(buf));
            close(fd);
            printf("Прочитан файл: %s\n", path);
        } else {
            perror("Ошибка чтения файла");
        }
        break;
    }
    case OP_WRITE: {
        int fd = open(path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
        if (fd >= 0) {
            const char *data = "Фаззинг данных";
            write(fd, data, strlen(data));
            close(fd);
            printf("Записаны данные в файл: %s\n", path);
        } else {
            perror("Ошибка записи в файл");
        }
        break;
    }
    }
}

// Случайный выбор операции
fs_operation_t get_random_operation() { return rand() % 4; }

int main() {
    printf("Подготовка NFS к фаззингу...\n");

    // Настройка и запуск NFS
    if (setup_nfs_server(NFS_EXPORT_DIR, TEST_DIR) != 0) {
        return 1;
    }

    // Монтирование NFS
    if (mount_nfs("127.0.0.1", NFS_EXPORT_DIR, TEST_DIR) != 0) {
        return 1;
    }

    printf("Инициализация KCOV...\n");
    kcov_init();

    printf("Запуск фаззинга...\n");
    srand(time(NULL));
    for (int i = 0; i < MAX_OPS; i++) {
        perform_operation(get_random_operation(), TEST_DIR);
    }

    printf("Отключение KCOV...\n");
    kcov_close();

    printf("Фаззинг завершен.\n");
    return 0;
}
