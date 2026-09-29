# Set CROSS_COMPILE to the prefix for the freestanding x86_64 toolchain.
CROSS_COMPILE ?= x86_64-elf-
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)ld
GRUB_MKRESCUE ?= grub-mkrescue
QEMU ?= qemu-system-x86_64
DEVICE ?=
MODE ?= both
DISK ?=
DISK_FORMAT ?= raw
DISK_SIZE ?= 2G
STORAGE ?= ahci
VM_NAME ?= UnitasOS
BOOT ?= live
FIRMWARE ?= bios
IMAGE ?= build/unitasos-installed.raw
VDI ?= build/unitasos-installed.vdi
BUILD := build
KERNEL := $(BUILD)/kernel.elf
ISO := $(BUILD)/unitasos.iso

CPPFLAGS := -Ikernel/include
CFLAGS := -std=gnu11 -ffreestanding -fno-stack-protector -fno-pic \
          -fno-pie -mno-red-zone -mgeneral-regs-only -mcmodel=small \
          -Wall -Wextra -Werror \
          -O2 -g
ASFLAGS := -ffreestanding -fno-pic -fno-pie -mno-red-zone -mcmodel=small
LDFLAGS := -nostdlib -z noexecstack -z max-page-size=0x1000 -T linker.ld

C_SOURCES := $(shell find kernel -name '*.c' -print)
S_SOURCES := $(shell find kernel boot -name '*.S' -print)
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(C_SOURCES)) \
           $(patsubst %.S,$(BUILD)/%.o,$(S_SOURCES))
DEPFILES := $(patsubst %.o,%.d,$(OBJECTS))

.PHONY: all iso run run-qemu run-vbox disk-image test install install-image install-vdi partition-edit clean
all: $(KERNEL)

$(KERNEL): $(OBJECTS) linker.ld
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASFLAGS) -c $< -o $@

iso: $(KERNEL)
	@mkdir -p $(BUILD)/iso/boot/grub $(BUILD)/iso/tools
	cp $(KERNEL) $(BUILD)/iso/boot/kernel.elf
	cp boot/grub.cfg $(BUILD)/iso/boot/grub/grub.cfg
	cp tools/install.sh tools/install-image.sh tools/install-vdi.sh \
	   tools/partition-editor.sh tools/run-qemu.sh tools/run-virtualbox.sh \
	   README.md CONTRIBUTING.md $(BUILD)/iso/tools/
	$(GRUB_MKRESCUE) -o $(ISO) $(BUILD)/iso

run: $(ISO)
	QEMU="$(QEMU)" ./tools/run-qemu.sh "$(ISO)" "$(STORAGE)" \
	   "$(DISK)" "$(DISK_FORMAT)" "$(BOOT)" "$(FIRMWARE)"

run-qemu: run

run-vbox: $(ISO)
	./tools/run-virtualbox.sh "$(ISO)" "$(VM_NAME)" "$(STORAGE)" \
	   "$(DISK)" "$(BOOT)" "$(FIRMWARE)"

disk-image:
	@test -n "$(DISK)" || { echo 'Set DISK to an output path such as build/disk.raw'; exit 2; }
	@mkdir -p "$(dir $(DISK))"
	qemu-img create -f "$(DISK_FORMAT)" "$(DISK)" "$(DISK_SIZE)"

test:
	$(MAKE) -C tests run

install: $(KERNEL)
	@test -n "$(DEVICE)" || { echo 'Set DEVICE to a whole disk such as /dev/sda or /dev/nvme0n1'; exit 2; }
	./tools/install.sh "$(DEVICE)" "$(MODE)"

install-image: $(KERNEL)
	./tools/install-image.sh "$(IMAGE)" "$(MODE)"

install-vdi: $(KERNEL)
	./tools/install-vdi.sh "$(VDI)" "$(MODE)"

partition-edit:
	@test -n "$(DEVICE)" || { echo 'Set DEVICE to a whole disk such as /dev/sda or /dev/nvme0n1'; exit 2; }
	./tools/partition-editor.sh "$(DEVICE)"

clean:
	rm -rf $(BUILD) tests/build

-include $(DEPFILES)
