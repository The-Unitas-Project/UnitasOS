# Security notes

These notes describe the current kernel limits. Treat all disk data and device input as untrusted.

## Raw disk access

The `/dev` file system exposes whole disks and partitions. A VFS handle can read or write any byte in a writable block device. A write can change a partition table or file data.

The kernel does not have user mode or process access checks. Add read and write permissions before user programs can open device files.

## Partition data

The scanner checks GPT header and entry-array CRC values. It checks partition bounds and rejects overlapping partitions. It limits the MBR extended-partition chain to 128 entries and detects loops.

The scanner reads the primary GPT only. It does not use a backup GPT when the primary copy is damaged. It supports up to 128 entries of 128 bytes each. Reject unsupported tables. Do not trust CRC values as proof that disk data is safe.

## Device registry

The block registry accepts path-safe device names. The block and partition registries use fixed-size arrays. They do not support concurrent registration. Add locks before drivers register devices from multiple CPUs or tasks.

## Input and boot

The USB code detects controller candidates. It does not start a USB controller or read USB reports. The kernel does not have a USB mass-storage or framebuffer driver. Do not treat these devices as supported.

GRUB can start the kernel through Multiboot2 with BIOS or UEFI firmware. The kernel does not use UEFI services after the handoff.

The host installer can erase the selected disk. It requires the user to type `WIPE` and the target path before it writes the disk.

## Review status

Both normal and serial kernel builds pass. Shell syntax and whitespace checks pass. QEMU and `grub-mkrescue` are not installed in this environment. The kernel has not been booted during this review.
