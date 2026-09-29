#include <kern/console.h>
#include <kern/io.h>
#include <kern/serial.h>
#include <kern/types.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile uint16_t *)0xb8000)
#define VGA_COLOR 0x0f

static size_t column;
static size_t row;

static void move_cursor(void) {
    uint16_t position = (uint16_t)(row * VGA_WIDTH + column);
    outb(0x3d4, 0x0f); outb(0x3d5, (uint8_t)position);
    outb(0x3d4, 0x0e); outb(0x3d5, (uint8_t)(position >> 8));
}

static void scroll_if_needed(void) {
    if (row < VGA_HEIGHT) return;
    for (size_t y = 1; y < VGA_HEIGHT; ++y)
        for (size_t x = 0; x < VGA_WIDTH; ++x)
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];
    for (size_t x = 0; x < VGA_WIDTH; ++x)
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = (VGA_COLOR << 8) | ' ';
    row = VGA_HEIGHT - 1;
}

void console_init(void) {
    console_clear();
}

void console_clear(void) {
    column = row = 0;
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i)
        VGA_MEMORY[i] = (VGA_COLOR << 8) | ' ';
    move_cursor();
}

void console_putc(char character) {
    if (character == '\n') serial_putc('\r');
    serial_putc(character);
    if (character == '\r') column = 0;
    else if (character == '\n') { column = 0; ++row; }
    else if (character == '\b') { if (column) --column; }
    else {
        VGA_MEMORY[row * VGA_WIDTH + column] = (VGA_COLOR << 8) | (uint8_t)character;
        if (++column == VGA_WIDTH) { column = 0; ++row; }
    }
    scroll_if_needed();
    move_cursor();
}

void console_write(const char *text, size_t length) {
    for (size_t i = 0; i < length; ++i) console_putc(text[i]);
}
