#include <kern/block.h>
#include <kern/io.h>
#include <kern/log.h>
#include <kern/pci.h>
#include <kern/storage.h>
#include <kern/string.h>

#define AHCI_MAX_PORTS 32
#define AHCI_HBA_CAP 0x00
#define AHCI_HBA_GHC 0x04
#define AHCI_HBA_PI 0x0c
#define AHCI_HBA_CAP2 0x24
#define AHCI_PORT_BASE 0x100
#define AHCI_PORT_STRIDE 0x80
#define AHCI_PX_CLB 0x00
#define AHCI_PX_CLBU 0x04
#define AHCI_PX_FB 0x08
#define AHCI_PX_FBU 0x0c
#define AHCI_PX_IS 0x10
#define AHCI_PX_IE 0x14
#define AHCI_PX_CMD 0x18
#define AHCI_PX_TFD 0x20
#define AHCI_PX_SIG 0x24
#define AHCI_PX_SSTS 0x28
#define AHCI_PX_SERR 0x30
#define AHCI_PX_CI 0x38
#define AHCI_CMD_ST (1u << 0)
#define AHCI_CMD_FRE (1u << 4)
#define AHCI_CMD_FR (1u << 14)
#define AHCI_CMD_CR (1u << 15)
#define ATA_STATUS_ERR 0x01
#define ATA_STATUS_DF 0x20

struct ahci_dma_area {
    uint8_t command_list[4096];
    uint8_t received_fis[4096];
    uint8_t command_table[4096];
    uint8_t data[4096];
};

struct ahci_port {
    volatile uint8_t *registers;
    struct ahci_dma_area *dma;
    uint64_t sectors;
    bool lba48;
    struct block_device block;
};

struct ahci_prdt { uint32_t data_base; uint32_t data_base_upper; uint32_t reserved; uint32_t byte_count; }
    __attribute__((packed));
struct ahci_command_header {
    uint16_t flags;
    uint16_t prdt_length;
    uint32_t transferred;
    uint32_t table_base;
    uint32_t table_base_upper;
    uint32_t reserved[4];
} __attribute__((packed));
struct ahci_command_table {
    uint8_t command_fis[64];
    uint8_t atapi_command[16];
    uint8_t reserved[48];
    struct ahci_prdt prdt;
} __attribute__((packed));

static struct ahci_dma_area dma_pool[AHCI_MAX_PORTS] __attribute__((aligned(4096)));
static struct ahci_port ports[AHCI_MAX_PORTS];
static volatile uint8_t *hba;
static unsigned port_slots;
static unsigned registered_count;

static uint32_t mmio_read(volatile uint8_t *base, uint32_t offset) {
    return *(volatile uint32_t *)(base + offset);
}
static void mmio_write(volatile uint8_t *base, uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(base + offset) = value;
}
static volatile uint8_t *port_registers(unsigned index) {
    return hba + AHCI_PORT_BASE + index * AHCI_PORT_STRIDE;
}

static int wait_clear(volatile uint8_t *registers, uint32_t offset, uint32_t mask) {
    for (uint32_t spin = 0; spin < 10000000; ++spin)
        if (!(mmio_read(registers, offset) & mask)) return 0;
    return -1;
}

static int stop_port(volatile uint8_t *registers) {
    uint32_t command = mmio_read(registers, AHCI_PX_CMD);
    command &= ~(AHCI_CMD_ST | AHCI_CMD_FRE);
    mmio_write(registers, AHCI_PX_CMD, command);
    if (wait_clear(registers, AHCI_PX_CMD, AHCI_CMD_CR) != 0) return -1;
    return wait_clear(registers, AHCI_PX_CMD, AHCI_CMD_FR);
}

static void start_port(volatile uint8_t *registers) {
    uint32_t command = mmio_read(registers, AHCI_PX_CMD);
    command |= AHCI_CMD_FRE;
    mmio_write(registers, AHCI_PX_CMD, command);
    command |= AHCI_CMD_ST;
    mmio_write(registers, AHCI_PX_CMD, command);
}

