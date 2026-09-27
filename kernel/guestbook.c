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

/* Everything that happens when a line is finished.  The byte buffer and the
 * echo above are the whole of the terminal handling, and everything worth
 * reading is below: read a noun, hand it and the session to one formula, print
 * what came back. */
static void gb_submit(void)
{
    noun line = 0;
    const char *why = "";

    serial_put_nl();

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
    serial_puts("  the machine is finished, so the\n");
    serial_puts("  session goes with it.  nothing here is written down.\n");
    serial_put_nl();
}
