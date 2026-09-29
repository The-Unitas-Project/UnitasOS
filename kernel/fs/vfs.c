#include <kern/string.h>
#include <kern/vfs.h>
#include <limits.h>

#define VFS_MAX_FILESYSTEMS 16
#define VFS_MAX_MOUNTS 32
#define VFS_MAX_HANDLES 64
#define VFS_PATH_MAX 256

struct mount {
    char path[VFS_PATH_MAX];
    const struct filesystem *filesystem;
    void *data;
};
struct open_file {
    bool used;
    const struct filesystem *filesystem;
    void *node;
    uint64_t offset;
};

static const struct filesystem *filesystems[VFS_MAX_FILESYSTEMS];
static struct mount mounts[VFS_MAX_MOUNTS];
static struct open_file handles[VFS_MAX_HANDLES];
static size_t filesystem_count, mount_count;

static bool valid_path(const char *path) {
    return path && path[0] == '/' && strlen(path) < VFS_PATH_MAX;
}

static bool valid_mountpoint(const char *path) {
    if (!valid_path(path)) return false;
    size_t length = strlen(path);
    return length == 1 || path[length - 1] != '/';
}

int vfs_register(const struct filesystem *filesystem) {
    if (!filesystem || !filesystem->name || filesystem_count == VFS_MAX_FILESYSTEMS)
        return -1;
    for (size_t i = 0; i < filesystem_count; ++i)
        if (filesystems[i] == filesystem || strcmp(filesystems[i]->name, filesystem->name) == 0)
            return -1;
    filesystems[filesystem_count++] = filesystem;
    return 0;
}

int vfs_mount(const char *mountpoint, const struct filesystem *filesystem,
              void *filesystem_data) {
    if (!valid_mountpoint(mountpoint) || !filesystem || mount_count == VFS_MAX_MOUNTS)
        return -1;
    bool registered = false;
    for (size_t i = 0; i < filesystem_count; ++i)
        if (filesystems[i] == filesystem) registered = true;
    if (!registered) return -1;
    for (size_t i = 0; i < mount_count; ++i)
        if (strcmp(mounts[i].path, mountpoint) == 0) return -1;
    struct mount *mount = &mounts[mount_count++];
    size_t length = strlen(mountpoint);
    memcpy(mount->path, mountpoint, length + 1);
    mount->filesystem = filesystem;
    mount->data = filesystem_data;
    return 0;
}

static struct mount *find_mount(const char *path) {
    struct mount *best = 0;
    size_t path_length = strlen(path);
    size_t best_length = 0;
    for (size_t i = 0; i < mount_count; ++i) {
        size_t length = strlen(mounts[i].path);
        if (length < best_length || path_length < length ||
            memcmp(path, mounts[i].path, length) != 0) continue;
        if (length > 1 && path_length > length && path[length] != '/') continue;
        best = &mounts[i];
        best_length = length;
    }
    return best;
}

int vfs_open(const char *path, uint32_t flags, int *handle) {
    if (!valid_path(path) || !handle ||
        (flags & ~(VFS_OPEN_CREATE | VFS_OPEN_TRUNCATE))) return -1;
    struct mount *mount = find_mount(path);
    if (!mount || !mount->filesystem->open) return -1;
    size_t prefix_length = strlen(mount->path);
    const char *relative = prefix_length == 1 ? path : path + prefix_length;
    if (!*relative) relative = "/";
    else if (*relative != '/') return -1;
    size_t slot;
    for (slot = 0; slot < VFS_MAX_HANDLES && handles[slot].used; ++slot) {}
    if (slot == VFS_MAX_HANDLES) return -1;
    void *node = 0;
    int result = mount->filesystem->open(relative, flags, mount->data, &node);
    if (result != 0) return result < 0 ? result : -1;
    handles[slot] = (struct open_file){ true, mount->filesystem, node, 0 };
    *handle = (int)slot;
    return 0;
}

static struct open_file *get_handle(int handle) {
    if (handle < 0 || handle >= VFS_MAX_HANDLES || !handles[handle].used) return 0;
    return &handles[handle];
}

int vfs_read(int handle, void *buffer, size_t length) {
    struct open_file *file = get_handle(handle);
    if (!file || !file->filesystem->read || (!buffer && length) || length > INT_MAX)
        return -1;
    int result = file->filesystem->read(file->node, file->offset, buffer, length);
    if (result > 0) {
        if ((size_t)result > length || (uint64_t)result > UINT64_MAX - file->offset)
            return -1;
        file->offset += (uint64_t)result;
    }
    return result;
}

int vfs_write(int handle, const void *buffer, size_t length) {
    struct open_file *file = get_handle(handle);
    if (!file || !file->filesystem->write || (!buffer && length) || length > INT_MAX)
        return -1;
    int result = file->filesystem->write(file->node, file->offset, buffer, length);
    if (result > 0) {
        if ((size_t)result > length || (uint64_t)result > UINT64_MAX - file->offset)
            return -1;
        file->offset += (uint64_t)result;
    }
    return result;
}

int vfs_seek(int handle, uint64_t offset) {
    struct open_file *file = get_handle(handle);
    if (!file) return -1;
    file->offset = offset;
    return 0;
}

int vfs_close(int handle) {
    struct open_file *file = get_handle(handle);
    if (!file) return -1;
    if (file->filesystem->close) file->filesystem->close(file->node);
    *file = (struct open_file){0};
    return 0;
}

int vfs_unlink(const char *path) {
    if (!valid_path(path)) return -1;
    struct mount *mount = find_mount(path);
    if (!mount || !mount->filesystem->unlink) return -1;
    const char *relative = strcmp(mount->path, "/") == 0 ? path : path + strlen(mount->path);
    if (!*relative) relative = "/";
    return mount->filesystem->unlink(relative, mount->data);
}

int vfs_readdir(const char *path, uint64_t index, struct vfs_dirent *entry) {
    if (!valid_path(path) || !entry) return -1;
    struct mount *mount = find_mount(path);
    if (!mount || !mount->filesystem->readdir) return -1;
    const char *relative = strcmp(mount->path, "/") == 0 ? path : path + strlen(mount->path);
    if (!*relative) relative = "/";
    struct vfs_dirent result_entry;
    int result = mount->filesystem->readdir(relative, mount->data, index, &result_entry);
    if (result <= 0) return result;
    if (result != 1) return -1;
    for (size_t i = 0; i < sizeof(result_entry.name); ++i) {
        if (!result_entry.name[i]) {
            *entry = result_entry;
            return 1;
        }
    }
    return -1;
}
