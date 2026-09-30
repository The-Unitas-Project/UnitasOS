ROOT := $(abspath ../..)
BUILD_ROOT := $(ROOT)/build
SOURCE_ARCHIVES := $(BUILD_ROOT)/gnu-sources
SOURCE_ROOT := $(BUILD_ROOT)/gnu-source
TOOL_ROOT := $(BUILD_ROOT)/gnu-tools
TOOL_BUILD := $(BUILD_ROOT)/gnu-tool-build
HEADER_ROOT := $(BUILD_ROOT)/gnu-kernel-headers
GLIBC_BUILD := $(BUILD_ROOT)/gnu-build-shared
SYSROOT := $(BUILD_ROOT)/gnu-sysroot
GLIBC_LIBDIR := $(SYSROOT)/usr/lib64
COREUTILS_BUILD := $(BUILD_ROOT)/gnu-coreutils-build
BASH_BUILD := $(BUILD_ROOT)/gnu-bash-build
GNU_BIN_DIR := $(BUILD_ROOT)/gnu-bin
GNU_DISPATCHER := $(BUILD_ROOT)/gnu-coreutils-dispatch

GLIBC_VERSION := 2.43
COREUTILS_VERSION := 9.12
BASH_VERSION := 5.3
BASH_PATCHLEVEL := 020
BASH_PATCH_NUMBERS := 001 002 003 004 005 006 007 008 009 010 \
	011 012 013 014 015 016 017 018 019 020
GAWK_VERSION := 5.4.1
BISON_VERSION := 3.8.2
LINUX_HEADERS_VERSION := 6.1.187-1

GLIBC_ARCHIVE := $(SOURCE_ARCHIVES)/glibc-$(GLIBC_VERSION).tar.xz
COREUTILS_ARCHIVE := $(SOURCE_ARCHIVES)/coreutils-$(COREUTILS_VERSION).tar.xz
BASH_ARCHIVE := $(SOURCE_ARCHIVES)/bash-$(BASH_VERSION).tar.gz
BASH_PATCH_DIR := $(SOURCE_ARCHIVES)/bash-$(BASH_VERSION)-patches
BASH_PATCHES := $(foreach number,$(BASH_PATCH_NUMBERS),$(BASH_PATCH_DIR)/bash53-$(number))
GAWK_ARCHIVE := $(SOURCE_ARCHIVES)/gawk-$(GAWK_VERSION).tar.xz
BISON_ARCHIVE := $(SOURCE_ARCHIVES)/bison-$(BISON_VERSION).tar.xz
LINUX_HEADERS_ARCHIVE := $(SOURCE_ARCHIVES)/linux-libc-dev_$(LINUX_HEADERS_VERSION)_amd64.deb

HOST_OS := $(shell uname -s)
HOST_ARCH := $(shell uname -m)
JOBS ?= 2
GMAKE ?= $(if $(filter Darwin,$(HOST_OS)),gmake,make)
M4 ?= $(if $(filter Darwin,$(HOST_OS)),$(if $(wildcard /opt/local/bin/gm4),/opt/local/bin/gm4,m4),m4)
CC ?= cc
SHA256_CHECK ?= $(if $(filter Darwin,$(HOST_OS)),shasum -a 256,sha256sum)
PYTHON ?= python3
PATCH ?= patch
GNU_TARGET := x86_64-linux-gnu
BUILD_TRIPLE := $(if $(filter Darwin,$(HOST_OS)),x86_64-apple-darwin,x86_64-linux-gnu)
HOST_CC := $(shell command -v cc 2>/dev/null)
HOST_CLANG := $(shell command -v clang 2>/dev/null)
HOST_CXX := $(shell command -v c++ 2>/dev/null)
HOST_CLANGXX := $(shell command -v clang++ 2>/dev/null)

