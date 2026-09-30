#include <kern/block.h>
#include <kern/fat32_disk.h>
#include <kern/mm.h>
#include <kern/network.h>
#include <kern/procfs.h>
#include <kern/ramfs.h>
#include <kern/string.h>
#include <kern/timer.h>
#include <kern/types.h>
#include <kern/vfs.h>
#include <limits.h>

/* Each open file owns a bounded snapshot. Add new paths to builders, lookup, stat, and readdir. */
#define PROC_FILE_COUNT 16
#define PROC_FILE_SIZE 4096

struct proc_file {
    bool used;
    uint64_t inode;
    size_t size;
    char data[PROC_FILE_SIZE];
};

struct text_builder {
    char *data;
    size_t size;
    bool failed;
};

static struct proc_file open_files[PROC_FILE_COUNT];
static const char *const proc_names[] = {
    "meminfo", "uptime", "cpuinfo", "mounts", "partitions"
};
static const char *const self_names[] = { "status" };
static const char *const net_names[] = { "ipv4" };

static void append_text(struct text_builder *builder, const char *text) {
    size_t length = strlen(text);
    if (builder->failed || length > PROC_FILE_SIZE - builder->size) {
        builder->failed = true;
        return;
    }
    memcpy(builder->data + builder->size, text, length);
    builder->size += length;
}

static void append_u64(struct text_builder *builder, uint64_t value) {
    char digits[20];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (count) {
        char digit[2] = { digits[--count], 0 };
        append_text(builder, digit);
    }
}

static void append_ipv4(struct text_builder *builder, const char *label,
                        const uint8_t address[4]) {
    append_text(builder, label);
    for (size_t index = 0; index < 4; ++index) {
        if (index) append_text(builder, ".");
        append_u64(builder, address[index]);
    }
    append_text(builder, "\n");
}

static void append_kib(struct text_builder *builder, const char *label,
                       uint64_t bytes) {
    append_text(builder, label);
    append_u64(builder, bytes / 1024);
    append_text(builder, " kB\n");
}

static void build_meminfo(struct text_builder *builder) {
    uint64_t total = (uint64_t)pmm_total_pages() * PAGE_SIZE;
    uint64_t free = (uint64_t)pmm_free_page_count() * PAGE_SIZE;
    append_kib(builder, "MemTotal:       ", total);
    append_kib(builder, "MemFree:        ", free);
    append_kib(builder, "MemAvailable:   ", free);
    append_text(builder, "Buffers:             0 kB\nCached:              0 kB\n");
    append_text(builder, "SwapCached:          0 kB\nSwapTotal:           0 kB\n");
    append_text(builder, "SwapFree:            0 kB\n");
}

static void build_uptime(struct text_builder *builder) {
    uint64_t ticks = timer_ticks();
    append_u64(builder, ticks / 100);
    append_text(builder, ".");
    if (ticks % 100 < 10) append_text(builder, "0");
    append_u64(builder, ticks % 100);
    append_text(builder, " 0.00\n");
}

static void read_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *a,
                       uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "0"(leaf), "2"(subleaf));
}

