# Lamp -- a freestanding Nock machine
#
#   make            build build/boot.elf
#   make run        boot it and watch the serial line
#   make test       boot it, print the self-test, fail the build if it fails
#   make hoontest   compile, and check the output with the machine's own code
#   make proofs     the native primitives against their Nock definitions
#   make teach      compile an expression and run it on the real machine
#   make check      both suites
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

# --- the host compiler -------------------------------------------------------
#
# The same sources, compiled as a hosted binary: kernel/noun.c, nock.c,
# primitives.c, book.c and guestbook.c, with tools/hoon.c on top.  The compiler's
# self-test prints each formula it builds with the machine's own printer, reads
# the text back with the machine's own reader, and runs the noun with the
# machine's own interpreter on a session the machine's own book built -- so
# "the compiler and the machine agree" is a check rather than a claim about two
# copies of the machine.
#
# Freestanding is dropped and libc kept, because this binary is not the machine,
# and -fno-pic goes with it: nothing here is loaded at a fixed address, and a
# host linker expects position-independent code.  The warnings are not dropped:
# -Werror is the same, because a warning nobody reads is a warning that was not
# worth having.  Objects land in build/host/ so the hosted and freestanding
# builds of the same file never share one .o.
HOSTCFLAGS := -std=c11 -O2 -g \
              -fno-stack-protector \
              -fno-asynchronous-unwind-tables -fno-unwind-tables \
              -Wall -Wextra -Werror -Wno-unused-parameter \
              -Ikernel

HOON      := $(BUILD)/hoon
HOON_SRCS := tools/hoon.c tools/host-machine.c kernel/noun.c kernel/nock.c \
             kernel/primitives.c kernel/book.c kernel/guestbook.c
HOON_OBJS := $(patsubst %.c,$(BUILD)/host/%.o,$(HOON_SRCS))

# The jet proofs: the native primitives against the Nock definitions that say
# what they are allowed to be.  Hosted, like the compiler, because a definition
# is a piece of text and the battery over it is thousands of cases -- the
# machine's own suite stays the small set of things that must be checked inside
# the image.  Same objects as the compiler, so the two cannot drift apart on
# what the interpreter does.
PROOFS      := $(BUILD)/jet-proofs
PROOFS_SRCS := tools/jet-proofs.c tools/host-machine.c kernel/noun.c \
               kernel/nock.c kernel/primitives.c kernel/book.c \
               kernel/guestbook.c
PROOFS_OBJS := $(patsubst %.c,$(BUILD)/host/%.o,$(PROOFS_SRCS))

.PHONY: all run test debug clean lines hoontest proofs teach check

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

# The README carries a line count, and the README says the count is reproducible
# or it is not worth carrying -- so here is the method as something to run rather
# than a claim to trust.  tools/lines.awk is awk rather than a shell one-liner
# because a /* comment can open on one line and close on another, and any
# per-line filter gets that wrong.
#
# The counts are the machine and nothing else: assembly, the C, the tests, and
# the headers they share.  No Makefile, no docs, because a line of prose about
# the machine is not part of the machine.
lines:
	@printf 'assembly      %5d\n' "$$(awk -f tools/lines.awk boot/boot.S)"
	@printf 'kernel C      %5d\n' "$$(cat kernel/*.c | awk -f tools/lines.awk)"
	@printf 'tests         %5d\n' "$$(cat tests/*.c | awk -f tools/lines.awk)"
	@printf 'headers       %5d\n' "$$(cat kernel/*.h tests/*.h | awk -f tools/lines.awk)"
	@printf 'total         %5d\n' \
	    "$$(cat boot/boot.S kernel/*.c kernel/*.h tests/*.c tests/*.h \
	        | awk -f tools/lines.awk)"

# The compiler's own suite.  It runs on the host and boots nothing: the
# formulas it checks are run by the machine's own interpreter, linked in, and
# the answers come from the machine's own book.  What it cannot do is prove the
# guest will read the text, because the reader it uses is the reader's code
# rather than the reader inside the image -- that is what `make teach` is for.
hoontest: $(HOON)
	@./$(HOON) --selftest

# Every native, accounted for: proved against a Nock definition, or listed with
# the reason it has none yet.  A primitive that is neither is a primitive whose
# right to exist has never been argued, and this is where that shows up.
proofs: $(PROOFS)
	@./$(PROOFS)

# The bridge, end to end.  This is the whole of step 3: the host compiles an
# expression, the text goes down the serial line as characters, the guest's
# reader reads it, the guest's interpreter runs it, and the answer comes back.
# Nothing here is a socket and nothing is a protocol.
#
# Four lines, and the answers are a claim about four lines of a session rather
# than four independent answers.  Line 2 answering 2 means the count read at /14
# was 1, so line 1 was typed, read, run and remembered; line 4 answering 4 means
# the count was 3, so all three of the lines before it were.  Line 3 answers 1
# on its own, and is the only one that does not read the count -- the session
# carrying is what proves it arrived.
teach: $(KERNEL) $(HOON)
	@{ \
	    printf '=+(/14 ~(/2 1 2));=+(/14 ~(/2 1 2));*(|(1 3) |(0 2));+(/14)\n' \
		| tr ';' '\n' \
		| while read -r e; do ./$(HOON) "$$e" || exit 1; printf '\n'; done; \
	    printf '\004'; \
	} | $(QEMU) -kernel $(KERNEL) > $(BUILD)/teach.log 2>&1; \
	status=$$?; \
	if [ $$status -ne 1 ]; then \
	    echo "make teach: FAILED -- qemu exited with $$status (1 is a clean halt)"; \
	    cat $(BUILD)/teach.log; exit 1; \
	fi; \
	i=1; failed=0; \
	for want in 1 2 1 4; do \
	    if ! grep -q -- "  $$want  ($$i so far)" $(BUILD)/teach.log; then \
	        echo "make teach: FAILED -- line $$i did not answer $$want"; \
	        failed=1; \
	    fi; \
	    i=$$((i + 1)); \
	done; \
	if [ $$failed -ne 0 ]; then \
	    sed -n '/guest book/,$$p' $(BUILD)/teach.log; exit 1; \
	fi; \
	echo "make teach: four expressions compiled, typed, read, run, and answered"

# Both suites.  The machine's own first, because it is the thing everything else
# is a claim about.
check: test hoontest proofs
	@echo "make check: the machine's suite, the compiler's, and the jet proofs all passed"

debug: $(KERNEL)
	qemu-system-x86_64 -machine pc -m 256 -no-reboot \
	    -display none -serial stdio -monitor none -S -s

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/host/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(HOSTCFLAGS) -c $< -o $@

$(HOON): $(HOON_OBJS)
	$(CC) $(HOSTCFLAGS) -o $@ $(HOON_OBJS)

$(PROOFS): $(PROOFS_OBJS)
	$(CC) $(HOSTCFLAGS) -o $@ $(PROOFS_OBJS)

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL): $(OBJS) boot/link.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	objcopy -O binary $@ $(IMAGE)

clean:
	rm -rf $(BUILD)