ifeq ($(HOST_OS),Darwin)
GNU_NATIVE_CC ?= x86_64-elf-gcc
GNU_BUILD_CXX ?= clang++
GNU_BUILD_CC ?= clang
GNU_AS ?= x86_64-elf-as
GNU_LD ?= x86_64-elf-ld
GNU_AR ?= x86_64-elf-ar
GNU_RANLIB ?= x86_64-elf-ranlib
GNU_NM ?= x86_64-elf-nm
GNU_STRIP ?= x86_64-elf-strip
GNU_OBJCOPY ?= x86_64-elf-objcopy
GNU_CP ?= $(if $(wildcard /opt/local/bin/gcp),/opt/local/bin/gcp,)
GNU_MAKE_BINS := $(TOOL_ROOT)/bin/make $(TOOL_ROOT)/bin/gnumake
GNU_UAPI_INCLUDE := $(HEADER_ROOT)/usr/include
GLIBC_CC := $(TOOL_ROOT)/bin/$(GNU_TARGET)-gcc
GLIBC_HEADERS := $(GNU_UAPI_INCLUDE)
GLIBC_COMPILER_DEP := $(GLIBC_CC)
GLIBC_HEADER_DEP := $(HEADER_ROOT)/.unitas-headers
else
GNU_NATIVE_CC ?= $(if $(HOST_CC),$(HOST_CC),$(HOST_CLANG))
GNU_BUILD_CC ?= $(GNU_NATIVE_CC)
GNU_BUILD_CXX ?= $(if $(HOST_CXX),$(HOST_CXX),$(if $(HOST_CLANGXX),$(HOST_CLANGXX),c++))
GNU_AS ?= as
GNU_LD ?= ld
GNU_AR ?= ar
GNU_RANLIB ?= ranlib
GNU_NM ?= nm
GNU_STRIP ?= strip
GNU_OBJCOPY ?= objcopy
GNU_CP ?= $(shell command -v cp)
GNU_MAKE_BINS :=
GNU_UAPI_INCLUDE ?= /usr/include
GLIBC_CC := $(GNU_NATIVE_CC)
GLIBC_HEADERS := $(GNU_UAPI_INCLUDE)
GLIBC_COMPILER_DEP :=
GLIBC_HEADER_DEP :=
endif

ifeq ($(strip $(GNU_NATIVE_CC)),)
$(error A host C compiler is required. Install GCC or Clang, or set GNU_NATIVE_CC)
endif

GLIBC_DIR := $(SOURCE_ROOT)/glibc-$(GLIBC_VERSION)
COREUTILS_DIR := $(SOURCE_ROOT)/coreutils-$(COREUTILS_VERSION)
BASH_DIR := $(SOURCE_ROOT)/bash-$(BASH_VERSION)
GAWK_DIR := $(SOURCE_ROOT)/gawk-$(GAWK_VERSION)
BISON_DIR := $(SOURCE_ROOT)/bison-$(BISON_VERSION)
GLIBC_CONFIG_STAMP := $(GLIBC_BUILD)/.unitas-configured-shared
GLIBC_INSTALL_STAMP := $(SYSROOT)/.unitas-glibc-installed
GLIBC_PATCH_STAMP := $(GLIBC_DIR)/.unitas-patched
GLIBC_ASM_PATCH_STAMP := $(GLIBC_DIR)/.unitas-memcmp-patched
GLIBC_MEMMOVE_PATCH_STAMP := $(GLIBC_DIR)/.unitas-memmove-patched
COREUTILS_CONFIG_STAMP := $(COREUTILS_BUILD)/.unitas-configured
COREUTILS_PATCH_STAMP := $(COREUTILS_DIR)/.unitas-patched
COREUTILS_INSTALL_STAMP := $(SYSROOT)/.unitas-coreutils-installed
BASH_CONFIG_STAMP := $(BASH_BUILD)/.unitas-configured
BASH_INSTALL_STAMP := $(SYSROOT)/.unitas-bash-installed
BASH_PATCH_STAMP := $(SOURCE_ROOT)/bash-$(BASH_VERSION)/.unitas-patched-$(BASH_PATCHLEVEL)
GNU_TOOL_BINS := $(TOOL_ROOT)/bin/gawk $(TOOL_ROOT)/bin/bison \
	$(TOOL_ROOT)/bin/cp $(GNU_MAKE_BINS)
