# Kernel contribution guide

This guide describes the current UnitasOS kernel boundaries and the assumptions
that matter when extending them. It is written for systems programmers who are
new to this repository.

## Boot and initialization

`boot/boot.S` is the Multiboot2 entry point. It preserves the bootloader magic
and information pointer, installs four page directories, and enters 64-bit
long mode with the first 4 GiB identity mapped. It then calls
`kernel/core/main.c:kernel_main` using the System V AMD64 calling convention.
The Multiboot information block remains reserved for the lifetime of this
kernel. Keep changes to the entry assembly small. Architecture-independent
initialization belongs in `kernel_main` or a subsystem initializer.

Initialization currently runs in this order:

1. Initialize VGA output and the COM1 logger.
2. Validate the Multiboot2 handoff and initialize the physical page allocator.
3. Initialize the heap on identity-mapped pages below 1 GiB.
4. Format and mount the RAM FAT32 volume through the VFS.
5. Load the IDT, remap and mask the legacy PIC, and register PIT and keyboard
   drivers.
6. Probe IDE, AHCI, and NVMe storage and register detected disks with the block
   layer.
7. Enable interrupts and run the shell from keyboard and serial input.

Hardware interrupt handlers should do bounded work. The keyboard handler only
translates set-1 make codes into bytes. Terminal processing remains in the main
loop. Device handlers are registered through the IRQ API and the legacy PIC
receives an EOI from the common dispatcher.

## Subsystem boundaries

- Put CPU-specific instructions, descriptor tables, and interrupt entry code in
  `kernel/arch/x86_64/`.
- Put public interfaces in `kernel/include/kern/`. Keep private state in the
  implementing `.c` file.
- Put device access and protocol handling in `kernel/drivers/`. Drivers should
  expose initialization through the driver registry where practical.
- Put physical-page and general allocation policy in `kernel/mm/`.
- Put filesystem routing in `kernel/fs/`. A filesystem implementation owns its
  node representation and supplies the callbacks declared by `kern/vfs.h`.
- Keep shared kernel primitives in `kernel/core/`. Avoid adding a second copy
  of functionality already available through a public header.

## Current implementation limits

This is a bootable kernel foundation, not a complete general-purpose OS. The
current x86_64 path uses identity mapping, supports physical page tracking up to
128 GiB, and limits heap backing pages to the first GiB. IDE uses polling PIO.
AHCI and NVMe use polling commands and identity-mapped DMA buffers below 4 GiB.
All three drivers currently require 512-byte logical sectors. The heap grows in
16-page regions and does not return empty regions to the physical allocator.
The interrupt path uses the legacy dual 8259 PIC. The keyboard driver supports
only a basic US set-1 mapping. The VFS currently mounts one in-memory FAT32
filesystem with 8.3 filenames and root-directory operations.

User mode, process scheduling, system calls, per-process address spaces, a full
virtual-memory manager, ACPI discovery, SMP, PCIe extended configuration-space access,
interrupt-driven storage, a network stack, FAT32 subdirectories, and persistent
filesystems are not yet implemented. Add these as separate, reviewable
subsystems with explicit initialization, ownership, and failure behavior. Do
not treat the existing interfaces as proof that those facilities are already
present.

## Build and tests

`make` builds the freestanding ELF kernel. `make iso` packages it with the GRUB
configuration, and `make run` starts that image under QEMU. `make test` runs
host-side tests for pure kernel helpers that do not require privileged CPU
instructions or hardware. Keep such tests independent of the kernel image and
add a test target when introducing reusable logic with meaningful edge cases.

The kernel is built without libc, stack protection, position-independent code,
or the x86_64 red zone. New code must preserve those constraints. Use the
minimal functions in `kern/string.h` until a fuller C runtime is available.
