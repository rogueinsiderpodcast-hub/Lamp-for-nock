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
image is loaded by QEMU's multiboot loader and the boot code zeroes its own
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

## 4. Multiboot 2 rather than Multiboot 1

**Decided:** Multiboot 2, memory map tag only.

**Why:** Multiboot 1's memory map entries are structs with compiler-dependent
padding, and the size field is ambiguous about how much padding is included.
Multiboot 2's tags are fixed-size and self-describing: 8 bytes of tag header,
then 24 bytes per map entry. The fields are read one byte at a time, so there
is nothing left to a compiler's idea of alignment.

**Cost:** a tag-walking loop, about 40 lines. Multiboot 1 would have been
shorter and would have been a latent bug.

**Not proven:** only that QEMU's map parses. Nothing depends on the rest of the
multiboot interface, and nothing ever will: the machine has no modules, no
console, no framebuffer and no command line.

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

**Evidence:** the rule `*[a 0 b] = /[b a]` uses `b` directly, and opcode 11's
own expansion is written `[... 0 3]`, both of which are only meaningful if
address 0's argument is a literal. docs.urbit.org and `vere/doc/spec/nock/4.txt`
agree. Same reasoning for opcode 9's address.

**Proven:** the test suite, against both readings of the argument convention.

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

**Not proven:** that any of these twenty is *equivalent to a Nock formula*. In
Urbit these are jets: each has a Nock definition in the standard library, and a
jet is only legitimate because it computes the same thing its Nock definition
computes. Step 1 has no Hoon compiler and no standard library, so there is no
Nock definition to compare against. Proving that equivalence, against the real
formulas, is the first job of the next step. Until then they are a tested native
bank, not proven jets.

---

## 10. Step 1 jets dispatch from a dynamic hint, using our own convention

**Decided:** opcode 11's dynamic hint is the hook:

```
[11 [<atom: primitive index> <formula: argument>] <the real formula>]
```

The interpreter evaluates the argument formula, hands the result to the native
primitive, and throws the primitive's answer away. Every primitive announces
itself and its result on the serial line.

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
evaluation's answer is bit-for-bit the same with hooks enabled and disabled.

**Not proven:** equivalence to any Nock definition — see item 9.

---

## 11. No host-side Nock implementation to check against

**Decided:** Step 1's ground truth is the written rules plus hand-derived
expected values, not a differential test against another interpreter.

**Why:** every Nock implementation found is at least a decade old C++ with a
runtime, a compiler and a build system far outside this project's scope. Pulling
one in to cross-check would add more unaudited code than the interpreter itself.

**Consequence, stated plainly:** if the reading of the rules in items 6 and 7 is
wrong, this machine is wrong in the same way, and its tests will not notice,
because the tests were written from the same reading. The mitigations are that
the reading comes from two independent sources that agree, that the two
ambiguous rules are flagged here, and that verifying against `vere` is a small,
well-defined task for the next step.

---

## 12. Scope: what is deliberately not here

No disk, no filesystem, no persistence across power cycles, no network, no input
device, no Hoon compiler, no Hoon standard library, no scheduler, no processes,
no memory protection, no GPU, no framebuffer, no audio, no Bluetooth, no package
manager, no remote update path, no user accounts, no clock.

The machine boots, evaluates Nock, tests itself, prints the result and halts.
That is Step 1 in full.
