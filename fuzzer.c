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
#include <stddef.h>
#include <stdint.h>
#include <linux/types.h>



#define KCOV_SUBSYSTEM_COMMON   (0x00ull << 56)
#define KCOV_SUBSYSTEM_USB  (0x01ull << 56)
#define KCOV_SUBSYSTEM_NFSD  (0x02ull << 56)
#define KCOV_SUBSYSTEM_LOCKD (0x03ull << 56)
#define KCOV_SUBSYSTEM_STATD (0x04ull << 56)
#define KCOV_SUBSYSTEM_MOUNTD (0x05ull << 56)

#define KCOV_INSTANCE_ID        12345
#define KCOV_SUBSYSTEM_MASK (0xffull << 56)
#define KCOV_INSTANCE_MASK  (0xffffffffull)

#define KCOV_INIT_TRACE                     _IOR('c', 1, unsigned long)
#define KCOV_ENABLE                 _IO('c', 100)
#define KCOV_DISABLE                        _IO('c', 101)
#define KCOV_REMOTE_ENABLE      _IOW('c', 102, struct kcov_remote_arg)
#define COVER_SIZE                  (64<<10)

#define KCOV_TRACE_PC  0
#define KCOV_TRACE_CMP 1
#define KCOV_COMMON_ID      0x42
#define KCOV_USB_BUS_NUM    1

#define KCOV_BUFFER_SIZE (64 * 1024) // Размер буфера для kcov
#define TEST_DIR "/mnt/nfs_client"
#define IMAGE_PATH "/tmp/test.img"
#define LOOP_DEVICE "/dev/loop99"
#define IMAGE_SIZE_MB 100
#define MAX_OPS 100

int kcov_fd = -1;
unsigned long *kcov_cover = NULL;

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

void kcov_init() {
    /* A single fd descriptor allows coverage collection on a single
     * thread.
     */
    kcov_fd = open("/sys/kernel/debug/kcov", O_RDWR);
    if (kcov_fd == -1)
            perror("open"), exit(1);
    /* Setup trace mode and trace size. */
    if (ioctl(kcov_fd, KCOV_INIT_TRACE, COVER_SIZE))
            perror("ioctl"), exit(1);
    /* Mmap buffer shared between kernel- and user-space. */
    kcov_cover = (unsigned long*)mmap(NULL, COVER_SIZE * sizeof(unsigned long),
                                 PROT_READ | PROT_WRITE, MAP_SHARED, kcov_fd, 0);
    if ((void*)kcov_cover == MAP_FAILED)
            perror("mmap"), exit(1);
    /* Enable coverage collection on the current thread. */
    if (ioctl(kcov_fd, KCOV_ENABLE, KCOV_TRACE_PC))
            perror("ioctl"), exit(1);
    /* Reset coverage from the tail of the ioctl() call. */
    __atomic_store_n(&kcov_cover[0], 0, __ATOMIC_RELAXED);
}

void kcov_close() {
    sleep(2);
    /* Read number of PCs collected. */
    int n = __atomic_load_n(&kcov_cover[0], __ATOMIC_RELAXED);

    FILE *file = fopen("output.txt", "w");
    for (int i = 0; i < n; i++) {
            printf("0x%lx\n", kcov_cover[i + 1]);
            fprintf(file, "0x%lx\n", kcov_cover[i + 1]);
    }
    /* Disable coverage collection for the current thread. After this call
     * coverage can be enabled for a different thread.
     */
    if (ioctl(kcov_fd, KCOV_DISABLE, 0))
            perror("ioctl"), exit(1);
    /* Free resources. */
    if (munmap(kcov_cover, COVER_SIZE * sizeof(unsigned long)))
            perror("munmap"), exit(1);
    if (close(kcov_fd))
            perror("close"), exit(1);
}

