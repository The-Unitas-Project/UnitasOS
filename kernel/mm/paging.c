#include <kern/mm.h>
#include <kern/string.h>

/* Keep the first 4 GiB mapped. Split kernel image pages so each section gets the required access. */

#define PAGE_PRESENT 0x001ULL
#define PAGE_WRITE 0x002ULL
#define PAGE_ADDRESS 0x000ffffffffff000ULL
#define PAGE_NX (1ULL << 63)
#define PAGE_TABLE_SPAN (2ULL * 1024 * 1024)
#define IDENTITY_LIMIT (4ULL * 1024 * 1024 * 1024)

extern uint64_t boot_pd0[512];
extern uint64_t boot_pd1[512];
extern uint64_t boot_pd2[512];
extern uint64_t boot_pd3[512];
extern char __kernel_start[];
extern char __kernel_end[];
extern char __multiboot_start[];
extern char __multiboot_end[];
extern char __text_start[];
extern char __text_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __data_end[];
extern char __bss_start[];
extern char __bss_end[];

static uint64_t *const page_directories[4] = {
    boot_pd0, boot_pd1, boot_pd2, boot_pd3
};

static bool enable_nx(void) {
    uint32_t eax = 0x80000000;
    uint32_t ebx, ecx, edx;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 0x80000001) return false;
    eax = 0x80000001;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (!(edx & (1u << 20))) return false;

    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0xc0000080));
    low |= 1u << 11;
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(0xc0000080));
    return true;
}

static bool section_contains(uint64_t address, const char *start,
                             const char *end) {
    return address >= (uintptr_t)start && address < (uintptr_t)end;
}

static uint64_t *allocate_page_table(void) {
    phys_addr_t physical = pmm_alloc_pages(1, IDENTITY_LIMIT);
    if (!physical) return 0;
    return (uint64_t *)(uintptr_t)physical;
}

int paging_init(void) {
    uint64_t kernel_start = (uintptr_t)__kernel_start;
    uint64_t kernel_end = (uintptr_t)__kernel_end;
    if (kernel_start >= kernel_end || kernel_end > IDENTITY_LIMIT || !enable_nx())
        return -1;

    /* Keep non-kernel identity mappings large and non-executable. */
    for (size_t directory = 0; directory < 4; ++directory) {
        for (size_t index = 0; index < 512; ++index) {
            uint64_t base = ((uint64_t)directory * 512 + index) * PAGE_TABLE_SPAN;
            uint64_t end = base + PAGE_TABLE_SPAN;
            if (end <= kernel_start || base >= kernel_end) {
                page_directories[directory][index] |= PAGE_NX;
                continue;
            }

            uint64_t *table = allocate_page_table();
            if (!table) return -1;
            for (size_t entry = 0; entry < 512; ++entry) {
                uint64_t address = base + entry * PAGE_SIZE;
                uint64_t flags = PAGE_PRESENT | PAGE_WRITE | PAGE_NX;
                if (section_contains(address, __multiboot_start, __multiboot_end))
                    flags = PAGE_PRESENT | PAGE_NX;
                else if (section_contains(address, __text_start, __text_end))
                    flags = PAGE_PRESENT;
                else if (section_contains(address, __rodata_start, __rodata_end))
                    flags = PAGE_PRESENT | PAGE_NX;
                else if (section_contains(address, __data_start, __data_end) ||
                         section_contains(address, __bss_start, __bss_end))
                    flags = PAGE_PRESENT | PAGE_WRITE | PAGE_NX;
                table[entry] = address | flags;
            }
            page_directories[directory][index] =
                ((uintptr_t)table & PAGE_ADDRESS) | PAGE_PRESENT | PAGE_WRITE;
        }
    }

    uint64_t control;
    __asm__ volatile("mov %%cr0, %0" : "=r"(control));
    control |= 1ULL << 16;
    __asm__ volatile("mov %0, %%cr0" : : "r"(control) : "memory");

    uint64_t root;
    __asm__ volatile("mov %%cr3, %0" : "=r"(root));
    __asm__ volatile("mov %0, %%cr3" : : "r"(root) : "memory");
    return 0;
}
