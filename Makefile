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
# Step 4's notebook target needs bash and nothing else does: it keeps the
# terminal's own output moving while a second process copies the records out of
# it, and a POSIX sh pipeline cannot do both at once without holding the guest's
# prompt back until the next newline arrives.  The prompt is a prompt only if it
# shows up when there is nothing to type yet.
SHELL := /bin/bash

# The notebook itself: the file the host keeps the guest book's log in.  It is
# plain text, one record a line, and it is meant to be read with cat.
JOURNAL ?= session.log

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

.PHONY: all run test debug clean lines hoontest proofs teach check urbit urbit-fetch urbit-rootfs urbit-update comet comet-rootfs urbit-checksums urbit-check-new urbit-repin urbit-clean

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
# Eleven lines, and the answers are a claim about eleven lines of a session
# rather than eleven independent answers.  Line 2 answering 2 means the count read at /14
# was 1, so line 1 was typed, read, run and remembered; line 4 answering 4 means
# the count was 3, so all three of the lines before it were.  Line 3 answers 1
# on its own, and is the only one that does not read the count -- the session
# carrying is what proves it arrived.
#
# Lines 5 to 11 are cores, and each one is a claim about a different address: the
# first and second things of a sample, a sample holding a read of the session, a
# core inside a core, a core pushed into, a conditional reading the sample, and
# two loops -- one of them counting up to three inside another core, answering
# with the limit it was given.  The
# loop is one line of source no matter how far it counts, and the machine walks
# it at run time; on the host the same formula costs 17 steps and 7 nouns per
# call, and a thousand calls fit inside the 7000-frame depth limit while seven
# thousand do not.  See decisions.md item 24.  A line whose answer came out one
# out is the failure this is here to catch: the compiler and the machine agreeing
# about a number is the only thing that makes the numbers mean anything.
teach: $(KERNEL) $(HOON)
	@{ \
	    printf '=>(/14 ?:(/2 1 2));=>(/14 ?:(/2 1 2));*(|(1 3) |(0 2));+(/14);=+(arm |(0 3) ?:(=(/4 /10) /10 ~(arm |(|(+(/4) /10) arm))));=+(a |(7 0) /4);=+(a |(0 /14) /10);=+(a |(0 0) =+(b |(9 0) /4));=+(a |(0 9) =>(/10 /26));=+(a |(0 1) ?:(=(/4 /4) 4 5));=+(a |(0 0) =+(b |(0 2) ?:(=(/4 /10) 9 ~(b |(|(+(/4) /10) b)))))\n' \
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
	for want in 1 2 1 4 3 7 6 9 9 4 9; do \
	    if ! grep -q -- "  $$want  ($$i so far)" $(BUILD)/teach.log; then \
	        echo "make teach: FAILED -- line $$i did not answer $$want"; \
	        failed=1; \
	    fi; \
	    i=$$((i + 1)); \
	done; \
	if [ $$failed -ne 0 ]; then \
	    sed -n '/guest book/,$$p' $(BUILD)/teach.log; exit 1; \
	fi; \
	echo "make teach: eleven expressions compiled, typed, read, run, and answered"

# --- the notebook (Step 4) ---------------------------------------------------
#
# The machine has no disk, no filesystem and no driver, so the only thing that can
# outlive it is the wire it already has.  This target is the whole of the other
# end: what the notebook holds is fed in before your own input, and every record
# the guest writes is copied out to the file as it arrives.  See decisions.md
# item 25.
#
# The records are copied out by a second process rather than by the shell at the
# end, because the end may never come: the point of writing a record as the line
# runs is that SIGKILL costs at most the line in flight, and a notebook that was
# written on the way out would be a save file and would cost the whole session.
#
# The sed in the copy-out is one line and it is not decoration.  The guest ends a
# line with CR LF because that is what a terminal wants, and a file wants LF, so
# the CR comes off on the way into the notebook.  A notebook with CRs in it is a
# notebook that every tool on the host will read as having a stray character at
# the end of every line, including the one doing the restoring.
#
# The records copied out are the session's % lines and the rules' ! lines.  A
# rule has to outlive a power cut exactly like a line does, or a boot would
# forget a rule the boot before it checked; decision 26.
notebook: $(KERNEL)
	@{ if [ -f $(JOURNAL) ]; then cat $(JOURNAL); fi; cat -; } \
	  | $(QEMU) -kernel $(KERNEL) \
	  | tee >(sed -n 's/\r$$//; /^[%!] /p' >> $(JOURNAL)); \
	wait

# Throwing the notebook away is a deliberate act and gets its own name, because a
# machine that forgets on request is a different claim from one that loses things
# by accident.
notebook-forget:
	@rm -f $(JOURNAL)
	@echo "make notebook-forget: $(JOURNAL) is gone, and so is the session in it"

# The test is a power cut and not a shutdown.  The first run is killed with
# SIGKILL while the guest is sitting at its prompt waiting for a third line, so
# the machine gets no chance to do anything on the way out -- if this passed by
# exiting politely it would be testing save-on-exit, which is not what item 25
# decided.  The second run is given nothing but the notebook and a question, and
# the answer has to be the count the first run reached.
#
# The third run is a record that lies: the formula is one that really answered
# 7, and the record claims it answered 8.  The session has to be left alone, and
# the count read afterwards has to be 0 rather than 1 -- a restore that cannot
# tell a true record from an edited one is a restore that takes the word of
# whatever is on the other end of the wire, and this is the check that it does
# not.
#
# A scratch journal in $(BUILD) rather than $(JOURNAL): the test is not allowed to
# touch the notebook someone is using, and a test that can destroy the thing it
# is testing is a test that will.
NOTEBOOK := $(BUILD)/notebook.log
# A second scratch notebook for the record-that-lies, kept apart so that a run of
# the test cannot depend on what the runs before it left behind.
NOTEBOOK2 := $(BUILD)/notebook2.log

notebook-test: $(KERNEL)
	@rm -f $(NOTEBOOK)
	@{ printf '[1 42 0]\n[1 7 0]\n'; sleep 8; } \
	  | timeout -s KILL 6 $(QEMU) -kernel $(KERNEL) \
	  | tee >(sed -n 's/\r$$//; /^% /p' >> $(NOTEBOOK)); \
	wait; \
	n=$$(grep -c '^% ' $(NOTEBOOK) 2>/dev/null || echo 0); \
	if [ "$$n" != "2" ]; then \
	    echo "make notebook-test: FAILED -- the notebook holds $$n records, and a power cut should leave 2"; \
	    cat $(NOTEBOOK) 2>/dev/null; exit 1; \
	fi; \
	printf '%s\n[0 14 0]\n\004' "$$(cat $(NOTEBOOK))" \
	  | timeout -s KILL 30 $(QEMU) -kernel $(KERNEL) \
	  | tee >(sed -n 's/\r$$//; /^% /p' >> $(NOTEBOOK)) > $(BUILD)/nb2.log 2>&1; \
	wait; \
	# The record says 2 and the count line says 3, and that is not a
	# contradiction: [0 14 0] reads the count as it was before the line ran,
	# so the two restored lines are the 2 it answers and the line being asked
	# is the 3 it leaves behind.  Both are checked, because a notebook that
	# wrote the count after the fact would restore a session one line ahead
	# of itself.
	if ! grep -q 'restored 2, answering 7' $(BUILD)/nb2.log; then \
	    echo "make notebook-test: FAILED -- the notebook did not restore both lines"; \
	    sed -n '/guest book/,$$p' $(BUILD)/nb2.log; exit 1; \
	fi; \
	if ! grep -q '  2  (3 so far)' $(BUILD)/nb2.log; then \
	    echo "make notebook-test: FAILED -- the restored session forgot its history"; \
	    sed -n '/guest book/,$$p' $(BUILD)/nb2.log; exit 1; \
	fi; \
	if ! grep -q '^% \[0 \[14 0\]\]: 2$$' $(NOTEBOOK); then \
	    echo "make notebook-test: FAILED -- the line run after the restore was not written down"; \
	    cat $(NOTEBOOK); exit 1; \
	fi; \
	rm -f $(NOTEBOOK2); \
	printf '%% [1 7 0]: 8\n[0 14 0]\n\004' \
	  | timeout -s KILL 30 $(QEMU) -kernel $(KERNEL) \
	  | tee >(sed -n 's/\r$$//; /^% /p' >> $(NOTEBOOK2)) > $(BUILD)/nb3.log 2>&1; \
	wait; \
	if ! grep -q 'does not say what it answered' $(BUILD)/nb3.log; then \
	    echo "make notebook-test: FAILED -- a record that lies about its answer was taken"; \
	    sed -n '/guest book/,$$p' $(BUILD)/nb3.log; exit 1; \
	fi; \
	if ! grep -q '  0  (1 so far)' $(BUILD)/nb3.log; then \
	    echo "make notebook-test: FAILED -- the session was not left alone after a bad record"; \
	    sed -n '/guest book/,$$p' $(BUILD)/nb3.log; exit 1; \
	fi; \
	echo "make notebook-test: SIGKILL, reboot, and the session came back with its history"

# Both suites.  The machine's own first, because it is the thing everything else
# is a claim about.
check: test hoontest proofs notebook-test rules-test
	@echo "make check: the machine's suite, the compiler's, the jet proofs and the notebook all passed"

# --- the rules (Step 5) and their removal (Step 6) --------------------------
#
# The whole of items 26 and 27 on the wire: type a rule, watch it be checked in
# full, watch a jet be answered by it within the domain and by the C native
# outside it, put it away and watch the native answer everywhere again, and
# watch the records come back on a later boot and be re-verified rather than
# re-admitted -- the notebook's last word on a primitive wins.  The same
# definition is compiled into the machine's own checklist (gb_rules_ok); if the
# copy below drifts from it, the battery refuses or the echo disagrees and this
# test says so.
#
# ADD_DEF and MUL_DEF are item 23's +add and item 28's +mul; decisions 26 and 28
# are the rules they are checked over, a + b < 64 and a * b < 128.  The counters
# are asserted as print, and they are exact: every boot runs both batteries in
# the checklist, so both rules' probes and the +add hints inside the +mul
# self-calling case are already on the tally before this test types anything --
# the machine says so below, and this test is read against what the machine
# prints.  The +mul install is a second battery in the same session, which is
# why the identity map has to cover the whole heap (boot/boot.S) and not just
# the low 64 MiB the machines once needed.
ADD_DEF := [9 [126 [[10 [[126 [1 [[6 [[5 [[0 [62 0]] [[1 [0 0]] 0]]] [[6 [[5 [[0 [14 0]] [[0 [2 0]] 0]]] [[9 [126 [[10 [[126 [0 [126 0]]] [[10 [[62 [1 [1 0]]] [[10 [[30 [0 [30 0]]] [[10 [[14 [1 [0 0]]] [[10 [[6 [0 [6 0]]] [[10 [[2 [0 [2 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] [[9 [126 [[10 [[126 [0 [126 0]]] [[10 [[62 [1 [0 0]]] [[10 [[30 [4 [[0 [30 0]] 0]]] [[10 [[14 [4 [[0 [14 0]] 0]]] [[10 [[6 [0 [6 0]]] [[10 [[2 [0 [2 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]]] [[6 [[5 [[0 [14 0]] [[0 [6 0]] 0]]] [[0 [30 0]] [[9 [126 [[10 [[126 [0 [126 0]]] [[10 [[62 [1 [1 0]]] [[10 [[30 [4 [[0 [30 0]] 0]]] [[10 [[14 [4 [[0 [14 0]] 0]]] [[10 [[6 [0 [6 0]]] [[10 [[2 [0 [2 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]]] 0]]]] 0]]] [[10 [[62 [1 [0 0]]] [[10 [[30 [1 [0 0]]] [[10 [[14 [1 [0 0]]] [[10 [[6 [0 [6 0]]] [[10 [[2 [0 [2 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]]
MUL_DEF := [9 [126 [[10 [[126 [1 [[6 [[5 [[0 [14 0]] [[0 [6 0]] 0]]] [[0 [62 0]] [[6 [[5 [[0 [30 0]] [[0 [2 0]] 0]]] [[9 [126 [[10 [[126 [0 [126 0]]] [[10 [[2 [0 [2 0]]] [[10 [[6 [0 [6 0]]] [[10 [[14 [4 [[0 [14 0]] 0]]] [[10 [[30 [1 [0 0]]] [[10 [[62 [0 [62 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] [[9 [126 [[10 [[126 [0 [126 0]]] [[10 [[2 [0 [2 0]]] [[10 [[6 [0 [6 0]]] [[10 [[14 [0 [14 0]]] [[10 [[30 [4 [[0 [30 0]] 0]]] [[10 [[62 [4 [[0 [62 0]] 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]]] 0]]]] 0]]] [[10 [[62 [1 [0 0]]] [[10 [[30 [1 [0 0]]] [[10 [[14 [1 [0 0]]] [[10 [[6 [0 [6 0]]] [[10 [[2 [0 [2 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]]

# The rule that lies about its domain, and the rule that lies about +add.  Both
# have to be refused, the first for claiming a domain the machine does not
# certify and the second because the battery caught that +add(0, 0) is not 3.
RULES_J  := $(BUILD)/rules.log
RULES_J2 := $(BUILD)/rules2.log

rules-test: $(KERNEL)
	@rm -f $(RULES_J)
	@{ printf '! 0 $(ADD_DEF)\n[11 [0 [1 [[9 5] 0]]] [0 2 0] 0]\n[11 [0 [1 [[1000 2000] 0]]] [0 2 0] 0]\n!\n! 0 0\n[11 [0 [1 [[9 5] 0]]] [0 2 0] 0]\n!\n\004'; } \
	  | $(QEMU) -kernel $(KERNEL) \
	  | tee >(sed -n 's/\r$$//; /^[%!] /p' >> $(RULES_J)) > $(BUILD)/rules1.log 2>&1; \
	wait; \
	if ! grep -q 'yes\.  +add is now a rule, sent as text and checked in full: 2080 pairs' $(BUILD)/rules1.log; then \
	    echo "make rules-test: FAILED -- the line was not checked in full before it was admitted"; \
	    cat $(BUILD)/rules1.log; exit 1; \
	fi; \
	if ! grep -Fqx "! 0 $(ADD_DEF): 64" $(RULES_J); then \
	    echo "make rules-test: FAILED -- the rule was not written down as a record"; \
	    cat $(RULES_J); exit 1; \
	fi; \
	if ! grep -q '      jet  +add(9, 5) = 14' $(BUILD)/rules1.log; then \
	    echo "make rules-test: FAILED -- +add(9, 5) was not answered inside the domain"; \
	    sed -n '/== guest book/,$$p' $(BUILD)/rules1.log; exit 1; \
	fi; \
	if ! grep -q '      jet  +add(1000, 2000) = 3000' $(BUILD)/rules1.log; then \
	    echo "make rules-test: FAILED -- +add(1000, 2000) was not answered outside the domain"; \
	    sed -n '/== guest book/,$$p' $(BUILD)/rules1.log; exit 1; \
	fi; \
	if ! grep -q 'the rule has answered 132 probes, the native 6' $(BUILD)/rules1.log; then \
	    echo "make rules-test: FAILED -- the counters do not divide between the definition and the native"; \
	    sed -n '/== guest book/,$$p' $(BUILD)/rules1.log; exit 1; \
	fi; \
	if ! grep -Fqx "! 0 0: 0" $(RULES_J); then \
	    echo "make rules-test: FAILED -- the removal was not written down as a record"; \
	    cat $(RULES_J); exit 1; \
	fi; \
	if ! grep -q 'is no longer a rule; the C native answers' $(BUILD)/rules1.log; then \
	    echo "make rules-test: FAILED -- the rule was not put away"; \
	    sed -n '/== guest book/,$$p' $(BUILD)/rules1.log; exit 1; \
	fi; \
	if ! grep -q 'domain a + b < 64\.  the C native answers' $(BUILD)/rules1.log; then \
	    echo "make rules-test: FAILED -- the report does not say the native answers everywhere again"; \
	    sed -n '/== guest book/,$$p' $(BUILD)/rules1.log; exit 1; \
	fi; \
	rm -f $(RULES_J2); \
	printf '%s\n!\n[0 14 0]\n! 0 0\n\004' "$$(cat $(RULES_J))" \
	  | $(QEMU) -kernel $(KERNEL) \
	  | tee >(sed -n 's/\r$$//; /^[%!] /p' >> $(RULES_J2)) > $(BUILD)/rules2.log 2>&1; \
	wait; \
	if ! grep -q 'yes\.  +add is now a rule, sent as text and checked in full: 2080 pairs' $(BUILD)/rules2.log; then \
	    echo "make rules-test: FAILED -- a rule record was not re-verified on the next boot"; \
	    cat $(BUILD)/rules2.log; exit 1; \
	fi; \
	if ! grep -q 'domain a + b < 64\.  the C native answers' $(BUILD)/rules2.log; then \
	    echo "make rules-test: FAILED -- the replayed removal did not stay away (last word loses)"; \
	    cat $(BUILD)/rules2.log; exit 1; \
	fi; \
	if ! grep -q '  3  (4 so far)' $(BUILD)/rules2.log; then \
	    echo "make rules-test: FAILED -- the rule lines were not written down as session history"; \
	    sed -n '/== guest book/,$$p' $(BUILD)/rules2.log; exit 1; \
	fi; \
	if ! grep -q 'there is no rule for +add to remove' $(BUILD)/rules2.log; then \
	    echo "make rules-test: FAILED -- a removal with nothing to remove was taken"; \
	    sed -n '/== guest book/,$$p' $(BUILD)/rules2.log; exit 1; \
	fi; \
	printf '! 0 $(ADD_DEF): 65\n!\n\004' \
	  | $(QEMU) -kernel $(KERNEL) \
	  | tee >(sed -n 's/\r$$//; /^[%!] /p' >> $(RULES_J2)) > $(BUILD)/rules3.log 2>&1; \
	wait; \
	if ! grep -q 'claims a domain the machine does not' $(BUILD)/rules3.log; then \
	    echo "make rules-test: FAILED -- a rule record that lies about its domain was taken"; \
	    cat $(BUILD)/rules3.log; exit 1; \
	fi; \
	printf '! 0 [1 3 0]\n\004' \
	  | $(QEMU) -kernel $(KERNEL) \
	  | tee >(sed -n 's/\r$$//; /^[%!] /p' >> $(RULES_J2)) > $(BUILD)/rules4.log 2>&1; \
	wait; \
	if ! grep -q 'the definition said +add(0, 0) is 3; the native says 0' $(BUILD)/rules4.log; then \
	    echo "make rules-test: FAILED -- a definition that lies about +add was not refused by name"; \
	    cat $(BUILD)/rules4.log; exit 1; \
	fi; \
	printf '! 2 $(MUL_DEF)\n[11 [2 [1 [[9 5] 0]]] [0 2 0] 0]\n[11 [2 [1 [[1000 2000] 0]]] [0 2 0] 0]\n!\n! 2 0\n[11 [2 [1 [[9 5] 0]]] [0 2 0] 0]\n!\n\004' \
	  | $(QEMU) -kernel $(KERNEL) \
	  | tee >(sed -n 's/\r$$//; /^[%!] /p' >> $(RULES_J2)) > $(BUILD)/rules5.log 2>&1; \
	wait; \
	if ! grep -q 'yes\.  +mul is now a rule, sent as text and checked in full: 892 pairs' $(BUILD)/rules5.log; then \
	    echo "make rules-test: FAILED -- the +mul line was not checked in full before it was admitted"; \
	    cat $(BUILD)/rules5.log; exit 1; \
	fi; \
	if ! grep -Fqx "! 2 $(MUL_DEF): 128" $(RULES_J2); then \
	    echo "make rules-test: FAILED -- the +mul rule was not written down as a record with its product domain"; \
	    grep -a '^! 2' $(RULES_J2); exit 1; \
	fi; \
	if ! grep -q '      jet  +mul(9, 5) = 45' $(BUILD)/rules5.log; then \
	    echo "make rules-test: FAILED -- +mul(9, 5) was not answered inside the domain"; \
	    sed -n '/== guest book/,$$p' $(BUILD)/rules5.log; exit 1; \
	fi; \
	if ! grep -q '      jet  +mul(1000, 2000) = 2000000' $(BUILD)/rules5.log; then \
	    echo "make rules-test: FAILED -- +mul(1000, 2000) was not answered outside the domain"; \
	    sed -n '/== guest book/,$$p' $(BUILD)/rules5.log; exit 1; \
	fi; \
	if ! grep -q 'the rule has answered 3 probes, the native 4' $(BUILD)/rules5.log; then \
	    echo "make rules-test: FAILED -- the +mul counters do not divide between the definition and the native"; \
	    grep -a '^  +mul:' $(BUILD)/rules5.log; exit 1; \
	fi; \
	if ! grep -Fqx "! 2 0: 0" $(RULES_J2); then \
	    echo "make rules-test: FAILED -- the +mul removal was not written down as a record"; \
	    grep -a '^! 2 0' $(RULES_J2); exit 1; \
	fi; \
	if ! grep -q 'yes\.  +mul is no longer a rule; the C native answers' $(BUILD)/rules5.log; then \
	    echo "make rules-test: FAILED -- the +mul rule was not put away"; \
	    sed -n '/== guest book/,$$p' $(BUILD)/rules5.log; exit 1; \
	fi; \
	if ! grep -q 'domain a \* b < 128 with a and b each under 128\.  the C native answers' $(BUILD)/rules5.log; then \
	    echo "make rules-test: FAILED -- the +mul report does not say the native answers everywhere again"; \
	    grep -a '^  +mul:' $(BUILD)/rules5.log; exit 1; \
	fi; \
	printf '! 2 $(MUL_DEF): 64\n\004' \
	  | $(QEMU) -kernel $(KERNEL) \
	  | tee >(sed -n 's/\r$$//; /^[%!] /p' >> $(RULES_J2)) > $(BUILD)/rules6.log 2>&1; \
	wait; \
	if ! grep -q 'claims a domain the machine does not' $(BUILD)/rules6.log; then \
	    echo "make rules-test: FAILED -- a +mul record claiming a sum domain was taken"; \
	    cat $(BUILD)/rules6.log; exit 1; \
	fi; \
	printf '! 2 [1 3 0]\n\004' \
	  | $(QEMU) -kernel $(KERNEL) \
	  | tee >(sed -n 's/\r$$//; /^[%!] /p' >> $(RULES_J2)) > $(BUILD)/rules7.log 2>&1; \
	wait; \
	if ! grep -q 'the definition said +mul(0, 0) is 3; the native says 0' $(BUILD)/rules7.log; then \
	    echo "make rules-test: FAILED -- a definition that lies about +mul was not refused by name"; \
	    cat $(BUILD)/rules7.log; exit 1; \
	fi; \
	printf '%s\n!\n\004' "$$(cat $(RULES_J2))" \
	  | $(QEMU) -kernel $(KERNEL) \
	  > $(BUILD)/rules8.log 2>&1; \
	wait; \
	if ! grep -q 'yes\.  +mul is now a rule, sent as text and checked in full: 892 pairs' $(BUILD)/rules8.log; then \
	    echo "make rules-test: FAILED -- a +mul record was not re-verified on the next boot"; \
	    cat $(BUILD)/rules8.log; exit 1; \
	fi; \
	if ! grep -q 'domain a \* b < 128 with a and b each under 128\.  the C native answers' $(BUILD)/rules8.log; then \
	    echo "make rules-test: FAILED -- the replayed +mul removal did not stay away (last word loses)"; \
	    cat $(BUILD)/rules8.log; exit 1; \
	fi; \
	echo "make rules-test: a rule is text, checked in full, re-verified out of the notebook, and put away by the same record shape"

debug: $(KERNEL)
	qemu-system-x86_64 -machine pc -m 256 -no-reboot \
	    -display none -serial stdio -monitor none -S -s

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/host/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(HOSTCFLAGS) -MMD -MP -c $< -o $@

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

# The .d files the -MMD -MP rules above write.  Without these, editing a header
# does not rebuild the objects that include it, and make links the old ones
# silently: the binary is then not the source, and a test that passes is a test
# of last week's kernel.  That is how NOCK_MAX_DEPTH stayed 10000 in the
# compiled image after kernel.h said 7000.  decisions.md item 31.
-include $(OBJS:.o=.d) $(HOON_OBJS:.o=.d) $(PROOFS_OBJS:.o=.d)

# --- a real Urbit, in QEMU, next to this one ---------------------------------
#
# Everything above is Lamp: a freestanding Nock machine that boots with no
# operating system under it.  This section is the other half of the project's
# stated endgame (docs/state.md, "the realistic endgame is Lamp as a scaffold
# *around* a real Urbit on the host"), and it is deliberately a separate thing
# rather than a change to the kernel.  Nothing in urbit/ is compiled into
# build/boot.elf and `make check` does not depend on any of it, so the freestanding
# machine keeps its guarantee while a real Urbit sits beside it.
#
# The shape is: a pinned Alpine bzImage with its pinned NIC driver modules, a
# pinned static vere, and a root filesystem holding nothing but those, a pill
# for the fake ship, and a ~330-line init (urbit/urbit-init.c, counted the way
# tools/lines.awk counts).  No distribution, no package manager, no shell, no
# modloop.  decisions.md item 33.
URBIT_DIR   := $(BUILD)/urbit
URBIT_LOCK  := urbit/urbit.lock
# Guest RAM, and 3072 is not enough.  The loom is a protected reservation of
# 2GiB, so a 3GiB guest has about 1GiB left to parse a 217MB pill into a ship,
# and it does not: the replay finishes, then "boot: parsing %brass pill" is
# followed by "king: boot failed" and nothing else, every time, at 2GiB and 3GiB
# and loom 30.  At 5GiB the same image gets all the way to installing the
# compiler and vanes and opening the dojo.  Override with URBIT_MEM= if the
# machine is smaller; nothing else in this section will tell you why it failed.
URBIT_MEM   ?= 5120
# The loom exponent, which is also a memory knob: 31 is 2GB and is what the
# 4.x loom wants, and a lower one lets the ship boot on a smaller machine at the
# cost of a smaller address space.  URBIT_LOOM=30 make urbit is 1GB.
URBIT_LOOM  ?= 31
# URBIT_VERBOSE=1 passes vere -v, which is how a pill replay that fails is
# made to say where.
URBIT_VERBOSE ?= 0
URBIT_PORT  ?= 8080
URBIT_SMP   ?= 2

# The lock is included conditionally, and this is load-bearing in the direction
# that is easy to get backwards.  A hard `include` makes the whole Makefile fail
# to parse when urbit/urbit.lock is absent -- including `make test` and
# `make check`, which need nothing from this section and are the guarantee the
# rest of the tree rests on.  A missing file in one corner should not take the
# freestanding machine down with it.  So the pins are left empty instead, the
# file targets below are given no prerequisites they could ever satisfy, and
# every entry point refuses by name: the project's standing answer to a premise
# it cannot back up (decisions.md item 26).
URBIT_LOCKED := $(wildcard $(URBIT_LOCK))
ifeq ($(URBIT_LOCKED),)
VERE_VERSION :=
VERE_URL     :=
VERE_TGZ_SHA256 :=
VERE_TGZ_BYTES  :=
VERE_FILE    :=
VERE_SHA256  :=
VERE_BYTES   :=
PILL_URL     :=
PILL_FILE    :=
PILL_SHA256  :=
PILL_BYTES   :=
KERNEL_URL   :=
KERNEL_APK_SHA256 :=
KERNEL_APK_BYTES  :=
KERNEL_FILE  :=
KERNEL_SHA256 :=
KERNEL_BYTES :=
KERNEL_MODULES :=
URBIT_PREREQS :=
URBIT_COMET_PREREQS :=
URBIT_FETCH_PREREQS :=
else
include $(URBIT_LOCK)
URBIT_PREREQS := $(URBIT_DIR)/initramfs.cpio.gz
URBIT_COMET_PREREQS := $(URBIT_DIR)/initramfs-comet.cpio.gz
URBIT_FETCH_PREREQS := $(URBIT_DIR)/.fetched
# The three driver modules the guest loads, as full paths, for mkinitramfs.py.
URBIT_MODULE_FILES := $(foreach m,$(KERNEL_MODULES),$(URBIT_DIR)/$(m))
endif

# Said the same way everywhere, so "no lock" is one sentence rather than four.
define URBIT_NO_LOCK
echo "make: urbit/urbit.lock is missing, and every target in this section needs it."; \
echo "  The freestanding machine does not: 'make test' and 'make check' are"; \
echo "  unaffected.  Restore the lock from git and try again."; \
exit 1
endef


# Fetching is separate from building, and both are separate from running: a
# wrong checksum should stop the build rather than surprise somebody twenty
# minutes into a boot.
#
# vere is a special case and the reason is worth keeping.  The release ships a
# gzipped tarball and what goes into the initramfs is the binary inside it, so
# there are two files and two hashes: the tarball is checked on the way in, the
# binary is checked after it is unpacked.  The first version of this compared
# the tarball against the binary's hash and refused a perfectly good download --
# which is the pin doing its job and the recipe being wrong at once, and the
# only reason it was caught at all is that the sizes were recorded as well.
$(URBIT_DIR)/.fetched: $(URBIT_LOCK)
	@mkdir -p $(URBIT_DIR)
	@echo "make urbit-fetch: vere $(VERE_VERSION) and $(KERNEL_FILE), checked against urbit/urbit.lock"
	@set -e; \
	fetch() { \
	  what=$$1; url=$$2; file=$$3; want=$$4; size=$$5; \
	  if [ -f $(URBIT_DIR)/$$file ] \
	     && [ "$$(sha256sum $(URBIT_DIR)/$$file | cut -d' ' -f1)" = "$$want" ]; then \
	    echo "  $$file: already here and correct"; return 0; \
	  fi; \
	  echo "  $$file: fetching"; \
	  curl -sSfL --retry 3 -o $(URBIT_DIR)/$$file.tmp "$$url"; \
	  got=$$(sha256sum $(URBIT_DIR)/$$file.tmp | cut -d' ' -f1); \
	  if [ "$$got" != "$$want" ]; then \
	    echo "  $$file: FAILED -- sha256 is $$got, not $$want"; \
	    echo "  Nothing was installed.  A pin that disagrees with the server means"; \
	    echo "  either the release was re-cut or the download was tampered with."; \
	    echo "  'make urbit-update' reports what a new release looks like without"; \
	    echo "  changing anything here."; \
	    rm -f $(URBIT_DIR)/$$file.tmp; exit 1; \
	  fi; \
	  got=$$(stat -c%s $(URBIT_DIR)/$$file.tmp); \
	  if [ "$$got" != "$$size" ]; then \
	    echo "  $$file: FAILED -- $$got bytes, not $$size"; \
	    rm -f $(URBIT_DIR)/$$file.tmp; exit 1; \
	  fi; \
	  mv $(URBIT_DIR)/$$file.tmp $(URBIT_DIR)/$$file; \
	  echo "  $$file: sha256 and size ok"; \
	}; \
	fetch_vere() { \
	  if [ -f $(URBIT_DIR)/$(VERE_FILE) ] \
	     && [ "$$(sha256sum $(URBIT_DIR)/$(VERE_FILE) | cut -d' ' -f1)" = "$(VERE_SHA256)" ]; then \
	    echo "  $(VERE_FILE): already here and correct"; return 0; \
	  fi; \
	  echo "  $(VERE_FILE): fetching (a tarball, then the binary inside it)"; \
	  curl -sSfL --retry 3 -o $(URBIT_DIR)/vere.tgz.tmp "$(VERE_URL)"; \
	  got=$$(sha256sum $(URBIT_DIR)/vere.tgz.tmp | cut -d' ' -f1); \
	  if [ "$$got" != "$(VERE_TGZ_SHA256)" ]; then \
	    echo "  vere.tgz: FAILED -- sha256 is $$got, not $(VERE_TGZ_SHA256)"; \
	    echo "  Nothing was installed."; rm -f $(URBIT_DIR)/vere.tgz.tmp; exit 1; \
	  fi; \
	  got=$$(stat -c%s $(URBIT_DIR)/vere.tgz.tmp); \
	  if [ "$$got" != "$(VERE_TGZ_BYTES)" ]; then \
	    echo "  vere.tgz: FAILED -- $$got bytes, not $(VERE_TGZ_BYTES)"; \
	    rm -f $(URBIT_DIR)/vere.tgz.tmp; exit 1; \
	  fi; \
	  echo "  vere.tgz: sha256 and size ok"; \
	  rm -rf $(URBIT_DIR)/vere.tgz.d; mkdir -p $(URBIT_DIR)/vere.tgz.d; \
	  tar xzf $(URBIT_DIR)/vere.tgz.tmp -C $(URBIT_DIR)/vere.tgz.d; \
	  got=$$(sha256sum $(URBIT_DIR)/vere.tgz.d/$(VERE_FILE) | cut -d' ' -f1); \
	  if [ "$$got" != "$(VERE_SHA256)" ]; then \
	    echo "  $(VERE_FILE): FAILED -- the binary inside the tarball is $$got"; \
	    echo "  and the pin says $(VERE_SHA256).  Nothing was installed."; \
	    rm -rf $(URBIT_DIR)/vere.tgz.tmp $(URBIT_DIR)/vere.tgz.d; exit 1; \
	  fi; \
	  got=$$(stat -c%s $(URBIT_DIR)/vere.tgz.d/$(VERE_FILE)); \
	  if [ "$$got" != "$(VERE_BYTES)" ]; then \
	    echo "  $(VERE_FILE): FAILED -- $$got bytes, not $(VERE_BYTES)"; \
	    rm -rf $(URBIT_DIR)/vere.tgz.tmp $(URBIT_DIR)/vere.tgz.d; exit 1; \
	  fi; \
	  mv $(URBIT_DIR)/vere.tgz.d/$(VERE_FILE) $(URBIT_DIR)/$(VERE_FILE); \
	  rm -rf $(URBIT_DIR)/vere.tgz.tmp $(URBIT_DIR)/vere.tgz.d; \
	  echo "  $(VERE_FILE): sha256 and size ok"; \
	}; \
	fetch_kernel() { \
	  if [ -f $(URBIT_DIR)/$(KERNEL_FILE) ] \
	     && [ "$$(sha256sum $(URBIT_DIR)/$(KERNEL_FILE) | cut -d' ' -f1)" = "$(KERNEL_SHA256)" ] \
	     && [ -f $(URBIT_DIR)/virtio_net.ko ]; then \
	    echo "  $(KERNEL_FILE): already here and correct"; return 0; \
	  fi; \
	  echo "  linux-virt: fetching the package (kernel and its modules together)"; \
	  curl -sSfL --retry 3 -o $(URBIT_DIR)/linux-virt.apk.tmp "$(KERNEL_URL)"; \
	  got=$$(sha256sum $(URBIT_DIR)/linux-virt.apk.tmp | cut -d' ' -f1); \
	  if [ "$$got" != "$(KERNEL_APK_SHA256)" ]; then \
	    echo "  linux-virt.apk: FAILED -- sha256 is $$got, not $(KERNEL_APK_SHA256)"; \
	    echo "  Nothing was installed."; rm -f $(URBIT_DIR)/linux-virt.apk.tmp; exit 1; \
	  fi; \
	  got=$$(stat -c%s $(URBIT_DIR)/linux-virt.apk.tmp); \
	  if [ "$$got" != "$(KERNEL_APK_BYTES)" ]; then \
	    echo "  linux-virt.apk: FAILED -- $$got bytes, not $(KERNEL_APK_BYTES)"; \
	    rm -f $(URBIT_DIR)/linux-virt.apk.tmp; exit 1; \
	  fi; \
	  echo "  linux-virt.apk: sha256 and size ok"; \
	  rm -rf $(URBIT_DIR)/kv; mkdir -p $(URBIT_DIR)/kv; \
	  ( cd $(URBIT_DIR)/kv && tar xzf ../linux-virt.apk.tmp ) 2>/dev/null; \
	  for m in $(KERNEL_MODULES); do \
	    src=$$(find $(URBIT_DIR)/kv/lib/modules -name "$$m.gz" -o -name "$$m" 2>/dev/null | head -1); \
	    if [ -z "$$src" ]; then \
	      echo "  $$m: FAILED -- not in the package"; \
	      echo "  Nothing was installed."; rm -rf $(URBIT_DIR)/kv $(URBIT_DIR)/linux-virt.apk.tmp; exit 1; \
	    fi; \
	    case "$$src" in *.gz) gunzip -c "$$src" > $(URBIT_DIR)/$$m ;; \
	                 *) cp "$$src" $(URBIT_DIR)/$$m ;; esac; \
	  done; \
	  src=$$(find $(URBIT_DIR)/kv/boot -name 'vmlinuz*' 2>/dev/null | head -1); \
	  if [ -z "$$src" ]; then \
	    echo "  $(KERNEL_FILE): FAILED -- the package has no bzImage in boot/"; \
	    echo "  Nothing was installed."; rm -rf $(URBIT_DIR)/kv $(URBIT_DIR)/linux-virt.apk.tmp; exit 1; \
	  fi; \
	  mv "$$src" $(URBIT_DIR)/$(KERNEL_FILE); \
	  got=$$(sha256sum $(URBIT_DIR)/$(KERNEL_FILE) | cut -d' ' -f1); \
	  if [ "$$got" != "$(KERNEL_SHA256)" ]; then \
	    echo "  $(KERNEL_FILE): FAILED -- the bzImage in the package is $$got"; \
	    echo "  and the pin says $(KERNEL_SHA256).  Nothing was installed."; \
	    rm -rf $(URBIT_DIR)/kv $(URBIT_DIR)/linux-virt.apk.tmp; exit 1; \
	  fi; \
	  got=$$(stat -c%s $(URBIT_DIR)/$(KERNEL_FILE)); \
	  if [ "$$got" != "$(KERNEL_BYTES)" ]; then \
	    echo "  $(KERNEL_FILE): FAILED -- $$got bytes, not $(KERNEL_BYTES)"; \
	    rm -rf $(URBIT_DIR)/kv $(URBIT_DIR)/linux-virt.apk.tmp; exit 1; \
	  fi; \
	  echo "  $(KERNEL_FILE): sha256 and size ok"; \
	  echo "  $(KERNEL_MODULES): extracted, in load order"; \
	  rm -rf $(URBIT_DIR)/kv $(URBIT_DIR)/linux-virt.apk.tmp; \
	}; \
	fetch_vere; \
	fetch pill   "$(PILL_URL)"   "$(PILL_FILE)"   "$(PILL_SHA256)"   "$(PILL_BYTES)"; \
	fetch_kernel; \
	touch $@

# The init is a static binary and the archive is written by hand, because a
# cpio(1) cannot record a device node that does not already exist and making one
# needs CAP_MKNOD.  urbit/mkinitramfs.py fills the rdev fields in directly, so
# the filesystem builds as an unprivileged user and is byte-for-byte
# reproducible: mtime 0, no uid, no hostname, gzip mtime 0.
$(URBIT_DIR)/init: urbit/urbit-init.c
	@mkdir -p $(dir $@)
	$(CC) -static -O2 -Wall -Wextra -DURBIT_LOOM='"$(URBIT_LOOM)"' -DURBIT_VERBOSE=$(URBIT_VERBOSE) -DURBIT_COMET=0 -o $@ $<

# A comet is a second image, not a flag: it has no pill, and its init is built
# with URBIT_COMET=1 so the exec line cannot be the fake ship's by accident.  A
# comet that booted with -F zod would be a fake ship wearing a comet's name,
# and it would be offline, and it would look like it was working.
$(URBIT_DIR)/init-comet: urbit/urbit-init.c
	@mkdir -p $(dir $@)
	$(CC) -static -O2 -Wall -Wextra -DURBIT_LOOM='"$(URBIT_LOOM)"' -DURBIT_VERBOSE=$(URBIT_VERBOSE) -DURBIT_COMET=1 -o $@ $<

$(URBIT_DIR)/initramfs.cpio.gz: $(URBIT_DIR)/init $(URBIT_DIR)/.fetched urbit/mkinitramfs.py
	@echo "make urbit-rootfs: a root filesystem with two files in it"
	@rm -rf $(URBIT_DIR)/root
	@mkdir -p $(URBIT_DIR)/root
	@cp $(URBIT_DIR)/init $(URBIT_DIR)/root/init
	@python3 urbit/mkinitramfs.py $(URBIT_DIR)/root $@ \
	    $(URBIT_DIR)/$(VERE_FILE) $(URBIT_DIR)/$(PILL_FILE) $(URBIT_MODULE_FILES)

$(URBIT_DIR)/initramfs-comet.cpio.gz: $(URBIT_DIR)/init-comet $(URBIT_DIR)/.fetched urbit/mkinitramfs.py
	@echo "make comet-rootfs: a root filesystem with no pill in it"
	@rm -rf $(URBIT_DIR)/root-comet
	@mkdir -p $(URBIT_DIR)/root-comet
	@cp $(URBIT_DIR)/init-comet $(URBIT_DIR)/root-comet/init
	@python3 urbit/mkinitramfs.py $(URBIT_DIR)/root-comet $@ \
	    $(URBIT_DIR)/$(VERE_FILE) $(URBIT_MODULE_FILES)

urbit-fetch: $(URBIT_FETCH_PREREQS)
	@if [ -z "$(VERE_URL)" ]; then $(URBIT_NO_LOCK); fi

urbit-rootfs: $(URBIT_PREREQS)
	@if [ -z "$(VERE_URL)" ]; then $(URBIT_NO_LOCK); fi

comet-rootfs: $(URBIT_COMET_PREREQS)
	@if [ -z "$(VERE_URL)" ]; then $(URBIT_NO_LOCK); fi

# Booting a fake ship installs the Arvo kernel, which is tens of thousands of
# events of replay on a machine with no KVM: minutes, not seconds.  The dojo is
# then on the serial console and on the host's $(URBIT_PORT).
#
# A fake ship is the right first thing here.  -F zod needs no Azimuth identity,
# no key file and no network, and it disables ames, so nothing has to be
# sponsored and nothing has to be reachable.  It is a complete Urbit that
# cannot talk to the network, which is exactly the shape of a thing Lamp can be
# pointed at.
urbit: $(URBIT_PREREQS)
	@if [ -z "$(VERE_URL)" ]; then $(URBIT_NO_LOCK); fi
	@echo "make urbit: a fake ship on the serial console, dojo on http://localhost:$(URBIT_PORT)/"
	@echo "  first boot installs the Arvo kernel and takes a few minutes; Ctrl-A X quits"
	@echo "  loom $(URBIT_LOOM) ($$(( (1 << $(URBIT_LOOM)) / 1024 / 1024 / 1024 ))GiB), $(URBIT_MEM)MB of guest RAM"
	@echo "  the pin is $(VERE_VERSION), checked against urbit/urbit.lock on every fetch"
	@qemu-system-x86_64 -machine pc -m $(URBIT_MEM) -smp $(URBIT_SMP) \
	    -nographic -no-reboot \
	    -kernel $(URBIT_DIR)/$(KERNEL_FILE) \
	    -initrd $(URBIT_DIR)/initramfs.cpio.gz \
	    -append "console=ttyS0 quiet panic=1" \
	    -netdev user,id=n0,hostfwd=tcp::$(URBIT_PORT)-:8080 \
	    -device virtio-net-pci,netdev=n0

# A comet is the other half of item 33, and it is a different animal in every
# way that matters:
#
#   -F zod, offline, the pinned pill, the same Arvo every single time, no
#   identity, no sponsorship, minutes.  That is `make urbit`, and it is the
#   shape of a thing Lamp can be pointed at.
#
#   -c, online, no pill: a real anonymous ship that has to find a star, ask to
#   be sponsored and sync from it, which on a machine with no KVM is hours and
#   not minutes.  It is a citizen of the public Urbit network, and there is no
#   version of "hermetic" that applies to it.
#
# So the two are separate targets and separate images, and neither is a flag on
# the other.  What a comet is actually for, here, is the thing item 33 says
# the fake ship cannot do: a real Arvo that is current, reached by sync, rather
# than by replaying a pill that was pinned weeks ago.
comet: $(URBIT_COMET_PREREQS)
	@if [ -z "$(VERE_URL)" ]; then $(URBIT_NO_LOCK); fi
	@echo "make comet: a real ship, mining against a star, on the serial console"
	@echo "  dojo on http://localhost:$(URBIT_PORT)/ once it finishes mining"
	@echo "  loom $(URBIT_LOOM) ($$(( (1 << $(URBIT_LOOM)) / 1024 / 1024 / 1024 ))GiB), $(URBIT_MEM)MB of guest RAM"
	@echo "  this joins the public network.  with no KVM a full sync is hours."
	@echo "  the pin is $(VERE_VERSION), checked against urbit/urbit.lock on every fetch"
	@echo "  there is no pill here: a comet fetches its own from the star"
	@qemu-system-x86_64 -machine pc -m $(URBIT_MEM) -smp $(URBIT_SMP) \
	    -nographic -no-reboot \
	    -kernel $(URBIT_DIR)/$(KERNEL_FILE) \
	    -initrd $(URBIT_DIR)/initramfs-comet.cpio.gz \
	    -append "console=ttyS0 quiet panic=1" \
	    -netdev user,id=n0,hostfwd=tcp::$(URBIT_PORT)-:8080 \
	    -device virtio-net-pci,netdev=n0

# Repinning is the "updating" half, and it is deliberately not automatic.  A
# checksum that changed under you is a new binary with new behaviour, and this
# project's rule is that a new number is written down with the reason before it
# is believed -- so this prints the diff and changes the lock, and a person runs
# `make urbit` afterwards to find out whether the new pin still boots.
urbit-update:
	@set -e; \
	new=$$(curl -sSfL -o /dev/null -w '%{url_effective}' \
	    https://github.com/urbit/vere/releases/latest); \
	tag=$${new##*/}; \
	if [ "$$tag" = "$(VERE_VERSION)" ]; then \
	    echo "make urbit-update: $$tag is already the pin"; \
	else \
	    echo "make urbit-update: latest vere is $$tag, the lock says $(VERE_VERSION)"; \
	    curl -sSfL --retry 3 -o /tmp/urbit-vere.tgz \
	        "https://github.com/urbit/vere/releases/download/$$tag/linux-x86_64.tgz"; \
	    rm -rf /tmp/urbit-vere && mkdir -p /tmp/urbit-vere; \
	    tar xzf /tmp/urbit-vere.tgz -C /tmp/urbit-vere; \
	    bin=$$(ls /tmp/urbit-vere | head -1); \
	    sha=$$(sha256sum /tmp/urbit-vere/$$bin | cut -d' ' -f1); \
	    size=$$(stat -c%s /tmp/urbit-vere/$$bin); \
	    tsha=$$(sha256sum /tmp/urbit-vere.tgz | cut -d' ' -f1); \
	    tsize=$$(stat -c%s /tmp/urbit-vere.tgz); \
	    echo "  $$bin"; \
	    echo "    sha256 $$sha"; echo "    $$size bytes"; \
	    echo "  linux-x86_64.tgz"; \
	    echo "    sha256 $$tsha"; echo "    $$tsize bytes"; \
	    echo; \
	    echo "  Both, because the release is a tarball and the binary is what"; \
	    echo "  runs.  The tarball is checked on the way in and the binary after"; \
	    echo "  it is unpacked."; \
	    echo; \
	    echo "  This is a new binary.  Lamp does not check Arvo's behaviour, so"; \
	    echo "  nothing here proves it is good.  Boot it before believing it:"; \
	    echo; \
	    echo "    make urbit-check-new URBIT_VERE=$$tag"; \
	    echo; \
	    echo "  If it boots, re-pin with those four numbers:"; \
	    echo; \
	    echo "    make urbit-repin VERE_VERSION=$$tag VERE_FILE=$$bin \\"; \
	    echo "      VERE_SHA256=$$sha VERE_BYTES=$$size \\"; \
	    echo "      VERE_TGZ_SHA256=$$tsha VERE_TGZ_BYTES=$$tsize"; \
	    echo; \
	    echo "  'make urbit-repin' rewrites the lock and nothing else.  The"; \
	    echo "  kernel and the pill keep their own pins: a new runtime is not a"; \
	    echo "  reason to re-download a 217 MB kernel."; \
	fi

# Boot a release the lock does not name, without changing the lock.  This has
# to exist, because `urbit-update` tells a person to run it: a message pointing
# at a target that is not there is a sentence in a list rather than a step, and
# this is the one place in the tree where a person is asked to run something
# unchecked on purpose.
#
#   make urbit-check-new URBIT_VERE=vere-v4.7
#
# The binary is deliberately not checked against the lock.  It is a release the
# lock does not name yet, and writing a pin down before a person has watched it
# boot is the thing this section exists to avoid.  What it prints instead is the
# sha256 and the size, which are exactly the two numbers `urbit-repin` wants and
# are printed *before* the boot, so a person who gives up waiting for the replay
# already has them.  The pill and the kernel stay pinned: only the runtime is
# new, so the question being asked is a question about one binary.
URBIT_NEW_DIR := $(BUILD)/urbit-new
URBIT_VERE    ?=

urbit-check-new: $(URBIT_FETCH_PREREQS)
	@if [ -z "$(VERE_URL)" ]; then $(URBIT_NO_LOCK); fi
	@if [ -z "$(URBIT_VERE)" ]; then \
	  echo "make urbit-check-new: say which release to try."; \
	  echo "  'make urbit-update' prints the line for the latest one."; \
	  echo; \
	  echo "    make urbit-check-new URBIT_VERE=vere-v4.7"; \
	  echo; exit 1; \
	fi
	@set -e; \
	rm -rf $(URBIT_NEW_DIR); mkdir -p $(URBIT_NEW_DIR); \
	echo "make urbit-check-new: $(URBIT_VERE), which the lock does not name"; \
	curl -sSfL --retry 3 -o $(URBIT_NEW_DIR)/vere.tgz \
	    "https://github.com/urbit/vere/releases/download/$(URBIT_VERE)/linux-x86_64.tgz"; \
	tar xzf $(URBIT_NEW_DIR)/vere.tgz -C $(URBIT_NEW_DIR); \
	bin=$$(ls $(URBIT_NEW_DIR) | grep -v '\.tgz$$' | head -1); \
	sha=$$(sha256sum $(URBIT_NEW_DIR)/$$bin | cut -d' ' -f1); \
	size=$$(stat -c%s $(URBIT_NEW_DIR)/$$bin); \
	tsha=$$(sha256sum $(URBIT_NEW_DIR)/vere.tgz | cut -d' ' -f1); \
	tsize=$$(stat -c%s $(URBIT_NEW_DIR)/vere.tgz); \
	echo; \
	echo "  $$bin"; \
	echo "    sha256 $$sha"; echo "    $$size bytes"; \
	echo "  linux-x86_64.tgz"; \
	echo "    sha256 $$tsha"; echo "    $$tsize bytes"; \
	echo; \
	echo "  These are the numbers a re-pin needs, and they are the only claim"; \
	echo "  made about this binary.  Nothing has checked that it works."; \
	echo; \
	rm -rf $(URBIT_NEW_DIR)/root; mkdir -p $(URBIT_NEW_DIR)/root; \
	cp $(URBIT_DIR)/init $(URBIT_NEW_DIR)/root/init; \
	python3 urbit/mkinitramfs.py $(URBIT_NEW_DIR)/root \
	    $(URBIT_NEW_DIR)/initramfs.cpio.gz \
	    $(URBIT_NEW_DIR)/$$bin $(URBIT_DIR)/$(PILL_FILE) >/dev/null; \
	echo "  booting it against the pinned $(PILL_FILE) and the pinned kernel."; \
	echo "  First boot installs the Arvo kernel and takes a few minutes; Ctrl-A X quits."; \
	qemu-system-x86_64 -machine pc -m $(URBIT_MEM) -smp $(URBIT_SMP) \
	    -nographic -no-reboot \
	    -kernel $(URBIT_DIR)/$(KERNEL_FILE) \
	    -initrd $(URBIT_NEW_DIR)/initramfs.cpio.gz \
	    -append "console=ttyS0 quiet panic=1" \
	    -netdev user,id=n0,hostfwd=tcp::$(URBIT_PORT)-:8080 \
	    -device virtio-net-pci,netdev=n0

# Rewrite the lock, and only the lock.  This is the one target in the tree that
# edits a checked-in file, so it takes all six numbers or none of them, and it
# prints the diff it made rather than leaving that to be found out later -- a
# re-pin that silently did nothing is exactly the failure this project has spent
# seven steps learning to name (item 31: a build that quietly disagrees with its
# source is worse than no build at all).
#
# Six rather than four because there are two files.  A re-pin that set the
# binary's hash and left the tarball's alone would fetch, unpack, and then fail
# on a pin nobody had looked at since the last release -- which is a confusing
# way to learn that a number is stale.
#
# If the binary is still sitting in build/urbit-new from a `urbit-check-new`, the
# sha256 given here is checked against it before anything is written, so a
# number copied from the wrong line fails here rather than at the next fetch.
urbit-repin:
	@if [ ! -f $(URBIT_LOCK) ]; then $(URBIT_NO_LOCK); fi
	@set -e; \
	for v in VERE_VERSION VERE_FILE VERE_SHA256 VERE_BYTES \
	         VERE_TGZ_SHA256 VERE_TGZ_BYTES; do \
	  if [ -z "$$(eval echo \$$$$v)" ]; then \
	    echo "make urbit-repin: $$v was not given."; \
	    echo "  A re-pin takes all six or none of them -- the release is a"; \
	    echo "  tarball and the binary inside it are two different files:"; \
	    echo; \
	    echo "    make urbit-repin VERE_VERSION=vere-v4.7 VERE_FILE=vere-v4.7-linux-x86_64 \\"; \
	    echo "      VERE_SHA256=<64 hex> VERE_BYTES=<size> \\"; \
	    echo "      VERE_TGZ_SHA256=<64 hex> VERE_TGZ_BYTES=<size>"; \
	    echo; \
	    echo "  'make urbit-update' prints all six."; \
	    echo; exit 1; \
	  fi; \
	done; \
	for h in VERE_SHA256 VERE_TGZ_SHA256; do \
	  if ! printf '%s' "$$(eval echo \$$$$h)" | grep -Eq '^[0-9a-f]{64}$$'; then \
	    echo "make urbit-repin: $$h is not 64 lowercase hex characters."; \
	    exit 1; \
	  fi; \
	done; \
	if [ -f $(URBIT_NEW_DIR)/$(VERE_FILE) ]; then \
	  got=$$(sha256sum $(URBIT_NEW_DIR)/$(VERE_FILE) | cut -d' ' -f1); \
	  if [ "$$got" != "$(VERE_SHA256)" ]; then \
	    echo "make urbit-repin: FAILED -- $(URBIT_NEW_DIR)/$(VERE_FILE) is $$got"; \
	    echo "  and the pin says $(VERE_SHA256).  Nothing was written."; \
	    exit 1; \
	  fi; \
	  echo "  the file in build/urbit-new hashes to the pin.  Good."; \
	else \
	  echo "  build/urbit-new/$(VERE_FILE) is not here, so the pin was not checked"; \
	  echo "  against anything.  'make urbit-check-new URBIT_VERE=$(VERE_VERSION)'"; \
	  echo "  prints it, and the fetch will check it on the way in."; \
	fi; \
	if [ -f $(URBIT_NEW_DIR)/vere.tgz ]; then \
	  got=$$(sha256sum $(URBIT_NEW_DIR)/vere.tgz | cut -d' ' -f1); \
	  if [ "$$got" != "$(VERE_TGZ_SHA256)" ]; then \
	    echo "make urbit-repin: FAILED -- the tarball is $$got and the pin says"; \
	    echo "  $(VERE_TGZ_SHA256).  Nothing was written."; \
	    exit 1; \
	  fi; \
	  echo "  the tarball in build/urbit-new hashes to its pin.  Good."; \
	fi; \
	cp $(URBIT_LOCK) $(URBIT_LOCK).bak; \
	sed -i \
	  -e 's|^VERE_VERSION :=.*|VERE_VERSION := $(VERE_VERSION)|' \
	  -e 's|^VERE_URL     :=.*|VERE_URL     := https://github.com/urbit/vere/releases/download/$(VERE_VERSION)/linux-x86_64.tgz|' \
	  -e 's|^VERE_TGZ_SHA256 :=.*|VERE_TGZ_SHA256 := $(VERE_TGZ_SHA256)|' \
	  -e 's|^VERE_TGZ_BYTES  :=.*|VERE_TGZ_BYTES  := $(VERE_TGZ_BYTES)|' \
	  -e 's|^VERE_FILE    :=.*|VERE_FILE    := $(VERE_FILE)|' \
	  -e 's|^VERE_SHA256  :=.*|VERE_SHA256  := $(VERE_SHA256)|' \
	  -e 's|^VERE_BYTES   :=.*|VERE_BYTES   := $(VERE_BYTES)|' \
	  $(URBIT_LOCK); \
	echo; \
	diff -u $(URBIT_LOCK).bak $(URBIT_LOCK) || true; \
	rm -f $(URBIT_LOCK).bak; \
	echo; \
	echo "  Only the runtime moved.  The pill and the kernel keep their pins."; \
	echo "  'make urbit-fetch' now fetches the new binary, checked against this."

# What the lock says, as sha256sum output, so it can be diffed against a
# fetched file by hand without trusting any of the code above.
urbit-checksums: $(URBIT_FETCH_PREREQS)
	@if [ -z "$(VERE_URL)" ]; then $(URBIT_NO_LOCK); fi
	@cd $(URBIT_DIR) && sha256sum $(VERE_FILE) $(PILL_FILE) $(KERNEL_FILE)
	@echo
	@echo "  That is the binary, the pill and the kernel.  The tarball is not in"
	@echo "  there: it is unpacked and deleted once the binary has been checked"
	@echo "  against the pin that matters, which is the binary's."

urbit-clean:
	rm -rf $(URBIT_DIR) $(URBIT_NEW_DIR)