static void build_cpuinfo(struct text_builder *builder) {
    uint32_t a, b, c, d;
    char vendor[13];
    char brand[49];
    uint32_t brand_words[12] = {0};
    uint32_t maximum_extended;
    read_cpuid(0, 0, &a, &b, &c, &d);
    memcpy(vendor, &b, 4);
    memcpy(vendor + 4, &d, 4);
    memcpy(vendor + 8, &c, 4);
    vendor[12] = 0;
    read_cpuid(1, 0, &a, &b, &c, &d);
    uint32_t base_family = (a >> 8) & 15;
    uint32_t family = base_family;
    uint32_t model = (a >> 4) & 15;
    if (base_family == 15) family += (a >> 20) & 255;
    if (base_family == 6 || base_family == 15)
        model |= ((a >> 16) & 15) << 4;
    read_cpuid(0x80000000, 0, &maximum_extended, &b, &c, &d);
    memset(brand, 0, sizeof(brand));
    if (maximum_extended >= 0x80000004) {
        for (uint32_t leaf = 0; leaf < 3; ++leaf)
            read_cpuid(0x80000002 + leaf, 0, &brand_words[leaf * 4],
                       &brand_words[leaf * 4 + 1], &brand_words[leaf * 4 + 2],
                       &brand_words[leaf * 4 + 3]);
        memcpy(brand, brand_words, sizeof(brand_words));
    }
    for (size_t index = sizeof(brand) - 1; index && brand[index - 1] == ' '; --index)
        brand[index - 1] = 0;
    append_text(builder, "processor\t: 0\nvendor_id\t: ");
    append_text(builder, vendor);
    append_text(builder, "\ncpu family\t: ");
    append_u64(builder, family);
    append_text(builder, "\nmodel\t\t: ");
    append_u64(builder, model);
    append_text(builder, "\nmodel name\t: ");
    append_text(builder, brand[0] ? brand : "x86_64 processor");
    append_text(builder, "\nflags\t\t:");
    read_cpuid(0x80000001, 0, &a, &b, &c, &d);
    if (maximum_extended >= 0x80000001) {
        if (d & (1u << 29)) append_text(builder, " lm");
        if (d & (1u << 20)) append_text(builder, " nx");
        if (d & (1u << 11)) append_text(builder, " syscall");
    }
    append_text(builder, "\n\n");
}

static void build_mounts(struct text_builder *builder) {
    append_text(builder, fat32_disk_root_active() ?
                 "root / fat32-disk ro 0 0\n" :
                 "root / fat32-ram rw 0 0\n");
    append_text(builder, "devfs /dev devfs rw 0 0\n");
    append_text(builder, "procfs /proc procfs ro 0 0\n");
}

static void build_self_status(struct text_builder *builder) {
    append_text(builder, "Name:\t\tUnitas\nState:\t\tR (running)\n");
    append_text(builder, "Tgid:\t\t1\nPid:\t\t1\nPPid:\t\t0\n");
    append_text(builder, "TracerPid:\t0\nUid:\t\t0\t0\t0\t0\n");
    append_text(builder, "Gid:\t\t0\t0\t0\t0\nThreads:\t1\n");
}

static void build_partitions(struct text_builder *builder) {
    append_text(builder, "# name sectors\n");
    for (size_t index = 0; index < block_device_count(); ++index) {
        struct block_device *device = block_device_at(index);
        if (!device) continue;
        append_text(builder, device->name);
        append_text(builder, " ");
        append_u64(builder, device->sector_count);
        append_text(builder, "\n");
    }
}

static void build_network(struct text_builder *builder) {
    struct network_status status;
    network_get_status(&status);
    append_text(builder, status.link_ready ? "Link: up\n" : "Link: down\n");
    append_text(builder, status.ipv4_ready ? "IPv4: configured\n" :
                                             "IPv4: waiting for DHCP\n");
    append_ipv4(builder, "Address: ", status.address);
    append_ipv4(builder, "Netmask: ", status.netmask);
    append_ipv4(builder, "Gateway: ", status.gateway);
    append_ipv4(builder, "DNS: ", status.dns);
}

static int find_file(const char *path) {
    if (!path || path[0] != '/') return -1;
    if (strcmp(path, "/self/status") == 0) return 5;
    if (strcmp(path, "/net/ipv4") == 0) return 6;
    for (size_t index = 0; index < ARRAY_SIZE(proc_names); ++index) {
        if (path[1] && strcmp(path + 1, proc_names[index]) == 0)
            return (int)index;
    }
    return -1;
}

