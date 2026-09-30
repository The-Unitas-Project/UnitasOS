# Security notes

Treat firmware data, disk data, and device input as untrusted. Check every size, address, and range before use.

## Memory

The kernel requires CPU support for the NX page bit. It maps kernel text as read-only and executable. It maps kernel data as writable and non-executable. It keeps the identity map supervisor-only and enables supervisor write protection.

User programs run in ring 3. Their pages use user page-table flags. Syscall handlers check user ranges before they copy data between user and kernel memory.

Unitas does not have user IDs or capability checks. Any user program can call the Linux `reboot` system call to halt, reboot, or power off the machine. Do not run untrusted programs.

## Storage

The block layer checks each sector range before it calls a device driver. The partition scanner checks MBR and GPT ranges. It checks both GPT copies and their CRC values. It rejects overlapping partitions and valid GPT copies that disagree.

The FAT32 disk root reads metadata from an untrusted block device and mounts it read-only. It accepts a volume label and required program paths as root-selection checks. It does not verify program signatures or file checksums.

A kernel block handle can write any byte in a writable disk or partition. A write can change a partition table or file data. User programs cannot open raw disk paths under `/dev`.

The installer erases its target disk or image. It asks for the target path and the word `WIPE` before it writes.

## Device state

The block registry and partition registry use fixed-size tables. Register devices before the kernel starts user code. Do not register devices from concurrent tasks.

The Ethernet driver accepts DHCP replies that match the active transaction and MAC address. DHCP does not authenticate its server. Do not use the assigned gateway or DNS data as trusted input.

## Review

Build both normal and serial kernels after changes to memory, syscall, or device code. Inspect disk writes and user-pointer checks during review. Boot the kernel in QEMU before release.
