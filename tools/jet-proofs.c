/* jet-proofs -- the native primitives against the Nock definitions that say
 * what they are allowed to be.
 *
 * docs/decisions.md item 9 is the rule this file carries out: a native may only
 * exist if some Nock formula computes the same thing.  The formula is what the
 * language means and the native is only ever a faster way to arrive at it, so
 * both halves of that claim are checked here rather than asserted in a
 * comment -- the formula has to be a formula the machine can actually run, and
 * the native has to be the same function of the same inputs.
 *
 * A proof here is always these steps, in this order:
 *
 *   1. The definition is Nock, in the bracket notation the specification uses,
 *      with the rule that makes it so written beside it.  It goes in through
 *      the machine's own reader, so a definition the guest could never be sent
 *      fails here instead of surprising somebody later.
 *   2. The machine's own printer writes the noun back out, and the bytes have
 *      to be the table's own spelling.  The notation in the table is therefore
 *      the machine's notation rather than a paraphrase of it, which is what
 *      makes a typo in the table impossible to miss.
 *   3. The definition is run by the machine's own interpreter, on a subject
 *      holding the operands, and its answer is compared with the native's --
 *      including the inputs where one of them stops.  A native that answers
 *      where its definition would have stopped, or stops where the definition
 *      would have answered, is the one way to make a wrong machine faster, so
 *      those are counted apart from the answers rather than inside them.
 *
 * The subject is [a b 0]: the first operand at /2, the second at /6.  Reading
 * the operands out of the subject instead of writing them into the definition
 * is most of the point -- a definition with the numbers baked in would agree
 * with the native on those two numbers and say nothing about any others.  A
 * three-element list numbers its elements 2, 6 and 14, the same way the book's
 * does, and /3 is not the second element: it is the pair [b 0], which is a cell
 * rather than an operand.
 *
 * What the battery is and is not.  For every primitive proved below, the rule
 * that gives its definition is short enough to read outright, and the
 * equivalence is settled by setting that rule beside the two lines of C that
 * implement it.  Thousands of inputs cannot establish that.  They are here for
 * the one thing a battery can do: catch the day somebody edits a native to be
 * cleverer than the opcode it stands for.  Where a definition can stop, the
 * battery has to reach an input that stops it, and that is checked rather than
 * assumed -- a crash path nothing ever arrives at has not been proven, it has
 * only been left alone.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "kernel.h"
#include "host-machine.h"

/* --- the tally -------------------------------------------------------------- */
/* The same shape as the compiler's self-test: one line per claim, and a count
 * at the end that has to be zero.  A proof that cannot fail is a comment. */

static int tally_pass;
static int tally_fail;

static void ok(const char *what)
{
    tally_pass++;
    printf("  pass  %s\n", what);
}

