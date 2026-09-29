#include <assert.h>
#include <kern/string.h>
#include <kern/vfs.h>
#include <stdio.h>

struct test_volume {
    const char *expected_path;
    const char *contents;
    size_t length;
};

static int test_open(const char *path, uint32_t flags, void *data, void **node) {
    (void)flags;
    struct test_volume *volume = data;
    if (strcmp(path, volume->expected_path) != 0) return -2;
    *node = volume;
    return 0;
}

static int test_read(void *node, uint64_t offset, void *buffer, size_t length) {
    struct test_volume *volume = node;
    if (offset >= volume->length) return 0;
    size_t available = volume->length - (size_t)offset;
    if (length > available) length = available;
    memcpy(buffer, volume->contents + offset, length);
    return (int)length;
}

int main(void) {
    struct test_volume root = { "/notes", "root-data", 9 };
    struct test_volume device = { "/console", "tty", 3 };
    const struct filesystem filesystem = {
        .name = "testfs", .open = test_open, .read = test_read
    };
    int root_handle, device_handle;
    char buffer[16] = {0};

    assert(vfs_register(&filesystem) == 0);
    assert(vfs_register(&filesystem) < 0);
    assert(vfs_mount("/", &filesystem, &root) == 0);
    assert(vfs_mount("/dev", &filesystem, &device) == 0);
    assert(vfs_mount("/dev", &filesystem, &device) < 0);

    /* Longest-prefix routing must select /dev instead of the root mount. */
    assert(vfs_open("/dev/console", 0, &device_handle) == 0);
    assert(vfs_read(device_handle, buffer, sizeof(buffer)) == 3);
    assert(memcmp(buffer, "tty", 3) == 0);
    assert(vfs_close(device_handle) == 0);

    assert(vfs_open("/notes", 0, &root_handle) == 0);
    assert(vfs_read(root_handle, buffer, 4) == 4);
    assert(memcmp(buffer, "root", 4) == 0);
    assert(vfs_read(root_handle, buffer, sizeof(buffer)) == 5);
    assert(memcmp(buffer, "-data", 5) == 0);
    assert(vfs_close(root_handle) == 0);
    assert(vfs_read(root_handle, buffer, 1) < 0);
    puts("VFS tests passed");
    return 0;
}
