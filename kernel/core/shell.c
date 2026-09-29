#include <kern/console.h>
#include <kern/block.h>
#include <kern/keyboard.h>
#include <kern/mm.h>
#include <kern/panic.h>
#include <kern/ramfs.h>
#include <kern/shell.h>
#include <kern/string.h>
#include <kern/vfs.h>
#include <limits.h>

#define SHELL_LINE_CAPACITY 128
static char line[SHELL_LINE_CAPACITY];
static size_t line_length;
static bool previous_character_was_cr;
static int console_output = -1;

static void write_output(const void *buffer, size_t length) {
    if (console_output < 0 || length > INT_MAX ||
        vfs_write(console_output, buffer, length) != (int)length)
        panic("console output failed");
}

static void print(const char *text) {
    write_output(text, strlen(text));
}

static void print_u64(uint64_t value) {
    char digits[20];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (count) {
        char character = digits[--count];
        write_output(&character, 1);
    }
}

static void print_hex_byte(uint8_t value) {
    static const char digits[] = "0123456789abcdef";
    write_output(&digits[value >> 4], 1);
    write_output(&digits[value & 0x0f], 1);
}

static void prompt(void) { print("unitas:/# "); }

static char *skip_spaces(char *text) {
    while (*text == ' ' || *text == '\t') ++text;
    return text;
}

static char *next_word(char **cursor) {
    char *word = skip_spaces(*cursor);
    if (!*word) { *cursor = word; return 0; }
    char *end = word;
    while (*end && *end != ' ' && *end != '\t') ++end;
    if (*end) *end++ = 0;
    *cursor = end;
    return word;
}

static void command_help(void) {
    print("help  ls  dev  cat FILE  touch FILE  write FILE TEXT  rm FILE\n");
    print("mem   clear  uname\n");
    print("disks  diskcheck\n");
    print("RAM files use FAT32 8.3 names and disappear when the machine reboots.\n");
}

static void command_list(void) {
    struct vfs_dirent entry;
    for (uint64_t index = 0; vfs_readdir("/", index, &entry) > 0; ++index) {
        print(entry.name);
        print("  ");
        if (entry.type) print("<DIR>\n");
        else { print("<FILE>\n"); }
    }
}

static void command_devices(void) {
    struct vfs_dirent entry;
    for (uint64_t index = 0; vfs_readdir("/dev", index, &entry) > 0; ++index) {
        print("/dev/");
        print(entry.name);
        print("\n");
    }
}

static void command_cat(const char *name) {
    char path[32] = "/";
    size_t name_length = strlen(name);
    if (name_length > sizeof(path) - 2) { print("filename is too long\n"); return; }
    memcpy(path + 1, name, name_length + 1);
    int handle;
    if (vfs_open(path, 0, &handle) < 0) { print("file not found\n"); return; }
    char buffer[64];
    int amount;
    while ((amount = vfs_read(handle, buffer, sizeof(buffer))) > 0)
        write_output(buffer, (size_t)amount);
    print("\n");
    (void)vfs_close(handle);
}

static void command_touch(const char *name) {
    char path[32] = "/";
    size_t length = strlen(name);
    if (length > sizeof(path) - 2) { print("filename is too long\n"); return; }
    memcpy(path + 1, name, length + 1);
    int handle;
    if (vfs_open(path, VFS_OPEN_CREATE, &handle) < 0) print("could not create file\n");
    else (void)vfs_close(handle);
}

static void command_write(const char *name, const char *content) {
    char path[32] = "/";
    size_t length = strlen(name);
    if (length > sizeof(path) - 2) { print("filename is too long\n"); return; }
    memcpy(path + 1, name, length + 1);
    int handle;
    if (vfs_open(path, VFS_OPEN_CREATE | VFS_OPEN_TRUNCATE, &handle) < 0) {
        print("could not open file for writing\n");
        return;
    }
    size_t content_length = strlen(content);
    if (vfs_write(handle, content, content_length) != (int)content_length)
        print("write failed\n");
    (void)vfs_close(handle);
}

static void command_remove(const char *name) {
    char path[32] = "/";
    size_t length = strlen(name);
    if (length > sizeof(path) - 2) { print("filename is too long\n"); return; }
    memcpy(path + 1, name, length + 1);
    if (vfs_unlink(path) < 0) print("file not found\n");
}

