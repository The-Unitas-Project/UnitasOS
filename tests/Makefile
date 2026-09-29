CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
BUILD := build
TESTS := $(BUILD)/test_ring_buffer $(BUILD)/test_vfs $(BUILD)/test_ram_fat32 $(BUILD)/test_block

.PHONY: all run clean
all: $(TESTS)

$(BUILD)/test_ring_buffer: test_ring_buffer.c ../kernel/include/kern/ring_buffer.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I../kernel/include $< -o $@

$(BUILD)/test_vfs: test_vfs.c ../kernel/fs/vfs.c ../kernel/core/string.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I../kernel/include $^ -o $@

$(BUILD)/test_ram_fat32: test_ram_fat32.c ../kernel/fs/fat32_ram.c ../kernel/fs/vfs.c ../kernel/core/string.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I../kernel/include $^ -o $@

$(BUILD)/test_block: test_block.c ../kernel/core/block.c ../kernel/core/string.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I../kernel/include $^ -o $@

run: all
	@for test in $(TESTS); do ./$$test; done

clean:
	rm -rf $(BUILD)
