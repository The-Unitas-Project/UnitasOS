#include <kern/block.h>
#include <kern/log.h>
#include <kern/pci.h>
#include <kern/storage.h>
#include <kern/string.h>

#define NVME_QUEUE_CAPACITY 16
#define NVME_REGISTER_CAP 0x00
#define NVME_REGISTER_CC 0x14
#define NVME_REGISTER_CSTS 0x1c
#define NVME_REGISTER_AQA 0x24
#define NVME_REGISTER_ASQ 0x28
#define NVME_REGISTER_ACQ 0x30
#define NVME_DOORBELL_BASE 0x1000

struct nvme_command {
    uint32_t command_id_opcode;
    uint32_t namespace_id;
    uint32_t reserved[2];
    uint64_t metadata_pointer;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t command_dword[6];
};

struct nvme_completion {
    uint32_t result;
    uint32_t reserved;
    uint32_t submission_head_queue;
    uint32_t command_id_status;
};

struct nvme_queue {
    struct nvme_command *submission;
    volatile struct nvme_completion *completion;
    volatile uint32_t *submission_doorbell;
    volatile uint32_t *completion_doorbell;
    uint16_t depth;
    uint16_t submission_tail;
    uint16_t completion_head;
    uint8_t completion_phase;
    uint16_t next_command_id;
    bool failed;
};

struct nvme_device {
    struct nvme_queue admin;
    struct nvme_queue io;
    uint64_t sectors;
    uint32_t namespace_id;
    uint8_t *data_page;
    struct block_device block;
};

static struct nvme_command admin_submission[NVME_QUEUE_CAPACITY]
    __attribute__((aligned(4096)));
static struct nvme_completion admin_completion[NVME_QUEUE_CAPACITY]
    __attribute__((aligned(4096)));
static struct nvme_command io_submission[NVME_QUEUE_CAPACITY]
    __attribute__((aligned(4096)));
static struct nvme_completion io_completion[NVME_QUEUE_CAPACITY]
    __attribute__((aligned(4096)));
static uint8_t io_data_page[4096] __attribute__((aligned(4096)));
static uint8_t identify_page[4096] __attribute__((aligned(4096)));
static struct nvme_device controller;
static volatile uint8_t *registers;
static uint32_t doorbell_stride;
static unsigned registered_count;

static uint32_t read_register32(uint32_t offset) {
    return *(volatile uint32_t *)(registers + offset);
}
static void write_register32(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(registers + offset) = value;
}
static uint64_t read_register64(uint32_t offset) {
    return *(volatile uint64_t *)(registers + offset);
}
static void write_register64(uint32_t offset, uint64_t value) {
    *(volatile uint64_t *)(registers + offset) = value;
}

static int wait_ready(bool ready) {
    for (uint32_t spin = 0; spin < 100000000; ++spin) {
        if (((read_register32(NVME_REGISTER_CSTS) & 1) != 0) == ready) return 0;
    }
    return -1;
}

static void set_doorbells(struct nvme_queue *queue, unsigned queue_id) {
    uint32_t offset = NVME_DOORBELL_BASE + queue_id * 2 * doorbell_stride;
    queue->submission_doorbell = (volatile uint32_t *)(registers + offset);
    queue->completion_doorbell = (volatile uint32_t *)(registers + offset + doorbell_stride);
}

static void queue_init(struct nvme_queue *queue, struct nvme_command *submission,
                       volatile struct nvme_completion *completion,
                       uint16_t depth, unsigned queue_id) {
    memset(submission, 0, sizeof(*submission) * NVME_QUEUE_CAPACITY);
    memset((void *)completion, 0, sizeof(*completion) * NVME_QUEUE_CAPACITY);
    queue->submission = submission;
    queue->completion = completion;
    queue->depth = depth;
    queue->submission_tail = 0;
    queue->completion_head = 0;
    queue->completion_phase = 1;
    queue->next_command_id = 1;
    queue->failed = false;
    set_doorbells(queue, queue_id);
}