static int submit_command(struct ahci_port *port, const uint8_t fis[20],
                          bool write, size_t byte_count) {
    struct ahci_command_header *headers = (void *)port->dma->command_list;
    struct ahci_command_table *table = (void *)port->dma->command_table;
    memset(&headers[0], 0, sizeof(headers[0]));
    memset(table, 0, sizeof(*table));
    memcpy(table->command_fis, fis, 20);
    headers[0].flags = 5 | (write ? (1u << 6) : 0);
    headers[0].prdt_length = byte_count ? 1 : 0;
    if (byte_count) {
        table->prdt.data_base = (uint32_t)(uintptr_t)port->dma->data;
        table->prdt.data_base_upper = 0;
        table->prdt.byte_count = (uint32_t)(byte_count - 1) | (1u << 31);
    }
    headers[0].table_base = (uint32_t)(uintptr_t)table;
    headers[0].table_base_upper = 0;
    mmio_write(port->registers, AHCI_PX_IS, 0xffffffff);
    mmio_write(port->registers, AHCI_PX_CI, 1);
    for (uint32_t spin = 0; spin < 10000000; ++spin) {
        uint32_t task = mmio_read(port->registers, AHCI_PX_TFD);
        if (task & (ATA_STATUS_ERR | ATA_STATUS_DF)) return -1;
        if (!(mmio_read(port->registers, AHCI_PX_CI) & 1)) return 0;
    }
    return -1;
}

static void make_fis(uint8_t fis[20], uint8_t command, uint64_t lba,
                     uint16_t count) {
    memset(fis, 0, 20);
    fis[0] = 0x27;
    fis[1] = 1u << 7;
    fis[2] = command;
    fis[4] = (uint8_t)lba;
    fis[5] = (uint8_t)(lba >> 8);
    fis[6] = (uint8_t)(lba >> 16);
    fis[7] = 1u << 6;
    fis[8] = (uint8_t)(lba >> 24);
    fis[9] = (uint8_t)(lba >> 32);
    fis[10] = (uint8_t)(lba >> 40);
    fis[12] = (uint8_t)count;
    fis[13] = (uint8_t)(count >> 8);
}

static int transfer_one(struct ahci_port *port, uint64_t lba,
                        void *buffer, bool write) {
    if (lba >= port->sectors) return -1;
    if (!port->lba48 && lba >= (1ULL << 28)) return -1;
    if (write) memcpy(port->dma->data, buffer, BLOCK_SECTOR_SIZE);
    uint8_t fis[20];
    make_fis(fis, port->lba48 ? (write ? 0x35 : 0x25) : (write ? 0xca : 0xc8), lba, 1);
    if (!port->lba48) fis[7] |= (uint8_t)((lba >> 24) & 0x0f);
    if (submit_command(port, fis, write, BLOCK_SECTOR_SIZE) != 0) return -1;
    if (!write) memcpy(buffer, port->dma->data, BLOCK_SECTOR_SIZE);
    return 0;
}

static int ahci_read(struct block_device *device, uint64_t lba, uint32_t count,
                     void *buffer) {
    struct ahci_port *port = device->private_data;
    for (uint32_t i = 0; i < count; ++i)
        if (transfer_one(port, lba + i,
                         (uint8_t *)buffer + (size_t)i * BLOCK_SECTOR_SIZE,
                         false) != 0) return -1;
    return 0;
}

static int ahci_write(struct block_device *device, uint64_t lba, uint32_t count,
                      const void *buffer) {
    struct ahci_port *port = device->private_data;
    for (uint32_t i = 0; i < count; ++i)
        if (transfer_one(port, lba + i,
                         (uint8_t *)buffer + (size_t)i * BLOCK_SECTOR_SIZE,
                         true) != 0) return -1;
    return 0;
}

static int ahci_flush(struct block_device *device) {
    struct ahci_port *port = device->private_data;
    uint8_t fis[20];
    make_fis(fis, 0xea, 0, 0);
    return submit_command(port, fis, false, 0);
}

