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
#define TEST_DIR "/mnt/shared"
#define IMAGE_PATH "/tmp/test.img"
#define LOOP_DEVICE "/dev/loop99"
#define IMAGE_SIZE_MB 400
#define MAX_OPS 100

int kcov_fd = -1;
unsigned long *kcov_cover = NULL;

// Типы операций для фаззинга
typedef enum { OP_CREATE, OP_DELETE, OP_READ, OP_WRITE } fs_operation_t;

// Создание тестового образа OCFS2
int create_ocfs2_image(const char *path, int size_mb) {
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

  char cmd[512];
  snprintf(cmd, sizeof(cmd),
           "mkfs.ocfs2 -F -C 4K --cluster-stack=pcmk --cluster-name=mycluster "
           "--fs-features=sparse %s",
           path);
  printf("Выполнение команды: %s\n", cmd);

  if (system(cmd) != 0) {
    fprintf(stderr, "Ошибка форматирования образа OCFS2\n");
    return -1;
  }
  return 0;
}

// Настройка loop-устройства
int setup_loop_device(const char *image_path, const char *loop_device) {
  char cmd[256];
  snprintf(cmd, sizeof(cmd), "losetup %s %s", loop_device, image_path);
  printf("Выволнение команды: %s\n", cmd);
  if (system(cmd) != 0) {
    fprintf(stderr, "Ошибка настройки loop-устройства\n");
    return -1;
  }
  return 0;
}

//
int steup_file_of_image() {
  char cmd[256];
  snprintf(cmd, sizeof(cmd), "dd if=/dev/zero of=%s bs=1M count=100",
           IMAGE_PATH);
  printf("Выволнение команды: %s\n", cmd);
  if (system(cmd) != 0) {
    fprintf(stderr, "Ошибка создания файла образа\n");
    return -1;
  }
  return 0;
}

// Настройка и запуск o2cb
int setup_o2cb() {
  // Настройка кластера (предварительно необходимо настроить конфигурационные
  // файлы) В реальном сценарии необходимо настроить /etc/ocfs2/cluster.conf Для
  // простоты будем считать, что кластер уже настроен

  // Запуск службы o2cb
  if (system("service o2cb restart") != 0) {
    fprintf(stderr, "Ошибка запуска o2cb\n");
    return -1;
  }
  return 0;
}

// Монтирование debugfs
void mount_debugfs() {
  if (mount("none", "/sys/kernel/debug", "debugfs", 0, NULL) != 0) {
    if (errno != EBUSY) {
      perror("Ошибка монтирования debugfs");
      exit(1);
    }
  }
}

// Инициализация kcov
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

  kcov_cover =
      (unsigned long *)mmap(NULL, COVER_SIZE * sizeof(unsigned long),
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

// Случайный выбор операции
fs_operation_t get_random_operation() { return rand() % 4; }

int file_exists(const char *path) { return access(path, F_OK) == 0; }

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

// Очистка ресурсов
void cleanup_resources() {
  printf("Очистка ресурсов...\n");

  if (umount(TEST_DIR) == 0) {
    printf("Файловая система размонтирована.\n");
  } else {
    perror("Ошибка размонтирования файловой системы");
  }

  if (system("losetup -d " LOOP_DEVICE) == 0) {
    printf("Loop-устройство удалено.\n");
  } else {
    perror("Ошибка удаления loop-устройства");
  }

  if (unlink(IMAGE_PATH) == 0) {
    printf("Тестовый образ удален.\n");
  } else {
    perror("Ошибка удаления тестового образа");
  }

  if (rmdir(TEST_DIR) == 0) {
    printf("Тестовая директория удалена.\n");
  } else {
    perror("Ошибка удаления тестовой директории");
  }
}

int main() {
  return 0;
  printf("Подготовка OCFS2 к фаззингу...\n");
  mount_debugfs();

  if(system("sudo service o2cb stop") != 0) {
    printf("sudo service o2cb stop failed.\n");
  }

  if(system("sudo service o2cb start") != 0) {
    printf("sudo service o2cb start failed.\n");
  }


  if (system("dd if=/dev/zero of=/tmp/test.img bs=1M count=300") != 0) {
    printf("Ошибка создания образа файловой системы\n");
    return 1;
  }

  if (system("losetup /dev/loop99 /tmp/test.img") != 0) {
    printf("Ошибка создания loop устройства\n");
    if (unlink(IMAGE_PATH) == 0) {
      printf("Тестовый образ удален.\n");
    } else {
      perror("Ошибка удаления тестового образа");
    }
    return 1;
  }

  if (system("mkfs.ocfs2 -F -C 4K --cluster-stack=o2cb --cluster-name=ocfs2 /dev/loop99")) {
    printf("Ошибка форматирования файловой системы ocfs2\n");
    if (system("losetup -d " LOOP_DEVICE) == 0) {
      printf("Loop-устройство удалено.\n");
    } else {
      perror("Ошибка удаления loop-устройства");
    }

    if (unlink(IMAGE_PATH) == 0) {
      printf("Тестовый образ удален.\n");
    } else {
      perror("Ошибка удаления тестового образа");
    }
    return 1;
  }

  printf("Создание директории для тестирования...\n");
  if (mkdir(TEST_DIR, 0777) != 0 && errno != EEXIST) {
    perror("Ошибка создания тестовой директории");
    if (system("losetup -d " LOOP_DEVICE) == 0) {
      printf("Loop-устройство удалено.\n");
    } else {
      perror("Ошибка удаления loop-устройства");
    }

    if (unlink(IMAGE_PATH) == 0) {
      printf("Тестовый образ удален.\n");
    } else {
      perror("Ошибка удаления тестового образа");
    }
    return 1;
  }

  printf("Инициализация kcov...\n");
  kcov_init();

  printf("Монтирование файловой системы OCFS2...\n");
  if (system("sudo mount -t ocfs2 -o cluster_stack=o2cb,cluster_name=ocfs2 /dev/loop99 /mnt/test") != 0) {
    perror("Ошибка монтирования файловой системы OCFS2");
    if (system("losetup -d " LOOP_DEVICE) == 0) {
      printf("Loop-устройство удалено.\n");
    } else {
      perror("Ошибка удаления loop-устройства");
    }

    if (unlink(IMAGE_PATH) == 0) {
      printf("Тестовый образ удален.\n");
    } else {
      perror("Ошибка удаления тестового образа");
    }
    return 1;
  }

  printf("Запуск фаззинга...\n");
  srand(time(NULL));
  for (int i = 0; i < 1; i++) {
    system("touch /mnt/test/file1");
    system("echo \"Hello OCFS2\" > /mnt/test/file1");
    system("cat /mnt/test/file1");
    system("rm /mnt/test/file1");
  }

  printf("Отключение kcov...\n");
  kcov_close();

  // cleanup_resources();
  if (system("losetup -d " LOOP_DEVICE) == 0) {
    printf("Loop-устройство удалено.\n");
  } else {
    perror("Ошибка удаления loop-устройства");
  }

  if (unlink(IMAGE_PATH) == 0) {
    printf("Тестовый образ удален.\n");
  } else {
    perror("Ошибка удаления тестового образа");
  }

  if (umount("/mnt/test") != 0) {
    perror("Ошибка размонтирования /mnt/tmp. \n");
  } 
  //sudo service o2cb stop
  printf("Фаззинг завершен.\n");

  return 0;
}
