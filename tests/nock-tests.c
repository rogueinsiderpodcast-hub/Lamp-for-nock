/* The test suite.
 *
 * Every expected value here is derived by hand from the Nock 4K rules, and the
 * derivation is written next to the test.  If a test fails, the comment beside
 * it is the thing to argue with.
 *
 * Four groups:
 *
 *   nouns       the noun representation and the three tree operators
 *   opcodes     each Nock instruction, including the pairs that are easy to
 *               confuse with one another (2 against 7, 0 against 9, literal
 *               arguments against formula arguments)
 *   crashes     every way this machine is allowed to stop, including a formula
 *               that reduces to itself, followed by checks that the machine
 *               still works afterwards
 *   primitives  the native bank, at the C level
 *
 * The crash tests matter more than they look.  A Nock formula that reduces to
 * itself is defined to be a crash, and "the machine stops" is only a useful
 * property if "the machine carries on" is true too.
 */

#include "kernel.h"
#include "tests.h"
#include "harness.h"

/* --- builders ---------------------------------------------------------- */

static noun A(u64 v) { return noun_atom(v); }
static noun C(noun a, noun b) { return noun_cons(a, b); }

/* Formulas: an opcode and n arguments, right-nested, with a filler 0 so the
 * last argument has somewhere to sit.  Remember that a formula is a cell, so
 * [0 2] below means opcode 0 with the literal address 2 -- see test_opcodes. */
static noun f1(u64 op, noun a)                 { return C(A(op), C(a, A(0))); }
static noun f2(u64 op, noun a, noun b)         { return C(A(op), C(a, C(b, A(0)))); }
static noun f3(u64 op, noun a, noun b, noun c)  { return C(A(op), C(a, C(b, C(c, A(0))))); }

/* Data, not formulas. */
static noun d2(noun a, noun b)         { return C(a, C(b, A(0))); }
static noun d3(noun a, noun b, noun c) { return C(a, C(b, C(c, A(0)))); }

/* --- nouns ------------------------------------------------------------- */

static u64 fingerprint(noun n, u64 acc)
{
    if (noun_is_atom(n))
        return acc * 31 + noun_atom_val(n);
    acc = fingerprint(noun_head(n), acc);
    return fingerprint(noun_tail(n), acc);
}

static void test_nouns(void)
{
    group("nouns");
    machine_reset_error();

    check(noun_is_atom(A(0)), "0 is an atom");
    check(noun_is_atom(A(12345)), "an atom is an even-tagged word");
    check(noun_atom_val(A(12345)) == 12345, "atom values survive the round trip");
    check(A(NOUN_ATOM_MAX) == (NOUN_ATOM_MAX << 1), "2^63 - 1 is a representable atom");
    check(noun_is_cell(C(A(1), A(2))), "cons produces a cell");

    noun one = C(A(1), A(2));
    check(noun_equal(noun_head(one), A(1)) && noun_equal(noun_tail(one), A(2)),
          "head and tail of a fresh cell");

    check(noun_equal(C(A(1), A(2)), C(A(1), A(2))),
          "equality is structural, not a comparison of arena indices");
    check(!noun_equal(C(A(1), A(2)), C(A(2), A(1))), "order matters in equality");
    check(!noun_equal(C(A(1), A(2)), A(0)), "a cell never equals an atom");
    check(!noun_equal(d2(A(1), A(2)), d3(A(1), A(2), A(3))),
          "a longer cell is not a shorter cell");

    /* [11 [22 [33 0]]] : slot 1 is the noun, 2 the head, 3 the tail, 6 the head
     * of the tail, 7 the tail of the tail, 14 the head of that.  Note that 4 and
     * 5 are the two children of slot 2, which here is the atom 11, so they
     * crash: the path to 22 runs through 3, not 2. */
    noun deep = d3(A(11), A(22), A(33));
    check(noun_equal(noun_slot(deep, 1), deep), "slot 1 of a noun is the noun");
    check(noun_equal(noun_slot(deep, 2), A(11)), "slot 2 is the head");
    check(noun_equal(noun_slot(deep, 3), d2(A(22), A(33))), "slot 3 is the tail");
    check(noun_equal(noun_slot(deep, 6), A(22)), "slot 6 is the head of the tail");
    check(noun_equal(noun_slot(deep, 7), C(A(33), A(0))), "slot 7 is the tail of the tail");
    check(noun_equal(noun_slot(deep, 14), A(33)), "slot 14 is the head of the tail of the tail");

    /* #[axis value noun] rebuilds rather than writing through, so the original
     * keeps its value.  This is what makes the arena append-only in practice. */
    noun edited = noun_edit(deep, 2, A(99));
    check(noun_equal(edited, d3(A(99), A(22), A(33))), "edit replaces one part");
    check(noun_equal(deep, d3(A(11), A(22), A(33))), "edit leaves the original alone");
    check(noun_equal(noun_edit(deep, 6, A(99)), d3(A(11), A(99), A(33))), "edit at a deeper address");
    check(noun_equal(noun_edit(deep, 1, A(7)), A(7)), "edit at address 1 replaces the whole noun");
}

