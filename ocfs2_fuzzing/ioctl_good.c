#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <linux/types.h>
#include <stdint.h>
#include <stdlib.h>

#define OCFS2_IOC_INFO _IOR('o', 5, struct ocfs2_info)
#define OCFS2_INFO_MAGIC 0x4F32494E
#define OCFS2_INFO_CLUSTERSIZE 1

#pragma pack(push, 1)  // Отключаем выравнивание структур
struct ocfs2_info_request {
    __u32 ir_magic;
    __u32 ir_code;
    __u32 ir_size;
    __u32 ir_flags;
};

struct ocfs2_info_clustersize {
    struct ocfs2_info_request ic_req;
    __u32 ic_clustersize;
    __u32 ic_pad;
};

struct ocfs2_info {
    __u64 oi_requests;  // Указатель на массив структур ocfs2_info_request*
    __u32 oi_count;
    __u32 oi_pad;
};
#pragma pack(pop)  // Восстанавливаем выравнивание

int main() {
    int fd = open("/mnt/shared/testfile", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // 1. Выделяем память для массива указателей на запросы
    struct ocfs2_info_request **requests = malloc(sizeof(void*));
    if (!requests) {
        perror("malloc");
        close(fd);
        return 1;
    }

    // 2. Выделяем память для самого запроса
    struct ocfs2_info_clustersize *cluster_info = malloc(sizeof(struct ocfs2_info_clustersize));
    if (!cluster_info) {
        perror("malloc");
        free(requests);
        close(fd);
        return 1;
    }

    memset(cluster_info, 0, sizeof(*cluster_info));
    
    // 3. Инициализация запроса
    cluster_info->ic_req.ir_magic = OCFS2_INFO_MAGIC;
    cluster_info->ic_req.ir_code = OCFS2_INFO_CLUSTERSIZE;
    cluster_info->ic_req.ir_size = sizeof(struct ocfs2_info_clustersize);
    cluster_info->ic_req.ir_flags = 0;

    // 4. Связываем указатели
    requests[0] = (struct ocfs2_info_request*)cluster_info;

    // 5. Подготавливаем структуру для ioctl
    struct ocfs2_info info = {
        .oi_requests = (__u64)(uintptr_t)requests,
        .oi_count = 1,
        .oi_pad = 0
    };

    printf("Размер ocfs2_info_clustersize: %zu\n", sizeof(*cluster_info));
    printf("Отправка ioctl...\n");

    if (ioctl(fd, OCFS2_IOC_INFO, &info) < 0) {
        perror("ioctl");
        free(cluster_info);
        free(requests);
        close(fd);
        return 1;
    }

    // 6. Проверяем результат
    if (cluster_info->ic_req.ir_flags & 0x40000000) {
        printf("Размер кластера: %u байт\n", cluster_info->ic_clustersize);
    } else {
        printf("Ошибка: данные не заполнены\n");
    }

    free(cluster_info);
    free(requests);
    close(fd);
    return 0;
}