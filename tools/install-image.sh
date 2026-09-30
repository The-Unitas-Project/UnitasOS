#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 4 ]; then
    printf 'Usage: %s OUTPUT.raw [uefi|bios|both] [gpt|mbr] [ext4|fat32|btrfs]\n' "$0" >&2
    exit 2
fi
image=$1
mode=${2:-both}
table=${3:-gpt}
root_fs=${4:-ext4}
kernel_image=${UNITAS_KERNEL_IMAGE:-build/kernel.elf}
userland_dir=${UNITAS_USERLAND_DIR:-build/user}
size=${UNITAS_IMAGE_SIZE:-8G}

case "$mode" in
    uefi|bios|both) ;;
    *) printf 'Unknown boot mode: %s\n' "$mode" >&2; exit 2 ;;
esac
case "$table" in
    gpt|mbr) ;;
    *) printf 'Unknown partition table: %s\n' "$table" >&2; exit 2 ;;
esac
case "$root_fs" in
    ext4) root_mkfs=mkfs.ext4 ;;
    fat32) root_mkfs=mkfs.fat ;;
    btrfs) root_mkfs=mkfs.btrfs ;;
    *) printf 'Unknown root file system: %s\n' "$root_fs" >&2; exit 2 ;;
esac
if [ "$table" = "mbr" ] && [ "$mode" != "bios" ]; then
    printf '%s\n' "The MBR layout supports BIOS boot only." >&2
    exit 2
fi
if [ -e "$image" ] || [ -L "$image" ]; then
    printf 'Output path already exists: %s\n' "$image" >&2
    exit 1
fi

required_tools="truncate losetup lsblk sfdisk mount umount grub-install grep cp mkdir $root_mkfs"
if [ "$mode" = "uefi" ] || [ "$mode" = "both" ]; then
    required_tools="$required_tools mkfs.fat"
fi
for tool in $required_tools; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'Required host tool is missing: %s\n' "$tool" >&2
        exit 1
    }
done
if [ "$(id -u)" -ne 0 ]; then
    printf '%s\n' "Run image installation as root to create a loop device." >&2
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
if losetup -j "$image" | grep -q .; then
    printf 'The image is already attached to a loop device: %s\n' "$image" >&2
    exit 1
fi

printf 'This creates a bootable %s raw disk image with size %s.\n' "$image" "$size"
printf 'Root file system: %s\n' "$root_fs"
printf 'Type CREATE %s to continue: ' "$image"
IFS= read -r confirmation
if [ "$confirmation" != "CREATE $image" ]; then
    printf '%s\n' "Image installation cancelled."
    exit 1
fi

mkdir -p "$(dirname "$image")"
truncate -s "$size" "$image"
loop_device=$(losetup --find --show --partscan "$image")
cleanup() { losetup --detach "$loop_device"; }
trap cleanup EXIT HUP INT TERM
printf 'WIPE %s\n' "$loop_device" | UNITAS_ALLOW_LOOP=1 \
    ./tools/install.sh "$loop_device" "$mode" "$table" "$root_fs"
printf 'Installed raw disk image: %s\n' "$image"
