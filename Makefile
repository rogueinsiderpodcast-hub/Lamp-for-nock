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
# call, and a thousand calls fit inside the 10000-frame depth limit while ten
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
# ADD_DEF is item 23's +add, decision 23 wrote it down and decision 26 is the
# rule that is checked over a + b < 64.  The counters are asserted as print, and
# they are exact: the checklist itself now proves a removed rule stops answering
# on every boot, which is one more native probe than Step 5's numbers had -- the
# machine says so below, and this test is read against what the machine prints.
ADD_DEF := [9 [126 [[10 [[126 [1 [[6 [[5 [[0 [62 0]] [[1 [0 0]] 0]]] [[6 [[5 [[0 [14 0]] [[0 [2 0]] 0]]] [[9 [126 [[10 [[126 [0 [126 0]]] [[10 [[62 [1 [1 0]]] [[10 [[30 [0 [30 0]]] [[10 [[14 [1 [0 0]]] [[10 [[6 [0 [6 0]]] [[10 [[2 [0 [2 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] [[9 [126 [[10 [[126 [0 [126 0]]] [[10 [[62 [1 [0 0]]] [[10 [[30 [4 [[0 [30 0]] 0]]] [[10 [[14 [4 [[0 [14 0]] 0]]] [[10 [[6 [0 [6 0]]] [[10 [[2 [0 [2 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]]] [[6 [[5 [[0 [14 0]] [[0 [6 0]] 0]]] [[0 [30 0]] [[9 [126 [[10 [[126 [0 [126 0]]] [[10 [[62 [1 [1 0]]] [[10 [[30 [4 [[0 [30 0]] 0]]] [[10 [[14 [4 [[0 [14 0]] 0]]] [[10 [[6 [0 [6 0]]] [[10 [[2 [0 [2 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]]] 0]]]] 0]]] [[10 [[62 [1 [0 0]]] [[10 [[30 [1 [0 0]]] [[10 [[14 [1 [0 0]]] [[10 [[6 [0 [6 0]]] [[10 [[2 [0 [2 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]]

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
	if ! grep -q 'the rule has answered 3 probes, the native 6' $(BUILD)/rules1.log; then \
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
	echo "make rules-test: a rule is text, checked in full, re-verified out of the notebook, and put away by the same record shape"

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
