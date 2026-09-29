# State

Where Lamp actually is, written down so that a power cut, a lost session or a
fresh pair of eyes costs a paragraph rather than a day. The design decisions live
in `decisions.md`; this file is the state of play, and it is expected to be
rewritten every time the state changes.

## The machine this is a piece of

Lamp is on its seventh step. Each step adds exactly one idea, and stopping after
any of them still leaves something real.

| Step | One-sentence goal | New idea | State |
|---|---|---|---|
| **1. Lamp** | It boots, and it counts. | the twenty shortcuts work | **done — green** |
| **2. Guest Book** | You type at it, it answers, and it remembers everything you did this session. | 1 + 2, writing rather than mutating | **done — green** |
| **3. Teacher** | You write instructions in a real language, and they run. | 3, code is data | **green — a Hoon-shaped language with cores, names and a loop** |
| **4. Notebook** | The guest book survives the power being turned off. | durability | **green — the log is written down as it runs, and replayed on the next boot** |
| **5. New Rules** | The machine rewrites its own behaviour from text you send it, and cannot be broken by it. | 5, it cannot be lied to | **green — a rule is text, checked in full before it is allowed to be behaviour** |
| **6. Rule removal** | An accepted rule can be put away, and the putting away is a record. | 6, the empty definition | **green — `! 0 0` puts the rule away, echoed as the record `! 0 0: 0`, and the notebook's last word wins** |
| **7. A second rule** | A rule is not special to one primitive, and the machine has as many pages as it has rules. | the shape of a domain is part of the row | **green — `+mul` is a rule over `a * b < 128`, and the identity map covers the heap (item 29)** |
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

## Where Step 4 stands

**Green, and the green part is that a SIGKILL costs at most one line.** The
machine has no disk, no filesystem and no driver, so the only thing that can
outlive it is the wire it already has, and the notebook is a file on the host at
the other end of that wire. `make notebook` is the shell line that holds it: the
records go in before your own input, and every record the guest writes is copied
out to the file as it arrives rather than at the end, because the end may never
come.

A record is the formula that ran, a colon, and the answer it gave:

```
% [1 7 0]: 7
```

**What is written down is the log and not the session.** The session noun is the
obvious thing to store and it is the wrong one: a session is a log of every line
that ran, and eleven lines of `make teach` is already a noun well over the 4096
characters a line may hold, so a snapshot is a noun the machine's own reader
cannot read back. Replaying the log has none of that problem -- a record is a
line, and a line is what the reader was built to take -- and it is the same
operation the guest book already does on every line, so restore is not a new
code path at all. It is the ordinary loop, fed its own history. A snapshot
restored by loading the present back in would be a mutable-state machine with
extra steps, which is the thing this project is an argument against.

**A restore checks itself.** The machine is deterministic, so a replayed record
must answer what the notebook says it answered. Each one is compared with
`noun_equal` and not by comparing text, and a record that does not reproduce is
reported by name and by value and the session is left alone. That is one check
in one direction, and it is not Step 5's "cannot be lied to": an attacker who can
edit both halves of a record is not caught, and that is written down in the
README's "not verified" list rather than left implied.

`make notebook-test` is the claim, checked on every build, and it is checked the
way it would actually be lost:

1. two lines run, then SIGKILL while the guest sits at its prompt -- no clean
   exit, no isa-debug-exit, nothing the guest could have done on the way out
2. the notebook is fed to a fresh boot and nothing else, and `[0 14 0]` has to
   answer 2
3. a record that lies -- the real formula, the wrong answer -- is refused by
   name, and the count read afterwards has to be 0 rather than 1

All three were checked against sabotage rather than trusted: emptying
`gb_record` fails phase 1, and making the answer comparison a constant 0 fails
phase 3.

## Where Step 5 stands

**Green.** The machine can now rewrite part of its own behaviour from text sent
down the same wire as everything else, and it checks the text in full before it
is allowed to mean anything. The line is `! 0 <definition>`: a rule names the
primitive it claims to be, and the machine proves the claim over that
primitive's certified domain before the rule is installed, by running every pair
in the domain with its own interpreter on its own hardware and comparing each
answer with its own native. Text cannot break the machine because there is never
a step at which a rule is believed: every path into behaviour is a refusal. Item
26 is the whole design, written down before the code, and the machine's own
checklist says it on every boot.

