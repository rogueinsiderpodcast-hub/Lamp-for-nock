/* hoon -- a compiler from a Hoon-shaped language to Nock, on the host.
 *
 * It reads one expression and writes one line of bracket text: the noun the
 * guest's reader already knows how to read.  No new transport, no new guest
 * code, no protocol -- that is the whole of decisions.md item 21.  The output
 * is a formula, so what comes back over the serial line is the interpreter's
 * own answer and not the host's opinion of one.
 *
 * THE LANGUAGE
 *
 * This is not Hoon.  It is a Hoon-shaped language that borrows Hoon's spelling
 * for a few things, and every difference is a decision rather than an accident:
 *
 *     42              an atom
 *     /14             the subject at that tree address
 *     ?(a)            0 if a is a cell, 1 if it is an atom
 *     =(a b)          0 if a and b are the same noun, 1 if not
 *     ~(c t e)        t if c is 0, e if c is 1
 *     *(a b)          call: a's value is the subject, b's value is the formula
 *     |(a b)          the three-word list [a b 0]
 *     +(a)            a plus one
 *     +(a b)          a plus b, and only when both are literals
 *     =+(a body)      push a's value onto the front of the subject, run body
 *
 * Hoon's `/14` is not an address written as a number, because this machine's
 * addresses are not a wing and spelling them as runes would hide the
 * arithmetic that book.c exists to get right.  `?`, `=` and `*` are not Hoon's
 * runes of those names, and `*` differs from Hoon's in the way that matters
 * here: Hoon's `*(a b)` runs the formula b on a's value, but on this machine
 * opcode 2 evaluates *both* of its arguments in the outer subject, and then
 * runs the second one's value on the first one's value.  So b is not a formula
 * written down, it is an expression whose result is one -- and since almost
 * everything in this language is a value, a call is usually written with `|`,
 * which is the one rune here that can make a formula out of two atoms.  See
 * the `*` case in the tests below for what that costs.
 *
 * `=+` is the one that means in Hoon what it means here, including the shift
 * it causes, and that shift is the thing in this file most likely to be got
 * wrong.
 *
 * WHAT IS REFUSED, AND WHY
 *
 * `+(a b)` for anything but two literals is refused by name, and that refusal
 * is the interesting part of this file.  Nock can add two values that are only
 * known at run time only by way of a loop over a core, and this machine has
 * twenty native integer operations that are a *tested native bank* and not
 * *proven jets* -- decisions.md item 9 says so, and says that proving each
 * against its Nock definition is the first job of the step after this one.  So
 * `+(2 3)` compiles, by folding on the host, and `+(a 2)` does not, and the
 * reason it gives is the reason the machine is not further along than it is.
 * A compiler that quietly emitted a native call here would be a compiler whose
 * answers nothing has ever checked.
 *
 * `=(a b)` has the same gap behind it and is not refused, because equality is
 * opcode 5 and needs no core.  The line between what the machine can do with
 * the rules it has and what it can only do by calling out to C is exactly the
 * line this language is drawn along.
 *
 * A cell is the other thing worth reading the code for.  Nock has no cons: it
 * can only edit, so a cell is built by editing a canned template of zeros, and
 * `|(a b)` is two edits where Hoon's `|` would be one word.  It is the same
 * trick book.c uses, at the same two addresses, and the tests here check that
 * this file and that one still agree about what a cell is.
 *
 * THE SUBJECT
 *
 * A typed line is run on the session, whose shape the compiler knows:
 *
 *     /2  the log      /6  the last answer   /8  the newest line
 *     /14 the count    /18 that line's answer
 *
 * `=+` pushes a value on the front, and every address inside the old subject
 * moves, because an address is a path and a push on the front is one more step
 * down the front of it.  So /2 becomes /6, /6 becomes /14 and /14 becomes /30,
 * which are book.c's AX_LOG, AX_LAST and AX_COUNT -- the check that the table in
 * this file and the constants in that one are the same table, since the book's
 * subject is a line pushed onto a session.
 *
 * It is worth being careful with the rest, because the shift looks like
 * arithmetic and is not: /8 goes to /24, /18 goes to /50, and the whole old
 * subject goes to /3.  Those are the addresses a line reaches for most, and
 * they are not the ones 2a + 2 gives -- 2a + 2 is right for /2, /6, /14 and /30
 * and wrong for every other address, which is the worst kind of rule to have,
 * because the three that work are the three book.c names.  See axis_shifted,
 * and the /24 case in the tests, which is where that was caught.
 *
 * An address that is not in the shape is refused rather than compiled, because
 * a wrong address on this machine usually names a real noun instead of
 * crashing -- it is the silent failure, and this language exists to make it
 * loud.  The refusal lists the addresses that are there.
 *
 * HOW THIS IS CHECKED
 *
 * The self-test links the machine's own noun.c, nock.c, primitives.c, book.c and
 * guestbook.c, so for every case it can do all three of these, in order:
 *
 *   1. compile the source, and print the formula with the machine's own
 *      noun_print, so the bytes that will be typed at the guest are the bytes
 *      the machine's own printer produced;
 *   2. hand that text back to the machine's own reader, and require the noun
 *      that comes out to be the formula that went in -- which also checks the
 *      text fits inside the reader's limits, because a formula the reader
 *      refuses is a formula the guest will never run;
 *   3. run it, with the machine's own interpreter, on a real session built by
 *      the machine's own book, and require the answer that the table names.
 *
 * The expected answers in the table are written as reader text and parsed by
 * the reader, so a test cannot fail by disagreeing about how a noun is spelled.
 * The one thing checked by hand rather than by round trip is the spelling of
 * each compiled formula, which is the point: a formula can be right and
 * misspelled, and the shape this compiler emits is meant to be the same shape
 * book.c writes by hand, trailing zeros and all.
 *
 * This is host code.  libc, stdio and setjmp are all fine here, and none of
 * that would be fine in kernel/ -- the machine has no libc, and the difference
 * between the two directories is the whole reason this file is in tools/.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* kernel.h defines NULL itself so the machine needs no stddef.  The two
 * definitions are the same pointer cast, but saying it twice is a warning. */