ifeq ($(HOST_OS),Darwin)
GLIBC_BUILD_TOOLS := $(TOOL_ROOT)/bin/unitas-bison
else
GLIBC_BUILD_TOOLS :=
endif

.PHONY: source tools headers configure-glibc glibc coreutils bash userland clean

source: $(GLIBC_DIR)/.unitas-source $(COREUTILS_DIR)/.unitas-source \
        $(BASH_PATCH_STAMP) $(GAWK_DIR)/.unitas-source \
        $(BISON_DIR)/.unitas-source

ifeq ($(HOST_OS),Darwin)
source: $(HEADER_ROOT)/.unitas-headers
headers: $(HEADER_ROOT)/.unitas-headers
else
headers:
	@test -f "$(GNU_UAPI_INCLUDE)/linux/version.h" || { \
	  echo 'Linux UAPI headers are missing. Install the host linux-libc-dev package.' >&2; exit 1; }
endif

$(SOURCE_ARCHIVES):
	mkdir -p "$@"

$(GLIBC_ARCHIVE): | $(SOURCE_ARCHIVES)
	curl -fL --retry 3 https://ftp.gnu.org/gnu/glibc/glibc-$(GLIBC_VERSION).tar.xz -o "$@.tmp"
	echo 'd9c86c6b5dbddb43a3e08270c5844fc5177d19442cf5b8df4be7c07cd5fa3831  $@.tmp' | $(SHA256_CHECK) -c -
	mv "$@.tmp" "$@"

$(COREUTILS_ARCHIVE): | $(SOURCE_ARCHIVES)
	curl -fL --retry 3 https://ftp.gnu.org/gnu/coreutils/coreutils-$(COREUTILS_VERSION).tar.xz -o "$@.tmp"
	echo 'a480198559733e9b3da999e90543ac6f888a2caa544d8d664c5a1f17e528e210  $@.tmp' | $(SHA256_CHECK) -c -
	mv "$@.tmp" "$@"

$(BASH_ARCHIVE): | $(SOURCE_ARCHIVES)
	curl -fL --retry 3 https://ftp.gnu.org/gnu/bash/bash-$(BASH_VERSION).tar.gz -o "$@.tmp"
	echo '0d5cd86965f869a26cf64f4b71be7b96f90a3ba8b3d74e27e8e9d9d5550f31ba  $@.tmp' | $(SHA256_CHECK) -c -
	mv "$@.tmp" "$@"

$(GAWK_ARCHIVE): | $(SOURCE_ARCHIVES)
	curl -fL --retry 3 https://ftp.gnu.org/gnu/gawk/gawk-$(GAWK_VERSION).tar.xz -o "$@.tmp"
	echo '07f6f7342b7febe4313fc2c2542ad93d64fe20ad8717200109f105a826f5fd37  $@.tmp' | $(SHA256_CHECK) -c -
	mv "$@.tmp" "$@"

$(BISON_ARCHIVE): | $(SOURCE_ARCHIVES)
	curl -fL --retry 3 https://ftp.gnu.org/gnu/bison/bison-$(BISON_VERSION).tar.xz -o "$@.tmp"
	echo '9bba0214ccf7f1079c5d59210045227bcf619519840ebfa80cd3849cff5a5bf2  $@.tmp' | $(SHA256_CHECK) -c -
	mv "$@.tmp" "$@"

$(GLIBC_DIR)/.unitas-source: $(GLIBC_ARCHIVE)
	mkdir -p "$(SOURCE_ROOT)"
	tar -xf "$<" -C "$(SOURCE_ROOT)"
	touch "$@"

$(COREUTILS_DIR)/.unitas-source: $(COREUTILS_ARCHIVE)
	mkdir -p "$(SOURCE_ROOT)"
	tar -xf "$<" -C "$(SOURCE_ROOT)"
	touch "$@"

$(COREUTILS_PATCH_STAMP): $(COREUTILS_DIR)/.unitas-source \
	                         $(ROOT)/ports/gnu/patches/coreutils-compat.py
	$(PYTHON) "$(ROOT)/ports/gnu/patches/coreutils-compat.py" "$(COREUTILS_DIR)"
	touch "$@"

