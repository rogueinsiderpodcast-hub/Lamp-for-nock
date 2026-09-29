/* The guest book: the first thing on the machine that answers.
 *
 * Step 2 adds one idea, and this file is where it lives: a session is a noun,
 * and recording something means building a bigger noun rather than changing a
 * smaller one.  Nothing here overwrites.  The arena only grows, so the history
 * of a session is literally still there, and the log is that history.
 *
 * The split of labour is deliberate and worth stating, because it is the whole
 * question of what this machine is:
 *
 *   - C does I/O and nothing else.  Reading a byte off the UART, echoing it,
 *     turning typed characters into a noun, and printing an answer.  That is
 *     the same category of work as the memory-map parser: it turns something
 *     on the wire into a noun, or a noun back into bytes.
 *
 *   - Nock does the rest.  Once a line is a noun, evaluating it, and
 *     everything the session remembers, are Nock formulas run by the
 *     interpreter from Step 1.  There is no library written in C, because a
 *     library written in C would make this a C program with a serial port
 *     rather than a Nock machine you can talk to.
 *
 * The interactive loop is the one part that cannot be tested from inside the
 * machine, because it waits on hardware.  It is therefore as thin as possible:
 * it reads a byte, and hands it to a function that has no idea a UART exists.
 * Everything worth testing lives behind that function, and the tests in
 * tests/guestbook-tests.c drive it with bytes fed in from C.
 */

#include "kernel.h"

/* --- the reader --------------------------------------------------------- */
/* Turn a typed line into a noun.
 *
 * This is the whole grammar, and it is the grammar of Nock's own surface
 * syntax: a noun is either a decimal atom, or a bracketed list of nouns.
 *
 *     0            the atom zero
 *     42           the atom forty-two
 *     [1 42]       the cell [1 42]
 *     [0 [1 2]]    the cell [0 [1 2]]
 *     [ [1 2] 3 ]  the same thing, with unhelpful spaces
 *
 * Three rules deserve naming, because each is a mistake a person makes on their
 * first line.  A line is *one* noun, so `1 2` is an error rather than two
 * results.  A bracket list is right-nested, so `[1 42 7]` is `[1 [42 7]]` and
 * not the four-headed thing the brackets look like.  And a number is read to the
 * first character that cannot be part of it, so `42]` is the atom 42 followed by
 * the end of a cell that was never opened.
 *
 * Every refusal says which of those it was.  A reader that says "no" is
 * useless; one that says "that is more than one noun; a line is one formula" can
 * be argued with.
 *
 * The parser's working state is a C array of nouns and a C array of frames, not
 * a noun built out of nouns, and that is not a contradiction of anything: a
 * frame is a local of the parser, not part of the session.  What the session
 * *is* is a noun; where the parser keeps its scratch while it works that out is
 * its own business, and a line of any permitted length is a fixed amount of
 * stack whether or not anyone has typed it yet.
 *
 * The reader's limits live in kernel.h, because they are its contract rather
 * than its business: the host compiler has to know the budget it is compiling
 * into, and the tests build their inputs from the numbers instead of hard-coding
 * some that rot.
 *
 * What the three mean, and why each is checked rather than trusted:
 *
 *   GB_LINE_MAX         the most characters one line may hold
 *   GB_PARSE_MAX_DEPTH  the most open brackets open at once
 *   GB_PARSE_MAX_ITEMS  the most items in one line
 *
 * They were sized for a person typing -- 128 characters and 32 brackets was
 * generous against that, because nobody types a formula a page long by hand --
 * and a compiler now writes these lines, which is the only reason they moved.
 * Every atom in a Nock formula is spelled out in brackets, so a two-term
 * expression costs more characters than it does terms, and a compiled expression
 * spends a page quickly.
 *
 * The items array is one noun per item and the frame array is one frame per open
 * bracket, both on the stack, with no allocation and nothing to free: 32KB and
 * 4KB, which the 1MB stack in boot.S is there for.  GB_PARSE_MAX_ITEMS cannot be
 * smaller than GB_LINE_MAX, because one item needs at least one character, so a
 * line of N characters cannot hold more than N items.  That is the invariant the
 * length check below rests on, and it is why the check is a check on length. */

/* One open bracket: where its items start in items[], and how many it has. */
struct gb_frame {
    u64 start;
    u64 count;
};

