# Decisions

Every decision that shaped Lamp, why it was made, and whether it is proven yet.
Written for a reader who does not want to read C first.

Terms used throughout:

- **Noun** — the only data type in Nock. Either an atom (a natural number) or a
  cell (an ordered pair of nouns). Nothing else.
- **Atom** — a noun that is a number. Here, 0 to 2^63 - 1.
- **Cell** — a noun that is a pair. Written `[head tail]`.
- **Formula** — the code half of a Nock program. Always a cell. `*` runs a
  subject and a formula to get a product.
- **Nock** — the specification of the reduction rules. 12 opcodes, about 300
  words of rules. This machine implements Nock 4K, the current version.

---

## 1. The machine owns one piece of hardware

**Decided:** the only device is the 16550 serial UART at port 0x3f8.

**Why:** the point of the exercise is to be able to read the whole machine and
know what it does. Every device added is code that has to be trusted, and there
is no way to shrink that number later — it only grows. A serial port is the
smallest thing that can talk to a human, and it is a register you can read in
one line of C.

**Also:** no interrupts, no timers, no framebuffer, no disk, no network, no USB.
A character out is a "wait until the transmit register is empty, then write a
byte". That is the whole driver.

**Proven:** yes. `make test` prints through it.

---

## 2. Boot straight into 64-bit mode

**Decided:** the boot code switches to long mode itself. 64-bit from the first
instruction after the handover. No bootloader, no 32-bit compatibility mode
after startup, no `lib32` multilib packages on the host.

**Why:** Nock atoms are large. A 32-bit build caps atoms at 2^31 and then has to
invent a two-word representation for every integer above that, which is
exactly the sort of complexity this project is trying to avoid. The x86
hardware supports 64-bit natively and the host compiler targets it, so there is
nothing to gain by starting smaller.

**Cost:** `boot/boot.S` has to build page tables and a GDT by hand, about 60
lines of assembly. That is the single largest chunk of non-obvious code in the
project.

**Proven:** yes, once `make test` passes.

---

## 3. No inherited drivers, no operating system underneath

**Decided:** freestanding C. `-ffreestanding -nostdlib`. No libc. The kernel
image is loaded by QEMU's PVH loader and the boot code zeroes its own
`.bss` and builds its own stack. The only memory the C code ever sees is what
QEMU's memory map says is available.

**Why:** "no inherited drivers" is the whole claim. A libc would quietly
reintroduce a heap, a `memcpy` with whatever the host compiler decided, and
possibly `setjmp`; a host OS would reintroduce a scheduler, a filesystem and a
network stack. A Nock machine that is supposed to be auditable cannot be built
on top of an unauditable foundation.

**Cost:** a few dozen lines that a libc would normally provide — `memcpy`,
`memset`, `memmove`, decimal printing, and the boot-time `.bss` zeroing.

**Proven:** yes — the build links with no undefined symbols, which is the
mechanical version of "nothing came from outside".

---

## 4. PVH, and only the memory map

**Decided:** the PVH interface, for the memory map and nothing else. QEMU loads
`build/boot.elf`, finds the entry point in the `.note.Xen` note, and jumps to it
in 32-bit protected mode with a physical address in `%ebx` pointing at an
`hvm_start_info` structure. That structure is where the memory map lives.

**Why:** PVH is the interface between a hypervisor and a guest it is loading
directly, which is exactly the relationship here. QEMU is not standing in for a
bootloader, there is no bootloader, and nothing in the machine will ever be
started by a bootloader, so Multiboot's tags — modules, a command line, a
framebuffer, an ACPI table pointer — are surface area for features that cannot
exist here. The whole of what we take is a struct in a register and a list of
fixed-size map entries.

**Why this is also the simple one:** the entry point is four words in an ELF
note, and the handover is a single structure. There is no header to get right
and no tag list to walk: the map is a pointer plus a count, and that is it.

**Cost:** about 30 lines of offset arithmetic. The structures are read field by
field at fixed offsets rather than by casting to a C struct. The layout happens
to be padding-free so a struct would come out the same, but offsets say what
they mean and cannot drift if the compiler or the word size changes.

**Not proven:** only that the map parses and the heap lands above the kernel.
Nothing else in the PVH interface is used, and nothing else ever will be. The
machine has no modules, no console, no framebuffer and no command line to
receive.

---

## 4a. Superseded: Multiboot 2 rather than Multiboot 1

