#ifndef UNITAS_KERN_VFS_H
#define UNITAS_KERN_VFS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

struct vfs_dirent;

/* Filesystems implement these callbacks while VFS owns path routing and handles. */
struct filesystem {
    const char *name;
    int (*open)(const char *path, uint32_t flags, void *filesystem_data, void **node);
    int (*read)(void *node, uint64_t offset, void *buffer, size_t length);
    int (*write)(void *node, uint64_t offset, const void *buffer, size_t length);
    void (*close)(void *node);
    int (*unlink)(const char *path, void *filesystem_data);
    int (*readdir)(const char *path, void *filesystem_data, uint64_t index,
                   struct vfs_dirent *entry);
};

/* Callback results use byte counts on success and negative values on failure. */
struct vfs_dirent { char name[256]; uint32_t type; uint64_t size; };

#define VFS_OPEN_CREATE 0x01u
#define VFS_OPEN_TRUNCATE 0x02u

int vfs_register(const struct filesystem *filesystem);
int vfs_mount(const char *mountpoint, const struct filesystem *filesystem,
              void *filesystem_data);
int vfs_open(const char *path, uint32_t flags, int *handle);
int vfs_read(int handle, void *buffer, size_t length);
int vfs_write(int handle, const void *buffer, size_t length);
int vfs_close(int handle);
int vfs_unlink(const char *path);
int vfs_readdir(const char *path, uint64_t index, struct vfs_dirent *entry);

#endif