$(BASH_DIR)/.unitas-source: $(BASH_ARCHIVE)
	mkdir -p "$(SOURCE_ROOT)"
	tar -xf "$<" -C "$(SOURCE_ROOT)"
	touch "$@"

$(BASH_PATCH_DIR): | $(SOURCE_ARCHIVES)
	mkdir -p "$@"

$(BASH_PATCH_DIR)/bash53-%: | $(BASH_PATCH_DIR)
	curl -fL --retry 3 "https://ftp.gnu.org/gnu/bash/bash-$(BASH_VERSION)-patches/bash53-$*" -o "$@.tmp"
	mv "$@.tmp" "$@"

$(BASH_PATCH_STAMP): $(BASH_ARCHIVE) $(BASH_PATCHES) \
			      $(ROOT)/ports/gnu/bash-5.3-patches.sha256
	rm -rf "$(BASH_DIR)"
	mkdir -p "$(SOURCE_ROOT)"
	tar -xf "$(BASH_ARCHIVE)" -C "$(SOURCE_ROOT)"
	cd "$(ROOT)" && $(SHA256_CHECK) -c ports/gnu/bash-5.3-patches.sha256
	set -e; for patch_file in $(BASH_PATCHES); do \
	  $(PATCH) -d "$(BASH_DIR)" -p0 < "$$patch_file"; \
	done
	touch "$@"

$(GAWK_DIR)/.unitas-source: $(GAWK_ARCHIVE)
	mkdir -p "$(SOURCE_ROOT)"
	tar -xf "$<" -C "$(SOURCE_ROOT)"
	touch "$@"

$(BISON_DIR)/.unitas-source: $(BISON_ARCHIVE)
	mkdir -p "$(SOURCE_ROOT)"
	tar -xf "$<" -C "$(SOURCE_ROOT)"
	touch "$@"

$(GLIBC_PATCH_STAMP): $(GLIBC_DIR)/.unitas-source $(ROOT)/ports/gnu/patches/glibc-x86_64-gas-sizes.py
	$(PYTHON) "$(ROOT)/ports/gnu/patches/glibc-x86_64-gas-sizes.py" "$(GLIBC_DIR)"
	touch "$@"

$(LINUX_HEADERS_ARCHIVE): | $(SOURCE_ARCHIVES)
	curl -fL --retry 3 https://deb.debian.org/debian/pool/main/l/linux/linux-libc-dev_$(LINUX_HEADERS_VERSION)_amd64.deb -o "$@.tmp"
	echo 'ebcd139c31438e19f161bcf277a38708402ddd0e3acbe208cbbb27bff1c52bb9  $@.tmp' | $(SHA256_CHECK) -c -
	mv "$@.tmp" "$@"

$(HEADER_ROOT)/.unitas-headers: $(LINUX_HEADERS_ARCHIVE)
	rm -rf "$(HEADER_ROOT)"
	mkdir -p "$(HEADER_ROOT)/tmp"
	cd "$(HEADER_ROOT)/tmp" && ar x "$(LINUX_HEADERS_ARCHIVE)" data.tar.xz
	tar -xf "$(HEADER_ROOT)/tmp/data.tar.xz" -C "$(HEADER_ROOT)"
	rm -rf "$(HEADER_ROOT)/tmp"
	ln -sfn x86_64-linux-gnu/asm "$(GNU_UAPI_INCLUDE)/asm"
	touch "$@"

tools: $(GNU_TOOL_BINS)

$(TOOL_ROOT)/bin/cp:
	@test -x "$(GNU_CP)" || { echo 'GNU cp is missing. Install GNU coreutils or set GNU_CP.' >&2; exit 1; }
	mkdir -p "$(TOOL_ROOT)/bin"
	printf '%s\n' '#!/bin/sh' 'exec "$(GNU_CP)" "$$@"' > "$@"
	chmod +x "$@"

