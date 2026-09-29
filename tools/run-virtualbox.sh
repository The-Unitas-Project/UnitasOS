#!/bin/sh
set -eu

iso=$1
vm_name=$2
storage=$3
disk=$4
boot=$5
firmware=$6
vbox=${VBOXMANAGE:-VBoxManage}

if [ ! -f "$iso" ]; then
    printf 'ISO image not found: %s\n' "$iso" >&2
    exit 1
fi
if ! command -v "$vbox" >/dev/null 2>&1; then
    printf '%s\n' "VBoxManage is required to create and launch the VM." >&2
    exit 1
fi
case "$boot" in
    live|disk) ;;
    *) printf 'Unknown boot selection: %s\n' "$boot" >&2; exit 2 ;;
esac
case "$firmware" in
    bios|uefi) ;;
    *) printf 'Unknown firmware mode: %s\n' "$firmware" >&2; exit 2 ;;
esac
if "$vbox" showvminfo "$vm_name" >/dev/null 2>&1; then
    printf 'A VM named %s already exists. Choose another VM_NAME or start it directly.\n' "$vm_name" >&2
    exit 1
fi
if [ -z "$disk" ]; then
    disk=build/unitasos.vdi
fi
if [ ! -f "$disk" ]; then
    mkdir -p "$(dirname "$disk")"
    "$vbox" createmedium disk --filename "$disk" --size 4096 --format VDI
fi

case "$storage" in
    ide|ahci|nvme) ;;
    *) printf 'Unknown storage controller: %s\n' "$storage" >&2; exit 2 ;;
esac

"$vbox" createvm --name "$vm_name" --ostype Other_64 --register
if [ "$boot" = "disk" ]; then
    boot1=disk
    boot2=dvd
else
    boot1=dvd
    boot2=disk
fi
if [ "$firmware" = "uefi" ]; then
    firmware_option=efi
else
    firmware_option=bios
fi
"$vbox" modifyvm "$vm_name" --memory 256 --vram 16 --firmware "$firmware_option" \
    --boot1 "$boot1" --boot2 "$boot2" --chipset ich9
"$vbox" storagectl "$vm_name" --name IDE --add ide --controller PIIX4
"$vbox" storageattach "$vm_name" --storagectl IDE --port 1 --device 0 \
    --type dvddrive --medium "$iso"

case "$storage" in
    ide)
        "$vbox" storageattach "$vm_name" --storagectl IDE --port 0 --device 0 \
            --type hdd --medium "$disk"
        ;;
    ahci)
        "$vbox" storagectl "$vm_name" --name SATA --add sata \
            --controller IntelAhci --portcount 4
        "$vbox" storageattach "$vm_name" --storagectl SATA --port 0 --device 0 \
            --type hdd --medium "$disk"
        ;;
    nvme)
        "$vbox" storagectl "$vm_name" --name NVMe --add pcie \
            --controller NVMe --portcount 1
        "$vbox" storageattach "$vm_name" --storagectl NVMe --port 0 --device 0 \
            --type hdd --medium "$disk"
        ;;
esac

printf 'Starting %s with %s storage and disk %s\n' "$vm_name" "$storage" "$disk"
"$vbox" startvm "$vm_name" --type gui
