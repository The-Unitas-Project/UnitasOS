#include <kern/e1000.h>
#include <kern/log.h>
#include <kern/mm.h>
#include <kern/network.h>
#include <kern/pci.h>
#include <kern/string.h>

/* This driver targets the 82540EM and uses DMA memory below 4 GiB. */
#define E1000_VENDOR_INTEL 0x8086
#define E1000_DEVICE_82540EM 0x100e
#define E1000_RX_COUNT 32
#define E1000_TX_COUNT 8
#define E1000_DMA_LIMIT 0x100000000ULL
#define E1000_MMIO_BYTES 0x20000

#define E1000_CTRL 0x0000
#define E1000_STATUS 0x0008
#define E1000_IMC 0x00d8
#define E1000_RCTL 0x0100
#define E1000_TCTL 0x0400
#define E1000_TIPG 0x0410
#define E1000_RDBAL 0x2800
#define E1000_RDBAH 0x2804
#define E1000_RDLEN 0x2808
#define E1000_RDH 0x2810
#define E1000_RDT 0x2818
#define E1000_TDBAL 0x3800
#define E1000_TDBAH 0x3804
#define E1000_TDLEN 0x3808
#define E1000_TDH 0x3810
#define E1000_TDT 0x3818
#define E1000_RAL 0x5400
#define E1000_RAH 0x5404
#define E1000_MTA 0x5200

#define E1000_CTRL_RST (1u << 26)
#define E1000_CTRL_SLU (1u << 6)
#define E1000_STATUS_LU (1u << 1)
#define E1000_RCTL_EN (1u << 1)
#define E1000_RCTL_BAM (1u << 15)
#define E1000_RCTL_SECRC (1u << 26)
#define E1000_TCTL_EN (1u << 1)
#define E1000_TCTL_PSP (1u << 3)
#define E1000_TX_DD 0x01
#define E1000_TX_CMD_EOP 0x01
#define E1000_TX_CMD_IFCS 0x02
#define E1000_TX_CMD_RS 0x08
#define E1000_RX_DD 0x01
#define E1000_RX_EOP 0x02