int gb_parse(const char *text, u64 len, noun *out, const char **why)
{
    /* The items of every open frame, left to right, and the frame stack that
     * says where each open frame's run begins.
     *
     * They are collected rather than consed as they arrive because Nock's
     * brackets are right-nested: [1 42 7] is [1 [42 7]], so the first item ends
     * up furthest from the front of the answer.  A forward pass cannot build that
     * by consing onto an accumulator -- consing forward gives [[1 42] 7], which
     * is a different noun -- and it cannot amend the tail as it goes either,
     * because a noun is never rewritten once built.  So a frame collects its
     * items and folds them from the right when it closes, which is the only
     * order that can be right and is also the clearest. */
    struct gb_frame frames[GB_PARSE_MAX_DEPTH];
    noun items[GB_PARSE_MAX_ITEMS];
    u64  nitems = 0;
    u64  depth = 0;
    u64  num = 0;
    int have_num = 0;

    *out = 0;
    *why = "";

    /* One item needs at least one character, so a line of GB_PARSE_MAX_ITEMS
     * characters cannot fill items[] beyond its end.  The check is here anyway,
     * because the limit is what the items array is sized for and it should be
     * the thing that says so rather than a scribble past the end of a stack. */
    if (len > GB_PARSE_MAX_ITEMS) {
        *why = "that line is too long";
        return GB_PARSE_ERROR;
    }

    /* The parser builds nouns, and noun_cons refuses to build on a machine that
     * has crashed.  A crash from an earlier line is not this line's fault, so
     * start clean and say so. */
    machine_reset_error();

    /* frames[0] is the implicit outermost cell, and is open from the first
     * character, so it needs setting up here rather than on the first "[".  A
     * line with no brackets in it accumulates straight into it, and it is the
     * only frame left over at the end. */
    frames[0].start = 0;
    frames[0].count = 0;

    /* One finished noun, added to the frame currently open. */
    #define GB_ADD(v) do { items[nitems++] = (v); frames[depth].count++; } while (0)

    for (u64 i = 0; i < len; i++) {
        char c = text[i];

        if (c >= '0' && c <= '9') {
            u64 d = (u64)(c - '0');
            /* Checked before the multiply, so the largest atom cannot be
             * produced by wrapping round a smaller one. */
            if (num > (NOUN_ATOM_MAX - d) / 10) {
                *why = "that number is too large for a noun";
                return GB_PARSE_ERROR;
            }
            num = num * 10 + d;
            have_num = 1;
            continue;
        }

        /* A digit run ends at any delimiter, and finishing it in one place is
         * what keeps an unexpected character from leaving a number behind. */
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '[' || c == ']') {
            if (have_num) {
                GB_ADD(noun_atom(num));
                have_num = 0;
                num = 0;
            }
        }

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;

        if (c == '[') {
            if (depth + 1 >= GB_PARSE_MAX_DEPTH) {
                *why = "too many [ in one line";
                return GB_PARSE_ERROR;
            }
            frames[depth + 1].start = nitems;
            frames[depth + 1].count = 0;
            depth++;
            continue;
        }

        if (c == ']') {
            /* Checked before anything is read or added, so a ] with a number
             * still waiting cannot reach past the bottom of the stack. */
            if (depth == 0) {
                *why = "a ] with nothing open";
                return GB_PARSE_ERROR;
            }
            /* A frame with no items in it is an empty cell, which is not a
             * noun this machine has: [] is an error, not 0. */
            if (frames[depth].count == 0) {
                *why = "[] is not a noun: a cell needs two sides";
                return GB_PARSE_ERROR;
            }
            /* Fold the frame's items right-nested, last one first. */
            u64 first = frames[depth].start;
            u64 last  = nitems - 1;
            noun cell = items[last];
            for (u64 k = last; k > first; k--)
                cell = noun_cons(items[k - 1], cell);
            /* The frame's items are the tail of the run the frame below it was
             * collecting, so closing the frame is just forgetting it and putting
             * the answer where the items were. */
            depth--;
            nitems = frames[depth].start + frames[depth].count;
            GB_ADD(cell);
            continue;
        }

        *why = "expected a digit, a space, [ or ]";
        return GB_PARSE_ERROR;
    }

    if (have_num)
        GB_ADD(noun_atom(num));

    if (depth != 0) {
        *why = "a [ was never closed";
        return GB_PARSE_ERROR;
    }

    #undef GB_ADD

    if (frames[0].count == 0) {
        *why = "nothing typed";
        return GB_PARSE_EMPTY;
    }
    if (frames[0].count > 1) {
        *why = "that is more than one noun; a line is one formula";
        return GB_PARSE_ERROR;
    }

    *out = frames[0].count == 1 ? items[0] : 0;
    return GB_PARSE_OK;
}

/* Long enough for any formula a person will type, short enough that the
 * buffer is a compile-time constant and not an allocation.  Overflowing it
 * drops characters at the end rather than refusing the line: a line too long
 * to hold is a line the typist can see was truncated. */

/* Ctrl-D leaves.  On a real terminal this is what end-of-file looks like from
 * the far end of a serial line, and it is the only way out, so that
 *
 *     printf '[1 42]\004' | make run
 *
 * behaves the same as a person pressing Ctrl-D.  There is no other exit: the
 * machine has no signals, no shutdown, and no way to be told to stop except by
 * being fed a byte. */
#define GB_END_OF_INPUT 0x04

static char gb_line[GB_LINE_MAX];
static u64  gb_line_len;

static void gb_line_reset(void)
{
    gb_line_len = 0;
}

static void gb_line_back(void)
{
    if (gb_line_len == 0)
        return;
    gb_line_len--;
    /* Erase it on the terminal: back, space, back.  The space is not
     * decoration -- without it the cursor sits on a character that is still
     * there. */
    serial_putc('\b');
    serial_putc(' ');
    serial_putc('\b');
}

static void gb_line_push(u8 c)
{
    if (gb_line_len >= GB_LINE_MAX - 1)
        return;
    gb_line[gb_line_len++] = (char)c;
    serial_putc((char)c);
}

/* The session, in one noun.  It is the only state the guest book has, and it
 * lives here rather than in the C around it: the loop below has no variables of
 * its own to speak of. */
static noun gb_session;

static void gb_show_session(void);

/* The first character of a record.  Named here because the submit path needs it
 * to tell a record from a line, and the section below is where it is written
 * down what one looks like. */
#define GB_RECORD '%'

/* The first character of a rule.  A rule is a Nock definition sent as text
 * that claims to be a primitive; it is checked exhaustively over a bounded
 * domain before it may be used.  decisions.md item 26. */
#define GB_RULE '!'

/* One record, written to the wire and read back from it, and the restore that
 * reads it.  Both are below; these are here because gb_submit writes one and
 * reads one. */
static void gb_record(noun line, noun answer);
static void gb_restore(void);

/* A rule line, and the checking it lives or dies by.  gb_submit routes to it
 * the way it routes a % to gb_restore. */
static void gb_rule(void);

