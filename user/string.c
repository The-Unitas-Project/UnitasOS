#include <stddef.h>
#include <stdint.h>

void *memcpy(void *destination, const void *source, size_t length) {
    unsigned char *out = destination;
    const unsigned char *in = source;
    for (size_t index = 0; index < length; ++index) out[index] = in[index];
    return destination;
}

void *memmove(void *destination, const void *source, size_t length) {
    unsigned char *out = destination;
    const unsigned char *in = source;
    if ((uintptr_t)out < (uintptr_t)in) {
        for (size_t index = 0; index < length; ++index) out[index] = in[index];
    } else if ((uintptr_t)out > (uintptr_t)in) {
        while (length) {
            --length;
            out[length] = in[length];
        }
    }
    return destination;
}

void *memset(void *destination, int value, size_t length) {
    unsigned char *out = destination;
    for (size_t index = 0; index < length; ++index) out[index] = (unsigned char)value;
    return destination;
}

int memcmp(const void *left, const void *right, size_t length) {
    const unsigned char *a = left;
    const unsigned char *b = right;
    for (size_t index = 0; index < length; ++index)
        if (a[index] != b[index]) return a[index] < b[index] ? -1 : 1;
    return 0;
}

size_t strlen(const char *text) {
    size_t length = 0;
    while (text[length]) ++length;
    return length;
}

int strcmp(const char *left, const char *right) {
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return (unsigned char)*left - (unsigned char)*right;
}
