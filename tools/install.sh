#!/bin/sh
set -eu

usage() {
    printf '%s\n' "Usage: $0 DEVICE [uefi|bios|both] [gpt|mbr] [ext4|fat32|btrfs] [--dry-run]"
    printf '%s\n' "The root file system defaults to ext4. UEFI installs use a separate FAT32 EFI partition."
}

if [ "$#" -lt 1 ] || [ "$#" -gt 5 ]; then
    usage
    exit 2
fi

device=$1
mode=${2:-both}
table=${UNITAS_PARTITION_TABLE:-gpt}
if [ "$#" -ge 3 ]; then table=$3; fi
root_fs=${4:-ext4}
kernel_image=${UNITAS_KERNEL_IMAGE:-build/kernel.elf}
userland_dir=${UNITAS_USERLAND_DIR:-build/user}
gnu_bin_dir=${UNITAS_GNU_BIN_DIR:-}
dry_run=false
if [ "$mode" = "--dry-run" ]; then
    mode=both
    table=gpt
    dry_run=true
fi
if [ "$table" = "--dry-run" ]; then
    table=gpt
    dry_run=true
fi
if [ "$root_fs" = "--dry-run" ]; then
    root_fs=ext4
    dry_run=true
fi
if [ "$#" -eq 5 ]; then
    [ "$5" = "--dry-run" ] || { usage; exit 2; }
    dry_run=true
fi

case "$mode" in
    uefi|bios|both) ;;
    *) usage; exit 2 ;;
esac
case "$table" in
    gpt|mbr) ;;
    *) usage; exit 2 ;;
esac
case "$root_fs" in
    ext4|fat32|btrfs) ;;
    *) usage; exit 2 ;;
esac
if [ "$table" = "mbr" ] && [ "$mode" != "bios" ]; then
    printf '%s\n' "The MBR layout supports BIOS boot only." >&2
    exit 2
fi

if [ ! -b "$device" ]; then
    printf 'Target is not a block device: %s\n' "$device" >&2
    exit 1
fi
required_tools="lsblk sfdisk mount umount grub-install grep cp mkdir"
case "$root_fs" in
    ext4) required_tools="$required_tools mkfs.ext4" ;;
    fat32) required_tools="$required_tools mkfs.fat" ;;
    btrfs) required_tools="$required_tools mkfs.btrfs" ;;
esac
if [ "$mode" = "uefi" ] || [ "$mode" = "both" ]; then
    required_tools="$required_tools mkfs.fat"
fi
for tool in $required_tools; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'Required host tool is missing: %s\n' "$tool" >&2
        exit 1
    }
done
device_type=$(lsblk -ndo TYPE "$device")
if [ "$device_type" != "disk" ] &&
   ! { [ "${UNITAS_ALLOW_LOOP:-0}" = "1" ] && [ "$device_type" = "loop" ]; }; then
    printf 'Target must be a whole disk device: %s\n' "$device" >&2
    exit 1
fi
if [ "$table" = "mbr" ]; then
    device_bytes=$(lsblk -bndo SIZE "$device")
    if [ "$device_bytes" -gt 2199023255552 ]; then
        printf '%s\n' "MBR with 512-byte sectors cannot address a disk larger than 2 TiB." >&2
        exit 1
    fi
fi
if lsblk -nrpo MOUNTPOINT "$device" | grep -q '[^[:space:]]'; then
    printf 'A target partition is mounted. Unmount it before continuing.\n' >&2
    exit 1
fi

if [ ! -f "$kernel_image" ] || [ ! -f boot/grub.cfg ]; then
    printf '%s\n' "Build the kernel first with make."
    exit 1
fi
for program in hello.elf reboot poweroff shutdown sh; do
    if [ ! -f "$userland_dir/$program" ]; then
        printf 'Built user program is missing: %s/%s\n' "$userland_dir" "$program" >&2
        exit 1
    fi
done
if [ -n "$gnu_bin_dir" ] && [ ! -d "$gnu_bin_dir" ]; then
    printf 'GNU command directory is missing: %s\n' "$gnu_bin_dir" >&2
    exit 1
fi

if [ "$table" = "mbr" ]; then
    root_partition_number=1
    if [ "$root_fs" = "fat32" ]; then root_type=c; else root_type=83; fi
    layout=$(printf 'label: dos\nunit: sectors\nstart=2048, type=%s, bootable\n' "$root_type")
elif [ "$mode" = "uefi" ]; then
    esp_partition_number=1
    root_partition_number=2
    layout=$(printf 'label: gpt\nunit: sectors\nstart=2048, size=2097152, type=U, name="UnitasOS EFI"\nstart=2099200, type=L, name="UnitasOS root"\n')
elif [ "$mode" = "bios" ]; then
    root_partition_number=2
    layout=$(printf 'label: gpt\nunit: sectors\nstart=2048, size=2048, type=21686148-6449-6e6f-744e-656564454649, name="BIOS boot"\nstart=4096, type=L, name="UnitasOS root"\n')
else
    esp_partition_number=2
    root_partition_number=3
    layout=$(printf 'label: gpt\nunit: sectors\nstart=2048, size=2048, type=21686148-6449-6e6f-744e-656564454649, name="BIOS boot"\nstart=4096, size=2097152, type=U, name="UnitasOS EFI"\nstart=2101248, type=L, name="UnitasOS root"\n')
