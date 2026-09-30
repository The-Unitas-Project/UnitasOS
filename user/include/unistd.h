#ifndef UNITAS_USER_UNISTD_H
#define UNITAS_USER_UNISTD_H

#include <stddef.h>

typedef long ssize_t;
typedef long off_t;

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

ssize_t read(int descriptor, void *buffer, size_t length);
ssize_t write(int descriptor, const void *buffer, size_t length);
ssize_t getdents64(int descriptor, void *buffer, size_t length);
int close(int descriptor);
int unlink(const char *path);
/* On success, execve replaces the current image and does not return. */
int execve(const char *path, char *const arguments[],
           char *const environment[]);
off_t lseek(int descriptor, off_t offset, int origin);
long brk(void *address);
void *sbrk(long increment);
void _exit(int status);

#endif
