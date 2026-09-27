# State

Where Lamp actually is, written down so that a power cut, a lost session or a
fresh pair of eyes costs a paragraph rather than a day. The design decisions live
in `decisions.md`; this file is the state of play, and it is expected to be
rewritten every time the state changes.

## The machine this is a piece of

Lamp is Step 1 of five. Each step adds exactly one idea, and stopping after any
of them still leaves something real.

| Step | One-sentence goal | New idea | State |
|---|---|---|---|
| **1. Lamp** | It boots, and it counts. | the twenty shortcuts work | **done — green** |
| **2. Guest Book** | You type at it, it answers, and it remembers everything you did this session. | 1 + 2, writing rather than mutating | next |
| **3. Teacher** | You write instructions in a real language, and they run. | 3, code is data | not started |
| **4. Notebook** | The guest book survives the power being turned off. | durability | not started |
| **5. New Rules** | The machine rewrites its own behaviour from text you send it, and cannot be broken by it. | 5, it cannot be lied to | not started |
| ~~Wire~~ | ~~Networking~~ | | deferred indefinitely |

**Step 3 is the bridge.** The Hoon compiler stays on the host: you type Hoon,
a helper process on the workstation compiles it, and the resulting noun is sent
into the guest as data. You get the experience of Urbit without porting its
compiler to bare metal. Nothing of this exists yet — there is no host-side
program, no `tools/`, and no serial protocol. The only host-to-guest handover
that exists at all is QEMU's boot-parameter pointer.

Note for later: `vere`/Arvo is a userspace process, not a kernel. The realistic
endgame is Lamp as a scaffold *around* a real Urbit on the host, not Urbit
replacing the host.

## Where Step 1 stands

**Green.** `make test` gives 139 checks, 0 failing, `LAMP: LIT`, checklist 9 of
9. The machine boots into long mode, reads a heap out of the PVH memory map,
and runs the noun, Nock and primitive layers. Step 1 is finished: the twenty
shortcuts are the whole of the trust base, and everything later stands on them.

The count is 139 rather than 140 because one test was deleted rather than fixed
— see bug 3 below. It asserted both sides of a contradiction, so there was
nothing there to repair.

## How the red machine became green

The first run of the self-test was 140 checks and 37 failing. Getting to zero
took five distinct bugs, of which **two were in the interpreter** and **one was
in the test harness itself**.

### The wrong turn, kept because it is instructive

The previous version of this file diagnosed all 37 failures as test-file bugs
and said, flatly, "neither the interpreter nor the noun layer is implicated."
That was wrong, and it was wrong in a way worth remembering: every visible
failure was explained by a defect already found in the test file, so the
explanation fitted all the evidence without needing to be true.

What gave it away was that the fixes were checked in `kernel/` at all. The
standing rule in this project is that a fix needing a change in `kernel/` means
the interpreter and `decisions.md` disagree, and that has to be argued rather
than assumed. The rule fired. It should not have been overridden by the sheer
number of tests that happened to agree with the wrong answer.

The lesson generalises: a test suite that is red in large numbers usually has a
few real bugs and a cascade, not a few dozen real bugs. Count the *kinds* of
failure before counting them.

### Bug 1: `arg()` never advanced — 8 opcodes broken

`arg()` in `kernel/nock.c` read its argument and then fell out of the loop
without incrementing its index, so every opcode that takes more than one
argument — 2, 5, 6, 7, 8, 9, 10, 11 — got the *same* value for each of them.

This was the root cause of most of the red. It was invisible to any test that
passed the same atom in two positions, and a bare `i--;` fixed it. A test suite
of 139 checks did not catch it because the suite was written to exercise the
shape of each opcode, not each argument independently; the opcode-6 branch tests
came closest, and only once their formulas were corrected.

### Bug 2: opcode 8 evaluated `c` instead of using it

`*[a 8 b c] = *[[*[a b] a] c]`. `b` is evaluated to get the new subject, and
`c` *is* the formula. The implementation evaluated `c` and then ran its product,
which is the opcode-2 reading of `c` and is wrong here.

This is precisely the "literal or formula" confusion in `decisions.md` item 7,
but in the position nobody re-read: both arguments are formulas, yet `b` is
*reduced* and `c` is *used*. It was found by fetching
`vere/doc/spec/nock/4.txt` and putting the three critical expansions —
opcodes 2, 7 and 8 — beside the code. Not by reading the tests, which the
implementation satisfied.

### Bug 3: the harness leaked its own crashes into later tests

