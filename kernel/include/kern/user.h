#ifndef UNITAS_KERN_USER_H
#define UNITAS_KERN_USER_H

#include <stdint.h>
#include <stddef.h>

/* Set up ring 3 entry, the Task State Segment (TSS) stack, and syscall entry. */
int user_init(void);
/* Run one absolute-path static x86-64 ELF image, then return its exit status. */
int user_exec(const char *path, size_t argument_count,
              const char *const arguments[]);
int userland_seed(void);
int user_handle_exception(uint64_t vector, uint64_t rip);
long user_linux_syscall(uint64_t number, uint64_t first, uint64_t second,
                        uint64_t third, uint64_t fourth, uint64_t fifth,
                        uint64_t sixth);

#endif