static int submit(struct nvme_queue *queue, struct nvme_command *command,
                  uint32_t *result) {
    if (queue->failed) return -1;
    uint16_t command_id = queue->next_command_id++;
    command->command_id_opcode = (command->command_id_opcode & 0xffffu) |
                                 ((uint32_t)command_id << 16);
    queue->submission[queue->submission_tail] = *command;
    __asm__ volatile("mfence" ::: "memory");
    queue->submission_tail = (queue->submission_tail + 1) % queue->depth;
    *queue->submission_doorbell = queue->submission_tail;

    for (uint32_t spin = 0; spin < 100000000; ++spin) {
        volatile struct nvme_completion *completion =
            &queue->completion[queue->completion_head];
        uint32_t status = completion->command_id_status;
        if (((status >> 16) & 1) != queue->completion_phase) continue;
        __asm__ volatile("mfence" ::: "memory");
        uint16_t completed_id = (uint16_t)status;
        if (completed_id != command_id) {
            log_write(LOG_WARN, "NVMe completion ID mismatch. Expected %u got %u\n",
                      command_id, completed_id);
            queue->failed = true;
            return -1;
        }
        int error = (status >> 17) & 0x7fff;
        if (result) *result = completion->result;
        queue->completion_head = (queue->completion_head + 1) % queue->depth;
        if (!queue->completion_head) queue->completion_phase ^= 1;
        *queue->completion_doorbell = queue->completion_head;
        if (error)
            log_write(LOG_WARN, "NVMe command failed with completion status 0x%x\n",
                      (unsigned)error);
        return error ? -1 : 0;
    }
    log_write(LOG_WARN, "NVMe command completion timed out\n");
    queue->failed = true;
    return -1;
}

static int admin_command(uint8_t opcode, uint32_t namespace_id,
                         uint64_t prp1, uint32_t cdw10, uint32_t cdw11,
                         uint32_t *result) {
    struct nvme_command command = {0};
    command.command_id_opcode = opcode;
    command.namespace_id = namespace_id;
    command.prp1 = prp1;
    command.command_dword[0] = cdw10;
    command.command_dword[1] = cdw11;
    return submit(&controller.admin, &command, result);
}

static int identify(uint32_t namespace_id, uint32_t selector) {
    memset(identify_page, 0, sizeof(identify_page));
    return admin_command(0x06, namespace_id,
                         (uintptr_t)identify_page, selector, 0, 0);
}

static int create_io_queues(void) {
    uint64_t completion_address = (uintptr_t)io_completion;
    uint64_t submission_address = (uintptr_t)io_submission;
    if (admin_command(0x05, 0, completion_address,
                      ((uint32_t)(controller.io.depth - 1) << 16) | 1,
                      1, 0) != 0) {
        log_write(LOG_WARN, "NVMe create completion queue command failed\n");
        return -1;
    }
    if (admin_command(0x01, 0, submission_address,
                      ((uint32_t)(controller.io.depth - 1) << 16) | 1,
                      ((uint32_t)1 << 16) | 1, 0) != 0) {
        log_write(LOG_WARN, "NVMe create submission queue command failed\n");
        return -1;
    }
    return 0;
}

static int nvme_transfer(struct nvme_device *device, uint64_t lba,
                         uint32_t count, void *buffer, bool write) {
    while (count) {
        if (device->io.failed) return -1;
        uint32_t page_sectors = 4096 / BLOCK_SECTOR_SIZE;
        uint32_t chunk = count > page_sectors ? page_sectors : count;
        if (write) memcpy(device->data_page, buffer, (size_t)chunk * BLOCK_SECTOR_SIZE);
        struct nvme_command command = {0};
        command.command_id_opcode = write ? 0x01 : 0x02;
        command.namespace_id = device->namespace_id;
        command.prp1 = (uintptr_t)device->data_page;
        command.command_dword[0] = (uint32_t)lba;
        command.command_dword[1] = (uint32_t)(lba >> 32);
        command.command_dword[2] = chunk - 1;
        if (submit(&device->io, &command, 0) != 0) return -1;
        if (!write) memcpy(buffer, device->data_page,
                           (size_t)chunk * BLOCK_SECTOR_SIZE);
        lba += chunk;
        count -= chunk;
        buffer = (uint8_t *)buffer + (size_t)chunk * BLOCK_SECTOR_SIZE;
    }
    return 0;
}

static int nvme_read(struct block_device *block, uint64_t lba,
                     uint32_t count, void *buffer) {
    return nvme_transfer(block->private_data, lba, count, buffer, false);
}

static int nvme_write(struct block_device *block, uint64_t lba,
                      uint32_t count, const void *buffer) {
    return nvme_transfer(block->private_data, lba, count, (void *)buffer, true);
}

static int nvme_flush(struct block_device *block) {
    struct nvme_device *device = block->private_data;
    struct nvme_command command = {0};
    command.command_id_opcode = 0x00;
    command.namespace_id = device->namespace_id;
    return submit(&device->io, &command, 0);
}