#undef NULL
#include "kernel.h"

/* --- the machine, on the host -------------------------------------------- */
/* The kernel's serial and memory, with the parts that are not the point
 * replaced.  Everything that is the point -- nouns, Nock, the book, the reader
 * -- is the machine's own code, linked in and not reimplemented here, so that
 * "the compiler agrees with the machine" is not a claim about two copies of
 * the machine. */

static char     host_heap[4 * 1024 * 1024];
/* The heap starts one word in, and never all the way at the front, because an
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

/* The serial line, in two modes: straight to stdout, or captured into a buffer
 * so the self-test can compare the machine's own printed text against a
 * hand-written expectation. */

static char   cap_buf[GB_LINE_MAX + 2];
static size_t cap_len;
static int    cap_on;
static int    cap_over;

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
u8   serial_getc(void)     { return 0; }

/* --- refusals ------------------------------------------------------------- */
/* A refusal is not an error in the machine's sense: nothing has been written
 * anywhere the machine can see, and the tool exits with a message.  When the
 * self-test is running, a refusal is a result to be checked, so it longjmps
 * back to the test instead of leaving.  The message is the thing under test as
 * much as the noun is, because a refusal that gives the wrong reason is a
 * person sent off to debug the wrong file. */

static jmp_buf refuse_env;
static int     refuse_armed;
static char    refuse_msg[1024];

static void refuse(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(refuse_msg, sizeof refuse_msg, fmt, ap);
    va_end(ap);

    if (refuse_armed) {
        refuse_armed = 0;
        longjmp(refuse_env, 1);
    }
    fprintf(stderr, "hoon: %s\n", refuse_msg);
    exit(1);
}

/* --- the shapes ----------------------------------------------------------- */
/* The three ways to say something as a formula, spelled the way book.c spells
 * them, because these are formulas: book.c's file is the derivation and this is
 * a transcription of it.
 *
 *   lit(n)         [1 n 0]           this literal noun, whatever the subject
 *   at(k)          [0 k 0]           the subject at address k
 *   edit(b, c, d)  [10 [b c 0] d 0]  c's value at address b of d's value
 *
 * The trailing zero in every one of them is not decoration.  Opcode 10 reads
 * the axis from the head of a cell of exactly two things and the value from
 * its tail, so [b c] and not [b c 0 0]: with a trailing zero the pair's tail is
 * [c 0 0], a formula whose opcode is whatever c is, and the crash that follows
 * says nothing about what went wrong.  f2 and f3 end the same way, so
 *
 *   f1(op, a)      [op a 0]          one operand
 *   f2(op, a, b)   [op a b 0]        two operands
 *   f3(op,a,b,c)   [op a b c 0]      three operands
 *
 * are the same three shapes the tests and the book use, and the shape of every
 * formula in the table below is written with them. */

static noun A(u64 v)                    { return noun_atom(v); }
static noun C(noun a, noun b)           { return noun_cons(a, b); }
static noun f1(u64 op, noun a)          { return C(A(op), C(a, A(0))); }
static noun f2(u64 op, noun a, noun b)  { return C(A(op), C(a, C(b, A(0)))); }
static noun f3(u64 op, noun a, noun b, noun c)
                                       { return C(A(op), C(a, C(b, C(c, A(0))))); }
static noun lit(noun n)                 { return f1(1, n); }
static noun at(u64 axis)                { return f1(0, A(axis)); }
static noun edit(u64 b, noun c, noun d) { return f2(10, C(A(b), c), d); }

/* Three atoms as a noun, which is a two-element list: a list ends in 0, and
 * that 0 is a leaf and takes up a slot of its own.  This is the template every
 * cell in this file is edited out of, and it is book.c's `two` exactly. */
static noun two_zeros(void)             { return C(A(0), C(A(0), A(0))); }

/* --- the shape of the subject -------------------------------------------- */
/* The addresses a session has, and what they are, carried through the compiler
 * rather than assumed.  Pushing a value on the front shifts every one of them
 * and adds two: /2 for the value and /3 for the whole old subject.  So an
 * expression inside a =+ is checked against the subject it will actually be run
 * on and not the one outside it -- which is why the check lives in emit() and
 * not in the parser. */

struct axis {
    u64         at;
    const char *name;
};

#define MAX_AXES 16

static struct axis shape[MAX_AXES];
static int         shape_len;

static void shape_session(void)
{
    shape_len = 0;
    shape[shape_len].at = 2;  shape[shape_len].name = "the log";           shape_len++;
    shape[shape_len].at = 6;  shape[shape_len].name = "the last answer";    shape_len++;
    shape[shape_len].at = 8;  shape[shape_len].name = "the newest line";    shape_len++;
    shape[shape_len].at = 14; shape[shape_len].name = "the count";          shape_len++;
    shape[shape_len].at = 18; shape[shape_len].name = "that line's answer"; shape_len++;
}

