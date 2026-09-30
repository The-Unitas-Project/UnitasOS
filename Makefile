# Select host tools for the x86-64 kernel.
HOST_OS := $(shell uname -s)
HOST_ARCH := $(shell uname -m)
HOST_CLANG := $(shell command -v clang 2>/dev/null)
ifneq ($(strip $(CROSS_COMPILE)),)
CC = clang --target=x86_64-elf
LD = $(CROSS_COMPILE)ld
else ifeq ($(HOST_OS),Darwin)
CC = clang --target=x86_64-elf
LD = x86_64-elf-ld
else ifneq ($(filter x86_64 amd64,$(HOST_ARCH)),)
ifneq ($(strip $(HOST_CLANG)),)
CC = clang
else
CC = cc
endif
LD = ld
else
$(error UnitasOS supports the x86_64 target. Set CROSS_COMPILE to an x86_64 ELF toolchain to cross-build from $(HOST_ARCH))
endif
PYTHON ?= python3
GRUB_MKRESCUE ?= grub-mkrescue
QEMU ?= qemu-system-x86_64
USB ?= 0
SERIAL ?= 0
serial ?= $(SERIAL)
ifeq ($(filter $(serial),0 1),)
$(error serial must be 0 or 1)
endif
ifeq ($(filter $(USB),0 1),)
$(error USB must be 0 or 1)
endif
DEVICE ?=
MODE ?= both
TABLE ?= gpt
DISK ?=
DISK_FORMAT ?= raw
DISK_SIZE ?= 2G
STORAGE ?= ahci
VM_NAME ?= UnitasOS
BOOT ?= live
FIRMWARE ?= bios
IMAGE ?= build/unitasos-installed.raw
VDI ?= build/unitasos-installed.vdi
ROOT_FS ?= ext4
GNU_BIN_DIR ?= $(abspath build/gnu-bin)
ifeq ($(serial),1)
BUILD := build-serial
else
BUILD := build
endif
KERNEL := $(BUILD)/kernel.elf
ISO := $(BUILD)/unitasos.iso

CPPFLAGS := -Ikernel/include -Iabi -DUNITAS_SERIAL=$(serial)
CFLAGS := -std=gnu11 -ffreestanding -fno-stack-protector -fno-pic \
          -fno-pie -mno-red-zone -mgeneral-regs-only -mcmodel=small \
          -Wall -Wextra -Werror \
          -O2 -g
ASFLAGS := -ffreestanding -fno-pic -fno-pie -mno-red-zone -mcmodel=small
LDFLAGS := -nostdlib -z noexecstack -z max-page-size=0x1000 -T linker.ld

C_SOURCES := $(shell find kernel -name '*.c' -print)
S_SOURCES := $(shell find kernel boot -name '*.S' -print)
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(C_SOURCES)) \
           $(patsubst %.S,$(BUILD)/%.o,$(S_SOURCES)) \
           $(BUILD)/kernel/userland_blob.o
DEPFILES := $(patsubst %.o,%.d,$(OBJECTS))

USER_CFLAGS := -std=gnu11 -ffreestanding -fno-stack-protector -fPIC \
               -mno-red-zone -mgeneral-regs-only -mcmodel=small \
               -fno-asynchronous-unwind-tables -fno-unwind-tables \
               -Wall -Wextra -Werror -O2
USER_OBJECTS := $(BUILD)/user/crt0.o $(BUILD)/user/syscall.o \
                $(BUILD)/user/unistd.o $(BUILD)/user/dirent.o $(BUILD)/user/stdio.o \
                $(BUILD)/user/string.o $(BUILD)/user/stdlib.o \
                $(BUILD)/user/hello.o
USERLAND_IMAGES := $(BUILD)/user/reboot $(BUILD)/user/poweroff \
                   $(BUILD)/user/shutdown
