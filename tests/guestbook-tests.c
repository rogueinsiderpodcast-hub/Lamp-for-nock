/* The reader's test suite.
 *
 * The reader is the first thing on this machine that a person can get wrong,
 * so it is tested the way everything else here is: every expected noun is
 * written out by hand from the grammar, in a comment, beside the test.
 *
 * There is almost no Nock in this file.  Parsing happens before any formula
 * exists to evaluate, so most of these checks use the plain harness ones --
 * expect_true and check -- and compare nouns with noun_equal directly.  The last
 * group exists to tie the reader to the interpreter, because a reader that
 * produced the wrong noun for the right text would pass everything else.
 */

#include "kernel.h"
#include "tests.h"
#include "harness.h"

/* --- builders ---------------------------------------------------------- */
/* The same short names the Nock suite uses, so a derivation can be written the
 * way it reads: [1 42] is C(1, 42). */

static noun A(u64 v) { return noun_atom(v); }
static noun C(noun a, noun b) { return noun_cons(a, b); }
static noun f1(u64 op, noun a)                { return C(A(op), C(a, A(0))); }
static noun f2(u64 op, noun a, noun b)        { return C(A(op), C(a, C(b, A(0)))); }
static noun f3(u64 op, noun a, noun b, noun c) { return C(A(op), C(a, C(b, C(c, A(0))))); }

/* --- small helpers, because there is no libc ---------------------------- */

static u64 slen(const char *s)
{
    u64 n = 0;
    while (s[n] != '\0')
        n++;
    return n;
}

static int seq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* Parse and check the reader answered this noun.  A failure says what it
 * actually said, because "the reader refused" is the sort of thing that passes
 * a test written to expect a noun. */
static void parse_is(const char *what, const char *text, noun expected)
{
    noun out = 0;
    const char *why = "";
    int rc = gb_parse(text, slen(text), &out, &why);

    if (rc == GB_PARSE_OK && noun_equal(out, expected)) {
        pass(what);
        return;
    }
    check(0, what);
    if (rc != GB_PARSE_OK) {
        serial_puts("         refused, saying \"");
        serial_puts(why);
        serial_puts("\"");
    } else {
        serial_puts("         got ");
        noun_print(out);
        serial_puts(" want ");
        noun_print(expected);
    }
    serial_put_nl();
}

/* Parse and check it was refused, for the stated reason.  The reason is part of
 * the check: "]" and "42 43" are both refusals, and a reader that refused
 * everything would otherwise pass this. */
static void parse_fails(const char *what, const char *text, const char *why_expected)
{
    noun out = 0;
    const char *why = "";
    int rc = gb_parse(text, slen(text), &out, &why);

    if (rc == GB_PARSE_ERROR && seq(why, why_expected)) {
        pass(what);
        return;
    }
    check(0, what);
    serial_puts("         said \"");
    serial_puts(why);
    serial_puts("\"");
    serial_put_nl();
}

/* Parse this text and hand back what it made, for the checks that then use the
 * noun rather than the reading of it.  The length is measured, never written
 * out: a formula here is long enough to miscount, and a miscounted length is a
 * bug in the test that looks exactly like a bug in the reader. */
/* Whether a message says something in particular.  The crash reason is only
 * readable until the machine is reset, so this is how a test gets at it. */
static int mentions(const char *text, const char *needle)
{
    u64 n = 0;
    while (needle[n] != 0)
        n++;
    for (u64 i = 0; text[i] != 0; i++) {
        u64 j = 0;
        while (j < n && text[i + j] == needle[j])
            j++;
        if (j == n)
            return 1;
    }
    return 0;
}

static noun typed(const char *text)
{
    noun out = 0;
    const char *why = "";
    int rc = gb_parse(text, slen(text), &out, &why);
    if (rc == GB_PARSE_OK)
        return out;
    check(0, text);
    serial_puts("         refused, saying \"");
    serial_puts(why);
    serial_puts("\"");
    serial_put_nl();
    return 0;
}

/* --- the grammar -------------------------------------------------------- */