/* A tree address is a leading 1 and then a path, and the new subject's tail --
 * the old subject -- is one more step down, so the old address a becomes a
 * leading 1, then that step, then a's own path.  /2 becomes /6, /6 becomes
 * /14, /14 becomes /30 and /30 becomes /62, which are the four shifts the
 * interpreter's tests already pin.
 *
 * The tempting way to write that is 2a + 2, and it is wrong.  It happens to give
 * the right answer for /2, /6, /14 and /30 -- the three axes book.c names and
 * the one below them -- and the wrong one everywhere else: /8 goes to /24 and
 * not /18, /18 goes to /50 and not /38, and the whole old subject goes to /3
 * and not /2.  Those three are exactly the axes a line is most likely to reach
 * for, so the version of this rule that looks like arithmetic is the version
 * that breaks, and the shift is written out instead.  book.c gets this right in
 * prose -- "one tail-step in front of the session's own path" -- and lists only
 * the three numbers that hold. */
static u64 axis_shifted(u64 a)
{
    int d = 0;

    while ((a >> d) > 1)
        d++;
    return (1ULL << (d + 1)) | (1ULL << d) | (a & ((1ULL << d) - 1));
}

/* One push on the front: /2 is the value just pushed and /3 is the old subject,
 * and every address inside the old subject has moved. */
static void shape_push(void)
{
    if (shape_len + 2 > MAX_AXES)
        refuse("that is more pushes on the front than a subject can hold");

    for (int i = 0; i < shape_len; i++)
        shape[i].at = axis_shifted(shape[i].at);

    /* Two entries go in at the front -- the value at /2 and the old subject at
     * /3 -- so every existing entry moves up by two, and downwards through the
     * indices, because a copy that runs the other way reads the entries it has
     * already moved: shape[i] is written when the loop reaches i - 2, which is
     * two iterations before the read at i.
     *
     * The first version of this moved entries up by *one*, which quietly lost
     * the /6 -- the log -- from the middle of the table.  Nothing failed: a
     * missing address is a hole, not a crash, and the only sign was a refusal
     * that listed one address too few.  Copying forwards instead of backwards
     * duplicates three of them.  Both showed up in the same test, the one that
     * asks for the pushed shape by name, which is the test that exists because
     * a refusal is the only place the whole shape is ever written down. */
    for (int i = shape_len + 1; i > 1; i--)
        shape[i] = shape[i - 2];
    shape[0].at = 2;
    shape[0].name = "the value just pushed on";
    shape[1].at = 3;
    shape[1].name = "the whole of the old subject";
    shape_len += 2;
}

/* Two entries came on with the push -- the value at /2 and the old subject at
 * /3 -- and two go off again, which is the pair of moves the value made. */
static void shape_pop(void)
{
    for (int i = 0; i < shape_len - 2; i++)
        shape[i] = shape[i + 2];
    shape_len -= 2;
}

static const char *axis_name(u64 a)
{
    for (int i = 0; i < shape_len; i++)
        if (shape[i].at == a)
            return shape[i].name;
    return NULL;
}

static void axis_refused(u64 a)
{
    char   buf[768];
    size_t n = 0;
    int    i;

    n += (size_t)snprintf(buf + n, sizeof buf - n,
                          "/%llu is not an address of this subject; the ones it has are",
                          (unsigned long long)a);
    for (i = 0; i < shape_len; i++)
        n += (size_t)snprintf(buf + n, sizeof buf - n, " /%llu %s",
                              (unsigned long long)shape[i].at, shape[i].name);
    refuse("%s.", buf);
}

/* --- expressions ---------------------------------------------------------- */
/* An expression is a tree, and a literal is a tree too, which is what lets the
 * code generator fold: it can ask a subexpression what it is without having
 * written a noun for it yet.  Nothing in here is a noun. */

enum kind { K_ATOM, K_AXIS, K_RUNE };

struct expr {
    enum kind     kind;
    u64           atom;
    u64           axis;
    char          rune;      /* '?' '=' '~' '*' '|' '+' or 'P' for =+ */
    int           nargs;
    struct expr  *arg[3];
};

#define MAX_EXPRS 512

static struct expr pool[MAX_EXPRS];
static int         pool_count;

static struct expr *new_expr(enum kind k)
{
    if (pool_count >= MAX_EXPRS)
        refuse("the expression nests too deeply to compile: more than %d of them, "
               "which is more than a 4096-character line can hold", MAX_EXPRS);
    struct expr *e = &pool[pool_count++];
    memset(e, 0, sizeof *e);
    e->kind = k;
    return e;
}

/* --- the parser ------------------------------------------------------------ */
/* The language is small enough that the parser reads characters rather than
 * building a token stream: a rune is a character, an atom is digits, and
 * everything else is a bracket of its own.  A separate lexer would be a second
 * thing to keep in step with the first. */

static const char *src;

static void skip(void)
{
    while (*src == ' ' || *src == '\t' || *src == '\n' || *src == '\r')
        src++;
}

static void expect(char c, const char *what)
{
    skip();
    if (*src != c)
        refuse("%s; there is '%c' here instead", what, *src ? *src : ' ');
    src++;
}

/* Digits, read straight out of the source.  This must not skip spaces and then
 * look for a digit: "12 34" is two atoms here, and a parser that keeps reading
 * across the space has 1234. */
static int at_digit(void)
{
    skip();
    return *src >= '0' && *src <= '9';
}

