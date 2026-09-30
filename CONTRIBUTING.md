# Kernel contribution guide

Use this guide when you change kernel code. Write comments, documentation, and docstrings in ASD-STE100. Write for programmers who know C and basic systems concepts. Do not explain an obvious line.

## Boot order

`boot/boot.S` is the Multiboot2 entry point. It creates an identity map for the first 4 GiB and enters 64-bit mode. The initial map uses 2 MiB pages.

`kernel/core/main.c` checks the boot data and starts the physical page allocator. `kernel/mm/paging.c` then splits the kernel image into 4 KiB pages. It makes text read-only and executable, and it makes data writable and non-executable. This step requires CPU support for NX.

The kernel starts the heap, mounts a disk FAT32 root when it finds one, or formats the RAM FAT32 file system. It mounts `/proc` and installs the built user programs. It then sets up the TSS and user entry code. The platform drivers start the timer, input, storage, partition scan, network, and device file system. The kernel enables interrupts and starts GNU Bash through `/bin/sh`. Keep command parsing and system utilities in user programs.

Keep interrupt handlers short. Keep hardware access in a driver or architecture file. Send device events to a kernel service when possible.

## Code layout

- Put CPU instructions, descriptor tables, and interrupt entry code in `kernel/arch/x86_64/`.
- Put public declarations in `kernel/include/kern/`.
- Put device access and protocol code in `kernel/drivers/`.
- Put physical page and heap code in `kernel/mm/`.
- Put file system code in `kernel/fs/`.
- Put shared kernel code in `kernel/core/`.
- Put Linux ABI definitions in `abi/linux/` and `abi/asm/`.
- Put Unitas-specific ABI definitions in `abi/unitas/`.
- Keep user program headers and library code in `user/`.

## Memory and file systems

The bootstrap maps physical addresses below 4 GiB to the same virtual addresses. The page allocator tracks RAM up to 128 GiB. Limit allocations to 4 GiB when the caller must access a page through the identity map.

The VFS owns mount and open-handle tables. A file system owns its node data. Keep callback paths relative to the mount. Return a negative value when a callback fails.

The RAM FAT32 volume owns its storage for the full boot. The proc file system is read-only. It creates a snapshot when a process opens a proc file. Keep proc output bounded by its file buffer.

Use `vfs_register` before `vfs_mount`. Keep each file system descriptor and mount data valid while the mount is active. Release each allocated node in its `close` callback.

## Build

Run `make` to build GNU libc, GNU Coreutils, Bash, and the kernel. Run `make serial=1` to build the same system with serial input and output. Run `make iso` to package the kernel with GRUB. Run `make run` to boot the ISO with QEMU.

Build both serial modes when a change affects shared code or configuration.

The kernel has no hosted C library. Keep kernel code freestanding. Do not use the red zone, stack protector, position-independent code, or SIMD registers.
