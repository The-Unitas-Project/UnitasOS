#include <kern/interrupts.h>
#include <kern/acpi.h>
#include <kern/block.h>
#include <kern/io.h>
#include <kern/log.h>
#include <kern/mm.h>
#include <kern/network.h>
#include <kern/rtc.h>
#include <kern/string.h>
#include <kern/timer.h>
#include <kern/types.h>
#include <kern/user.h>
#include <kern/vfs.h>
#include <unitas/dirent.h>
#include <linux/abi.h>
#include <asm/unistd_64.h>
#include <unitas/linux_stat.h>
#include <unitas/syscall.h>
#include <unitas/utsname.h>
#include <limits.h>

#define USER_BASE 0x100000000ULL
#define USER_LIMIT 0x140000000ULL
#define USER_STACK_BYTES (64ULL * 1024)
#define USER_STACK_START (USER_LIMIT - USER_STACK_BYTES)
#define USER_MAX_FILE (64ULL * 1024 * 1024)
#define USER_MAX_IMAGE (128ULL * 1024 * 1024)
#define USER_FD_COUNT 64
#define USER_ARG_COUNT 64
#define USER_ENV_COUNT 64
#define USER_EXEC_STRING_LIMIT 4096
#define USER_EXEC_BYTES_LIMIT (16 * 1024)
#define USER_WRITE_LIMIT (1024U * 1024U)
#define PAGE_PRESENT 0x001ULL
#define PAGE_WRITE 0x002ULL
#define PAGE_USER 0x004ULL
#define PAGE_ADDRESS 0x000ffffffffff000ULL
#define PAGE_NX (1ULL << 63)
#define ELF_PT_LOAD 1
#define ELF_PF_X 1
#define ELF_PF_W 2
#define ELF_ET_EXEC 2
#define ELF_EM_X86_64 62
#define ELF_PT_DYNAMIC 2
#define ELF_PT_INTERP 3
#define ELF_AT_NULL 0
#define ELF_AT_PHDR 3
#define ELF_AT_PHENT 4
#define ELF_AT_PHNUM 5
#define ELF_AT_PAGESZ 6
#define ELF_AT_BASE 7
#define ELF_AT_FLAGS 8
#define ELF_AT_ENTRY 9
#define ELF_AT_UID 11
#define ELF_AT_EUID 12
#define ELF_AT_GID 13
#define ELF_AT_EGID 14
#define ELF_AT_SECURE 23
#define ELF_AT_RANDOM 25
#define ELF_AT_EXECFN 31
#define USER_ENOENT 2
#define USER_E2BIG 7
#define USER_ENOMEM 12
#define USER_EFAULT 14
#define USER_EBUSY 16
#define USER_EACCES 13
#define USER_EINVAL 22
#define USER_EMFILE 24
#define USER_EBADF 9
#define USER_ENOSYS 38
#define USER_EIO 5
#define USER_EISDIR 21
#define USER_EEXIST 17
#define USER_ENOTEMPTY 39
#define USER_ENOTDIR 20
#define USER_ENOTTY 25
#define USER_OPEN_APPEND 0x10000ULL
#define USER_OPEN_EXCLUSIVE 0x20000ULL
#define USER_OPEN_DIRECTORY 0x40000ULL
#define USER_OPEN_CLOEXEC 0x80000ULL
#define USER_IOCTL_TCGETS 0x5401UL
#define USER_IOCTL_TIOCGWINSZ 0x5413UL
#define USER_IOCTL_FIONREAD 0x541bUL
#define USER_AT_EACCESS 0x200UL
#define USER_AT_SYMLINK_NOFOLLOW 0x100UL
#define LINUX_REBOOT_MAGIC1 0xfee1deadU
#define LINUX_REBOOT_MAGIC2 0x28121969U
#define LINUX_REBOOT_MAGIC2A 0x05121996U
#define LINUX_REBOOT_MAGIC2B 0x16041998U
#define LINUX_REBOOT_MAGIC2C 0x20112000U
#define LINUX_REBOOT_CMD_RESTART 0x01234567U
#define LINUX_REBOOT_CMD_HALT 0xcdef0123U
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321fedcU

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct user_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

struct elf64_header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_offset;
    uint64_t section_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_entry_size;
    uint16_t program_count;
    uint16_t section_entry_size;
    uint16_t section_count;
    uint16_t section_names_index;
} __attribute__((packed));

struct elf64_program {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} __attribute__((packed));

struct user_fd {
    bool used;
    bool readable;
    bool writable;
    bool directory;
    bool append;
    bool close_on_exec;
    uint32_t status_flags;
    int vfs_handle;
    uint64_t offset;
    uint64_t directory_index;
    char path[256];
};

struct user_termios {
    uint32_t input_flags;
    uint32_t output_flags;
    uint32_t control_flags;
    uint32_t local_flags;
    uint8_t line;
    uint8_t control_characters[19];
};

/* One user image owns its page tables, process settings, and descriptor table. */
struct user_space {
    uint64_t root;
    uint64_t directory_pointer;
    uint64_t directory;
    uint64_t heap_start;
    uint64_t heap_break;
    uint64_t heap_limit;
    uint64_t mmap_cursor;
    uint32_t file_mask;
    char cwd[256];
    struct user_termios terminal;
    bool active;
    bool exiting;
    int exit_status;
    struct user_fd descriptors[USER_FD_COUNT];
};

static struct tss64 kernel_tss;
static uint8_t user_kernel_stack[16384] __attribute__((aligned(16)));
static struct user_space current_space;
static uint64_t kernel_root;
static bool nx_available;
uint64_t user_resume_rsp;
uint64_t user_resume_rflags;
uint64_t user_kernel_stack_top;
uint64_t user_syscall_user_rsp;
uint64_t user_syscall_saved_r12;
uint64_t user_fs_base;
int user_syscall_exit_requested;
uint64_t user_syscall_exec_entry;
uint64_t user_syscall_exec_stack;
int user_syscall_exec_requested;

extern uint64_t gdt_tss_descriptor[2];
extern void user_interrupt_stub(void);
extern void user_syscall_entry(void);
extern void arch_enter_user(uint64_t entry, uint64_t stack);
extern uint8_t unitas_hello_elf_start[];
extern uint8_t unitas_hello_elf_end[];
struct embedded_program { const char *name; uint8_t *start; uint8_t *end; };
extern const struct embedded_program unitas_program_table[];
extern const uint64_t unitas_program_count;
static struct user_fd *get_descriptor(uint64_t number);
static bool hardware_random_word(uint64_t *value);

static uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value & PAGE_ADDRESS;
}

static void write_cr3(uint64_t value) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

static uint64_t *allocate_table(void) {
    phys_addr_t page = pmm_alloc_pages(1, 0x100000000ULL);
    if (!page) return 0;
    memset((void *)(uintptr_t)page, 0, PAGE_SIZE);
    return (uint64_t *)(uintptr_t)page;
}

static void enable_nx_if_supported(void) {
    uint32_t eax = 0x80000000, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 0x80000001) return;
    eax = 0x80000001;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (!(edx & (1u << 20))) return;
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0xc0000080));
    low |= 1u << 11;
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(0xc0000080));
    nx_available = true;
}

static uint64_t read_msr(uint32_t index) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(index));
    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t index, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(index));
}

static void enable_syscall_instruction(void) {
    uint32_t eax = 0x80000000, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 0x80000001) return;
    eax = 0x80000001;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (!(edx & (1u << 11))) return;
    uint64_t efer = read_msr(0xc0000080) | 1;
    write_msr(0xc0000080, efer);
    write_msr(0xc0000081, (0x10ULL << 48) | (0x08ULL << 32));
    write_msr(0xc0000082, (uintptr_t)user_syscall_entry);
    write_msr(0xc0000084, 0x700);
}

int user_init(void) {
    memset(&kernel_tss, 0, sizeof(kernel_tss));
    kernel_tss.rsp0 = (uintptr_t)(user_kernel_stack + sizeof(user_kernel_stack));
    user_kernel_stack_top = kernel_tss.rsp0;
    kernel_tss.iomap_base = sizeof(kernel_tss);
    uintptr_t base = (uintptr_t)&kernel_tss;
    uint64_t limit = sizeof(kernel_tss) - 1;
    gdt_tss_descriptor[0] = (limit & 0xffff) |
        ((base & 0xffffff) << 16) | (0x89ULL << 40) |
        (((limit >> 16) & 0xf) << 48) | ((base & 0xff000000ULL) << 32);
    gdt_tss_descriptor[1] = base >> 32;
    uint16_t selector = 0x28;
    __asm__ volatile("ltr %0" : : "r"(selector));
    interrupts_register_user_call(user_interrupt_stub);
    kernel_root = read_cr3();
    uint64_t control;
    __asm__ volatile("mov %%cr0, %0" : "=r"(control));
    control |= 1ULL << 16;
    __asm__ volatile("mov %0, %%cr0" : : "r"(control) : "memory");
    enable_nx_if_supported();
    enable_syscall_instruction();
    return kernel_root ? 0 : -1;
}

/* Copy the kernel mappings and reserve one user PML4 slot for this image. */
static bool make_address_space(void) {
    uint64_t *root = allocate_table();
    uint64_t *directory_pointer = allocate_table();
    if (!root || !directory_pointer) {
        if (root) pmm_free_pages((uintptr_t)root, 1);
        if (directory_pointer) pmm_free_pages((uintptr_t)directory_pointer, 1);
        return false;
    }
    uint64_t *source_root = (void *)(uintptr_t)kernel_root;
    uint64_t source_entry = source_root[0];
    if (!(source_entry & PAGE_PRESENT)) {
        pmm_free_pages((uintptr_t)root, 1);
        pmm_free_pages((uintptr_t)directory_pointer, 1);
        return false;
    }
    uint64_t *source_pointer = (void *)(uintptr_t)(source_entry & PAGE_ADDRESS);
    memcpy(root, source_root, PAGE_SIZE);
    memcpy(directory_pointer, source_pointer, PAGE_SIZE);
    root[0] = ((uintptr_t)directory_pointer & PAGE_ADDRESS) |
              (source_entry & 0xfff) | PAGE_USER;
    current_space.root = (uintptr_t)root;
    current_space.directory_pointer = (uintptr_t)directory_pointer;
    current_space.file_mask = 0022;
    current_space.terminal.local_flags = 0x0001 | 0x0002 | 0x0008;
    current_space.terminal.control_characters[4] = 4;
    current_space.terminal.control_characters[5] = 0;
    current_space.terminal.control_characters[6] = 1;
    current_space.active = true;
    return true;
}

static uint64_t *user_pte(uint64_t address) {
    if (!current_space.active || address < USER_BASE || address >= USER_LIMIT)
        return 0;
    uint64_t *root = (void *)(uintptr_t)current_space.root;
    uint64_t pml4_entry = root[(address >> 39) & 0x1ff];
    if (!(pml4_entry & PAGE_PRESENT)) return 0;
    uint64_t *directory_pointer = (void *)(uintptr_t)(pml4_entry & PAGE_ADDRESS);
    uint64_t pdpt_entry = directory_pointer[(address >> 30) & 0x1ff];
    if (!(pdpt_entry & PAGE_PRESENT) || (pdpt_entry & (1ULL << 7))) return 0;
    uint64_t *directory = (void *)(uintptr_t)(pdpt_entry & PAGE_ADDRESS);
    uint64_t pd_entry = directory[(address >> 21) & 0x1ff];
    if (!(pd_entry & PAGE_PRESENT) || (pd_entry & (1ULL << 7))) return 0;
    uint64_t *table = (void *)(uintptr_t)(pd_entry & PAGE_ADDRESS);
    return &table[(address >> 12) & 0x1ff];
}