/* Everything that happens when a line is finished.  The byte buffer and the
 * echo above are the whole of the terminal handling, and everything worth
 * reading is below: read a noun, hand it and the session to one formula, print
 * what came back. */
static void gb_submit(void)
{
    noun line = 0;
    const char *why = "";

    serial_put_nl();

    /* A line from the notebook rather than from a person.  The check is on the
     * first character that is not a space, because the host's records start with
     * one and a person pasting one in from a terminal may not. */
    u64 first = 0;
    while (first < gb_line_len && gb_line[first] == ' ')
        first++;
    if (first < gb_line_len && gb_line[first] == GB_RECORD) {
        gb_restore();
        return;
    }
    if (first < gb_line_len && gb_line[first] == GB_RULE) {
        gb_rule();
        return;
    }

    int rc = gb_parse(gb_line, gb_line_len, &line, &why);

    if (rc == GB_PARSE_EMPTY) {
        gb_show_session();
        return;
    }
    if (rc != GB_PARSE_OK) {
        serial_puts("  no: ");
        serial_puts(why);
        serial_put_nl();
        return;
    }

    noun next = 0;
    int code = gb_step(line, gb_session, &next);

    if (code != NOCK_OK) {
        /* The session is not touched.  The book builds a new noun and never
         * edits the old one, so a line that crashed leaves nothing to undo --
         * and a machine that lost its history to a typo would be a machine
         * worth distrusting. */
        if (code == NOCK_STEPS_OUT)
            serial_puts("  it ran out of steps.\n");
        else {
            serial_puts("  it crashed: ");
            serial_puts(nock_crash_reason());
            serial_puts("\n");
        }
        serial_puts("  the session is as it was.\n");
        /* The machine is not crashed: the line was, and the reason has been
         * said.  Leaving the error set would silence every noun_print from here
         * on, because the printer stops the moment the machine has crashed. */
        machine_reset_error();
        return;
    }

    gb_session = next;

    serial_puts("  ");
    noun_print(gb_last(gb_session));
    serial_puts("  (");
    noun_print(gb_count(gb_session));
    serial_puts(" so far)\n");

    /* Step 4: the record goes out now, while the line that caused it is the
     * newest thing that has happened, and not at exit.  A notebook that is
     * written on the way out is a save file, and a save file is what a power
     * cut takes; a notebook written as it goes is a prefix of the session at
     * every moment, and the worst case is a cut between this line running and
     * these characters reaching the host. */
    gb_record(gb_entry_line(gb_log_front(gb_log(gb_session))), gb_last(gb_session));
}

/* --- the notebook --------------------------------------------------------- */
/* One record is the formula that ran, a colon, and what it answered:
 *
 *     % [1 42 0]: 42
 *
 * Noun text is digits, spaces and brackets, so a colon cannot be inside either
 * half and the first colon splits the line.  The host finds the records in the
 * stream with a `grep` for `^% `, which is why the percent is there and not a
 * bracket: a record has to be findable without parsing the guest's output, and
 * a person has to be able to read the file.  decisions.md item 25. */

static void gb_record(noun line, noun answer)
{
    serial_puts("% ");
    noun_print(line);
    serial_puts(": ");
    noun_print(answer);
    serial_put_nl();
}

/* Feed a record back.  A record is run against the session exactly as a typed
 * line is, so restore is the ordinary loop and not a second path through the
 * book, and the answer it produces is compared with the answer the notebook
 * wrote down: the machine is deterministic, so a record that does not reproduce
 * is a notebook that has been changed since it was written, and the session is
 * left alone rather than restored from it.  Nothing is echoed back as a record,
 * because the host appends every record it sees and a notebook that doubles
 * itself on each boot is not a notebook. */
static void gb_restore(void)
{
    u64 sep = 0;

    while (sep < gb_line_len && gb_line[sep] != ':')
        sep++;
    if (sep == gb_line_len) {
        serial_puts("  that record has no colon in it.  a record is a formula, a\n");
        serial_puts("  colon, and the answer it gave: % [1 42 0]: 42\n");
        return;
    }

    noun line = 0, said = 0;
    const char *why = "";

    if (gb_parse(gb_line + 1, sep - 1, &line, &why) != GB_PARSE_OK) {
        serial_puts("  that record has no formula in it: ");
        serial_puts(why);
        serial_put_nl();
        return;
    }
    if (gb_parse(gb_line + sep + 1, gb_line_len - sep - 1, &said, &why) != GB_PARSE_OK) {
        serial_puts("  that record has no answer in it: ");
        serial_puts(why);
        serial_put_nl();
        return;
    }

    noun next = 0;
    int code = gb_step(line, gb_session, &next);

    if (code != NOCK_OK) {
        /* The same three lines gb_submit has, said about a record rather than a
         * line, because a notebook that cannot be replayed is a broken notebook
         * and the reason belongs in the log where the person can see it. */
        if (code == NOCK_STEPS_OUT)
            serial_puts("  that record ran out of steps.\n");
        else {
            serial_puts("  that record crashed: ");
            serial_puts(nock_crash_reason());
            serial_puts("\n");
        }
        serial_puts("  the session is as it was.\n");
        machine_reset_error();
        return;
    }

    if (!noun_equal(gb_last(next), said)) {
        serial_puts("  that record does not say what it answered.  it says ");
        noun_print(said);
        serial_puts(", and it answers ");
        noun_print(gb_last(next));
        serial_puts(".  the session is left alone,\n");
        serial_puts("  because a session restored from a notebook somebody has\n");
        serial_puts("  edited by hand is worse than one that was never restored.\n");
        machine_reset_error();
        return;
    }

    gb_session = next;
    serial_puts("  restored ");
    noun_print(gb_count(gb_session));
    serial_puts(", answering ");
    noun_print(gb_last(gb_session));
    serial_put_nl();
}