The first rule is `+add`, because that is the one definition the machine already
carries (item 23). Its certified domain is the triangle `a + b < 64` -- 2080
pairs, the largest triangle whose exhaustive battery still settles in the arena
-- and the battery settles near 2.4 million cells, which fits the guest arena
and is far over the host's. This is a check a bare-metal machine can perform and
its own workstation cannot: the attempt ran out of arena at `+add(2, 58)`, so
`make rules-test`, like everything else, is the machine testing itself.

**The counters are the proof that the split took.** Outside the domain the
native answers, inside it the definition does, and the machine counts per
primitive how many probes each path answered. The boot checklist runs a probe
inside the domain, one outside it, and one after a removal, and asserts each by
the *deltas*: the counters are machine-lifetime, the self-test's own jets run
before any rule exists, so `native_runs` starts at 2 and the assertion is `+1`
on the path that answered rather than an absolute number. `!` alone reports the
split on demand; live, the rule answered 9 + 5 = 14 and the native 1000 + 2000
= 3000, and the report before the rule was put away said "the rule has answered
132 probes, the native 6" (Step 6's removal probe is the extra native probe that
Step 5's "native 5" became, and Step 7's `+mul` battery runs 129 `+add(2, 3)`
hints inside its self-calling case, which is the rest of the jump from 3). The
session evaluates a line's answer twice (once
for the entry, once for the new session), which is idempotent, so a jetting
line fires twice and gains two probes per line -- that is measured and said,
not hidden.

**Refusals are by name.** The ways to be wrong are each named with the pair that
caught them: a definition that lies (`+add(0, 0) = 3`), a definition that stops
(tree address 0 names no noun), and a definition that computes the answer by
hinting the very primitive it claims to be. The last one is a decline, not a
crash -- while the battery runs and while the rule is in use, a hint at the
primitive being defined declines and the definition's own fallback is judged,
because a declined probe is a normal thing in this machine, not a bug. A def
that passed the battery by calling the native through the fallback would have
had to be right on all 2080 pairs to pass, at which point hinting was pointless.
`+add` is the only primitive with a certified domain; a rule claiming to be, say,
`+mul` is refused by name before it is read, because a claim the machine does
not know how to check is a premise it declines to take.

**A rule is a record.** The same `claim: answer` shape as a `%` line, with the
domain as the answer half: `! 0 <definition>: 64`. On a later boot the notebook
restore feeds it back and the machine re-runs the battery rather than believing
the record -- restore is a check, never an act of trust -- and replay does not
echo or re-write a record, so a notebook does not double itself on every boot. A
record claiming a domain the machine does not certify (`: 65`) is refused by
name.

`make rules-test` is the claim, run from `make check`, and it is checked the way
the feature is a claim about: the definition's text in the Makefile is the same
992 characters item 23 wrote down, and the machine's own echo of the installed
rule is required to be byte-for-byte identical to it -- a drift between the two
copies is a failed build rather than a typo. The test boots with the rule typed
at it, watches `+add(9, 5) = 14` answered in-domain and `+add(1000, 2000) =
3000` out of it, reads the counters, boots again with only the notebook to go on
and requires the record to be re-verified rather than trusted, then refuses a
lying domain and a lying definition by name. The `!` grammar was deliberately
not given a host unit test -- `gb_rule` reads the machine's static line buffer,
so the coverage is the checklist's own 2080-pair battery plus this wire-level
test, which is what a claim about a running machine should be.

**What it does not do, said plainly.** No opcode changed and no arithmetic
changed; the compiler's refusal of `+(a b)` for runtime operands stands;
removal is a record and Step 6's own, below; and a bounded battery is a bounded
proof -- for `a + b < 64` the machine does not need its `+add` native, and for
everything else it still does (item 26).

## Where Step 6 stands

**Green.** A rule can be put away, and the putting away is a record. The line
`! 0 0` -- the primitive's index, then the empty definition -- takes the rule
for `+add` back down to the row it was born with, and the machine says so:
"yes.  +add is no longer a rule; the C native answers everywhere again." The
definition is discarded, never replaced, and nothing is re-checked, because a
removal claims nothing: the record's answer half is 0, the way an install's is
its certified limit. Item 27 is the design, written down before the code.

**A removal is a record because replay is the notebook's last word.** The
install was written down as a record, so the removal is written down next to
it: typed `! 0 0` is echoed `! 0 0: 0`. On a later boot the checklist installs
the certified `+add`, then the notebook's records replay in order, and the last
word on a primitive wins -- a notebook ending in an install leaves the rule in,
one ending in a removal leaves it away, exactly as a session line's records do.
The boot checklist's own install always runs before the restore, so it can
never fight a removal, and a removal record replayed therefore always has a
rule to remove.