static bool map_user_page(uint64_t address, bool writable, bool executable) {
    address = ALIGN_DOWN(address, PAGE_SIZE);
    if (address < USER_BASE || address >= USER_LIMIT) return false;
    uint64_t *root = (void *)(uintptr_t)current_space.root;
    uint64_t *directory_pointer = (void *)(uintptr_t)current_space.directory_pointer;
    root[0] |= PAGE_USER | PAGE_WRITE;
    uint64_t pdpt_entry = directory_pointer[4];
    if (!(pdpt_entry & PAGE_PRESENT)) {
        uint64_t *directory = allocate_table();
        if (!directory) return false;
        current_space.directory = (uintptr_t)directory;
        directory_pointer[4] = (uintptr_t)directory | PAGE_PRESENT |
                               PAGE_WRITE | PAGE_USER;
        pdpt_entry = directory_pointer[4];
    }
    uint64_t *directory = (void *)(uintptr_t)(pdpt_entry & PAGE_ADDRESS);
    size_t directory_index = (size_t)((address >> 21) & 0x1ff);
    uint64_t pd_entry = directory[directory_index];
    if (!(pd_entry & PAGE_PRESENT)) {
        uint64_t *table = allocate_table();
        if (!table) return false;
        directory[directory_index] = (uintptr_t)table | PAGE_PRESENT |
                                     PAGE_WRITE | PAGE_USER;
        pd_entry = directory[directory_index];
    }
    uint64_t *table = (void *)(uintptr_t)(pd_entry & PAGE_ADDRESS);
    uint64_t *pte = &table[(address >> 12) & 0x1ff];
    uint64_t flags = PAGE_PRESENT | PAGE_USER;
    if (writable) flags |= PAGE_WRITE;
    if (nx_available && !executable) flags |= PAGE_NX;
    if (*pte & PAGE_PRESENT) {
        if (!(*pte & PAGE_USER)) return false;
        if (writable) *pte |= PAGE_WRITE;
        if (executable) *pte &= ~PAGE_NX;
        return true;
    }
    phys_addr_t page = pmm_alloc_pages(1, 0x100000000ULL);
    if (!page) return false;
    memset((void *)(uintptr_t)page, 0, PAGE_SIZE);
    *pte = page | flags;
    return true;
}

/* Free user page tables and frames. Keep the shared kernel mappings. */
static void release_address_space(struct user_space *space) {
    if (!space->active) return;
    uint64_t *directory_pointer =
        (void *)(uintptr_t)space->directory_pointer;
    uint64_t pdpt_entry = directory_pointer[4];
    if (pdpt_entry & PAGE_PRESENT) {
        uint64_t *directory = (void *)(uintptr_t)(pdpt_entry & PAGE_ADDRESS);
        for (size_t di = 0; di < 512; ++di) {
            uint64_t pd_entry = directory[di];
            if (!(pd_entry & PAGE_PRESENT) || (pd_entry & (1ULL << 7))) continue;
            uint64_t *table = (void *)(uintptr_t)(pd_entry & PAGE_ADDRESS);
            for (size_t ti = 0; ti < 512; ++ti) {
                uint64_t pte = table[ti];
                if ((pte & (PAGE_PRESENT | PAGE_USER)) ==
                    (PAGE_PRESENT | PAGE_USER))
                    pmm_free_pages(pte & PAGE_ADDRESS, 1);
            }
            pmm_free_pages((uintptr_t)table, 1);
        }
        pmm_free_pages((uintptr_t)directory, 1);
    }
    pmm_free_pages(space->directory_pointer, 1);
    pmm_free_pages(space->root, 1);
    *space = (struct user_space){0};
}

static void free_address_space(void) {
    release_address_space(&current_space);
}

static bool copy_to_user_image(uint64_t destination, const void *source,
                               size_t length) {
    const uint8_t *bytes = source;
    while (length) {
        uint64_t *pte = user_pte(destination);
        if (!pte || !(*pte & PAGE_PRESENT)) return false;
        size_t offset = (size_t)(destination & PAGE_MASK);
        size_t amount = PAGE_SIZE - offset;
        if (amount > length) amount = length;
        uint8_t *physical = (void *)(uintptr_t)((*pte & PAGE_ADDRESS) + offset);
        memcpy(physical, bytes, amount);
        destination += amount;
        bytes += amount;
        length -= amount;
    }
    return true;
}

static bool validate_user_range(uint64_t address, size_t length, bool writable) {
    if (!length) return true;
    if (address < USER_BASE || address >= USER_LIMIT ||
        (uint64_t)length > USER_LIMIT - address)
        return false;
    uint64_t end = address + length;
    uint64_t page = ALIGN_DOWN(address, PAGE_SIZE);
    while (page < end) {
        uint64_t *pte = user_pte(page);
        if (!pte || (*pte & (PAGE_PRESENT | PAGE_USER)) !=
                    (PAGE_PRESENT | PAGE_USER) ||
            (writable && !(*pte & PAGE_WRITE))) return false;
        page += PAGE_SIZE;
    }
    return true;
}

/* Accept static x86-64 ET_EXEC files. Reject writable and executable page overlap. */
static bool validate_elf(const uint8_t *image, size_t length,
                         const struct elf64_header **header_out) {
    if (length < sizeof(struct elf64_header)) return false;
    const struct elf64_header *header = (const void *)image;
    if (memcmp(header->ident, "\x7f" "ELF", 4) != 0 ||
        header->ident[4] != 2 || header->ident[5] != 1 ||
        header->ident[6] != 1 || header->type != ELF_ET_EXEC ||
        header->machine != ELF_EM_X86_64 || header->version != 1 ||
        header->header_size != sizeof(*header) ||
        header->program_entry_size != sizeof(struct elf64_program) ||
        !header->program_count || header->program_count > 128 ||
        header->program_offset > length ||
        (uint64_t)header->program_count * sizeof(struct elf64_program) >
            length - header->program_offset)
        return false;
    const struct elf64_program *programs =
        (const void *)(image + header->program_offset);
    bool executable_entry = false;
    size_t load_count = 0;
    uint64_t image_bytes = 0;
    for (size_t i = 0; i < header->program_count; ++i) {
        const struct elf64_program *program = &programs[i];
        if (program->type == ELF_PT_DYNAMIC || program->type == ELF_PT_INTERP)
            return false;
        if (program->type != ELF_PT_LOAD) continue;
        ++load_count;
        if (program->memory_size < program->file_size ||
            program->offset > length || program->file_size > length - program->offset ||
            program->virtual_address < USER_BASE ||
            program->virtual_address >= USER_STACK_START ||
            program->memory_size > USER_STACK_START - program->virtual_address ||
            program->memory_size > USER_MAX_IMAGE - image_bytes ||
            (program->flags & ~7u) ||
            ((program->flags & (ELF_PF_W | ELF_PF_X)) ==
             (ELF_PF_W | ELF_PF_X)) ||
            (program->alignment > 1 &&
             ((program->alignment & (program->alignment - 1)) ||
              program->virtual_address % program->alignment !=
                  program->offset % program->alignment)))
            return false;
        image_bytes += program->memory_size;
        if (program->memory_size && (program->flags & ELF_PF_X) &&
            header->entry >= program->virtual_address &&
            header->entry - program->virtual_address < program->memory_size)
            executable_entry = true;
        for (size_t j = 0; j < i; ++j) {
            const struct elf64_program *previous = &programs[j];
            if (previous->type != ELF_PT_LOAD || !program->memory_size ||
                !previous->memory_size) continue;
            uint64_t end = program->virtual_address + program->memory_size;
            uint64_t previous_end = previous->virtual_address + previous->memory_size;
            if (program->virtual_address < previous_end &&
                previous->virtual_address < end) return false;
            uint64_t page_start = ALIGN_DOWN(program->virtual_address, PAGE_SIZE);
            uint64_t page_end = ALIGN_UP(end, PAGE_SIZE);
            uint64_t previous_page_start =
                ALIGN_DOWN(previous->virtual_address, PAGE_SIZE);
            uint64_t previous_page_end = ALIGN_UP(previous_end, PAGE_SIZE);
            bool page_overlap = page_start < previous_page_end &&
                                previous_page_start < page_end;
            bool write_execute =
                ((program->flags & ELF_PF_W) && (previous->flags & ELF_PF_X)) ||
                ((program->flags & ELF_PF_X) && (previous->flags & ELF_PF_W));
            if (page_overlap && write_execute) return false;
        }
    }
    if (!load_count || !executable_entry) return false;
    *header_out = header;
    return true;
}

static int read_executable(const char *path, uint8_t **image_out,
                           size_t *length_out) {
    int handle;
    if (vfs_open(path, 0, &handle) < 0) return -USER_ENOENT;
    size_t capacity = 4096;
    size_t length = 0;
    uint8_t *image = kmalloc(capacity);
    if (!image) {
        (void)vfs_close(handle);
        return -USER_ENOMEM;
    }
    for (;;) {
        if (length == capacity) {
            if (capacity >= USER_MAX_FILE) {
                kfree(image);
                (void)vfs_close(handle);
                return -USER_EINVAL;
            }
            size_t next_capacity = capacity * 2;
            if (next_capacity > USER_MAX_FILE) next_capacity = USER_MAX_FILE;
            uint8_t *next = krealloc(image, next_capacity);
            if (!next) {
                kfree(image);
                (void)vfs_close(handle);
                return -USER_ENOMEM;
            }
            image = next;
            capacity = next_capacity;
        }
        int amount = vfs_read(handle, image + length, capacity - length);
        if (amount < 0) {
            kfree(image);
            (void)vfs_close(handle);
            return -USER_EINVAL;
        }
        if (!amount) break;
        length += (size_t)amount;
    }
    (void)vfs_close(handle);
    *image_out = image;
    *length_out = length;
    return 0;
}

static bool load_elf_segments(const uint8_t *image, const struct elf64_header *header) {
    const struct elf64_program *programs =
        (const void *)(image + header->program_offset);
    uint64_t highest_address = USER_BASE;
    for (size_t i = 0; i < header->program_count; ++i) {
        const struct elf64_program *program = &programs[i];
        if (program->type != ELF_PT_LOAD || !program->memory_size) continue;
        uint64_t first = ALIGN_DOWN(program->virtual_address, PAGE_SIZE);
        uint64_t last = ALIGN_UP(program->virtual_address + program->memory_size,
                                 PAGE_SIZE);
        for (uint64_t page = first; page < last; page += PAGE_SIZE)
            if (!map_user_page(page, (program->flags & ELF_PF_W) != 0,
                               (program->flags & ELF_PF_X) != 0)) return false;
        if (!copy_to_user_image(program->virtual_address,
                                image + program->offset,
                                (size_t)program->file_size)) return false;
        uint64_t end = program->virtual_address + program->memory_size;
        if (end > highest_address) highest_address = end;
    }
    current_space.heap_start = ALIGN_UP(highest_address, PAGE_SIZE);
    current_space.heap_break = current_space.heap_start;
    current_space.heap_limit = USER_STACK_START;
    current_space.mmap_cursor = USER_STACK_START;
    for (uint64_t page = USER_STACK_START; page < USER_LIMIT; page += PAGE_SIZE)
        if (!map_user_page(page, true, false)) return false;
    return true;
}

