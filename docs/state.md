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
| **2. Guest Book** | You type at it, it answers, and it remembers everything you did this session. | 1 + 2, writing rather than mutating | **done — green** |
| **3. Teacher** | You write instructions in a real language, and they run. | 3, code is data | **green — a Hoon-shaped language, no loops yet** |
| **4. Notebook** | The guest book survives the power being turned off. | durability | not started |
| **5. New Rules** | The machine rewrites its own behaviour from text you send it, and cannot be broken by it. | 5, it cannot be lied to | not started |
| ~~Wire~~ | ~~Networking~~ | | deferred indefinitely |

**Step 3 is the bridge.** The compiler stays on the host: you type into
`tools/hoon.c`, a program on the workstation compiles it, and the resulting
noun is sent into the guest as data. You get the experience of Urbit without
porting its compiler to bare metal. The handover is the serial reader and
nothing else — no protocol, no framing, no new guest code, and no boot-parameter
trick. What exists now is a Hoon-*shaped* language rather than Hoon itself: ten
forms, addresses checked against the subject the expression will run on, and
refusals by name for everything else. See `decisions.md` item 22 and "Where Step
3 stands" below.

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

### A bug in Step 1 that eleven steps of writing did not find

Found by going back over Step 1 to look for problems rather than to add
anything, and worth setting down because of what it says about the rest.

The jet test built its hint's argument as the *list* `[2 [3 0]]` while the
interpreter read a *pair* `[2 3]`, so the native was handed `+add(2, 4) = 6`
and answered with it on the serial line, one line under a comment saying the
test was about `+add(2, 3) = 5`. It passed. The test checked that a primitive
ran, that the answer was unchanged, and not one thing about the numbers that
went in or came out.

What made it survivable is the shape of the assertion, not the arithmetic: every
other check in the suite asks for a value, and that one asked for an *event*. A
jet reading the right answer out of the wrong operand, or the wrong answer out
of the right operand, satisfies every check that only looks at the answer —
which is all the checks that looked. Two rules came out of it: a hint is jetted
only when its argument is shaped like operands, and a native that stops backs
the hint out rather than taking the machine down over a claim that turned out
to be wrong. Both are in `decisions.md` item 10, and removing either one now
fails a check.

The lesson carried forward is in the twenty-natives work: a suite that counts
events and a suite that checks values are not the same suite, and the second is
what catches a machine that computes the wrong thing correctly.

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

## Where Step 2 stands

**Done.** The self-test is 262 checks, 0 failing, `LAMP: LIT`, checklist 11 of 11
-- the eleventh being the new one, that a formula typed at the machine runs and
what it leaves behind matters.  It was checked by breaking the count increment
and watching the lamp go dark, because a checklist item that cannot fail is a
sentence in a list rather than a check.
A line of text typed at the machine comes back as the noun it is, a line that is
not a noun is refused with a reason, and a line that is a formula is *run* -- on
the session the last line left behind, and the answer goes into the history.

```
> [1 42 0]
  42  (1 so far)
> [1 7 0]
  7  (2 so far)
> [0 14 0]
  2  (3 so far)
> [1 2]
  it crashed: formula is missing arguments
  the session is as it was.
> [0 8 0]
  [0 [14 0]]  (4 so far)
>
  the session, newest first:
    [0 [8 0]] answered [0 [14 0]]
    [0 [14 0]] answered 2
    [1 [7 0]] answered 7
    [1 [42 0]] answered 42
  4 entries, last answer [0 [14 0]]
```

A blank line shows the session, which is the only place C walks it: Nock has no
loop, so printing a list of unknown length is C's job and nothing else is. The
session is a noun and the book is one formula (`kernel/book.c`), with the whole
derivation in comments next to it, because the derivation is the part worth
reading twice. Both halves are tested: 49 checks for the reader, and the book's
own group pins the address arithmetic, the shape of a session, two lines
chained, a line reading its own history, a line that breaks, and a line that
runs out of steps.

