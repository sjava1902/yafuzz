#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <linux/types.h>
#include <stdlib.h>
#include <stdint.h>

#define OCFS2_IOC_INFO _IOR('o', 5, struct ocfs2_info)
#define OCFS2_INFO_MAGIC 0x4F32494E

// Коды запросов
enum {
    OCFS2_INFO_CLUSTERSIZE = 1,
    OCFS2_INFO_BLOCKSIZE,
    OCFS2_INFO_MAXSLOTS,
    OCFS2_INFO_LABEL,
    OCFS2_INFO_UUID,
    OCFS2_INFO_FS_FEATURES,
    OCFS2_INFO_JOURNAL_SIZE,
    OCFS2_INFO_FREEINODE,
    OCFS2_INFO_FREEFRAG,
    OCFS2_INFO_NUM_TYPES
};

// Определения структур (точные копии из ядра)
#pragma pack(push, 1)
struct ocfs2_info_request {
    __u32 ir_magic;
    __u32 ir_code;
    __u32 ir_size;
    __u32 ir_flags;
};

struct ocfs2_info {
    __u64 oi_requests;
    __u32 oi_count;
    __u32 oi_pad;
};

struct ocfs2_info_clustersize {
    struct ocfs2_info_request ic_req;
    __u32 ic_clustersize;
    __u32 ic_pad;
};

struct ocfs2_info_blocksize {
    struct ocfs2_info_request ib_req;
    __u32 ib_blocksize;
    __u32 ib_pad;
};

struct ocfs2_info_maxslots {
    struct ocfs2_info_request im_req;
    __u32 im_max_slots;
    __u32 im_pad;
};

#define OCFS2_MAX_VOL_LABEL_LEN 64
struct ocfs2_info_label {
    struct ocfs2_info_request il_req;
    __u8 il_label[OCFS2_MAX_VOL_LABEL_LEN];
};

#define OCFS2_TEXT_UUID_LEN 32
struct ocfs2_info_uuid {
    struct ocfs2_info_request iu_req;
    __u8 iu_uuid_str[OCFS2_TEXT_UUID_LEN + 1];
};

struct ocfs2_info_fs_features {
    struct ocfs2_info_request if_req;
    __u32 if_compat_features;
    __u32 if_incompat_features;
    __u32 if_ro_compat_features;
    __u32 if_pad;
};

struct ocfs2_info_journal_size {
    struct ocfs2_info_request ij_req;
    __u64 ij_journal_size;
};

#define OCFS2_MAX_SLOTS 255
struct ocfs2_info_freeinode {
    struct ocfs2_info_request ifi_req;
    struct {
        __u64 lfi_total;
        __u64 lfi_free;
    } ifi_stat[OCFS2_MAX_SLOTS];
    __u32 ifi_slotnum;
    __u32 ifi_pad;
};

#define OCFS2_INFO_MAX_HIST 32
struct ocfs2_info_freefrag {
    struct ocfs2_info_request iff_req;
    struct {
        struct {
            __u32 fc_chunks[OCFS2_INFO_MAX_HIST];
            __u32 fc_clusters[OCFS2_INFO_MAX_HIST];
        } ffs_fc_hist;
        __u32 ffs_clusters;
        __u32 ffs_free_clusters;
        __u32 ffs_free_chunks;
        __u32 ffs_free_chunks_real;
        __u32 ffs_min;
        __u32 ffs_max;
        __u32 ffs_avg;
        __u32 ffs_pad;
    } iff_ffs;
    __u32 iff_chunksize;
    __u32 iff_pad;
};
#pragma pack(pop)

void init_request(struct ocfs2_info_request *req, int code, size_t size) {
    req->ir_magic = OCFS2_INFO_MAGIC;
    req->ir_code = code;
    req->ir_size = size;
    req->ir_flags = 0;
}

