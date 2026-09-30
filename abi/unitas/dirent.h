#ifndef UNITAS_ABI_DIRENT_H
#define UNITAS_ABI_DIRENT_H

#include <stdint.h>

struct unitas_dirent64 {
    uint64_t inode;
    int64_t next_offset;
    uint16_t record_length;
    uint8_t type;
    char name[];
} __attribute__((packed));

#define UNITAS_DT_UNKNOWN 0
#define UNITAS_DT_DIRECTORY 4
#define UNITAS_DT_REGULAR 8

#endif
