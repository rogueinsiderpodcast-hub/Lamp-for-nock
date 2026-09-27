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
 * its own business, and a 128-character line is a fixed amount of stack whether
 * or not anyone has typed it yet.
 *
 * The two limits below are real and both are checked.  A formula a person types
 * is nowhere near 32 open brackets, and a line is nowhere near 129 characters --
 * but a machine that reads text off a wire has to say so rather than walk off
 * the end of an array, and the first version of this file would have.
 */

/* 32 open brackets.  A 128-character line can nest 64 deep, so this is a limit
 * a long enough line can reach, and it is refused by name. */
#define GB_PARSE_MAX_DEPTH 32

/* One item needs at least one character, so a line cannot hold more items than
 * it has characters.  Both limits are fixed so the parser is a noun array and a
 * frame array on the stack, with no allocation and nothing to free. */
#define GB_PARSE_MAX_ITEMS 128

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
#define GB_LINE_MAX 128

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

/* Everything that happens when a line is finished.  The byte buffer and the
 * echo above are the whole of the terminal handling; this is where the machine
 * starts being a machine. */
static void gb_submit(void)
{
    noun formula = 0;
    const char *why = "";

    serial_put_nl();

    int rc = gb_parse(gb_line, gb_line_len, &formula, &why);

    if (rc == GB_PARSE_EMPTY) {
        /* A blank line is not an error and not a noun.  Once the session exists
         * it is the request to be shown the session, and saying so now is honest
         * rather than a placeholder. */
        serial_puts("  (nothing typed; the session is not here yet)\n");
        return;
    }
    if (rc != GB_PARSE_OK) {
        serial_puts("  no: ");
        serial_puts(why);
        serial_put_nl();
        return;
    }

    /* The noun, and nothing else.  Whether it is a formula and what it means is
     * the next question, and guessing at it here would be the reader lying about
     * what it knows. */
    serial_puts("  that is the noun ");
    noun_print(formula);
    serial_put_nl();
}

/* One question the checklist can ask about the reader before anyone has typed
 * anything: does a line of text come back as the noun it is?
 *
 * The checklist is printed on the way into the guest book, and most of its lines
 * are claims this file cannot check.  This is one that can be, and it is the one
 * that matters, because everything the guest book will do is a noun somebody
 * typed. */
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
    serial_puts("  type a noun, in brackets, and press enter.  Ctrl-D leaves.\n");
    serial_puts("  > ");

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
    serial_puts("  leaving the guest book.  nothing was kept.\n");
    serial_put_nl();
}
