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
| **1. Lamp** | It boots, and it counts. | the twenty shortcuts work | **here, red** |
| **2. Guest Book** | You type at it, it answers, and it remembers everything you did this session. | 1 + 2, writing rather than mutating | not started |
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

**Red.** `make test` gives 140 checks, 37 failing, `LAMP: DARK`, checklist 7 of
9. The machine boots into long mode, reads a heap out of the PVH memory map,
and runs the noun, Nock and primitive layers — the first time any of that code
has ever executed.

Passing: the 64-bit handover, the serial line, the heap, the noun arena, all
twelve opcodes, the step-limit abort, the twenty primitives at the C level,
and the append-only check that a subject is unchanged part for part after an
evaluation that edited it.

Failing: everything else, and it is not the machine's fault.

## The 37 failures are two bugs, both in the test file

Neither the interpreter nor the noun layer is implicated. The interpreter
behaves as `decisions.md` item 7 says it should, and the tests contradict
themselves.

### Bug 1: a bare atom where a formula is required — 36 failures

Eighteen call sites build a formula whose argument is the atom `n` when the
opcode *evaluates* that argument, so evaluation reaches the atom and the
interpreter correctly reports `a formula must be a cell, but this is an atom`.
The test label in every case already states the intended formula, usually
`[0 2]`, so the fix is mechanical: wrap the atom as `f1(0, A(n))`.

Which arguments are literals and which are formulas is not a matter of taste;
it is the table in `decisions.md` item 7, and `kernel/nock.c` implements it.

| line | now | should be | why |
|---|---|---|---|
| 220 | `f1(3, A(2))` | `f1(3, f1(0, A(2)))` | opcode 3 evaluates `b` |
| 224 | `f1(4, A(2))` | `f1(4, f1(0, A(2)))` | opcode 4 evaluates `b` |
| 236 | `f2(2, A(2), f1(1, A(9)))` | `f2(2, f1(0, A(2)), f1(1, A(9)))` | opcode 2 evaluates `b` and `c` |
| 238 | `f2(2, f1(0, A(3)), f2(7, A(2), …))` | inner `A(2)` → `f1(0, A(2))` | opcode 7 evaluates `b` |
| 245, 247 | `f2(7, A(2), f1(1, A(5)))` | `A(2)` → `f1(0, A(2))` | opcode 7 evaluates `b`; the 2-against-7 pair |
| 259 | `f3(6, …, A(2), …)` | `A(2)` → `f1(1, A(2))` | the else-branch `c` is evaluated |
| 264, 266, 268, 515 | `f2(8, A(2), …)` | `A(2)` → `f1(0, A(2))` | opcode 8 evaluates `b` |
| 276 | `f2(9, A(2), A(1))` | `A(1)` → `f1(0, A(1))` | axis is a literal, `c` is a formula |
| 282, 284, 286, 519 | `f2(10, C(A(n), f1(1, A(99))), A(1))` | `A(1)` → `f1(0, A(1))` | the pair is literal, the target `d` is a formula |
| 293, 295, 318 | `f2(11, …, A(2))` | `A(2)` → `f1(0, A(2))` | opcode 11 evaluates `d`; this is why the jet checklist item fails |
| 363 | `f2(2, A(1), A(1))` | `f2(2, f1(0, A(1)), f1(0, A(1)))` | see bug 1b |

The `solid state` failures at 523, 530 and 531 are consequences of 515 and 519:
`pushed` and `edited` crash while being built, so nothing is pushed or edited.

### Bug 1b: the runaway test contradicts its own comment — 2 failures

Line 363. The comment says the intent is `F = [2 [0 1] [0 1]]`, which evaluates
both arguments to the subject and therefore reduces to itself. Two things are
wrong. `f2(2, A(1), A(1))` is not that formula, and the call passes `trapped` as
the *formula* when `trapped` is the subject. Both need fixing together:

```
noun runaway = f2(2, f1(0, A(1)), f1(0, A(1)));   /* [2 [0 1] [0 1]] */
noun trapped = C(runaway, A(0));                  /* the subject: [F 0]    */
expect_code("a formula that reduces to itself hits the step limit",
            NOCK_STEPS_OUT, trapped, runaway);
```

The two checks after it — that the machine still works — fail only because the
error state leaks out of a crash that was never the one intended.

### Bug 2: a test that asserts both sides of a contradiction — 1 failure

```
447:    expect_prim("+div", 5, 0, 0);      /* expects a value      */
492:    expect_prim_crash("+div", 1, 0);    /* expects a crash      */
```

Division by zero is defined to crash (`kernel/primitives.c:50`) and line 492
says so. Line 447 asks for the same operation to return `0`. Line 447 is the
error; delete it.

### One test that passes for the wrong reason

Line 348, `expect_code("cannot increment a cell", NOCK_CRASH, s_cell, f1(4, A(2)))`,
crashes — but on the malformed formula, not on incrementing a cell. After bug 1
is fixed it should be `f1(4, f1(0, A(1)))`, which increments the head of
`s_cell`, and that head is a cell. Worth fixing, not worth failing over.

## How to finish Step 1

1. Apply the table above. Do it as edits to `tests/nock-tests.c` only — if a fix
   needs a change in `kernel/`, stop and argue, because that would mean the
   interpreter and `decisions.md` disagree.
2. `make test`. Expect 140 checks, 0 failing, `LAMP: LIT`, checklist 9 of 9.
3. If any test still fails, the same rule applies as at the time of writing:
   the label and the comment beside a test are the specification. Where a label
   and a comment disagree, the comment wins, because labels were written in
   bulk and comments were written beside the derivation.
4. Reconcile the documentation, which still describes the machine this was
   before the PVH change: `decisions.md` items 4 and the README both say
   multiboot 2, and the checklist line in `kernel/main.c` still reads "heap
   taken from the multiboot memory map". Rewrite item 4 as a record of the PVH
   decision and what it cost.
5. Commit. Step 1 is then genuinely finished, and Step 2 can be specified.

## The questions waiting on the bridge

Asked before the power went out, never answered. They are Step 3 questions, so
they need not be answered to finish Step 1.

1. **Finish Step 1 first, or design the bridge now?** The recommendation was
   and remains: finish Step 1. A red machine is a bad foundation to design on.
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