$(TOOL_ROOT)/bin/gawk: $(GAWK_DIR)/.unitas-source
	mkdir -p "$(TOOL_BUILD)/gawk" "$(TOOL_ROOT)"
	cd "$(TOOL_BUILD)/gawk" && CC="$(GNU_BUILD_CC)" CXX="$(GNU_BUILD_CXX)" \
	  "$(GAWK_DIR)/configure" --prefix="$(TOOL_ROOT)" --disable-nls
	$(GMAKE) -C "$(TOOL_BUILD)/gawk" -j$(JOBS)
	$(GMAKE) -C "$(TOOL_BUILD)/gawk" install

$(TOOL_ROOT)/bin/bison: $(BISON_DIR)/.unitas-source
	mkdir -p "$(TOOL_BUILD)/bison" "$(TOOL_ROOT)"
	cd "$(TOOL_BUILD)/bison" && M4="$(M4)" CC="$(GNU_BUILD_CC)" \
	  CXX="$(GNU_BUILD_CXX)" "$(BISON_DIR)/configure" \
	  --prefix="$(TOOL_ROOT)" --disable-nls
	$(GMAKE) -C "$(TOOL_BUILD)/bison" -j$(JOBS)
	$(GMAKE) -C "$(TOOL_BUILD)/bison" install

ifeq ($(HOST_OS),Darwin)
$(TOOL_ROOT)/bin/make $(TOOL_ROOT)/bin/gnumake:
	mkdir -p "$(TOOL_ROOT)/bin"
	printf '%s\n' '#!/bin/sh' 'exec "$(GMAKE)" "$$@"' > "$@"
	chmod +x "$@"

$(TOOL_ROOT)/bin/unitas-bison: $(TOOL_ROOT)/bin/bison
	mkdir -p "$(TOOL_ROOT)/bin"
	printf '%s\n' '#!/bin/sh' 'M4="$(M4)"' 'export M4' 'exec "$(TOOL_ROOT)/bin/bison" "$$@"' > "$@"
	chmod +x "$@"

# Use the ELF compiler for C and assembly. Clang cannot assemble all generated glibc files.
$(TOOL_ROOT)/bin/$(GNU_TARGET)-gcc: $(GNU_TOOL_BINS) \
				   $(HEADER_ROOT)/.unitas-headers \
				   $(ROOT)/ports/gnu/Makefile
	mkdir -p "$(TOOL_ROOT)/bin"
	printf '%s\n' '#!/bin/sh' \
	  'exec "$(GNU_NATIVE_CC)" -D__linux__ -D__gnu_linux__ -nostdinc -isystem "$$($(GNU_NATIVE_CC) -print-file-name=include)" -isystem "$(GNU_UAPI_INCLUDE)" "$$@"' > "$@"
	chmod +x "$@"
endif

configure-glibc: $(GLIBC_CONFIG_STAMP)

$(GLIBC_CONFIG_STAMP): $(GLIBC_PATCH_STAMP) $(GNU_TOOL_BINS) \
                        $(GLIBC_COMPILER_DEP) $(GLIBC_HEADER_DEP)
	@test -f "$(GLIBC_HEADERS)/linux/version.h" || { \
	  echo 'Linux UAPI headers are missing.' >&2; exit 1; }
	rm -rf "$(GLIBC_BUILD)"
	mkdir -p "$(GLIBC_BUILD)"
	cd "$(GLIBC_BUILD)" && PATH="$(TOOL_ROOT)/bin:$(PATH)" \
	  BUILD_CC="$(GNU_BUILD_CC)" CC="$(GLIBC_CC)" CXX="$(GNU_BUILD_CXX)" \
	  CFLAGS='-O2 -g -mcmodel=large' \
	  AR="$(GNU_AR)" AS="$(GNU_AS)" \
	  LD="$(GNU_LD)" NM="$(GNU_NM)" RANLIB="$(GNU_RANLIB)" \
	  "$(GLIBC_DIR)/configure" --build="$(BUILD_TRIPLE)" --host="$(GNU_TARGET)" \
	  --prefix=/usr --disable-default-pie \
	  --disable-multi-arch --disable-werror \
	  --with-headers="$(GLIBC_HEADERS)" --enable-kernel=6.1.0
	touch "$@"

glibc: $(GLIBC_INSTALL_STAMP)