struct nvme_scan { bool initialized; };
static void find_nvme(const struct pci_device *device, void *context) {
    struct nvme_scan *scan = context;
    if (scan->initialized || device->class_code != 1 || device->subclass != 8 ||
        device->programming_interface != 2) return;
    bool io = false, wide = false;
    uint64_t address = pci_bar_address(device, 0, &io, &wide);
    if (!address || io || address >= 0x100000000ULL) return;
    log_write(LOG_INFO, "NVMe controller detected at 0x%llx\n",
              (unsigned long long)address);
    if (wide) {
        bool ignored_io = false;
        bool ignored_wide = false;
        (void)pci_bar_address(device, 1, &ignored_io, &ignored_wide);
    }
    pci_enable_command(device, (1u << 1) | (1u << 2));
    registers = (volatile uint8_t *)(uintptr_t)address;
    uint64_t capability = read_register64(NVME_REGISTER_CAP);
    uint32_t maximum_entries = (uint32_t)(capability & 0xffff) + 1;
    uint16_t depth = maximum_entries < NVME_QUEUE_CAPACITY ?
                     (uint16_t)maximum_entries : NVME_QUEUE_CAPACITY;
    if (depth < 2) { registers = 0; return; }
    doorbell_stride = 4u << ((capability >> 32) & 0x0f);

    uint32_t configuration = read_register32(NVME_REGISTER_CC);
    if (configuration & 1) {
        write_register32(NVME_REGISTER_CC, configuration & ~1u);
        if (wait_ready(false) != 0) { registers = 0; return; }
    }
    if ((capability >> 48) & 0x0f) { registers = 0; return; }
    write_register32(0x0c, 0xffffffff);
    memset(admin_submission, 0, sizeof(admin_submission));
    memset(admin_completion, 0, sizeof(admin_completion));
    write_register32(NVME_REGISTER_AQA,
                     ((uint32_t)(depth - 1) << 16) | (depth - 1));
    write_register64(NVME_REGISTER_ASQ, (uintptr_t)admin_submission);
    write_register64(NVME_REGISTER_ACQ, (uintptr_t)admin_completion);
    queue_init(&controller.admin, admin_submission, admin_completion, depth, 0);
    uint32_t new_configuration = 1u | (6u << 16) | (4u << 20);
    write_register32(NVME_REGISTER_CC, new_configuration);
    if (wait_ready(true) != 0) {
        log_write(LOG_WARN, "NVMe controller did not become ready\n");
        registers = 0;
        return;
    }

    controller.io.depth = depth;
    queue_init(&controller.io, io_submission, io_completion, depth, 1);
    if (identify(0, 1) != 0) {
        log_write(LOG_WARN, "NVMe controller identify command failed\n");
        registers = 0;
        return;
    }
    if (create_io_queues() != 0) {
        log_write(LOG_WARN, "NVMe I/O queue creation failed\n");
        registers = 0;
        return;
    }
    uint32_t namespace_count = *(uint32_t *)(identify_page + 516);
    if (!namespace_count || identify(1, 0) != 0) {
        log_write(LOG_WARN, "NVMe namespace identify command failed\n");
        registers = 0;
        return;
    }
    controller.namespace_id = 1;
    controller.sectors = *(uint64_t *)identify_page;
    uint8_t format_index = identify_page[26] & 0x0f;
    uint8_t last_format_index = identify_page[25] & 0x0f;
    uint8_t sector_shift = identify_page[130 + format_index * 4];
    if (!controller.sectors || format_index > last_format_index || sector_shift != 9) {
        log_write(LOG_WARN, "NVMe namespace uses an unsupported sector size\n");
        registers = 0;
        return;
    }
    controller.data_page = io_data_page;
    memcpy(controller.block.name, "nvme0n1", sizeof("nvme0n1"));
    controller.block.sector_count = controller.sectors;
    controller.block.private_data = &controller;
    controller.block.read = nvme_read;
    controller.block.write = nvme_write;
    controller.block.flush = nvme_flush;
    if (block_register(&controller.block) == 0) {
        ++registered_count;
        scan->initialized = true;
    }
}

int nvme_init(void) {
    struct nvme_scan scan = { false };
    pci_enumerate(find_nvme, &scan);
    if (registered_count) log_write(LOG_INFO, "NVMe registered %u namespace(s)\n", registered_count);
    return 0;
}
