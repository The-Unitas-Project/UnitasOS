#!/bin/sh
set -eu

iso=$1
storage=$2
disk=$3
disk_format=$4
boot=$5
firmware=$6
qemu=${QEMU:-qemu-system-x86_64}
display=${QEMU_DISPLAY:-none}

if [ ! -f "$iso" ]; then
    printf 'ISO image not found: %s\n' "$iso" >&2
    exit 1
fi
if [ -n "$disk" ] && [ ! -f "$disk" ]; then
    printf 'Disk image not found: %s\n' "$disk" >&2
    exit 1
fi

if [ "$boot" = "disk" ]; then
    boot_order=c
elif [ "$boot" = "live" ]; then
    boot_order=d
else
    printf 'Unknown boot selection: %s\n' "$boot" >&2
    exit 2
fi
set -- "$qemu" -m 256M -cdrom "$iso" -display "$display" -serial stdio -no-reboot -no-shutdown \
    -boot "order=$boot_order"
if [ "$firmware" = "uefi" ]; then
    firmware_image=
    for candidate in /usr/share/OVMF/OVMF_CODE.fd \
                     /usr/share/edk2/x64/OVMF_CODE.fd \
                     /usr/share/edk2-ovmf/x64/OVMF_CODE.fd; do
        if [ -f "$candidate" ]; then firmware_image=$candidate; break; fi
    done
    if [ -z "$firmware_image" ]; then
        printf '%s\n' "OVMF firmware was not found. Install an OVMF package or use FIRMWARE=bios." >&2
        exit 1
    fi
    set -- "$@" -bios "$firmware_image"
elif [ "$firmware" != "bios" ]; then
    printf 'Unknown firmware mode: %s\n' "$firmware" >&2
    exit 2
fi
if [ -n "$disk" ]; then
    case "$storage" in
        ide)
            set -- "$@" -drive "file=$disk,format=$disk_format,if=ide"
            ;;
        ahci)
            set -- "$@" -drive "file=$disk,format=$disk_format,if=none,id=unitas-disk"
            set -- "$@" -device ich9-ahci,id=unitas-ahci
            set -- "$@" -device ide-hd,drive=unitas-disk,bus=unitas-ahci.0
            ;;
        nvme)
            set -- "$@" -drive "file=$disk,format=$disk_format,if=none,id=unitas-disk"
            set -- "$@" -device nvme,id=unitas-nvme,serial=UNITASNVME
            set -- "$@" -device nvme-ns,drive=unitas-disk,nsid=1
            ;;
        *)
            printf 'Unknown storage controller: %s\n' "$storage" >&2
            exit 2
            ;;
    esac
fi
exec "$@"