/* Build the Linux x86-64 initial stack: argc, argv, envp, then auxiliary data. */
static bool setup_user_stack(size_t argument_count, const char *const arguments[],
                             size_t environment_count,
                             const char *const environment[],
                             const char *execution_path,
                             const uint8_t *image,
                             const struct elf64_header *header,
                             uint64_t *stack_out) {
    if (!argument_count || argument_count > USER_ARG_COUNT || !arguments ||
        environment_count > USER_ENV_COUNT ||
        (environment_count && !environment) || !execution_path)
        return false;
    uint64_t values[USER_ARG_COUNT * 2 + USER_ENV_COUNT * 2 + 40] = {0};
    uint64_t string_cursor = USER_LIMIT;
    size_t total_length = 0;
    uint64_t argument_pointers[USER_ARG_COUNT] = {0};
    uint64_t environment_pointers[USER_ENV_COUNT] = {0};
    for (size_t index = 0; index < argument_count; ++index) {
        if (!arguments[index]) return false;
        size_t length = strlen(arguments[index]) + 1;
        if (length > USER_EXEC_STRING_LIMIT ||
            length > USER_EXEC_BYTES_LIMIT - total_length ||
            length > string_cursor - USER_STACK_START) return false;
        string_cursor -= length;
        total_length += length;
        if (!copy_to_user_image(string_cursor, arguments[index], length))
            return false;
        argument_pointers[index] = string_cursor;
    }
    for (size_t index = 0; index < environment_count; ++index) {
        if (!environment[index]) return false;
        size_t length = strlen(environment[index]) + 1;
        if (length > USER_EXEC_STRING_LIMIT ||
            length > USER_EXEC_BYTES_LIMIT - total_length ||
            length > string_cursor - USER_STACK_START) return false;
        string_cursor -= length;
        total_length += length;
        if (!copy_to_user_image(string_cursor, environment[index], length))
            return false;
        environment_pointers[index] = string_cursor;
    }
    size_t execution_path_length = strlen(execution_path) + 1;
    if (execution_path_length > USER_EXEC_STRING_LIMIT ||
        execution_path_length > USER_EXEC_BYTES_LIMIT - total_length ||
        execution_path_length > string_cursor - USER_STACK_START)
        return false;
    string_cursor -= execution_path_length;
    total_length += execution_path_length;
    if (!copy_to_user_image(string_cursor, execution_path,
                            execution_path_length)) return false;
    uint64_t execution_path_pointer = string_cursor;
    uint64_t random_words[2];
    for (size_t index = 0; index < ARRAY_SIZE(random_words); ++index) {
        if (hardware_random_word(&random_words[index])) continue;
        uint32_t low, high;
        __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
        random_words[index] = ((uint64_t)high << 32) | low;
        random_words[index] ^= (uintptr_t)&random_words ^
                               (index ? random_words[index - 1] : 0);
    }
    string_cursor -= sizeof(random_words);
    total_length += sizeof(random_words);
    if (total_length > USER_EXEC_BYTES_LIMIT ||
        !copy_to_user_image(string_cursor, random_words, sizeof(random_words)))
        return false;
    uint64_t random_address = string_cursor;
    uint64_t program_headers = 0;
    const struct elf64_program *programs =
        (const void *)(image + header->program_offset);
    uint64_t headers_end = header->program_offset +
        (uint64_t)header->program_count * sizeof(struct elf64_program);
    for (size_t index = 0; index < header->program_count; ++index) {
        const struct elf64_program *program = &programs[index];
        if (program->type != ELF_PT_LOAD ||
            header->program_offset < program->offset ||
            headers_end > program->offset + program->file_size) continue;
        program_headers = program->virtual_address +
            header->program_offset - program->offset;
        break;
    }
    if (!program_headers) return false;
    size_t word_count = 0;
    values[word_count++] = argument_count;
    for (size_t index = 0; index < argument_count; ++index)
        values[word_count++] = argument_pointers[index];
    values[word_count++] = 0;
    for (size_t index = 0; index < environment_count; ++index)
        values[word_count++] = environment_pointers[index];
    values[word_count++] = 0;
#define ADD_AUXILIARY(type, value) do { \
    values[word_count++] = (type); \
    values[word_count++] = (value); \
} while (0)
    ADD_AUXILIARY(ELF_AT_PHDR, program_headers);
    ADD_AUXILIARY(ELF_AT_PHENT, sizeof(struct elf64_program));
    ADD_AUXILIARY(ELF_AT_PHNUM, header->program_count);
    ADD_AUXILIARY(ELF_AT_PAGESZ, PAGE_SIZE);
    ADD_AUXILIARY(ELF_AT_BASE, 0);
    ADD_AUXILIARY(ELF_AT_FLAGS, 0);
    ADD_AUXILIARY(ELF_AT_ENTRY, header->entry);
    ADD_AUXILIARY(ELF_AT_UID, 0);
    ADD_AUXILIARY(ELF_AT_EUID, 0);
    ADD_AUXILIARY(ELF_AT_GID, 0);
    ADD_AUXILIARY(ELF_AT_EGID, 0);
    ADD_AUXILIARY(ELF_AT_SECURE, 0);
    ADD_AUXILIARY(ELF_AT_RANDOM, random_address);
    ADD_AUXILIARY(ELF_AT_EXECFN, execution_path_pointer);
    ADD_AUXILIARY(ELF_AT_NULL, 0);
#undef ADD_AUXILIARY
    uint64_t stack_bytes = word_count * sizeof(uint64_t);
    uint64_t stack = ALIGN_DOWN(string_cursor - stack_bytes, 16);
    if (stack < USER_STACK_START) return false;
    if (!copy_to_user_image(stack, values, (size_t)stack_bytes)) return false;
    *stack_out = stack;
    return true;
}

static int open_standard_descriptors(void) {
    for (int descriptor = 0; descriptor < 3; ++descriptor) {
        int handle;
        if (vfs_open("/dev/console", 0, &handle) < 0) return -1;
        current_space.descriptors[descriptor] = (struct user_fd) {
            .used = true,
            .readable = descriptor == 0,
            .writable = descriptor != 0,
            .vfs_handle = handle
        };
        memcpy(current_space.descriptors[descriptor].path, "/dev/console",
               sizeof("/dev/console"));
    }
    return 0;
}

static void close_descriptors(void) {
    for (size_t index = 0; index < USER_FD_COUNT; ++index) {
        if (!current_space.descriptors[index].used) continue;
        if (!current_space.descriptors[index].directory)
            (void)vfs_close(current_space.descriptors[index].vfs_handle);
    }
}

static void close_user_descriptor(uint64_t number) {
    struct user_fd *descriptor = get_descriptor(number);
    if (!descriptor) return;
    if (!descriptor->directory) (void)vfs_close(descriptor->vfs_handle);
    *descriptor = (struct user_fd){0};
}

static long duplicate_user_descriptor(uint64_t old_number, uint64_t minimum,
                                      uint64_t exact, bool replace) {
    struct user_fd *old = get_descriptor(old_number);
    if (!old) return -USER_EBADF;
    if (replace) {
        if (exact >= USER_FD_COUNT) return -USER_EBADF;
        if (old_number == exact) return (long)exact;
        close_user_descriptor(exact);
        minimum = exact;
    }
    if (minimum >= USER_FD_COUNT) return -USER_EINVAL;
    size_t target = (size_t)minimum;
    if (!replace) {
        while (target < USER_FD_COUNT && current_space.descriptors[target].used)
            ++target;
    }
    if (target >= USER_FD_COUNT) return -USER_EMFILE;
    struct user_fd duplicate = *old;
    if (!old->directory) {
        int handle = vfs_dup(old->vfs_handle);
        if (handle < 0) return -USER_EBADF;
        duplicate.vfs_handle = handle;
    }
    duplicate.close_on_exec = false;
    current_space.descriptors[target] = duplicate;
    return (long)target;
}

int user_exec(const char *path, size_t argument_count,
              const char *const arguments[]) {
    if (!path || path[0] != '/' || strlen(path) >= 256 || current_space.active)
        return -1;
    if (!argument_count || argument_count > USER_ARG_COUNT || !arguments)
        return -USER_EINVAL;
    if (strlen(path) >= 5 && memcmp(path, "/dev/", 5) == 0)
        return -USER_EACCES;
    uint8_t *image = 0;
    size_t image_length = 0;
    int result = read_executable(path, &image, &image_length);
    if (result < 0) return result;
    static const char *const initial_environment[] = {
        "PATH=/bin", "HOME=/root", "TERM=dumb"
    };
    const struct elf64_header *header = 0;
    if (!validate_elf(image, image_length, &header) || !make_address_space() ||
        !load_elf_segments(image, header)) {
        kfree(image);
        write_cr3(kernel_root);
        free_address_space();
        return -USER_EINVAL;
    }
    uint64_t stack;
    if (!setup_user_stack(argument_count, arguments,
                          ARRAY_SIZE(initial_environment), initial_environment,
                          path, image, header, &stack) ||
        open_standard_descriptors() != 0) {
        kfree(image);
        close_descriptors();
        write_cr3(kernel_root);
        free_address_space();
        return -USER_ENOMEM;
    }
    uint64_t entry = header->entry;
    kfree(image);
    current_space.active = true;
    current_space.exiting = false;
    current_space.exit_status = 0;
    user_syscall_exit_requested = 0;
    user_syscall_exec_requested = 0;
    user_fs_base = 0;
    write_msr(0xc0000100, 0);
    current_space.cwd[0] = '/';
    current_space.cwd[1] = 0;
    write_cr3(current_space.root);
    arch_enter_user(entry, stack);
    write_cr3(kernel_root);
    int exit_status = current_space.exit_status;
    close_descriptors();
    free_address_space();
    return exit_status;
}

static bool user_string(uint64_t address, char *output, size_t capacity) {
    if (!capacity) return false;
    for (size_t i = 0; i < capacity; ++i) {
        if (!validate_user_range(address + i, 1, false)) return false;
        output[i] = ((const char *)(uintptr_t)address)[i];
        if (!output[i]) return true;
    }
    return false;
}

/* Normalize a user path and resolve relative names against the process cwd. */
static bool user_path(uint64_t address, char output[256]) {
    char input[256];
    if (!user_string(address, input, sizeof(input)) || !input[0]) return false;
    char combined[512];
    size_t length = 0;
    if (input[0] == '/') {
        length = strlen(input);
        if (length >= sizeof(combined)) return false;
        memcpy(combined, input, length + 1);
    } else {
        size_t cwd_length = strlen(current_space.cwd);
        if (cwd_length + (cwd_length > 1) + strlen(input) >= sizeof(combined))
            return false;
        memcpy(combined, current_space.cwd, cwd_length);
        length = cwd_length;
        if (cwd_length > 1) combined[length++] = '/';
        memcpy(combined + length, input, strlen(input) + 1);
    }
    size_t source = 0;
    size_t target = 0;
    output[target++] = '/';
    while (source < length) {
        while (source < length && combined[source] == '/') ++source;
        if (source == length) break;
        size_t start = source;
        while (source < length && combined[source] != '/') ++source;
        size_t component_length = source - start;
        if (component_length == 1 && combined[start] == '.') continue;
        if (component_length == 2 && combined[start] == '.' &&
            combined[start + 1] == '.') {
            if (target > 1) {
                --target;
                while (target > 1 && output[target - 1] != '/') --target;
            }
            continue;
        }
        if (target > 1) output[target++] = '/';
        if (component_length >= 256 - target) return false;
        memcpy(output + target, combined + start, component_length);
        target += component_length;
    }
    output[target] = 0;
    return true;
}