int main() {
    int fd = open("/mnt/shared/testfile", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // Выделяем память для всех структур запросов
    struct ocfs2_info_clustersize *clustersize = malloc(sizeof(*clustersize));
    struct ocfs2_info_blocksize *blocksize = malloc(sizeof(*blocksize));
    struct ocfs2_info_maxslots *maxslots = malloc(sizeof(*maxslots));
    struct ocfs2_info_label *label = malloc(sizeof(*label));
    struct ocfs2_info_uuid *uuid = malloc(sizeof(*uuid));
    struct ocfs2_info_fs_features *features = malloc(sizeof(*features));
    struct ocfs2_info_journal_size *journal = malloc(sizeof(*journal));
    struct ocfs2_info_freeinode *freeinode = malloc(sizeof(*freeinode));
    struct ocfs2_info_freefrag *freefrag = malloc(sizeof(*freefrag));

    // Инициализация запросов
    memset(clustersize, 0, sizeof(*clustersize));
    init_request(&clustersize->ic_req, OCFS2_INFO_CLUSTERSIZE, sizeof(*clustersize));

    memset(blocksize, 0, sizeof(*blocksize));
    init_request(&blocksize->ib_req, OCFS2_INFO_BLOCKSIZE, sizeof(*blocksize));

    memset(maxslots, 0, sizeof(*maxslots));
    init_request(&maxslots->im_req, OCFS2_INFO_MAXSLOTS, sizeof(*maxslots));

    memset(label, 0, sizeof(*label));
    init_request(&label->il_req, OCFS2_INFO_LABEL, sizeof(*label));

    memset(uuid, 0, sizeof(*uuid));
    init_request(&uuid->iu_req, OCFS2_INFO_UUID, sizeof(*uuid));

    memset(features, 0, sizeof(*features));
    init_request(&features->if_req, OCFS2_INFO_FS_FEATURES, sizeof(*features));

    memset(journal, 0, sizeof(*journal));
    init_request(&journal->ij_req, OCFS2_INFO_JOURNAL_SIZE, sizeof(*journal));

    memset(freeinode, 0, sizeof(*freeinode));
    init_request(&freeinode->ifi_req, OCFS2_INFO_FREEINODE, sizeof(*freeinode));

    memset(freefrag, 0, sizeof(*freefrag));
    init_request(&freefrag->iff_req, OCFS2_INFO_FREEFRAG, sizeof(*freefrag));
    freefrag->iff_chunksize = 1; // Пример значения для фрагмента

    // Создаем массив указателей на запросы
    struct ocfs2_info_request *requests[] = {
        (struct ocfs2_info_request *)clustersize,
        (struct ocfs2_info_request *)blocksize,
        (struct ocfs2_info_request *)maxslots,
        (struct ocfs2_info_request *)label,
        (struct ocfs2_info_request *)uuid,
        (struct ocfs2_info_request *)features,
        (struct ocfs2_info_request *)journal,
        (struct ocfs2_info_request *)freeinode,
        (struct ocfs2_info_request *)freefrag
    };

    const int num_requests = sizeof(requests)/sizeof(requests[0]);

    // Подготавливаем структуру для ioctl
    struct ocfs2_info info = {
        .oi_requests = (__u64)(uintptr_t)requests,
        .oi_count = num_requests,
        .oi_pad = 0
    };

    printf("Отправка %d запросов через ioctl...\n", num_requests);

    if (ioctl(fd, OCFS2_IOC_INFO, &info) < 0) {
        perror("ioctl");
        goto cleanup;
    }

    // Выводим результаты
    for (int i = 0; i < num_requests; i++) {
        struct ocfs2_info_request *req = requests[i];
        
        if (!(req->ir_flags & 0x40000000)) {
            printf("[%d] Ошибка: данные не заполнены\n", req->ir_code);
            continue;
        }

        switch (req->ir_code) {
            case OCFS2_INFO_CLUSTERSIZE:
                printf("Размер кластера: %u байт\n", 
                    ((struct ocfs2_info_clustersize *)req)->ic_clustersize);
                break;
                
            case OCFS2_INFO_BLOCKSIZE:
                printf("Размер блока: %u байт\n", 
                    ((struct ocfs2_info_blocksize *)req)->ib_blocksize);
                break;
                
            case OCFS2_INFO_MAXSLOTS:
                printf("Максимальное количество слотов: %u\n", 
                    ((struct ocfs2_info_maxslots *)req)->im_max_slots);
                break;
                
            case OCFS2_INFO_LABEL:
                printf("Метка тома: '%.*s'\n", 
                    OCFS2_MAX_VOL_LABEL_LEN,
                    ((struct ocfs2_info_label *)req)->il_label);
                break;
                
            case OCFS2_INFO_UUID:
                printf("UUID: %s\n", 
                    ((struct ocfs2_info_uuid *)req)->iu_uuid_str);
                break;
                
            case OCFS2_INFO_FS_FEATURES:
                printf("Флаги совместимости: 0x%x\n"
                       "Флаги несовместимости: 0x%x\n"
                       "RO-флаги: 0x%x\n",
                    ((struct ocfs2_info_fs_features *)req)->if_compat_features,
                    ((struct ocfs2_info_fs_features *)req)->if_incompat_features,
                    ((struct ocfs2_info_fs_features *)req)->if_ro_compat_features);
                break;
                
            case OCFS2_INFO_JOURNAL_SIZE:
                printf("Размер журнала: %lu байт\n", 
                    ((struct ocfs2_info_journal_size *)req)->ij_journal_size);
                break;
                
            case OCFS2_INFO_FREEINODE: {
                struct ocfs2_info_freeinode *fi = (struct ocfs2_info_freeinode *)req;
                printf("Свободные inode (слот %u):\n", fi->ifi_slotnum);
                for (int j = 0; j < fi->ifi_slotnum; j++) {
                    printf("  Слот %d: %lu/%lu\n", j,
                        fi->ifi_stat[j].lfi_free,
                        fi->ifi_stat[j].lfi_total);
                }
                break;
            }
                
            case OCFS2_INFO_FREEFRAG: {
                struct ocfs2_info_freefrag *ff = (struct ocfs2_info_freefrag *)req;
                printf("Фрагментация (размер фрагмента %u кластеров):\n"
                       "  Всего кластеров: %u\n"
                       "  Свободных кластеров: %u\n",
                    ff->iff_chunksize,
                    ff->iff_ffs.ffs_clusters,
                    ff->iff_ffs.ffs_free_clusters);
                break;
            }
                
            default:
                printf("Неизвестный тип запроса: %d\n", req->ir_code);
        }
    }

cleanup:
    free(clustersize);
    free(blocksize);
    free(maxslots);
    free(label);
    free(uuid);
    free(features);
    free(journal);
    free(freeinode);
    free(freefrag);
    close(fd);
    return 0;
}