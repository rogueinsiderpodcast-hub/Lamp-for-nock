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
    serial_put_nl();
    if (gb_line_len == 0) {
        serial_puts("  (nothing typed)\n");
        return;
    }
    serial_puts("  ");
    serial_put_dec(gb_line_len);
    serial_puts(" characters, not yet a noun\n");
}

void gb_run(void)
{
    serial_put_nl();
    serial_puts("== guest book\n");
    serial_puts("--------------------------------------------------------------\n");
    serial_puts("  type a formula and press enter.  Ctrl-D leaves.\n");
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