/* Keep copied strings and pointer vectors alive while the new stack is built. */
struct linux_exec_arguments {
    size_t argument_count;
    size_t environment_count;
    size_t string_bytes;
    char path[256];
    char storage[USER_EXEC_BYTES_LIMIT];
    const char *arguments[USER_ARG_COUNT];
    const char *environment[USER_ENV_COUNT];
};

/* Copy a NULL-terminated user pointer vector into kernel-owned storage. */
static long copy_exec_vector(uint64_t vector, const char **strings,
                             size_t capacity, size_t *count,
                             struct linux_exec_arguments *arguments) {
    *count = 0;
    if (!vector) return 0;

    /* Bound pointer count and copied bytes before replacing the image. */
    for (size_t index = 0; index <= capacity; ++index) {
        uint64_t slot = vector + index * sizeof(uint64_t);
        if (!validate_user_range(slot, sizeof(uint64_t), false))
            return -USER_EFAULT;
        uint64_t source = *(const uint64_t *)(uintptr_t)slot;
        if (!source) {
            *count = index;
            return 0;
        }
        if (index == capacity) return -USER_E2BIG;

        size_t remaining = USER_EXEC_BYTES_LIMIT - arguments->string_bytes;
        size_t limit = remaining < USER_EXEC_STRING_LIMIT ?
                       remaining : USER_EXEC_STRING_LIMIT;
        char *destination = arguments->storage + arguments->string_bytes;
        size_t length = 0;
        for (; length < limit; ++length) {
            uint64_t character_address = source + length;
            if (!validate_user_range(character_address, 1, false))
                return -USER_EFAULT;
            destination[length] = *(const char *)(uintptr_t)character_address;
            if (!destination[length]) {
                ++length;
                break;
            }
        }
        if (!length || destination[length - 1]) return -USER_E2BIG;
        strings[index] = destination;
        arguments->string_bytes += length;
    }
    return -USER_E2BIG;
}

/* Replace the current image only after the new ELF and stack are ready. */
static long linux_execve(uint64_t path_address, uint64_t argv_address,
                         uint64_t envp_address) {
    struct linux_exec_arguments *arguments = kcalloc(1, sizeof(*arguments));
    if (!arguments) return -USER_ENOMEM;
    if (!user_path(path_address, arguments->path)) {
        kfree(arguments);
        return -USER_EFAULT;
    }
    long result = copy_exec_vector(argv_address, arguments->arguments,
                                   USER_ARG_COUNT, &arguments->argument_count,
                                   arguments);
    if (result == 0 && !arguments->argument_count) result = -USER_EINVAL;
    if (result == 0)
        result = copy_exec_vector(envp_address, arguments->environment,
                                  USER_ENV_COUNT,
                                  &arguments->environment_count, arguments);
    if (result == 0 &&
        arguments->string_bytes + strlen(arguments->path) + 1 +
            2 * sizeof(uint64_t) > USER_EXEC_BYTES_LIMIT)
        result = -USER_E2BIG;
    if (result < 0) {
        kfree(arguments);
        return result;
    }
    if (strlen(arguments->path) >= 5 &&
        memcmp(arguments->path, "/dev/", 5) == 0) {
        kfree(arguments);
        return -USER_EACCES;
    }

    uint8_t *image = 0;
    size_t image_length = 0;
    result = read_executable(arguments->path, &image, &image_length);
    if (result < 0) {
        kfree(arguments);
        return result;
    }
    const struct elf64_header *header = 0;
    if (!validate_elf(image, image_length, &header)) {
        kfree(image);
        kfree(arguments);
        return -USER_EINVAL;
    }

    struct user_space *previous = kmalloc(sizeof(*previous));
    if (!previous) {
        kfree(image);
        kfree(arguments);
        return -USER_ENOMEM;
    }
    *previous = current_space;
    current_space = (struct user_space){0};

    uint64_t stack = 0;
    bool loaded = make_address_space() &&
                  load_elf_segments(image, header) &&
                  setup_user_stack(arguments->argument_count,
                                   arguments->arguments,
                                   arguments->environment_count,
                                   arguments->environment, arguments->path,
                                   image, header, &stack);
    if (!loaded) {
        release_address_space(&current_space);
        current_space = *previous;
        kfree(previous);
        kfree(image);
        kfree(arguments);
        return -USER_ENOMEM;
    }

    current_space.file_mask = previous->file_mask;
    current_space.terminal = previous->terminal;
    memcpy(current_space.cwd, previous->cwd, sizeof(current_space.cwd));
    /* Keep open descriptors unless they have the close-on-exec flag. */
    for (size_t index = 0; index < USER_FD_COUNT; ++index) {
        struct user_fd *descriptor = &previous->descriptors[index];
        if (!descriptor->used) continue;
        if (descriptor->close_on_exec) {
            if (!descriptor->directory)
                (void)vfs_close(descriptor->vfs_handle);
        } else {
            current_space.descriptors[index] = *descriptor;
        }
        descriptor->used = false;
    }

    uint64_t entry = header->entry;
    uint64_t new_root = current_space.root;
    /* Switch roots before freeing the old user mappings. */
    write_cr3(new_root);
    release_address_space(previous);
    kfree(previous);
    kfree(image);
    kfree(arguments);

    user_fs_base = 0;
    write_msr(0xc0000100, 0);
    user_syscall_exit_requested = 0;
    user_syscall_exec_entry = entry;
    user_syscall_exec_stack = stack;
    user_syscall_exec_requested = 1;
    return 0;
}

static struct user_fd *get_descriptor(uint64_t number) {
    if (number >= USER_FD_COUNT || !current_space.descriptors[number].used)
        return 0;
    return &current_space.descriptors[number];
}

static bool user_path_at(uint64_t directory, uint64_t path_address,
                         char output[256]) {
    char path[256];
    if (!user_string(path_address, path, sizeof(path)) || !path[0]) return false;
    if (path[0] == '/') return user_path(path_address, output);
    if ((int64_t)directory == AT_FDCWD)
        return user_path(path_address, output);
    struct user_fd *base = get_descriptor(directory);
    if (!base) return false;
    if (!base->directory) return false;
    char saved_cwd[sizeof(current_space.cwd)];
    memcpy(saved_cwd, current_space.cwd, sizeof(saved_cwd));
    memcpy(current_space.cwd, base->path, strlen(base->path) + 1);
    bool result = user_path(path_address, output);
    memcpy(current_space.cwd, saved_cwd, sizeof(saved_cwd));
    return result;
}

/* Translate Linux open flags and reject raw device paths outside the allowlist. */
static long user_open_path(const char *path, uint64_t flags) {
    const uint64_t allowed = UNITAS_O_WRONLY | UNITAS_O_RDWR |
                             UNITAS_O_CREAT | UNITAS_O_TRUNC |
                             USER_OPEN_APPEND | USER_OPEN_EXCLUSIVE |
                             USER_OPEN_DIRECTORY | USER_OPEN_CLOEXEC;
    if ((flags & ~allowed) || (flags & (UNITAS_O_WRONLY | UNITAS_O_RDWR)) ==
        (UNITAS_O_WRONLY | UNITAS_O_RDWR)) return -USER_EINVAL;
    bool writable = (flags & (UNITAS_O_WRONLY | UNITAS_O_RDWR)) != 0;
    bool readable = !(flags & UNITAS_O_WRONLY);
    if ((flags & UNITAS_O_TRUNC) && !writable) return -USER_EINVAL;
    if (strlen(path) >= 5 && memcmp(path, "/dev/", 5) == 0 &&
        strcmp(path, "/dev/console") != 0 && strcmp(path, "/dev/null") != 0 &&
        strcmp(path, "/dev/zero") != 0 && strcmp(path, "/dev/kbd") != 0 &&
        strcmp(path, "/dev/serial0") != 0) return -USER_EACCES;
    size_t descriptor;
    for (descriptor = 3; descriptor < USER_FD_COUNT &&
         current_space.descriptors[descriptor].used; ++descriptor) {}
    if (descriptor == USER_FD_COUNT) return -USER_EMFILE;
    struct vfs_dirent first_entry;
    int directory_result = vfs_readdir(path, 0, &first_entry);
    struct vfs_stat existing_stat;
    bool exists = vfs_stat(path, &existing_stat) == 0;
    bool is_directory = exists &&
        (existing_stat.mode & S_IFMT) == S_IFDIR;
    if ((flags & USER_OPEN_EXCLUSIVE) && (flags & UNITAS_O_CREAT) && exists)
        return -USER_EEXIST;
    if ((flags & USER_OPEN_DIRECTORY) && !is_directory)
        return exists ? -USER_ENOTDIR : -USER_ENOENT;
    if (directory_result >= 0 || is_directory) {
        if (writable || (flags & (UNITAS_O_CREAT | UNITAS_O_TRUNC)))
            return -USER_EISDIR;
        current_space.descriptors[descriptor] = (struct user_fd) {
            .used = true, .readable = true, .directory = true,
            .vfs_handle = -1
        };
        size_t length = strlen(path);
        memcpy(current_space.descriptors[descriptor].path, path, length + 1);
        return (long)descriptor;
    }
    uint32_t vfs_flags = 0;
    if (flags & UNITAS_O_CREAT) vfs_flags |= VFS_OPEN_CREATE;
    if (flags & UNITAS_O_TRUNC) vfs_flags |= VFS_OPEN_TRUNCATE;
    int handle;
    if (vfs_open(path, vfs_flags, &handle) < 0) return -USER_ENOENT;
    current_space.descriptors[descriptor] = (struct user_fd) {
        .used = true, .readable = readable, .writable = writable,
        .vfs_handle = handle,
        .append = (flags & USER_OPEN_APPEND) != 0,
        .close_on_exec = (flags & USER_OPEN_CLOEXEC) != 0,
        .status_flags = (uint32_t)(flags & (UNITAS_O_WRONLY | UNITAS_O_RDWR |
                                             USER_OPEN_APPEND))
    };
    if (vfs_set_append(handle, (flags & USER_OPEN_APPEND) != 0) < 0) {
        (void)vfs_close(handle);
        current_space.descriptors[descriptor] = (struct user_fd){0};
        return -USER_EINVAL;
    }
    memcpy(current_space.descriptors[descriptor].path, path, strlen(path) + 1);
    return (long)descriptor;
}

static long user_open(uint64_t path_address, uint64_t flags) {
    char path[256];
    if (!user_path(path_address, path)) return -USER_EFAULT;
    return user_open_path(path, flags);
}

