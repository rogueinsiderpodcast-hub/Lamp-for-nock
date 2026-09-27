# Lamp

A freestanding Nock machine. Step 1: it boots, it evaluates Nock, it tests
itself, it prints the result, it halts.

Nock is a 300-word specification of a combinator calculator. It is the layer
Urbit's Hoon compiles down to, and it is small enough to read in full and
implement in full. Lamp implements all of it, from nothing, on a serial port.

```
$ make test
...
  checklist: 9 of 9 done
  LAMP: LIT
```

## What it is

A 64-bit x86 program loaded straight into long mode by QEMU's PVH loader.
No operating system, no libc, no bootloader, no disk, no network. The only piece
of hardware it touches is the 16550 serial port at 0x3f8, polled, one byte at a
time.

The whole machine is 1,991 lines, counting code but not comments or blank
lines: 133 of assembly to reach long mode, 1,200 of C, and 658 of tests. It
implements Nock 4K — all twelve opcodes — over a noun representation where
atoms are 63-bit numbers and cells point into an arena that only ever grows, so
the state of the machine is a log and its history is everything it has already
written.

## Running it

Needs `qemu-system-x86_64` (`sudo pacman -S qemu-system-x86`), a C compiler and
binutils. Nothing else.

```
make        # build build/boot.elf and build/boot.bin
make run    # boot it, watch the serial line
make test   # boot it, fail the build if any check fails
make debug  # boot it with QEMU stopped at the reset vector
make clean
```

`make test` is the interesting one. The machine runs its own test suite on
boot, prints a pass or fail line for every check, and exits QEMU with a status
the Makefile turns into a build failure. There is no host-side test harness
because there is no host-side machine to test.

It then opens the guest book and waits, which is why the test pipes a line and a
Ctrl-D rather than hanging: `[1 42 0]` is a formula, and there is one build and
one binary, so there is no test mode to switch into.

    printf '[1 42 0]\n\004' | make run

Type a noun in brackets, press enter, and the machine tells you what it read.

## Layout

```
boot/boot.S        PVH entry, 32-bit -> 64-bit, stack, .bss zeroing
boot/link.ld       one load segment at 0x100000
kernel/kernel.h    the only header
kernel/serial.c    polled 16550 UART
kernel/memory.c    PVH memory map, bump allocator
kernel/noun.c      nouns, slot, edit, structural equality
kernel/nock.c      the interpreter
kernel/primitives.c  the twenty native primitives
kernel/main.c      facts, self-test, checklist, halt
tests/nock-tests.c the test suite, with every expected value derived by hand
docs/decisions.md  every decision, why, and whether it is proven
```

## What is verified, and what is not

This matters more than the feature list, so it is stated plainly.

**Verified by the test suite, mechanically, on every boot:**

- the 64-bit handover, the serial line, and the memory map
- the noun representation: 63-bit atoms, structural equality, tree addressing,
  persistent edit
- all twelve Nock opcodes, each against a value derived by hand from the rules,
  written out beside the test
- the pairs that are easy to get backwards: opcode 2 against opcode 7, literal
  arguments against formula arguments
- every crash path, including a formula that provably reduces to itself, and
  then checks that the machine still works afterwards
- all twenty primitives at the C level, including overflow, underflow, division
  by zero and decrementing zero
- the append-only property: a subject is unchanged part for part after an
  evaluation that edited it, and the arena only grows
- determinism: the same subject and formula give the same answer twice
- a jet firing from a dynamic hint, and the answer being identical with the
  hook enabled and disabled

**Not verified, and said so in `docs/decisions.md`:**

- that opcodes 6 and 9 match the reference implementation. The specification's
  own expansions for them do not bracket into valid formulas, so they are
  implemented from the accompanying prose, which is what `urbit/vere` does
  (item 6)
- that the twenty primitives are equivalent to any Nock formula. In Urbit these
  are jets, and a jet is only legitimate because it computes what its Nock
  definition computes. There is no Hoon compiler or standard library here yet,
  so there is nothing to compare against (item 9)
- the hint convention used to dispatch jets is ours, not Urbit's. It is a
  demonstration that the mechanism works, and it is labelled as ours (item 10)
- the rules were not cross-checked against a second implementation. The
  interpreter is the only one here, and the tests were written from the same
  reading of the rules, so a misreading would not be caught (item 11)

None of these are hard. They are the work of Step 2, and they are listed rather
than buried.

## The idea this is a piece of

A conventional operating system is a pile of mutable state that is hard to
reason about and harder to reproduce. An operating *function* is one function
from history to present. Urbit is an attempt at that. Lamp is a very small,
fully readable machine whose entire state is an append-only list, whose native
trust base is twenty functions you can read in one sitting, and whose history is
not deleted but appended to.

Step 2 is the guest book: a real program inside this machine that computes its
own next state from its own history, and remembers everything you did this
session. Step 3 is the bridge — a Hoon compiler running on the host, so that
the instructions you type are written in a language rather than in brackets.

## Licence

Do as you like with it.
