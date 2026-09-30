#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 4 ]; then
    printf 'Usage: %s OUTPUT.vdi [uefi|bios|both] [gpt|mbr] [ext4|fat32|btrfs]\n' "$0" >&2
    exit 2
fi
vdi=$1
mode=${2:-both}
table=${3:-gpt}
root_fs=${4:-ext4}
raw="$vdi.raw"

if ! command -v VBoxManage >/dev/null 2>&1; then
    printf '%s\n' "VBoxManage is required to create a VirtualBox disk image." >&2
    exit 1
fi
if [ -e "$vdi" ] || [ -e "$raw" ]; then
    printf '%s\n' "The output VDI or temporary raw image already exists." >&2
    exit 1
fi
./tools/install-image.sh "$raw" "$mode" "$table" "$root_fs"
VBoxManage convertfromraw "$raw" "$vdi" --format VDI
printf 'Installed VirtualBox disk image: %s\n' "$vdi"
printf 'Use make run-vbox DISK=%s BOOT=disk with a new VM_NAME.\n' "$vdi"
