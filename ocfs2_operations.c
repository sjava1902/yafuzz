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

int kcov_fd = -1;
unsigned long *kcov_cover = NULL;
const char *kcov_path = "/sys/kernel/debug/kcov";

#define KCOV_INIT_TRACE _IOR('c', 1, unsigned long)
#define KCOV_ENABLE _IO('c', 100)
#define KCOV_REMOTE_ENABLE _IOW('c', 102, unsigned long)
#define KCOV_DISABLE _IO('c', 101)
#define COVER_SIZE (64 << 10)

#define KCOV_TRACE_PC 0
#define KCOV_TRACE_CMP 1

#define KCOV_BUFFER_SIZE (64 * 1024)
#define TEST_DIR "/mnt/shared"
#define NFS_DIR "/mnt/nfs"

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

void kcov_init_remote(uint64_t remote_id) {
  kcov_fd = open(kcov_path, O_RDWR);
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

  // 💥 Передаём ID для удалённого покрытия
  if (ioctl(kcov_fd, KCOV_REMOTE_ENABLE, remote_id)) {
    perror("Ошибка включения kcov remote");
    munmap(kcov_cover, COVER_SIZE * sizeof(unsigned long));
    close(kcov_fd);
    exit(1);
  }

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

int main() {
    uint64_t remote_id = 12345;  // любой уникальный ID
    kcov_init_remote(remote_id);
    kcov_init();

    // Открытие файла
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "%s/test_file.txt", TEST_DIR);

    int fd = open(file_path, O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        perror("Ошибка открытия файла");
        kcov_close();
        return 1;
    }
    printf("Файл %s успешно открыт\n", file_path);

    // Запись в файл
    const char *data = "Тестовые данные для записи в файл\n";
    if (write(fd, data, strlen(data)) < 0) {
        perror("Ошибка записи в файл");
        close(fd);
        kcov_close();
        return 1;
    }
    printf("Данные успешно записаны в файл\n");

    // Закрытие файла
    if (close(fd) < 0) {
        perror("Ошибка закрытия файла");
        kcov_close();
        return 1;
    }
    printf("Файл успешно закрыт\n");

    kcov_close();
}