/* --- the rules (Step 5) --------------------------------------------------- */
/* A rule line is `! 0 <definition>`: a primitive's bank index and a Nock
 * formula that claims to be that primitive.  The machine checks the claim
 * before it may be used -- every pair in the primitive's certified domain,
 * run by the machine's own interpreter and compared with the native by
 * noun_equal -- and refuses by name, with the first pair that failed, any
 * definition that stops, answers a cell, disagrees, or tries to answer by
 * calling the primitive it defines.  decisions.md item 26.
 *
 * The notebook records a rule the way it records a line, with ! where a
 * session record has %:
 *
 *     ! 0 <definition>: 64
 *
 * so a rule survives a power cut and is re-verified-- never believed -- on the
 * next boot.  A line with a colon in it is therefore a record being replayed
 * and is not echoed as a record again; a rule typed in by hand has no colon
 * and is echoed, exactly as a session line is.  A record is genuine only if
 * the domain it claims is the domain the machine certifies. */

static const char *gb_colon_in(u64 from)
{
    for (u64 i = from; i < gb_line_len; i++)
        if (gb_line[i] == ':')
            return gb_line + i;
    return NULL;
}

/* What a primitive's domain is, in the machine's own words.  The shape is part
 * of the claim: a number alone would read the same for a sum and a product.
 * (decisions.md item 28.) */
static void gb_print_domain(int index, u64 limit)
{
    switch (prim_rule_shape(index)) {
    case RULE_SHAPE_SUM:
        serial_puts("a + b < ");
        serial_put_dec(limit);
        break;
    case RULE_SHAPE_PRODUCT:
        serial_puts("a * b < ");
        serial_put_dec(limit);
        serial_puts(" with a and b each under ");
        serial_put_dec(limit);
        break;
    default:
        serial_puts("none");
        break;
    }
}

/* The machine says what a primitive currently is, and how its two possible
 * answers have actually divided the probes.  `!` alone reports every primitive
 * with a certified domain; `! 0` reports only the one. */
static void gb_rule_report(int index)
{
    if (index < 0) {
        for (int i = 0; i < prim_count(); i++)
            if (prim_rule_domain(i, NULL))
                gb_rule_report(i);
        return;
    }
    const prim_entry *e = prim_get(index);
    u64  limit = 0;
    int  has = prim_rule_domain(index, &limit);
    noun def = 0;
    int  installed = prim_rule_state(index, &def);

    serial_puts("  ");
    serial_puts(e != NULL ? e->name : "?");
    if (!has) {
        serial_puts(" has no certified domain, so no rule is accepted for it.\n");
        return;
    }
    serial_puts(": domain ");
    gb_print_domain(index, limit);
    serial_puts(".  ");
    serial_puts(installed ? "the rule is installed" : "the C native answers");
    serial_puts(";  the rule has answered ");
    serial_put_dec(prim_rule_runs(index));
    serial_puts(" probes, the native ");
    serial_put_dec(prim_native_runs(index));
    serial_puts(".\n");
}

/* The battery: every pair the primitive's domain contains, once, in order, with
 * the pairs counted by the shape of the domain rather than by a formula -- the
 * count the machine prints is the count it ran.  Prints a dot every 256 pairs so
 * a check that is doing the honest thing can be watched doing it, and says, on
 * the first pair that failed, exactly what failed.  The definition runs with the
 * machine's own reader and its own interpreter under the gate, so a rule that
 * tries to answer by calling the primitive it defines has its probe declined and
 * its own fallback judged instead.  Leaves the machine's error state clear,
 * because a refusal is not a crash. */
static int gb_rule_verify(int index, noun def, u64 *pairs)
{
    const prim_entry *e = prim_get(index);
    u64 limit = 0;

    if (!prim_rule_domain(index, &limit))
        return 0;

    u64 done = 0;
    for (u64 a = 0; a < limit; a++) {
        for (u64 b = 0; b < limit; b++) {
            /* For a fixed a, both domains hold for b up to a bound and not past
             * it -- the sum's is a + b < limit, the product's is a * b < limit --
             * so the first pair outside the domain ends this a's row. */
            if (!prim_rule_in_domain(index, a, b))
                break;
            if (done != 0 && (done & 255u) == 0)
                serial_putc('.');
            done++;

            machine_reset_error();
            noun subject = noun_cons(noun_atom(a),
                                     noun_cons(noun_atom(b), noun_atom(0)));
            noun out = 0;
            prim_rule_gate(index);
            int rc = nock_run(subject, def, &out);
            prim_rule_ungate();

            if (rc != NOCK_OK) {
                const char *reason = nock_crash_reason();
                machine_reset_error();
                serial_puts("  no.  the definition stopped at ");
                serial_puts(e->name);
                serial_putc('(');
                serial_put_dec(a);
                serial_puts(", ");
                serial_put_dec(b);
                serial_puts("): ");
                serial_puts(reason);
                serial_put_nl();
                return 0;
            }
            if (!noun_is_atom(out)) {
                machine_reset_error();
                serial_puts("  no.  the definition answered ");
                serial_puts(e->name);
                serial_putc('(');
                serial_put_dec(a);
                serial_puts(", ");
                serial_put_dec(b);
                serial_puts(") with a cell.\n");
                return 0;
            }

            machine_reset_error();
            u64 nat = prim_call(index, a, b);
            if (machine_err) {
                machine_reset_error();
                serial_puts("  no.  the native itself stopped at ");
                serial_puts(e->name);
                serial_putc('(');
                serial_put_dec(a);
                serial_puts(", ");
                serial_put_dec(b);
                serial_puts("): ");
                serial_puts(nock_crash_reason());
                serial_put_nl();
                return 0;
            }
            if (noun_atom_val(out) != nat) {
                serial_puts("  no.  the definition said ");
                serial_puts(e->name);
                serial_putc('(');
                serial_put_dec(a);
                serial_puts(", ");
                serial_put_dec(b);
                serial_puts(") is ");
                serial_put_dec(noun_atom_val(out));
                serial_puts("; the native says ");
                serial_put_dec(nat);
                serial_puts(".\n");
                return 0;
            }
        }
    }
    if (pairs != NULL)
        *pairs = done;
    return 1;
}

