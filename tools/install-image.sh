#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    printf 'Usage: %s OUTPUT.raw [uefi|bios|both]\n' "$0" >&2
    exit 2
fi
image=$1
mode=${2:-both}
size=${UNITAS_IMAGE_SIZE:-8G}

case "$mode" in
    uefi|bios|both) ;;
    *) printf 'Unknown boot mode: %s\n' "$mode" >&2; exit 2 ;;
esac
if [ -e "$image" ] || [ -L "$image" ]; then
    printf 'Output path already exists: %s\n' "$image" >&2
    exit 1
fi

for tool in truncate losetup lsblk sfdisk mkfs.fat mount umount grub-install; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'Required host tool is missing: %s\n' "$tool" >&2
        exit 1
    }
done
if [ "$(id -u)" -ne 0 ]; then
    printf '%s\n' "Run image installation as root to create a loop device." >&2
    exit 1
fi
if [ ! -f build/kernel.elf ] || [ ! -f boot/grub.cfg ]; then
    printf '%s\n' "Build the kernel first with make."
    exit 1
fi
if losetup -j "$image" | grep -q .; then
    printf 'The image is already attached to a loop device: %s\n' "$image" >&2
    exit 1
fi

printf 'This creates a bootable %s raw disk image with size %s.\n' "$image" "$size"
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
    ./tools/install.sh "$loop_device" "$mode"
printf 'Installed raw disk image: %s\n' "$image"
