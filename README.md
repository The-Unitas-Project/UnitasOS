# UnitasOS

UnitasOS is an experimental x86_64 operating-system foundation. The current
kernel boots through the Multiboot2 protocol and provides a documented starting
point for architecture code, memory management, interrupts, device drivers, and
future OS services. It is intentionally small enough for newcomers to trace from
the bootloader to `kernel_main`.

## Build and boot

Requirements: GCC with 64-bit freestanding support, GNU binutils, Make, GRUB's
`grub-mkrescue`, and `xorriso`.

```sh
make                 # build build/kernel.elf
make iso             # build build/unitasos.iso
make run             # boot with QEMU and a serial shell
QEMU_DISPLAY=gtk make run # show the QEMU VGA console when GTK is available
make disk-image DISK=build/disk.raw
make run STORAGE=ahci DISK=build/disk.raw
make run STORAGE=nvme DISK=build/disk.raw
make run STORAGE=nvme DISK=build/unitasos.raw BOOT=disk FIRMWARE=uefi
make install-vdi VDI=build/unitasos.vdi MODE=both
make run-vbox STORAGE=ahci DISK=build/unitasos.vdi BOOT=disk VM_NAME=UnitasAHCI
make run-vbox STORAGE=nvme DISK=build/unitasos.vdi BOOT=disk FIRMWARE=uefi VM_NAME=UnitasNVMe
make test            # run host-side kernel unit tests
make install DEVICE=/dev/sda MODE=both
make install-image IMAGE=build/unitasos.raw MODE=both
make install-vdi VDI=build/unitasos.vdi MODE=both
make partition-edit DEVICE=/dev/sda
make clean
```

GRUB loads the ELF kernel using the Multiboot2 protocol. `boot/boot.S` begins in
32-bit protected mode, installs bootstrap page tables, enables long mode, and
passes the Multiboot information pointer to the C entry point. The first 4 GiB
are identity mapped with 2 MiB pages so early kernel code and boot data are
reachable before a full virtual-memory manager exists.

## Kernel map

- `kernel/arch/x86_64/`: CPU entry, descriptor tables, and interrupt dispatch.
- `kernel/core/`: boot orchestration, logging, panic handling, and shared helpers.
- `kernel/drivers/`: console, serial, interrupt, keyboard, IDE, AHCI, and NVMe drivers.
- `kernel/core/block.c`: shared sector-based block-device registry.
- `kernel/fs/`: VFS dispatch and a 64 MiB FAT32 volume formatted in RAM.
- `kernel/include/kern/`: documented public kernel interfaces.
- `kernel/mm/`: Multiboot memory-map parsing, physical pages, and kernel heap.
- `kernel/core/shell.c`: live command shell for the RAM volume.
- `kernel/modules/`: reserved for future loadable-module support.
- `kernel/net/`: reserved for network stack implementation.
- `tests/`: host-runnable tests for the ring buffer, block layer, VFS, and RAM FAT32 volume.
- `tools/`: Linux host installer and partition editor entry points.

The live shell supports `help`, `ls`, `cat`, `touch`, `write`, `rm`, `mem`,
`disks`, `diskcheck`, `clear`, and `uname`. You can use the shell from the VGA
console or through the COM1 serial console. The FAT32 volume uses 8.3 filenames
and is recreated at every boot. File changes remain in RAM until shutdown.
QEMU uses a headless display by default and provides the shell through its
terminal. Set `QEMU_DISPLAY=gtk` to use the VGA window on a desktop host.
VirtualBox creates a new VM for each launch. Use a different `VM_NAME` for each
new VM. For a live boot, omit `DISK` or attach a blank disk image.

## Disk installation

The installers run on a Linux host or a separate Linux live environment. They
use GRUB for BIOS and UEFI boot, then copy the kernel and GRUB configuration to
a FAT32 system partition. Set `DEVICE` to a whole disk such as `/dev/sda` or
`/dev/nvme0n1` to install to hardware. Use `make install-image` to create a
bootable raw disk image or `make install-vdi` to create a VirtualBox disk image.
Each image operation requires root access for loop devices and asks you to type
the output path before writing. `MODE` can be `uefi`, `bios`, or `both`. The
partition editor opens `cfdisk` after a confirmation. Host installation needs
util-linux, dosfstools, and GRUB tools for each selected firmware mode.

The live kernel detects IDE, SATA AHCI, and NVMe disks through the block-device
layer. The install tools still run on Linux because GRUB installation and disk
partitioning are not implemented in the kernel shell. IDE uses legacy PIO.
AHCI and NVMe use polling and DMA buffers below 4 GiB. The initial drivers
support 512-byte logical sectors. The installed kernel still uses a RAM
filesystem, so files do not persist across reboots. The hardware installer
and partition editor modify real disks.

## Contributing

Keep hardware-specific operations behind driver or architecture interfaces, and
keep public declarations in `kernel/include/kern/`. New code should explain why
it exists, ownership/lifetime rules, and hardware assumptions. Add a host test
for pure logic where practical. Hardware-dependent paths should document a
manual QEMU or device test procedure. This kernel is a learning and development
foundation, not yet a secure or production-ready operating system.