static u64 read_number(const char *what)
{
    u64 v = 0;
    int any = 0;

    skip();
    while (*src >= '0' && *src <= '9') {
        if (v > (NOUN_ATOM_MAX - (u64)(*src - '0')) / 10)
            refuse("%s is larger than an atom can hold: atoms are 0 to 2^63 - 1", what);
        v = v * 10 + (u64)(*src - '0');
        src++;
        any = 1;
    }
    if (!any)
        refuse("expected a number, and there is none here");
    return v;
}

static struct expr *parse_expr(void);

/* An axis: a slash and an address.  Not checked here, and that is on purpose.
 * The parser reads the body of a =+ before anything has been pushed on, so an
 * axis written inside a push would be checked against the subject outside it --
 * which is how /8 came to be accepted inside a =+ where it names nothing.  The
 * check is in emit(), where the shape is the one the formula will be run on. */
static struct expr *parse_axis(void)
{
    expect('/', "an axis starts with /");

    struct expr *e = new_expr(K_AXIS);
    e->axis = read_number("an axis");
    return e;
}

static struct expr *parse_atom(void)
{
    struct expr *e = new_expr(K_ATOM);
    e->atom = read_number("a number");
    return e;
}

/* A rune and its bracket of arguments.  The arity is checked here so that every
 * handler below can count on the arguments it wants being there. */
static struct expr *parse_rune(char rune, int min_args, int max_args)
{
    expect(rune, "a rune is a character and then (");
    expect('(', "a rune is a character and then (");

    struct expr *e = new_expr(K_RUNE);
    e->rune = rune;

    skip();
    if (*src == ')')
        src++;
    else
        for (;;) {
            e->arg[e->nargs++] = parse_expr();
            skip();
            if (*src == ')') {
                src++;
                break;
            }
            /* Which of these three it is matters: an expression that stops in
             * the middle is a missing bracket, an expression that goes on is
             * too many arguments, and anything else is two things where one
             * was wanted.  Each says so, because a refusal that names the
             * wrong one sends somebody looking in the wrong place. */
            if (*src == '\0')
                refuse("a rune is closed with ), and the end of the expression "
                       "is not that");
            if (e->nargs >= max_args)
                refuse("this rune takes at most %d argument%s", max_args,
                       max_args == 1 ? "" : "s");
            if (!at_digit() && *src != '/' && !strchr("?=~|*+", *src))
                refuse("arguments are separated by a space, and then there is '%c'",
                       *src);
        }

    if (e->nargs < min_args)
        refuse("this rune needs at least %d argument%s", min_args,
               min_args == 1 ? "" : "s");
    return e;
}

/* =+ : the value to push, and the body to run there, both inside the brackets,
 * which is how it is written.  It is the only rune whose two parts are not the
 * same kind of thing -- one is read in the subject outside and one in the
 * subject inside -- so it is parsed rather than counted. */
static struct expr *parse_push(void)
{
    expect('=', "expected =+");
    expect('+', "expected =+, not =-");
    expect('(', "=+ is written =+(a body)");

    struct expr *e = new_expr(K_RUNE);
    e->rune = 'P';
    e->nargs = 2;
    e->arg[0] = parse_expr();
    e->arg[1] = parse_expr();
    expect(')', "=+ is closed with )");
    return e;
}

static struct expr *parse_expr(void)
{
    skip();

    if (*src == '/')
        return parse_axis();
    if (*src >= '0' && *src <= '9')
        return parse_atom();

    if (*src == '=') {
        if (src[1] == '+')
            return parse_push();
        if (src[1] == '-')
            refuse("=- is not a rune in this machine.  Opcode 8 pushes onto the "
                   "front of a subject and there is no opcode that pushes onto "
                   "its tail, so the head is the only end that can be pushed on.  "
                   "Write =+ and remember that every address inside the old "
                   "subject moves with it.");
        return parse_rune('=', 2, 2);
    }

    switch (*src) {
    case '?': return parse_rune('?', 1, 1);
    case '~': return parse_rune('~', 3, 3);
    case '|': return parse_rune('|', 2, 2);
    case '*': return parse_rune('*', 2, 2);
    case '+': return parse_rune('+', 1, 2);
    default:
        refuse("expected a number, an axis /, or one of ? ~ = | * + =+");
    }
    return NULL;    /* not reached */
}

/* --- folding ---------------------------------------------------------------- */

/* An expression that is only a number, and the number.  The host has a CPU and
 * the same 63-bit atoms the machine has, so an expression of literals is worked
 * out here rather than by the interpreter.  It is the only arithmetic in the
 * language that is not the machine's, and it is arithmetic on numbers that were
 * written down, so there is nothing for the machine to disagree with. */
static int is_literal(struct expr *e, u64 *out)
{
    u64 x, y;

    if (e->kind == K_ATOM) {
        *out = e->atom;
        return 1;
    }
    if (e->kind == K_RUNE && e->rune == '+' && e->nargs == 2) {
        if (is_literal(e->arg[0], &x) && is_literal(e->arg[1], &y)) {
            if (y > NOUN_ATOM_MAX - x)
                refuse("that addition does not fit in an atom: atoms are 0 to 2^63 - 1");
            *out = x + y;
            return 1;
        }
    }
    return 0;
}

/* --- code generation --------------------------------------------------------- */

