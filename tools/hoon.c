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
 *     arm             the arm of the core this is inside, and only inside one
 *     ?(a)            0 if a is a cell, 1 if it is an atom
 *     =(a b)          0 if a and b are the same noun, 1 if not
 *     ?:(c t e)       t if c is 0, e if c is 1
 *     *(a b)          call: a's value is the subject, b's value is the formula
 *     [a b c]         the list [a b c 0], the same noun as |(a b c)
 *     |(a b c)        two or more things side by side
 *     +(a)            a plus one
 *     +(a b)          a plus b, and only when both are literals
 *     =>(a body)      push a's value onto the front of the subject, run body
 *     =+(arm sample body)   a core, and an arm that can call itself
 *     ~(arm core)     call the arm that core holds, on that core
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
 * `=>` is the one that means in Hoon what it means here, including the shift
 * it causes, and that shift is the thing in this file most likely to be got
 * wrong.  `=+` and `~` mean in Hoon what they mean here too, and they are the
 * two halves of a core: a core is `[sample arm 0]`, the arm is at /6 whatever
 * the sample is, and `~(arm core)` is opcode 9 with the arm's address, which
 * is always 6.  A name is a read of the arm, so the arm is carried in the
 * noun instead of being written down again -- which is the whole of what lets
 * an arm call itself.  See decisions.md item 24, which is where the loop and
 * its costs are measured, and the loop cases in the tests below.
 *
 * The sample of a core is a `|`, and it has to be: the compiler compiles the
 * body in the core's own coordinates, and the only way it can know which
 * addresses those are is by walking the sample.  A two-thing sample is /4 and
 * /10 -- one HEAD-step in front of the addresses it has out here, which is not
 * the push's axis_shifted, because a push puts the old subject in the whole
 * tail and a core puts the sample in the head.  A `|` inside the sample is a
 * walk and not a formula, and a list is an address as well as a container, so
 * a body can pass one along.
 *
 * There is no way to count DOWN.  `+(a)` is the only arithmetic this machine
 * can do at run time, so a loop can count up and a loop that counts down needs
 * `-add`, which is one of the seventeen in decisions.md item 23.  That is the
 * first thing to add and it is not a compiler problem.
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
 * `=>` pushes a value on the front, and every address inside the old subject
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
 * loud.  The refusal lists the addresses that are there, and inside a core
 * those are the core's own, so the same refusal reads as the sample's contents.
 *
 * A `=`+ swaps the table for the core's own, and puts it back afterwards, so
 * a core inside a core inside a core is a stack rather than a rewrite.  Both
 * that stack and the stack of arms are cleared at the top of every compile,
 * because a refusal longjmps out of the middle of a `=`+ and would otherwise
 * leave the next line believing it is inside a core that never finished.
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
#include "host-machine.h"

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

/* The template for a list of n things: n zeros in a row, the same shape
 * two_zeros is for n = 2.  Built by loop because the number of things is the
 * reader's, and a reader may write |(1 2 3 4) with no idea what it costs. */
static noun zeros(int n)
{
    noun t = A(0);
    for (int i = 0; i < n; i++)
        t = C(A(0), t);
    return t;
}

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
    char        name_buf[192];   /* for a label this file built rather than a fixed one */
    int         nargs;           /* 0 for a number or a read, the count for a list */
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

/* --- the shape of a core ----------------------------------------------------- */
/* A core is [sample arm 0], so the arm is always at /6 whatever the sample is,
 * and the sample is at the head.  The sample's own addresses are therefore one
 * head-step in front of the ones it has out here, which is NOT axis_shifted: a
 * push puts the old subject in the whole tail, and a core puts the sample in the
 * head of the tail, so the two rules differ by a step and only one of them is
 * about a tail.  Both are written out rather than computed, for the same reason
 * axis_shifted is. */
#define ARM_AXIS 6

#define MAX_NAME 32
#define MAX_ARM_DEPTH 8
static char arm_name[MAX_ARM_DEPTH][MAX_NAME];
/* The sample's own shape, kept per arm so that an arm call can be checked
 * against it.  A call builds a NEW core out of a new sample, and the addresses
 * the body uses mean whatever that new sample says they mean -- so rebuilding
 * the sample in a different order is a silently wrong loop, and it was one:
 * =+(arm |(0 5) ?:(=(/4 /10) 1 ~(arm |(arm |(+(/4) /10))))) answers 5 rather
 * than 1, with no complaint anywhere, because the arm went into the first slot
 * and pushed the counter into the second.  Comparing the two walks turns that
 * into a refusal naming both shapes. */
static struct axis arm_sample[MAX_ARM_DEPTH][MAX_AXES];
static int         arm_sample_len[MAX_ARM_DEPTH];
static int  arm_depth;
static int  in_core(void)                 { return arm_depth > 0; }

