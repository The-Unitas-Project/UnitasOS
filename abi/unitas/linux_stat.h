#ifndef UNITAS_ABI_LINUX_STAT_H
#define UNITAS_ABI_LINUX_STAT_H

#include <stdint.h>

struct unitas_linux_stat {
    uint64_t device;
    uint64_t inode;
    uint64_t links;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    int32_t padding;
    uint64_t special_device;
    int64_t size;
    int64_t block_size;
    int64_t blocks;
    int64_t access_seconds;
    int64_t access_nanoseconds;
    int64_t modify_seconds;
    int64_t modify_nanoseconds;
    int64_t change_seconds;
    int64_t change_nanoseconds;
    int64_t reserved[3];
};

_Static_assert(sizeof(struct unitas_linux_stat) == 144,
               "Linux x86-64 stat size");

#endif