/* --- opcodes ----------------------------------------------------------- */

static void test_opcodes(void)
{
    group("nock opcodes");

    noun s      = d2(A(7), A(8));          /* [7 [8 0]]                    */
    noun s2     = d3(A(7), A(9), A(10));   /* [7 [9 [10 0]]]               */
    noun triple = d3(A(1), A(2), A(3));    /* [1 [2 [3 0]]]                */

    /* [1 b] is a constant: b is data, not a formula. */
    expect_atom("[1 42] is 42 whatever the subject is", s, f1(1, A(42)), 42);
    expect_noun("[1 [7 8]] is the cell itself, unevaluated", s, f1(1, d2(A(7), A(8))), s);

    /* [0 b] is a tree address in the subject.  The address is a literal, which
     * is why these formulas carry the atom 2 and not a formula for 2. */
    expect_atom("[0 1] is the whole subject, when it is an atom", A(5), f1(0, A(1)), 5);
    expect_noun("[0 1] is the whole subject, when it is a cell", s, f1(0, A(1)), s);
    expect_atom("[0 2] is the head", s, f1(0, A(2)), 7);
    expect_noun("[0 3] is the tail", s, f1(0, A(3)), C(A(8), A(0)));
    expect_atom("[0 6] is the head of the tail", s2, f1(0, A(6)), 9);
    expect_noun("[0 7] is the tail of the tail", s2, f1(0, A(7)), C(A(10), A(0)));
    expect_atom("[0 14] is the head of the tail of the tail", s2, f1(0, A(14)), 10);

    /* [3 b] is 0 for a cell and 1 for an atom. */
    expect_atom("[3 [1 5]] is 1, because 5 is an atom", s, f1(3, f1(1, A(5))), 1);
    expect_atom("[3 [1 [7 8]]] is 0, because a cell", s, f1(3, f1(1, d2(A(7), A(8)))), 0);
    expect_atom("[3 [0 2]] looks at the subject and finds an atom", s, f1(3, f1(0, A(2))), 1);

    /* [4 b] adds one. */
    expect_atom("[4 [1 41]] is 42", s, f1(4, f1(1, A(41))), 42);
    expect_atom("[4 [0 2]] increments the head of the subject", s, f1(4, f1(0, A(2))), 8);

    /* [5 b c] is 0 for the same noun and 1 for different. */
    expect_atom("[5] is 0 for two equal atoms", s, f2(5, f1(1, A(3)), f1(1, A(3))), 0);
    expect_atom("[5] is 1 for two different atoms", s, f2(5, f1(1, A(3)), f1(1, A(4))), 1);
    expect_atom("[5] is 0 for two equal cells built separately",
                s, f2(5, f1(1, d2(A(1), A(2))), f1(1, d2(A(1), A(2)))), 0);
    expect_atom("[5] is 1 for cells with the same parts in the other order",
                s, f2(5, f1(1, d2(A(1), A(2))), f1(1, d2(A(2), A(1)))), 1);

    /* [2 b c] is *[*[a b] *[a c]]: both arguments are evaluated against the
     * old subject, then b's product is the subject and c's product is the
     * formula.  With the subject [7 [8 0]], b = [0 3] is its tail, so the new
     * subject is [8 0]; c = [1 [0 2]] is a constant holding the formula
     * [0 2], which reads address 2.  The answer is the head of [8 0], which is
     * 8.  Read against the old subject instead, address 2 would be 7, so the
     * answer says which subject was in play.  It is c's *product* that becomes
     * the formula, and that is the whole difference from opcode 7. */
    expect_atom("[2] puts a new subject in play: b's product is the subject",
                s, f2(2, f1(0, A(3)), f1(1, f1(0, A(2)))), 8);
    expect_code("[2] with a constant c runs an atom as a formula, so it crashes",
                NOCK_CRASH, s, f2(2, f1(0, A(3)), f1(1, A(9))));

    /* [7 b c] is compose: c is handed over unevaluated, and that is the whole
     * difference from opcode 2.  With c = [1 5], opcode 7 answers 5, while
     * opcode 2 first reduces c to the atom 5 -- and a formula must be a cell,
     * so opcode 2 crashes.  The pair of tests below is the point. */
    expect_atom("[7 [0 2] [1 5]] is 5, because c is never evaluated", s,
                f2(7, f1(0, A(2)), f1(1, A(5))), 5);
    expect_code("[2 [0 2] [1 5]] crashes, because c is evaluated and 5 is not a formula",
                NOCK_CRASH, s, f2(2, f1(0, A(2)), f1(1, A(5))));

    /* [6 b c d]: 0 is true and 1 is false, as everywhere in Nock. */
    expect_atom("[6] takes the then branch when the test is 0",
                s, f3(6, f1(1, A(0)), f1(1, A(111)), f1(1, A(222))), 111);
    expect_atom("[6] takes the else branch when the test is 1",
                s, f3(6, f1(1, A(1)), f1(1, A(111)), f1(1, A(222))), 222);
    expect_atom("[6] takes the else branch for any test that is not 0",
                s, f3(6, f1(1, A(7)), f1(1, A(111)), f1(1, A(222))), 222);
    expect_atom("[6] evaluates the test as a formula: 0 = 0 is 0, so then",
                s, f3(6, f2(5, f1(1, A(0)), f1(1, A(0))), f1(1, A(111)), f1(1, A(222))), 111);
    expect_atom("[6] evaluates the test as a formula: 0 = 7 is 1, so else",
                s, f3(6, f2(5, f1(1, A(0)), f1(1, A(7))), f1(1, A(111)), f1(1, A(222))), 222);

    /* [8 b c] pushes b's product onto the front of the subject, then runs c
     * there.  c is the formula, not something to evaluate first, so the tests
     * below read an address of the new subject.  The new subject is
     * [7 [7 [8 0]]] when the subject is [7 [8 0]] and b's product is 7. */
    expect_atom("[8] pushes b: address 2 of the new subject is b", s,
                f2(8, f1(0, A(2)), f1(0, A(2))), 7);
    expect_noun("[8] the old subject survives whole: address 3 is the old subject", s,
                f2(8, f1(0, A(2)), f1(0, A(3))), s);
    expect_atom("[8] the old subject is still reachable at depth: address 14 is 8", s,
                f2(8, f1(0, A(2)), f1(0, A(14))), 8);

    /* [9 b c]: evaluate c to get a core, pull a formula out of it by address,
     * then call that formula with the core as the subject.  The core below is
     * [[1 42] 0 0], so address 2 holds the formula [1 42], and calling it
     * against the core gives 42.  b is a literal axis; c is a formula. */
    noun core = d2(f1(1, A(42)), A(0));
    expect_atom("[9 2 [0 1]] calls address 2 of the core as a formula", core,
                f2(9, A(2), f1(0, A(1))), 42);
    expect_code("[9] with an axis that lands on an atom has no formula to call",
                NOCK_CRASH, core, f2(9, A(3), f1(0, A(1))));

    /* [10 [b c] d]: the pair is a literal.  b is the address to replace, c is a
     * formula for the new value, d is a formula for the noun to edit.  The
     * results are written out with C because they are genuine cells, not the
     * right-nested lists that d2 and d3 build. */
    expect_noun("[10] replaces address 2", triple, f2(10, C(A(2), f1(1, A(99))), f1(0, A(1))),
                C(A(99), C(A(2), C(A(3), A(0)))));
    expect_noun("[10] replaces address 3", triple, f2(10, C(A(3), f1(1, A(99))), f1(0, A(1))),
                C(A(1), A(99)));
    expect_noun("[10] replaces address 14", triple, f2(10, C(A(14), f1(1, A(99))), f1(0, A(1))),
                d3(A(1), A(2), A(99)));
    expect_noun("[10] takes its new value from a formula, not a literal", triple,
                f2(10, C(A(2), f1(1, A(7))), f1(0, A(1))), C(A(7), C(A(2), C(A(3), A(0)))));

    /* [11 b c]: the hint is computed and thrown away. */
    expect_atom("[11] with an atom hint is a static hint and does nothing",
                s, f2(11, A(12345), f1(0, A(2))), 7);
    expect_atom("[11] with a cell hint runs the hint's tail and ignores it",
                s, f2(11, C(A(0), f1(1, A(1))), f1(0, A(2))), 7);
    expect_code("[11] must not skip a dynamic hint that would crash",
                NOCK_CRASH, s, f2(11, C(A(0), A(0)), f1(0, A(2))));
}