static void gb_rule(void)
{
    u64 at = 0;
    while (at < gb_line_len && gb_line[at] == ' ')
        at++;
    if (at >= gb_line_len || gb_line[at] != GB_RULE)
        return;
    at++;

    while (at < gb_line_len && gb_line[at] == ' ')
        at++;
    if (at >= gb_line_len) {
        gb_rule_report(-1);
        return;
    }

    if (gb_line[at] < '0' || gb_line[at] > '9') {
        serial_puts("  no.  a rule names a primitive by its index number:\n");
        serial_puts("  ! 0 <definition> is a rule claiming to be +add.\n");
        return;
    }

    u64 index = 0;
    while (at < gb_line_len && gb_line[at] >= '0' && gb_line[at] <= '9') {
        u64 d = (u64)(gb_line[at] - '0');
        if (index > (NOUN_ATOM_MAX - d) / 10) {
            serial_puts("  no.  that index is too large for a noun.\n");
            return;
        }
        index = index * 10 + d;
        at++;
    }
    if (index >= (u64)prim_count()) {
        serial_puts("  no.  the bank has primitives 0 to ");
        serial_put_dec((u64)(prim_count() - 1));
        serial_puts(", not ");
        serial_put_dec(index);
        serial_puts(".\n");
        return;
    }

    while (at < gb_line_len && gb_line[at] == ' ')
        at++;
    if (at >= gb_line_len) {
        gb_rule_report((int)index);
        return;
    }

    /* Everything from here is the definition, split by the first colon if there
     * is one: a colon makes the line a record being replayed, claiming the
     * domain as its answer. */
    const char *colon = gb_colon_in(at);
    u64 end = colon != NULL ? (u64)(colon - gb_line) : gb_line_len;

    while (at < end && gb_line[at] == ' ')
        at++;
    if (at >= end) {
        serial_puts("  no.  a rule needs a definition: ! 0 <definition>\n");
        return;
    }

    noun def = 0;
    const char *why = "";
    if (gb_parse(gb_line + at, end - at, &def, &why) != GB_PARSE_OK) {
        serial_puts("  no.  the definition is not a noun the reader takes: ");
        serial_puts(why);
        serial_put_nl();
        return;
    }

    /* The atom 0 is the empty definition: it is not a formula, so there was
     * never a rule that 0 could be, and the row a primitive is born with is
     * definition 0 over limit 0.  `! 0 0` therefore puts the rule away --
     * the machine writes the row back to the state it booted into.  The
     * record's answer half is 0 too, because a removal claims nothing, and
     * the record is the same claim: answer shape as everything else. */
    if (noun_is_atom(def) && noun_atom_val(def) == 0) {
        if (colon != NULL) {
            noun claimed = 0;
            if (gb_parse(colon + 1, gb_line_len - (u64)(colon + 1 - gb_line),
                         &claimed, &why) != GB_PARSE_OK) {
                serial_puts("  no.  the record's domain is not a noun the reader takes: ");
                serial_puts(why);
                serial_put_nl();
                return;
            }
            if (!noun_is_atom(claimed) || noun_atom_val(claimed) != 0) {
                serial_puts("  no.  to remove a rule, the record is ! ");
                serial_put_dec(index);
                serial_puts(" 0: 0 --\n");
                serial_puts("  nothing claimed nothing certified.\n");
                return;
            }
        }
        if (!prim_rule_state((int)index, NULL)) {
            serial_puts("  no.  there is no rule for ");
            serial_puts(prim_get((int)index)->name);
            serial_puts(" to remove.\n");
            return;
        }
        prim_rule_set((int)index, 0);
        serial_puts("  yes.  ");
        serial_puts(prim_get((int)index)->name);
        serial_puts(" is no longer a rule; the C native answers\n");
        serial_puts("  everywhere again.\n");
        if (colon == NULL) {
            serial_puts("! ");
            serial_put_dec(index);
            serial_puts(" 0: 0\n");
        }
        return;
    }
    if (noun_is_atom(def)) {
        serial_puts("  no.  a definition has to be a formula, which is a cell,\n");
        serial_puts("  and an atom is nothing to run.\n");
        return;
    }

    if (colon != NULL) {
        noun claimed = 0;
        if (gb_parse(colon + 1, gb_line_len - (u64)(colon + 1 - gb_line),
                     &claimed, &why) != GB_PARSE_OK) {
            serial_puts("  no.  the record's domain is not a noun the reader takes: ");
            serial_puts(why);
            serial_put_nl();
            return;
        }
        u64 limit = 0;
        if (!prim_rule_domain((int)index, &limit)) {
            serial_puts("  no.  this primitive has no certified domain at all.\n");
            return;
        }
        if (!noun_is_atom(claimed) || noun_atom_val(claimed) != limit) {
            serial_puts("  no.  the record claims a domain the machine does not\n");
            serial_puts("  certify: a rule is only what the machine checked.\n");
            return;
        }
    }

    u64 pairs = 0;
    if (!gb_rule_verify((int)index, def, &pairs))
        return;

    prim_rule_set((int)index, def);

    serial_puts("  yes.  ");
    serial_puts(prim_get((int)index)->name);
    serial_puts(" is now a rule, sent as text and checked in full: ");
    u64 limit = 0;
    prim_rule_domain((int)index, &limit);
    serial_put_dec(pairs);
    serial_puts(" pairs with ");
    gb_print_domain((int)index, limit);
    serial_puts(", and it\n");
    serial_puts("  answered the native on every one.  for those pairs the\n");
    serial_puts("  machine answers without its C ");
    serial_puts(prim_get((int)index)->name);
    serial_puts(".\n");

    /* A record being replayed is not echoed; a rule typed in by hand is, so the
     * notebook holds it and a later boot can re-verify it. */
    if (colon == NULL) {
        serial_putc('!');
        serial_putc(' ');
        serial_put_dec(index);
        serial_putc(' ');
        noun_print(def);
        serial_puts(": ");
        serial_put_dec(limit);
        serial_put_nl();
    }
}

