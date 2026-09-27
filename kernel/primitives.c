/* The native primitive bank.
 *
 * Twenty integer operations and nothing else.  This table is the entire
 * native trust base of the machine: everything Nock can compute ultimately
 * bottoms out here or in the interpreter's own opcode implementations, and
 * both are short enough to read in one sitting.
 *
 * Overflow policy: Nock atoms are naturals, so there is nowhere for a
 * negative number to go, and a solid-state machine has no business silently
 * wrapping around.  Every operation that would leave the 63-bit atom range
 * crashes instead.  Division and remainder by zero crash.  There is no
 * undefined behaviour to work around: either the answer fits in an atom, or
 * the machine stops and says why.
 */

#include "kernel.h"

#define A2 (PRIM_MAX_ARGS)

static u64 p_add(u64 a, u64 b)
{
    if (a > NOUN_ATOM_MAX - b) {
        machine_crash("+add: result does not fit in an atom");
        return 0;
    }
    return a + b;
}

static u64 p_sub(u64 a, u64 b)
{
    if (b > a) {
        machine_crash("+sub: atoms are natural numbers, so a - b < 0");
        return 0;
    }
    return a - b;
}

static u64 p_mul(u64 a, u64 b)
{
    if (a != 0 && b > NOUN_ATOM_MAX / a) {
        machine_crash("+mul: result does not fit in an atom");
        return 0;
    }
    return a * b;
}

static u64 p_div(u64 a, u64 b)
{
    if (b == 0) {
        machine_crash("+div: division by zero");
        return 0;
    }
    return a / b;
}

static u64 p_mod(u64 a, u64 b)
{
    if (b == 0) {
        machine_crash("+mod: remainder by zero");
        return 0;
    }
    return a % b;
}

static u64 p_min(u64 a, u64 b) { return a < b ? a : b; }
static u64 p_max(u64 a, u64 b) { return a > b ? a : b; }

static u64 p_eq(u64 a, u64 b) { return a == b ? 0 : 1; }  /* 0 is true, as in Nock */
static u64 p_lt(u64 a, u64 b) { return a <  b ? 0 : 1; }
static u64 p_le(u64 a, u64 b) { return a <= b ? 0 : 1; }
static u64 p_gt(u64 a, u64 b) { return a >  b ? 0 : 1; }
static u64 p_ge(u64 a, u64 b) { return a >= b ? 0 : 1; }

static u64 p_and(u64 a, u64 b) { return a & b; }
static u64 p_or(u64 a, u64 b)  { return a | b; }
static u64 p_xor(u64 a, u64 b) { return a ^ b; }

/* A shift amount is a bit position, and a 63-bit atom has positions 0 to 62.
 * The guard is also what keeps the checks below out of undefined behaviour: C
 * leaves a shift of 64 or more undefined, so an unbounded amount would be a
 * crash of a different kind.  Note that this is about the amount and not the
 * result -- 1 +rsh 63 is 0, which is in range, and is still refused.  A domain
 * restriction, not an overflow check; +lsh's result check is the overflow one. */
static u64 p_lsh(u64 a, u64 b)
{
    if (b > 62) {
        machine_crash("+lsh: a shift of 63 or more is not a position in a "
                      "63-bit atom");
        return 0;
    }
    if (a > (NOUN_ATOM_MAX >> b)) {
        machine_crash("+lsh: result does not fit in an atom");
        return 0;
    }
    return a << b;
}

static u64 p_rsh(u64 a, u64 b)
{
    if (b > 62) {
        machine_crash("+rsh: a shift of 63 or more is not a position in a "
                      "63-bit atom");
        return 0;
    }
    return a >> b;
}

static u64 p_inc(u64 a, u64 b)
{
    if (a == NOUN_ATOM_MAX) {
        machine_crash("+inc: 2^63 - 1 plus one does not fit in an atom");
        return 0;
    }
    return a + 1;
}

static u64 p_dec(u64 a, u64 b)
{
    if (a == 0) {
        machine_crash("+dec: zero has no predecessor among natural numbers");
        return 0;
    }
    return a - 1;
}

/* The complement within the atom range, not within 64 bits. */
static u64 p_not(u64 a, u64 b) { return a ^ NOUN_ATOM_MAX; }

static const prim_entry bank[] = {
    { "+add", p_add, A2, 1 },
    { "+sub", p_sub, A2, 1 },
    { "+mul", p_mul, A2, 1 },
    { "+div", p_div, A2, 1 },
    { "+mod", p_mod, A2, 1 },
    { "+min", p_min, A2, 1 },
    { "+max", p_max, A2, 1 },
    { "+eq",  p_eq,  A2, 1 },
    { "+lt",  p_lt,  A2, 1 },
    { "+le",  p_le,  A2, 1 },
    { "+gt",  p_gt,  A2, 1 },
    { "+ge",  p_ge,  A2, 1 },
    { "+and", p_and, A2, 1 },
    { "+or",  p_or,  A2, 1 },
    { "+xor", p_xor, A2, 1 },
    { "+lsh", p_lsh, A2, 1 },
    { "+rsh", p_rsh, A2, 1 },
    { "+inc", p_inc, 1,  1 },
    { "+dec", p_dec, 1,  1 },
    { "+not", p_not, 1,  1 },
};

int prim_count(void)
{
    return (int)(sizeof(bank) / sizeof(bank[0]));
}

const prim_entry *prim_get(int index)
{
    if (index < 0 || index >= prim_count())
        return NULL;
    return &bank[index];
}

const prim_entry *prim_find(const char *name)
{
    for (int i = 0; i < prim_count(); i++) {
        const char *a = bank[i].name;
        const char *b = name;
        while (*a != '\0' && *a == *b) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0')
            return &bank[i];
    }
    return NULL;
}

int prim_index(const char *name)
{
    for (int i = 0; i < prim_count(); i++) {
        const char *a = bank[i].name;
        const char *b = name;
        while (*a != '\0' && *a == *b) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0')
            return i;
    }
    return -1;
}

u64 prim_call(int index, u64 a, u64 b)
{
    const prim_entry *entry = prim_get(index);
    if (entry == NULL) {
        machine_crash("no such primitive");
        return 0;
    }
    if (entry->arity == 1)
        b = 0;
    return entry->fn(a, b);
}