void kcov_init_remote() {
    struct kcov_remote_arg *arg;
    kcov_fd = open("/sys/kernel/debug/kcov", O_RDWR);
    if (kcov_fd == -1) {
        perror("open");
        exit(1);
    }

    // Init trace size
    if (ioctl(kcov_fd, KCOV_INIT_TRACE, COVER_SIZE)) {
        perror("KCOV_INIT_TRACE");
        exit(1);
    }

    kcov_cover = (unsigned long*)mmap(NULL, COVER_SIZE * sizeof(unsigned long),
                                      PROT_READ | PROT_WRITE, MAP_SHARED, kcov_fd, 0);
    if (kcov_cover == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // // Создание и заполнение структуры с 1 handle
    // size_t arg_size = sizeof(struct kcov_remote_arg) + sizeof(__u64); // 1 handle
    // arg = malloc(arg_size);
    // if (!arg) {
    //     perror("malloc");
    //     exit(1);
    // }

    /* Enable coverage collection via common handle and from USB bus #1. */
    arg = calloc(1, sizeof(*arg) + sizeof(uint64_t) * 4);
    if (!arg)
            perror("calloc"), exit(1);

    //__u64 handle = kcov_remote_handle(KCOV_SUBSYSTEM_COMMON, 0);

    arg->trace_mode = KCOV_TRACE_PC;
    arg->area_size = COVER_SIZE;
    //arg->num_handles = 4;
    arg->common_handle = kcov_remote_handle(KCOV_SUBSYSTEM_COMMON, 11111);
    // arg->handles[0] = kcov_remote_handle(KCOV_SUBSYSTEM_NFSD, 0);
	// arg->handles[1] = kcov_remote_handle(KCOV_SUBSYSTEM_NFSD, 1);
	// arg->handles[2] = kcov_remote_handle(KCOV_SUBSYSTEM_NFSD, 2);
	// arg->handles[3] = kcov_remote_handle(KCOV_SUBSYSTEM_LOCKD, 0);

    // Enable remote coverage
    if (ioctl(kcov_fd, KCOV_REMOTE_ENABLE, arg)) {
        perror("KCOV_REMOTE_ENABLE");
        free(arg);
        exit(1);
    }

    free(arg);

    // Reset buffer
    __atomic_store_n(&kcov_cover[0], 0, __ATOMIC_RELAXED);
}


int main(int argc, char **argv) {
    printf("Монтирование debugfs...\n");
    mount_debugfs();
    system("touch /etc/exports");
    system("rm /etc/exports");
    system("touch /etc/exports");

    if (system("echo -en \"127.0.0.1\tsyzkaller\n\" | sudo tee $DIR/etc/hosts")) {
        perror("echo syzkaller");
    }
    
    if(mkdir("/mnt/nfs_server", 0777) != 0 && errno != EEXIST) {
        perror("Ошибка создания экспортируемой директории для NFS");
        //return 1;
    }

    if (system("chown nobody:nogroup /mnt/nfs_server")) {
        perror("Ошибка chown для /mnt/nfs_server");
        //return 1;
    }

    if (system("chmod 777 /mnt/nfs_server")) {
        perror("Ошибка chmod для /mnt/nfs_server");
        //return 1;
    }

    if (system("echo \"/mnt/nfs_server *(rw,sync,no_subtree_check,no_root_squash)\" >> /etc/exports")) {
        perror("Ошибка заполнения /etc/exports");
        //return 1;
    }

    if (system("exportfs -a")) {
        perror("exportfs -a error");
        system("rm /etc/exports");
        return 1;
    }

    if (system("systemctl enable nfs-kernel-server")) {
        perror("Ошибка systemctl enable nfs-kernel-server");
        //return 1;
    }

    if (system("systemctl restart nfs-kernel-server")) {
        perror("Ошибка systemctl restart nfs-kernel-server");
        //return 1;
    }

    sleep(10);

    // if (system("systemctl restart nfs-kernel-server")) {
    //     perror("Ошибка systemctl restart nfs-kernel-server");
    //     return 1;
    // }


    printf("Создание директории для монтирования NFS...\n");
    if (mkdir(TEST_DIR, 0777) != 0 && errno != EEXIST) {
        perror("Ошибка создания директории для NFS");
        //return 1;
    }

    // printf("Монтирование NFS...\n");
    // if (mount("192.168.100.2:/mnt/nfs", TEST_DIR, "nfs4", 0, NULL) != 0) {
    //     perror("Ошибка монтирования NFS");
    //     return 1;
    // }

    if (system("mount -t nfs4 127.0.0.1:/mnt/nfs_server /mnt/nfs_client")) {
        perror("Ошибка монтирования NFS");
        return 1;
    }

    // Инициализация kcov (remote)
    kcov_init_remote();
    //kcov_init();

    initialize_test_files(TEST_DIR);

    printf("Запуск фаззинга...\n");
    srand(time(NULL));
    for (int i = 0; i < MAX_OPS; i++) {
        fs_operation_t op = get_random_operation();
        perform_operation(op, TEST_DIR);
    }

    sleep(5);


    kcov_close();

    // Размонтирование NFS
    if (umount(TEST_DIR) == 0) {
        printf("NFS размонтирован.\n");
    } else {
        perror("Ошибка размонтирования NFS");
    }

    if (system("rm /etc/exports")) {
        perror("Ошибка очистки /etc/exports");
    }

    printf("Фаззинг завершен.\n");
    return 0;
}



// int main(int argc, char **argv) {
//     //cleanup_resources();
//     const char *kcov_path = "/sys/kernel/debug/kcov";

//     // Подготовка тестового окружения
//     printf("Создание тестового образа...\n");
//     if (create_test_image(IMAGE_PATH, IMAGE_SIZE_MB) != 0) {
//         return 1;
//     }

//     printf("Настройка loop-устройства...\n");
//     if (setup_loop_device(IMAGE_PATH, LOOP_DEVICE) != 0) {
//         return 1;
//     }

//     printf("Монтирование debugfs...\n");
//     mount_debugfs();

//     printf("Создание директории для тестирования...\n");
//     if (mkdir(TEST_DIR, 0777) != 0 && errno != EEXIST) {
//         perror("Ошибка создания тестовой директории");
//         return 1;
//     }

//     printf("Монтирование файловой системы...\n");
//     if (mount(LOOP_DEVICE, TEST_DIR, "ext4", 0, NULL) != 0) {
//         perror("Ошибка монтирования файловой системы");
//         return 1;
//     }

//     // printf("Инициализация kcov...\n");
//     // int kcov_fd = init_kcov(kcov_path);
//     // if (kcov_fd < 0) {
//     //     umount(TEST_DIR);
//     //     return 1;
//     // }

//     // // Запуск kcov в режиме трассировки
//     // if (ioctl(kcov_fd, KCOV_ENABLE, KCOV_TRACE_PC)) {
//     //     perror("Ошибка запуска kcov");
//     //     close(kcov_fd);
//     //     umount(TEST_DIR);
//     //     return 1;
//     // }

    

//     initialize_test_files(TEST_DIR);

//     //kcov_init();
//     kcov_init_remote();


//     printf("Запуск фаззинга...\n");
//     srand(time(NULL));
//     for (int i = 0; i < MAX_OPS; i++) {
//         fs_operation_t op = get_random_operation();
//         perform_operation(op, TEST_DIR);
//     }

//     kcov_close();

//     // // Отключение kcov
//     // ioctl(kcov_fd, KCOV_DISABLE);
//     // close(kcov_fd);

//     // Размонтирование файловой системы
//     //printf("Размонтирование файловой системы...\n");
//     //umount(TEST_DIR);

//     cleanup_resources();

//     printf("Фаззинг завершен.\n");
//     return 0;
// }
