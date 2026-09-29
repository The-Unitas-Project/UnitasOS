# Kernel contribution guide

Use this guide when you change UnitasOS kernel code. It describes boot order, code boundaries, and current limits.

## Boot and start

`boot/boot.S` is the Multiboot2 entry point. It saves the bootloader magic value and information pointer. It sets up four page directories and maps the first 4 GiB to the same physical addresses.

The boot code enables 64-bit mode. It then calls `kernel/core/main.c:kernel_main` with the System V AMD64 calling convention. The kernel reserves the Multiboot information block for its full lifetime.

Keep changes to the boot assembly small. Put common setup in `kernel_main` or a subsystem function.

The kernel starts these parts in this order:

1. Set up VGA output and the COM1 logger.
2. Check the Multiboot2 data and start the physical page allocator.
3. Start the heap with identity-mapped pages below 1 GiB.
4. Format and mount the RAM FAT32 file system through the VFS.
5. Load the IDT, remap and mask the legacy PIC, then set up the PIT and keyboard.
6. Find IDE, AHCI, and NVMe storage. Add detected disks to the block layer.
7. Scan disk partition tables. Add valid partitions to the block layer and mount `/dev`.
8. Enable interrupts and start the shell with input and output through `/dev/console`.

Keep interrupt handlers short. The PS/2 handler converts set-1 make codes to bytes. The main loop handles terminal input. Register device handlers with the IRQ API. The common interrupt handler sends an EOI to the legacy PIC.

## Code boundaries

- Put CPU instructions, descriptor tables, and interrupt entry code in `kernel/arch/x86_64/`.
- Put public declarations in `kernel/include/kern/`.
- Put device access and protocol code in `kernel/drivers/`. Use the driver registry for device setup when it fits.
- Put physical page and heap allocation code in `kernel/mm/`.
- Put file system routing code in `kernel/fs/`. Each file system owns its node format and implements the callbacks in `kern/vfs.h`.
- Put shared kernel code in `kernel/core/`. Reuse public functions when they provide the needed behavior.

## Current limits

- The kernel uses identity mapping and tracks physical pages up to 128 GiB.
- The heap gets pages from the first 1 GiB. It grows in 16-page regions and does not return empty regions to the page allocator.
- The boot page tables map the first 4 GiB as writable and executable. The kernel does not set page-level read, write, or execute permissions.
- IDE uses polling PIO. AHCI and NVMe use polling and identity-mapped DMA buffers below 4 GiB.
- The three disk drivers need 512-byte logical sectors.
- The interrupt path uses the legacy pair of 8259 PICs.
- The PS/2 keyboard uses US set-1 input. Serial input and output work only after you build with `make serial=1`.
- PCI scan code reports USB controller candidates. The HID code converts 8-byte boot reports to shell input.
- The xHCI probe does not start a controller or read reports. Use `USB=1` to add an xHCI controller and keyboard in QEMU.
- The kernel scans GPT and MBR primary and logical partitions. It checks GPT header and entry-array CRC values and adds valid partitions to the block layer.
- The GPT scan reads the primary header. It supports at most 128 entries, each 128 bytes. It does not recover from a bad primary GPT.
- The `/dev` file system lists console, keyboard, null, zero, disks, and partitions. Serial builds also list `/dev/serial0`.
- Block device files read and write bytes at the offset that `vfs_seek` sets. They use read-modify-write for partial sectors. A write can change partition tables or disk data.
- IDE and AHCI partitions use names such as `/dev/hda1` and `/dev/sda1`. NVMe partitions use names such as `/dev/nvme0n1p1`.
- Call `partition_scan_device` after a driver registers a new disk. The block registry and partition scan do not support concurrent registration.
- The shell reads input and writes output through `/dev/console`. The `dev` command lists device files. The `diskcheck` command reads MBR and GPT signatures through VFS calls.
- No USB mass-storage or framebuffer driver exists yet. A device appears in `/dev` after its driver registers it with the block layer or device file system.
- The VFS mounts one RAM FAT32 file system. It supports 8.3 names and root directory operations.
- FAT32 unlink fails while a file is open. This keeps its directory entry valid for open file handles.
- The kernel formats its RAM file system at each boot. It does not keep files after reboot.

The kernel does not have user mode, processes, system calls, per-process address spaces, or a full virtual-memory manager. It also does not have ACPI discovery, SMP, PCIe extended configuration access, disk interrupts, a network stack, FAT32 subdirectories, or a persistent file system.

Add each missing part as a separate subsystem. Define its start order, resource ownership, and error behavior. Do not infer support from an interface alone.

Raw writes to `/dev/<disk>` can change partition tables and file data. Add access checks before you allow user programs to open raw disk devices.

## Build and checks

Run `make` to build the freestanding ELF kernel. Run `make iso` to add the kernel and GRUB configuration to an ISO. Run `make run` to boot the ISO with QEMU.

Run `make test` for host tests. These tests cover kernel helpers that do not need privileged instructions or hardware. Keep host tests separate from the kernel image. Add tests for reusable code with important edge cases.

The kernel build has no libc, stack protector, position-independent code, SIMD register use, or x86_64 red zone. Keep new code within these limits. Use the functions in `kern/string.h` until the kernel has a full C runtime.

## Comments

Use ASD-STE100 for comments, documentation, and docstrings. Write for programmers who know C and basic systems concepts. Use direct sentences. State contracts, ownership, limits, and hardware requirements. Do not describe an obvious line of code. Do not wrap prose at a fixed column width.