`noun_cons()` returns `0` while `machine_err` is set, which is correct: after a
crash the machine has no state worth building in. But a formula in the test
suite is built as a C *call argument*, so it is constructed before `nock_run()`
gets a chance to reset the error state. Every test after a crash therefore
received `0` in place of its formula and failed with `a formula must be a cell,
but this is an atom` — a true statement about a noun that should never have
existed.

This accounted for 29 of the original 37 failures. Three places needed the
reset, and one of them is the obvious one people miss: `expect_code()` resets
when a crash is *expected and the check passes*, not only when one is expected
and the check fails. `expect_prim_crash()` likewise has to clean up after a
crash that was supposed to happen.

### Bug 4: bare atoms where formulas were required

Eighteen call sites passed the atom `n` as an argument that the opcode
*evaluates*, so evaluation reached an atom and correctly reported that a formula
must be a cell. The test label in nearly every case already stated the intended
formula, usually `[0 2]`, so these were mechanical: wrap it as `f1(0, A(n))`.
Which arguments are literals and which are formulas is not a matter of taste —
it is the table in `decisions.md` item 7.

### Bug 5: expected values and labels that did not say what they meant

Four of these, all found only once the cascade was gone and individual failures
could be read:

- **The runaway test contradicted its own comment.** `F = [2 [0 1] [0 1]]`
  evaluates both arguments to the subject, so `nock(S, F)` is `nock(S, S)`. The
  test wrapped `F` in a subject `[F 0]`, which is a different noun, and passed
  the *subject* where the *formula* went. The simplification is that the subject
  and the formula should both be `F`: then `nock(F, F)` is `nock(F, F)` again,
  forever, with nothing to unwind.
- **One test asserted both sides of a contradiction.** `expect_prim("+div", 5,
  0, 0)` demanded a value for a division by zero that `kernel/primitives.c`
  defines as a crash, and `expect_prim_crash("+div", 1, 0)` demanded the same.
  Deleted, not repaired.
- **One test passed for the wrong reason.** "cannot increment a cell" was
  crashing on its own malformed formula rather than on the cell. Rewritten to
  increment the head of a subject whose head is a cell.
- **`d2()` and `d3()` are list builders, not cell builders.** They produce
  `[a [b 0]]` and `[a [b [c 0]]]`, right-nested with a trailing `0`. Several
  opcode-10 expectations were written with them when the result of an edit is a
  genuine cell, and were expecting a `[99 0]` that the edit never produces.
  Notably, one of those expectations was right *by accident* for address 14 and
  wrong for the others, because address 14 sits at a depth where the two shapes
  happen to coincide.

## How to finish Step 2

Step 1 needed no argument to fix: the specification was in `decisions.md`, the
tests, and `vere/doc/spec/nock/4.txt`, and it was enough. Step 2 should be
specified before it is built, in one page, answering:

1. **What is a session, concretely?** What the machine remembers and what
   "everything you did this session" means, stated as nouns rather than prose.
2. **What does typing look like?** The character set, line editing, and whether
   a line is submitted on enter or is a formula evaluated as it is typed.
3. **What does writing instead of mutating change here?** Step 2's one new idea
   is that a noun is produced rather than edited in place. The arena is
   append-only, so this should be nearly free — confirm that, and write down what
   it costs if it is not.
4. **How much fits?** How long a session can get before the arena is a problem,
   so Step 4 is designed for rather than discovered at.

## The questions waiting on the bridge

Asked before the power went out, still unanswered. They are Step 3 questions, so
they never needed to be answered to finish Step 1.

1. **Finish Step 1 first, or design the bridge now?** Answered, implicitly:
   finish Step 1. A red machine is a bad foundation to design on, and this one
   turned out to have interpreter bugs in it.
2. **Transport.** (a) serial only, human-readable text — most consistent with
   the kill list, but a real Hoon program will not survive text framing.
   (b) serial plus binary jam/cue framing, which needs a jammer written on the
   host. (c) QEMU `-serial unix:` socket with a host process, the guest side
   still being only the UART. The leaning was (b).
3. **Direction.** One-way — you type Hoon on the host, it compiles there, nouns
   flow in, results come back over the serial line. Or two-way, where the guest
   can also call out to the host, which is what running a real `vere` would
   eventually need.

## The kill list, still in force

GPU · display · mouse · keyboard drivers · USB · disk drivers · filesystems ·
networking · sound · Bluetooth · Wi-Fi · fonts · animation · windowing · package
manager · systemd · any login or user system · any inherited driver · multi-user
· persistence of anything except the guest book.

If it is not on that list and the current step does not need it, it is out. The
list is the reason the project stays small enough to hold in one head.