static int procfs_open(const char *path, uint32_t flags, void *data,
                       void **node) {
    (void)data;
    int file_id = find_file(path);
    if (flags || file_id < 0 || !node) return -1;
    size_t slot = 0;
    while (slot < ARRAY_SIZE(open_files) && open_files[slot].used) ++slot;
    if (slot == ARRAY_SIZE(open_files)) return -1;

    /* Build once at open so later reads return one consistent snapshot. */
    struct proc_file *file = &open_files[slot];
    memset(file, 0, sizeof(*file));
    struct text_builder builder = { .data = file->data };
    switch (file_id) {
    case 0: build_meminfo(&builder); break;
    case 1: build_uptime(&builder); break;
    case 2: build_cpuinfo(&builder); break;
    case 3: build_mounts(&builder); break;
    case 4: build_partitions(&builder); break;
    case 5: build_self_status(&builder); break;
    case 6: build_network(&builder); break;
    default: return -1;
    }
    if (builder.failed) return -1;
    file->used = true;
    file->inode = (uint64_t)file_id + 2;
    file->size = builder.size;
    *node = file;
    return 0;
}

static int procfs_read(void *node, uint64_t offset, void *buffer, size_t length) {
    struct proc_file *file = node;
    if (!file || !file->used || (!buffer && length) || length > INT_MAX)
        return -1;
    if (offset >= file->size) return 0;
    size_t amount = file->size - (size_t)offset;
    if (amount > length) amount = length;
    memcpy(buffer, file->data + (size_t)offset, amount);
    return (int)amount;
}

static void procfs_close(void *node) {
    struct proc_file *file = node;
    if (file) memset(file, 0, sizeof(*file));
}

static void fill_stat(uint64_t inode, bool directory, size_t size,
                      struct vfs_stat *result) {
    memset(result, 0, sizeof(*result));
    result->inode = inode;
    result->links = directory ? 2 : 1;
    result->block_size = PAGE_SIZE;
    result->size = size;
    result->mode = directory ? (0040000 | 0555) : (0100000 | 0444);
}

static int procfs_stat_path(const char *path, void *data,
                            struct vfs_stat *result) {
    (void)data;
    if (!path || !result || path[0] != '/') return -1;
    if (strcmp(path, "/") == 0) {
        fill_stat(1, true, 0, result);
        return 0;
    }
    if (strcmp(path, "/self") == 0) {
        fill_stat(100, true, 0, result);
        return 0;
    }
    if (strcmp(path, "/net") == 0) {
        fill_stat(101, true, 0, result);
        return 0;
    }
    int file_id = find_file(path);
    if (file_id < 0) return -1;
    fill_stat((uint64_t)file_id + 2, false, 0, result);
    return 0;
}

static int procfs_stat_node(void *node, struct vfs_stat *result) {
    struct proc_file *file = node;
    if (!file || !file->used || !result) return -1;
    fill_stat(file->inode, false, file->size, result);
    return 0;
}

static int procfs_readdir(const char *path, void *data, uint64_t index,
                          struct vfs_dirent *entry) {
    (void)data;
    if (!path || !entry) return -1;
    const char *name;
    if (strcmp(path, "/") == 0) {
        if (index < ARRAY_SIZE(proc_names)) {
            name = proc_names[index];
        } else if (index == ARRAY_SIZE(proc_names)) {
            name = "self";
            entry->type = 1;
            entry->size = 0;
        } else if (index == ARRAY_SIZE(proc_names) + 1) {
            name = "net";
            entry->type = 1;
            entry->size = 0;
        } else {
            return 0;
        }
    } else if (strcmp(path, "/self") == 0) {
        if (index >= ARRAY_SIZE(self_names)) return 0;
        name = self_names[index];
    } else if (strcmp(path, "/net") == 0) {
        if (index >= ARRAY_SIZE(net_names)) return 0;
        name = net_names[index];
    } else {
        return -1;
    }
    size_t length = strlen(name);
    memcpy(entry->name, name, length + 1);
    if (strcmp(path, "/self") == 0 || strcmp(path, "/net") == 0) {
        entry->type = 0;
        entry->size = 0;
    }
    return 1;
}

static const struct filesystem procfs = {
    .name = "procfs",
    .open = procfs_open,
    .read = procfs_read,
    .close = procfs_close,
    .stat_path = procfs_stat_path,
    .stat_node = procfs_stat_node,
    .readdir = procfs_readdir
};

int procfs_init(void) {
    if (vfs_register(&procfs) != 0) return -1;
    return vfs_mount("/proc", &procfs, 0);
}
