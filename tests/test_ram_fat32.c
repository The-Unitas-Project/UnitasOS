#include <assert.h>
#include <kern/ramfs.h>
#include <kern/string.h>
#include <kern/vfs.h>
#include <stdio.h>

static uint64_t count_root_entries(void) {
    struct vfs_dirent entry;
    uint64_t count = 0;
    while (vfs_readdir("/", count, &entry) == 1) ++count;
    return count;
}

static int find_root_entry(const char *name, struct vfs_dirent *output) {
    struct vfs_dirent entry;
    for (uint64_t index = 0; vfs_readdir("/", index, &entry) == 1; ++index) {
        if (strcmp(entry.name, name) == 0) {
            if (output) *output = entry;
            return 1;
        }
    }
    return 0;
}

int main(void) {
    static const char payload[] = "RAM FAT32 data remains available until reboot";
    char buffer[sizeof(payload)] = {0};
    struct vfs_dirent entry;
    int handle;

    assert(ramfs_init() == 0);
    assert(ramfs_capacity_bytes() == 64u * 1024u * 1024u);
    uint64_t initial_entries = count_root_entries();
    assert(initial_entries > 0);
    assert(find_root_entry("bin", 0));
    assert(find_root_entry("dev", 0));
    assert(vfs_open("/TEST.TXT", VFS_OPEN_CREATE, &handle) == 0);
    assert(vfs_write(handle, payload, sizeof(payload) - 1) == (int)sizeof(payload) - 1);
    assert(vfs_close(handle) == 0);

    assert(find_root_entry("test.txt", &entry));
    assert(entry.size == sizeof(payload) - 1);
    assert(vfs_open("/TEST.TXT", 0, &handle) == 0);
    assert(vfs_read(handle, buffer, sizeof(buffer)) == (int)sizeof(payload) - 1);
    assert(strcmp(buffer, payload) == 0);
    assert(vfs_unlink("/TEST.TXT") < 0);
    assert(vfs_close(handle) == 0);

    assert(vfs_unlink("/TEST.TXT") == 0);
    assert(!find_root_entry("test.txt", 0));

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
    assert(entries == initial_entries + 17);
    puts("RAM FAT32 tests passed");
    return 0;
}