GNU_PROGRAMS := $(if $(strip $(GNU_BIN_DIR)),$(wildcard $(GNU_BIN_DIR)/*))
USER_POWER_IMAGES := $(BUILD)/user/reboot $(BUILD)/user/poweroff \
                     $(BUILD)/user/shutdown
# These command names share one image. The program selects its action from argv[0].
USER_COMMON_OBJECTS := $(BUILD)/user/crt0.o $(BUILD)/user/syscall.o \
                       $(BUILD)/user/unistd.o $(BUILD)/user/dirent.o \
                       $(BUILD)/user/stdio.o $(BUILD)/user/string.o \
                       $(BUILD)/user/stdlib.o
USER_ASM_OBJECTS := $(BUILD)/user/crt0.o $(BUILD)/user/syscall.o
USER_C_OBJECTS := $(BUILD)/user/unistd.o $(BUILD)/user/dirent.o $(BUILD)/user/stdio.o \
                  $(BUILD)/user/string.o $(BUILD)/user/stdlib.o \
                  $(BUILD)/user/hello.o $(BUILD)/user/power.o
DEPFILES += $(USER_C_OBJECTS:.o=.d)

.PHONY: all kernel-build userland gnu-source gnu-glibc gnu-coreutils gnu-bash gnu-userland gnu-system iso run run-qemu run-vbox disk-image test install install-image install-vdi partition-edit clean
all: kernel-build
userland: $(BUILD)/user/hello.elf $(USERLAND_IMAGES)

gnu-source:
	$(MAKE) -C ports/gnu source

gnu-glibc:
	$(MAKE) -C ports/gnu glibc

gnu-coreutils:
	$(MAKE) -C ports/gnu coreutils

gnu-bash:
	$(MAKE) -C ports/gnu bash

gnu-userland:
	$(MAKE) -C ports/gnu userland GNU_BIN_DIR="$(abspath $(GNU_BIN_DIR))"

gnu-system: all

kernel-build:
	$(MAKE) GNU_BIN_DIR="$(abspath $(GNU_BIN_DIR))" gnu-userland
	$(MAKE) GNU_BIN_DIR="$(abspath $(GNU_BIN_DIR))" $(KERNEL)

$(KERNEL): $(OBJECTS) linker.ld
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASFLAGS) -c $< -o $@

$(USER_ASM_OBJECTS): $(BUILD)/user/%.o: user/%.S
	@mkdir -p $(@D)
	$(CC) -ffreestanding -fno-pic -fno-pie -mno-red-zone -mcmodel=large \
	  -Iuser/include -Iabi -c $< -o $@

$(USER_C_OBJECTS): $(BUILD)/user/%.o: user/%.c
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -Iuser/include -Iuser/libc -Iabi -MMD -MP -c $< -o $@

$(BUILD)/user/hello.elf: $(USER_OBJECTS) user/hello.ld
	@mkdir -p $(@D)
	$(LD) -nostdlib -z noexecstack -T user/hello.ld -o $@ $(USER_OBJECTS)

$(USER_POWER_IMAGES): $(BUILD)/user/%: $(USER_COMMON_OBJECTS) \
                      $(BUILD)/user/power.o user/hello.ld
	$(LD) -nostdlib -z noexecstack -T user/hello.ld -o $@ \
	  $(USER_COMMON_OBJECTS) $(BUILD)/user/power.o

$(BUILD)/userland_blob.S: $(BUILD)/user/hello.elf $(USERLAND_IMAGES) \
                          $(GNU_PROGRAMS) tools/embed-userland.py
	@mkdir -p $(@D)
	$(PYTHON) tools/embed-userland.py --output "$@" \
	  --hello "$(abspath $(BUILD)/user/hello.elf)" \
	  $(foreach image,$(USERLAND_IMAGES) $(GNU_PROGRAMS),--image "$(abspath $(image))")

$(BUILD)/kernel/userland_blob.o: $(BUILD)/userland_blob.S
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ASFLAGS) -c $< -o $@

iso: kernel-build
	@mkdir -p $(BUILD)/iso/boot/grub $(BUILD)/iso/tools
	cp $(KERNEL) $(BUILD)/iso/boot/kernel.elf
	cp boot/grub.cfg $(BUILD)/iso/boot/grub/grub.cfg
	cp tools/install.sh tools/install-image.sh tools/install-vdi.sh \
	   tools/partition-editor.sh tools/run-qemu.sh tools/run-virtualbox.sh \
	   README.md CONTRIBUTING.md ROADMAP.md $(BUILD)/iso/tools/
	$(GRUB_MKRESCUE) -o $(ISO) $(BUILD)/iso

run: $(ISO)
	QEMU="$(QEMU)" ./tools/run-qemu.sh "$(ISO)" "$(STORAGE)" \
	   "$(DISK)" "$(DISK_FORMAT)" "$(BOOT)" "$(FIRMWARE)" "$(USB)"

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

install: kernel-build
	@test -n "$(DEVICE)" || { echo 'Set DEVICE to a whole disk such as /dev/sda or /dev/nvme0n1'; exit 2; }
	UNITAS_KERNEL_IMAGE="$(KERNEL)" UNITAS_USERLAND_DIR="$(BUILD)/user" \
	  UNITAS_GNU_BIN_DIR="$(GNU_BIN_DIR)" \
	  ./tools/install.sh "$(DEVICE)" "$(MODE)" "$(TABLE)" "$(ROOT_FS)"

install-image: kernel-build
	UNITAS_KERNEL_IMAGE="$(KERNEL)" UNITAS_USERLAND_DIR="$(BUILD)/user" \
	  UNITAS_GNU_BIN_DIR="$(GNU_BIN_DIR)" \
	  ./tools/install-image.sh "$(IMAGE)" "$(MODE)" "$(TABLE)" "$(ROOT_FS)"

install-vdi: kernel-build
	UNITAS_KERNEL_IMAGE="$(KERNEL)" UNITAS_USERLAND_DIR="$(BUILD)/user" \
	  UNITAS_GNU_BIN_DIR="$(GNU_BIN_DIR)" \
	  ./tools/install-vdi.sh "$(VDI)" "$(MODE)" "$(TABLE)" "$(ROOT_FS)"

partition-edit:
	@test -n "$(DEVICE)" || { echo 'Set DEVICE to a whole disk such as /dev/sda or /dev/nvme0n1'; exit 2; }
	./tools/partition-editor.sh "$(DEVICE)"

clean:
	rm -rf build build-serial tests/build

-include $(DEPFILES)
