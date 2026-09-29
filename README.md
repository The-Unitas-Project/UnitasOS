# UnitasOS

UnitasOS is an experimental x86_64 kernel. It boots with the Multiboot2 protocol. It has memory, interrupt, driver, and file system code.

## Build and boot

Install these tools to build the kernel:

- `make`
- `x86_64-elf-gcc`
- `x86_64-elf-ld`

Install `grub-mkrescue` and `xorriso` to build an ISO. Install QEMU to boot the ISO with `make run`.

On macOS, install the cross compiler and binutils with MacPorts:

```sh
sudo port install x86_64-elf-gcc x86_64-elf-binutils
```

Use Docker or a Linux environment to run `grub-mkrescue` on macOS. Set `CROSS_COMPILE` if your tool names use a different prefix. You can build the kernel ELF without GRUB or QEMU.

### Build commands

```sh
make                 # Build build/kernel.elf.
make iso             # Build build/unitasos.iso.
make serial=1        # Build with serial input and output.
make serial=1 iso    # Build a serial ISO in build-serial/.
make test            # Run host-side unit tests.
```

Serial input and output are off by default. Add `serial=1` to each build or run command to turn them on.

### Run with QEMU

```sh
make run
make serial=1 run
make serial=1 run USB=1
QEMU_DISPLAY=none make serial=1 run
FIRMWARE=uefi make run
```

The first command uses the QEMU display. The second command sends the serial console to the terminal. The third command adds an xHCI controller and USB keyboard. The fourth command uses the serial console only. The fifth command boots the ISO with UEFI firmware.

The kernel can find the xHCI controller. It cannot read USB keyboard reports yet. Use `USB=1` to check controller discovery.

QEMU uses OVMF for UEFI boot. Set `QEMU_UEFI_FIRMWARE` if OVMF is outside the usual Linux paths. For example, set it to `/path/to/OVMF_CODE.fd`.

### Run with VirtualBox

```sh
make run-vbox STORAGE=ahci DISK=build/unitasos.vdi BOOT=disk VM_NAME=UnitasAHCI
make run-vbox STORAGE=nvme DISK=build/unitasos.vdi BOOT=disk FIRMWARE=uefi VM_NAME=UnitasNVMe
```

GRUB loads the ELF kernel with Multiboot2. `boot/boot.S` starts in 32-bit protected mode. It sets up page tables, enables 64-bit mode, and calls `kernel_main`. The page tables map the first 4 GiB to the same physical addresses. They use 2 MiB pages.

## Install to a disk

The install tools run on Linux. They use GRUB and a FAT32 system partition. They erase the selected disk. Check the device name before you confirm the install.

Set `DEVICE` to a whole disk, such as `/dev/sda` or `/dev/nvme0n1`. Set `MODE` to `uefi`, `bios`, or `both`. Run `make install` to install to a disk.

Run `make install-image` to create a raw disk image. Run `make install-vdi` to create a VirtualBox disk image. These commands need root access to set up loop devices. Each command asks you to confirm the output path.

The install tools need util-linux, dosfstools, and GRUB tools for each selected boot mode. The partition editor uses `cfdisk` after you confirm the target disk.

The install scripts create a GPT partition table. They support BIOS boot, UEFI boot, or both. GRUB starts the kernel with Multiboot2 in either mode. The kernel does not use UEFI services after GRUB starts it.

The kernel can find IDE, SATA AHCI, and NVMe disks. The block layer uses polling for disk commands. AHCI and NVMe use DMA buffers below 4 GiB. The disk drivers need 512-byte logical sectors.

The kernel scans GPT and MBR primary and logical partitions. It adds each partition to the block layer. The `/dev` file system lists whole disks and partitions as byte devices. Use `vfs_seek` to select a byte offset, then read or write disk data, including MBR and GPT sectors. Raw writes can change disk data.

The `/dev` file system also provides `/dev/console`, `/dev/kbd`, `/dev/null`, and `/dev/zero`. Serial builds also provide `/dev/serial0`. USB storage devices will appear after a USB storage driver registers them with the block layer. The kernel does not have a framebuffer device yet.

The install tools run on Linux because the kernel shell cannot install GRUB or edit partitions. The kernel formats a RAM FAT32 file system at each boot. Files in this file system do not persist after a reboot.

## Contribute

Keep hardware access in drivers or architecture code. Put public declarations in `kernel/include/kern/`. In code comments, explain contracts, ownership, limits, and hardware requirements. Add host tests for reusable code when useful. Document a QEMU or device test for hardware code.

Read [CONTRIBUTING.md](CONTRIBUTING.md) for kernel rules. Read [ROADMAP.md](ROADMAP.md) for planned work on input, disk storage, file paths, user mode, network access, and graphics.

Read [SECURITY.md](SECURITY.md) for current device access and kernel security limits.