**What is refused, by name.** `! 0 0` when the rule is already gone -- removed
moments ago, or never accepted -- is refused: "there is no rule for +add to
remove". `! 0 0: 64` is refused, because a rule of nothing claims nothing and a
removal's record is `: 0`. Both are the same instinct item 26 had: a claim the
machine cannot back up is a premise it declines to take.

**The checklist proves the direction on every boot.** After the battery and the
split, `gb_rules_ok` removes the rule, probes inside the former domain, and the
counters must show the native answering while the rule moves nothing, then
reinstalls so a boot lands where it always did. `make rules-test` proves it on
the wire, from the same boots that already proved install and replay: a jet
`+add(9, 5)` still answers 14 after removal, now by the native; the report says
"the C native answers"; a notebook replayed on a fresh boot ends with the
removal holding and a second `! 0 0` refused as "no rule to remove"; and the
removal record neither doubles itself on replay (a checked record is not
re-echoed, as with installs) nor touches the session.

## Where Step 7 stands

**Green, and the point of it is that a rule is not a feature of one primitive.**
`+mul` is now a rule like `+add`: the line is `! 2 <definition>`, the record is
`! 2 <definition>: 128`, it is put away with `! 2 0`, and every refusal Step 5
and 6 named for `+add` is named for `+mul` too, in the same words. Nothing in
the grammar, the record shape, the replay or the gate changed. The 736-character
definition is item 28's, it counts up twice and never subtracts, and its battery
runs 892 pairs and settles at 1,406,432 cells against `+add`'s 2080 pairs and
1,926,080. The machine says the sum out loud on every boot: "the two rule
batteries settled at 3352981 of 8316544 cells".

**A domain is a shape and a number, and the shape is part of the row.** `+add`
is bounded by the sum of its operands, `+mul` by their product, and the probe
that decides whether a rule answers, the report that says what a primitive is,
and the battery that enumerates the domain all have to ask which they are
looking at. The product bound needed a clause to be finite at all -- `0 * b` is
inside `a * b < 128` for every `b`, and a domain a battery cannot enumerate is
not a domain the machine has checked -- so the row is `a * b < 128` with both
operands under 128, which is 892 pairs and costs nothing honest, because when
both operands are positive the product bound already puts each under 128. The
in-domain tests compare against the limit by division and never form `a + b` or
`a * b`, both of which wrap on large operands and would certify a pair as inside
the domain that is nowhere near it.

**A domain the machine cannot map is a promise with no pages behind it.** The
whole of Step 7's guest-side measurement was blocked for a while by a Step 1
bug that eleven steps of writing had not found, and it is the one worth
remembering. The kernel's initial page tables identity-mapped only the low 64
MiB, from when that was all the machine asked for, while `mem_init` trusted the
boot loader's memory map for the heap and `mem_alloc` handed pointers out of it.
`noun_init` divided a 254 MiB heap in half and promised itself 8,316,544 cells,
nearly twice what was really there. The batteries fit anyway, because they never
quite reached the edge of what was mapped, so `make test` stayed green for six
steps. The moment a session checked a *second* rule -- boot batteries plus a
typed install -- the machine wrote past 64 MiB, page-faulted, triple-faulted
because the IDT does not survive a second exception, and QEMU exited with
nothing on the wire. The map now has 128 2 MiB leaves and covers the whole
heap, and 8,316,544 cells is the truth. Item 29 has it, including why the
readiness of the guest is the only thing that could have found it.

**The lesson, kept because it is the same one twice.** A suite that counts
events and a suite that checks values are not the same suite -- that is the
jet bug above, found by going back to look for problems. This one is a claim
the machine made about itself that nothing in the suite was asking it to keep:
`make test` asked the batteries to pass, and they passed inside half the arena
the machine had promised, which is exactly the shape of a passing test over a
number nothing was cross-checking. The rule that came out of it is in item 29:
the identity map has to cover the heap, because that is the contract the bump
allocator already had with it.

