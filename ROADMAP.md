# UnitasOS roadmap

This roadmap lists the requested system goals. Complete one goal at a time. Keep each change small. Boot and inspect the kernel after each change.

## 1. Input devices and terminal

The kernel has a PS/2 keyboard driver. It uses IRQ 1 and scan code set 1. It maps US letters, shifted symbols, caps lock, Enter, Tab, and Backspace. Use `make serial=1` to turn on serial input and output.

The USB code has a HID boot keyboard report decoder. It maps US keys and sends new key presses to the shared shell queue. The PCI scan reports xHCI and legacy USB controllers. The kernel does not start these controllers or send USB transfers. Run `make serial=1 run USB=1` to add an xHCI controller and a USB keyboard in QEMU. The kernel can report the controller. It cannot read keyboard reports yet. Add an xHCI driver that gets reports and calls the decoder. Add mouse input after keyboard reports work.

Check this goal:

- Type letters, shifted symbols, Enter, Tab, and Backspace with PS/2 and serial input.
- Start QEMU with a USB keyboard. Confirm that the shell receives its input.

## 2. Disk install and persistent files

Host scripts can create disk images and copy the kernel and GRUB files to a disk from Linux. The kernel scans GPT and MBR primary and logical partitions. It checks the GPT header and entry-array CRC values. The GPT scan supports at most 128 entries of 128 bytes each. It reads the primary GPT only. It does not recover from a damaged primary GPT.

The `/dev` file system lists whole disks and partitions as byte devices. For example, it uses `/dev/sda1` and `/dev/nvme0n1p1` for partitions. Use `vfs_seek` to select an offset. Read and write calls can access MBR and GPT sectors. Partial-sector writes use read-modify-write. Raw writes can change disk data. The kernel formats a RAM FAT32 file system at each boot, so files do not stay after reboot.

The host scripts install GRUB for BIOS, UEFI, or both. GRUB starts the kernel through Multiboot2. The kernel does not use UEFI services after GRUB starts it.

Boot test images in QEMU with BIOS and UEFI firmware. Check that the install tools write to the selected image or disk. Then mount a file system from a partition.

Check this goal:

- Install to a test disk image. Boot it with BIOS and UEFI firmware.
- Create a file, reboot, and read the same file.

## 3. Unix paths and device files

The VFS mounts `/dev` with console, keyboard, null, zero, disk, and partition devices. Serial builds also have `/dev/serial0`. The shell uses `/dev/console` for input and output.

Add directory support and more file system types to the VFS. Add a device registry for character devices. Add raw HID event files and `/dev/fb0` after the USB and framebuffer drivers work. Route all device access through VFS file handles.

Check this goal:

- Open, read, write, and close regular files and device files with the same VFS calls.
- Mount a persistent file system at `/`.
- Open, read, and write a block device through `/dev`.

## 4. User mode and user tools

Add page address spaces and access rules. Add page fault handling and a kernel stack for each task. Add a safe switch to ring 3, an ELF loader, and a system call interface.

Add access checks for raw disk devices before user programs can open them.

Start with one user process and a small `init` program. Add a scheduler and more processes after process start and exit work.

Check this goal:

- Load a user program from the mounted file system.
- Let it use console and file calls.
- Confirm that it cannot read or write kernel memory.

## 5. Network access

Add a driver for a supported Ethernet controller. Add Ethernet, ARP, IPv4, UDP, DHCP, and TCP. Add kernel calls and user tools after process and file interfaces work.

Check this goal:

- Get an IP address from a DHCP server in QEMU.
- Find a peer with ARP and send data to a test service.

## 6. Graphical desktop

Add a framebuffer interface, a graphics console, USB mouse input, and a basic window system. Keep the serial console for `serial=1` builds.

Check this goal:

- Boot to a graphical shell.
- Start a user program.
- Keep serial output for fault checks.