The first version of this machine booted through Multiboot 2, and the reasoning
for choosing it over Multiboot 1 is kept here because it was right and because
the comparison against the PVH map is what made the switch obvious:

> **Decided (superseded):** Multiboot 2, memory map tag only.
>
> **Why:** Multiboot 1's memory map entries are structs with compiler-dependent
> padding, and the size field is ambiguous about how much padding is included.
> Multiboot 2's tags are fixed-size and self-describing: 8 bytes of tag header,
> then 24 bytes per map entry. The fields are read one byte at a time, so there
> is nothing left to a compiler's idea of alignment.
>
> **Cost:** a tag-walking loop, about 40 lines. Multiboot 1 would have been
> shorter and would have been a latent bug.

What the switch exposed: both Multiboot versions are interfaces between a guest
and a *bootloader*, and this machine has no bootloader. The tag-walking loop was
buying alignment safety that PVH gives for free, and paying for it with a
layer of indirection — a tag list to find the map inside — that a hypervisor
interface does not have at all, because there the map is a field.

---

## 5. Nouns are one 64-bit word; cells live in an append-only arena

**Decided:**

```
noun = <1 bit: cell or atom> <63 bits: payload>

bit 0 == 0   atom,   payload is the number
bit 0 == 1   cell,   payload is an index into the arena
```

The arena is a flat array of `{head, tail}` pairs that only ever grows.

**Why:** this is the load-bearing decision of the whole design.

- *Atoms are 63 bits*, so a number is a number, and native 64-bit arithmetic
  is directly an atom. No limb representation.
- *Consing never overwrites and never frees.* Every value the machine has ever
  built is still there, in order. The state of the machine is a list, and its
  history is the list of lists it has already built. That is the "guest book"
  idea, and it falls out of the representation rather than being bolted on.
- *There is no garbage collector to get wrong.* There is no allocation policy
  to tune, no mark phase, no write barrier, no generational anything.
- *Sharing is a bonus, not a requirement.* Identical nouns may share an arena
  cell; nothing depends on it.

**Cost, and it is a real one:** equality is structural, not a word comparison.
Two structurally identical nouns built separately have different arena indices,
so `noun_equal` has to walk them. Deeper nouns cost more to compare. The
alternative — packing head and tail into single bits of one word, as Urbit
does — makes equality free but caps the atoms that can sit directly in a cell,
and the packing is fiddly enough to be a bug source. Correctness by inspection
beat a constant factor, at a scale where the constant factor is irrelevant.

**Cost:** a crash leaves the arena exactly as it was, because nothing is ever
rolled back. That is a feature here. A Nock formula that reduces to itself is
defined as a crash; there is nothing to unwind, because nothing was ever
borrowed.

**Proven:** yes. The suite checks that a subject is unchanged, part for part,
after an evaluation that edited it, and that the arena only ever grows.

---

## 6. Opcodes 6 and 9 are implemented from the wording, not the expansion

**Decided:** opcodes 0 to 5, 7, 8, 10 and 11 follow the rules directly.
Opcodes 6 and 9 follow the *prose* description in the Nock documentation.

**Why:** the Nock 4K specification gives an expansion for opcode 6:

```
*[a 6 b c d]  =  *[a *[[c d] 0 *[[2 3] 0 *[a 4 4 b]]]]
```

That expansion cannot be transcribed literally. It uses a compressed notation in
which `*[a 4 4 b]` and `2 [0 1] 0 b` do not bracket into valid formulas — the
first asks to increment a cell, which the same specification defines as a
crash. Opcode 9's expansion has the same problem. The prose that accompanies the
rules ("`6` is if `b`, then `c`, else `d`") is unambiguous and is what the
reference implementation in `urbit/vere` does.

**Also:** opcode 6 evaluates only the branch that is taken. The expansion, read
charitably, evaluates both. These differ only when the untaken branch would
crash. Lazy is what `vere` does and what Hoon relies on.

**Open item:** if a real Urbit ever needs to run on this machine, these two
opcodes and the literal-versus-formula question in item 7 need to be checked
against `vere` directly. Step 1 has no Hoon and no Urbit compatibility
requirement, so the wording is authoritative here.

---

## 7. Some Nock arguments are literals and some are formulas

**Decided:**

| opcode | argument | literal or formula |
|---|---|---|
| 0 | address | literal |
| 1 | constant | literal |
| 2 | b, c | formulas |
| 3, 4 | b | formula |
| 5 | b, c | formulas |
| 6 | b, c, d | formulas |
| 7 | b formula, c literal | mixed — this is the point |
| 8 | b, c | formulas |
| 9 | address literal, c formula | mixed |
| 10 | `[address literal, value formula]`, d formula | mixed |
| 11 | hint literal, d formula | mixed |