static noun emit(struct expr *e)
{
    u64   v;
    noun  a, b, c;
    static const char *GAP = "addition of two values that are only known at run time";

    if (is_literal(e, &v))
        return lit(A(v));

    switch (e->kind) {
    case K_ATOM: return lit(A(e->atom));

    case K_AXIS:
        /* Here, and not in the parser: this is where the shape is the subject
         * the formula will be run on, which for the body of a =+ is the pushed
         * one.  An address that is not in the shape is refused rather than
         * compiled, because a wrong address on this machine usually names a real
         * noun instead of crashing. */
        if (axis_name(e->axis) == NULL)
            axis_refused(e->axis);
        return at(e->axis);

    case K_RUNE: break;
    default:     refuse("this expression is not one the code generator knows");
    }

    switch (e->rune) {
    case '?':
        /* 0 for a cell, 1 for an atom. */
        return f1(3, emit(e->arg[0]));

    case '=':
        /* 0 for the same noun, 1 for a different one. */
        return f2(5, emit(e->arg[0]), emit(e->arg[1]));

    case '~':
        /* if c then t else e, with 0 as true. */
        return f3(6, emit(e->arg[0]), emit(e->arg[1]), emit(e->arg[2]));

    case '*':
        /* nock(nock(subject, a), nock(subject, b)) -- evaluate a, then run b
         * with a's value as the subject.  The argument order is Hoon's, so the
         * difference is subtler and is in the second argument: it is evaluated
         * in the *outer* subject and its value is the formula, not a formula
         * written down.  Hence the usual spelling here is *(|(1 3) |(0 2)),
         * where the second | builds the bone [0 2 0].  nock.c's comment on
         * opcode 2 says the same thing about the difference from opcode 8. */
        return f2(2, emit(e->arg[0]), emit(e->arg[1]));

    case '|':
        /* Two things, side by side.  Nock cannot make a noun and can only edit,
         * so this is two edits into a template of three zeros: the head at axis
         * 2, and the second thing at axis 6, which is the head of the tail
         * because the tail of a list is a list.  Axis 3 would be the whole tail
         * and would make a two-word cell instead, which is a real noun and is
         * not one this machine's text can say: the reader folds a bracket
         * right-nested, so [1 2] is [1 2 0] and there is no spelling of a cell
         * whose tail is an atom at all.  book.c makes its entries with these
         * same two addresses, for the same reason. */
        a = emit(e->arg[0]);
        b = emit(e->arg[1]);
        return edit(6, b, edit(2, a, lit(two_zeros())));

    case '+':
        if (e->nargs == 2) {
            /* is_literal has already folded the two-literal case, by not getting
             * here.  Anything else is the gap decisions.md item 9 names. */
            refuse("%s has no Nock definition in this machine yet: the twenty "
                   "native operations are a tested bank and not proven jets, and "
                   "writing this rune as a native call would make the compiler "
                   "answer questions the machine has never been asked.  +(2 3) "
                   "works, because the host can fold that.", GAP);
        }
        /* Increment.  Opcode 8 pushes a's value onto the front of the subject,
         * where the head is /2, and opcode 4 increments what it finds at an
         * address -- so [8 a [4 [0 2]]] is a plus one, whatever a is.  This is
         * the one piece of arithmetic the machine can do without a core. */
        return f2(8, emit(e->arg[0]), f1(4, at(2)));

    case 'P':
        /* =+ : push a's value on, then run the body there.  a is compiled in
         * the subject as it is now, and the body in the subject with the value
         * pushed on -- so the body's own /2 is the value and every other
         * address inside the old subject has moved.  Nothing in the emitted
         * nouns has to be rewritten: each formula is written in the coordinates
         * of the subject it will be run on, which is the whole reason this is a
         * compiler and not a macro expander.  The shape is pushed for the body
         * and popped after it, and that is the only state this walk carries. */
        a = emit(e->arg[0]);
        shape_push();
        b = emit(e->arg[1]);
        shape_pop();
        return f2(8, a, b);

    default:
        refuse("this rune is not one the code generator knows");
    }
    (void)c;
    return 0;    /* not reached */
}

/* --- compiling one source line ----------------------------------------------- */

static noun compile(const char *text)
{
    noun formula;

    /* The machine's error state is sticky, and a stale one is a lie: the check
     * at the end of this function reads machine_err to see whether the build
     * went wrong, and a crash left over from the last case would turn into a
     * refusal about this one.  The interpreter in the self-test leaves it set
     * when a formula does not answer, and this is the fix for the report that
     * said so. */
    machine_reset_error();
    pool_count = 0;
    shape_session();

    src = text;
    struct expr *e = parse_expr();
    skip();
    if (*src != '\0')
        refuse("there is more after the expression, starting at '%s'", src);

    formula = emit(e);
    if (machine_err)
        refuse("the machine ran out of something building that: %s", machine_err_msg);
    return formula;
}

/* Print a noun the way the machine prints one, into cap_buf, so the text can be
 * compared and read back. */
static size_t print_to_capture(noun n)
{
    cap_len = 0;
    cap_over = 0;
    cap_on = 1;
    noun_print(n);
    cap_on = 0;
    cap_buf[cap_len] = '\0';
    return cap_len;
}

/* --- the self-test ------------------------------------------------------------- */

/* Each case is the source, the text the formula is meant to be printed as, and
 * the answer the machine is meant to give it.  The text is the one thing here
 * that is not checked by round trip, because that is what it is for: a formula
 * can be right and misspelled, and these shapes are meant to be the shapes
 * book.c writes by hand, trailing zeros and all.  Everything after the text --
 * the reader taking the printed text back to the same noun, and the interpreter
 * giving the answer -- is the machine's own code on both sides. */

