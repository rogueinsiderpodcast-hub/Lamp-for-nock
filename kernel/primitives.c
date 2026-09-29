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
/* Logical not, which is what Urbit's !. does: 1 for zero, 0 for anything else.
 * This was a ^ NOUN_ATOM_MAX, a 63-bit complement, wearing the name of a
 * logical not: a name is the contract, and a primitive called +not that flips
 * every bit is not the function its name promises.  A program written against
 * Urbit would have got a number back that was not the answer to anything. */
static u64 p_not(u64 a, u64 b) { return a == 0 ? 1 : 0; }

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

/* --- rules (Step 5) ------------------------------------------------------
 * A rule is a Nock formula, received as text, that claims to be a primitive.
 * The machine never believes one: it is checked exhaustively over a bounded
 * domain -- every pair in the domain, run by the machine's own interpreter
 * and compared with the native by noun_equal -- before it may be used, and it
 * is only used inside the bound the check covered.  See decisions.md item 26.
 *
 * The domain is a shape and a number, and the shape is settled here because it
 * is the machine's contract rather than the text's: a rule is only accepted for
 * a primitive the machine knows how to bound, and a primitive with no domain can
 * be sent all the rules it likes and every one is refused without being read.
 * +add's limit is the largest triangle whose whole battery still settles in the
 * guest's arena -- item 23's definition costs 275 + 21*(a + b) cells a case, the
 * triangle has N*(N+1)/2 cases, and N = 64 settles at about 2.4 million cells.
 * +mul's is the largest product region whose battery does too: item 28's
 * definition is checked over 892 pairs and settles at 1.4 million cells, so the
 * two batteries together measure 3.35 million of the guest's 8.32 million.  Both
 * are far over the host's 131 thousand -- so these are checks the machine can
 * perform and the host cannot. */

struct rule_slot {
    noun def;    /* the checked definition, or 0 when none is installed */
    u64  limit;  /* the certified domain, or 0 when there is no domain */
    rule_shape shape; /* what the limit bounds */
};

/* One slot per bank entry; the designator gives +add (index 0) a sum domain and
 * +mul (index 2) a product one, and every other primitive no domain at all,
 * which means "no checkable domain". */
static struct rule_slot rule_slots[sizeof(bank) / sizeof(bank[0])] = {
    [0] = { 0, RULE_ADD_LIMIT, RULE_SHAPE_SUM },
    [2] = { 0, RULE_MUL_LIMIT, RULE_SHAPE_PRODUCT },
};

static u64 rule_runs[sizeof(bank) / sizeof(bank[0])];
static u64 native_runs[sizeof(bank) / sizeof(bank[0])];

#define RULE_LEN (sizeof(rule_slots) / sizeof(rule_slots[0]))

/* The primitive whose rule is currently answering, or -1.  While it is set, a
 * probe at that primitive declines without touching the native -- a definition
 * of a thing may not answer by calling the thing, whether or not it is
 * installed yet -- and a declined probe is ordinary in this machine, so the
 * definition's own fallback formula runs and the battery judges that. */
static int rule_active = -1;

int prim_rule_domain(int index, u64 *limit)
{
    if (index < 0 || index >= (int)RULE_LEN)
        return 0;
    if (rule_slots[index].limit == 0)
        return 0;
    if (limit != NULL)
        *limit = rule_slots[index].limit;
    return 1;
}

rule_shape prim_rule_shape(int index)
{
    if (index < 0 || index >= (int)RULE_LEN)
        return RULE_SHAPE_NONE;
    return rule_slots[index].shape;
}

/* Is this pair inside the primitive's certified domain?  Written by division
 * and comparison rather than by forming the sum or the product, because these
 * are 64-bit atoms: a + b on two operands near 2^63 wraps to a small number and
 * a * b on two of them wraps to zero, and either would certify a pair as
 * checked that is nowhere near the domain the battery ran.  A rule answering
 * such a pair is not a wrong answer, it is an unchecked one, which is worse. */
int prim_rule_in_domain(int index, u64 a, u64 b)
{
    if (index < 0 || index >= (int)RULE_LEN)
        return 0;
    u64 limit = rule_slots[index].limit;
    if (limit == 0)
        return 0;
    if (a >= limit || b >= limit)
        return 0;                          /* the battery never looked past here */
    switch (rule_slots[index].shape) {
    case RULE_SHAPE_SUM:
        return a < limit - b;              /* a + b < limit, without the sum */
    case RULE_SHAPE_PRODUCT:
        if (a == 0 || b == 0)
            return 1;                      /* 0 is under any limit */
        return b <= (limit - 1) / a;       /* a * b < limit, without the product */
    default:
        return 0;
    }
}

int prim_rule_state(int index, noun *def)
{
    if (index < 0 || index >= (int)RULE_LEN)
        return 0;
    if (def != NULL)
        *def = rule_slots[index].def;
    return rule_slots[index].def != 0;
}

void prim_rule_set(int index, noun def)
{
    if (index < 0 || index >= (int)RULE_LEN)
        return;
    rule_slots[index].def = def;
}

void prim_rule_gate(int index) { rule_active = index; }
void prim_rule_ungate(void)    { rule_active = -1; }

u64 prim_rule_runs(int index)
{
    if (index < 0 || index >= (int)RULE_LEN)
        return 0;
    return rule_runs[index];
}

u64 prim_native_runs(int index)
{
    if (index < 0 || index >= (int)RULE_LEN)
        return 0;
    return native_runs[index];
}

/* The probe a step-1 hint is answered by.  Its shape is the native's: return
 * 0 with machine_err clear and *result set when it answers, return non-zero
 * with machine_err set when it declines.  Which path answers:

 *   - no rule installed, or the operands outside the domain: the C native;
 *   - a rule installed and the operands inside the domain: the rule's
 *     definition, run by the interpreter with the live step and depth budget,
 *     and declined (never the native) if it stops or answers a cell, or if it
 *     probes the primitive it defines. */
int prim_rule_probe(int index, u64 a, u64 b, u64 *result)
{
    if (index < 0 || index >= (int)RULE_LEN)
        return 1;
    if (rule_active == index) {
        machine_crash("a rule may not answer by calling the primitive it defines");
        return 1;
    }
    if (rule_slots[index].limit != 0 && rule_slots[index].def != 0
        && prim_rule_in_domain(index, a, b)) {
        rule_active = index;
        noun subject = noun_cons(noun_atom(a),
                                 noun_cons(noun_atom(b), noun_atom(0)));
        noun product = nock_apply(subject, rule_slots[index].def);
        rule_active = -1;
        if (machine_err || !noun_is_atom(product)) {
            if (!machine_err)
                machine_crash("a rule answered with a cell");
            return 1;
        }
        rule_runs[index]++;
        *result = noun_atom_val(product);
        return 0;
    }
    *result = prim_call(index, a, b);
    if (machine_err)
        return 1;
    native_runs[index]++;
    return 0;
}
