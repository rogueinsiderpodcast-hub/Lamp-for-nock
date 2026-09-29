# Lamp

A freestanding Nock machine. It boots, it evaluates Nock, it tests itself, it
prints the result, it halts; it opens a guest book that remembers everything you
did this session; it runs instructions written in a small Hoon-shaped language
that a program on the host compiles into the bracket text the guest already
knows how to read; and it rewrites part of its own behaviour from text you send
it, checking that text in full before it is allowed to mean anything.

Nock is a 300-word specification of a combinator calculator. It is the layer
Urbit's Hoon compiles down to, and it is small enough to read in full and
implement in full. Lamp implements all of it, from nothing, on a serial port.

```
$ make test
...
  checklist: 13 of 13 done
  LAMP: LIT
```

## What it is

A 64-bit x86 program loaded straight into long mode by QEMU's PVH loader.
No operating system, no libc, no bootloader, no disk, no network. The only piece
of hardware it touches is the 16550 serial port at 0x3f8, polled, one byte at a
time.

The whole machine is 3,043 lines, counting non-blank lines with `/* */` and
`//` comments stripped: 133 of assembly to reach long mode, 1,922 of C, 852 of
tests, and 136 of headers. `make lines` runs the count, so the figure is a
command and not a claim. (Stated that way because the previous figure, 1,991,
could not be reproduced by any counting method and was therefore not worth
carrying, and because a number nobody can re-derive is a number nobody should
trust: the method is `tools/lines.awk`, and it is awk because a `/*` comment can
open on one line and close on another, which any per-line filter gets wrong.
The 852 of tests are 262 checks over the noun layer, the interpreter, the
primitives, the reader and the book; the rules claim is not in them, because the
2080-pair `+add` battery and the 892-pair `+mul` one fit the machine's arena and
not the host's, so they are made
in-kernel, by the checklist item that runs the batteries and by `make rules-test`.
The host compiler in `tools/hoon.c` is 951
lines and is *not* in this figure, because it is not part of the machine — it is
the thing that feeds it, and neither is `tools/jet-proofs.c`.) It
implements Nock 4K — all twelve opcodes — over a noun representation where
atoms are 63-bit numbers and cells point into an arena that only ever grows, so
the state of the machine is a log and its history is everything it has already
written.

## Running it

Needs `qemu-system-x86_64` (`sudo pacman -S qemu-system-x86`), a C compiler and
binutils. `make notebook` also wants bash, because it keeps the guest's output
moving while a second process copies the records out of it, and a POSIX sh
pipeline cannot do both at once without holding the prompt back until the next
newline. Nothing else does.

```
make        # build build/boot.elf and build/boot.bin
make run    # boot it, watch the serial line
make test   # boot it, fail the build if any check fails
make check  # all five suites: machine, compiler, jet proofs, notebook, rules
make hoontest  # the host compiler's own checks
make proofs  # the native primitives against their Nock definitions
make teach  # compile eleven expressions and watch the guest run them
make notebook  # run the guest book with a notebook on the host (Step 4)
make notebook-test  # SIGKILL the machine, reboot, and check the session came back
make notebook-forget  # throw the notebook away, on purpose
make rules-test  # a rule sent as text, checked in full, re-verified from the notebook, and put away
make debug  # boot it with QEMU stopped at the reset vector
make lines  # count the machine's lines, the way this README counts them
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

A line may be 4096 characters, with 256 open brackets and 4096 items in it,
which is the same as saying that Step 3's compiler has to fit inside a page;
a longer line is refused by name rather than truncated. Those three numbers are
in `kernel.h` and are part of the reader's contract, so a host program can read
them rather than guess them.

Type a formula in brackets and press enter, and the machine runs it and answers:

    > [1 42 0]
      42  (1 so far)
    > [1 7 0]
      7  (2 so far)
    > [0 14 0]
      2  (3 so far)

A blank line prints the session, newest first. The session is a noun and the
step that produces it is a formula, so a line can read its own history: `[0 14
0]` is how many lines have run, `[0 6 0]` the last answer, `[0 8 0]` the line
before this one, `[0 18 0]` that line's answer. A line that crashes or runs out
of steps says so and leaves the session exactly as it was.

### Writing it in something that reads like a language

`build/hoon` compiles a small Hoon-shaped language to that same bracket text, and
`make teach` sends the result down the serial line and checks what comes back:

```
$ make build/hoon
$ ./build/hoon '=>(/14 ?:(/2 1 2))'
[8 [[0 [14 0]] [[6 [[0 [2 0]] [[1 [1 0]] [[1 [2 0]] 0]]]] 0]]]
$ ./build/hoon '=+(arm |(0 3) ?:(=(/4 /10) /10 ~(arm |(|(+(/4) /10) arm))))'
[9 [6 ... a core, and the machine walks the loop at run time ...]]
$ ./build/hoon '+(2 3)'
[1 [5 0]]
$ ./build/hoon '+(/14 1)'
hoon: addition of two values that are only known at run time has no Nock definition in this machine yet: the twenty native operations are a tested bank and not proven jets, and writing this rune as a native call would make the compiler answer questions the machine has never been asked.  +(2 3) works, because the host can fold that.
```

Thirteen forms: an atom, `/axis`, a name, a list `[a b]`, `?(a)`, `=(a b)`,
`*(a b)`, `+(a)`, `+(a b)` for two literals only, `~(arm core)`, `|(a b ...)`,
`=>` (push on, run there) and `?:(c t e)` (0 is true). On top of those, `=+(arm
sample body)` builds a core: `[sample arm 0]`, the arm at `/6` whatever the
sample is, the sample's things at `/4`, `/10`, `/22`, and a name in a body a
read of that `/6`. That is enough to write a loop, and `make teach` sends eleven
expressions down the serial line and checks the machine's own answers to all of
them — seven of them cores, nested cores, a pushed core and two loops. `+(a b)` for anything
but two literals is still refused by name, because no opcode in this machine adds
two values it only has at run time. An address is checked against the subject
the expression will be run on, so an address that is not there is refused by name
rather than compiled — a wrong address on this machine usually names a real noun
instead of crashing, and the whole point of the language is that it is loud about
that. `docs/decisions.md` item 22 has the table and the reasons, item 24 has the core,
the loop, what it costs, and the two silent wrong answers the compiler now
refuses, and `docs/state.md` has the four things the compiler got wrong first
and the three it got wrong later.

### Keeping the session when the power goes off

Every line that runs is written down as it runs, as a record: the formula, a
colon, and the answer it gave. The host keeps them in a file, and the next boot
is handed them before your own input, so the session is rebuilt by replaying its
own log rather than by loading a snapshot back in.

```
$ make notebook
  > [1 42 0]
  42  (1 so far)
