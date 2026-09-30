#ifndef UNITAS_KERN_VFS_H
#define UNITAS_KERN_VFS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

struct vfs_dirent;
struct vfs_stat;

/* Give callbacks a path relative to the selected mount. */
/* Use "/" for the mount root. */
/* Require absolute VFS paths shorter than 256 bytes. */
/* VFS does not normalize paths. Reject components that the file system does not support. */
/* On success, open returns a node that VFS owns until close. */
/* Read and write receive a byte offset and return a byte count or a negative error. */
struct filesystem {
    const char *name;
    int (*open)(const char *path, uint32_t flags, void *filesystem_data, void **node);
    int (*read)(void *node, uint64_t offset, void *buffer, size_t length);
    int (*write)(void *node, uint64_t offset, const void *buffer, size_t length);
    void (*close)(void *node);
    int (*unlink)(const char *path, void *filesystem_data);
    int (*stat_path)(const char *path, void *filesystem_data,
                     struct vfs_stat *result);
    int (*stat_node)(void *node, struct vfs_stat *result);
    int (*mkdir)(const char *path, uint32_t mode, void *filesystem_data);
    int (*rmdir)(const char *path, void *filesystem_data);
    int (*rename)(const char *source, const char *destination,
                  void *filesystem_data);
    int (*truncate)(void *node, uint64_t size);
    int (*sync)(void *node, bool data_only);
    /* Return 1 with a terminated name, 0 at end, or a negative error. */
    int (*readdir)(const char *path, void *filesystem_data, uint64_t index,
                   struct vfs_dirent *entry);
};

struct vfs_dirent { char name[256]; uint32_t type; uint64_t size; };
struct vfs_stat {
    uint64_t inode;
    uint64_t device;
    uint64_t special_device;
    uint64_t size;
    uint64_t blocks;
    uint64_t block_size;
    uint64_t links;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    int64_t access_seconds;
    int64_t access_nanoseconds;
    int64_t modify_seconds;
    int64_t modify_nanoseconds;
    int64_t change_seconds;
    int64_t change_nanoseconds;
};

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
/* Keep duplicate handles on the same open file and byte offset. */
int vfs_dup(int handle);
int vfs_tell(int handle, uint64_t *offset);
int vfs_set_append(int handle, bool append);
int vfs_is_append(int handle, bool *append);
/* Set the next byte offset for a file or device handle. */
int vfs_seek(int handle, uint64_t offset);
int vfs_close(int handle);
int vfs_unlink(const char *path);
int vfs_stat(const char *path, struct vfs_stat *result);
int vfs_fstat(int handle, struct vfs_stat *result);
int vfs_mkdir(const char *path, uint32_t mode);
int vfs_rmdir(const char *path);
int vfs_rename(const char *source, const char *destination);
int vfs_truncate(int handle, uint64_t size);
int vfs_sync(int handle, bool data_only);
int vfs_readdir(const char *path, uint64_t index, struct vfs_dirent *entry);

#endif