struct expect {
    const char *what;
    const char *source;
    const char *text;      /* the spelling, hand-written */
    u64         entries;   /* lines in the session the formula is run on */
    const char *answer;    /* the expected answer, as reader text */
};

static const struct expect cases[] = {
    /* The three shapes on their own, and an atom.  The text is the machine's own
     * spelling -- every cell bracketed, so [1 [42 0]] is the formula for the atom
     * 42, and the book's [1 42 0] is the same noun with the list written in
     * line.  Both are one noun and the reader takes both, and the round trip
     * below is what settles that, so the table can be the spelling and not the
     * meaning: the meaning is checked by the interpreter on the right. */
    { "an atom is a constant", "42", "[1 [42 0]]", 0, "42" },
    { "an axis is a read of the subject", "/14", "[0 [14 0]]", 3, "3" },
    { "the log is at /2, and an empty log is the atom 0", "/2", "[0 [2 0]]", 0, "0" },
    { "the last answer is at /6", "/6", "[0 [6 0]]", 3, "3" },
    { "the newest line is at /8, and it is a list", "/8", "[0 [8 0]]", 3, "[1 3 0]" },
    { "that line's answer is at /18", "/18", "[0 [18 0]]", 3, "3" },

    /* The runes, one at a time. */
    { "a cell test is opcode 3, and an atom is 1", "?(/6)", "[3 [[0 [6 0]] 0]]", 3, "1" },
    { "equality is opcode 5, and the same noun is 0", "=(/14 3)", "[5 [[0 [14 0]] [[1 [3 0]] 0]]]", 3, "0" },
    { "equality of different nouns is 1", "=(/14 4)", "[5 [[0 [14 0]] [[1 [4 0]] 0]]]", 3, "1" },
    { "a conditional is opcode 6, and 0 takes the first arm", "~(/14 7 8)", "[6 [[0 [14 0]] [[1 [7 0]] [[1 [8 0]] 0]]]]", 0, "7" },
    { "and 1 takes the other arm", "~(/14 7 8)", "[6 [[0 [14 0]] [[1 [7 0]] [[1 [8 0]] 0]]]]", 1, "8" },

    /* Two things side by side, which is two edits into a template of three
     * zeros: the head at axis 2 and the second thing at axis 6, because the
     * tail of a list is a list and so is a cell of its own.  Axis 6 and not
     * axis 3 -- axis 3 is the whole tail, and using it would make a two-word
     * cell, which is a real noun and is not one this machine's text can say:
     * the reader folds a bracket right-nested, so [1 2] is [1 2 0] and there is
     * no spelling at all for a cell whose tail is an atom.  book.c builds its
     * entries at these same two addresses for the same reason.
     *
     * The first pair of edits is [6 1 3 0] -- the axis, then the formula that
     * gives the value, which is a list itself and so takes up the rest of the
     * line.  The second is [2 1 1 0], and the template is the constant
     * [1 [0 0 0] 0]. */
    { "two things are two edits into a template of three zeros", "|(1 3)", "[10 [[6 [1 [3 0]]] [[10 [[2 [1 [1 0]]] [[1 [[0 [0 0]] 0]] 0]]] 0]]]", 0, "[1 3 0]" },
    { "and the second thing can come from the subject", "|([1 7] /14)", "[10 [[6 [0 [14 0]]] [[10 [[2 [1 [7 0]]] [[1 [[0 [0 0]] 0]] 0]]] 0]]]", 2, "[7 2 0]" },

    /* Composition, and it is worth being exact about what opcode 2 does on this
     * machine, because it is not the obvious reading.  nock.c evaluates *both*
     * arguments in the outer subject, and then uses the second one's value as
     * the formula to run on the first one's value: *[*[a b] *[a c]].  So b is
     * not a formula written down, it is an expression whose result is one.
     *
     * Which means a bone -- /2, which is the formula [0 2 0] -- has to be the
     * result of an expression, and the only way this language can make a
     * three-word list out of two atoms is |.  So the call is |(0 2), which
     * makes the list [0 2 0] and is a formula for the head of whatever subject
     * it is run on, and the subject is |(1 3), which makes [1 3 0].  The head
     * of that is 1, and the answer is 1.
     *
     * This is why value and formula are kept apart so carefully in compile()
     * below: in this language almost everything is a value, and a value is not
     * a formula until something has made one. */
    { "composition calls b's value on a's value", "*(|(1 3) |(0 2))", "[2 [[10 [[6 [1 [3 0]]] [[10 [[2 [1 [1 0]]] [[1 [[0 [0 0]] 0]] 0]]] 0]]] [[10 [[6 [1 [2 0]]] [[10 [[2 [1 [0 0]]] [[1 [[0 [0 0]] 0]] 0]]] 0]]] 0]]]", 0, "1" },

    /* Increment, and the one thing the host is allowed to do by itself. */
    { "increment is a push, then opcode 4 on the head", "+(/14)", "[8 [[0 [14 0]] [[4 [[0 [2 0]] 0]] 0]]]", 2, "3" },
    { "two literals are folded on the host, so it is a constant", "+(2 3)", "[1 [5 0]]", 0, "5" },

    /* =+ , and the shift.  Inside the body the new subject's /2 is the value
     * just pushed and its /3 is the whole old session, and every address
     * inside the old session has moved: the count is at /30 and the newest line
     * at /24.  /30 is book.c's AX_COUNT arrived at from the other side, and
     * /24 is the one that 2a + 2 would have got wrong. */
    { "=+ pushes on the front and runs the body there", "=+(/14 ~(/2 1 2))", "[8 [[0 [14 0]] [[6 [[0 [2 0]] [[1 [1 0]] [[1 [2 0]] 0]]]] 0]]]", 0, "1" },
    { "with the count moved, the same expression is false", "=+(/14 ~(/2 1 2))", "[8 [[0 [14 0]] [[6 [[0 [2 0]] [[1 [1 0]] [[1 [2 0]] 0]]]] 0]]]", 1, "2" },
    { "inside =+, the whole old session is at /3", "=+(/14 /3)", "[8 [[0 [14 0]] [[0 [3 0]] 0]]]", 0, "[0 0 0 0]" },
    { "inside =+, the last answer is at /14", "=+(/14 /14)", "[8 [[0 [14 0]] [[0 [14 0]] 0]]]", 5, "5" },
    { "inside =+, the log is at /6, and an empty log is the atom 0", "=+(/14 /6)", "[8 [[0 [14 0]] [[0 [6 0]] 0]]]", 0, "0" },
    { "inside =+, the count is at /30, as in book.c", "=+(/14 /30)", "[8 [[0 [14 0]] [[0 [30 0]] 0]]]", 5, "5" },
    { "inside =+, the newest line is at /24 and not /18", "=+(/14 /24)", "[8 [[0 [14 0]] [[0 [24 0]] 0]]]", 5, "[1 5 0]" },
    { "a second push shifts again, so the count is at /62", "=+(/14 =+(/30 /62))", "[8 [[0 [14 0]] [[8 [[0 [30 0]] [[0 [62 0]] 0]]] 0]]]", 7, "7" },
};