% [1 [42 0]]: 42
  > [0 14 0]
  1  (2 so far)
% [0 [14 0]]: 1
  > # power cut here -- the machine is killed, not shut down
$ make notebook
  > % [1 [42 0]]: 42
  restored 1, answering 42
  > % [0 [14 0]]: 1
  restored 2, answering 1
  > [0 14 0]
  2  (3 so far)
% [0 [14 0]]: 2
```

There is no disk, no filesystem and no driver involved, because the kill list
has none of those: the serial line is the only thing that can outlive the
machine, and the file is on the host at the other end of it. `make
notebook-test` is the claim checked on every build, and it is checked with
SIGKILL rather than a clean exit, because a notebook written on the way out
would pass a test that shut down politely and fail this one. A record that lies
about its answer is refused by name and the session is left alone, so a
hand-edited notebook cannot quietly become a session.

**Step 5 is in:** `! 0 <definition>` typed at the same prompt as everything else
is a rule -- a Nock formula claiming to be a primitive. Before it may be used,
the machine checks the claim on the wire: it runs every pair in the primitive's
certified domain (`a + b < 64` for `+add`, 2080 pairs, near 1.9 million cells)
with its own interpreter and compares each answer with its own native. Only then
is the rule installed and written down as a record, and on a later boot the
record is re-verified rather than believed. Inside the domain the rule answers,
outside it the native does, and the machine counts and reports which path
answered. Step 6 put it away: `! 0 0` removes the rule, echoed as the record
`! 0 0: 0`, and replay is the notebook's last word -- a records file that ends
in a removal boots without the rule. Step 7 made a rule a property of the
machine rather than of one primitive: `+mul` is a rule over `a * b < 128` (with
both operands under 128, so the set is finite -- 892 pairs, 1.4 million cells),
`! 2 <definition>` is the same record shape, and a domain is a *shape* and a
number rather than a number. Step 7 also found and fixed a Step 1 bug: the
kernel's identity page map covered only the low 64 MiB while the bump allocator
trusted the boot memory map for the whole heap, so the machine promised nearly
twice the cells it had pages for and triple-faulted the first time a session
checked a second rule. `make rules-test` is the claim, run from `make check`.

## Layout

```
boot/boot.S        PVH entry, 32-bit -> 64-bit, identity map, stack, .bss zeroing
boot/link.ld       one load segment at 0x100000
kernel/kernel.h    the only header
kernel/serial.c    polled 16550 UART
kernel/memory.c    PVH memory map, bump allocator
kernel/noun.c      nouns, slot, edit, structural equality
kernel/nock.c      the interpreter
kernel/primitives.c  the twenty native primitives
kernel/book.c      the guest book's one formula, and the session it builds
kernel/guestbook.c  the line editor, the parser, the loop, the display
kernel/main.c      facts, self-test, checklist, halt
tests/harness.c    the counters and expectations both suites share
tests/nock-tests.c the Nock suite, with every expected value derived by hand
tests/guestbook-tests.c  the reader and the book
tools/hoon.c       the host compiler, and its 40 checks
tools/jet-proofs.c the natives against their Nock definitions
tools/host-machine.c  the machine's serial and heap, stood up on the host
tools/lines.awk    the line count this README carries
docs/decisions.md  every decision, why, and whether it is proven
docs/state.md      where this stands, and what is still open
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
  hook enabled and disabled — plus that the native was handed the two numbers the
  formula wrote, that a hint of any other shape is not jetted at all, and that a
  native which stops backs the hint out instead of stopping the machine