**Why this matters:** getting it backwards produces a machine that is
self-consistent and wrong. The sharpest case is 2 against 7, which have
identical-looking shapes and opposite behaviour: with a second argument of
`[1 5]`, opcode 7 answers `5`, while opcode 2 reduces that argument to the atom
`5`, discovers a formula must be a cell, and crashes. The test suite pins both
halves of that pair down.

Opcode 8 is the same trap wearing a different hat, and it is the one that was
actually got wrong while this machine was being written. `b` is evaluated and
`c` is *used as the formula*, so the two arguments are both formulas but in
different positions — the same word in the table covers two different mechanisms.
Evaluating `c` and then running its product is the opcode-2 reading, and it is
wrong here. The bug was found only by checking the implementation against the
spec line, not by reading the test that exercises it.

**Evidence:** the rule `*[a 0 b] = /[b a]` uses `b` directly, and opcode 11's
own expansion is written `[... 0 3]`, both of which are only meaningful if
address 0's argument is a literal. Same reasoning for opcode 9's address. The
three that matter most, from `vere/doc/spec/nock/4.txt`:

```
*[a 2 b c]  *[*[a b] *[a c]]      both arguments reduced, product of c is the formula
*[a 7 b c]  *[*[a b] c]           b reduced, c itself is the formula
*[a 8 b c]  *[[*[a b] a] c]       b reduced, c itself is the formula
```

Note that 2 differs from 7 and 8 only in the last step, and that is the entire
2-against-7 distinction: what happens to `c`.

**Proven:** the test suite, against both readings of the argument convention.
The opcode-2 success case pins down that `c`'s *product* is the formula (it is
given a constant formula and answers through it), and the 2-against-7 pair pins
down the other half.