#define MAX_CORE_DEPTH 8

static struct { struct axis axes[MAX_AXES]; int len; } core_stack[MAX_CORE_DEPTH];
static int core_depth;

/* The addresses a | holds, in a core's coordinates.  depth and path are the
 * sample's own, so the first call is depth 1 and path 0: the sample sits at the
 * head of the core, and a head-step is a zero at the front of a path.  The
 * address of a leaf is therefore (1 << depth) | path with nothing shifted again,
 * and a | inside a | is walked from where the inner list landed, which is why
 * this is a walk and not a formula.
 *
 * The i-th thing in a list is reached by i-1 tail-steps and then a head-step, so
 * inside the list its path is 2^i - 2 in i bits: 0, 2, 6, 14 for the first four,
 * which are the /2 /6 /14 /30 a reader of this file already knows. */
static const char *ordinal(int i)
{
    switch (i) {
    case 0: return "first";
    case 1: return "second";
    case 2: return "third";
    case 3: return "fourth";
    case 4: return "fifth";
    default: return "later";
    }
}

static void shape_add(u64 at, const char *name)
{
    if (shape_len >= MAX_AXES)
        refuse("that is more addresses in one subject than a table can hold (%d); "
               "the reader's own limit is 4096 characters and a shape is a table",
               MAX_AXES);
    shape[shape_len].at = at;
    shape[shape_len].nargs = 0;
    shape[shape_len].name = NULL;
    snprintf(shape[shape_len].name_buf, sizeof shape[shape_len].name_buf, "%s", name);
    shape[shape_len].name = shape[shape_len].name_buf;
    shape_len++;
}

/* The core's own table: the whole core, the sample at the head, the arm at /6,
 * and then whatever the sample turned out to hold.  The old table goes on a
 * stack and comes off again, because a =+ inside the arm of a =+ is legal and
 * has to get back to the outer subject when it does. */
static void shape_core_enter(void)
{
    if (core_depth >= MAX_CORE_DEPTH)
        refuse("this nests cores %d deep, which is more than the compiler tracks",
               MAX_CORE_DEPTH);
    for (int i = 0; i < shape_len; i++)
        core_stack[core_depth].axes[i] = shape[i];
    core_stack[core_depth].len = shape_len;
    core_depth++;

    shape_len = 0;
    shape_add(1, "the whole of the core");
    shape_add(2, "the sample");
    shape_add(3, "the arm and the end of the list");
    shape_add(ARM_AXIS, "the arm");
}