/* --- jets -------------------------------------------------------------- */

/* Step 1 jet convention:
 *
 *     [11 [<primitive index> <argument formula>] <real formula>]
 *
 * The interpreter evaluates <argument formula>, hands the result to the native
 * primitive and throws the primitive's answer away.  +add is primitive 0. */
static void test_jets(void)
{
    group("jets");

    noun s      = d2(A(7), A(8));
    u64  before = nock_jet_fires();

    /* The argument formula is the constant [2 3] -- a pair of two atoms, which
     * is the shape the convention asks for -- so the primitive is called with 2
     * and 3.  The real formula reads address 2 of the subject, which is 7. */
    noun formula = f2(11, C(A(0), f1(1, C(A(2), A(3)))), f1(0, A(2)));

    nock_jet_hooks(0);
    noun quiet = 0;
    int rc_quiet = nock_run(s, formula, &quiet);

    nock_jet_hooks(1);
    noun loud = 0;
    int rc_loud = nock_run(s, formula, &loud);

    check(rc_quiet == NOCK_OK && rc_loud == NOCK_OK, "the hint formula runs either way");
    check(nock_jet_fires() == before + 1, "exactly one primitive ran, so the jet fired");
    check(noun_equal(quiet, loud) && noun_equal(loud, A(7)),
          "the jet does not change the answer, which is the whole point of a hint");

    /* Counting firings is not a test of the jet.  It has to be the numbers the
     * formula wrote, going in, and the sum of them, coming out: a jet reading
     * the right answer out of the wrong operand would pass every check above. */
    u64 idx = 0, ja = 0, jb = 0, jr = 0;
    nock_jet_last(&idx, &ja, &jb, &jr);
    check(prim_index("+add") >= 0 && idx == (u64)prim_index("+add")
          && ja == 2 && jb == 3 && jr == 5,
          "the native was handed the two numbers written and answered 5");

    /* An argument that is not a pair of numbers is not ours, and must not be
     * jetted.  Read as a pair this one is ([1 2], 0) -- a cell where a number
     * should be, which atom_val() reports as an arena index -- so +div would be
     * called as (0, 0) and stop the machine over a division nothing asked for,
     * with the real formula still to answer. */
    noun bomb = f2(11, C(A(3), f1(1, C(C(A(1), A(2)), A(0)))), f1(0, A(2)));
    noun out  = 0;
    int rc_bomb = nock_run(s, bomb, &out);
    check(rc_bomb == NOCK_OK && noun_equal(out, A(7))
          && nock_jet_fires() == before + 1,
          "an argument that is not a pair of numbers is left to the real formula");

    /* A native that stops is a hint that was wrong, not a fault in the program
     * being run.  +inc cannot increment 2^63 - 1, and the real formula was a
     * moment from answering 7 all the same, so the probe backs out and the
     * answer with hooks off is the answer here too. */
    int inc = prim_index("+inc");
    check(inc >= 0, "+inc is in the bank, to be hinted at below");

    if (inc >= 0) {
        u64 declined = nock_jet_declines();
        noun over = f2(11, C(A((u64)inc), f1(1, C(A(9223372036854775807ULL), A(0)))),
                       f1(0, A(2)));
        noun ovr = 0;
        int rc_over = nock_run(s, over, &ovr);
        check(rc_over == NOCK_OK && noun_equal(ovr, A(7))
              && nock_jet_declines() == declined + 1
              && nock_jet_fires() == before + 1,
              "a native that stops backs the hint out instead of stopping the machine");
    }

    check(prim_count() == 20, "the native bank holds twenty primitives");
    check(prim_index("+add") == 0, "+add is primitive 0");
    check(prim_index("+nosuch") < 0, "an unknown primitive name is not found");
}

