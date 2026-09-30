#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

DIR *opendir(const char *path) {
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return 0;
    DIR *directory = malloc(sizeof(*directory));
    if (!directory) {
        (void)close(descriptor);
        return 0;
    }
    directory->descriptor = descriptor;
    directory->offset = 0;
    directory->length = 0;
    return directory;
}

struct dirent *readdir(DIR *directory) {
    if (!directory) {
        errno = EINVAL;
        return 0;
    }
    if (directory->offset >= directory->length) {
        ssize_t amount = getdents64(directory->descriptor, directory->buffer,
                                   sizeof(directory->buffer));
        if (amount <= 0) return 0;
        directory->offset = 0;
        directory->length = (size_t)amount;
    }
    struct dirent *entry = (void *)(directory->buffer + directory->offset);
    size_t minimum = offsetof(struct dirent, d_name) + 1;
    if (entry->d_reclen < minimum || entry->d_reclen >
        directory->length - directory->offset) {
        errno = EINVAL;
        return 0;
    }
    directory->offset += entry->d_reclen;
    return entry;
}

int closedir(DIR *directory) {
    if (!directory) {
        errno = EINVAL;
        return -1;
    }
    int result = close(directory->descriptor);
    free(directory);
    return result;
}

void rewinddir(DIR *directory) {
    if (!directory) {
        errno = EINVAL;
        return;
    }
    if (lseek(directory->descriptor, 0, SEEK_SET) == 0) {
        directory->offset = 0;
        directory->length = 0;
    }
}
