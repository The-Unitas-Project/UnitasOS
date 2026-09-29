#include <stddef.h>

/* These routines replace libc functions in the freestanding kernel. */
void *memset(void *destination, int value, size_t length) {
    unsigned char *out = destination;
    for (size_t i = 0; i < length; ++i) out[i] = (unsigned char)value;
    return destination;
}

void *memcpy(void *destination, const void *source, size_t length) {
    unsigned char *out = destination;
    const unsigned char *in = source;
    for (size_t i = 0; i < length; ++i) out[i] = in[i];
    return destination;
}

int memcmp(const void *left, const void *right, size_t length) {
    const unsigned char *a = left, *b = right;
    for (size_t i = 0; i < length; ++i)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

size_t strlen(const char *text) {
    size_t length = 0;
    while (text[length]) ++length;
    return length;
}

int strcmp(const char *left, const char *right) {
    while (*left && *left == *right) { ++left; ++right; }
    return (unsigned char)*left - (unsigned char)*right;
}

char *strchr(const char *text, int character) {
    while (*text) {
        if (*text == (char)character) return (char *)text;
        ++text;
    }
    return character == 0 ? (char *)text : 0;
}