static void command_disks(void) {
    size_t count = block_device_count();
    if (!count) { print("no physical block devices detected\n"); return; }
    for (size_t i = 0; i < count; ++i) {
        struct block_device *device = block_device_at(i);
        print(device->name);
        print("  ");
        print_u64(device->sector_count);
        print(" sectors\n");
    }
}

static void command_diskcheck(void) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    for (size_t i = 0; i < block_device_count(); ++i) {
        struct block_device *device = block_device_at(i);
        char path[BLOCK_NAME_MAX + 6] = "/dev/";
        size_t name_length = strlen(device->name);
        memcpy(path + 5, device->name, name_length + 1);
        int handle = -1;
        print(device->name);
        if (vfs_open(path, 0, &handle) < 0 ||
            vfs_read(handle, sector, sizeof(sector)) != (int)sizeof(sector)) {
            print(" sector 0 read failed\n");
            if (handle >= 0) (void)vfs_close(handle);
            continue;
        }
        print(" sector 0 read OK. MBR signature 0x");
        print_hex_byte(sector[511]);
        print_hex_byte(sector[510]);
        if (device->sector_count >= 2 &&
            vfs_seek(handle, BLOCK_SECTOR_SIZE) == 0 &&
            vfs_read(handle, sector, sizeof(sector)) == (int)sizeof(sector) &&
            memcmp(sector, "EFI PART", 8) == 0)
            print(". GPT header found");
        print("\n");
        (void)vfs_close(handle);
    }
}

static void execute_line(char *input) {
    char *cursor = input;
    char *command = next_word(&cursor);
    if (!command) return;
    if (strcmp(command, "help") == 0) command_help();
    else if (strcmp(command, "ls") == 0) command_list();
    else if (strcmp(command, "dev") == 0) command_devices();
    else if (strcmp(command, "cat") == 0) {
        char *name = next_word(&cursor);
        if (name) command_cat(name); else print("usage: cat FILE\n");
    } else if (strcmp(command, "touch") == 0) {
        char *name = next_word(&cursor);
        if (name) command_touch(name); else print("usage: touch FILE\n");
    } else if (strcmp(command, "write") == 0) {
        char *name = next_word(&cursor);
        char *content = skip_spaces(cursor);
        if (name && *content) command_write(name, content);
        else print("usage: write FILE TEXT\n");
    } else if (strcmp(command, "rm") == 0) {
        char *name = next_word(&cursor);
        if (name) command_remove(name); else print("usage: rm FILE\n");
    } else if (strcmp(command, "disks") == 0) command_disks();
    else if (strcmp(command, "diskcheck") == 0) command_diskcheck();
    else if (strcmp(command, "mem") == 0) {
        print("free pages: ");
        print_u64(pmm_free_page_count());
        print(" / tracked pages: ");
        print_u64(pmm_total_pages());
        print("\nRAM FAT32 volume bytes: ");
        print_u64(ramfs_capacity_bytes());
        print("\n");
    } else if (strcmp(command, "clear") == 0) console_clear();
    else if (strcmp(command, "uname") == 0) print("UnitasOS x86_64\n");
    else print("command not found. Type help for available commands.\n");
}

void shell_init(void) {
    static const char greeting[] = "UnitasOS live shell\nType help to list commands.\n";
    if (vfs_open("/dev/console", 0, &console_output) < 0)
        panic("failed to open console output device");
    int handle;
    if (vfs_open("/WELCOME.TXT", VFS_OPEN_CREATE | VFS_OPEN_TRUNCATE, &handle) == 0) {
        (void)vfs_write(handle, greeting, sizeof(greeting) - 1);
        (void)vfs_close(handle);
    }
    prompt();
}

void shell_process_char(char character) {
    if (character == '\b' || character == '\x7f') {
        if (line_length) {
            --line_length;
            static const char erase[] = "\b \b";
            write_output(erase, sizeof(erase) - 1);
        }
        previous_character_was_cr = false;
        return;
    }
    if (character == '\n' && previous_character_was_cr) {
        previous_character_was_cr = false;
        return;
    }
    previous_character_was_cr = character == '\r';
    if (character == '\n' || character == '\r') {
        write_output("\n", 1);
        line[line_length] = 0;
        execute_line(line);
        line_length = 0;
        prompt();
        return;
    }
    /* The parser treats Tab as a space. Show a space on the console. */
    if (character == '\t') character = ' ';
    if (character < 0x20 || line_length + 1 >= sizeof(line)) return;
    line[line_length++] = character;
    write_output(&character, 1);
}