/* The refusals, which are the other half of the language.  Each is checked by
 * its message, because a refusal that gives the wrong reason sends a person off
 * to debug the wrong file -- and one of these messages is the reason the
 * language is drawn where it is. */
static const struct {
    const char *what;
    const char *source;
    const char *why;
} refusals[] = {
    /* These two check the whole list the refusal prints, and not just its first
     * words, because that list is the only place the compiler ever writes its
     * idea of the subject down.  A prefix check is how a shape with a hole in
     * it passes its own test. */
    { "an address the session does not have", "/37",
      "are /2 the log /6 the last answer /8 the newest line /14 the count "
      "/18 that line's answer" },
    { "the wrong way round, which book.c names: /5, /13, /29", "/5",
      "is not an address of this subject" },
    { "doubling a session address, which also does not work", "/28",
      "is not an address of this subject" },
    { "an address that has moved, inside a push", "=+(/14 /8)",
      "are /2 the value just pushed on /3 the whole of the old subject "
      "/6 the log /14 the last answer /24 the newest line /30 the count "
      "/50 that line's answer" },
    { "run-time addition", "+(/14 1)",
      "has no Nock definition in this machine yet" },
    { "run-time addition, folded nowhere", "+(+(1 2) /14)",
      "has no Nock definition in this machine yet" },
    { "=- , which Nock has no opcode for", "=-(/14 /2)",
      "=- is not a rune in this machine" },
    { "an atom too large to be an atom", "99999999999999999999999",
      "larger than an atom can hold" },
    { "an axis with no number after it", "/x", "expected a number" },
    { "a rune with too few arguments", "=(/14)", "at least 2 arguments" },
    { "a rune with no arguments at all", "|()", "at least 2 arguments" },
    { "a rune with too many arguments", "?(/14 /6)", "at most 1 argument" },
    { "a rune that is never closed", "~(/14 1 2", "closed with )" },
    { "a rune with no opening bracket", "? /6", "a rune is a character and then (" },
    { "two expressions where one was wanted", "/14 /6",
      "there is more after the expression" },
    { "something that is not an expression at all", "]", "expected a number" },
    { "nothing at all", "", "expected a number" },
};

/* A session with n entries in it, built by running the machine's own book n
 * times on the machine's own empty session, with [1 k] as each line -- so the
 * newest line is [1 n 0] and the count and the last answer are both n. */
static noun session_with(u64 n)
{
    noun s = gb_empty_session();

    for (u64 i = 1; i <= n; i++) {
        noun next = 0;
        int  rc = gb_step(lit(A(i)), s, &next);
        if (rc != NOCK_OK)
            refuse("the machine could not build a session of %llu entries: %s",
                   (unsigned long long)n, machine_err_msg);
        s = next;
    }
    return s;
}

/* Read a noun out of reader text, the way the guest would, so that the expected
 * answers in the table are written the way the machine is addressed and a test
 * cannot fail over a spelling. */
static noun read(const char *text, const char *what)
{
    noun out = 0;
    const char *why = "";

    if (gb_parse(text, (u64)strlen(text), &out, &why) != GB_PARSE_OK)
        refuse("the expected %s, '%s', is not text the reader accepts: %s",
               what, text, why);
    return out;
}

/* The two counters and the two reports.  They are file-scope because a longjmp
 * out of a refusal is allowed to land in selftest() and a local that is not
 * volatile is not required to still hold its value afterwards -- which gcc says
 * out loud, and is right about. */
static int tally_pass;
static int tally_fail;

static void ok(const char *what)
{
    printf("  pass  %s\n", what);
    tally_pass++;
}

/* A failure says what went wrong rather than only that something did, because
 * "the compiler disagrees" is not something anybody can act on. */