/* --- crashes ----------------------------------------------------------- */

static void test_crashes(void)
{
    group("crashes");

    noun s      = d2(A(7), A(8));
    noun s_cell = d2(A(1), A(2));

    expect_code("tree address 0 does not name a noun", NOCK_CRASH, s, f1(0, A(0)));
    expect_code("a tree address cannot descend into an atom", NOCK_CRASH, s, f1(0, A(4)));
    expect_code("cannot increment a cell", NOCK_CRASH, s_cell, f1(4, f1(0, A(1))));
    expect_code("a formula must be a cell, so a bare atom is a crash", NOCK_CRASH, s, A(1));
    expect_code("12 is not a Nock instruction", NOCK_CRASH, s, f1(12, A(0)));
    expect_code("a formula is missing its arguments", NOCK_CRASH, s, C(A(2), A(1)));
    expect_code("a conditional test must be an atom", NOCK_CRASH, s,
                f3(6, f1(1, d2(A(1), A(2))), f1(1, A(1)), f1(1, A(2))));
    expect_code("an edit pair must be a cell", NOCK_CRASH, s, f2(10, A(2), A(1)));
    expect_code("an edit axis must be an atom", NOCK_CRASH, s,
                f2(10, C(d2(A(1), A(1)), f1(1, A(1))), A(1)));
    expect_code("a tree address in [0] must be an atom", NOCK_CRASH, s, f1(0, d2(A(1), A(2))));

    /* A formula that reduces to itself.  F = [2 [0 1] [0 1]]: both of its
     * arguments are the formula [0 1], which against any subject answers the
     * subject itself.  So nock(S, F) is nock(S, S) -- and with S = F, which is
     * what the subject is here, that is nock(F, F) again, forever.  Nothing
     * terminates, so the step limit has to stop it. */
    noun runaway = f2(2, f1(0, A(1)), f1(0, A(1)));

    nock_init(5000);
    expect_code("a formula that reduces to itself hits the step limit",
                NOCK_STEPS_OUT, runaway, runaway);
    nock_init(NOCK_DEFAULT_STEP_LIMIT);
    nock_jet_hooks(1);

    /* The important half: the machine is still here. */
    expect_atom("the machine still works after a runaway formula", s, f1(1, A(4242)), 4242);
    expect_atom("and after a crash it can still increment", s, f1(4, f1(1, A(41))), 42);
}