static long syscall_read(uint64_t number, uint64_t address, uint64_t length) {
    struct user_fd *descriptor = get_descriptor(number);
    if (!descriptor || !descriptor->readable) return -USER_EBADF;
    if (descriptor->directory) return -USER_EISDIR;
    if (length > USER_WRITE_LIMIT || !validate_user_range(address, (size_t)length, true))
        return -USER_EFAULT;
    for (;;) {
        int amount = vfs_read(descriptor->vfs_handle, (void *)(uintptr_t)address,
                              (size_t)length);
        if (amount < 0) return -USER_EINVAL;
        if (amount || number != 0 || !length) {
            if (vfs_tell(descriptor->vfs_handle, &descriptor->offset) < 0)
                descriptor->offset += (uint64_t)amount;
            return amount;
        }
        network_poll();
        cpu_enable_interrupts();
        cpu_halt();
        cpu_disable_interrupts();
    }
}

/* Validate the user buffer before passing it to a device or filesystem. */
static long syscall_write(uint64_t number, uint64_t address, uint64_t length) {
    struct user_fd *descriptor = get_descriptor(number);
    if (!descriptor || !descriptor->writable) return -USER_EBADF;
    if (descriptor->directory) return -USER_EISDIR;
    if (length > USER_WRITE_LIMIT || !validate_user_range(address, (size_t)length, false))
        return -USER_EFAULT;
    bool append;
    if (vfs_is_append(descriptor->vfs_handle, &append) < 0)
        return -USER_EBADF;
    if (append) {
        struct vfs_stat info;
        if (vfs_fstat(descriptor->vfs_handle, &info) < 0 ||
            vfs_seek(descriptor->vfs_handle, info.size) < 0)
            return -USER_EINVAL;
        descriptor->offset = info.size;
    }
    int amount = vfs_write(descriptor->vfs_handle, (const void *)(uintptr_t)address,
                           (size_t)length);
    if (amount < 0) return -USER_EINVAL;
    if (vfs_tell(descriptor->vfs_handle, &descriptor->offset) < 0)
        descriptor->offset += (uint64_t)amount;
    return amount;
}

static long syscall_seek(uint64_t number, int64_t offset, uint64_t origin) {
    struct user_fd *descriptor = get_descriptor(number);
    if (!descriptor) return -USER_EBADF;
    if (descriptor->directory) {
        if (origin != UNITAS_SEEK_SET || offset != 0) return -USER_EINVAL;
        descriptor->directory_index = 0;
        return 0;
    }
    uint64_t next;
    uint64_t current_offset = descriptor->offset;
    if (origin == UNITAS_SEEK_CUR &&
        vfs_tell(descriptor->vfs_handle, &current_offset) < 0)
        return -USER_EBADF;
    if (origin == UNITAS_SEEK_SET) {
        if (offset < 0) return -USER_EINVAL;
        next = (uint64_t)offset;
    } else if (origin == UNITAS_SEEK_CUR) {
        uint64_t magnitude = offset < 0 ? (uint64_t)(-(offset + 1)) + 1 :
                                          (uint64_t)offset;
        if (offset < 0 && magnitude > current_offset)
            return -USER_EINVAL;
        if (offset > 0 && magnitude > UINT64_MAX - current_offset)
            return -USER_EINVAL;
        next = offset < 0 ? current_offset - magnitude :
                            current_offset + magnitude;
    } else if (origin == UNITAS_SEEK_END) {
        struct vfs_stat info;
        if (vfs_fstat(descriptor->vfs_handle, &info) < 0)
            return -USER_EBADF;
        uint64_t base = info.size;
        uint64_t magnitude = offset < 0 ? (uint64_t)(-(offset + 1)) + 1 :
                                          (uint64_t)offset;
        if ((offset < 0 && magnitude > base) ||
            (offset > 0 && magnitude > UINT64_MAX - base))
            return -USER_EINVAL;
        next = offset < 0 ? base - magnitude : base + magnitude;
    } else {
        return -USER_ENOSYS;
    }
    if (vfs_seek(descriptor->vfs_handle, next) < 0) return -USER_EINVAL;
    descriptor->offset = next;
    return (long)next;
}

static long syscall_brk(uint64_t requested) {
    if (!requested) return (long)current_space.heap_break;
    if (requested < current_space.heap_start || requested > current_space.heap_limit)
        return -USER_ENOMEM;
    uint64_t first = ALIGN_UP(current_space.heap_break, PAGE_SIZE);
    uint64_t last = ALIGN_UP(requested, PAGE_SIZE);
    for (uint64_t page = first; page < last; page += PAGE_SIZE)
        if (!map_user_page(page, true, false)) return -USER_ENOMEM;
    current_space.heap_break = requested;
    return (long)requested;
}

/* Return Linux dirent64 records from the VFS directory cursor. */
static long syscall_getdents64(uint64_t number, uint64_t address,
                               uint64_t length) {
    struct user_fd *descriptor = get_descriptor(number);
    if (!descriptor || !descriptor->directory) return -USER_EBADF;
    if (length > USER_WRITE_LIMIT || !validate_user_range(address, (size_t)length, true))
        return -USER_EFAULT;
    size_t copied = 0;
    while (copied < length) {
        struct vfs_dirent entry;
        int result = vfs_readdir(descriptor->path, descriptor->directory_index,
                                 &entry);
        if (result < 0) return copied ? (long)copied : -USER_EINVAL;
        if (!result) break;
        size_t name_length = strlen(entry.name);
        size_t record_length =
            ALIGN_UP(offsetof(struct unitas_dirent64, name) + name_length + 1, 8);
        if (record_length > length - copied)
            return copied ? (long)copied : -USER_EINVAL;
        uint8_t record[280] = {0};
        struct unitas_dirent64 *directory_entry = (void *)record;
        directory_entry->inode = descriptor->directory_index + 1;
        directory_entry->next_offset = (int64_t)(descriptor->directory_index + 1);
        directory_entry->record_length = (uint16_t)record_length;
        directory_entry->type = entry.type ? UNITAS_DT_DIRECTORY : UNITAS_DT_UNKNOWN;
        memcpy(directory_entry->name, entry.name, name_length + 1);
        if (!copy_to_user_image(address + copied, record, record_length))
            return copied ? (long)copied : -USER_EFAULT;
        copied += record_length;
        ++descriptor->directory_index;
    }
    return (long)copied;
}

static void set_uts_field(char field[UNITAS_UTSNAME_LENGTH], const char *text) {
    size_t length = strlen(text);
    if (length >= UNITAS_UTSNAME_LENGTH) length = UNITAS_UTSNAME_LENGTH - 1;
    memcpy(field, text, length);
    field[length] = 0;
}

static long syscall_uname(uint64_t address) {
    if (!validate_user_range(address, sizeof(struct unitas_utsname), true))
        return -USER_EFAULT;
    struct unitas_utsname name = {0};
    set_uts_field(name.system, "UnitasOS");
    set_uts_field(name.node, "unitas");
    set_uts_field(name.release, "0.1");
    set_uts_field(name.version, "development");
    set_uts_field(name.machine, "x86_64");
    set_uts_field(name.domain, "localdomain");
    return copy_to_user_image(address, &name, sizeof(name)) ? 0 : -USER_EFAULT;
}

static long syscall_unlink(uint64_t path_address) {
    char path[256];
    if (!user_path(path_address, path)) return -USER_EFAULT;
    if (strlen(path) >= 5 && memcmp(path, "/dev/", 5) == 0)
        return -USER_EACCES;
    return vfs_unlink(path) == 0 ? 0 : -USER_ENOENT;
}

static void to_linux_stat(const struct vfs_stat *source,
                          struct unitas_linux_stat *target) {
    memset(target, 0, sizeof(*target));
    target->device = source->device;
    target->inode = source->inode;
    target->links = source->links;
    target->mode = source->mode;
    target->uid = source->uid;
    target->gid = source->gid;
    target->special_device = source->special_device;
    target->size = (int64_t)source->size;
    target->block_size = (int64_t)source->block_size;
    target->blocks = (int64_t)source->blocks;
    target->access_seconds = source->access_seconds;
    target->access_nanoseconds = source->access_nanoseconds;
    target->modify_seconds = source->modify_seconds;
    target->modify_nanoseconds = source->modify_nanoseconds;
    target->change_seconds = source->change_seconds;
    target->change_nanoseconds = source->change_nanoseconds;
}

static long linux_stat_normalized(const char *path, uint64_t result_address) {
    if (!validate_user_range(result_address, sizeof(struct unitas_linux_stat),
                             true)) return -USER_EFAULT;
    struct vfs_stat result;
    if (vfs_stat(path, &result) < 0) return -USER_ENOENT;
    struct unitas_linux_stat output;
    to_linux_stat(&result, &output);
    return copy_to_user_image(result_address, &output, sizeof(output)) ?
           0 : -USER_EFAULT;
}

/* Convert VFS metadata to the Linux x86-64 stat layout. */
static long linux_stat_path_at(uint64_t directory, uint64_t path_address,
                               uint64_t result_address) {
    char path[256];
    if (!user_path_at(directory, path_address, path)) {
        if ((int64_t)directory != AT_FDCWD) {
            struct user_fd *base = get_descriptor(directory);
            if (!base) return -USER_EBADF;
            if (!base->directory) return -USER_ENOTDIR;
        }
        return -USER_EFAULT;
    }
    return linux_stat_normalized(path, result_address);
}

static long linux_stat_path(uint64_t path_address, uint64_t result_address) {
    return linux_stat_path_at(AT_FDCWD, path_address,
                              result_address);
}

static long linux_fchdir(uint64_t descriptor_number) {
    struct user_fd *descriptor = get_descriptor(descriptor_number);
    if (!descriptor) return -USER_EBADF;
    if (!descriptor->directory) return -USER_ENOTDIR;
    memcpy(current_space.cwd, descriptor->path, strlen(descriptor->path) + 1);
    return 0;
}

static long linux_mkdirat(uint64_t directory, uint64_t path_address,
                          uint64_t mode) {
    char path[256];
    if (!user_path_at(directory, path_address, path)) {
        if ((int64_t)directory != AT_FDCWD) {
            struct user_fd *base = get_descriptor(directory);
            if (!base) return -USER_EBADF;
            if (!base->directory) return -USER_ENOTDIR;
        }
        return -USER_EFAULT;
    }
    if (strlen(path) >= 5 && memcmp(path, "/dev/", 5) == 0)
        return -USER_EACCES;
    if (vfs_mkdir(path, (uint32_t)mode & ~current_space.file_mask) == 0)
        return 0;
    struct vfs_stat existing;
    return vfs_stat(path, &existing) == 0 ? -USER_EEXIST : -USER_ENOENT;
}

static long linux_unlinkat(uint64_t directory, uint64_t path_address,
                           uint64_t flags) {
    char path[256];
    if (!user_path_at(directory, path_address, path)) {
        if ((int64_t)directory != AT_FDCWD) {
            struct user_fd *base = get_descriptor(directory);
            if (!base) return -USER_EBADF;
            if (!base->directory) return -USER_ENOTDIR;
        }
        return -USER_EFAULT;
    }
    if (strlen(path) >= 5 && memcmp(path, "/dev/", 5) == 0)
        return -USER_EACCES;
    if (flags == 0) return vfs_unlink(path) == 0 ? 0 : -USER_ENOENT;
    if (flags == 0x200) return vfs_rmdir(path) == 0 ? 0 : -USER_ENOTEMPTY;
    return -USER_EINVAL;
}