/* The checklist's question about rules: is a rule checked exhaustively over its
 * whole domain before it is used, is a definition that lies or stops or calls
 * the thing it defines refused by name, and -- once a rule is installed -- does
 * the machine answer within the domain by the definition and not by the C
 * native, and outside it by the C native?  The battery below is the same one a
 * `!` line runs; the checklist runs it on the real definition once, on every
 * boot. */
int gb_rules_ok(void)
{
    /* The refusal suite: a lie is refused by the pair it disagreed on, a
     * definition that stops is refused by what it stopped at, and a definition
     * that tries to answer by calling the primitive it defines is refused
     * because its probe was declined and its own fallback was judged.  The
     * suite is the same for every rule primitive: what it tests is the record
     * and the gate, not the arithmetic behind them. */
    static const char wrong[]  = "[1 3 0]"; /* the constant 3: disagrees at (0, 0) */
    static const char stopped[] = "[0 0 0]"; /* axis 0: a path that names nothing */
    static const char selfdef[] = "[11 [0 [1 [[2 3] 0]]] [0 2 0] 0]"; /* hints +add(2,3), replies a */
    static const char adddef[] =
        "[9 [126 [[10 [[126 [1 [[6 [[5 [[0 [62 0]] [[1 [0 0]] 0]]] [[6 [[5 [[0 [14 0]] [[0 [2 0]] 0]]] [[9 [126 [[10 [[126 [0 [126 0]]] [[10 [[62 [1 [1 0]]] [[10 [[30 [0 [30 0]]] [[10 [[14 [1 [0 0]]] [[10 [[6 [0 [6 0]]] [[10 [[2 [0 [2 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] [[9 [126 [[10 [[126 [0 [126 0]]] [[10 [[62 [1 [0 0]]] [[10 [[30 [4 [[0 [30 0]] 0]]] [[10 [[14 [4 [[0 [14 0]] 0]]] [[10 [[6 [0 [6 0]]] [[10 [[2 [0 [2 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]]] [[6 [[5 [[0 [14 0]] [[0 [6 0]] 0]]] [[0 [30 0]] [[9 [126 [[10 [[126 [0 [126 0]]] [[10 [[62 [1 [1 0]]] [[10 [[30 [4 [[0 [30 0]] 0]]] [[10 [[14 [4 [[0 [14 0]] 0]]] [[10 [[6 [0 [6 0]]] [[10 [[2 [0 [2 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]]] 0]]]] 0]]] [[10 [[62 [1 [0 0]]] [[10 [[30 [1 [0 0]]] [[10 [[14 [1 [0 0]]] [[10 [[6 [0 [6 0]]] [[10 [[2 [0 [2 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]]";
    static const char muldef[] =
        "[9 [126 [[10 [[126 [1 [[6 [[5 [[0 [14 0]] [[0 [6 0]] 0]]] [[0 [62 0]] [[6 [[5 [[0 [30 0]] [[0 [2 0]] 0]]] [[9 [126 [[10 [[126 [0 [126 0]]] [[10 [[2 [0 [2 0]]] [[10 [[6 [0 [6 0]]] [[10 [[14 [4 [[0 [14 0]] 0]]] [[10 [[30 [1 [0 0]]] [[10 [[62 [0 [62 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] [[9 [126 [[10 [[126 [0 [126 0]]] [[10 [[2 [0 [2 0]]] [[10 [[6 [0 [6 0]]] [[10 [[14 [0 [14 0]]] [[10 [[30 [4 [[0 [30 0]] 0]]] [[10 [[62 [4 [[0 [62 0]] 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]]] 0]]]] 0]]] [[10 [[62 [1 [0 0]]] [[10 [[30 [1 [0 0]]] [[10 [[14 [1 [0 0]]] [[10 [[6 [0 [6 0]]] [[10 [[2 [0 [2 0]]] [[1 [[0 [0 [0 [0 [0 [0 0]]]]]] 0]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]] 0]]]";

    /* One row per rule primitive: the domain it claims, the definition that is
     * its own, and a pair inside and a pair outside that domain.  The two
     * definitions are different arithmetic on differently shaped domains; every
     * other thing the checklist asks of a rule is asked of both. */
    static const struct {
        int         index;
        u64         limit;
        const char *def;
        u64         def_len;
        u64         in_a, in_b, in_want;
        u64         out_a, out_b, out_want;
    } rules[] = {
        { 0, RULE_ADD_LIMIT, adddef, sizeof adddef - 1,  3, 4,     7, 2000, 1000,    3000 },
        { 2, RULE_MUL_LIMIT, muldef, sizeof muldef - 1,  3, 4,    12, 2000, 1000, 2000000 },
    };

    noun a, b, c, def;
    const char *why = "";
    int all = 1;
    u64 pairs = 0;

    for (unsigned r = 0; r < sizeof rules / sizeof rules[0]; r++) {
        int index = rules[r].index;
        u64 limit = 0;

        if (!prim_rule_domain(index, &limit) || limit != rules[r].limit)
            all = 0;
        if (prim_rule_state(index, NULL))
            all = 0;                          /* nothing installed at boot */

        if (gb_parse(wrong, sizeof wrong - 1, &a, &why) != GB_PARSE_OK)
            all = 0;
        if (all && gb_rule_verify(index, a, &pairs))   /* the lie: fails at (0, 0) */
            all = 0;
        if (all && prim_rule_state(index, NULL))
            all = 0;

        if (gb_parse(stopped, sizeof stopped - 1, &b, &why) != GB_PARSE_OK)
            all = 0;
        if (all && gb_rule_verify(index, b, &pairs))   /* axis 0: stops at once */
            all = 0;

        if (gb_parse(selfdef, sizeof selfdef - 1, &c, &why) != GB_PARSE_OK)
            all = 0;
        if (all && gb_rule_verify(index, c, &pairs))   /* the self-call: fallback judged */
            all = 0;

        if (gb_parse(rules[r].def, rules[r].def_len, &def, &why) != GB_PARSE_OK)
            all = 0;
        if (all && !gb_rule_verify(index, def, &pairs)) /* the real definition passes */
            all = 0;
        if (!all)
            continue;

        prim_rule_set(index, def);

        /* The counters count for the machine's whole life, so the probes are
         * judged by what they move: the first must answer by the definition and
         * move nothing else, the second by the native. */
        u64 rt0 = prim_rule_runs(index);
        u64 nt0 = prim_native_runs(index);

        u64 res = 0;
        machine_reset_error();
        if (prim_rule_probe(index, rules[r].in_a, rules[r].in_b, &res) != 0
            || res != rules[r].in_want)
            all = 0;
        if (prim_rule_runs(index) != rt0 + 1 || prim_native_runs(index) != nt0)
            all = 0;

        machine_reset_error();
        res = 0;
        if (prim_rule_probe(index, rules[r].out_a, rules[r].out_b, &res) != 0
            || res != rules[r].out_want)
            all = 0;                          /* outside the domain: the native */
        if (prim_native_runs(index) != nt0 + 1)
            all = 0;

        /* Removal (item 27): the empty definition takes the rule back down to
         * the row it was born with, and a probe inside the former domain must
         * then be answered by the native -- the counters are the proof, since
         * a removed rule cannot tell the two paths apart by value. */
        prim_rule_set(index, 0);
        if (prim_rule_state(index, NULL))
            all = 0;
        u64 rt1 = prim_rule_runs(index);
        u64 nt1 = prim_native_runs(index);
        machine_reset_error();
        res = 0;
        if (prim_rule_probe(index, rules[r].in_a, rules[r].in_b, &res) != 0
            || res != rules[r].in_want)
            all = 0;
        if (prim_rule_runs(index) != rt1 || prim_native_runs(index) != nt1 + 1)
            all = 0;
        prim_rule_set(index, def);            /* and put it back, as a boot lands */
    }
    machine_reset_error();
    return all;
}

