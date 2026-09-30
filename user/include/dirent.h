#ifndef UNITAS_USER_DIRENT_H
#define UNITAS_USER_DIRENT_H

#include <stddef.h>
#include <stdint.h>

struct dirent {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[256];
};

typedef struct {
    int descriptor;
    size_t offset;
    size_t length;
    unsigned char buffer[4096];
} DIR;

/* readdir returns storage owned by DIR. A later read can replace its contents. */
DIR *opendir(const char *path);
struct dirent *readdir(DIR *directory);
int closedir(DIR *directory);
void rewinddir(DIR *directory);

#endif