static long linux_fcntl(uint64_t descriptor_number, uint64_t command,
                       uint64_t argument) {
    struct user_fd *descriptor = get_descriptor(descriptor_number);
    if (!descriptor) return -USER_EBADF;
    switch (command) {
    case 0:
    case 1030: {
        long duplicate = duplicate_user_descriptor(descriptor_number, argument,
                                                   0, false);
        if (duplicate >= 0 && command == 1030)
            current_space.descriptors[duplicate].close_on_exec = true;
        return duplicate;
    }
    case 1:
        return descriptor->close_on_exec ? 1 : 0;
    case 2:
        if (argument & ~1ULL) return -USER_EINVAL;
        descriptor->close_on_exec = (argument & 1) != 0;
        return 0;
    case 3: {
        uint64_t flags = descriptor->status_flags & 3;
        bool append;
        if (vfs_is_append(descriptor->vfs_handle, &append) == 0 && append)
            flags |= 0x400;
        return (long)flags;
    }
    case 4:
        if (argument & ~(3ULL | 0x400 | 0x800)) return -USER_EINVAL;
        if (vfs_set_append(descriptor->vfs_handle,
                           (argument & 0x400) != 0) < 0)
            return -USER_EBADF;
        descriptor->append = (argument & 0x400) != 0;
        descriptor->status_flags = (uint32_t)(argument & 3);
        return 0;
    default:
        return -USER_EINVAL;
    }
}

static long linux_fstat(uint64_t descriptor_number, uint64_t result_address) {
    struct user_fd *descriptor = get_descriptor(descriptor_number);
    if (!descriptor) return -USER_EBADF;
    if (!validate_user_range(result_address, sizeof(struct unitas_linux_stat),
                             true)) return -USER_EFAULT;
    struct vfs_stat result;
    int status = descriptor->directory ? vfs_stat(descriptor->path, &result) :
                                        vfs_fstat(descriptor->vfs_handle, &result);
    if (status < 0) return -USER_EBADF;
    struct unitas_linux_stat output;
    to_linux_stat(&result, &output);
    return copy_to_user_image(result_address, &output, sizeof(output)) ?
           0 : -USER_EFAULT;
}

static long linux_ftruncate(uint64_t descriptor_number, uint64_t size) {
    struct user_fd *descriptor = get_descriptor(descriptor_number);
    if (!descriptor) return -USER_EBADF;
    if (!descriptor->writable) return -USER_EBADF;
    if (descriptor->directory) return -USER_EISDIR;
    if ((int64_t)size < 0) return -USER_EINVAL;
    return vfs_truncate(descriptor->vfs_handle, size) == 0 ? 0 : -USER_EINVAL;
}

static long linux_sync(uint64_t descriptor_number, bool data_only) {
    struct user_fd *descriptor = get_descriptor(descriptor_number);
    if (!descriptor) return -USER_EBADF;
    if (descriptor->directory) return -USER_EISDIR;
    return vfs_sync(descriptor->vfs_handle, data_only) == 0 ? 0 : -USER_EINVAL;
}

static long linux_getcwd(uint64_t output_address, uint64_t size) {
    size_t length = strlen(current_space.cwd) + 1;
    if (length > size) return -USER_EINVAL;
    if (!validate_user_range(output_address, length, true)) return -USER_EFAULT;
    return copy_to_user_image(output_address, current_space.cwd, length) ?
           (long)length : -USER_EFAULT;
}

static long linux_chdir(uint64_t path_address) {
    char path[256];
    if (!user_path(path_address, path)) return -USER_EFAULT;
    struct vfs_stat result;
    if (vfs_stat(path, &result) < 0) return -USER_ENOENT;
    if ((result.mode & S_IFMT) != S_IFDIR)
        return -USER_ENOTDIR;
    memcpy(current_space.cwd, path, strlen(path) + 1);
    return 0;
}

static long linux_mkdir(uint64_t path_address, uint64_t mode) {
    char path[256];
    if (!user_path(path_address, path)) return -USER_EFAULT;
    if (strlen(path) >= 5 && memcmp(path, "/dev/", 5) == 0)
        return -USER_EACCES;
    if (vfs_mkdir(path, (uint32_t)mode & ~current_space.file_mask) == 0)
        return 0;
    struct vfs_stat existing;
    return vfs_stat(path, &existing) == 0 ? -USER_EEXIST : -USER_ENOENT;
}

static long linux_rmdir(uint64_t path_address) {
    char path[256];
    if (!user_path(path_address, path)) return -USER_EFAULT;
    if (strlen(path) >= 5 && memcmp(path, "/dev/", 5) == 0)
        return -USER_EACCES;
    if (vfs_rmdir(path) == 0) return 0;
    struct vfs_stat result;
    return vfs_stat(path, &result) == 0 ? -USER_ENOTEMPTY : -USER_ENOENT;
}

static long linux_ioctl(uint64_t descriptor_number, uint64_t request,
                        uint64_t argument) {
    struct user_fd *descriptor = get_descriptor(descriptor_number);
    if (!descriptor) return -USER_EBADF;
    if (strcmp(descriptor->path, "/dev/console") != 0)
        return -USER_ENOTTY;
    if (request == USER_IOCTL_TCGETS) {
        if (!validate_user_range(argument, sizeof(current_space.terminal), true))
            return -USER_EFAULT;
        return copy_to_user_image(argument, &current_space.terminal,
                                  sizeof(current_space.terminal)) ? 0 :
               -USER_EFAULT;
    }
    if (request == 0x5402 || request == 0x5403 || request == 0x5404) {
        if (!validate_user_range(argument, sizeof(current_space.terminal), false))
            return -USER_EFAULT;
        memcpy(&current_space.terminal, (const void *)(uintptr_t)argument,
               sizeof(current_space.terminal));
        return 0;
    }
    if (request == USER_IOCTL_TIOCGWINSZ) {
        struct linux_winsize { uint16_t rows, columns, x_pixels, y_pixels; };
        const struct linux_winsize size = {25, 80, 0, 0};
        if (!validate_user_range(argument, sizeof(size), true))
            return -USER_EFAULT;
        return copy_to_user_image(argument, &size, sizeof(size)) ?
               0 : -USER_EFAULT;
    }
    if (request == USER_IOCTL_FIONREAD) {
        int available = 0;
        if (!validate_user_range(argument, sizeof(available), true))
            return -USER_EFAULT;
        return copy_to_user_image(argument, &available, sizeof(available)) ?
               0 : -USER_EFAULT;
    }
    return -USER_ENOTTY;
}

static long linux_renameat(uint64_t source_directory, uint64_t source_address,
                           uint64_t destination_directory,
                           uint64_t destination_address,
                           bool no_replace) {
    char source[256], destination[256];
    if (!user_path_at(source_directory, source_address, source)) {
        if ((int64_t)source_directory != AT_FDCWD) {
            struct user_fd *base = get_descriptor(source_directory);
            if (!base) return -USER_EBADF;
            if (!base->directory) return -USER_ENOTDIR;
        }
        return -USER_EFAULT;
    }
    if (!user_path_at(destination_directory, destination_address, destination)) {
        if ((int64_t)destination_directory != AT_FDCWD) {
            struct user_fd *base = get_descriptor(destination_directory);
            if (!base) return -USER_EBADF;
            if (!base->directory) return -USER_ENOTDIR;
        }
        return -USER_EFAULT;
    }
    if ((strlen(source) >= 5 && memcmp(source, "/dev/", 5) == 0) ||
        (strlen(destination) >= 5 &&
         memcmp(destination, "/dev/", 5) == 0))
        return -USER_EACCES;
    if (strcmp(source, destination) == 0) return 0;
    if (no_replace) {
        struct vfs_stat existing;
        if (vfs_stat(destination, &existing) == 0) return -USER_EEXIST;
    }
    return vfs_rename(source, destination) == 0 ? 0 : -USER_ENOENT;
}

static long linux_access(const char *path, uint64_t mode) {
    if (mode & ~7ULL) return -USER_EINVAL;
    struct vfs_stat result;
    if (vfs_stat(path, &result) < 0) return -USER_ENOENT;
    if ((mode & 1) && !(result.mode & 0111)) return -USER_EACCES;
    return 0;
}

static long linux_truncate(uint64_t path_address, uint64_t size) {
    char path[256];
    if (!user_path(path_address, path)) return -USER_EFAULT;
    if ((int64_t)size < 0) return -USER_EINVAL;
    long descriptor = user_open_path(path, UNITAS_O_WRONLY);
    if (descriptor < 0) return descriptor;
    int handle = current_space.descriptors[descriptor].vfs_handle;
    int status = vfs_truncate(handle, size);
    close_user_descriptor((uint64_t)descriptor);
    return status == 0 ? 0 : -USER_EINVAL;
}

static bool cpu_has_rdrand(void) {
    uint32_t eax = 1, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (ecx & (1u << 30)) != 0;
}

static bool hardware_random_word(uint64_t *value) {
    if (!cpu_has_rdrand()) return false;
    for (unsigned attempt = 0; attempt < 10; ++attempt) {
        unsigned char ready;
        uint64_t word;
        __asm__ volatile("rdrand %0; setc %1" : "=r"(word), "=qm"(ready));
        if (ready) {
            *value = word;
            return true;
        }
    }
    return false;
}

/* Fill random output from RDRAND. Return ENOSYS if the CPU has no RDRAND. */
static long linux_getrandom(uint64_t address, uint64_t length, uint64_t flags) {
    if (flags & ~3ULL) return -USER_EINVAL;
    if (length > USER_WRITE_LIMIT || !validate_user_range(address,
            (size_t)length, true)) return -USER_EFAULT;
    size_t copied = 0;
    while (copied < length) {
        uint64_t value;
        if (!hardware_random_word(&value))
            return copied ? (long)copied : -USER_ENOSYS;
        size_t amount = (size_t)(length - copied);
        if (amount > sizeof(value)) amount = sizeof(value);
        if (!copy_to_user_image(address + copied, &value, amount))
            return copied ? (long)copied : -USER_EFAULT;
        copied += amount;
    }
    return (long)copied;
}

static long linux_clock_gettime(uint64_t clock_id, uint64_t output_address) {
    struct { int64_t seconds; int64_t nanoseconds; } value;
    uint64_t ticks = timer_ticks();
    if (clock_id == 0 || clock_id == 5) {
        if (rtc_read_unix_seconds(&value.seconds) != 0) return -USER_EINVAL;
        value.nanoseconds = (int64_t)(ticks % 100) * 10000000;
    } else if (clock_id == 1 || clock_id == 6) {
        value.seconds = (int64_t)(ticks / 100);
        value.nanoseconds = (int64_t)(ticks % 100) * 10000000;
    } else {
        return -USER_EINVAL;
    }
    if (clock_id == 5 || clock_id == 6) value.nanoseconds = 0;
    if (!validate_user_range(output_address, sizeof(value), true))
        return -USER_EFAULT;
    return copy_to_user_image(output_address, &value, sizeof(value)) ?
           0 : -USER_EFAULT;
}

static long linux_gettimeofday(uint64_t output_address) {
    if (!output_address) return 0;
    struct { int64_t seconds; int64_t microseconds; } value;
    if (rtc_read_unix_seconds(&value.seconds) != 0) return -USER_EINVAL;
    value.microseconds = (int64_t)(timer_ticks() % 100) * 10000;
    if (!validate_user_range(output_address, sizeof(value), true))
        return -USER_EFAULT;
    return copy_to_user_image(output_address, &value, sizeof(value)) ?
           0 : -USER_EFAULT;
}