/* --- native primitives ------------------------------------------------- */

static void test_primitives(void)
{
    group("native primitives");

    expect_prim("+add", 2, 3, 5);
    expect_prim("+add", 0, 0, 0);
    expect_prim("+add", NOUN_ATOM_MAX, 0, NOUN_ATOM_MAX);
    expect_prim("+add", 123456789, 987654321, 1111111110ULL);

    expect_prim("+sub", 5, 3, 2);
    expect_prim("+sub", 3, 3, 0);
    expect_prim("+sub", 0, 0, 0);

    expect_prim("+mul", 0, 100, 0);
    expect_prim("+mul", 1, NOUN_ATOM_MAX, NOUN_ATOM_MAX);
    expect_prim("+mul", 6, 7, 42);
    expect_prim("+mul", 1000000, 1000000, 1000000000000ULL);

    expect_prim("+div", 42, 6, 7);
    expect_prim("+div", 7, 6, 1);
    expect_prim("+div", 0, 5, 0);

    expect_prim("+mod", 43, 6, 1);
    expect_prim("+mod", 42, 6, 0);
    expect_prim("+mod", 0, 5, 0);

    expect_prim("+min", 3, 5, 3);
    expect_prim("+min", 5, 3, 3);
    expect_prim("+max", 3, 5, 5);
    expect_prim("+max", 5, 3, 5);

    /* Comparisons answer 0 for true and 1 for false, like Nock's own 0 rule. */
    expect_prim("+eq", 3, 3, 0);
    expect_prim("+eq", 3, 4, 1);
    expect_prim("+lt", 3, 4, 0);
    expect_prim("+lt", 4, 3, 1);
    expect_prim("+le", 3, 3, 0);
    expect_prim("+le", 4, 3, 1);
    expect_prim("+gt", 4, 3, 0);
    expect_prim("+gt", 3, 4, 1);
    expect_prim("+ge", 3, 3, 0);
    expect_prim("+ge", 3, 4, 1);

    expect_prim("+and", 0xF0F0, 0x0FF0, 0x00F0);
    expect_prim("+or",  0xF0F0, 0x0FF0, 0xFFF0);
    expect_prim("+xor", 0xF0F0, 0x0FF0, 0xFF00);

    expect_prim("+lsh", 1, 10, 1024);
    expect_prim("+lsh", 3, 0, 3);
    expect_prim("+rsh", 1024, 10, 1);
    expect_prim("+rsh", 7, 1, 3);

    /* 62 is the last bit position in a 63-bit atom, and it is the last answer
     * either shift gives: 2^62 is in range, 2^62 >> 62 is 0.  Pinning the
     * boundary from the answering side is what says 63 is a domain limit
     * rather than an accident. */
    expect_prim("+lsh", 1, 62, 4611686018427387904ULL);
    expect_prim("+lsh", 0, 62, 0);
    expect_prim("+rsh", 4611686018427387904ULL, 62, 1);
    expect_prim("+rsh", 1, 62, 0);

    expect_prim("+inc", 0, 0, 1);
    expect_prim("+inc", 41, 0, 42);
    expect_prim("+dec", 1, 0, 0);
    expect_prim("+dec", 42, 0, 41);

    /* +not is logical, as Urbit's !. is: only zero answers 1, and everything
     * else answers 0. */
    expect_prim("+not", 0, 0, 1);
    expect_prim("+not", 1, 0, 0);
    expect_prim("+not", 5, 0, 0);
    expect_prim("+not", NOUN_ATOM_MAX, 0, 0);
    expect_prim("+not", 0x0F, 0, 0);

    /* Nothing wraps around, nothing goes negative, nothing divides by zero. */
    expect_prim_crash("+add", NOUN_ATOM_MAX, 1);
    expect_prim_crash("+sub", 3, 4);
    expect_prim_crash("+mul", NOUN_ATOM_MAX, 2);
    expect_prim_crash("+div", 1, 0);
    expect_prim_crash("+mod", 1, 0);
    expect_prim_crash("+inc", NOUN_ATOM_MAX, 0);
    expect_prim_crash("+dec", 0, 0);
    expect_prim_crash("+lsh", 1, 63);
    expect_prim_crash("+lsh", NOUN_ATOM_MAX, 1);
    expect_prim_crash("+rsh", 1, 63);
    /* What the guard is really for: C leaves a shift of 64 or more undefined,
     * so an unbounded amount is a crash of a different kind entirely. */
    expect_prim_crash("+lsh", 1, 64);
    expect_prim_crash("+rsh", 1, 100);
    expect_prim_crash("+lsh", 0, 999);
}