/* A blank line asks to be shown the session.  The log is walked here rather
 * than by a formula, and the reason is worth stating: Nock cannot loop, so a
 * formula can reach the newest entry but cannot count them.  Walking to print
 * is I/O, which is the one thing C is for, and nothing is decided on the way
 * past. */
static void gb_show_session(void)
{
    noun l = gb_log(gb_session);
    u64  n = 0;

    serial_puts("  the session, newest first:\n");
    while (noun_is_cell(l)) {
        noun entry = gb_log_front(l);
        serial_puts("    ");
        noun_print(gb_entry_line(entry));
        serial_puts(" answered ");
        noun_print(gb_entry_answer(entry));
        serial_put_nl();
        l = gb_log_rest(l);
        n++;
    }
    if (n == 0)
        serial_puts("    (nothing has been run yet)\n");

    serial_puts("  ");
    noun_print(gb_count(gb_session));
    if (n == 0) {
        serial_puts(" lines, none run\n");
    } else {
        serial_puts(n == 1 ? " entry, last answer " : " entries, last answer ");
        noun_print(gb_last(gb_session));
        serial_put_nl();
    }
}

/* The same question about the book: does a formula typed at the machine run,
 * and does what it leaves behind matter?  Beside gb_reader_ok because these two
 * are the checklist's whole claim about the guest book, and a claim is only
 * worth making if it is worth checking. */
int gb_session_ok(void)
{
    static const char first[]  = "[1 42 0]";
    static const char second[] = "[0 14 0]";
    static const char broken[] = "[1 2]";
    noun one = 0;
    noun two = 0;
    noun s1  = 0;
    noun s2  = 0;
    noun out = 0;
    const char *why = "";

    if (gb_parse(first, sizeof first - 1, &one, &why) != GB_PARSE_OK)
        return 0;

    /* [1 42 0] is the constant 42, so the answer is 42 and there is one entry. */
    if (gb_step(one, gb_empty_session(), &s1) != NOCK_OK)
        return 0;
    if (!noun_equal(gb_last(s1), noun_atom(42)) || !noun_equal(gb_count(s1), noun_atom(1)))
        return 0;

    /* A line asking how many lines have run is answered from before it was
     * asked -- the subject is the session as it was -- and asking still counts,
     * so there are two entries afterwards. */
    if (gb_parse(second, sizeof second - 1, &two, &why) != GB_PARSE_OK)
        return 0;
    if (gb_step(two, s1, &s2) != NOCK_OK)
        return 0;
    if (!noun_equal(gb_last(s2), noun_atom(1)) || !noun_equal(gb_count(s2), noun_atom(2)))
        return 0;

    /* A line that breaks changes nothing, which is the part that makes the
     * history worth keeping. */
    if (gb_parse(broken, sizeof broken - 1, &two, &why) != GB_PARSE_OK)
        return 0;
    if (gb_step(two, s2, &out) == NOCK_OK)
        return 0;
    machine_reset_error();

    return out == 0
        && noun_equal(gb_count(s2), noun_atom(2))
        && noun_equal(gb_last(s2), noun_atom(1));
}

