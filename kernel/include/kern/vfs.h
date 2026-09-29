#ifndef UNITAS_KERN_VFS_H
#define UNITAS_KERN_VFS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

struct vfs_dirent;

/*
 * Give callbacks a path that is relative to the selected mount. Use "/" for
 * the mount root. Require absolute paths shorter than 256 bytes at the VFS API.
 * The VFS does not normalize paths. Reject path components that a file system
 * does not support. On success, open returns 0 and a node. The VFS holds the
 * node until close. Read and write receive a byte offset for the open handle.
 * They return the byte count or a negative error.
 */
struct filesystem {
    const char *name;
    int (*open)(const char *path, uint32_t flags, void *filesystem_data, void **node);
    int (*read)(void *node, uint64_t offset, void *buffer, size_t length);
    int (*write)(void *node, uint64_t offset, const void *buffer, size_t length);
    void (*close)(void *node);
    int (*unlink)(const char *path, void *filesystem_data);
    /* Return 1 with a terminated name, 0 at end, or a negative error. */
    int (*readdir)(const char *path, void *filesystem_data, uint64_t index,
                   struct vfs_dirent *entry);
};

struct vfs_dirent { char name[256]; uint32_t type; uint64_t size; };

#define VFS_OPEN_CREATE 0x01u
#define VFS_OPEN_TRUNCATE 0x02u

/* Keep this descriptor valid after registration. */
int vfs_register(const struct filesystem *filesystem);
/* Keep filesystem_data valid while the mount is active. */
int vfs_mount(const char *mountpoint, const struct filesystem *filesystem,
              void *filesystem_data);
/* open accepts only the flags defined above. */
int vfs_open(const char *path, uint32_t flags, int *handle);
int vfs_read(int handle, void *buffer, size_t length);
int vfs_write(int handle, const void *buffer, size_t length);
/* Set the next byte offset for a file or device handle. */
int vfs_seek(int handle, uint64_t offset);
int vfs_close(int handle);
int vfs_unlink(const char *path);
int vfs_readdir(const char *path, uint64_t index, struct vfs_dirent *entry);

#endif