**Green, and both rules are covered on the wire.** Step 7 reused the rules
check rather than adding a checklist item, because a rule is a rule. `make
rules-test` now covers both primitives: `+add(9, 5) = 14` in-domain and `+add(1000,
2000) = 3000` out of it, `+mul(9, 5) = 45` in-domain and `+mul(1000, 2000) =
2000000` out of it, each read from the counters, each put away and each refused
by name when it lies -- a `+mul` record claiming a sum domain is "claims a domain
the machine does not certify", and a definition that says `+mul(0, 0) = 3` is
caught by the battery. `make check` is green end to end.

**Green.** `make test` gives 262 checks, 0 failing, `LAMP: LIT`, and a checklist
of 13 -- 11 of Step 1's own, plus the one Step 4 added and the one Steps 5, 6
and 7 share. The machine boots into
long mode, reads a heap out of the PVH memory map, and runs the noun, Nock and
primitive layers. Step 1 is finished: the twenty
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

**Done.** The self-test is 262 checks, 0 failing, `LAMP: LIT`, checklist 13 of 13
-- the eleventh having been the one that pulled this step out of the red, that a
formula typed at the machine runs and what it leaves behind matters. The rules
check, item 13, is Step 5's own.  It was checked by breaking the count increment
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
   machine now says so: 8,316,544 cells with QEMU's 256MB, 406 used by the
   self-test -- and since item 29 every one of those is a cell the identity map
   has a page for, which the number did not used to mean. A line costs about 3
   cells at one character and about 4100 at a
   full 4096-character line, so a session runs to somewhere between tens of
   thousands and a couple of million entries -- and when the arena is full,
   `noun_cons` crashes rather than reusing, which is Step 4's problem to solve
   and not something to discover at. The reader's line limit, not the arena, is
   what a session of compiled formulas will run into first: at 4096 characters
   it spends 4100 cells of 8,316,544, and the depth limit of 256 open brackets
   is the one reached first in practice.

## Where Step 3 stands

**Green, for a language that is ten forms long.** `tools/hoon.c` is 605 lines of
host C, it links the machine's own `noun.c`, `nock.c`, `primitives.c`,
`book.c` and `guestbook.c` rather than a copy of them, and it has two suites:
`make hoontest` is 40 checks on the host, where the formulas are run by the
machine's own interpreter and the answers come from the machine's own book, and
`make teach` is the bridge end to end — the host compiles eleven expressions, the
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
was caught by reading a refusal, and it is the one worth remembering. The
spellings in this section are the ones the compiler had while it was finding
them: `=+` was the push here, and the push is `=>` now, with `=+` the core rune
(item 24). Nothing below is wrong about the machine; the names have moved on.

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

No run-time addition, on purpose. No types beyond "an address the session has"
and "a value the machine can put in a noun", and nothing that could grow past a
line of 4096 characters. Cores and names are here now -- they were the missing
piece of this list, and the section below is what they came to.

The next step was meant to be the native operations proven against their Nock
definitions, which is what would unblock `+(a b)`. Work on that started, and it
turned the next step into a smaller question than it looked like, and then into a
different one.

**Three of the twenty are proved.** `make proofs` carries the Nock definitions of
`+inc`, `+eq` and `+not`, runs each through the machine's own reader, printer and
interpreter, and requires it to agree with the native over 1,412 inputs --
including on which inputs stop, since a native that answers where its definition
would have stopped is the one way to make a wrong machine faster.

**The other seventeen cannot be proved this way, and not for the reason this file
used to give.** It used to say that a loop in Nock is a core which calls its own
arm, which is a noun containing itself, which Hoon writes with a *name*, and that
this language has no names. The middle of that is wrong, and it was worth checking
instead of leaving in place: a core does not have to contain itself. Put the arm
inside the core -- it is a subtree, so the core already holds it -- and the arm
reads its own arm back out of the core and rebuilds the core around that. The
self-reference is a tree address resolved at run time rather than an edge in the
noun graph, so nothing cyclic is ever built and `noun_equal` and the printer stay
well behaved. `decisions.md` item 23 has it measured: a loop runs at 24 steps and
21 nouns a turn, and the machine's call-depth ceiling stops it at about 4,900
turns.

