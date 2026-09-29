#include <kern/keyboard.h>
#include <kern/string.h>
#include <kern/usb_hid_keyboard.h>

static uint8_t previous_keys[6];
static bool caps_lock_enabled;

static bool report_has_key(const uint8_t report[8], uint8_t usage) {
    for (unsigned i = 2; i < 8; ++i)
        if (report[i] == usage) return true;
    return false;
}

static bool key_was_down(uint8_t usage) {
    for (unsigned i = 0; i < sizeof(previous_keys); ++i)
        if (previous_keys[i] == usage) return true;
    return false;
}

static bool report_has_rollover(const uint8_t report[8]) {
    for (unsigned i = 2; i < 8; ++i)
        if (report[i] == 1) return true;
    return false;
}

static char key_to_ascii(uint8_t usage, bool shift) {
    static const char letters[] = "abcdefghijklmnopqrstuvwxyz";
    static const char digits[] = "1234567890";
    static const char shifted_digits[] = "!@#$%^&*()";

    if (usage >= 0x04 && usage <= 0x1d) {
        char character = letters[usage - 0x04];
        if (shift != caps_lock_enabled) character -= 'a' - 'A';
        return character;
    }
    if (usage >= 0x1e && usage <= 0x27)
        return shift ? shifted_digits[usage - 0x1e] : digits[usage - 0x1e];

    switch (usage) {
    case 0x28: return '\n';
    case 0x29: return '\x1b';
    case 0x2a: return '\b';
    case 0x2b: return '\t';
    case 0x2c: return ' ';
    case 0x2d: return shift ? '_' : '-';
    case 0x2e: return shift ? '+' : '=';
    case 0x2f: return shift ? '{' : '[';
    case 0x30: return shift ? '}' : ']';
    case 0x31: return shift ? '|' : '\\';
    case 0x33: return shift ? ':' : ';';
    case 0x34: return shift ? '"' : '\'';
    case 0x35: return shift ? '~' : '`';
    case 0x36: return shift ? '<' : ',';
    case 0x37: return shift ? '>' : '.';
    case 0x38: return shift ? '?' : '/';
    default: return 0;
    }
}

void usb_hid_keyboard_init(void) {
    memset(previous_keys, 0, sizeof(previous_keys));
    caps_lock_enabled = false;
}

void usb_hid_keyboard_report(const uint8_t report[8]) {
    if (!report || report_has_rollover(report)) return;

    if (report_has_key(report, 0x39) && !key_was_down(0x39))
        caps_lock_enabled = !caps_lock_enabled;

    bool shift = (report[0] & 0x22) != 0;
    for (unsigned i = 2; i < 8; ++i) {
        uint8_t usage = report[i];
        if (!usage || key_was_down(usage)) continue;
        char character = key_to_ascii(usage, shift);
        if (character) (void)keyboard_queue_char(character);
    }

    memcpy(previous_keys, report + 2, sizeof(previous_keys));
}