/* One question the checklist can ask about the reader before anyone has typed
 * anything: does a line of text come back as the noun it is?
 *
 * Most of the checklist's lines are claims this file cannot check.  This is one
 * that can be, and it is the one that matters, because everything the guest book
 * will do is a noun somebody typed. */
/* Step 4, checked on the machine and not only on the host.  A record is a
 * formula, a colon and an answer, and the claim is that a line printed by the
 * printer and read by the reader is the same noun -- the reader and the printer
 * have to agree, or a notebook full of records is a notebook full of garbage.
 * This builds the record the way gb_record writes it, parses both halves with
 * the reader the prompt uses, runs the formula half against the book, and asks
 * whether the answer it gets is the answer the record claims. */
int gb_journal_ok(void)
{
    static const char record[] = "% [1 42 0]: 42";

    noun line = 0, said = 0;
    const char *why = "";

    /* Everything after the % up to the colon is the formula. */
    const char *sep = record + 2;
    while (*sep && *sep != ':')
        sep++;

    if (gb_parse(record + 2, (u64)(sep - (record + 2)), &line, &why) != GB_PARSE_OK)
        return 0;
    if (gb_parse(sep + 1, sizeof record - 1 - (u64)(sep + 1 - record), &said, &why) != GB_PARSE_OK)
        return 0;
    if (!noun_equal(said, noun_atom(42)))
        return 0;

    /* And the formula half has to run to the answer the record claims, through
     * the same book the prompt uses, on an empty session. */
    noun out = 0;
    if (gb_step(line, gb_empty_session(), &out) != NOCK_OK) {
        machine_reset_error();
        return 0;
    }
    int same = noun_equal(gb_last(out), said);
    machine_reset_error();
    return same;
}

int gb_reader_ok(void)
{
    static const char text[] = "[1 42 0]";
    noun out = 0;
    const char *why = "";

    if (gb_parse(text, sizeof text - 1, &out, &why) != GB_PARSE_OK)
        return 0;
    /* [1 42 0] is the cell of 1 and the cell of 42 and 0, written the way Nock
     * writes it: a formula, with its opcode and a zero to end the arguments. */
    return noun_equal(out, noun_cons(noun_atom(1),
                                     noun_cons(noun_atom(42), noun_atom(0))));
}

void gb_run(void)
{
    serial_put_nl();
    serial_puts("== guest book\n");
    serial_puts("--------------------------------------------------------------\n");
    serial_puts("  type a formula and press enter.  A blank line shows the\n");
    serial_puts("  session.  Ctrl-D leaves.\n");
    serial_put_nl();
    serial_puts("  [1 42 0] is the constant 42.  [0 14 0] is how many lines\n");
    serial_puts("  have been run.  [0 8 0] is the last line typed.\n");
    serial_put_nl();
    serial_puts("  every line that runs is written down as a record, % formula\n");
    serial_puts("  and the answer it gave, and a line that starts with a % is a\n");
    serial_puts("  record from the notebook: it is run again, and checked against\n");
    serial_puts("  the answer written beside it.  `make notebook` keeps that file\n");
    serial_puts("  on the host and hands it back at the start of the next boot.\n");
    serial_put_nl();
    serial_puts("  a line that starts with ! is a rule: a formula that claims to\n");
    serial_puts("  be a primitive, checked exhaustively over the domain the machine\n");
    serial_puts("  certifies before it may be used.  ! 0 <definition> claims\n");
    serial_puts("  +add, ! <index> says what that primitive is right now, and\n");
    serial_puts("  ! <index> 0 puts its rule away.  the record of either is ! <i>\n");
    serial_puts("  <thing>: the domain it was checked over, or 0 for a removal.\n");
    serial_put_nl();
    serial_puts("  > ");

    gb_session = gb_empty_session();

    for (;;) {
        u8 c = serial_getc();

        if (c == GB_END_OF_INPUT)
            break;
        if (c == '\r' || c == '\n') {
            gb_submit();
            gb_line_reset();
            serial_puts("  > ");
            continue;
        }
        if (c == 0x08 || c == 0x7F) {
            gb_line_back();
            continue;
        }
        /* Anything else outside printable ASCII is dropped silently.  It is
         * either a control sequence meant for a terminal emulator or a typo,
         * and neither is worth an error message per byte. */
        if (c < 0x20 || c > 0x7E)
            continue;

        gb_line_push(c);
    }

    serial_put_nl();
    serial_puts("  leaving the guest book, with ");
    noun_print(gb_count(gb_session));
    serial_puts(noun_atom_val(gb_count(gb_session)) == 1 ? " line in it." : " lines in it.");
    serial_puts("\n");
    serial_puts("  every one of them was written down as it ran, so the\n");
    serial_puts("  notebook on the host already has them.  nothing is written\n");
    serial_puts("  now: a power cut costs the same as this exit, which is at\n");
    serial_puts("  most the line that was in flight.\n");
    serial_put_nl();
}