The addresses are the cost of doing it properly, and they are in `decisions.md`
item 19. Briefly: a session's parts are at `/2` (the log), `/6` (the last answer),
`/8` (the newest line), `/14` (the count) and `/18` (that line's answer); the
line is run on `[line session]`, so the session is at `/3` and its three
book-named parts are at `/6`, `/14` and `/30`. Every one of those numbers is a
place a plausible guess is silently wrong, and there were five of them rather
than three: `/8` and `/18` were missing from the written list until the
compiler asked for them, and a line that pushes on the front found `/8` missing
from the pushed shape too — see item 22, where the shift that "should" be
`2a + 2` and is not nearly cost an afternoon.

### The four questions, answered

1. **What is a session, concretely?** A session is `[log last count]`. The log
   is newest-first, each entry `[line answer]`, and `count` is the number of
   entries, which makes a blank line's "how long have we been going" answer a
   noun rather than a counter in C. One answer here changed while building it.
   The plan was for `last` to be the most recent *subject*, with the next line
   evaluated against the state the last one produced; what is built instead runs
   every line on the whole previous session and keeps the last *answer*, which
   is what makes the count and the log readable from inside a formula at all.
   The plan would have made the history reachable only from C.
2. **What does typing look like?** Settled by measurement, not taste: printable
   ASCII, enter submits a whole line, backspace deletes, Ctrl-D leaves. A line is
   one noun and is submitted on enter rather than evaluated as it is typed,
   because a half-typed `[6 [3 [0 ` is not a thing to evaluate. The grammar is
   decimal atoms and right-nested bracketed lists, in `decisions.md` item 17.
3. **What does writing instead of mutating change here?** Confirmed nearly free,
   and the parser is the proof: it cannot amend the tail of a noun as it reads,
   so it collects the items and folds them right-nested when the bracket closes.
   A 39-character formula costs 38 new cells and rewrites nothing. The cost of
   immutability here is that items have to be held until their order is known.
4. **How much fits?** The arena is half the heap at 16 bytes a cell, and the
   machine now says so: 8,317,184 cells with QEMU's 256MB, 406 used by the
   self-test. A line costs about 3 cells at one character and about 4100 at a
   full 4096-character line, so a session runs to somewhere between tens of
   thousands and a couple of million entries -- and when the arena is full,
   `noun_cons` crashes rather than reusing, which is Step 4's problem to solve
   and not something to discover at. The reader's line limit, not the arena, is
   what a session of compiled formulas will run into first: at 4096 characters
   it spends 4100 cells of 8,317,184, and the depth limit of 256 open brackets
   is the one reached first in practice.

## Where Step 3 stands

**Green, for a language that is ten forms long.** `tools/hoon.c` is 605 lines of
host C, it links the machine's own `noun.c`, `nock.c`, `primitives.c`,
`book.c` and `guestbook.c` rather than a copy of them, and it has two suites:
`make hoontest` is 40 checks on the host, where the formulas are run by the
machine's own interpreter and the answers come from the machine's own book, and
`make teach` is the bridge end to end — the host compiles four expressions, the
text goes down the serial line as characters, the guest's reader reads it, the
guest's interpreter runs it, and the answers come back.

```
> [8 [[0 [14 0]] [[6 [[0 [2 0]] [[1 [1 0]] [[1 [2 0]] 0]]]] 0]]]
  1  (1 so far)
> [8 [[0 [14 0]] [[6 [[0 [2 0]] [[1 [1 0]] [[1 [2 0]] 0]]]] 0]]]
  2  (2 so far)
> [2 [[10 [[6 [1 [3 0]]] ...] [[10 [[6 [1 [2 0]]] ...] 0]]]
  1  (3 so far)
> [8 [[0 [14 0]] [[4 [[0 [2 0]] 0]] 0]]]
  4  (4 so far)
```

Those four answers are one claim, not four. The second answering 2 means the
count read at `/14` was 1, so the first line was typed, read, run and
remembered; the fourth answering 4 means the count was 3, so all three before it
were. The text going down that line is a single `=` + three nested opcode-10
edits, and it is a `4096`-character budget spent in 70.

The language, and the three things that shaped it, are in `decisions.md` item 22.
Briefly: a cell is two edits into a template of zeros because Nock cannot cons,
so `|(a b)` is `[a b 0]` and not `[a b]` — the reader folds a bracket
right-nested and therefore has no spelling at all for a cell whose tail is an
atom; an address is checked against the subject the expression will be run on,
so `/8` inside a `=+` is a refusal rather than a plausible answer; and `+(a b)`
folds on the host for two literals and refuses everything else, because the
twenty native integer operations are a tested bank and not proven jets.

### What the compiler found about the machine

The interesting part of this step was not writing the compiler, it was the four
things the compiler got wrong first. Three were caught by the tests. The fourth
was caught by reading a refusal, and it is the one worth remembering.

**The shift under `=+` is not `2a + 2`.** An address is a leading 1 and then a
path, so a push on the front puts one more step in front of every path inside
the old subject: `/2` → `/6`, `/6` → `/14`, `/14` → `/30`, `/30` → `/62`. The
compiler's first version computed `2a + 2`, which agrees on all five of those
and is wrong everywhere else — `/8` → `/24` and not `/18`, `/18` → `/50` and not
`/38`, the whole old subject → `/3` and not `/2`. It agreed because the three
addresses `book.c` names are the only ones anyone had written down. The test
`=+(/14 /24)` is in the suite because that is the case that caught it, and
`axis_shifted` writes the shift out rather than computing it, with a comment
saying why the arithmetic-looking version is the dangerous one.

**The axis check was in the wrong place.** It lived in the parser, which reads
the *body* of a `=+` before anything has been pushed on — so `/8` inside a
push was being checked against the subject outside it, and was accepted, because
`/8` is a real address of the session. It moved to `emit`, where the shape is
the one the formula will be run on. A compiler that knows what the subject is
has to know it at the point of emission, not at the point of reading.

**Opcode 2 is not Hoon's `*`.** This machine's opcode 2 evaluates *both*
arguments in the outer subject and then runs the second one's value on the
first one's value, so `b` is an expression whose *result* is a formula. In a
language where nearly everything is a value, that means a call is usually
written `*(|(1 3) |(0 2))`: make the subject `[1 3 0]`, make the formula
`[0 2 0]`, answer 1. The first version of that test, `*(|(1 3) /2)`, was not a
bug in the compiler at all — it was a wrong guess about the opcode, and the
interpreter's own complaint (`a formula must be a cell, but this is an atom`) is
what showed it. `nock.c` has a comment on the difference from opcode 8, which
does not evaluate its second argument; that comment is the reason the two are
not confused again here.

**A shape with a hole in it, and a test that passed anyway.** A push puts two
entries at the front of the shape — the value at `/2` and the old subject at
`/3` — so every existing entry has to move up by two. The first version moved
them up by *one*, which quietly lost the `/6`, the log, from the middle of the
table, and nothing failed: a missing address is a hole, not a crash, so the
addresses that were asked for still compiled and the ones that were not were
still refused. The only sign was a refusal that listed one address too few, and
nobody reads a refusal message to check off an address list.

The test suite did not catch it because the suite checked that refusals
happened, not what they said. Both refusal checks now compare the *whole* list
the message prints — `/2 /3 /6 /14 /24 /30 /50` for the pushed subject, `/2 /6
/8 /14 /18` for the session — which is the only place the compiler's idea of
the subject is ever written down, and therefore the only place worth asserting.
The second version of the fix, copying forwards instead of backwards, was wrong
in the other direction and duplicated three entries; both directions are now
described in `axis_shifted` and `shape_push` so the next reader does not
rediscover them.

### What is not here

No loops, so no Hoon recursion: `=+(a ~(c =+(a b) 0))` is the shape every
recursion takes and it cannot be written. No run-time addition, on purpose. No
types beyond "an address the session has" and "a value the machine can put in a
noun", no user-defined cores, and nothing that could grow past a line of 4096
characters.

The next step is not more of this language; it is the native operations proven
against their Nock definitions, which is what unblocks `+(a b)`. Work on that
started, and it turned the next step into a smaller question than it looked like.

**Two of the twenty are proved.** `make proofs` carries the Nock definitions of
`+inc` and `+eq`, runs each through the machine's own reader, printer and
interpreter, and requires it to agree with the native over 1,412 inputs --
including on which inputs stop, since a native that answers where its definition
would have stopped is the one way to make a wrong machine faster.

**The other eighteen are blocked, and the reason is worth having.** The blocker
is not arithmetic. It is that a loop in Nock is a core that calls its own arm,
which is a noun containing itself, which in Hoon is written with a *name*:
`=+(a b)`, where `a` names the arm being written. This language has no names, so
it cannot write a core that refers to itself, so it cannot write any definition
that iterates -- and `+add`, the four comparisons, `+div`, `+mul` and the six
bitwise and shift operations are all 63-step loops. Unrolling 63 steps is longer
than a line of input and proves nothing extra.

So the next step is **names**, not proofs, and the evidence for that is the
three that did get proved: `+inc`, `+eq` and `+not` are exactly the three whose
Nock definition is a fixed number of opcodes with no iteration and no bit reads
in it. Unrolling was tried as a way around this and does not work: an unrolled
63-bit carry chain still has to read a bit at each step, and reading a bit needs
a comparison, and a comparison is a borrow chain that has to read bits. The
tower is circular rather than merely long.

One of the three is proved because a bug was fixed rather than because the
definition was found. `+not` was `a ^ NOUN_ATOM_MAX` -- a 63-bit complement
wearing the name of a logical not, so a program written against Urbit's `!.`
would have got back a number that was not the answer to anything. Making the
body match the name is what gave it a definition at all: the complement needs
63 bit positions and a loop, and the logical not is equality and a conditional. Every native is now named in one of
two tables in `tools/jet-proofs.c` -- proved, or pending with the reason it is
pending -- and a primitive in neither is a failure, so this gap cannot quietly
grow back open.

What this does *not* do: it does not unblock `+(a b)`. That still needs `+add`,
and `+add` needs a loop.

## The questions waiting on the bridge

Asked before the power went out, when the bridge was still a question. All three
are answered now, and the answers are in Step 3's own words below. They were
Step 3 questions, so they never needed answering to finish Step 1 -- but leaving
them open in the state file while the decision log had answered them is how a
later reader ends up arguing with a decision that was already made.

1. **Finish Step 1 first, or design the bridge now?** Answered, implicitly:
   finish Step 1. A red machine is a bad foundation to design on, and this one
   turned out to have interpreter bugs in it.
2. **Transport.** Answered, in `decisions.md` item 21, and it is (a) — the
   serial text the reader already reads, with no new protocol, no framing and no
   new guest code. The doubt recorded here was that "a real Hoon program will
   not survive text framing", which is true of the *length* and of nothing else:
   a compiled core is a noun, and a noun is what the reader reads. The reply to
   a length problem is to make the reader's job someone else's, so the limits
   moved from a person's (128 characters, 32 open brackets) to a compiler's
   (4096, 256), and they now live in `kernel.h` as part of the reader's
   contract, with the tests building their inputs from the numbers rather than
   hard-coding some that rot. (b) is not dismissed, it is next: a jammer is what
   comes after the ceiling is reached, and a limit raised again and again is a
   protocol reinvented badly. (c) is still out; a socket is a device.
3. **Direction.** Answered: one-way, as the leaning said. Hoon is typed on the
   host, compiled on the host, and the nouns flow in one at a time, with answers
   coming back over the serial line. Two-way, where the guest can call out to the
   host, is a real problem but it belongs to running a real `vere`, which is a
   much later thing than this.

## The kill list, still in force

GPU · display · mouse · keyboard drivers · USB · disk drivers · filesystems ·
networking · sound · Bluetooth · Wi-Fi · fonts · animation · windowing · package
manager · systemd · any login or user system · any inherited driver · multi-user
· persistence of anything except the guest book.

If it is not on that list and the current step does not need it, it is out. The
list is the reason the project stays small enough to hold in one head.