- the notebook, in `make notebook-test`: a SIGKILL mid-session, a reboot, and a
  session that came back with its history -- plus a record whose answer does not
  reproduce, which is refused by name and leaves the session alone
- the rules, in `make rules-test`: a rule typed at the machine is checked in
  full (all 2080 pairs of `a + b < 64` for `+add`, all 892 of `a * b < 128` with
  both operands under 128 for `+mul`) before it is installed, jetted answers
  come from the rule in-domain and from the C native out of it with the counters
  to prove which, a `!` record fed back on a later boot is re-verified rather
  than trusted, a record or definition that lies is refused by name, and putting
  the rule away is also a record: `! 0 0` is echoed `! 0 0: 0`, a replayed
  notebook's last word on a primitive wins, and a removal with nothing to remove
  is refused by name -- with the definitions' 992 and 736 characters in the
  Makefile required to match the machine's own echo byte for byte
- the arena claim itself: the checklist reports the cells its two rule batteries
  settled at against what the arena holds (3,352,981 of 8,316,544), and the
  kernel's identity page map covers the whole heap so that number is cells the
  machine can actually write -- the first time a session checked a second rule,
  the old 64 MiB map triple-faulted on a promise it had no pages for (item 29)
- three of the twenty primitives against their Nock definitions, in `make proofs`:
  `+inc`, `+eq` and `+not` are read by the machine's own reader, printed back by
  the machine's own printer, run by the machine's own interpreter, and required to
  agree with the native over 1,412 inputs — including on which inputs stop, since
  a native that answers where its definition would have stopped is the one way to
  make a wrong machine faster. The other seventeen are named in the same table
  with the reason each is out of reach, and a primitive in neither is a failure

**Not verified, and said so in `docs/decisions.md`:**

- that opcodes 6 and 9 match the reference implementation. The specification's
  own expansions for them do not bracket into valid formulas, so they are
  implemented from the accompanying prose, which is what `urbit/vere` does
  (item 6)
- that the other seventeen primitives are equivalent to any Nock formula, and
  cannot be: Nock's arithmetic here is opcode 4 and opcode 5, increment and
  equality, and a jet cannot make up the difference because a native's answer is
  discarded rather than handed to the formula. Counting is the only addition left
  and costs its operand — the counting `+add` in item 23 is 992 characters, the
  reader takes it and the printer gives it back byte for byte, and it answers
  `+add(1000, 1000) = 2000` before stopping with `call depth exceeded` at
  `+add(3000, 500)`, against a battery whose smallest large input is 2^31. A
  loop turns out not to need a name at all: a core can rebuild itself around its
  own arm (item 23). Every primitive is named in one of two tables in
  `tools/jet-proofs.c`, proved or pending-with-a-reason, and one in neither fails
  the suite (items 9 and 23)
- the hint convention used to dispatch jets is ours, not Urbit's. It is a
  demonstration that the mechanism works, and it is labelled as ours (item 10)
- the rules were not cross-checked against a second implementation. The
  interpreter is the only one here, and the tests were written from the same
  reading of the rules, so a misreading would not be caught (item 11)
- that the notebook on the host is not tampered with between boots. A restore
  checks every record against the answer written beside it and refuses one that
  does not reproduce -- for a `!` record it re-runs the battery rather than
  believing the answer at all -- and it is still not a proof against an attacker
  who can edit both halves of a `%` record, or swap one rule definition for a
  different one that still passes the battery. Those are the remaining edges of
  Step 5's "cannot be lied to", and a `!` record that claims a domain the
  machine does not certify is refused by name (items 25 and 26)

None of these are hard, and none of them is the guest book: the session is a
noun and the step is a formula, so what is left here is a question about Hoon
and about jets, which is Step 3. They are listed rather than buried.

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
Step 5 is where behaviour stops being built-in: the machine can accept a Nock
definition of one of its own primitives as text, prove it over a bounded domain,
and use it — a very small step toward a machine whose trust base is what it has
acted on rather than what it was born with.

## Licence

Do as you like with it.