$(GLIBC_ASM_PATCH_STAMP): $(GLIBC_PATCH_STAMP) \
			       $(ROOT)/ports/gnu/patches/glibc-x86_64-gas-memcmp.py
	$(PYTHON) "$(ROOT)/ports/gnu/patches/glibc-x86_64-gas-memcmp.py" "$(GLIBC_DIR)"
	touch "$@"

$(GLIBC_MEMMOVE_PATCH_STAMP): $(GLIBC_PATCH_STAMP) \
			       $(ROOT)/ports/gnu/patches/glibc-x86_64-gas-memmove.py
	$(PYTHON) "$(ROOT)/ports/gnu/patches/glibc-x86_64-gas-memmove.py" "$(GLIBC_DIR)"
	touch "$@"

$(GLIBC_INSTALL_STAMP): $(GLIBC_CONFIG_STAMP) $(GLIBC_BUILD_TOOLS) \
			 $(GLIBC_ASM_PATCH_STAMP) $(GLIBC_MEMMOVE_PATCH_STAMP) \
			 $(ROOT)/ports/gnu/archive-glibc-static.py
	@mkdir -p "$(BUILD_ROOT)"
	@if ! PATH="$(TOOL_ROOT)/bin:$(PATH)" M4="$(M4)" \
	  $(GMAKE) -C "$(GLIBC_BUILD)" BISON="$(if $(GLIBC_BUILD_TOOLS),$(TOOL_ROOT)/bin/unitas-bison,bison)" \
	  -k -j$(JOBS) lib > "$(BUILD_ROOT)/gnu-glibc-library.log" 2>&1; then \
	  if ! grep -q 'elf/ld.so' "$(BUILD_ROOT)/gnu-glibc-library.log" || \
	     ! grep -q 'undefined reference' "$(BUILD_ROOT)/gnu-glibc-library.log"; then \
	    cat "$(BUILD_ROOT)/gnu-glibc-library.log"; exit 1; \
	  fi; \
	fi
	@if ! test -s "$(GLIBC_BUILD)/libc.a"; then \
	  $(PYTHON) "$(ROOT)/ports/gnu/archive-glibc-static.py" \
	    --build "$(GLIBC_BUILD)" --ar "$(GNU_AR)" \
	    --ranlib "$(GNU_RANLIB)"; \
	fi
	@test -s "$(GLIBC_BUILD)/libc.a" || { cat "$(BUILD_ROOT)/gnu-glibc-library.log"; exit 1; }
	PATH="$(TOOL_ROOT)/bin:$(PATH)" $(GMAKE) -C "$(GLIBC_BUILD)" \
	  install_root="$(SYSROOT)" install-headers
	mkdir -p "$(SYSROOT)/usr/include/gnu"
	{ sed '/^@/d' "$(GLIBC_DIR)/include/stubs-prologue.h"; \
	  find "$(GLIBC_BUILD)" -mindepth 2 -maxdepth 2 -type f -name stubs \
	    -exec cat {} + | LC_ALL=C sort; \
	} > "$(SYSROOT)/usr/include/gnu/stubs.h"
	mkdir -p "$(GLIBC_LIBDIR)"
	for archive in "$(GLIBC_BUILD)"/lib*.a; do \
	  test -f "$$archive" || continue; \
	  cp "$$archive" "$(GLIBC_LIBDIR)/$${archive##*/}"; \
	  "$(GNU_RANLIB)" "$(GLIBC_LIBDIR)/$${archive##*/}"; \
	done
	for startup in crt1.o crti.o crtn.o; do \
	  cp "$(GLIBC_BUILD)/csu/$$startup" "$(GLIBC_LIBDIR)/$$startup"; \
	done
	touch "$@"

ifeq ($(HOST_OS),Darwin)
COREUTILS_CC := $(TOOL_ROOT)/bin/unitas-user-gcc
COREUTILS_COMPILER_DEP := $(COREUTILS_CC)
$(COREUTILS_CC): $(GLIBC_INSTALL_STAMP)
	mkdir -p "$(TOOL_ROOT)/bin"
	printf '%s\n' '#!/bin/sh' 'exec "$(GNU_NATIVE_CC)" -D__linux__ -D__gnu_linux__ -nostdinc -isystem "$$($(GNU_NATIVE_CC) -print-file-name=include)" -isystem "$(SYSROOT)/usr/include" -isystem "$$($(GNU_NATIVE_CC) -print-file-name=include-fixed)" -isystem "$(GNU_UAPI_INCLUDE)" -L"$(SYSROOT)/usr/lib64" -L"$(SYSROOT)/usr/lib" "$$@"' > "$@"
	chmod +x "$@"