static void test_atoms(void)
{
    group("the reader: atoms");

    /* 0 is the atom 0. */
    parse_is("0 is the atom zero", "0", A(0));
    /* 42 is the atom forty-two. */
    parse_is("42 is the atom forty-two", "42", A(42));
    /* A number is read to the first character that cannot be part of it, so a
     * trailing space is not part of the atom. */
    parse_is("a trailing space is not part of the number", "42 ", A(42));
    parse_is("leading spaces are ignored", "   42   ", A(42));
    /* 007 is 7: a leading zero is not an octal marker. */
    parse_is("007 is 7, not octal", "007", A(7));
    /* Atoms are 63-bit, so 2^63 - 1 is the largest noun that is an atom. */
    parse_is("the largest atom is 2^63 - 1", "9223372036854775807",
             A(NOUN_ATOM_MAX));
    /* One more is not a noun.  This is checked rather than left to wrap: the
     * multiply is done after the check precisely so that 2^63 comes out as an
     * error instead of as 0. */
    parse_fails("2^63 is too large for a noun", "9223372036854775808",
                "that number is too large for a noun");
    /* 2^64 - 1 overflows much further out, and still says so. */
    parse_fails("2^64 - 1 is too large for a noun", "18446744073709551615",
                "that number is too large for a noun");
}

static void test_cells(void)
{
    group("the reader: cells");

    /* [1 42] is a cell whose head is 1 and whose tail is 42. */
    parse_is("[1 42] is a cell", "[1 42]", C(A(1), A(42)));
    /* [1 42 7] is [1 [42 7]]: a cell of two, whose tail is itself a cell. */
    parse_is("[1 42 7] is [1 [42 7]]", "[1 42 7]", C(A(1), C(A(42), A(7))));
    /* [0 [1 2]] is a cell of 0 and the cell [1 2]. */
    parse_is("[0 [1 2]] nests", "[0 [1 2]]", C(A(0), C(A(1), A(2))));
    /* The shape a formula is actually typed in. */
    parse_is("[0 1] is a cell of 0 and 1", "[0 1]", C(A(0), A(1)));
    /* Space inside the brackets does not matter. */
    parse_is("[  1   42  ] is [1 42]", "[  1   42  ]", C(A(1), A(42)));
    /* A newline is a space, so a formula can be typed across two lines. */
    parse_is("[1\n42] is [1 42]", "[1\n42]", C(A(1), A(42)));
    /* A cell of two cells. */
    parse_is("[[1 2] [3 4]] is two cells", "[[1 2] [3 4]]",
             C(C(A(1), A(2)), C(A(3), A(4))));
    /* Deep enough to be worth a test, shallow enough to read. */
    parse_is("[0 [1 [2 [3 4]]]] nests four deep", "[0 [1 [2 [3 4]]]]",
             C(A(0), C(A(1), C(A(2), C(A(3), A(4))))));
}

static void test_refusals(void)
{
    group("the reader: what it refuses");

    /* Nothing typed is not an error.  A blank line is a request to be shown the
     * session, which is the one thing the reader's caller does differently. */
    noun out = 0;
    const char *why = "";
    expect_true("a blank line is empty, not wrong",
                gb_parse("", 0, &out, &why) == GB_PARSE_EMPTY);
    expect_true("so is a line of nothing but spaces",
                gb_parse("   \t ", 4, &out, &why) == GB_PARSE_EMPTY);

    /* A line is one noun, so two of them is a mistake worth naming. */
    parse_fails("two atoms in a line is refused", "1 2",
                "that is more than one noun; a line is one formula");
    parse_fails("two cells in a line is refused", "[1 2] [3 4]",
                "that is more than one noun; a line is one formula");
    /* Two atoms *inside* one cell is not two nouns -- it is one noun, a cell
     * of two cells.  The refusal above is about the top level, where a line
     * holds exactly one noun. */
    parse_is("[1 2 3 4] is one noun, not four", "[1 2 3 4]",
             C(A(1), C(A(2), C(A(3), A(4)))));

    /* Brackets have to match, in both directions. */
    parse_fails("an unclosed [ is refused", "[1 2", "a [ was never closed");
    parse_fails("a ] with nothing open is refused", "1 2]", "a ] with nothing open");
    parse_fails("a ] before any [ is refused", "]", "a ] with nothing open");
    /* The second ] has nothing left to close, which is the same mistake as
     * "1 2]" and is reported the same way. */
    parse_fails("a second ] is refused", "[1 2]]", "a ] with nothing open");

    /* [] is a cell with no sides, which is not a noun on this machine. */
    parse_fails("[] is refused rather than read as 0", "[]",
                "[] is not a noun: a cell needs two sides");
    parse_fails("[[]] is refused", "[[]]",
                "[] is not a noun: a cell needs two sides");

    /* Anything not in the grammar. */
    parse_fails("a letter is refused", "x", "expected a digit, a space, [ or ]");
    parse_fails("a sign is refused", "-1", "expected a digit, a space, [ or ]");
    parse_fails("a full stop is refused", "1.5", "expected a digit, a space, [ or ]");
    parse_fails("a comma between numbers is refused", "1, 2",
                "expected a digit, a space, [ or ]");

    /* The depth limit, tested rather than assumed.  40 brackets against 32
     * frames, so the limit is what refuses it and not the bracket balance. */
    {
        char deep[2 * 40 + 3];
        for (int i = 0; i < 40; i++)
            deep[i] = '[';
        deep[40] = '1';
        for (int i = 41; i < 81; i++)
            deep[i] = ']';
        deep[81] = '\0';
        parse_fails("more brackets than there are frames is refused", deep,
                    "too many [ in one line");
    }
    /* Just inside the limit, to show the limit is where it is. */
    {
        char ok[2 * 31 + 3];
        for (int i = 0; i < 31; i++)
            ok[i] = '[';
        ok[31] = '1';
        for (int i = 32; i < 63; i++)
            ok[i] = ']';
        ok[63] = '\0';
        noun out2 = 0;
        const char *why2 = "";
        expect_true("31 brackets are still inside the limit",
                    gb_parse(ok, slen(ok), &out2, &why2) == GB_PARSE_OK);
    }
}