static void no(const char *what, const char *fmt, ...)
{
    va_list ap;

    tally_fail++;
    printf("  FAIL  %s\n         ", what);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* --- the definitions -------------------------------------------------------- */

struct definition {
    const char *name;     /* the native, as the bank spells it */
    const char *formula;  /* its Nock definition, in the machine's own notation */
    const char *rule;     /* why that text computes what it computes */
    int         can_stop; /* whether the definition is allowed to stop at all */
};

/* Opcode 4 is the specification's increment: *[a 4 b] is the product of b, plus
 * one.  Here b reads /2, so the product is a and the whole formula is a+1.
 * The stop at 2^63 - 1 is not a second guard that happens to agree: it is
 * opcode 4's own, which is the very thing the native is standing in for.
 *
 * Opcode 5 compares the products of its two arms and answers 0 when they are
 * the same noun and 1 when they are not.  Both arms read an atom out of the
 * subject, so the answer is 0 exactly when a and b are equal, which is what the
 * native says.  One thing worth being honest about: opcode 5 is structural, so
 * it also answers for cells and this definition inherits that -- but no native
 * here takes a cell, since a native is handed two u64s, so the domain that has
 * to agree is the atoms alone. */
static const struct definition definitions[] = {
    { "+inc", "[4 [[0 [2 0]] 0]]",
      "opcode 4 is the specification's increment: it adds one to the product "
      "of its arm, and that arm reads the operand at /2", 1 },
    { "+eq",  "[5 [[0 [2 0]] [[0 [6 0]] 0]]]",
      "opcode 5 answers 0 when the products of its two arms are the same noun, "
      "and the arms read /2 and /6, so this is 0 exactly when a equals b", 0 },
};

#define N_DEFINITIONS ((int)(sizeof definitions / sizeof definitions[0]))

/* --- the natives with no definition yet ------------------------------------- */

struct pending {
    const char *name;
    const char *why;
};

/* One reason runs through all of these, and it is the same reason in every
 * case: Nock has no loop.  A core that can call its own arm is a noun that has
 * to contain itself, and the notation for that in Hoon is a name -- `=+(a b)`,
 * where `a` names the arm being written.  Lamp's language has no names, so it
 * cannot write a core that refers to itself, so it cannot write a definition
 * that iterates.  Every operation below is a loop over the 63 bits of its
 * operands: a carry chain for +add, a bit scan for the comparisons, a
 * repetition for the bitwise ones, a long division for +div.  Unrolling 63
 * steps would be longer than a line of input and would prove nothing a loop
 * does not, so they wait for names.  See docs/decisions.md item 9. */
static const struct pending pending[] = {
    { "+add", "a carry chain over 63 bits, which needs a loop" },
    { "+sub", "a borrow chain, which needs a loop" },
    { "+mul", "63 shift-and-add steps, which needs a loop" },
    { "+div", "a long division, which needs a loop" },
    { "+mod", "the remainder that long division leaves behind" },
    { "+min", "a comparison, and the comparisons need a bit scan" },
    { "+max", "a comparison, and the comparisons need a bit scan" },
    { "+lt",  "a bit scan from the top down, which needs a loop" },
    { "+le",  "a bit scan from the top down, which needs a loop" },
    { "+gt",  "a bit scan from the top down, which needs a loop" },
    { "+ge",  "a bit scan from the top down, which needs a loop" },
    { "+and", "63 bit positions, which needs a loop" },
    { "+or",  "63 bit positions, which needs a loop" },
    { "+xor", "63 bit positions, which needs a loop" },
    { "+lsh", "a shift across 63 positions, which needs a loop" },
    { "+rsh", "a shift across 63 positions, which needs a loop" },
    { "+dec", "a borrow chain, which needs a loop" },
    { "+not", "63 bit positions, which needs a loop" },
};

#define N_PENDING ((int)(sizeof pending / sizeof pending[0]))

/* --- the battery ------------------------------------------------------------ */

/* The values worth trying.  Zero and one, both ends of the atom range, powers
 * of two with a neighbour on either side, and the places a shift or a carry is
 * most likely to go wrong. */
static const u64 interesting[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8,
    15, 16, 17, 31, 32, 33, 62, 63, 64, 65,
    1000, 65535, 65536,
    (u64)1 << 31, ((u64)1 << 31) + 1,
    (u64)1 << 32, (u64)1 << 40,
    (u64)1 << 61, (u64)1 << 62,
    NOUN_ATOM_MAX - 1, NOUN_ATOM_MAX
};

#define N_INTERESTING ((int)(sizeof interesting / sizeof interesting[0]))

/* A fixed linear congruential sweep, on MMIX's multiplier and increment, from a
 * seed of 1.  Fixed because a suite whose failures cannot be reproduced is a
 * suite that gets ignored.  The shift down by one is what keeps a draw inside
 * the atom range: these are naturals, and a draw above 2^63 - 1 is not an atom
 * this machine has. */
static u64 lcg_next(u64 *state)
{
    *state = *state * 6364136223846793005ULL + 1442695040888963407ULL;
    return *state >> 1;
}

#define SWEEP_PAIRS 512

/* --- running one case both ways --------------------------------------------- */

struct counted {
    u64 agree;        /* the same atom from both */
    u64 both_stop;    /* both refused, which is agreement of a kind */
    u64 wrong;        /* both answered, and the answers differ */
    u64 nock_stopped; /* the definition stopped and the native answered */
    u64 prim_stopped; /* the native stopped and the definition answered */
    u64 cell_answer;  /* the definition answered with a cell, which no native can */
    u64 no_subject;   /* there was no arena left to build a subject in */
    u64 total;
    char first[256];  /* the first failure, spelled out */
};

#define MSG_MAX 64

static void run_case(int index, noun formula, u64 a, u64 b, struct counted *c)
{
    char     nock_msg[MSG_MAX];
    char     prim_msg[MSG_MAX];
    noun     subject;
    noun     answer  = 0;
    u64      native;
    int      nock_stopped;
    int      prim_stopped;
    int      disagree = 0;

    c->total++;

    /* Cleared before the subject is built, not after.  noun_cons() refuses to
     * allocate while an error is standing, so an input whose native stopped
     * would otherwise leave the next input with no subject at all -- and the
     * failure that reports is "tree address descends into an atom", which
     * sends whoever reads it looking for a tree bug that is not there. */
    machine_reset_error();

    /* [a b 0] is cons(a, cons(b, 0)), so the first operand is at /2 and the
     * second at /6: a three-element list numbers its elements 2, 6, 14, the
     * same way the book's does.  /3 is the pair [b 0] rather than b, which is
     * a cell, and a definition that reads it stops for the wrong reason. */
    subject = noun_cons(noun_atom(a), noun_cons(noun_atom(b), noun_atom(0)));
    if (subject == 0) {
        snprintf(c->first, sizeof c->first,
                 "no subject for %llu %llu: %s", (unsigned long long)a,
                 (unsigned long long)b, machine_err_msg);
        c->no_subject++;
        return;
    }

    /* Each message is copied out before the next run, because the second run
     * overwrites it and a report that names the wrong crash is worse than no
     * report. */
    machine_reset_error();
    nock_stopped = (nock_run(subject, formula, &answer) != NOCK_OK);
    snprintf(nock_msg, sizeof nock_msg, "%s", machine_err_msg);

    machine_reset_error();
    native       = prim_call(index, a, b);
    prim_stopped = (machine_err_msg[0] != '\0');
    snprintf(prim_msg, sizeof prim_msg, "%s", machine_err_msg);

    if (nock_stopped && prim_stopped) {
        c->both_stop++;
    } else if (nock_stopped) {
        c->nock_stopped++;
        disagree = 1;
    } else if (prim_stopped) {
        c->prim_stopped++;
        disagree = 1;
    } else if (!noun_is_atom(answer)) {
        c->cell_answer++;
        disagree = 1;
    } else if (noun_atom_val(answer) != native) {
        c->wrong++;
        disagree = 1;
    } else {
        c->agree++;
    }

    if (!disagree)
        return;

    /* First failure only: a battery that prints all of them buries the one
     * worth reading. */
    if (c->first[0] != '\0')
        return;

    if (nock_stopped)
        snprintf(c->first, sizeof c->first,
                 "on %llu %llu the definition stopped (%s) and the native "
                 "answered %llu",
                 (unsigned long long)a, (unsigned long long)b, nock_msg,
                 (unsigned long long)native);
    else if (prim_stopped)
        snprintf(c->first, sizeof c->first,
                 "on %llu %llu the native stopped (%s) and the definition "
                 "answered %llu",
                 (unsigned long long)a, (unsigned long long)b, prim_msg,
                 (unsigned long long)noun_atom_val(answer));
    else if (!noun_is_atom(answer))
        snprintf(c->first, sizeof c->first,
                 "on %llu %llu the definition answered a cell, which no native "
                 "here can", (unsigned long long)a, (unsigned long long)b);
    else
        snprintf(c->first, sizeof c->first,
                 "on %llu %llu the definition answered %llu and the native "
                 "answered %llu",
                 (unsigned long long)a, (unsigned long long)b,
                 (unsigned long long)noun_atom_val(answer),
                 (unsigned long long)native);
}

/* --- proving one primitive -------------------------------------------------- */

static void prove(const struct definition *d)
{
    noun        formula = 0;
    noun        back    = 0;
    const char *why     = "";
    struct counted c;
    int  index;
    int  i, j;

    printf("== %s\n", d->name);
    printf("  definition  %s\n", d->formula);
    printf("  %s\n", d->rule);

    /* 1. the reader takes it */
    if (gb_parse(d->formula, (u64)strlen(d->formula), &formula, &why)
        != GB_PARSE_OK) {
        no("the definition is Nock the machine's reader takes",
           "the reader refused it: %s", why);
        return;
    }
    ok("the definition is Nock the machine's reader takes");

    /* 2. the machine prints it back as the table spells it */
    capture_begin();
    noun_print(formula);
    capture_end();
    if (capture_over()) {
        no("the machine prints it back as the table spells it",
           "the printed text did not fit in %d characters", GB_LINE_MAX);
    } else if (strcmp(capture_text(), d->formula) != 0) {
        no("the machine prints it back as the table spells it",
           "printed\n         %s\n         the table says\n         %s",
           capture_text(), d->formula);
    } else {
        ok("the machine prints it back as the table spells it");
    }

    /* The round trip as well, so the claim is about the noun and not only about
     * the bytes: what the machine printed has to read back as what went in. */
    if (gb_parse(capture_text(), (u64)capture_len(), &back, &why)
        != GB_PARSE_OK) {
        no("the reader reads the printed form back as the same noun",
           "it refused the printed text: %s", why);
    } else if (!noun_equal(back, formula)) {
        no("the reader reads the printed form back as the same noun",
           "it read back a different noun");
    } else {
        ok("the reader reads the printed form back as the same noun");
    }

    index = prim_index(d->name);
    if (index < 0) {
        no("the bank has this native", "'%s' is not in the bank", d->name);
        return;
    }

    memset(&c, 0, sizeof c);

    for (i = 0; i < N_INTERESTING; i++)
        for (j = 0; j < N_INTERESTING; j++)
            run_case(index, formula, interesting[i], interesting[j], &c);

    {
        u64 state = 1;

        for (i = 0; i < SWEEP_PAIRS; i++) {
            /* Drawn into locals first: two calls in one argument list would
             * leave the order the compiler picks them in unspecified, and a
             * battery that cannot be reproduced is a battery that gets
             * ignored. */
            u64 a = lcg_next(&state);
            u64 b = lcg_next(&state);

            run_case(index, formula, a, b, &c);
        }
    }

    /* 3. the battery */
    if (c.wrong || c.nock_stopped || c.prim_stopped || c.cell_answer
        || c.no_subject) {
        no("the definition and the native agree on every input tried",
           "%llu of %llu inputs disagree: %s",
           (unsigned long long)(c.wrong + c.nock_stopped + c.prim_stopped
                                + c.cell_answer + c.no_subject),
           (unsigned long long)c.total, c.first);
    } else {
        ok("the definition and the native agree on every input tried");
        printf("         %llu inputs: %llu the same atom, %llu stopping in both\n",
               (unsigned long long)c.total, (unsigned long long)c.agree,
               (unsigned long long)c.both_stop);
    }

    /* 4. and where it can stop, the battery has to have got there */
    if (d->can_stop) {
        if (c.both_stop == 0)
            no("the battery reaches an input the definition stops on",
               "nothing in %d interesting values and %d swept pairs stops it, so "
               "its stopping path is untested", N_INTERESTING, SWEEP_PAIRS);
        else
            ok("the battery reaches an input the definition stops on");
    }
}

/* --- every native accounted for --------------------------------------------- */

/* Two directions, and both matter.  A primitive in neither table is a native
 * whose right to exist has never been argued, which is the failure this whole
 * file is about.  A name in a table that is not in the bank is a typo that
 * would otherwise sit here looking like a proof forever. */
static int is_proved(const char *name)
{
    int i;

    for (i = 0; i < N_DEFINITIONS; i++)
        if (strcmp(definitions[i].name, name) == 0)
            return 1;
    return 0;
}

static int is_pending(const char *name)
{
    int i;

    for (i = 0; i < N_PENDING; i++)
        if (strcmp(pending[i].name, name) == 0)
            return 1;
    return 0;
}

static void coverage(void)
{
    int total = prim_count();
    int seen  = 0;
    int bad   = 0;
    int i;

    for (i = 0; i < total; i++) {
        const prim_entry *e = prim_get(i);
        int hits;

        if (e == NULL)
            continue;
        hits = is_proved(e->name) + is_pending(e->name);
        if (hits != 1) {
            no("every native in the bank is accounted for exactly once",
               "'%s' appears %d times across the two tables", e->name, hits);
            bad++;
        }
    }

    for (i = 0; i < N_DEFINITIONS; i++)
        if (prim_index(definitions[i].name) < 0) {
            no("every name in the tables is a native that exists",
               "'%s' is proved here but is not in the bank", definitions[i].name);
            bad++;
        }

    for (i = 0; i < N_PENDING; i++)
        if (prim_index(pending[i].name) < 0) {
            no("every name in the tables is a native that exists",
               "'%s' is listed as pending but is not in the bank",
               pending[i].name);
            bad++;
        }

    seen = N_DEFINITIONS + N_PENDING;
    if (bad == 0 && seen != total) {
        no("every native in the bank is accounted for exactly once",
           "%d in the bank, %d in the tables", total, seen);
        bad++;
    }

    if (bad == 0)
        ok("every native in the bank is accounted for exactly once, and every "
           "name in the tables is a native that exists");

    printf("         %d in the bank: %d proved, %d pending\n",
           total, N_DEFINITIONS, N_PENDING);

}

/* --- main -------------------------------------------------------------------- */

int main(void)
{
    int i;

    noun_init();
    nock_init(NOCK_DEFAULT_STEP_LIMIT);

    printf("== the bank\n");
    coverage();

    printf("\n== the definitions\n");
    for (i = 0; i < N_DEFINITIONS; i++) {
        if (i > 0)
            printf("\n");
        prove(&definitions[i]);
    }

    printf("\n== and the %d with no definition yet\n", N_PENDING);
    for (i = 0; i < N_PENDING; i++)
        printf("  %-5s %s\n", pending[i].name, pending[i].why);

    printf("\n%d checks, %d failed\n", tally_pass + tally_fail, tally_fail);
    return tally_fail == 0 ? 0 : 1;
}