else
COREUTILS_CC := $(TOOL_ROOT)/bin/unitas-user-gcc
COREUTILS_COMPILER_DEP := $(COREUTILS_CC)
# Use the Unitas sysroot for target headers and libraries.
$(COREUTILS_CC): $(GLIBC_INSTALL_STAMP) $(ROOT)/ports/gnu/Makefile
	mkdir -p "$(TOOL_ROOT)/bin"
	printf '%s\n' '#!/bin/sh' \
	  'set -- -D__linux__ -D__gnu_linux__ -nostdinc -isystem "$$($(GNU_NATIVE_CC) -print-file-name=include)" -isystem "$(SYSROOT)/usr/include" -isystem "$(GNU_UAPI_INCLUDE)" "$$@"' \
	  'fixed="$$($(GNU_NATIVE_CC) -print-file-name=include-fixed)"' \
	  'if [ -d "$$fixed" ]; then set -- "$$@" -isystem "$$fixed"; fi' \
	  'exec "$(GNU_NATIVE_CC)" "$$@" -L"$(GLIBC_LIBDIR)" -L"$(SYSROOT)/usr/lib"' > "$@"
	chmod +x "$@"
endif

BASH_CC := $(COREUTILS_CC)
BASH_COMPILER_DEP := $(COREUTILS_COMPILER_DEP)

$(COREUTILS_CONFIG_STAMP): $(COREUTILS_PATCH_STAMP) $(GLIBC_INSTALL_STAMP) $(COREUTILS_COMPILER_DEP)
	rm -rf "$(COREUTILS_BUILD)"
	mkdir -p "$(COREUTILS_BUILD)"
	# Build only static tools. stdbuf requires a shared library.
	cd "$(COREUTILS_BUILD)" && utils_cv_stdbuf_supported=no CC="$(COREUTILS_CC)" \
	  AR="$(GNU_AR)" RANLIB="$(GNU_RANLIB)" NM="$(GNU_NM)" STRIP="$(GNU_STRIP)" \
	  CFLAGS='-O2 -g -mcmodel=large -fno-pie -fno-pic' \
	  LDFLAGS='-static -no-pie -nostartfiles -Wl,-Ttext-segment=0x100000000 $(GLIBC_LIBDIR)/crt1.o $(GLIBC_LIBDIR)/crti.o' \
	  LIBS='-lc -lgcc $(GLIBC_LIBDIR)/crtn.o' \
	  "$(COREUTILS_DIR)/configure" --build="$(BUILD_TRIPLE)" --host="$(GNU_TARGET)" \
	  --prefix=/usr --disable-nls --without-selinux --enable-single-binary=symlinks
	touch "$@"

coreutils: $(COREUTILS_INSTALL_STAMP)

$(COREUTILS_INSTALL_STAMP): $(COREUTILS_CONFIG_STAMP)
	# Generate headers before parallel compilation starts.
	printf '%s\n' 'unitas-built-sources: $$(BUILT_SOURCES)' | \
	  $(GMAKE) -C "$(COREUTILS_BUILD)" -f Makefile -f - unitas-built-sources
	$(GMAKE) -C "$(COREUTILS_BUILD)" lib/libcoreutils.a
	# Keep Coreutils' private time helper local to avoid a glibc symbol conflict.
	$(GNU_OBJCOPY) --localize-symbol=__mktime_internal "$(COREUTILS_BUILD)/lib/libcoreutils_a-mktime.o"
	$(GNU_AR) r "$(COREUTILS_BUILD)/lib/libcoreutils.a" "$(COREUTILS_BUILD)/lib/libcoreutils_a-mktime.o"
	$(GNU_RANLIB) "$(COREUTILS_BUILD)/lib/libcoreutils.a"
	$(GMAKE) -C "$(COREUTILS_BUILD)" -j$(JOBS)
	$(GMAKE) -C "$(COREUTILS_BUILD)" DESTDIR="$(SYSROOT)" install
	touch "$@"

