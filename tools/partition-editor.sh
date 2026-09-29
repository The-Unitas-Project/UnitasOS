#!/bin/sh
set -eu

if [ "$#" -ne 1 ] || [ ! -b "$1" ]; then
    printf 'Usage: %s WHOLE_DISK_DEVICE\n' "$0" >&2
    exit 2
fi
device=$1
if [ "$(lsblk -ndo TYPE "$device")" != "disk" ]; then
    printf 'Target must be a whole disk device: %s\n' "$device" >&2
    exit 1
fi
if [ "$(id -u)" -ne 0 ]; then
    printf '%s\n' "Run the partition editor as root." >&2
    exit 1
fi
if lsblk -nrpo MOUNTPOINT "$device" | grep -q '[^[:space:]]'; then
    printf '%s\n' "Unmount every partition on the target disk first." >&2
    exit 1
fi
if ! command -v cfdisk >/dev/null 2>&1; then
    printf '%s\n' "Install cfdisk from util-linux to edit the partition table." >&2
    exit 1
fi

printf 'Editing %s can make its data inaccessible. Continue with cfdisk? [y/N] ' "$device"
IFS= read -r answer
case "$answer" in
    y|Y|yes|YES) exec cfdisk "$device" ;;
    *) printf '%s\n' "Partition editing cancelled." ;;
esac
