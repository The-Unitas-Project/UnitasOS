#include <errno.h>
#include <fcntl.h>
#include <internal.h>
#include <stdlib.h>
#include <unistd.h>
#include <unitas/syscall.h>
#include <sys/utsname.h>
#include <sys/reboot.h>
#include <asm/unistd_64.h>

int errno;

static long result(long value) {
    if (value < 0) {
        errno = (int)-value;
        return -1;
    }
    return value;
}

ssize_t read(int descriptor, void *buffer, size_t length) {
    return (ssize_t)result(__unitas_syscall(UNITAS_SYS_READ, descriptor, (long)buffer,
                                           (long)length, 0));
}

ssize_t write(int descriptor, const void *buffer, size_t length) {
    return (ssize_t)result(__unitas_syscall(UNITAS_SYS_WRITE, descriptor, (long)buffer,
                                           (long)length, 0));
}

ssize_t getdents64(int descriptor, void *buffer, size_t length) {
    return (ssize_t)result(__unitas_syscall(UNITAS_SYS_GETDENTS64, descriptor,
                                           (long)buffer, (long)length, 0));
}

int open(const char *path, int flags, ...) {
    return (int)result(__unitas_syscall(UNITAS_SYS_OPEN, (long)path, flags, 0, 0));
}

int close(int descriptor) {
    return (int)result(__unitas_syscall(UNITAS_SYS_CLOSE, descriptor, 0, 0, 0));
}

int unlink(const char *path) {
    return (int)result(__unitas_syscall(UNITAS_SYS_UNLINK, (long)path, 0, 0, 0));
}

int execve(const char *path, char *const arguments[],
           char *const environment[]) {
    /* execve uses the Linux syscall table; the other wrappers use Unitas calls. */
    return (int)result(__linux_syscall(__NR_execve, (long)path,
                                      (long)arguments, (long)environment,
                                      0, 0, 0));
}

int reboot(int command) {
    /* The Linux syscall requires two fixed values before its command. */
    return (int)result(__linux_syscall(__NR_reboot, 0xfee1dead,
                                      0x28121969, command, 0, 0, 0));
}

int uname(struct utsname *name) {
    return (int)result(__unitas_syscall(UNITAS_SYS_UNAME, (long)name, 0, 0, 0));
}

off_t lseek(int descriptor, off_t offset, int origin) {
    return (off_t)result(__unitas_syscall(UNITAS_SYS_LSEEK, descriptor, offset, origin, 0));
}

long brk(void *address) {
    return result(__unitas_syscall(UNITAS_SYS_BRK, (long)address, 0, 0, 0));
}

void *sbrk(long increment) {
    long current = brk(0);
    if (current < 0 || increment > 0x7fffffffL || increment < -0x7fffffffL) {
        errno = ENOMEM;
        return (void *)-1;
    }
    long next;
    if (__builtin_add_overflow(current, increment, &next) ||
        brk((void *)next) < 0) {
        errno = ENOMEM;
        return (void *)-1;
    }
    return (void *)current;
}
