#include <kern/acpi.h>
#include <kern/io.h>
#include <kern/string.h>
#include <kern/timer.h>
#include <kern/types.h>

/* ACPI power control uses legacy PM1 I/O ports. */
#define ACPI_ADDRESS_LIMIT 0x100000000ULL
#define ACPI_TABLE_LIMIT (1024u * 1024u)
#define ACPI_HEADER_SIZE 36u
#define ACPI_PM1_SLP_TYP_MASK (7u << 10)
#define ACPI_PM1_SLP_EN (1u << 13)
#define ACPI_PM1_SCI_EN 1u

struct acpi_register {
    uint16_t port;
    bool valid;
};

struct acpi_reset_register {
    uint16_t port;
    uint8_t value;
    bool valid;
};

static struct acpi_register pm1a_control;
static struct acpi_register pm1b_control;
static struct acpi_reset_register reset_register;
static uint8_t s5_type_a;
static uint8_t s5_type_b;
static uint16_t smi_command_port;
static uint8_t acpi_enable_command;
static bool acpi_ready;

static uint16_t read_le16(const uint8_t *data) {
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint64_t read_le64(const uint8_t *data) {
    return (uint64_t)read_le32(data) | ((uint64_t)read_le32(data + 4) << 32);
}

static bool checksum_valid(const uint8_t *data, size_t length) {
    uint8_t sum = 0;
    for (size_t index = 0; index < length; ++index) sum += data[index];
    return sum == 0;
}

static bool valid_rsdp(const uint8_t *rsdp, size_t available,
                       size_t *validated_size) {
    if (!rsdp || available < 20 || memcmp(rsdp, "RSD PTR ", 8) != 0 ||
        !checksum_valid(rsdp, 20)) return false;
    size_t length = 20;
    if (rsdp[15] >= 2) {
        if (available < 36) return false;
        uint32_t extended_length = read_le32(rsdp + 20);
        if (extended_length < 36 || extended_length > 4096 ||
            extended_length > available ||
            !checksum_valid(rsdp, extended_length)) return false;
        length = extended_length;
    }
    *validated_size = length;
    return true;
}

static const uint8_t *find_rsdp(const void *candidate, size_t candidate_size,
                                size_t *rsdp_size) {
    size_t length;
    if (valid_rsdp(candidate, candidate_size, &length)) {
        *rsdp_size = length;
        return candidate;
    }

    for (uintptr_t address = 0x000e0000; address + 36 <= 0x00100000;
         address += 16) {
        const uint8_t *rsdp = (const uint8_t *)address;
        if (valid_rsdp(rsdp, 0x00100000 - address, &length)) {
            *rsdp_size = length;
            return rsdp;
        }
    }
    return 0;
}

static const uint8_t *valid_table(uint64_t address, const char signature[4],
                                  size_t *length_out) {
    if (!address || address > ACPI_ADDRESS_LIMIT - ACPI_HEADER_SIZE) return 0;
    const uint8_t *table = (const uint8_t *)(uintptr_t)address;
    uint32_t length = read_le32(table + 4);
    if (length < ACPI_HEADER_SIZE || length > ACPI_TABLE_LIMIT ||
        length > ACPI_ADDRESS_LIMIT - address ||
        memcmp(table, signature, 4) != 0 || !checksum_valid(table, length))
        return 0;
    *length_out = length;
    return table;
}

static const uint8_t *find_fadt(uint64_t root_address, bool extended) {
    size_t root_length;
    const uint8_t *root = valid_table(root_address,
                                      extended ? "XSDT" : "RSDT",
                                      &root_length);
    size_t entry_size = extended ? 8 : 4;
    if (!root || (root_length - ACPI_HEADER_SIZE) % entry_size != 0) return 0;
    size_t count = (root_length - ACPI_HEADER_SIZE) / entry_size;
    for (size_t index = 0; index < count; ++index) {
        const uint8_t *entry = root + ACPI_HEADER_SIZE + index * entry_size;
        uint64_t address = extended ? read_le64(entry) : read_le32(entry);
        size_t table_length;
        const uint8_t *table = valid_table(address, "FACP", &table_length);
        if (table && table_length >= 116) return table;
    }
    return 0;
}

static bool gas_control_register(const uint8_t *gas,
                                 struct acpi_register *result) {
    uint8_t address_space = gas[0];
    uint8_t bit_width = gas[1];
    uint8_t bit_offset = gas[2];
    uint8_t access_size = gas[3];
    uint64_t address = read_le64(gas + 4);
    if (!address || address_space != 1 || bit_width != 16 ||
        bit_offset != 0 || (access_size != 0 && access_size != 2) ||
        address > UINT16_MAX - 1) return false;
    result->port = (uint16_t)address;
    result->valid = true;
    return true;
}

static bool set_control_register(const uint8_t *fadt, size_t fadt_length,
                                 size_t extended_offset, size_t legacy_offset,
                                 struct acpi_register *result) {
    if (fadt_length >= extended_offset + 12 &&
        read_le64(fadt + extended_offset + 4) != 0)
        return gas_control_register(fadt + extended_offset, result);

    uint32_t legacy_address = read_le32(fadt + legacy_offset);
    uint8_t control_length = fadt[89];
    if (!legacy_address) return true;
    if (control_length < 2 || legacy_address > UINT16_MAX - 1) return false;
    result->port = (uint16_t)legacy_address;
    result->valid = true;
    return true;
}

static bool read_aml_integer(const uint8_t *aml, size_t end, size_t *cursor,
                             uint64_t *value) {
    if (*cursor >= end) return false;
    uint8_t opcode = aml[(*cursor)++];
    if (opcode == 0x00) { *value = 0; return true; }
    if (opcode == 0x01) { *value = 1; return true; }
    if (opcode == 0xff) { *value = UINT64_MAX; return true; }
    size_t width;
    if (opcode == 0x0a) width = 1;
    else if (opcode == 0x0b) width = 2;
    else if (opcode == 0x0c) width = 4;
    else if (opcode == 0x0e) width = 8;
    else return false;
    if (width > end - *cursor) return false;
    *value = width == 1 ? aml[*cursor] :
             width == 2 ? read_le16(aml + *cursor) :
             width == 4 ? read_le32(aml + *cursor) :
                          read_le64(aml + *cursor);
    *cursor += width;
    return true;
}

static bool package_length(const uint8_t *aml, size_t end, size_t *cursor,
                           size_t *package_end) {
    /* ACPI Machine Language (AML) package lengths include this field. */
    size_t start = *cursor;
    if (start >= end) return false;
    uint8_t lead = aml[(*cursor)++];
    unsigned follow = lead >> 6;
    uint64_t length = follow ? lead & 0x0f : lead & 0x3f;
    if (follow > 3 || follow > end - *cursor) return false;
    for (unsigned index = 0; index < follow; ++index)
        length |= (uint64_t)aml[(*cursor)++] << (4 + index * 8);
    if (length < *cursor - start || length > end - start) return false;
    *package_end = start + (size_t)length;
    return true;
}

/* Read sleep types from the _S5_ package. This code does not interpret all AML. */
static bool find_s5(const uint8_t *dsdt, size_t dsdt_length,
                    uint8_t *type_a, uint8_t *type_b) {
    for (size_t index = ACPI_HEADER_SIZE; index + 7 <= dsdt_length; ++index) {
        if (dsdt[index] != 0x08) continue;
        size_t cursor = index + 1;
        if (dsdt[cursor] == 0x5c) ++cursor;
        if (cursor + 4 > dsdt_length ||
            memcmp(dsdt + cursor, "_S5_", 4) != 0) continue;
        cursor += 4;
        if (cursor >= dsdt_length || dsdt[cursor++] != 0x12) continue;
        size_t package_end;
        if (!package_length(dsdt, dsdt_length, &cursor, &package_end) ||
            cursor >= package_end) continue;
        uint8_t elements = dsdt[cursor++];
        uint64_t value_a, value_b;
        if (elements < 2 ||
            !read_aml_integer(dsdt, package_end, &cursor, &value_a) ||
            !read_aml_integer(dsdt, package_end, &cursor, &value_b) ||
            value_a > 7 || value_b > 7) continue;
        *type_a = (uint8_t)value_a;
        *type_b = (uint8_t)value_b;
        return true;
    }
    return false;
}

static bool set_reset_register(const uint8_t *fadt, size_t fadt_length) {
    /* Use reset I/O only when the Fixed ACPI Description Table marks it supported. */
    if (fadt_length < 129 || !(read_le32(fadt + 112) & (1u << 10)))
        return false;
    const uint8_t *gas = fadt + 116;
    uint64_t address = read_le64(gas + 4);
    if (!address || gas[1] != 8 || gas[2] != 0 || gas[3] > 1 ||
        gas[0] != 1 || address > UINT16_MAX) return false;
    reset_register.port = (uint16_t)address;
    reset_register.value = fadt[128];
    reset_register.valid = true;
    return true;
}

/* Validate the ACPI tables before caching any hardware control ports. */
int acpi_init(const void *rsdp_candidate, size_t rsdp_candidate_size) {
    acpi_ready = false;
    pm1a_control = (struct acpi_register){0};
    pm1b_control = (struct acpi_register){0};
    reset_register = (struct acpi_reset_register){0};

    size_t rsdp_size = 0;
    const uint8_t *rsdp = find_rsdp(rsdp_candidate, rsdp_candidate_size,
                                    &rsdp_size);
    if (!rsdp || rsdp_size < 20) return -1;
    uint64_t root_address = read_le32(rsdp + 16);
    bool extended_root = false;
    if (rsdp[15] >= 2) {
        uint64_t xsdt_address = read_le64(rsdp + 24);
        size_t root_length;
        if (xsdt_address && valid_table(xsdt_address, "XSDT", &root_length)) {
            (void)root_length;
            root_address = xsdt_address;
            extended_root = true;
        }
    }
    const uint8_t *fadt = find_fadt(root_address, extended_root);
    if (!fadt && extended_root)
        fadt = find_fadt(read_le32(rsdp + 16), false);
    if (!fadt) return -1;

    size_t fadt_length = read_le32(fadt + 4);
    (void)set_reset_register(fadt, fadt_length);
    if (read_le32(fadt + 112) & (1u << 20)) return -1;
    uint64_t dsdt_address = read_le32(fadt + 40);
    if (fadt_length >= 148 && read_le64(fadt + 140))
        dsdt_address = read_le64(fadt + 140);
    size_t dsdt_length;
    const uint8_t *dsdt = valid_table(dsdt_address, "DSDT", &dsdt_length);
    if (!dsdt || !find_s5(dsdt, dsdt_length, &s5_type_a, &s5_type_b)) return -1;

    if (!set_control_register(fadt, fadt_length, 172, 64, &pm1a_control) ||
        !pm1a_control.valid ||
        !set_control_register(fadt, fadt_length, 184, 68, &pm1b_control))
        return -1;
    uint32_t smi_port = read_le32(fadt + 48);
    smi_command_port = smi_port <= UINT16_MAX ? (uint16_t)smi_port : 0;
    acpi_enable_command = fadt[52];
    acpi_ready = true;
    return 0;
}

static uint16_t read_control(const struct acpi_register *reg) {
    return inw(reg->port);
}

static void write_control(const struct acpi_register *reg, uint16_t value) {
    outw(reg->port, value);
}

static bool enable_acpi_mode(void) {
    if (read_control(&pm1a_control) & ACPI_PM1_SCI_EN) return true;
    if (!smi_command_port || !acpi_enable_command) return false;
    outb(smi_command_port, acpi_enable_command);
    for (size_t attempt = 0; attempt < 1000000; ++attempt) {
        if (read_control(&pm1a_control) & ACPI_PM1_SCI_EN) return true;
        __asm__ volatile("pause");
    }
    return false;
}

static uint16_t sleep_control_value(uint16_t current, uint8_t sleep_type) {
    current &= (uint16_t)~(ACPI_PM1_SLP_TYP_MASK | ACPI_PM1_SLP_EN);
    return current | (uint16_t)((sleep_type & 7u) << 10) | ACPI_PM1_SLP_EN;
}

int acpi_poweroff(void) {
    if (!acpi_ready || !enable_acpi_mode()) return -1;
    write_control(&pm1a_control,
                  sleep_control_value(read_control(&pm1a_control), s5_type_a));
    if (pm1b_control.valid)
        write_control(&pm1b_control,
                      sleep_control_value(read_control(&pm1b_control), s5_type_b));
    timer_sleep(100);
    return -1;
}

static bool wait_for_keyboard_controller(void) {
    for (size_t attempt = 0; attempt < 100000; ++attempt) {
        if (!(inb(0x64) & 2u)) return true;
        __asm__ volatile("pause");
    }
    return false;
}

static void write_reset_register(void) {
    if (!reset_register.valid) return;
    outb(reset_register.port, reset_register.value);
}

/* Try the supported ACPI reset port, then the keyboard controller. */
int acpi_reboot(void) {
    write_reset_register();
    for (size_t attempt = 0; attempt < 1000000; ++attempt)
        __asm__ volatile("pause");
    if (wait_for_keyboard_controller()) outb(0x64, 0xfe);
    for (size_t attempt = 0; attempt < 1000000; ++attempt)
        __asm__ volatile("pause");
    return -1;
}