static void shape_core_leave(void)
{
    core_depth--;
    shape_len = core_stack[core_depth].len;
    for (int i = 0; i < shape_len; i++)
        shape[i] = core_stack[core_depth].axes[i];
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

#define MAX_LIST 8      /* the most things one | can hold side by side */
#define MAX_ARGS MAX_LIST

enum kind { K_ATOM, K_AXIS, K_NAME, K_RUNE };

struct expr {
    enum kind     kind;
    u64           atom;
    u64           axis;
    char          name[MAX_NAME];
    /* '?' is the cell test, 'v' the conditional, '=' equality, '~' an arm call,
     * '|' a list, '*' a call, '+' addition, 'P' the push, 'C' a core. */
    char          rune;
    int           nargs;
    struct expr  *arg[MAX_ARGS];
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

static void list_shape_walk(struct expr *e, int depth, u64 path, const char *where,
                            struct axis *table, int *len)
{
    if (e->kind == K_RUNE && e->rune == '|') {
        for (int i = 0; i < e->nargs; i++) {
            int    d = depth + i + 1;
            u64    p = (path << (i + 1)) | ((1ULL << (i + 1)) - 2);
            char   here[MAX_AXES][128];
            char   ll[MAX_AXES][192];
            snprintf(here[d], sizeof here[d], "the %s thing of %.80s", ordinal(i), where);
            /* A list is an address as well as a container.  The table used to
             * hold only the leaves, which meant the sample |(|(0 5) 9) could
             * name the 5 and the 9 but not the list in front of them -- so a
             * body could not pass a list along, and the refusal that said so
             * listed addresses that were really there. */
            if (e->arg[i]->kind == K_RUNE && e->arg[i]->rune == '|') {
                snprintf(ll[d], sizeof ll[d], "%.80s, which is a list of %d things here",
                         here[d], e->arg[i]->nargs);
                if (*len >= MAX_AXES)
                    refuse("that is more addresses in one core than a table can hold (%d); "
                           "the reader's own limit is 4096 characters and a shape is a table",
                           MAX_AXES);
                table[*len].at = (1ULL << d) | p;
                table[*len].nargs = e->arg[i]->nargs;
                table[*len].name = NULL;
                snprintf(table[*len].name_buf, sizeof table[*len].name_buf, "%s", ll[d]);
                table[*len].name = table[*len].name_buf;
                (*len)++;
            }
            list_shape_walk(e->arg[i], d, p, here[d], table, len);
        }
        return;
    }
    char label[192];
    snprintf(label, sizeof label, "%.80s, which is a %s here", where,
             e->kind == K_NAME ? "name" : (e->kind == K_AXIS ? "read" : "number"));
    if (*len >= MAX_AXES)
        refuse("that is more addresses in one core than a table can hold (%d); "
               "the reader's own limit is 4096 characters and a shape is a table",
               MAX_AXES);
    table[*len].at = (1ULL << depth) | path;
    table[*len].nargs = 0;
    table[*len].name = NULL;
    snprintf(table[*len].name_buf, sizeof table[*len].name_buf, "%s", label);
    table[*len].name = table[*len].name_buf;
    (*len)++;
}

static void list_shape_core(struct expr *e, int depth, u64 path, const char *where)
{
    list_shape_walk(e, depth, path, where, shape, &shape_len);
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

/* --- names --------------------------------------------------------------- */
/* A name is the arm of a core, and it is the only thing in this language that
 * has one.  The character test lives here rather than in parse_name because the
 * rune parser needs it too: an argument can be a name, and "arguments are
 * separated by a space" is a refusal that has to know what an argument is. */
static int is_name_start(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int is_name_char(int c)
{
    return is_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '_';
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
            if (!at_digit() && *src != '/' && !is_name_start(*src) &&
                !strchr("?=~|*+", *src) && !(src[0] == '?' && src[1] == ':'))
                refuse("arguments are separated by a space, and then there is '%c'",
                       *src);
        }

    if (e->nargs < min_args)
        refuse("this rune needs at least %d argument%s", min_args,
               min_args == 1 ? "" : "s");
    return e;
}

/* [a b c] -- a list, which is the same noun as |(a b c) and the same noun the
 * machine's own reader folds: [1 2] is [1 2 0], so a bracket here is a line of
 * the wire format rather than a cell.  Parsed into the same K_RUNE '|' rather
 * than a kind of its own, so there is exactly one list in this compiler and one
 * set of addresses for it. */
static struct expr *parse_bracket(void)
{
    expect('[', "a list is [ then things then ]");
    struct expr *e = new_expr(K_RUNE);
    e->rune = '|';

    for (;;) {
        skip();
        if (*src == ']') {
            src++;
            break;
        }
        if (e->nargs >= MAX_LIST)
            refuse("a list holds at most %d things, and this one wants more",
                   MAX_LIST);
        e->arg[e->nargs++] = parse_expr();
    }
    if (e->nargs == 0)
        refuse("a list with nothing in it is not a noun this machine has a name "
               "for; an empty list is 0");
    return e;
}

/* ?:(c t e) -- the conditional, which is Hoon's spelling for what this file used
 * to call ~ before ~ went back to meaning an arm call.  Parsed rather than
 * counted because ?: is two characters and every other rune is one. */
static struct expr *parse_cond(void)
{
    expect('?', "expected ?:");
    expect(':', "expected ?:, not ?(");
    expect('(', "? is written ?(a) and the conditional is ?:(c t e)");

    struct expr *e = new_expr(K_RUNE);
    e->rune = 'v';
    e->nargs = 3;
    e->arg[0] = parse_expr();
    e->arg[1] = parse_expr();
    e->arg[2] = parse_expr();
    expect(')', "?: is closed with )");
    return e;
}

/* A name: the arm of a core, which is the only thing in this language that is
 * named.  It is read here and bound by =+ below, so a name outside a core is
 * refused at emit time with the reason rather than here, where the reason would
 * be "expected a rune". */
static struct expr *parse_name(void)
{
    char buf[MAX_NAME];
    size_t n = 0;

    skip();
    while (is_name_char(*src) && n < sizeof buf - 1)
        buf[n++] = *src++;
    buf[n] = '\0';
    if (n == 0)
        refuse("a name starts with a letter, and there is none here");

    struct expr *e = new_expr(K_NAME);
    snprintf(e->name, sizeof e->name, "%s", buf);
    return e;
}

/* => : the value to push, and the body to run there, both inside the brackets,
 * which is how it is written.  It is the only rune whose two parts are not the
 * same kind of thing -- one is read in the subject outside and one in the
 * subject inside -- so it is parsed rather than counted.
 *
 * This was =+ until item 24 gave =+ back to Hoon, where =+ defines a core.  The
 * push is not a Hoon rune: there is no opcode that pushes a value onto the front
 * of a subject, so the shape is invented and the shift is the thing in this file
 * most likely to be got wrong.  See shape_push. */
static struct expr *parse_push(void)
{
    expect('=', "expected =>");
    expect('>', "expected =>, not =-");
    expect('(', "=> is written =>(a body)");

    struct expr *e = new_expr(K_RUNE);
    e->rune = 'P';
    e->nargs = 2;
    e->arg[0] = parse_expr();
    e->arg[1] = parse_expr();
    expect(')', "=> is closed with )");
    return e;
}

/* =+ : Hoon's core-defining rune, and the one that makes a loop writable here at
 * all.  =+(arm sample body) builds the core [|arm sample] -- the arm at the
 * head, the sample behind it -- and then calls it, so the whole expression is
 * the arm's answer.  Inside the body the subject is the core rather than the
 * session, which is why the arm needs its own address table; see shape_core.
 *
 * The sample has to be a |, because the arm's addresses are the sample's
 * addresses with one tail-step in front of them, and the compiler can only know
 * a |'s addresses if it emitted the | itself.  Anything else is refused by name
 * in emit_core. */
static struct expr *parse_core(void)
{
    expect('=', "expected =+");
    expect('+', "expected =+, not =- or =>");
    expect('(', "=+ is written =+(arm sample body)");

    struct expr *e = new_expr(K_RUNE);
    e->rune = 'C';
    e->nargs = 3;
    e->arg[0] = parse_name();
    e->arg[1] = parse_expr();
    e->arg[2] = parse_expr();
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
    if (is_name_start(*src))
        return parse_name();

    if (*src == '=') {
        if (src[1] == '+')
            return parse_core();
        if (src[1] == '>')
            return parse_push();
        if (src[1] == '-')
            refuse("=- is not a rune in this machine.  Opcode 8 pushes onto the "
                   "front of a subject and there is no opcode that pushes onto "
                   "its tail, so the head is the only end that can be pushed on.  "
                   "Write => and remember that every address inside the old "
                   "subject moves with it.");
        return parse_rune('=', 2, 2);
    }

    if (*src == '[')
        return parse_bracket();

    switch (*src) {
    case '?':
        if (src[1] == ':')
            return parse_cond();
        return parse_rune('?', 1, 1);
    case '~': return parse_rune('~', 2, 2);
    case '|': return parse_rune('|', 2, MAX_LIST);
    case '*': return parse_rune('*', 2, 2);
    case '+': return parse_rune('+', 1, 2);
    default:
        refuse("expected a number, an axis /, a name, a list [a b], or one of "
               "? ?: = | * + => =+");
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

    case K_NAME:
        /* A name is the arm of the core that is running, and as a value it is a
         * read of that arm: the arm is travelling in the noun rather than being
         * written down again, which is what lets an arm call itself.  This is
         * decisions.md item 23's mechanism, and the name is the same noun the
         * ~ below hands to the machine. */
        if (!in_core())
            refuse("the name '%s' is not the arm of any core here: a name means the "
                   "arm of a =+, and this is not inside one", e->name);
        /* Every name in a body is a read of the same /6, so without this a name
         * nobody bound would quietly mean the arm: =+(a |(0 0) b) and =+(a |(0 0) a)
         * are the same noun, and the second is what a reader meant. */
        if (strcmp(arm_name[arm_depth - 1], e->name) != 0)
            refuse("the name '%s' is not the arm here: the innermost core is for the "
                   "arm '%s', and a name means that arm, not some other one",
                   e->name, arm_name[arm_depth - 1]);
        return at(ARM_AXIS);

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

    case 'v':
        /* ?: -- if c then t else e, with 0 as true.  Opcode 6 wants the arms
         * the Hoon way round, so nothing is swapped here: the same three
         * formulas in the same order. */
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

    case '|': {
        /* Two or more things, side by side.  Nock cannot make a noun and can
         * only edit, so this is one edit per thing into a template of n zeros:
         * the first at axis 2, the second at axis 6, the third at 14, and so
         * on, each the head of the tail because the tail of a list is a list.
         * Axis 3 would be the whole tail and would make a two-word cell
         * instead, which is a real noun and is not one this machine's text can
         * say: the reader folds a bracket right-nested, so [1 2] is [1 2 0] and
         * there is no spelling of a cell whose tail is an atom at all.  book.c
         * makes its entries with these same two addresses, for the same
         * reason.  A list is also how a core's sample says what it holds, so
         * this is the one | whose meaning the shape table depends on. */
        noun t = lit(zeros(e->nargs));
        for (int i = e->nargs - 1; i >= 0; i--)
            t = edit((1ULL << (i + 2)) - 2, emit(e->arg[i]), t);
        return t;
    }

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
        /* => : push a's value on, then run the body there.  a is compiled in
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

    case '~':
        /* ~(arm core) -- run the arm that core holds, on that core.  Opcode 9
         * wants an address and a core, and the name is the address written the
         * way a reader writes it: the arm of a core is always at /6, whatever
         * the sample is, so the name compiles to the number 6 and the machine
         * gets that as a constant.  A number is refused in this position on
         * purpose -- it would let a core call an address nobody has checked --
         * so an arm is only ever called by its name. */
        if (e->arg[0]->kind != K_NAME)
            refuse("an arm is called by its name, and this is a %s where the name "
                   "goes: a bare address here could be a typo that names a real "
                   "arm of the core instead of the one meant",
                   e->arg[0]->kind == K_AXIS ? "read" : "number");
        if (!in_core())
            refuse("an arm call has to be inside a =+, because the name here is the "
                   "arm of the core it sits in, and this is not inside one");
        /* A core is [sample arm 0], so the thing an arm is called on has to be a
         * two-thing list whose first thing is itself a list.  Anything else is a
         * noun with no arm in it, and the machine would find that out by
         * descending into an atom. */
        if (e->arg[1]->kind != K_RUNE || e->arg[1]->rune != '|' || e->arg[1]->nargs != 2 ||
            e->arg[1]->arg[0]->kind != K_RUNE || e->arg[1]->arg[0]->rune != '|')
            refuse("an arm is called on a core, which is a two-thing list whose first "
                   "thing is the sample: [sample arm 0], and this is not one");
        {
            struct axis  tbl[MAX_AXES];
            int          n = 0;
            int          i;
            const struct axis *want = arm_sample[arm_depth - 1];
            int          want_n = arm_sample_len[arm_depth - 1];

            list_shape_walk(e->arg[1]->arg[0], 1, 0, "the sample", tbl, &n);
            if (n != want_n)
                refuse("this arm call builds a sample with %d address%s where the "
                       "core it sits in has %d, so the addresses the arm uses would "
                       "mean something else in the new core: a sample that goes back "
                       "into a core has to be put back the same way round",
                       n, n == 1 ? "" : "es", want_n);
            /* Only the shape is compared, not the expressions that fill it.  A
             * sample that counts itself is a read where the one it came from was
             * a number, and that is the whole point of rebuilding one. */
            for (i = 0; i < n; i++)
                if (tbl[i].at != want[i].at || tbl[i].nargs != want[i].nargs)
                    refuse("this arm call builds a sample whose /%llu is %s where the "
                           "core it sits in has %s, so the addresses the arm uses "
                           "would mean something else in the new core: a sample that "
                           "goes back into a core has to be put back the same way round",
                           (unsigned long long)tbl[i].at,
                           tbl[i].nargs ? "a list of a different number of things" : "a single thing",
                           want[i].nargs ? "a list of a different number of things" : "a single thing");
        }
        /* nock.c's opcode 9 reads the axis out of the formula itself rather than
         * evaluating it, so the arm goes in as a bare atom and not behind a
         * constant.  Opcode 10 is the other way round, and the two are not
         * interchangeable: an axis that is a formula is a crash, not a read. */
        return f2(9, A(ARM_AXIS), emit(e->arg[1]));

    case 'C':
        /* =+(arm sample body) -- a core.  The core is [sample arm 0]: the arm at
         * /6 and the sample at the head, so the sample's own addresses sit one
         * head-step in front of the ones it has out here (the first thing in the
         * sample is /4, the second /10).  The body is compiled in the core's
         * coordinates, which is the shape table's job and the reason this is one
         * recursive walk and not a macro.  The arm is written into the core as a
         * constant, so the machine recurses at run time on a noun of a fixed
         * size rather than the compiler unrolling anything. */
        if (e->arg[1]->kind != K_RUNE || e->arg[1]->rune != '|')
            refuse("the sample of a core is a |, so that the compiler knows what "
                   "addresses the body may use, and this sample is not one");
        a = emit(e->arg[1]);                    /* the sample, in the outer subject */
        shape_core_enter();
        list_shape_core(e->arg[1], 1, 0, "the sample");
        if (arm_depth >= MAX_ARM_DEPTH)
            refuse("this nests arms %d deep, which is more than the compiler tracks",
                   MAX_ARM_DEPTH);
        snprintf(arm_name[arm_depth], MAX_NAME, "%s", e->arg[0]->name);
        /* The walk is already in the table, after the four fixed entries. */
        arm_sample_len[arm_depth] = shape_len - 4;
        for (int i = 0; i < arm_sample_len[arm_depth]; i++)
            arm_sample[arm_depth][i] = shape[4 + i];
        arm_depth++;
        b = emit(e->arg[2]);                    /* the body, in the core's coordinates */
        arm_depth--;
        shape_core_leave();
        return f2(9, A(ARM_AXIS), edit(ARM_AXIS, lit(b),
                                       edit(2, a, lit(two_zeros()))));

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
    /* The compiler's own stacks, and the same reason.  A refusal in the middle
     * of a =+ longjmps straight out of emit(), so the arm is still pushed and
     * the core's shape table is still installed when the next line is read --
     * and the next line then believes it is inside a core, because the name it
     * wrote resolves to an arm that a refused compile left behind.  Three of
     * this file's own tests were failing that way before this line existed. */
    core_depth = 0;
    arm_depth = 0;
    shape_len = 0;
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

/* Print a noun the way the machine prints one, into the capture buffer, so the text can be
 * compared and read back. */
static size_t print_to_capture(noun n)
{
    capture_begin();
    noun_print(n);
    capture_end();
    return capture_len();
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
    { "?: is opcode 6, and 0 takes the first arm", "?:(/14 7 8)", "[6 [[0 [14 0]] [[1 [7 0]] [[1 [8 0]] 0]]]]", 0, "7" },
    { "and 1 takes the other arm", "?:(/14 7 8)", "[6 [[0 [14 0]] [[1 [7 0]] [[1 [8 0]] 0]]]]", 1, "8" },

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
    { "two things are two edits into a template of three zeros", "|(1 3)", "[10 [[2 [1 [1 0]]] [[10 [[6 [1 [3 0]]] [[1 [[0 [0 0]] 0]] 0]]] 0]]]", 0, "[1 3 0]" },
    { "and the second thing can come from the subject", "|([1 7] /14)", "[10 [[2 [10 [[2 [1 [1 0]]] [[10 [[6 [1 [7 0]]] [[1 [[0 [0 0]] 0]] 0]]] 0]]]] [[10 [[6 [0 [14 0]]] [[1 [[0 [0 0]] 0]] 0]]] 0]]]", 2, "[[1 7 0] 2 0]" },
    { "a list may hold three things, at /2 /6 and /14", "|(1 3 5)", "[10 [[2 [1 [1 0]]] [[10 [[6 [1 [3 0]]] [[10 [[14 [1 [5 0]]] [[1 [[0 [0 [0 0]]] 0]] 0]]] 0]]] 0]]]", 0, "[1 3 5 0]" },
    { "and [a b c] is the same noun as |(a b c)", "[1 3 5]", "[10 [[2 [1 [1 0]]] [[10 [[6 [1 [3 0]]] [[10 [[14 [1 [5 0]]] [[1 [[0 [0 [0 0]]] 0]] 0]]] 0]]] 0]]]", 0, "[1 3 5 0]" },

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
    { "composition calls b's value on a's value", "*(|(1 3) |(0 2))", "[2 [[10 [[2 [1 [1 0]]] [[10 [[6 [1 [3 0]]] [[1 [[0 [0 0]] 0]] 0]]] 0]]] [[10 [[2 [1 [0 0]]] [[10 [[6 [1 [2 0]]] [[1 [[0 [0 0]] 0]] 0]]] 0]]] 0]]]", 0, "1" },

    /* Increment, and the one thing the host is allowed to do by itself. */
    { "increment is a push, then opcode 4 on the head", "+(/14)", "[8 [[0 [14 0]] [[4 [[0 [2 0]] 0]] 0]]]", 2, "3" },
    { "two literals are folded on the host, so it is a constant", "+(2 3)", "[1 [5 0]]", 0, "5" },

    /* =+ , and the shift.  Inside the body the new subject's /2 is the value
     * just pushed and its /3 is the whole old session, and every address
     * inside the old session has moved: the count is at /30 and the newest line
     * at /24.  /30 is book.c's AX_COUNT arrived at from the other side, and
     * /24 is the one that 2a + 2 would have got wrong. */
    { "=> pushes on the front and runs the body there", "=>(/14 ?:(/2 1 2))", "[8 [[0 [14 0]] [[6 [[0 [2 0]] [[1 [1 0]] [[1 [2 0]] 0]]]] 0]]]", 0, "1" },
    { "with the count moved, the same expression is false", "=>(/14 ?:(/2 1 2))", "[8 [[0 [14 0]] [[6 [[0 [2 0]] [[1 [1 0]] [[1 [2 0]] 0]]]] 0]]]", 1, "2" },
    { "inside =>, the whole old session is at /3", "=>(/14 /3)", "[8 [[0 [14 0]] [[0 [3 0]] 0]]]", 0, "[0 0 0 0]" },
    { "inside =>, the last answer is at /14", "=>(/14 /14)", "[8 [[0 [14 0]] [[0 [14 0]] 0]]]", 5, "5" },
    { "inside =>, the log is at /6, and an empty log is the atom 0", "=>(/14 /6)", "[8 [[0 [14 0]] [[0 [6 0]] 0]]]", 0, "0" },
    { "inside =>, the count is at /30, as in book.c", "=>(/14 /30)", "[8 [[0 [14 0]] [[0 [30 0]] 0]]]", 5, "5" },
    { "inside =>, the newest line is at /24 and not /18", "=>(/14 /24)", "[8 [[0 [14 0]] [[0 [24 0]] 0]]]", 5, "[1 5 0]" },
    { "a second push shifts again, so the count is at /62", "=>(/14 =>(/30 /62))", "[8 [[0 [14 0]] [[8 [[0 [30 0]] [[0 [62 0]] 0]]] 0]]]", 7, "7" },

    /* Cores and names: =+(arm sample body) and ~(arm core), which are the two
     * halves of the thing decisions.md item 23 said was possible and item 24
     * measures.  A core is [sample arm 0], so the arm is at /6 whatever the
     * sample is, and the sample's own addresses sit one HEAD-step in front of
     * the ones they have out here: a two-thing sample is /4 and /10, a
     * three-thing one is /4 /10 /22, and a | inside the sample is a walk rather
     * than a formula.  The arm is written into the core as a constant, so the
     * machine recurses at run time on a noun of a fixed size -- the loop below
     * answers 1000 in the same 165 nouns the 5 does, which is the size claim
     * measured rather than asserted.
     *
     * Only the first of these carries a text, because it is the one short
     * enough to read.  The rest are checked by the round trip and the
     * interpreter below, and a NULL here skips the spelling check and nothing
     * else. */
    { "a core is [sample arm 0], and its arm is at /6", "=+(arm |(0 5) /4)", "[9 [6 [[10 [[6 [1 [[0 [4 0]] 0]]] [[10 [[2 [10 [[2 [1 [0 0]]] [[10 [[6 [1 [5 0]]] [[1 [[0 [0 0]] 0]] 0]]] 0]]]] [[1 [[0 [0 0]] 0]] 0]]] 0]]] 0]]]", 0, "0" },
    { "a core's second thing is at /10", "=+(arm |(0 5) /10)", NULL, 0, "5" },
    { "a three-thing sample puts its third thing at /22", "=+(arm |(0 1 2) /22)", NULL, 0, "2" },
    { "a | inside the sample is a walk: its 5 is at /18", "=+(arm |(|(0 5) 9) /18)", NULL, 0, "5" },
    { "a list is an address as well as a container, so it can be passed on", "=+(arm |(|(0 5) 9) ?(/4))", NULL, 0, "0" },
    { "a name is a read of the arm, and the arm is a cell", "=+(arm |(0 1) ?(arm))", NULL, 0, "0" },
    { "a core inside a core, and the shape comes back afterwards", "=+(a |(0 0) =+(b |(0 5) /10))", NULL, 0, "5" },
    /* The loop.  The new core is a two-thing list too: the new sample in front,
     * the same arm behind it -- and `arm` here is a read of /6, so the arm goes
     * back into the core it came from rather than travelling through the sample.
     * Every step is a read of an address the first walk gave; nothing is passed
     * down the recursion but the new core, and the formula never grows. */
    { "the loop: six calls, and the base case answers 1", "=+(arm |(0 5) ?:(=(/4 /10) 1 ~(arm |(|(+(/4) /10) arm))))", NULL, 0, "1" },
    { "and the base case fires at once when the limit is already met", "=+(arm |(0 0) ?:(=(/4 /10) /10 ~(arm |(|(+(/4) /10) arm))))", NULL, 0, "0" },
    { "the loop answers with a value it carried through every call", "=+(arm |(0 5) ?:(=(/4 /10) /10 ~(arm |(|(+(/4) /10) arm))))", NULL, 0, "5" },
    { "and with the last value it computed", "=+(arm |(0 5) ?:(=(/4 /10) /4 ~(arm |(|(+(/4) /10) arm))))", NULL, 0, "5" },
    { "a thousand is the same loop, and the same size", "=+(arm |(0 1000) ?:(=(/4 /10) 1 ~(arm |(|(+(/4) /10) arm))))", NULL, 0, "1" },
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
    /* What used to be a passing test.  The sample was rebuilt the other way
     * round, so the arm took the counter's address and the counter took the
     * limit's: the loop answered 5 instead of 1, every time, silently, and the
     * comment above it said it counted.  It is a refusal now, and it is the
     * reason the walk of a sample is compared when a sample goes back into a
     * core. */
    { "a sample rebuilt the other way round is a refusal, not a wrong answer",
      "=+(arm |(0 5) ?:(=(/4 /10) 1 ~(arm |(arm |(+(/4) /10)))))",
      "an arm is called on a core, which is a two-thing list whose first thing "
      "is the sample: [sample arm 0], and this is not one" },
    { "and a sample with more things in it than the core has addresses for",
      "=+(arm |(0 5) ?:(=(/4 /10) 1 ~(arm |(|(+(/4) /10 0) arm))))",
      "this arm call builds a sample with 3 addresses where the core it sits in "
      "has 2, so the addresses the arm uses would mean something else in the "
      "new core: a sample that goes back into a core has to be put back the same "
      "way round" },
    { "and a name in a body that no core bound, rather than a quiet read of the arm",
      "=+(a |(0 0) b)",
      "the name 'b' is not the arm here: the innermost core is for the arm 'a'" },
    { "and an arm called on something that is not a core",
      "=+(arm |(0 5) ?:(=(/4 /10) 1 ~(arm 42)))",
      "an arm is called on a core, which is a two-thing list whose first thing "
      "is the sample: [sample arm 0], and this is not one" },
    { "an address the session does not have", "/37",
      "are /2 the log /6 the last answer /8 the newest line /14 the count "
      "/18 that line's answer" },
    { "the wrong way round, which book.c names: /5, /13, /29", "/5",
      "is not an address of this subject" },
    { "doubling a session address, which also does not work", "/28",
      "is not an address of this subject" },
    { "an address that has moved, inside a push", "=>(/14 /8)",
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
    { "a rune that is never closed", "?:(/14 1 2", "closed with )" },
    { "a rune with no opening bracket", "? /6", "a rune is a character and then (" },
    { "two expressions where one was wanted", "/14 /6",
      "there is more after the expression" },
    { "something that is not an expression at all", "]", "expected a number" },
    { "nothing at all", "", "expected a number" },

    /* Cores and names, which are the other half of the language and the part
     * where a wrong answer is a wrong noun rather than a crash. */
    { "a name outside a core, which is not the arm of anything", "arm",
      "is not the arm of any core here" },
    { "a name in the sample of a core, where there is no arm yet",
      "=+(arm |(0 arm) /4)", "is not the arm of any core here" },
    { "a sample that is not a list, so the compiler cannot know its addresses",
      "=+(arm /2 *(/2 /6))",
      "the sample of a core is a |, so that the compiler knows what addresses" },
    { "an arm called by a number rather than by its name", "~(/6 |(0 0))",
      "an arm is called by its name" },
    { "an arm call outside a core", "~(arm |(0 0))",
      "an arm call has to be inside a =+" },
    { "an address of the core that is not the core's", "=+(arm |(0 5) /8)",
      "are /1 the whole of the core /2 the sample /3 the arm and the end of the "
      "list /6 the arm /4 the first thing of the sample" },
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
        /* A case that refuses is a failure and not a vanished case.  It used to
         * be neither: bad was set, so every check below was skipped and nothing
         * was counted, which is how "|([1 7] /14)" sat in this table refusing
         * "[1 7] is not a thing this language can read" through several
         * versions of the file.  A test that cannot fail is not a test. */
        if (bad)
            no(c->what, "'%s' was refused, saying\n         %s", c->source,
               refuse_msg);
        else if (c->text != NULL) {
            print_to_capture(formula);
            if (capture_over())
                no(c->what, "the formula did not fit in %d characters", GB_LINE_MAX);
            else if (strcmp(capture_text(), c->text) != 0)
                no(c->what, "said\n         %s\n         wanted\n         %s",
                   capture_text(), c->text);
        }

        /* 2. The round trip: the machine's own reader, on the machine's own
         *    printed text, must give back the noun that went in.  A formula the
         *    reader refuses is a formula the guest will never run, and the
         *    reader's own limits are enforced here rather than in a comment. */
        if (!bad) {
            noun        back = 0;
            const char *rwhy = "";

            print_to_capture(formula);
            if (gb_parse(capture_text(), (u64)capture_len(), &back, &rwhy) != GB_PARSE_OK)
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
                no(c->what, "answered %s, wanted %s", capture_text(), c->answer);
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
                "  arm       the arm of this core   ?(a)      cell test\n"
                "  =(a b)    equality, 0 if the same\n"
                "  ?:(c t e) if c then t else e    *(a b)    call b's value on a's\n"
                "  [a b c]   the list [a b c 0]    |(a b c)  the same, spelt with a rune\n"
                "  +(a)      a plus one             +(a b)    a plus b, both literals\n"
                "  =>(a body)   push a on the front, then run body\n"
                "  =+(arm sample body)   a core, and an arm that can call itself\n"
                "  ~(arm core)   call the arm that core holds, on that core\n");
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
    memcpy(text, capture_text(), capture_len() + 1);
    fputs(text, stdout);
    fputc('\n', stdout);
    return 0;
}