static bool identify_port(struct ahci_port *port) {
    uint32_t status = mmio_read(port->registers, AHCI_PX_SSTS);
    if ((status & 0x0f) != 3 || ((status >> 8) & 0x0f) != 1 ||
        mmio_read(port->registers, AHCI_PX_SIG) != 0x00000101) return false;
    if (stop_port(port->registers) != 0) return false;
    port->dma = &dma_pool[port - ports];
    uintptr_t list = (uintptr_t)port->dma->command_list;
    uintptr_t fis = (uintptr_t)port->dma->received_fis;
    mmio_write(port->registers, AHCI_PX_CLB, (uint32_t)list);
    mmio_write(port->registers, AHCI_PX_CLBU, 0);
    mmio_write(port->registers, AHCI_PX_FB, (uint32_t)fis);
    mmio_write(port->registers, AHCI_PX_FBU, 0);
    mmio_write(port->registers, AHCI_PX_SERR, 0xffffffff);
    mmio_write(port->registers, AHCI_PX_IS, 0xffffffff);
    start_port(port->registers);

    uint8_t command_fis[20];
    uint16_t identify[256];
    make_fis(command_fis, 0xec, 0, 0);
    if (submit_command(port, command_fis, false, sizeof(identify)) != 0) return false;
    memcpy(identify, port->dma->data, sizeof(identify));
    if (!(identify[49] & (1u << 9))) return false;
    port->lba48 = (identify[83] & 0xc400) == 0x4400;
    if (port->lba48) {
        port->sectors = (uint64_t)identify[100] |
            ((uint64_t)identify[101] << 16) |
            ((uint64_t)identify[102] << 32) |
            ((uint64_t)identify[103] << 48);
    } else {
        port->sectors = (uint32_t)identify[60] | ((uint32_t)identify[61] << 16);
    }
    return port->sectors != 0;
}

struct ahci_scan { unsigned found; };
static void find_controller(const struct pci_device *device, void *context) {
    (void)context;
    if (hba || device->class_code != 1 || device->subclass != 6 ||
        device->programming_interface != 1) return;
    bool io = false;
    uint64_t address = pci_bar_address(device, 5, &io, 0);
    if (!address || io || address >= 0x100000000ULL) return;
    pci_enable_command(device, (1u << 1) | (1u << 2));
    hba = (volatile uint8_t *)(uintptr_t)address;
    uint32_t ghc = mmio_read(hba, AHCI_HBA_GHC);
    mmio_write(hba, AHCI_HBA_GHC, (ghc | (1u << 31)) & ~(1u << 1));
    port_slots = (mmio_read(hba, AHCI_HBA_CAP) & 0x1f) + 1;
    if (port_slots > AHCI_MAX_PORTS) port_slots = AHCI_MAX_PORTS;
    if (mmio_read(hba, AHCI_HBA_CAP2) & 1) {
        uint32_t bohc = mmio_read(hba, 0x28);
        mmio_write(hba, 0x28, bohc | (1u << 1));
        for (uint32_t spin = 0; spin < 10000000; ++spin) {
            if (!(mmio_read(hba, 0x28) & (1u << 4))) break;
            if (spin == 9999999) hba = 0;
        }
        if (!hba) return;
    }
}

int ahci_init(void) {
    pci_enumerate(find_controller, 0);
    if (!hba) return 0;
    uint32_t implemented = mmio_read(hba, AHCI_HBA_PI);
    for (unsigned i = 0; i < port_slots; ++i) {
        if (!(implemented & (1u << i))) continue;
        struct ahci_port *port = &ports[i];
        port->registers = port_registers(i);
        if (!identify_port(port)) continue;
        port->block.name[0] = 's'; port->block.name[1] = 'a';
        port->block.name[2] = 't'; port->block.name[3] = 'a';
        unsigned number = registered_count;
        unsigned digits = 4;
        do {
            port->block.name[digits++] = (char)('0' + number % 10);
            number /= 10;
        } while (number && digits < BLOCK_NAME_MAX - 1);
        for (unsigned left = 4, right = digits - 1; left < right; ++left, --right) {
            char temporary = port->block.name[left];
            port->block.name[left] = port->block.name[right];
            port->block.name[right] = temporary;
        }
        port->block.name[digits] = 0;
        port->block.sector_count = port->sectors;
        port->block.private_data = port;
        port->block.read = ahci_read;
        port->block.write = ahci_write;
        port->block.flush = ahci_flush;
        if (block_register(&port->block) == 0) ++registered_count;
    }
    if (registered_count) log_write(LOG_INFO, "AHCI registered %u SATA disk(s)\n", registered_count);
    return 0;
}
