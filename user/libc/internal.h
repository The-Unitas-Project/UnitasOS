#ifndef UNITAS_USER_LIBC_INTERNAL_H
#define UNITAS_USER_LIBC_INTERNAL_H

long __unitas_syscall(long number, long first, long second, long third,
                      long fourth);
long __linux_syscall(long number, long first, long second, long third,
                     long fourth, long fifth, long sixth);

#endif
