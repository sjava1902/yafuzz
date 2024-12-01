#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <linux/kcov.h>
#include <errno.h>
#include <time.h>

#define KCOV_BUFFER_SIZE (64 * 1024) // Размер буфера для kcov
#define TEST_DIR "/mnt/ext4_test"
#define IMAGE_PATH "/tmp/test.img"
#define LOOP_DEVICE "/dev/loop99"
#define IMAGE_SIZE_MB 100
#define MAX_OPS 100

// Типы операций для фаззинга
typedef enum {
    OP_CREATE,
    OP_DELETE,
    OP_READ,
    OP_WRITE
} fs_operation_t;

// Создание тестового образа
int create_test_image(const char *path, int size_mb) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        perror("Ошибка создания образа");
        return -1;
    }
    char buffer[1024 * 1024] = {0};
    for (int i = 0; i < size_mb; i++) {
        if (fwrite(buffer, 1, sizeof(buffer), file) != sizeof(buffer)) {
            perror("Ошибка записи в образ");
            fclose(file);
            return -1;
        }
    }
    fclose(file);
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mkfs.ext4 %s", path);
    if (system(cmd) != 0) {
        fprintf(stderr, "Ошибка форматирования образа\n");
        return -1;
    }
    return 0;
}

// Настройка loop-устройства
int setup_loop_device(const char *image_path, const char *loop_device) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "losetup %s %s", loop_device, image_path);
    if (system(cmd) != 0) {
        fprintf(stderr, "Ошибка настройки loop-устройства\n");
        return -1;
    }
    return 0;
}

// Установка debugfs
void mount_debugfs() {
    if (mount("none", "/sys/kernel/debug", "debugfs", 0, NULL) != 0) {
        if (errno != EBUSY) {
            perror("Ошибка монтирования debugfs");
            exit(1);
        }
    }
}

// Инициализация kcov
int init_kcov(const char *kcov_path) {
    int fd = open(kcov_path, O_RDWR);
    if (fd < 0) {
        perror("Ошибка открытия kcov");
        return -1;
    }

    // Установка режима kcov
    if (ioctl(fd, KCOV_INIT_TRACE, KCOV_BUFFER_SIZE)) {
        perror("Ошибка инициализации kcov");
        close(fd);
        return -1;
    }

    // Маппинг буфера трассировки
    void *trace_buffer = mmap(NULL, KCOV_BUFFER_SIZE * sizeof(unsigned long),
                              PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (trace_buffer == MAP_FAILED) {
        perror("Ошибка маппинга буфера kcov");
        close(fd);
        return -1;
    }

    return fd;
}

// Случайный выбор операции
fs_operation_t get_random_operation() {
    return rand() % 4;
}

int file_exists(const char *path) {
    return access(path, F_OK) == 0;
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
            if (file_exists(path)) {
                int fd = open(path, O_RDONLY);
                if (fd >= 0) {
                    char buf[128];
                    read(fd, buf, sizeof(buf));
                    close(fd);
                    printf("Прочитан файл: %s\n", path);
                } else {
                    perror("Ошибка чтения файла");
                }
            } else {
                printf("Файл не существует для чтения: %s\n", path);
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

void initialize_test_files(const char *test_dir) {
    for (int i = 0; i < 10; i++) {
        char path[256];
        snprintf(path, sizeof(path), "%s/file_%d", test_dir, i);
        int fd = creat(path, S_IRUSR | S_IWUSR);
        if (fd >= 0) {
            close(fd);
            printf("Инициализирован файл: %s\n", path);
        } else {
            perror("Ошибка инициализации файла");
        }
    }
}

// Очистка ресурсов
void cleanup_resources() {
    printf("Очистка ресурсов...\n");

    // Размонтирование файловой системы
    if (umount(TEST_DIR) == 0) {
        printf("Файловая система размонтирована.\n");
    } else {
        perror("Ошибка размонтирования файловой системы");
    }

    // Удаление loop-устройства
    if (system("losetup -d " LOOP_DEVICE) == 0) {
        printf("Loop-устройство удалено.\n");
    } else {
        perror("Ошибка удаления loop-устройства");
    }

    // Удаление временных файлов
    if (unlink(IMAGE_PATH) == 0) {
        printf("Тестовый образ удален.\n");
    } else {
        perror("Ошибка удаления тестового образа");
    }

    // Удаление тестовой директории
    if (rmdir(TEST_DIR) == 0) {
        printf("Тестовая директория удалена.\n");
    } else {
        perror("Ошибка удаления тестовой директории");
    }
}


int main(int argc, char **argv) {
    //cleanup_resources();
    const char *kcov_path = "/sys/kernel/debug/kcov";

    // Подготовка тестового окружения
    printf("Создание тестового образа...\n");
    if (create_test_image(IMAGE_PATH, IMAGE_SIZE_MB) != 0) {
        return 1;
    }

    printf("Настройка loop-устройства...\n");
    if (setup_loop_device(IMAGE_PATH, LOOP_DEVICE) != 0) {
        return 1;
    }

    printf("Монтирование debugfs...\n");
    mount_debugfs();

    printf("Создание директории для тестирования...\n");
    if (mkdir(TEST_DIR, 0777) != 0 && errno != EEXIST) {
        perror("Ошибка создания тестовой директории");
        return 1;
    }

    printf("Монтирование файловой системы...\n");
    if (mount(LOOP_DEVICE, TEST_DIR, "ext4", 0, NULL) != 0) {
        perror("Ошибка монтирования файловой системы");
        return 1;
    }

    printf("Инициализация kcov...\n");
    int kcov_fd = init_kcov(kcov_path);
    if (kcov_fd < 0) {
        umount(TEST_DIR);
        return 1;
    }

    // Запуск kcov в режиме трассировки
    if (ioctl(kcov_fd, KCOV_ENABLE, KCOV_TRACE_PC)) {
        perror("Ошибка запуска kcov");
        close(kcov_fd);
        umount(TEST_DIR);
        return 1;
    }

    initialize_test_files(TEST_DIR);

    printf("Запуск фаззинга...\n");
    srand(time(NULL));
    for (int i = 0; i < MAX_OPS; i++) {
        fs_operation_t op = get_random_operation();
        perform_operation(op, TEST_DIR);
    }

    // Отключение kcov
    ioctl(kcov_fd, KCOV_DISABLE);
    close(kcov_fd);

    // Размонтирование файловой системы
    //printf("Размонтирование файловой системы...\n");
    //umount(TEST_DIR);

    cleanup_resources();

    printf("Фаззинг завершен.\n");
    return 0;
}
