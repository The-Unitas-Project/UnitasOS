#include <stdio.h>
#include <unistd.h>

int putchar(int character) {
    char byte = (char)character;
    return write(1, &byte, 1) == 1 ? (unsigned char)byte : -1;
}

int puts(const char *text) {
    size_t length = 0;
    while (text[length]) ++length;
    if (write(1, text, length) != (ssize_t)length || write(1, "\n", 1) != 1)
        return -1;
    return 0;
}
