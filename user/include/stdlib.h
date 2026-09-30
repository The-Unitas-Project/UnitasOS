#ifndef UNITAS_USER_STDLIB_H
#define UNITAS_USER_STDLIB_H

#include <stddef.h>

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);
void exit(int status);
void _exit(int status);
void *sbrk(long increment);

#endif
