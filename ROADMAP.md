# UnitasOS roadmap

This roadmap lists the requested system goals. Build each goal in code. Keep interfaces consistent with the Linux x86-64 ABI or the Unitas ABI.

Do not clone the full GCC or Linux source trees. Use the installed host compiler and linker. Fetch only the upstream source files or packages that a task needs. The current kernel target is x86-64.

## Power control

ACPI table checks and Linux `reboot` system-call support are in place. User programs in `/bin` provide `reboot`, `poweroff`, and `shutdown`. Add suspend and resume support. The current power controls support x86 legacy PM1 I/O registers. Add user and permission checks before the system has multiple users.

Check this goal with BIOS and UEFI virtual machines. Shut down, reboot, suspend, and resume each machine.

## Persistent storage

The installer creates MBR or GPT layouts, a BIOS boot area when GPT needs it, a separate EFI System Partition for UEFI, and a root partition. It formats the root partition as ext4 by default and also accepts FAT32 or Btrfs. It copies the kernel and built programs to the root partition and installs GRUB for BIOS or UEFI boot. The kernel can mount a labelled FAT32 root read-only when it finds the expected programs in `/bin`.

The kernel uses the RAM FAT32 root for ext4 and Btrfs installs because those runtime drivers are not present. Add writable FAT32 support, then add an ext4 driver and mount the default installed root. Add Btrfs runtime support only after its tree, checksum, and copy-on-write rules have a complete driver.

Check this goal with disposable MBR and GPT images. Create files, reboot, and read the files again.

## Device support

Register USB storage, NVMe, SATA, IDE, display, and input devices under `/dev`. Keep block devices and character devices behind VFS handles.

Check each driver with QEMU and supported hardware. Read and write data through the same file calls that regular files use.

## Network access

The Intel 82540EM driver sends and receives Ethernet frames. The network code requests IPv4 settings through DHCP and reports them in `/proc/net/ipv4`. Add ARP, general UDP, TCP, sockets, and user tools that use network services.

Check this goal by requesting a DHCP lease and exchanging data with a test service.

## Userland

The default `make` target builds static GNU libc, GNU Coreutils, and Bash. It embeds these programs in the kernel image. It provides `/bin/bash` and `/bin/sh` as names for Bash. The kernel displays `Welcome to GNU/Unitas` before it checks for an init program. Add and validate systemd as PID 1.

Complete process control so Bash can run external programs. Complete file, directory, terminal, signal, memory, time, and locale behavior. Run GNU programs from the persistent root file system. Check the ABI before you claim support for GNU libc, GNU Coreutils, Bash, or POSIX shell scripts.

Port GNU Guile and GNU Shepherd after the kernel supports process creation, wait, and signals. Shepherd is GNU's init and service manager. It uses Guile Scheme.

## Graphical desktop

Add a framebuffer, a graphics console, mouse input, and a basic window system. Keep serial output available in `serial=1` builds.

Check this goal by starting the graphical shell and a user program. Keep serial output for fault checks.