/* --- against the interpreter -------------------------------------------- */
/* The reader is only worth having if what it reads is what the interpreter would
 * have taken.  So: build a formula with the test suite's own builders, parse
 * the same formula as text, and check the two are the same noun.  If they are,
 * then every result the Nock suite proves about the built formula is proved
 * about the typed one, and the second half of this group runs typed formulas to
 * show the interpreter agreeing. */

static void test_against_the_interpreter(void)
{
    group("the reader: typed is the same noun as built");

    /* The reader's whole value is that what it reads is what could have been
     * written in C by hand, so check that directly: parse the text, then compare
     * against the noun this suite's own builders would have made.
     *
     * A formula ends in 0, so the text of f1(1, A(42)) is [1 42 0] and not
     * [1 42].  The last two checks in this group are about that. */

    /* The constant 42.  Opcode 1 returns its argument and ignores the subject. */
    expect_true("[1 42 0] typed is the same noun as f1(1, A(42))",
                noun_equal(typed("[1 42 0]"), f1(1, A(42))));
    /* One argument: opcode 4, the increment, applied to a constant. */
    expect_true("[4 [1 42 0] 0] typed is the same noun as built",
                noun_equal(typed("[4 [1 42 0] 0]"), f1(4, f1(1, A(42)))));
    /* Two arguments: opcode 5, the equality test. */
    expect_true("[5 [1 42 0] [1 42 0] 0] typed is the same noun as built",
                noun_equal(typed("[5 [1 42 0] [1 42 0] 0]"),
                           f2(5, f1(1, A(42)), f1(1, A(42)))));
    /* A conditional, which is the first thing worth typing that is more than one
     * opcode.  Its test is the cell test of the subject, reached through opcode
     * 0, which is the subtree at a literal address: [0 1 0] is the whole
     * subject, and [3 [0 1 0] 0] asks whether that is a cell. */
    expect_true("a typed conditional is the same noun as the builder's",
                noun_equal(typed("[6 [3 [0 1 0] 0] [1 111 0] [1 222 0] 0]"),
                           f3(6, f1(3, f1(0, A(1))),
                              f1(1, A(111)), f1(1, A(222)))));
    /* [1 42] is a perfectly good noun -- the cell of 1 and 42 -- and it is not
     * the constant 42.  The reader has no business refusing it. */
    expect_true("[1 42] typed is a cell, not a formula",
                noun_equal(typed("[1 42]"), C(A(1), A(42))));

    /* And then run some, to show the interpreter agrees with the reader about
     * what it was handed.  A cell and an atom, because that is the only
     * difference the conditional below can notice. */
    noun cell_subject = C(A(7), C(A(8), A(0)));   /* [7 8] */
    noun atom_subject = A(7);

    /* The constant, against both: opcode 1 does not look at the subject. */
    expect_atom("a typed constant answers 42 whatever the subject is",
                cell_subject, typed("[1 42 0]"), 42);
    expect_atom("... including when the subject is an atom",
                atom_subject, typed("[1 42 0]"), 42);
    /* The increment, against both. */
    expect_atom("a typed increment answers 43, whatever the subject is",
                cell_subject, typed("[4 [1 42 0] 0]"), 43);

    /* The conditional.  The cell test of a cell is 0, so the then branch runs. */
    expect_atom("a typed conditional answers 111, because the subject is a cell",
                cell_subject, typed("[6 [3 [0 1 0] 0] [1 111 0] [1 222 0] 0]"),
                111);
    /* The same typed formula against an atom, whose cell test is 1, so the else
     * branch runs.  One formula, two answers, and the difference is the subject
     * -- which is the whole reason Nock takes one. */
    expect_atom("a typed conditional answers 222, because the subject is an atom",
                atom_subject, typed("[6 [3 [0 1 0] 0] [1 111 0] [1 222 0] 0]"),
                222);

    /* And the mistake a person makes first: [1 42] is a good noun and not a
     * whole formula, because a formula ends in 0.  The reader is right to take
     * it; the interpreter is the one that should object. */
    expect_true("a bare cell is a good noun, so the reader takes it",
                noun_equal(typed("[1 42]"), C(A(1), A(42))));
    expect_code("... and a formula with no trailing 0 is the interpreter's complaint",
                NOCK_CRASH, cell_subject, typed("[1 42]"));
    /* The same for a bare atom, which is not even a cell. */
    expect_true("a bare atom is a good noun too",
                noun_equal(typed("7"), A(7)));
    expect_code("... and it is the interpreter that refuses it as a formula",
                NOCK_CRASH, cell_subject, typed("7"));
}

