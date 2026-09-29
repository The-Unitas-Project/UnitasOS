# UnitasOS

UnitasOS is an experimental x86_64 operating-system foundation. The current kernel boots through the Multiboot2 protocol and provides a starting point for architecture code, memory management, interrupts, device drivers, and future OS services.

## Build and boot

Requirements for `make`: Make, `x86_64-elf-gcc`, and `x86_64-elf-ld`. Building an ISO with `make iso` also requires GRUB's `grub-mkrescue` and `xorriso`. Running the ISO with `make run` requires QEMU.

On macOS with MacPorts, install the cross compiler and binutils with `sudo port install x86_64-elf-gcc x86_64-elf-binutils`. Building the ISO on macOS requires a Docker container for `grub-mkrescue` or an equivalent environment.

Set `CROSS_COMPILE` if your toolchain uses a different executable prefix. The kernel ELF can be built without GRUB or QEMU.

### To build

```sh
make                 # build build/kernel.elf
make iso             # build build/unitasos.iso
make test            # run host-side kernel unit tests
```

### To run on QEMU

```sh
make run                   # boot with QEMU and a serial shell
QEMU_DISPLAY=gtk make run  # show the QEMU VGA console when GTK is available
```

### To run on VirtualBox

```sh
make run-vbox STORAGE=ahci DISK=build/unitasos.vdi BOOT=disk VM_NAME=UnitasAHCI # Run with AHCI driver
make run-vbox STORAGE=nvme DISK=build/unitasos.vdi BOOT=disk FIRMWARE=uefi VM_NAME=UnitasNVMe # Run with NVMe driver
```

GRUB loads the ELF kernel using the Multiboot2 protocol. `boot/boot.S` begins in 32-bit protected mode, installs bootstrap page tables, enables long mode, and passes the Multiboot information pointer to the C entry point. The first 4 GiB are identity mapped with 2 MiB pages so early kernel code and boot data are reachable before a full virtual-memory manager exists.

## Disk installation

The installers run on a Linux host or a separate Linux live environment. They use GRUB for BIOS and UEFI boot, then copy the kernel and GRUB configuration to a FAT32 system partition. Set `DEVICE` to a whole disk such as `/dev/sda` or `/dev/nvme0n1` to install to hardware. Use `make install-image` to create a bootable raw disk image or `make install-vdi` to create a VirtualBox disk image. Each image operation requires root access for loop devices and asks you to type the output path before writing. `MODE` can be `uefi`, `bios`, or `both`. The partition editor opens `cfdisk` after a confirmation. Host installation needs util-linux, dosfstools, and GRUB tools for each selected firmware mode.

The live kernel detects IDE, SATA AHCI, and NVMe disks through the block-device layer. The install tools still run on Linux because GRUB installation and disk partitioning are not implemented in the kernel shell. IDE uses legacy PIO. AHCI and NVMe use polling and DMA buffers below 4 GiB. The initial drivers support 512-byte logical sectors. The installed kernel still uses a RAM filesystem, so files do not persist across reboots. The hardware installer and partition editor modify real disks.

## Contributing

Keep hardware-specific operations behind driver or architecture interfaces, and keep public declarations in `kernel/include/kern/`. New code should explain why it exists, ownership/lifetime rules, and hardware assumptions. Add a host test where practical. Hardware-dependent paths should document a manual QEMU or device test procedure.