/* --- solid state ------------------------------------------------------- */

static void test_solid_state(void)
{
    group("solid state");

    noun s = d3(A(1), A(2), A(3));        /* [1 [2 [3 0]]] */
    u64  before = fingerprint(s, 7);
    u64  cells_before = noun_cell_count();

    /* Push 55 onto the subject, then read address 6 of the new subject.  The
     * new subject is [55 [1 [2 [3 0]]]], whose address 6 is the head of its
     * tail, which is the head of the old subject: 1.  Address 4 would be the
     * head of the head, which is the atom 55. */
    noun pushed = f2(8, f1(1, A(55)), f1(0, A(6)));
    expect_atom("push 55, then read address 6 of the pushed subject", s, pushed, 1);

    /* Edit address 2 of the subject, evaluated through opcode 10. */
    noun edited = f2(10, C(A(2), f1(1, A(99))), f1(0, A(1)));
    expect_noun("opcode 10 replaces address 2", s, edited, d3(A(99), A(2), A(3)));

    check(fingerprint(s, 7) == before, "the subject is unchanged afterwards, part for part");
    check(noun_cell_count() > cells_before, "the arena only ever grows");

    /* The same input gives the same output, which is the whole claim. */
    noun first = 0, second = 0;
    int rc1 = nock_run(s, edited, &first);
    int rc2 = nock_run(s, edited, &second);
    check(rc1 == NOCK_OK && rc2 == NOCK_OK && noun_equal(first, second),
          "the same subject and formula give the same answer");
    check(noun_cell_count() > cells_before, "and it grew the arena again rather than reusing it");

    /* The ceiling, in the one place it matters.  Everything the guest book will
     * keep is a noun, so this number is how long a session can get. */
    serial_puts("  the arena holds ");
    serial_put_dec(noun_capacity());
    serial_puts(" cells, and holds ");
    serial_put_dec(noun_cell_count());
    serial_puts(" of them now\n");
}

/* --- entry point ------------------------------------------------------- */

int nock_tests_run(void)
{
    machine_reset_error();
    test_nouns();
    test_opcodes();
    test_jets();
    test_crashes();
    test_primitives();
    test_solid_state();
    return tests_failed_count();
}