static void no(const char *what, const char *fmt, ...)
{
    va_list ap;

    printf("  FAIL  %s\n", what);
    printf("         ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    tally_fail++;
}

static int selftest(void)
{
    /* The formula, and whether this case has got as far as having one, survive
     * a refusal that longjmps out of the middle of the compile. */
    static noun   formula;
    static int    bad;
    /* The loop counter is a static for the same reason: a refusal longjmps out
     * of the compile in the middle of the body, and the case is looked up again
     * on the far side of that jump, so the index has to still be the same one. */
    static unsigned i;

    printf("== the compiler\n");

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const struct expect *c = &cases[i];
        int  before = tally_fail;

        formula = 0;
        bad = 0;

        if (setjmp(refuse_env) == 0) {
            refuse_armed = 1;
            formula = compile(c->source);
            refuse_armed = 0;
        } else {
            bad = 1;
        }

        /* 1. The text itself, against the spelling in the table.  This is the
         *    only thing here checked as a string, and it is checked as a string
         *    on purpose: the text is what goes down the wire, so the bytes on
         *    the wire are the contract and not a detail of it.  The table spells
         *    each one the way the machine's own noun_print does -- every cell
         *    bracketed -- which book.c's comments write with fewer brackets and
         *    which is the same noun either way, so the round trip below is what
         *    settles it. */
        if (!bad && c->text != NULL) {
            print_to_capture(formula);
            if (cap_over)
                no(c->what, "the formula did not fit in %d characters", GB_LINE_MAX);
            else if (strcmp(cap_buf, c->text) != 0)
                no(c->what, "said\n         %s\n         wanted\n         %s",
                   cap_buf, c->text);
        }

        /* 2. The round trip: the machine's own reader, on the machine's own
         *    printed text, must give back the noun that went in.  A formula the
         *    reader refuses is a formula the guest will never run, and the
         *    reader's own limits are enforced here rather than in a comment. */
        if (!bad) {
            noun        back = 0;
            const char *rwhy = "";

            print_to_capture(formula);
            if (gb_parse(cap_buf, (u64)cap_len, &back, &rwhy) != GB_PARSE_OK)
                no(c->what, "the reader refused the printed form: %s", rwhy);
            else if (!noun_equal(back, formula))
                no(c->what, "the reader read the printed form as a different noun");
        }

        /* 3. The answer, from the machine's own interpreter, on a session the
         *    machine's own book built by running its own step. */
        if (!bad) {
            noun session = session_with(c->entries);
            noun answer  = 0;
            noun want    = read(c->answer, "answer");
            int  rc      = nock_run(session, formula, &answer);

            if (rc != NOCK_OK)
                no(c->what, "the interpreter did not answer: %s", machine_err_msg);
            else if (!noun_equal(answer, want)) {
                print_to_capture(answer);
                no(c->what, "answered %s, wanted %s", cap_buf, c->answer);
            }
        }

        if (!bad && tally_fail == before)
            ok(c->what);
    }

    printf("== and the refusals\n");

    for (i = 0; i < sizeof refusals / sizeof refusals[0]; i++) {
        int refused = 0;

        if (setjmp(refuse_env) == 0) {
            refuse_armed = 1;
            (void)compile(refusals[i].source);
            refuse_armed = 0;
        } else {
            refused = 1;
        }

        if (!refused)
            no(refusals[i].what, "'%s' was not refused", refusals[i].source);
        else if (strstr(refuse_msg, refusals[i].why) == NULL) {
            printf("  FAIL  %s\n", refusals[i].what);
            printf("         '%s' was refused, saying\n         %s\n",
                   refusals[i].source, refuse_msg);
            tally_fail++;
        } else {
            ok(refusals[i].what);
        }
    }

    printf("%d checks, %d failed\n", tally_pass + tally_fail, tally_fail);
    return tally_fail == 0 ? 0 : 1;
}

/* --- main --------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    noun  formula;
    char  text[GB_LINE_MAX + 2];

    noun_init();
    nock_init(NOCK_DEFAULT_STEP_LIMIT);

    if (argc == 2 && strcmp(argv[1], "--selftest") == 0)
        return selftest();

    if (argc != 2) {
        fprintf(stderr,
                "usage: hoon <expression>\n"
                "       hoon --selftest\n"
                "\n"
                "  42        an atom                /14       the subject at that address\n"
                "  ?(a)      cell test              =(a b)    equality, 0 if the same\n"
                "  ~(c t e)  if c then t else e    *(a b)    call b's value on a's\n"
                "  |(a b)    the list [a b 0]      +(a)      a plus one\n"
                "  +(a b)    a plus b, both literals\n"
                "  =+(a body)   push a on the front, then run body\n");
        return 2;
    }

    if (setjmp(refuse_env) == 0) {
        refuse_armed = 1;
        formula = compile(argv[1]);
        refuse_armed = 0;
    } else {
        fprintf(stderr, "hoon: %s\n", refuse_msg);
        return 1;
    }

    /* Print it with the machine's printer, into a buffer, so the length can be
     * checked before a single byte goes near the serial line. */
    if (print_to_capture(formula) > GB_LINE_MAX) {
        fprintf(stderr,
                "hoon: the formula is longer than the %d characters a line may be\n",
                GB_LINE_MAX);
        return 1;
    }
    memcpy(text, cap_buf, cap_len + 1);
    fputs(text, stdout);
    fputc('\n', stdout);
    return 0;
}