$(BASH_CONFIG_STAMP): $(BASH_PATCH_STAMP) $(GLIBC_INSTALL_STAMP) $(BASH_COMPILER_DEP)
	rm -rf "$(BASH_BUILD)"
	mkdir -p "$(BASH_BUILD)"
	cd "$(BASH_BUILD)" && CC="$(BASH_CC)" \
	  AR="$(GNU_AR)" RANLIB="$(GNU_RANLIB)" NM="$(GNU_NM)" STRIP="$(GNU_STRIP)" \
	  CFLAGS='-O2 -g -mcmodel=large -fno-pie -fno-pic' \
	  LDFLAGS='-static -no-pie -nostartfiles -Wl,-Ttext-segment=0x100000000 $(GLIBC_LIBDIR)/crt1.o $(GLIBC_LIBDIR)/crti.o' \
	  LIBS='-lc -lgcc $(GLIBC_LIBDIR)/crtn.o' \
	  "$(BASH_DIR)/configure" --build="$(BUILD_TRIPLE)" --host="$(GNU_TARGET)" \
	  --prefix=/usr --disable-nls --enable-readline --without-bash-malloc \
	  --enable-static-link
	touch "$@"

bash: $(BASH_INSTALL_STAMP)

$(BASH_INSTALL_STAMP): $(BASH_CONFIG_STAMP)
	# Build this host tool without the Linux target libraries.
	$(GMAKE) -C "$(BASH_BUILD)/support" man2html LIBS=
	# Do not pass Linux dynamic-export flags to the static Bash link.
	$(GMAKE) -C "$(BASH_BUILD)" LOCAL_LDFLAGS= -j$(JOBS)
	$(GMAKE) -C "$(BASH_BUILD)" DESTDIR="$(SYSROOT)" install
	touch "$@"

$(GNU_DISPATCHER): $(ROOT)/ports/gnu/coreutils-dispatch.S
	mkdir -p "$(BUILD_ROOT)"
	$(GNU_NATIVE_CC) -ffreestanding -fno-pic -fno-pie -nostdlib -no-pie \
	  -Wl,-z,noexecstack -Wl,--build-id=none \
	  -Wl,-Ttext-segment=0x100000000 -o "$@" "$<"

userland: $(COREUTILS_INSTALL_STAMP) $(BASH_INSTALL_STAMP) $(GNU_DISPATCHER)
	rm -rf "$(GNU_BIN_DIR)"
	mkdir -p "$(GNU_BIN_DIR)"
	for program in "$(SYSROOT)/usr/bin"/*; do \
	  if [ "$${program##*/}" != bashbug ] && [ -f "$$program" ] && [ -x "$$program" ]; then \
	    if [ -L "$$program" ]; then \
	      target=$$(readlink "$$program"); \
	      target_name=$${target##*/}; \
	      if [ "$$target_name" = coreutils ]; then \
	        cp "$(GNU_DISPATCHER)" "$(GNU_BIN_DIR)/$${program##*/}"; \
	      else \
	        ln -s "$$target" "$(GNU_BIN_DIR)/$${program##*/}"; \
	      fi; \
	    else \
	      cp "$$program" "$(GNU_BIN_DIR)/$${program##*/}"; \
	    fi; \
	  fi; \
	done
	# Bash provides the /bin/sh command name through the same image.
	ln -s bash "$(GNU_BIN_DIR)/sh"

clean:
	rm -rf "$(BUILD_ROOT)/gnu-build-shared" "$(BUILD_ROOT)/gnu-coreutils-build" \
	  "$(BUILD_ROOT)/gnu-bash-build" \
	  "$(BUILD_ROOT)/gnu-coreutils-dispatch" "$(BUILD_ROOT)/gnu-sysroot" \
	  "$(BUILD_ROOT)/gnu-bin"