static long linux_nanosleep(uint64_t request_address,
                            uint64_t remaining_address) {
    struct { int64_t seconds; int64_t nanoseconds; } request;
    if (!validate_user_range(request_address, sizeof(request), false))
        return -USER_EFAULT;
    memcpy(&request, (const void *)(uintptr_t)request_address, sizeof(request));
    if (request.seconds < 0 || request.nanoseconds < 0 ||
        request.nanoseconds >= 1000000000) return -USER_EINVAL;
    uint64_t seconds = (uint64_t)request.seconds;
    if (seconds > UINT64_MAX / 100) return -USER_EINVAL;
    uint64_t ticks = seconds * 100 +
        ((uint64_t)request.nanoseconds + 9999999) / 10000000;
    uint64_t start = timer_ticks();
    timer_sleep(ticks);
    if (remaining_address) {
        struct { int64_t seconds; int64_t nanoseconds; } remaining = {0};
        uint64_t elapsed = timer_ticks() - start;
        if (elapsed < ticks) {
            uint64_t left = ticks - elapsed;
            remaining.seconds = (int64_t)(left / 100);
            remaining.nanoseconds = (int64_t)(left % 100) * 10000000;
        }
        if (!validate_user_range(remaining_address, sizeof(remaining), true))
            return -USER_EFAULT;
        if (!copy_to_user_image(remaining_address, &remaining,
                                sizeof(remaining))) return -USER_EFAULT;
    }
    return 0;
}

static long linux_openat(uint64_t directory, uint64_t path, uint64_t flags,
                         uint64_t mode) {
    (void)mode;
    char normalized[256];
    if (!user_path_at(directory, path, normalized)) {
        if ((int64_t)directory != AT_FDCWD) {
            struct user_fd *base = get_descriptor(directory);
            if (!base) return -USER_EBADF;
            if (!base->directory) return -USER_ENOTDIR;
        }
        return -USER_EFAULT;
    }
    const uint64_t allowed = 3 | 0x40 | 0x80 | 0x100 | 0x200 | 0x400 |
        0x800 | 0x10000 | 0x20000 | 0x80000;
    if (flags & ~allowed) return -USER_EINVAL;
    uint32_t unitas_flags = 0;
    if (flags & 1) unitas_flags |= UNITAS_O_WRONLY;
    if (flags & 2) unitas_flags |= UNITAS_O_RDWR;
    if (flags & 0x40) unitas_flags |= UNITAS_O_CREAT;
    if (flags & 0x200) unitas_flags |= UNITAS_O_TRUNC;
    if (flags & 0x80) unitas_flags |= USER_OPEN_EXCLUSIVE;
    if (flags & 0x400) unitas_flags |= USER_OPEN_APPEND;
    if (flags & 0x10000) unitas_flags |= USER_OPEN_DIRECTORY;
    if (flags & 0x80000) unitas_flags |= USER_OPEN_CLOEXEC;
    return user_open_path(normalized, unitas_flags);
}

/* Allocate zero-filled user pages in the process mmap range. */
static long linux_mmap(uint64_t address, uint64_t length, uint64_t protection,
                       uint64_t flags, uint64_t descriptor, uint64_t offset) {
    const uint64_t supported_flags = MAP_SHARED |
        MAP_PRIVATE | MAP_FIXED |
        MAP_ANONYMOUS | MAP_FIXED_NOREPLACE;
    if (!length || length > USER_LIMIT - USER_BASE ||
        (protection & ~(uint64_t)(PROT_READ |
                                 PROT_WRITE |
                                 PROT_EXEC)) ||
        (flags & ~supported_flags) ||
        !!(flags & MAP_SHARED) ==
            !!(flags & MAP_PRIVATE) ||
        !(flags & MAP_ANONYMOUS) || descriptor != UINT64_MAX ||
        offset || ((protection & (PROT_WRITE |
                                  PROT_EXEC)) ==
                   (PROT_WRITE | PROT_EXEC)))
        return -USER_EINVAL;
    uint64_t rounded = ALIGN_UP(length, PAGE_SIZE);
    if (rounded < length || rounded > USER_STACK_START - USER_BASE)
        return -USER_ENOMEM;
    bool fixed = (flags & (MAP_FIXED |
                           MAP_FIXED_NOREPLACE)) != 0;
    if (fixed && (address & PAGE_MASK)) return -USER_EINVAL;
    if (fixed && (address < USER_BASE + 16 * 1024 * 1024 ||
                  address >= USER_STACK_START ||
                  rounded > USER_STACK_START - address)) return -USER_ENOMEM;
    uint64_t selected = fixed ? address : 0;
    if (fixed) {
        for (uint64_t page = selected; page < selected + rounded;
             page += PAGE_SIZE) {
            uint64_t *pte = user_pte(page);
            if (pte && (*pte & PAGE_PRESENT) && !(*pte & PAGE_USER))
                return -USER_ENOMEM;
            if (pte && (*pte & PAGE_PRESENT)) {
                if (flags & MAP_FIXED_NOREPLACE)
                    return -USER_EBUSY;
                break;
            }
        }
        if (flags & MAP_FIXED) {
            for (uint64_t page = selected; page < selected + rounded;
                 page += PAGE_SIZE) {
                uint64_t *pte = user_pte(page);
                if (!pte || !(*pte & PAGE_PRESENT)) continue;
                pmm_free_pages(*pte & PAGE_ADDRESS, 1);
                *pte = 0;
                __asm__ volatile("invlpg (%0)" : : "r"(page) : "memory");
            }
        }
    } else {
        if (current_space.mmap_cursor < rounded + USER_BASE +
                                        16 * 1024 * 1024)
            return -USER_ENOMEM;
        uint64_t candidate = ALIGN_DOWN(current_space.mmap_cursor - rounded,
                                        PAGE_SIZE);
        while (candidate >= USER_BASE + 16 * 1024 * 1024) {
            if (rounded > USER_STACK_START - candidate) {
                if (candidate < USER_BASE + 16 * 1024 * 1024 + PAGE_SIZE)
                    break;
                candidate -= PAGE_SIZE;
                continue;
            }
            uint64_t collision = 0;
            for (uint64_t page = candidate; page < candidate + rounded;
                 page += PAGE_SIZE) {
                uint64_t *pte = user_pte(page);
                if (pte && (*pte & PAGE_PRESENT)) {
                    collision = page;
                    break;
                }
            }
            if (!collision) {
                selected = candidate;
                break;
            }
            if (collision < rounded + USER_BASE + 16 * 1024 * 1024)
                break;
            candidate = ALIGN_DOWN(collision - rounded, PAGE_SIZE);
        }
        if (!selected) return -USER_ENOMEM;
        current_space.mmap_cursor = selected;
    }
    size_t mapped = 0;
    bool writable = (protection & PROT_WRITE) != 0;
    bool executable = (protection & PROT_EXEC) != 0;
    for (uint64_t page = selected; page < selected + rounded;
         page += PAGE_SIZE) {
        if (!map_user_page(page, writable, executable)) break;
        ++mapped;
    }
    if (mapped != rounded / PAGE_SIZE) {
        for (size_t index = 0; index < mapped; ++index) {
            uint64_t page = selected + index * PAGE_SIZE;
            uint64_t *pte = user_pte(page);
            if (pte && (*pte & (PAGE_PRESENT | PAGE_USER)) ==
                       (PAGE_PRESENT | PAGE_USER)) {
                pmm_free_pages(*pte & PAGE_ADDRESS, 1);
                *pte = 0;
                __asm__ volatile("invlpg (%0)" : : "r"(page) : "memory");
            }
        }
        return -USER_ENOMEM;
    }
    if (fixed) {
        for (uint64_t page = selected; page < selected + rounded;
             page += PAGE_SIZE)
            __asm__ volatile("invlpg (%0)" : : "r"(page) : "memory");
    }
    return (long)selected;
}

static long linux_munmap(uint64_t address, uint64_t length) {
    if ((address & PAGE_MASK) || !length || address < USER_BASE + 16 * 1024 * 1024 ||
        address >= USER_STACK_START || length > USER_STACK_START - address)
        return -USER_EINVAL;
    uint64_t end = ALIGN_UP(address + length, PAGE_SIZE);
    if (end < address || end > USER_STACK_START) return -USER_EINVAL;
    for (uint64_t page = address; page < end; page += PAGE_SIZE) {
        uint64_t *pte = user_pte(page);
        if (pte && (*pte & PAGE_PRESENT) && !(*pte & PAGE_USER))
            return -USER_EINVAL;
    }
    for (uint64_t page = address; page < end; page += PAGE_SIZE) {
        uint64_t *pte = user_pte(page);
        if (!pte || !(*pte & PAGE_PRESENT)) continue;
        pmm_free_pages(*pte & PAGE_ADDRESS, 1);
        *pte = 0;
        __asm__ volatile("invlpg (%0)" : : "r"(page) : "memory");
    }
    return 0;
}

static long linux_mprotect(uint64_t address, uint64_t length,
                           uint64_t protection) {
    /* Reject writable and executable pages in the same mapping. */
    if ((address & PAGE_MASK) || !length ||
        address < USER_BASE || address >= USER_STACK_START ||
        length > USER_STACK_START - address ||
        (protection & ~(uint64_t)(PROT_READ |
                                 PROT_WRITE |
                                 PROT_EXEC)) ||
        (protection & (PROT_WRITE | PROT_EXEC)) ==
            (PROT_WRITE | PROT_EXEC))
        return -USER_EINVAL;
    uint64_t end = ALIGN_UP(address + length, PAGE_SIZE);
    if (end < address || end > USER_STACK_START) return -USER_EINVAL;
    for (uint64_t page = address; page < end; page += PAGE_SIZE) {
        uint64_t *pte = user_pte(page);
        if (!pte || (*pte & (PAGE_PRESENT | PAGE_USER)) !=
                    (PAGE_PRESENT | PAGE_USER)) return -USER_ENOMEM;
    }
    for (uint64_t page = address; page < end; page += PAGE_SIZE) {
        uint64_t *pte = user_pte(page);
        if (protection & PROT_WRITE) *pte |= PAGE_WRITE;
        else *pte &= ~PAGE_WRITE;
        if (nx_available) {
            if (protection & PROT_EXEC) *pte &= ~PAGE_NX;
            else *pte |= PAGE_NX;
        }
        __asm__ volatile("invlpg (%0)" : : "r"(page) : "memory");
    }
    return 0;
}