So the seventeen are not waiting for names. They are waiting for arithmetic this
Nock does not have. The interpreter's whole arithmetic is opcode 4 and opcode 5 --
increment and equality -- and a step-1 jet cannot make up the rest, because the
native's answer is thrown away rather than handed to the formula: `[11 [0 [7 5]]
[1 999]]` answers 999 with the hooks on and with them off, while `+add(7, 5) = 12`
is computed beside it and discarded. Counting is the only addition left, and
counting costs its operand. The definition that does it is in item 23: 992
characters, the reader takes it, the printer gives it back byte for byte, it
answers `+add(1000, 1000) = 2000`, and it stops with `call depth exceeded` at
`+add(3000, 500)`. The battery's smallest large input is 2^31, so the distance
between a definition existing and a definition being provable here is about
six orders of magnitude.

**Which leaves the pattern the three already showed.** `+inc` is opcode 4, `+eq`
is opcode 5, `+not` is opcode 6 over opcode 5. The method reaches the primitives
the opcode set already implements, and the other seventeen have no opcode. The
seventeen are the C table's own trust, which is what `decisions.md` item 9 has
said from the start; `make proofs` now says it in the open rather than implying a
queue that something is going to arrive and empty. Unrolling was tried as a way
around this and does not work either: an unrolled 63-bit carry chain still has to
read a bit at each step, reading a bit needs a comparison, and a comparison is a
borrow chain that has to read bits. The tower is circular rather than merely long.

One of the three is proved because a bug was fixed rather than because the
definition was found. `+not` was `a ^ NOUN_ATOM_MAX` -- a 63-bit complement
wearing the name of a logical not, so a program written against Urbit's `!.`
would have got back a number that was not the answer to anything. Making the
body match the name is what gave it a definition at all: the complement needs
63 bit positions and a loop, and the logical not is equality and a conditional.
Every native is named in one of two tables in `tools/jet-proofs.c` -- proved, or
pending with the reason it is pending -- and a primitive in neither is a failure,
so this gap cannot quietly grow back open.

What this does *not* do: it does not unblock `+(a b)`, and the reason is now
permanent rather than pending. A line of this language cannot add two numbers it
read at run time, because no formula can obtain a native's value and no opcode
combines two values. So the language's next step was taken in that direction
rather than in the direction of a proof: cores and names as features, `=+(a b)`
worth being able to write, item 23 saying what one costs. What they cost is
below, and the answer is 17 steps and 7 nouns a call, with the source the same
size however far the loop counts.

## Cores, names, and a loop

`=+(arm sample body)` builds `[sample arm 0]`, so `arm` is at `/6` and the
sample's things are `/4`, `/10`, `/22` -- reads in the outer subject, evaluated
when the body is compiled. A name in a body is a read of `/6`; a name in a
sample is refused, because a sample is compiled where that name means nothing.
Six calls of the loop below count up to five and answer with the limit, at 17
steps and 7 nouns a call, with the formula the same size as the count grows:

```
=+(arm |(0 5) ?:(=(/4 /10) /10 ~(arm |(|(+(/4) /10) arm))))
```

The second half of this is a compiler check that came out of a wrong loop: the
same source with the arm in front of the new sample answered `5` instead of `1`
on every limit and said nothing, because `/4` read the arm and `/10` read the
counter, and the tests had been written from its answers. A sample that goes
back into a core is now walked again and compared with the walk that gave the
arm's addresses -- addresses and nesting, not the expressions filling them -- and
a mismatch is a refusal naming both shapes. `docs/decisions.md` item 24 has the
measurements, the old expression, and why the machine was right throughout.

Two more refusals came out of the same hunt: an arm call's core cannot carry a
number where the arm goes, since the machine would run it as a formula and stop,
and a name in a sample is refused rather than compiled, because a sample is
compiled in the outer subject where that name is a read of `/6` -- the last
answer at the top level, the enclosing arm in a nested core. The third find was
in the compiler rather than the language: a shape row's label was a pointer into
its own buffer, and rows move, so a core pushed into had a refusal that listed
`/3` twice and no first thing of the sample. The addresses were always right,
which is exactly why nothing failed. The rows carry their labels now, and the
pushed core's addresses have value tests of their own.

## Where the Nock definitions are, and are not

The seventeen pending operations want proofs against Nock definitions, and the
obvious place to look for an authoritative set is the evaluation corpus under
`~/kev/evals/external`. It is not there. `scienthoon-v1` is support-ticket
classification, `semif-v1` is evidence interpretation, `ekzhang-mmlupro-v1` is
a bag of yes/no puzzles, and the one hit for "nock" anywhere in the corpus is
the word "knockout". So the only Nock in the tree is `kernel/nock.c`, which is
what every claim in this file has been checked against, and the arithmetic
questions are not waiting on a definition to be found -- they are waiting on
someone to write one, as item 23's `+add` was written rather than found.

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