fi

printf 'Target disk: %s\n' "$device"
printf 'Boot mode: %s\n' "$mode"
printf 'Root file system: %s\n' "$root_fs"
printf '%s\n' "The selected disk will be repartitioned and all existing data will be lost."
if [ "$dry_run" = true ]; then
    printf '%s\n' "Dry run. The partition table was not changed."
    printf '%s\n' "$layout"
    exit 0
fi
if [ "$(id -u)" -ne 0 ]; then
    printf '%s\n' "Run the installer as root after reviewing the target device." >&2
    exit 1
fi
printf 'Type WIPE %s to continue: ' "$device"
IFS= read -r confirmation
if [ "$confirmation" != "WIPE $device" ]; then
    printf '%s\n' "Installation cancelled."
    exit 1
fi

printf '%s\n' "$layout" | sfdisk --wipe always --wipe-partitions always "$device"
if command -v partprobe >/dev/null 2>&1
then
    partprobe "$device"
fi
if command -v udevadm >/dev/null 2>&1
then
    udevadm settle
fi

partition_path() {
    case "$(basename "$device")" in
        *[0-9]) printf '%sp%s\n' "$device" "$1" ;;
        *) printf '%s%s\n' "$device" "$1" ;;
    esac
}
root_partition=$(partition_path "$root_partition_number")
if [ ! -b "$root_partition" ]; then
    printf 'Root partition device did not appear: %s\n' "$root_partition" >&2
    exit 1
fi
if [ "$mode" = "uefi" ] || [ "$mode" = "both" ]; then
    esp_partition=$(partition_path "$esp_partition_number")
    if [ ! -b "$esp_partition" ]; then
        printf 'EFI partition device did not appear: %s\n' "$esp_partition" >&2
        exit 1
    fi
fi

mountpoint=$(mktemp -d)
root_mounted=false
esp_mounted=false
esp_mountpoint="$mountpoint/boot/efi"
cleanup() {
    if [ "$esp_mounted" = true ]; then umount "$esp_mountpoint"; fi
    if [ "$root_mounted" = true ]; then umount "$mountpoint"; fi
    rmdir "$mountpoint"
}
trap cleanup EXIT HUP INT TERM

case "$root_fs" in
    ext4) mkfs.ext4 -F -L UNITASOS "$root_partition" ;;
    fat32) mkfs.fat -F 32 -n UNITASOS "$root_partition" ;;
    btrfs) mkfs.btrfs -f -L UNITASOS "$root_partition" ;;
esac
mount "$root_partition" "$mountpoint"
root_mounted=true
mkdir -p "$mountpoint/bin" "$mountpoint/boot/grub" "$mountpoint/dev" \
    "$mountpoint/etc" "$mountpoint/home" "$mountpoint/lib" \
    "$mountpoint/lib64" "$mountpoint/media" "$mountpoint/mnt" \
    "$mountpoint/opt" "$mountpoint/proc" "$mountpoint/root" \
    "$mountpoint/run" "$mountpoint/sbin" "$mountpoint/srv" \
    "$mountpoint/sys" "$mountpoint/tmp" "$mountpoint/usr" \
    "$mountpoint/var"
cp "$kernel_image" "$mountpoint/boot/kernel.elf"
cp boot/grub.cfg "$mountpoint/boot/grub/grub.cfg"
cp "$userland_dir/hello.elf" "$mountpoint/bin/hello.elf"
cp "$userland_dir/reboot" "$mountpoint/bin/reboot"
cp "$userland_dir/poweroff" "$mountpoint/bin/poweroff"
cp "$userland_dir/shutdown" "$mountpoint/bin/shutdown"
if [ -n "$gnu_bin_dir" ]; then
    if [ ! -x "$gnu_bin_dir/bash" ] || [ ! -x "$gnu_bin_dir/sh" ]; then
        printf '%s\n' 'GNU Bash and its sh command name are required.' >&2
        exit 1
    fi
    for program in "$gnu_bin_dir"/*; do
        [ -f "$program" ] && [ -x "$program" ] || continue
        program_name=${program##*/}
        case "$program_name" in
            .|..) printf 'Invalid GNU command name: %s\n' "$program_name" >&2; exit 1 ;;
        esac
        cp "$program" "$mountpoint/bin/$program_name"
    done
fi

if [ "$mode" = "uefi" ] || [ "$mode" = "both" ]; then
    mkfs.fat -F 32 -n UNITAS_EFI "$esp_partition"
    mkdir -p "$esp_mountpoint"
    mount "$esp_partition" "$esp_mountpoint"
    esp_mounted=true
    grub-install --target=x86_64-efi --efi-directory="$esp_mountpoint" \
        --boot-directory="$mountpoint/boot" --removable --no-nvram
fi
if [ "$mode" = "bios" ] || [ "$mode" = "both" ]; then
    grub-install --target=i386-pc --boot-directory="$mountpoint/boot" \
        --recheck "$device"
fi
sync
printf 'UnitasOS installed to %s with %s root on %s\n' \
    "$device" "$root_fs" "$root_partition"
