/* host-machine -- the machine's outside world, stood up on the host.
 *
 * A hosted build has no 16550 to talk to and no firmware to hand it a memory
 * map, so two things have to be answered for: the six serial calls the kernel
 * makes, and the three calls noun_init() makes when it wants somewhere to put
 * nouns.  This is both of those, and nothing else.
 *
 * Everything that *is* the point -- nouns, Nock, the primitives, the book, the
 * reader -- is the machine's own code, linked in and not reimplemented here, so
 * that "the compiler agrees with the machine" and "the proof agrees with the
 * machine" are checks against one machine rather than claims about two.
 *
 * The capture is here rather than in each tool because two tools want it: the
 * compiler's self-test compares the machine's own printed text against a
 * hand-written spelling, and the jet proofs read a definition back after the
 * machine has printed it.  A second copy of a serial line is the kind of thing
 * that stays identical right up to the day one of them is edited.
 *
 * capture_begin() makes every later serial write land in the buffer instead of
 * on the screen, and capture_end() hands it back.  The overflow flag -- set
 * when a capture outgrew GB_LINE_MAX and dropped characters -- is cleared with
 * the buffer, so a caller never has to clear it by hand.  A noun printed while
 * a capture is open is the machine's own noun_print writing into this buffer,
 * which is the point: the text under test is the text the machine would put on
 * the wire.
 */

#include <stdint.h>
#include <stdio.h>

#include "kernel.h"
#include "host-machine.h"

/* --- memory ----------------------------------------------------------------- */

static char      host_heap[4 * 1024 * 1024];
/* The heap starts one word in, and never at the very front, because an
 * allocator that hands out NULL for its first allocation is indistinguishable
 * from one that has run out -- and noun_init() reads NULL as failure and
 * carries on with a null arena, which crashes a long way from here.  The
 * machine's own memory.c gets this for free by pointing above the boot
 * parameters; a static array has nobody else to point above. */
static uintptr_t host_used = sizeof(void *);

u64 mem_heap_start(void) { return (u64)(uintptr_t)host_heap; }
u64 mem_heap_end(void)   { return (u64)(uintptr_t)host_heap + sizeof host_heap; }

void *mem_alloc(u64 size, u64 align)
{
    /* Offsets, not addresses: want and host_used are both measured from the
     * front of host_heap, and only the last line turns one into a pointer. */
    uintptr_t want = host_used + (align ? align - 1 : 0);

    want &= ~(uintptr_t)(align - 1);
    if (want + size > (uintptr_t)sizeof host_heap)
        return NULL;
    host_used = want + size;
    return host_heap + want;
}

/* --- serial ----------------------------------------------------------------- */

static char   cap_buf[GB_LINE_MAX + 2];
static size_t cap_len;
static int    cap_on;
static int    cap_over;

int  capture_over(void)
{
    return cap_over;
}

const char *capture_text(void)
{
    return cap_buf;
}

size_t capture_len(void)
{
    return cap_len;
}

void capture_begin(void)
{
    cap_len = 0;
    cap_over = 0;
    cap_on = 1;
}

void capture_end(void)
{
    cap_on = 0;
    cap_buf[cap_len] = '\0';
}

static void cap_putc(char c)
{
    if (!cap_on) {
        putchar(c);
        return;
    }
    if (cap_len + 1 >= sizeof cap_buf) {
        cap_over = 1;
        return;
    }
    cap_buf[cap_len++] = c;
}

void serial_putc(char c) { cap_putc(c); }

void serial_put_dec(u64 v)
{
    char buf[24];
    int  n = 0;

    if (v == 0) {
        cap_putc('0');
        return;
    }
    while (v > 0 && n < (int)sizeof buf) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n-- > 0)
        cap_putc(buf[n]);
}

void serial_puts(const char *s) { while (*s) cap_putc(*s++); }
void serial_put_nl(void)         { cap_putc('\n'); }

void serial_put_hex(u64 v) { serial_puts("0x"); serial_put_dec(v); }

/* Nothing is ever typed at a hosted build: the tools that want input read it
 * from the command line, and the machine's reader is tested against text it is
 * handed rather than text it is made to wait for. */
u8   serial_getc(void)     { return 0; }