/* --- the book ----------------------------------------------------------- */
/* The book is the guest book's whole behaviour: one formula, one session in, one
 * session out.  These tests are written as nouns, each with the shape spelled
 * out beside it, so a failure says which of the six fields went wrong. */

static void test_the_book(void)
{
    group("the book: one line, one session");

    /* Where a list's parts are, which is the arithmetic the whole book rests
     * on.  [1 2 3] is C(1, C(2, C(3, 0))): an even axis takes a head, an odd
     * one a tail, and each part of a list lands at twice the last plus two. */
    {
        noun list = C(A(1), C(A(2), C(A(3), A(0))));
        expect_true("a list's head is at /2",        noun_equal(noun_slot(list, 2),  A(1)));
        expect_true("its tail is at /3",             noun_equal(noun_slot(list, 3),  C(A(2), C(A(3), A(0)))));
        expect_true("its second part is at /6, not /5",  noun_equal(noun_slot(list, 6),  A(2)));
        expect_true("its third part is at /14, not /13", noun_equal(noun_slot(list, 14), A(3)));
    }

    /* The book's subject is [line session], so the session sits at /3, and a
     * part of it needs one tail-step in front of its own path.  Multiplying by
     * three, or doubling and adding one, both give a different noun rather than
     * an error, which is what the first version of book_build did. */
    {
        noun line    = f1(1, A(42));
        noun log     = A(0);
        noun last    = A(7);
        noun count   = A(2);
        noun session = C(log, C(last, C(count, A(0))));
        noun subject = C(line, session);
        expect_true("the line is at /2",         noun_equal(noun_slot(subject, 2),  line));
        expect_true("the session is at /3",      noun_equal(noun_slot(subject, 3),  session));
        expect_true("its log is at /6, not /5",  noun_equal(noun_slot(subject, 6),  log));
        expect_true("its last answer at /14",    noun_equal(noun_slot(subject, 14), last));
        expect_true("its count is at /30",       noun_equal(noun_slot(subject, 30), count));
        expect_true("and /5 is the line's tail, not the log",
                    noun_equal(noun_slot(subject, 5), noun_tail(line)));
    }

    /* A fresh session is [0 0 0]: no log, no last answer, nothing run.  As a
     * list that is four zeros, the three fields and the 0 every list ends in. */
    expect_noun("an empty session is [0 0 0]",
                A(0), f1(1, gb_empty_session()), C(A(0), C(A(0), C(A(0), A(0)))));

    /* One line, one step.  The line [1 42 0] is the constant 42.
     *
     *   answer    = 42
     *   entry     = [line [answer 0]]   = [[1 42 0] [42 0]]
     *   log       = [entry 0]           = [[[1 42 0] [42 0]] 0]
     *   count     = 1
     *   new       = [log answer count]
     */
    {
        noun line   = f1(1, A(42));
        noun answer = A(42);
        noun entry  = C(line, C(answer, A(0)));            /* [line [answer 0]] */
        noun newlog = C(entry, C(A(0), A(0)));             /* [entry [0 0]]     */
        noun want   = C(newlog, C(answer, C(A(1), A(0)))); /* [log [answer [1 0]]] */

        expect_noun("one line gives one entry, and the answer in it",
                    C(line, gb_empty_session()), gb_book(), want);

        /* The same step, read through the names rather than the shape.  These are
         * the three addresses a person needs and the one they get wrong. */
        noun out = 0;
        expect_true("the step succeeds",
                    gb_step(line, gb_empty_session(), &out) == NOCK_OK);
        expect_true("the last answer is at /6 of the session, not /3", noun_equal(gb_last(out), A(42)));
        expect_true("the count is at /14 of the session, not /8", noun_equal(gb_count(out), A(1)));
        expect_true("the log is the entry, newest first",
                    noun_equal(gb_log(out), newlog));
        expect_true("the log's front is the entry",
                    noun_equal(gb_log_front(gb_log(out)), entry));
        expect_true("and the entry holds the line that was typed",
                    noun_equal(gb_entry_line(gb_log_front(gb_log(out))), line));
        expect_true("and the answer that came back",
                    noun_equal(gb_entry_answer(gb_log_front(gb_log(out))), A(42)));
        expect_true("and nothing left after it, at /6 of the log",
                    gb_log_rest(gb_log(out)) == A(0));
    }

    /* Two lines, and the second run on the session the first left.  This is the
     * whole point of a session: the second line can read the first. */
    {
        noun one  = typed("[1 42 0]");
        noun two  = typed("[1 7 0]");
        noun s1   = 0;
        noun s2   = 0;
        expect_true("the first line runs",  gb_step(one, gb_empty_session(), &s1) == NOCK_OK);
        expect_true("the second runs on the first's session", gb_step(two, s1, &s2) == NOCK_OK);
        expect_true("so there are 2 lines, not 1", noun_equal(gb_count(s2), A(2)));
        expect_true("the newest entry is the second line",
                    noun_equal(gb_entry_line(gb_log_front(gb_log(s2))), two));
        expect_true("and behind it, at /6 of the log, is the first",
                    noun_equal(gb_entry_line(gb_log_front(gb_log_rest(gb_log(s2)))), one));
        expect_true("and behind that is nothing",
                    gb_log_rest(gb_log_rest(gb_log(s2))) == A(0));
    }

    /* Reading the session from inside a formula, which is the thing the C is
     * not doing: these go through the book like any other line. */
    {
        noun first = typed("[1 42 0]");
        noun s     = 0;
        noun out   = 0;
        expect_true("a session with one line in it", gb_step(first, gb_empty_session(), &s) == NOCK_OK);

        expect_true("a line can ask how many lines have run",
                    gb_step(typed("[0 14 0]"), s, &out) == NOCK_OK);
        expect_true("... and be told 1, which was true when it asked",
                    noun_equal(gb_last(out), A(1)));
        expect_true("... and asking still counts as a line", noun_equal(gb_count(out), A(2)));

        expect_true("a line can ask for the line before it",
                    gb_step(typed("[0 8 0]"), s, &out) == NOCK_OK);
        expect_true("... and be given the line that was typed",
                    noun_equal(gb_last(out), first));

        expect_true("a line can ask for the answer before it",
                    gb_step(typed("[0 6 0]"), s, &out) == NOCK_OK);
        expect_true("... and be given 42", noun_equal(gb_last(out), A(42)));
    }

    /* A line that breaks changes nothing.  The book builds a new noun and never
     * edits the old one, so there is nothing to undo, and a machine that lost
     * its history to a typo would be a machine worth distrusting. */
    {
        noun s   = 0;
        noun out = 0;
        expect_true("a session to lose", gb_step(typed("[1 42 0]"), gb_empty_session(), &s) == NOCK_OK);
        expect_true("a line that is not a formula stops on the interpreter's complaint",
                    gb_step(typed("[1 2]"), s, &out) == NOCK_CRASH);
        expect_true("... saying the formula was missing arguments",
                    mentions(nock_crash_reason(), "missing arguments"));
        machine_reset_error();
        expect_true("and the session is untouched, since the count did not move",
                    noun_equal(gb_count(s), A(1)) && out == 0);
        expect_true("and the next line still runs on it",
                    gb_step(typed("[1 7 0]"), s, &out) == NOCK_OK && noun_equal(gb_count(out), A(2)));
    }

    /* Running out of steps is not crashing, and the two are told apart: a
     * formula that gives up and a formula that breaks are different news.  The
     * limit is ten million, far too many to reach by typing, so this one line
     * gets a small limit and then hands it back. */
    {
        nock_init(8);
        expect_code("a formula that runs out of steps stops, rather than going on",
                    NOCK_STEPS_OUT, C(typed("[1 42 0]"), gb_empty_session()), gb_book());
        nock_init(NOCK_DEFAULT_STEP_LIMIT);
    }
}

int guestbook_tests_run(void)
{
    /* No noun_init() here.  The arena is allocated once, at boot, and
     * reallocating it would throw away every noun built so far -- which is not
     * a thing this machine can undo, because the old arena is still underneath
     * the new bump pointer. */
    machine_reset_error();

    test_atoms();
    test_cells();
    test_refusals();
    test_against_the_interpreter();
    test_the_book();

    return tests_failed_count();
}
