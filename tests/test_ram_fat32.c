#include <assert.h>
#include <kern/ramfs.h>
#include <kern/string.h>
#include <kern/vfs.h>
#include <stdio.h>

int main(void) {
    static const char payload[] = "RAM FAT32 data remains available until reboot";
    char buffer[sizeof(payload)] = {0};
    struct vfs_dirent entry;
    int handle;

    assert(ramfs_init() == 0);
    assert(ramfs_capacity_bytes() == 64u * 1024u * 1024u);
    assert(vfs_readdir("/", 0, &entry) == 0);
    assert(vfs_open("/TEST.TXT", VFS_OPEN_CREATE, &handle) == 0);
    assert(vfs_write(handle, payload, sizeof(payload) - 1) == (int)sizeof(payload) - 1);
    assert(vfs_close(handle) == 0);

    assert(vfs_readdir("/", 0, &entry) == 1);
    assert(strcmp(entry.name, "TEST.TXT") == 0);
    assert(entry.size == sizeof(payload) - 1);
    assert(vfs_open("/TEST.TXT", 0, &handle) == 0);
    assert(vfs_read(handle, buffer, sizeof(buffer)) == (int)sizeof(payload) - 1);
    assert(strcmp(buffer, payload) == 0);
    assert(vfs_unlink("/TEST.TXT") < 0);
    assert(vfs_close(handle) == 0);

    assert(vfs_unlink("/TEST.TXT") == 0);
    assert(vfs_readdir("/", 0, &entry) == 0);

    for (unsigned index = 0; index < 17; ++index) {
        char path[] = "/F00.TXT";
        path[2] = (char)('0' + index / 10);
        path[3] = (char)('0' + index % 10);
        assert(vfs_open(path, VFS_OPEN_CREATE, &handle) == 0);
        assert(vfs_close(handle) == 0);
        assert(vfs_open(path, 0, &handle) == 0);
        assert(vfs_close(handle) == 0);
    }
    unsigned entries = 0;
    int result;
    while ((result = vfs_readdir("/", entries, &entry)) > 0) ++entries;
    assert(result == 0);
    assert(entries == 17);
    puts("RAM FAT32 tests passed");
    return 0;
}