struct e1000_rx_descriptor {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_descriptor {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} __attribute__((packed));

static volatile uint32_t *registers;
static struct e1000_rx_descriptor *receive_ring;
static struct e1000_tx_descriptor *transmit_ring;
static uint8_t *receive_buffers;
static uint8_t *transmit_buffers;
static uint8_t local_mac[6];
static uint8_t receive_index;
static uint8_t transmit_index;
static bool initialized;

static uint32_t mmio_read(uint32_t offset) {
    return registers[offset / sizeof(uint32_t)];
}

static void mmio_write(uint32_t offset, uint32_t value) {
    registers[offset / sizeof(uint32_t)] = value;
    (void)registers[E1000_STATUS / sizeof(uint32_t)];
}

static void *allocate_dma_pages(size_t count) {
    phys_addr_t address = pmm_alloc_pages(count, E1000_DMA_LIMIT);
    if (!address) return 0;
    void *memory = (void *)(uintptr_t)address;
    memset(memory, 0, count * PAGE_SIZE);
    return memory;
}

static bool find_controller(const struct pci_device *device) {
    return device->vendor_id == E1000_VENDOR_INTEL &&
           device->device_id == E1000_DEVICE_82540EM;
}

static int reset_controller(void) {
    uint32_t control = mmio_read(E1000_CTRL);
    mmio_write(E1000_CTRL, control | E1000_CTRL_RST);
    for (size_t attempt = 0; attempt < 1000000; ++attempt) {
        if (!(mmio_read(E1000_CTRL) & E1000_CTRL_RST)) return 0;
        __asm__ volatile("pause");
    }
    return -1;
}

static int read_mac_address(uint8_t address[6]) {
    uint32_t low = mmio_read(E1000_RAL);
    uint32_t high = mmio_read(E1000_RAH);
    if (!(high & (1u << 31))) return -1;
    for (size_t index = 0; index < 4; ++index)
        address[index] = (uint8_t)(low >> (index * 8));
    address[4] = (uint8_t)high;
    address[5] = (uint8_t)(high >> 8);
    bool all_zero = true;
    bool all_ones = true;
    for (size_t index = 0; index < 6; ++index) {
        all_zero &= address[index] == 0;
        all_ones &= address[index] == 0xff;
    }
    return all_zero || all_ones ? -1 : 0;
}

static bool initialize_rings(void) {
    /* The controller reads these rings and buffers without CPU copies. */
    receive_ring = allocate_dma_pages(1);
    transmit_ring = allocate_dma_pages(1);
    receive_buffers = allocate_dma_pages(E1000_RX_COUNT);
    transmit_buffers = allocate_dma_pages(E1000_TX_COUNT);
    if (!receive_ring || !transmit_ring || !receive_buffers || !transmit_buffers) {
        if (receive_ring) pmm_free_pages((uintptr_t)receive_ring, 1);
        if (transmit_ring) pmm_free_pages((uintptr_t)transmit_ring, 1);
        if (receive_buffers)
            pmm_free_pages((uintptr_t)receive_buffers, E1000_RX_COUNT);
        if (transmit_buffers)
            pmm_free_pages((uintptr_t)transmit_buffers, E1000_TX_COUNT);
        receive_ring = 0;
        transmit_ring = 0;
        receive_buffers = 0;
        transmit_buffers = 0;
        return false;
    }

    for (size_t index = 0; index < E1000_RX_COUNT; ++index)
        receive_ring[index].address =
            (uintptr_t)(receive_buffers + index * PAGE_SIZE);
    for (size_t index = 0; index < E1000_TX_COUNT; ++index) {
        transmit_ring[index].address =
            (uintptr_t)(transmit_buffers + index * PAGE_SIZE);
        transmit_ring[index].status = E1000_TX_DD;
    }

    uintptr_t rx_address = (uintptr_t)receive_ring;
    uintptr_t tx_address = (uintptr_t)transmit_ring;
    mmio_write(E1000_RDBAL, (uint32_t)rx_address);
    mmio_write(E1000_RDBAH, (uint32_t)(rx_address >> 32));
    mmio_write(E1000_RDLEN,
               E1000_RX_COUNT * sizeof(struct e1000_rx_descriptor));
    mmio_write(E1000_RDH, 0);
    mmio_write(E1000_RDT, E1000_RX_COUNT - 1);
    mmio_write(E1000_TDBAL, (uint32_t)tx_address);
    mmio_write(E1000_TDBAH, (uint32_t)(tx_address >> 32));
    mmio_write(E1000_TDLEN,
               E1000_TX_COUNT * sizeof(struct e1000_tx_descriptor));
    mmio_write(E1000_TDH, 0);
    mmio_write(E1000_TDT, 0);
    receive_index = 0;
    transmit_index = 0;
    return true;
}

static void probe_device(const struct pci_device *device, void *context) {
    bool *found = context;
    if (*found || !find_controller(device)) return;
    bool io_bar = false;
    bool bar64 = false;
    uint64_t bar = pci_bar_address(device, 0, &io_bar, &bar64);
    (void)bar64;
    if (!bar || io_bar || bar > E1000_DMA_LIMIT - E1000_MMIO_BYTES)
        return;

    pci_enable_command(device, (1u << 1) | (1u << 2));
    registers = (volatile uint32_t *)(uintptr_t)bar;
    mmio_write(E1000_IMC, UINT32_MAX);
    if (reset_controller() != 0) return;
    mmio_write(E1000_IMC, UINT32_MAX);

    uint8_t mac[6];
    if (read_mac_address(mac) != 0 || !initialize_rings()) return;

    for (uint32_t offset = 0; offset < 0x200; offset += 4)
        mmio_write(E1000_MTA + offset, 0);
    mmio_write(E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
    mmio_write(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP |
                           (0x10u << 4) | (0x40u << 12));
    mmio_write(E1000_TIPG, 0x0060200a);
    mmio_write(E1000_CTRL, mmio_read(E1000_CTRL) | E1000_CTRL_SLU);

    memcpy(local_mac, mac, sizeof(local_mac));
    initialized = true;
    *found = true;
    network_set_link(mac, (mmio_read(E1000_STATUS) & E1000_STATUS_LU) != 0);
    log_write(LOG_INFO, "E1000 network device initialized at %02x:%02x:%02x:%02x:%02x:%02x\n",
              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

int e1000_init(void) {
    initialized = false;
    registers = 0;
    bool found = false;
    pci_enumerate(probe_device, &found);
    return found ? 0 : -1;
}

void e1000_poll(void) {
    if (!initialized) return;
    /* Handle each frame before the controller can reuse its receive buffer. */
    network_set_link(local_mac,
                     (mmio_read(E1000_STATUS) & E1000_STATUS_LU) != 0);
    for (size_t handled = 0; handled < E1000_RX_COUNT; ++handled) {
        struct e1000_rx_descriptor *descriptor = &receive_ring[receive_index];
        if (!(descriptor->status & E1000_RX_DD)) break;
        if ((descriptor->status & E1000_RX_EOP) && !descriptor->errors &&
            descriptor->length >= 14 && descriptor->length <= 1518)
            network_receive(receive_buffers + receive_index * PAGE_SIZE,
                            descriptor->length);
        descriptor->status = 0;
        descriptor->length = 0;
        __sync_synchronize();
        mmio_write(E1000_RDT, receive_index);
        receive_index = (uint8_t)((receive_index + 1) % E1000_RX_COUNT);
    }
}

int e1000_send(const void *frame, size_t length) {
    if (!initialized || !frame || length < 14 || length > 1518) return -1;
    struct e1000_tx_descriptor *descriptor = &transmit_ring[transmit_index];
    for (size_t attempt = 0; attempt < 1000000; ++attempt) {
        if (descriptor->status & E1000_TX_DD) break;
        __asm__ volatile("pause");
        if (attempt + 1 == 1000000) return -1;
    }

    uint8_t *buffer = transmit_buffers + transmit_index * PAGE_SIZE;
    memcpy(buffer, frame, length);
    descriptor->length = (uint16_t)length;
    descriptor->checksum_offset = 0;
    descriptor->checksum_start = 0;
    descriptor->special = 0;
    descriptor->command = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    descriptor->status = 0;
    __sync_synchronize();
    transmit_index = (uint8_t)((transmit_index + 1) % E1000_TX_COUNT);
    mmio_write(E1000_TDT, transmit_index);
    return 0;
}
