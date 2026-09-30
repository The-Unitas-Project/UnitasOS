# UnitasOS

UnitasOS is an x86-64 operating system kernel. It boots through the Multiboot2 protocol. The kernel supports ring 3 programs, a virtual file system, RAM and read-only disk FAT32 roots, and IDE, AHCI, and NVMe block devices.

## Build

The kernel target is x86-64. On Linux x86-64, the build uses installed Clang and the system ELF linker. If Clang is not installed, it uses `cc`. On macOS, Clang emits x86-64 ELF files and the build uses the x86-64 ELF linker. The build does not fetch compiler or kernel source trees.

On macOS Ventura, install the Apple command line tools and the MacPorts cross linker:

```sh
xcode-select --install
sudo port install x86_64-elf-binutils
```

On Linux x86-64, install Clang or GCC and GNU binutils. The build uses the host tools by default. Set `CROSS_COMPILE` only when your x86-64 ELF linker uses a different name prefix.

Run `make` to build GNU libc, GNU Coreutils, Bash, and the kernel. The build embeds the GNU programs in the kernel image. It uses Bash for both `/bin/bash` and `/bin/sh`.

```sh
make
make serial=1
make userland
```

Add `serial=1` to enable serial input and output. This build also creates the GNU userland. The build does not register `/dev/serial0` unless you set `serial=1`.

The `gnu-system` target is an alias for the default build. The GNU build downloads pinned source packages when they are not present. It does not download GCC or Linux source trees. A native Linux x86-64 build uses the host compiler and linker. It does not require `CROSS_COMPILE`.

GNU Shepherd is GNU's init and service manager. It uses GNU Guile, which this build does not include. The kernel also needs process creation and wait support before it can run Shepherd as PID 1.

Install GRUB, xorriso, and QEMU to create and boot an ISO:

```sh
make iso
make run
```

Use `FIRMWARE=uefi` to boot with UEFI firmware. Use `USB=1` to add a USB controller in QEMU.

## Memory and system files

The bootstrap maps the first 4 GiB with 2 MiB pages. Before it starts the heap, the kernel replaces pages that contain the kernel image with 4 KiB mappings. Kernel text is read-only and executable. Read-only data is not executable. Kernel data and RAM are writable and not executable. All kernel pages stay outside user access. The CPU must support the NX page bit.

The read-only `/proc` file system provides `/proc/meminfo`, `/proc/uptime`, `/proc/cpuinfo`, `/proc/mounts`, `/proc/partitions`, `/proc/self/status`, and `/proc/net/ipv4`. Each open file returns a snapshot. The partition file lists each block device name and its sector count. The network file reports link and DHCP state.

The kernel starts `/bin/sh`. This path is GNU Bash in `sh` compatibility mode. The power commands use the Linux `reboot` system call. The kernel flushes block devices before a power change. Shutdown uses ACPI S5 and requires valid tables with an S5 state and legacy PM1 I/O registers. Reboot uses the FADT reset register when firmware marks it as supported, then tries the keyboard controller. Unitas does not have user IDs or power-control permissions yet.

QEMU starts an Intel 82540EM network device by default. The driver polls Ethernet frames and uses DHCP to request an IPv4 address, gateway, and DNS server. The current network code does not provide general UDP, ARP, TCP, sockets, or application network access.

Linux x86-64 syscall numbers use the names in `abi/asm/unistd_64.h` and `abi/linux/syscall.h`. Unitas-specific calls use interrupt vector `0x81` and the headers in `abi/unitas/`.

## Storage

The kernel scans MBR and GPT partition tables. It validates partition bounds and both GPT copies. It can use a valid backup GPT when the primary copy is damaged. It exposes registered disks and partitions under `/dev`.

The installer runs on Linux. It needs util-linux, GRUB tools, and the formatter for the selected root file system. It creates a root partition with ext4 by default. Set `ROOT_FS=fat32` or `ROOT_FS=btrfs` to choose another format. UEFI layouts use a separate FAT32 EFI System Partition. GPT supports BIOS, UEFI, or both boot modes. MBR supports BIOS mode. The installer copies the kernel and built user programs to the root partition and installs GRUB.

At startup, the kernel looks for a FAT32 volume labelled `UNITASOS` that contains the expected `/bin` programs, including `/bin/bash` and `/bin/sh`. It mounts that root read-only. The disk driver supports short 8.3 names. If the root is ext4 or Btrfs, or if the FAT32 volume is missing, the kernel uses the RAM FAT32 root. The installer defaults to ext4, so set `ROOT_FS=fat32` to use the current disk-backed root driver. Ext4 and Btrfs runtime drivers are not available yet.

Create an ext4 raw image with `make install-image`. Choose another root format with `make install-image ROOT_FS=fat32` or `make install-image ROOT_FS=btrfs`. Create a VirtualBox image with `make install-vdi`. These commands need root access for loop devices. Review the target path before you confirm an install. The installer erases the target disk or image.

## Contribute

Use ASD-STE100 for comments, documentation, and docstrings. Write for programmers who know C and basic systems concepts. State contracts, ownership, limits, and hardware requirements.

Read [CONTRIBUTING.md](CONTRIBUTING.md) for code and build rules. Read [ROADMAP.md](ROADMAP.md) for planned system work. Read [SECURITY.md](SECURITY.md) before you change memory, user access, or disk code.