**A known laxness, recorded because it was found while looking for something
else.** A formula is checked for being a cell and for having the arguments an
opcode reads, and it is *not* checked for being a list that ends in 0. So `[5 a
b 0]` and `[5 a b 1]` are the same formula to this machine. That is not a wrong
answer, because `arg()` walks to an argument by position and no opcode can
distinguish the two, but it is an asymmetry: too short is a crash ("formula is
missing arguments"), wrongly terminated is not. The check is left out on purpose
— it would cost a test on every formula the machine evaluates, in the hottest
loop there is, to reject a noun that would have been evaluated identically
anyway. A deviance that cannot change an answer is recorded here instead.

---

## 8. A formula that reduces to itself is a crash, and the machine survives it

**Decided:** every `nock_run` has a step budget and a call-depth ceiling. Running
out is a reported crash, not a hang. Crashes are per-evaluation: the machine
carries on and the next evaluation starts clean.

**Why:** Nock defines a non-terminating formula as a crash rather than as
infinity. A machine that hangs is not a machine, it is a hang. But a machine
that stops on the first bad formula is not much of a machine either — and the
whole thesis is that the state is a log you can keep, so being able to write a
bad entry, notice, and continue is the point.

**Proven:** yes. The suite builds a formula that provably reduces to itself,
confirms the step limit stops it, and then confirms the next two evaluations
produce the right answers.

---

## 9. Twenty native primitives, with no wraparound and no undefined behaviour

**Decided:** the native trust base is a fixed table of twenty integer
operations: `+add +sub +mul +div +mod +min +max +eq +lt +le +gt +ge +and +or
+xor +lsh +rsh +inc +dec +not`. Comparisons answer 0 for true and 1 for false,
matching Nock's own convention. Nothing wraps around: any operation that would
leave the 0 to 2^63 - 1 range, go negative, divide by zero, or decrement zero
crashes with a reason.

**Why:** the table is enumerable in one screen and auditable in one sitting.
That is the property worth having. Silent wraparound is not a performance
choice, it is a way of lying to the user about what the machine computed.

**Proven:** yes, at the C level, including every crash case.

**Proved against a Nock definition:** two of the twenty, and the other eighteen
are now named and accounted for rather than quietly unproved.

`+inc` is opcode 4 -- `[4 [[0 [2 0]] 0]]` -- and `+eq` is opcode 5 on two
operands read out of the subject, `[5 [[0 [2 0]] [[0 [6 0]] 0]]]`. `make proofs`
is where that is established: each definition is read by the machine's own
reader, printed by the machine's own printer and required to come back
byte-for-byte as the table spells it, then run by the machine's own interpreter
against the native over 1,412 inputs -- the cross product of thirty interesting
values plus a fixed 512-pair sweep -- and required to agree, *including* on which
inputs stop. A native that answers where its definition would have stopped is
the one way to make a wrong machine faster, so that is counted separately from
the answers rather than inside them.

**Not proven:** the other eighteen. The blocker is not arithmetic, it is that
Nock has no loop. `+add` is a carry chain over 63 bits, the comparisons are bit
scans, `+div` is a long division, and each of them has to iterate. Iterating in
Nock means a core that calls its own arm, which means a noun that contains
itself, and the notation for that in Hoon is a *name*: `=+(a b)`, where `a` names
the arm being written. Lamp's language has no names, so it cannot write a core
that refers to itself, so it cannot write a definition that iterates. Unrolling
63 steps would be longer than a line of input and would prove nothing a loop does
not. So the first job of the next step is names, not proofs -- and this is the
one piece of evidence for that claim worth keeping: the two primitives that
*are* proved are exactly the two whose Nock definition is a single opcode and no
iteration.

**What the battery is and is not,** since a suite that oversells itself is worse
than none: for both proved primitives the rule that gives the definition is one
line of the specification, and the equivalence is settled by setting that rule
beside the two lines of C implementing it. 1,412 inputs cannot establish it. They
are there to catch the day somebody edits a native to be cleverer than the opcode
it stands for, and they were checked by deliberately breaking the natives six
ways -- a wrong answer, a crash one input short of the ceiling, a crash where
the definition answers, a flipped comparison convention, a primitive nobody had
accounted for, and a misspelt name in the table. All six are caught.

---

## 10. Step 1 jets dispatch from a dynamic hint, using our own convention

**Decided:** opcode 11's dynamic hint is the hook:

```
[11 [<atom: primitive index> <formula: argument>] <the real formula>]
```

The interpreter evaluates the argument formula, hands the result to the native
primitive, and throws the primitive's answer away. Every primitive announces
itself and its result on the serial line.

Two rules make that safe rather than merely intended, and both were added after
the tests below turned out not to be testing it:

- **The argument is an atom, or a pair of two atoms** -- the shape the convention
  asks for, and nothing else is jetted at all. `noun_atom_val()` on a cell hands
  back an arena index rather than the value a formula wrote, so a hint carrying
  something else would hand the native a number nobody wrote.
- **The native is a probe, and the real formula is the authority.** If the native
  stops -- an operand that overflows, a division by a zero -- the claim the hint
  made was wrong, and the probe backs out instead of stopping a program whose
  real formula was a moment from answering. The primitives compute in registers
  and allocate nothing, so backing out is clearing the error. A hint that turns
  out not to apply is ordinary rather than faulty, so it is counted, not printed.

**Why:** a dynamic hint is the one place in the Nock specification where an
interpreter is explicitly permitted to do something extra — "a practical
interpreter can do anything with discarded data, so long as the result it
computes complies with the semantics of Nock". That is the entire legal surface
for a jet, and it is the same surface Urbit uses. Using it means a jet cannot
change an answer, which the tests then check by running the same formula with
hooks on and off and comparing.

**This is not Urbit's hint protocol.** Urbit marks jets with specific hint
constants. Ours is a plain array index. It is a demonstration that the mechanism
works end to end, and it is labelled as ours everywhere it appears.

**Proven:** the mechanism. A jet fires, a native function runs, and the
evaluation's answer is bit-for-bit the same with hooks enabled and disabled. A
hint of the wrong shape does not jet, and a native that stops does not stop the
machine.

**Found by looking, and fixed:** the test that was supposed to check all of this
built its argument as the *list* `[2 [3 0]]` while the interpreter read a *pair*
`[2 3]`, so the native was handed `+add(2, 4) = 6` and the test's own comment
claimed `+add(2, 3) = 5`. It passed anyway, because it checked that a primitive
ran, that the answer was unchanged, and not one thing about the numbers that went
in. Counting firings is not a test of a jet: a jet reading the right answer out of
the wrong operand passes every check that only looks at the answer. The test now
asks the interpreter what the native actually received, and asserts the two
numbers written and the sum of them. Both rules above were added because removing
either one makes a check fail.

**Not proven:** that the jet mechanism can stand in for a *standard library*
definition. Two of the twenty primitives now have Nock definitions and are proved
against them (`make proofs`); the other eighteen cannot be written down until the
language has names, because a loop is a name. See item 9.

---

## 11. No host-side Nock implementation to check against

**Decided:** Step 1's ground truth is the written rules plus hand-derived
expected values, not a differential test against another interpreter.

**Why:** every Nock implementation found is at least a decade old C++ with a
runtime, a compiler and a build system far outside this project's scope. Pulling
one in to cross-check would add more unaudited code than the interpreter itself.

**Consequence, stated plainly:** if the reading of the rules in items 6 and 7 is
wrong, this machine is wrong in the same way, and its tests will not notice,
because the tests were written from the same reading.

**That is no longer hypothetical, and it is worth reading how it went.** Getting
the machine green required going outside the test suite for the first time: the
two interpreter bugs Step 1 ended on -- `arg()` never advancing its index, and
opcode 8 evaluating `c` when the rule says it is the formula -- were both found
by putting `vere/doc/spec/nock/4.txt` beside the code. The tests were satisfied
by the wrong behaviour, and no arrangement of more tests written from the same
reading would have caught either.

So the mitigation is not "write more tests". It is that a shared *misreading* is
invisible to a suite derived from it, and the only defence is an authority the
suite was not derived from. The rule already in this file -- that a fix needing
a change in `kernel/` means the interpreter and this document disagree, and has
to be argued rather than assumed -- is what caught it, and it exists because it
was written before it was needed.


---

## 12. Scope: what is deliberately not here

No disk, no filesystem, no persistence across power cycles, no network, no
keyboard or mouse, no Hoon compiler, no Hoon standard library, no scheduler, no
processes, no memory protection, no GPU, no framebuffer, no audio, no Bluetooth,
no package manager, no remote update path, no user accounts, no clock.

The machine boots, evaluates Nock, tests itself, prints the result, and halts.
That is Step 1 in full. Step 2 adds input, which means the serial port is now a
device the machine both reads and writes -- and nothing else, so "no input
device" above means no input device *other than* the UART.

---

## 13. The serial line is the entire user interface, in both directions

**Decided:** the 16550 is read as well as written, still one byte at a time,
still polled, still with no interrupts. A line of printable ASCII ends on enter;
backspace deletes; Ctrl-D leaves. Nothing else is a key.

**Why:** this is the only piece of hardware the machine is allowed (item 1), and
it is a bidirectional wire. A machine that can only speak cannot be talked to,
and adding a second input path would break item 1 to no purpose. The cost of
polling is that the machine is either printing or waiting and never both, which
matters not at all at 115200 baud with one user.

**Cost:** one `serial_getc()`, a bounded line buffer, and a loop that waits on
`LSR_DATA_READY`. That loop is the only place in the machine that can block.

**Proven:** the whole path, from a pipe on the host to bytes in the guest's
buffer and back, by `make test`, which feeds `[1 42]` in and reads the answer
back out of the log. The interactive loop has no test of its own and is not
meant to: it is ten lines of hardware waiting, wrapped around logic that is
tested without it.

---

## 14. One binary; the guest book is entered by feeding it a byte

**Decided:** there is no mode flag. The machine runs its self-test, prints its
verdict, and then opens the guest book, which reads bytes until it is fed
Ctrl-D. `make test` pipes `[1 42]\n\004`; `make run` waits for a person.

**Why:** the first attempt was a boot argument, so `make test` would pass
nothing and `make run` would pass `repl`. That does not work, and finding out
why was worth the detour: **QEMU does not fill in a command line for a PVH
guest.** `hvm_start_info` has `cmdline_paddr` and `cmdline_len` at offsets 24
and 32, the machine parses the same structure for the memory map, and the fields
arrive as zero. So the flag had to be delivered some other way -- a second build
variant, or the mode fixed at compile time -- and both of those put a fact
about *how the machine was started* into something the machine cannot see.

Feeding a byte has none of that problem. The machine is always in the same state
before input arrives; a test is a script rather than a different binary; and
`make test` ends up covering the whole path -- build, boot, self-test, read,
evaluate, print, exit -- instead of stopping at the self-test.

**Consequence, accepted:** a test run now depends on QEMU delivering piped
stdin to the UART. Measured working on QEMU 11.1, and it is the same code path
in every other respect, but it is a dependency that did not exist before and it
would hang rather than fail if it broke.

**Correction to item 4:** that entry said nothing outside the memory map would
ever be used from the start-info structure, and said so with some confidence.
The command line turned out to be exactly the thing that wanted to be used, and
exactly the thing PVH does not deliver. The entry was right about the shape of
the interface and wrong about how much of it exists.

---

## 15. The UART FIFOs are left switched off

**Decided:** `serial_init` writes `0x00` to the FIFO control register. No
receive FIFO, no transmit FIFO, 14-byte trigger or otherwise.

**Why:** measured, not tidiness. QEMU has already buffered the first byte of a
session by the time the kernel runs -- a pipe delivers every byte at once -- and
QEMU discards whatever it is holding at the moment the FIFO is enabled,
whatever the clear bits are set to. Enabling it therefore costs the first byte
of every session, whether the machine is being typed at or fed a script. All
four control values were tried: `0xC1`, `0xC7`, `0x01` and `0x00`, and the
first three each lost the leading `A` of `ABCDE` while `0x00` did not.

**Cost:** QEMU can offer one byte per read, so a piped script transfers at the
speed of a serial port rather than a socket. At 115200 baud that is not a limit
anything here can feel, and a 16-byte FIFO would have bought nothing for a
machine that reads one byte at a time and then stops.


---

## 16. The build forbids SSE, because the boot code never enables it

**Decided:** `CFLAGS` and `ASFLAGS` both carry `-mgeneral-regs-only`. No SSE
register, no AVX, no vectorised anything.

**Why:** found the hard way, by a triple fault. The reader's test suite builds a
96-byte buffer of `]` characters, gcc vectorises the fill, and the machine dies
with `EAX = 0x5d5d5d5d` and an invalid opcode at the `movd`. The boot code puts
the CPU in long mode and stops: `CR4.OSFXSR` and `CR4.OSXMMEXCPT` are never set,
so there is no SSE state and any SSE instruction traps as `#UD`. Nothing in the
machine had needed SIMD before, so 139 checks passed on a kernel that would have
died the moment the compiler used a register it was entitled to use.

**Why not enable SSE in the boot code instead:** that is defensible and would
also work. It is three more CR4 bits, and if the compiler is ever allowed to
emit `fxsave`/`fxrstor` it is also a 512-byte FXSAVE area aligned to 64 bytes --
real state to get right, for a machine with no floating point and no SIMD to run.
The machine's rule is that the hardware is one serial port and nothing else, and
the cheapest honest way to keep that true is to build to match what the boot
code enables rather than the other way round.

**Cost:** a loop that gcc would have widened stays narrow, and the flag is
cc-specific. `-march=native` in a future build would bring the same fault back,
which is why this is written down rather than left in a Makefile as a mystery.

---

## 17. The reader collects items and folds them right-nested

**Decided:** `gb_parse` keeps one array of finished nouns and one frame stack of
`(start, count)` pairs. When a `]` closes a frame, the frame's items are folded
right-nested, last one first, and the result goes back into the array where they
were. A line is one noun; more than one at the top level is a refusal with a
reason.

**Why:** Nock's brackets are right-nested -- `[1 42 7]` is `[1 [42 7]]` -- and
that cannot be built by consing onto an accumulator as the characters arrive,
which gives `[[1 42] 7]`, a different noun. It cannot be built by amending the
tail as it goes either, because a noun is never rewritten once it exists: that
is the same immutability the solid-state test is about. So the items are
collected and folded backwards at the `]`, which is the only order that can be
right, and which also happens to be the clearest to read.

**Cost:** one noun per item, on the stack, bounded by `GB_PARSE_MAX_ITEMS` (128,
one per character at worst) and `GB_PARSE_MAX_DEPTH` (32 open brackets). Both
are refused with a reason rather than run off the end. A formula is not a thing
a person types with 32 brackets, and a 129-character line is not either.

Those two numbers were sized for the only writer there was, a person, and item
21 is what moved them: a compiler writes these lines now, so the limits are the
compiler's. The reasoning above is unchanged by that, and the cost is unchanged
too -- still one noun per item on the stack, still refused by name. Only the
number that was a guess about people turned out to be a real ceiling.

---

## 18. The machine's verdict is QEMU's exit status

**Decided:** the guest writes to QEMU's `isa-debug-exit` port, which turns a code
into an exit status of `(code << 1) | 1`. So status 1 is a clean halt and status
3 is a machine that found a failing check. `make test` reads both, and `make run`
treats 1 as success and anything else as a failure.

**Why:** it is the only way this machine has to say something to the host, and
it should be the one it uses rather than a grep over the serial log. `make test`
was the reason this exists -- a test that only fails if someone reads the output
is a test that gets skipped.

**Cost:** the codes are QEMU's convention, not ours, so they are one step removed
and easy to misread. The `run` target had it wrong in the other direction for a
while: it passed QEMU's status straight through to make, so Ctrl-D -- the
documented way to leave -- always reported a build error. Both directions of
that mistake are invisible unless you pipe input into `make run`, which is
exactly what a person does when they are not at a terminal.

---

## 19. The session is one noun, and its addresses are arithmetic

**Decided:** the session is `[log last count]`, a three-element list, so it is
five atoms deep and its parts are at `/2`, `/6` and `/14`. A line is run on
`[line session]`, which puts the session at `/3`; reaching a part *of* it means
putting one tail-step in front of the session's own path, so the log is at `/6`,
the last answer at `/14` and the count at `/30`. The book is a single formula,
`new = #[2 newlog #[6 answer #[14 newcount [0 0 0 0]]]]`, and the session is
replaced rather than amended.

**Why:** the book has to be a formula, because a C loop deciding what a session
means would be the machine deciding something, and this machine's whole argument
is that it does not. That forces two things that are easy to get wrong and
invisible when you get them wrong. First, Nock has no cons: every noun in the
step is a canned all-atoms template with its parts edited in, and an edit at
axis 1 is not a cons but a replacement of the whole noun, so `#[1 x T]` is `x`
whatever `T` was. The first version of this derivation wrote every edit at axis
1 as though it were a cons and the book answered with the line it had been
given. Second, addresses are read from the right, so the parts of a list are at
2, 6, 14 and not at 2, 3, 4; and an address is not something you multiply by
three to move into a containing noun, because a noun sitting at an odd axis has
its path pushed along rather than its number. `/5`, `/13` and `/29` are the
tempting wrong answers, and each gives a different noun rather than an error.

**Cost:** the numbers are in `kernel/book.c` next to the derivation, and pinned
by tests, because they are the part of this file that is easy to write wrongly
and hard to see. What it buys is that a line can read its own history from
inside a formula: `[0 14 0]` answers how many lines have run, `[0 6 0]` the last
answer, `[0 8 0]` the line before it and `[0 18 0]` that line's answer. None of
that is C.

---

## 20. A line that fails leaves the session exactly as it was

**Decided:** the book builds a new noun and never edits the old one, so a line
that crashes, or that runs out of steps, changes nothing and says which of the
two it was. Both reasons are told apart in the message: a formula that gives up
is not a formula that broke. The count does not move, and the next line runs on
the session as it was.

**Why:** a machine that lost its history to a typo would be a machine worth
distrusting, and a session is the one thing here that cannot be rebuilt. It is
also the cheapest possible guarantee: nothing has to be undone, because nothing
was done. The two failure modes being separate news matters more than it looks --
`nock_run` returns a distinct code for them, and a reader who was told "it
crashed" about a formula that merely took too long would be told something
false.

**Cost:** the step limit is ten million, far more than any hand-typed formula
needs, so the test lowers it for one line and hands it back. A real session that
runs out of steps is therefore only reachable by a formula built in C, which is
a Step 3 question. The step limit itself is not a property of the book: it is
the interpreter's, and it was tested there first.

---

## 21. The bridge is the reader, and the reader's limits were a person's limits

**Decided:** a host program compiles Hoon into a Nock formula and writes it to
the guest as bracket text, which the guest's existing parser reads and the book
runs. No protocol, no new guest code, no framing, no socket. What did change is
the reader's limits: the line went from 128 characters to 4096, the item
ceiling from 128 to 4096, and the bracket depth from 32 to 256, with the
parser's arrays left on the stack, which the 1MB one makes affordable.

**Why:** this answers the transport question `state.md` had left open, and the
answer is the option it had doubted. It doubted text framing on the grounds that
"a real Hoon program will not survive it", which is true of the *length* and
nothing else: a compiled core is a noun, and a noun is what the reader reads.
The programme's own reply is to make the reader's job someone else's. The size
is real and it is now a stated ceiling rather than a guess -- see the cost.

**Cost:** 4096 characters is the whole budget, and a compiled expression spends
it fast, because every atom and every axis in a Nock formula is spelled out in
brackets. Anything larger needs the jammer the earlier question leaned towards,
and the jammer is the honest next step rather than a bigger number: a limit
raised again and again is a protocol being reinvented badly. A 4096-character
line costs about 4100 noun cells out of 8,317,184, so the reader's ceiling is
the binding constraint, not the machine's. The depth limit of 256 is the one
that will bite first in practice, since every `=+` costs several levels. Until
the jammer exists the ceiling is 4096 and the failure is a refusal by name
rather than a scribble, which is what the limits have always been for.

---

## 22. The language is Hoon-shaped, and the shift is not arithmetic

**Decided:** the host compiler reads a small language that borrows Hoon's
spelling and none of its type system. Ten forms, and each one is a named opcode
or a refusal:

| Written | Is |
|---|---|
| `42` | the atom 42 (opcode 1) |
| `/14` | the subject at that tree address (opcode 0) |
| `?(a)` | 0 if `a` is a cell, 1 if it is an atom (opcode 3) |
| `=(a b)` | 0 if the nouns are the same, 1 if not (opcode 5) |
| `~(c t e)` | `t` if `c` is 0, `e` if `c` is 1 (opcode 6) |
| `*(a b)` | call: `a`'s value is the subject, `b`'s value is the formula (opcode 2) |
| `\|(a b)` | the three-word list `[a b 0]` (two opcode-10 edits) |
| `+(a)` | `a` plus one (opcode 8, then opcode 4) |
| `+(a b)` | `a` plus `b`, and only when both are literals |
| `=+(a body)` | push `a` on the front and run `body` there (opcode 8) |

Addresses are checked against the subject the expression will be run on, and an
address that is not in that shape is refused by name. `+(a b)` is refused for
anything but two literals, and `=-` is refused outright.

**Why:** three things in this project are worth a language of their own rather
than C, and each one earned it. *Cells* are the first: Nock cannot cons, so a
cell is two edits into a canned template of zeros, and `book.c` already does
this at addresses 2 and 6. The compiler does the same at the same addresses, and
the tests check the two files still agree, which is the only way either of them
can be wrong quietly. *Addresses* are the second: a wrong address on this
machine usually names a real noun instead of crashing, which is the worst kind
of bug to have, so the shape is part of the type system and `/8` inside a `=+`
is a refusal rather than a plausible answer. *Arithmetic* is the third, and it
is the one that decided the shape of everything: `+(2 3)` folds on the host
because folding is provable by inspection, and `+(/14 1)` does not compile at
all, because the twenty native integer operations are a tested bank and not
proven jets (item 9). A compiler that quietly emitted a native call there would
be a compiler whose answers nothing has ever checked. `=(a b)` has the same gap
behind it and is *not* refused, because equality is opcode 5 and needs no core
-- and that difference is the whole line this language is drawn along.

The shift under `=+` is the part most likely to be got wrong, and it is worth
writing down as its own fact because the wrong version looks like arithmetic. A
tree address is a leading 1 and then a path, so a push on the front of the
subject puts one more step in front of every path inside it: `/2` becomes `/6`,
`/6` becomes `/14`, `/14` becomes `/30` and `/30` becomes `/62`. Those five are
also exactly what `2a + 2` gives, and `2a + 2` is *wrong* for every other
address -- `/8` goes to `/24` and not `/18`, `/18` goes to `/50` and not `/38`,
and the whole old subject goes to `/3` and not `/2`. The three that work are the
three `book.c` names, so the rule agrees with the constants everywhere anyone
had already looked. The compiler writes the shift out instead of computing it,
and the `/24` case in its tests exists because that is the test that caught it.

**Also:** `*(a b)` is not Hoon's `*`, and the difference is not cosmetic. This
machine's opcode 2 evaluates *both* arguments in the outer subject and then
runs the second one's **value** on the first one's value, so `b` is not a
formula written down but an expression whose result is one. In a language where
almost everything is a value, a call is therefore usually written with `|`,
which is the one form here that can make a formula out of two atoms: `*(|(1 3)
|(0 2))` makes the subject `[1 3 0]`, makes the formula `[0 2 0]`, and answers
1. That is uglier than Hoon's spelling, and it is what the opcode means.

**Cost:** the language is a bridge, not a port, and three things are visibly
missing. There is no loop, so there is no way to say `=+(a ~(c =+(a b) 0))`
yet -- which is also the shape every Hoon recursion takes, and much of the reason the
compiler is 605 lines rather than 40. `+(a b)` for run-time values is missing on
purpose, so arithmetic on a line is a Step 4 question that needs the native
operations proven first. And `*` costs an extra `|` per call for the reason
above, which is the first place where this language is worse to write than the
one it borrows from.