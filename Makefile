# Lamp -- a freestanding Nock machine
#
#   make            build build/boot.elf
#   make run        boot it and watch the serial line
#   make test       boot it, print the self-test, fail the build if it fails
#   make debug      boot it with QEMU stopped at the reset vector
#   make clean

CC       := gcc
LD       := ld

CFLAGS := -std=c11 -O2 -g \
          -ffreestanding -nostdlib -fno-builtin \
          -fno-pic -fno-pie -fno-stack-protector \
          -fno-asynchronous-unwind-tables -fno-unwind-tables \
          -fomit-frame-pointer \
          -Wall -Wextra -Werror -Wno-unused-parameter \
          -Ikernel -Itests

ASFLAGS := -ffreestanding -fno-pic

LDFLAGS := -T boot/link.ld --build-id=none -z noexecstack

BUILD   := build
KERNEL  := $(BUILD)/boot.elf
IMAGE   := $(BUILD)/boot.bin

C_SRCS  := kernel/serial.c kernel/memory.c kernel/noun.c kernel/nock.c \
           kernel/primitives.c kernel/main.c tests/nock-tests.c
ASM_SRCS := boot/boot.S
OBJS    := $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS)) \
           $(patsubst %.S,$(BUILD)/%.o,$(ASM_SRCS))

# QEMU's isa-debug-exit device makes the guest exit with (code << 1) | 1, so a
# clean run leaves status 1 and a failed run leaves status 3.  A single -kernel
# image, no disk, no firmware, no network: the only I/O is the serial line.
QEMU := qemu-system-x86_64 -machine pc -m 256 -no-reboot \
        -display none -serial stdio -monitor none \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04

.PHONY: all run test debug clean

all: $(KERNEL)

run: $(KERNEL)
	$(QEMU) -kernel $(KERNEL)

test: $(KERNEL)
	@$(QEMU) -kernel $(KERNEL); status=$$?; \
	if [ $$status -eq 1 ]; then \
	    echo "make test: the machine halted cleanly and every check passed"; \
	elif [ $$status -eq 3 ]; then \
	    echo "make test: FAILED -- the machine reported failing checks"; exit 1; \
	else \
	    echo "make test: FAILED -- qemu exited with $$status (expected 1 or 3)"; exit 1; \
	fi

debug: $(KERNEL)
	qemu-system-x86_64 -machine pc -m 256 -no-reboot \
	    -display none -serial stdio -monitor none -S -s

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL): $(OBJS) boot/link.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	objcopy -O binary $@ $(IMAGE)

clean:
	rm -rf $(BUILD)