/* Return Linux errors as negative errno values for the entry stub. */
long user_linux_syscall(uint64_t number, uint64_t first, uint64_t second,
                        uint64_t third, uint64_t fourth, uint64_t fifth,
                        uint64_t sixth) {
    switch (number) {
    case __NR_read:
        return syscall_read(first, second, third);
    case __NR_write:
        return syscall_write(first, second, third);
    case __NR_open:
        return linux_openat(AT_FDCWD, first, second, third);
    case __NR_openat:
        return linux_openat(first, second, third, fourth);
    case __NR_close: {
        struct user_fd *descriptor = get_descriptor(first);
        if (!descriptor) return -USER_EBADF;
        if (!descriptor->directory) (void)vfs_close(descriptor->vfs_handle);
        *descriptor = (struct user_fd){0};
        return 0;
    }
    case __NR_lseek:
        return syscall_seek(first, (int64_t)second, third);
    case __NR_brk:
        return syscall_brk(first);
    case __NR_getdents64:
        return syscall_getdents64(first, second, third);
    case __NR_uname:
        return syscall_uname(first);
    case __NR_unlink:
        return syscall_unlink(first);
    case __NR_unlinkat:
        return linux_unlinkat(first, second, third);
    case __NR_rename:
        return linux_renameat(AT_FDCWD, first,
                              AT_FDCWD, second, false);
    case __NR_renameat:
        return linux_renameat(first, second, third, fourth, false);
    case __NR_renameat2:
        if (fifth & ~1ULL) return -USER_EINVAL;
        return linux_renameat(first, second, third, fourth,
                              (fifth & 1) != 0);
    case __NR_exit:
    case __NR_exit_group:
        current_space.exiting = true;
        current_space.exit_status = (int)(first & 0xff);
        user_syscall_exit_requested = 1;
        user_fs_base = 0;
        write_msr(0xc0000100, 0);
        write_cr3(kernel_root);
        return 0;
    case __NR_getpid:
    case __NR_gettid:
    case __NR_set_tid_address:
        return 1;
    case __NR_getuid:
    case __NR_getgid:
    case __NR_geteuid:
    case __NR_getegid:
        return 0;
    case __NR_set_robust_list:
        return second == sizeof(uint64_t) * 3 ? 0 : -USER_EINVAL;
    case __NR_arch_prctl:
        if (first == ARCH_SET_FS) {
            if (second && !validate_user_range(second, 1, false))
                return -USER_EFAULT;
            user_fs_base = second;
            write_msr(0xc0000100, second);
            return 0;
        }
        if (first == ARCH_GET_FS) {
            if (!validate_user_range(second, sizeof(user_fs_base), true))
                return -USER_EFAULT;
            return copy_to_user_image(second, &user_fs_base,
                                      sizeof(user_fs_base)) ? 0 : -USER_EFAULT;
        }
        return -USER_EINVAL;
    case __NR_mmap:
        return linux_mmap(first, second, third, fourth, fifth, sixth);
    case __NR_stat:
    case __NR_lstat:
        return linux_stat_path(first, second);
    case __NR_fstat:
        return linux_fstat(first, second);
    case __NR_newfstatat:
        if (fourth & ~(uint64_t)AT_SYMLINK_NOFOLLOW)
            return -USER_ENOSYS;
        return linux_stat_path_at(first, second, third);
    case __NR_getcwd:
        return linux_getcwd(first, second);
    case __NR_chdir:
        return linux_chdir(first);
    case __NR_fchdir:
        return linux_fchdir(first);
    case __NR_mkdir:
        return linux_mkdir(first, second);
    case __NR_mkdirat:
        return linux_mkdirat(first, second, third);
    case __NR_rmdir:
        return linux_rmdir(first);
    case __NR_access: {
        char path[256];
        if (!user_path(first, path)) return -USER_EFAULT;
        return linux_access(path, second);
    }
    case __NR_faccessat: {
        if (fourth & ~(uint64_t)(USER_AT_EACCESS |
                                USER_AT_SYMLINK_NOFOLLOW))
            return -USER_EINVAL;
        char path[256];
        if (!user_path_at(first, second, path)) {
            if ((int64_t)first != AT_FDCWD) {
                struct user_fd *base = get_descriptor(first);
                if (!base) return -USER_EBADF;
                if (!base->directory) return -USER_ENOTDIR;
            }
            return -USER_EFAULT;
        }
        return linux_access(path, third);
    }
    case __NR_clock_gettime:
        return linux_clock_gettime(first, second);
    case __NR_clock_getres:
        if (first != 0 && first != 1 && first != 5 && first != 6)
            return -USER_EINVAL;
        if (third) {
            struct { int64_t seconds; int64_t nanoseconds; } resolution = {
                first >= 5 ? 1 : 0, first >= 5 ? 0 : 10000000
            };
            if (!validate_user_range(third, sizeof(resolution), true))
                return -USER_EFAULT;
            if (!copy_to_user_image(third, &resolution, sizeof(resolution)))
                return -USER_EFAULT;
        }
        return 0;
    case __NR_gettimeofday:
        return linux_gettimeofday(first);
    case __NR_nanosleep:
        return linux_nanosleep(first, second);
    case __NR_clock_nanosleep:
        if (second != 0 || (int64_t)first < 0 || first > 1)
            return -USER_EINVAL;
        return linux_nanosleep(third, fourth);
    case __NR_time: {
        int64_t current_time;
        if (rtc_read_unix_seconds(&current_time) != 0) return -USER_EINVAL;
        if (first && (!validate_user_range(first, sizeof(current_time), true) ||
                      !copy_to_user_image(first, &current_time,
                                          sizeof(current_time))))
            return -USER_EFAULT;
        return (long)current_time;
    }
    case __NR_getrandom:
        return linux_getrandom(first, second, third);
    case __NR_reboot: {
        /* Validate the Linux ABI tokens before changing machine power state. */
        uint32_t command = (uint32_t)third;
        if (first != LINUX_REBOOT_MAGIC1 ||
            (second != LINUX_REBOOT_MAGIC2 &&
             second != LINUX_REBOOT_MAGIC2A &&
             second != LINUX_REBOOT_MAGIC2B &&
             second != LINUX_REBOOT_MAGIC2C))
            return -USER_EINVAL;
        if (command != LINUX_REBOOT_CMD_RESTART &&
            command != LINUX_REBOOT_CMD_HALT &&
            command != LINUX_REBOOT_CMD_POWER_OFF)
            return -USER_ENOSYS;
        /* User IDs are not implemented. Any process can request a power change. */
        if (block_flush_all() != 0) return -USER_EIO;
        if (command == LINUX_REBOOT_CMD_RESTART)
            return acpi_reboot() == 0 ? 0 : -USER_EIO;
        if (command == LINUX_REBOOT_CMD_HALT) {
            cpu_disable_interrupts();
            for (;;) cpu_halt();
        }
        return acpi_poweroff() == 0 ? 0 : -USER_EIO;
    }
    case __NR_ioctl:
        return linux_ioctl(first, second, third);
    case __NR_truncate:
        return linux_truncate(first, second);
    case __NR_ftruncate:
        return linux_ftruncate(first, second);
    case __NR_fsync:
        return linux_sync(first, false);
    case __NR_fdatasync:
        return linux_sync(first, true);
    case __NR_umask: {
        uint32_t previous = current_space.file_mask;
        current_space.file_mask = (uint32_t)first & 0777;
        return previous;
    }
    case __NR_dup:
        return duplicate_user_descriptor(first, 0, 0, false);
    case __NR_dup2:
        return duplicate_user_descriptor(first, 0, second, true);
    case __NR_fcntl:
        return linux_fcntl(first, second, third);
    case __NR_mprotect:
        return linux_mprotect(first, second, third);
    case __NR_munmap:
        return linux_munmap(first, second);
    case __NR_fork:
    case __NR_wait4:
        return -USER_ENOSYS;
    case __NR_execve:
        return linux_execve(first, second, third);
    default:
        return -USER_ENOSYS;
    }
}

/* Dispatch Unitas calls sent through the dedicated interrupt vector. */
int user_syscall_dispatch(struct user_frame *frame) {
    if (!frame || !current_space.active) return 1;
    uint64_t number = frame->rax;
    long result;
    switch (number) {
    case UNITAS_SYS_EXIT:
        current_space.exiting = true;
        current_space.exit_status = (int)(frame->rdi & 0xff);
        user_fs_base = 0;
        write_msr(0xc0000100, 0);
        write_cr3(kernel_root);
        return 1;
    case UNITAS_SYS_WRITE:
        result = syscall_write(frame->rdi, frame->rsi, frame->rdx);
        break;
    case UNITAS_SYS_READ:
        result = syscall_read(frame->rdi, frame->rsi, frame->rdx);
        break;
    case UNITAS_SYS_OPEN:
        result = user_open(frame->rdi, frame->rsi);
        break;
    case UNITAS_SYS_CLOSE: {
        struct user_fd *descriptor = get_descriptor(frame->rdi);
        if (!descriptor) result = -USER_EBADF;
        else {
            if (!descriptor->directory) (void)vfs_close(descriptor->vfs_handle);
            *descriptor = (struct user_fd){0};
            result = 0;
        }
        break;
    }
    case UNITAS_SYS_LSEEK:
        result = syscall_seek(frame->rdi, (int64_t)frame->rsi, frame->rdx);
        break;
    case UNITAS_SYS_BRK:
        result = syscall_brk(frame->rdi);
        break;
    case UNITAS_SYS_GETDENTS64:
        result = syscall_getdents64(frame->rdi, frame->rsi, frame->rdx);
        break;
    case UNITAS_SYS_UNAME:
        result = syscall_uname(frame->rdi);
        break;
    case UNITAS_SYS_UNLINK:
        result = syscall_unlink(frame->rdi);
        break;
    default:
        result = -USER_ENOSYS;
        break;
    }
    frame->rax = (uint64_t)result;
    return 0;
}

int user_handle_exception(uint64_t vector, uint64_t rip) {
    if (!current_space.active) return 0;
    user_fs_base = 0;
    write_msr(0xc0000100, 0);
    log_write(LOG_ERROR, "user process fault: vector=%llu rip=0x%llx\n",
              (unsigned long long)vector, (unsigned long long)rip);
    current_space.exiting = true;
    current_space.exit_status = 128 + (int)vector;
    write_cr3(kernel_root);
    return 1;
}

static bool userland_is_installed(void) {
    static const char *const paths[] = {
        "/bin/hello.elf", "/bin/reboot", "/bin/poweroff", "/bin/shutdown",
        "/bin/sh"
    };
    for (size_t index = 0; index < sizeof(paths) / sizeof(paths[0]); ++index) {
        int handle;
        if (vfs_open(paths[index], 0, &handle) < 0) return false;
        (void)vfs_close(handle);
    }
    return true;
}

int userland_seed(void) {
    /* An installed read-only root already has these images. */
    if (userland_is_installed()) return 0;
    size_t length = (size_t)(unitas_hello_elf_end - unitas_hello_elf_start);
    if (!length || length > INT_MAX) return -1;
    int handle;
    if (vfs_open("/bin/HELLO.ELF", VFS_OPEN_CREATE | VFS_OPEN_TRUNCATE,
                 &handle) < 0) return -1;
    int written = vfs_write(handle, unitas_hello_elf_start, length);
    (void)vfs_close(handle);
    if (written != (int)length) return -1;
    for (uint64_t index = 0; index < unitas_program_count; ++index) {
        const struct embedded_program *program = &unitas_program_table[index];
        size_t name_length = strlen(program->name);
        size_t program_length = (size_t)(program->end - program->start);
        if (!name_length || name_length > 240 || !program_length ||
            program_length > INT_MAX) return -1;
        char path[256] = "/bin/";
        memcpy(path + 5, program->name, name_length + 1);
        if (vfs_open(path, VFS_OPEN_CREATE | VFS_OPEN_TRUNCATE, &handle) < 0)
            return -1;
        written = vfs_write(handle, program->start, program_length);
        (void)vfs_close(handle);
        if (written != (int)program_length) return -1;
    }
    return 0;
}
