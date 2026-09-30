#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <internal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <unitas/syscall.h>

struct allocation {
    size_t size;
    bool free;
    struct allocation *previous;
    struct allocation *next;
};

static struct allocation *first;
static struct allocation *last;
static const size_t header_size = (sizeof(struct allocation) + 15) & ~(size_t)15;

void *malloc(size_t size) {
    if (!size) size = 1;
    if (size > SIZE_MAX - 15) {
        errno = ENOMEM;
        return 0;
    }
    size = (size + 15) & ~(size_t)15;
    for (struct allocation *block = first; block; block = block->next) {
        if (block->free && block->size >= size) {
            block->free = false;
            return (unsigned char *)block + header_size;
        }
    }
    if (size > (size_t)LONG_MAX - header_size) {
        errno = ENOMEM;
        return 0;
    }
    struct allocation *block = sbrk((long)(header_size + size));
    if (block == (void *)-1) return 0;
    block->size = size;
    block->free = false;
    block->previous = last;
    block->next = 0;
    if (last) last->next = block;
    else first = block;
    last = block;
    return (unsigned char *)block + header_size;
}

void free(void *pointer) {
    if (!pointer) return;
    struct allocation *block = (void *)((uintptr_t)pointer - header_size);
    block->free = true;
    if (block->next && block->next->free &&
        (uintptr_t)block + header_size + block->size == (uintptr_t)block->next) {
        struct allocation *next = block->next;
        block->size += header_size + next->size;
        block->next = next->next;
        if (block->next) block->next->previous = block;
        else last = block;
    }
    if (block->previous && block->previous->free &&
        (uintptr_t)block->previous + header_size + block->previous->size ==
        (uintptr_t)block) {
        struct allocation *previous = block->previous;
        previous->size += header_size + block->size;
        previous->next = block->next;
        if (block->next) block->next->previous = previous;
        else last = previous;
    }
}

void *calloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) {
        errno = ENOMEM;
        return 0;
    }
    size_t length = count * size;
    void *memory = malloc(length);
    if (!memory) return 0;
    memset(memory, 0, length);
    return memory;
}

void *realloc(void *pointer, size_t size) {
    if (!pointer) return malloc(size);
    if (!size) {
        free(pointer);
        return 0;
    }
    struct allocation *block = (void *)((uintptr_t)pointer - header_size);
    if (block->size >= size) return pointer;
    void *replacement = malloc(size);
    if (!replacement) return 0;
    memcpy(replacement, pointer, block->size);
    free(pointer);
    return replacement;
}

void _exit(int status) {
    (void)__unitas_syscall(UNITAS_SYS_EXIT, status, 0, 0, 0);
    for (;;) {}
}

void exit(int status) { _exit(status); }
