#!/bin/sh
set -eu

usage() {
    printf '%s\n' "Usage: $0 DEVICE [uefi|bios|both] [--dry-run]"
    printf '%s\n' "The installer creates a GPT table and a FAT32 system partition."
}

if [ "$#" -lt 1 ] || [ "$#" -gt 3 ]; then
    usage
    exit 2
fi

device=$1
mode=${2:-both}
dry_run=false
if [ "$mode" = "--dry-run" ]; then
    mode=both
    dry_run=true
elif [ "$#" -eq 3 ]; then
    [ "$3" = "--dry-run" ] || { usage; exit 2; }
    dry_run=true
fi

case "$mode" in
    uefi|bios|both) ;;
    *) usage; exit 2 ;;
esac

if [ ! -b "$device" ]; then
    printf 'Target is not a block device: %s\n' "$device" >&2
    exit 1
fi
for tool in lsblk sfdisk mkfs.fat mount umount grub-install grep; do
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
if lsblk -nrpo MOUNTPOINT "$device" | grep -q '[^[:space:]]'; then
    printf 'A target partition is mounted. Unmount it before continuing.\n' >&2
    exit 1
fi

if [ ! -f build/kernel.elf ] || [ ! -f boot/grub.cfg ]; then
    printf '%s\n' "Build the kernel first with make iso."
    exit 1
fi

if [ "$mode" = "uefi" ]; then
    esp_number=1
    layout=$(printf 'label: gpt\nunit: sectors\nstart=2048, size=2097152, type=U, name="UnitasOS EFI"\n')
else
    esp_number=2
    if [ "$mode" = "bios" ]; then
        data_type=L
        data_name="UnitasOS system"
    else
        data_type=U
        data_name="UnitasOS EFI"
    fi
    layout=$(printf 'label: gpt\nunit: sectors\nstart=2048, size=2048, type=21686148-6449-6e6f-744e-656564454649, name="BIOS boot"\nstart=4096, size=2097152, type=%s, name="%s"\n' "$data_type" "$data_name")
fi

printf 'Target disk: %s\n' "$device"
printf 'Boot mode: %s\n' "$mode"
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

case "$(basename "$device")" in
    *[0-9]) partition="${device}p${esp_number}" ;;
    *) partition="${device}${esp_number}" ;;
esac
if [ ! -b "$partition" ]; then
    printf 'Partition device did not appear: %s\n' "$partition" >&2
    exit 1
fi

mountpoint=$(mktemp -d)
mounted=false
cleanup() {
    if [ "$mounted" = true ]; then umount "$mountpoint"; fi
    rmdir "$mountpoint"
}
trap cleanup EXIT HUP INT TERM

mkfs.fat -F 32 -n UNITASOS "$partition"
mount "$partition" "$mountpoint"
mounted=true
mkdir -p "$mountpoint/boot/grub"
cp build/kernel.elf "$mountpoint/boot/kernel.elf"
cp boot/grub.cfg "$mountpoint/boot/grub/grub.cfg"

if [ "$mode" = "uefi" ] || [ "$mode" = "both" ]; then
    grub-install --target=x86_64-efi --efi-directory="$mountpoint" \
        --boot-directory="$mountpoint/boot" --removable --no-nvram
fi
if [ "$mode" = "bios" ] || [ "$mode" = "both" ]; then
    grub-install --target=i386-pc --boot-directory="$mountpoint/boot" \
        --recheck "$device"
fi
sync
printf 'UnitasOS installed to %s\n' "$device"
