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
          -mgeneral-regs-only \
          -Wall -Wextra -Werror -Wno-unused-parameter \
          -Ikernel -Itests

# -mgeneral-regs-only is load-bearing, and not an optimisation choice.  The boot
# code puts the CPU in long mode and does nothing else: CR4.OSFXSR and
# CR4.OSXMMEXCPT are never set, so there is no SSE state and any SSE
# instruction traps as an invalid opcode.  gcc is entitled to reach for SSE2
# when vectorising a loop -- it did so on a 96-byte memset in the reader's
# tests, and the machine triple-faulted with EAX full of ']' -- so the machine
# has to be built to match what the boot code enables rather than the other way
# round.  The machine has no floating point and no SIMD, so nothing is lost.
# See docs/decisions.md item 16.

ASFLAGS := -ffreestanding -fno-pic -mgeneral-regs-only

LDFLAGS := -T boot/link.ld --build-id=none -z noexecstack

BUILD   := build
KERNEL  := $(BUILD)/boot.elf
IMAGE   := $(BUILD)/boot.bin

C_SRCS  := kernel/serial.c kernel/memory.c kernel/noun.c kernel/nock.c \
           kernel/primitives.c kernel/book.c kernel/guestbook.c kernel/main.c \
           tests/harness.c tests/nock-tests.c tests/guestbook-tests.c
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

# The isa-debug-exit device turns the machine's own verdict into QEMU's exit
# status as (code << 1) | 1, so a clean halt is 1 and a machine that found a
# failing check is 3.  QEMU's own failures are something else again.  Left
# unhandled, a clean Ctrl-D made make report a build error, so the one command
# that runs the machine for a person also always looked broken.
run: $(KERNEL)
	@$(QEMU) -kernel $(KERNEL); \
	status=$$?; \
	if [ $$status -ne 1 ]; then \
	    echo "make run: qemu exited with $$status (1 is a clean halt)"; exit 1; \
	fi

# The guest book reads bytes until it sees Ctrl-D, so a test run has to supply
# one.  Feeding a real formula and checking the answer is in the output makes
# `make test` cover the whole path -- build, boot, self-test, read, evaluate,
# print, exit -- rather than stopping at the self-test.  The exit status still
# comes from the isa-debug-exit device inside the guest, never from grep.
test: $(KERNEL)
	@printf '[1 42 0]\n\004' | $(QEMU) -kernel $(KERNEL) > $(BUILD)/test.log 2>&1; \
	status=$$?; \
	if [ $$status -eq 1 ] && grep -q 'LAMP: LIT' $(BUILD)/test.log; then \
	    echo "make test: the machine halted cleanly and every check passed"; \
	elif [ $$status -eq 3 ]; then \
	    echo "make test: FAILED -- the machine reported failing checks"; \
	    sed -n '/== self-test/,/== guest book/p' $(BUILD)/test.log; exit 1; \
	else \
	    echo "make test: FAILED -- qemu exited with $$status (expected 1 or 3)"; \
	    cat $(BUILD)/test.log; exit 1; \
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
