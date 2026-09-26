/* The Nock interpreter.
 *
 * Nock is a reduction system.  Given a subject and a formula, it reduces them
 * to a product.  A formula that reduces to itself is an infinite loop, which
 * the specification defines as a crash; this interpreter reports a crash
 * instead of hanging, and the machine carries on afterwards.
 *
 * The rules implemented here are the Nock 4K rules, which are the rules
 * printed in docs/decisions.md and in vere/doc/spec/nock/4.txt.  In words:
 *
 *     [0 b]           the noun at tree address b in the subject
 *     [1 b]           the constant b
 *     [2 b c]         nock(nock(subject, b), nock(subject, c))
 *     [3 b]           0 if nock(subject, b) is a cell, else 1
 *     [4 b]           the atom nock(subject, b) plus one
 *     [5 b c]         0 if the two products are the same noun, else 1
 *     [6 b c d]       if b then c else d  (0 is true, 1 is false)
 *     [7 b c]         nock(nock(subject, b), c)          -- c is not evaluated
 *     [8 b c]         nock([nock(subject, b) subject], nock(subject, c))
 *     [9 b c]         d = nock(subject, c); nock(d, slot(d, b))
 *     [10 [b c] d]    #[b nock(subject, c) nock(subject, d)]
 *     [11 b c]        nock(subject, c), with b as a discarded hint
 *
 * and the rule that covers every other shape: a formula whose head is a cell
 * is a pair of formulas, and reduces to the cell of their products.
 *
 * Which arguments are formulas and which are literals is not a detail, and
 * the specification is not uniform about it.  0, 1, 9, 10 and 11 take their
 * addresses, constants and hints literally; 2, 3, 4, 5, 6, 7, 8 take formulas.
 * Opcode 2 versus opcode 7 is the sharpest version of the same point: 2
 * evaluates its second argument (that is where the new formula comes from) and
 * 7 does not (it hands the second argument straight to the next evaluation).
 * The test suite pins every one of these down.
 *
 * Opcodes 6 and 9 are implemented directly from the above wording rather
 * than by transcribing the specification's expansion, because those
 * expansions use a compressed notation that does not bracket cleanly.  The
 * wording is the normative version.  See docs/decisions.md.
 */

#include "kernel.h"

static u64 step_limit;
static u64 steps_left;
static u64 steps_used;
static u64 call_depth;
static int  jet_hooks_enabled;
static u64 jet_fires;

int         machine_err;
const char *machine_err_msg = "";

void machine_reset_error(void)
{
    machine_err     = 0;
    machine_err_msg = "";
}

void machine_crash(const char *msg)
{
    if (machine_err == 0) {
        machine_err     = 1;
        machine_err_msg = msg;
    }
}

void machine_crash_code(int code, const char *msg)
{
    if (machine_err == 0) {
        machine_err     = code;
        machine_err_msg = msg;
    }
}

const char *nock_crash_reason(void)
{
    return machine_err_msg;
}

void nock_init(u64 limit)
{
    step_limit        = limit;
    jet_hooks_enabled = 1;
}

u64  nock_steps_used(void) { return steps_used; }
u64  nock_jet_fires(void)  { return jet_fires; }
void nock_jet_hooks(int enable) { jet_hooks_enabled = enable; }

u64 nock_opcode(noun formula)
{
    if (!noun_is_cell(formula))
        return NOCK_NO_OPCODE;
    noun head = noun_head(formula);
    if (!noun_is_atom(head))
        return NOCK_NO_OPCODE;
    u64 op = noun_atom_val(head);
    if (op > 11)
        return NOCK_NO_OPCODE;
    return op;
}

/* The i-th argument of a formula, 0-based, counted after the opcode.
 *
 * [2 b c] -> arg(f, 0) is the formula b, arg(f, 1) is the formula c.  A
 * formula that runs out of elements is a crash, not a wild read. */
static noun arg(noun f, int i)
{
    noun t = noun_tail(f);
    while (i > 0) {
        if (!noun_is_cell(t)) {
            machine_crash("formula is missing arguments");
            return 0;
        }
        t = noun_tail(t);
    }
    if (!noun_is_cell(t)) {
        machine_crash("formula is missing arguments");
        return 0;
    }
    return noun_head(t);
}

static noun nock(noun subject, noun formula);

/* A literal tree address taken straight out of a formula.  Used by opcodes 0
 * and 9, whose address arguments the specification does not evaluate. */
static noun slot_by_literal(noun subject, noun axis_noun)
{
    if (!noun_is_atom(axis_noun)) {
        machine_crash("a tree address must be an atom");
        return 0;
    }
    return noun_slot(subject, noun_atom_val(axis_noun));
}

/* Step 1 jet dispatch.  See kernel.h for the hint convention. */
static void jet_maybe(noun hint_head, noun hint_product)
{
    if (!jet_hooks_enabled || machine_err)
        return;
    if (!noun_is_atom(hint_head))
        return;

    u64 index = noun_atom_val(hint_head);
    if (index >= (u64)prim_count())
        return;

    const prim_entry *entry = prim_get((int)index);
    if (entry->fn == NULL)
        return;

    u64 a = 0, b = 0;
    if (noun_is_cell(hint_product)) {
        a = noun_atom_val(noun_head(hint_product));
        b = noun_atom_val(noun_tail(hint_product));
    } else {
        a = noun_atom_val(hint_product);
    }

    u64 result = prim_call((int)index, a, b);
    jet_fires++;

    if (entry->verbose) {
        serial_puts("      jet  ");
        serial_puts(entry->name);
        serial_putc('(');
        serial_put_dec(a);
        if (entry->arity == 2) {
            serial_puts(", ");
            serial_put_dec(b);
        }
        serial_puts(") = ");
        serial_put_dec(result);
        serial_put_nl();
    }
}

static noun nock(noun subject, noun formula)
{
    if (machine_err)
        return 0;

    if (steps_left == 0) {
        machine_crash_code(2, "step limit exceeded: the formula does not terminate");
        return 0;
    }
    steps_left--;
    steps_used++;

    if (call_depth >= NOCK_MAX_DEPTH) {
        machine_crash_code(2, "call depth exceeded");
        return 0;
    }
    call_depth++;

    noun result = 0;

    if (!noun_is_cell(formula)) {
        machine_crash("a formula must be a cell, but this is an atom");
        goto done;
    }

    noun head = noun_head(formula);

    /* The general rule: a formula whose head is a cell is a pair of formulas,
     * and reduces to the cell of their products.  [x y] is (cons x y) in
     * Lisp terms, so this is what makes Nock code look like data. */
    if (noun_is_cell(head)) {
        noun a = nock(subject, head);
        noun b = nock(subject, noun_tail(formula));
        result = noun_cons(a, b);
        goto done;
    }

    switch (noun_atom_val(head)) {
    case 0:  /* slot -- the address is a literal, not a formula */
        result = slot_by_literal(subject, arg(formula, 0));
        break;

    case 1:  /* constant -- the argument is data, not a formula */
        result = arg(formula, 0);
        break;

    case 2:  /* call: evaluate both, then call */
    {
        noun sub = nock(subject, arg(formula, 0));
        noun fml = nock(subject, arg(formula, 1));
        result = nock(sub, fml);
        break;
    }

    case 3:  /* cell test: 0 for a cell, 1 for an atom */
    {
        noun p = nock(subject, arg(formula, 0));
        if (machine_err)
            break;
        result = noun_atom(noun_is_cell(p) ? 0 : 1);
        break;
    }

    case 4:  /* increment */
    {
        noun p = nock(subject, arg(formula, 0));
        if (machine_err)
            break;
        if (!noun_is_atom(p)) {
            machine_crash("cannot increment a cell");
            break;
        }
        u64 v = noun_atom_val(p);
        if (v == NOUN_ATOM_MAX) {
            machine_crash("atom overflow: 2^63 - 1 plus one");
            break;
        }
        result = noun_atom(v + 1);
        break;
    }

    case 5:  /* equality: 0 for the same noun, 1 for different */
    {
        noun a = nock(subject, arg(formula, 0));
        noun b = nock(subject, arg(formula, 1));
        if (machine_err)
            break;
        result = noun_atom(noun_equal(a, b) ? 0 : 1);
        break;
    }

    case 6:  /* if / then / else; 0 is true, 1 is false */
    {
        noun test = nock(subject, arg(formula, 0));
        if (machine_err)
            break;
        if (!noun_is_atom(test)) {
            machine_crash("a conditional test must be an atom");
            break;
        }
        if (noun_atom_val(test) == 0)
            result = nock(subject, arg(formula, 1));
        else
            result = nock(subject, arg(formula, 2));
        break;
    }

    case 7:  /* compose: the second argument is NOT evaluated */
        result = nock(nock(subject, arg(formula, 0)), arg(formula, 1));
        break;

    case 8:  /* push: evaluate the first argument onto the front of the subject */
    {
        noun b = nock(subject, arg(formula, 0));
        noun fml = nock(subject, arg(formula, 1));
        result = nock(noun_cons(b, subject), fml);
        break;
    }

    case 9:  /* call an arm: evaluate to a core, extract a formula from it, call it */
    {
        noun axis = arg(formula, 0);
        noun core = nock(subject, arg(formula, 1));
        if (machine_err)
            break;
        if (!noun_is_atom(axis)) {
            machine_crash("an arm axis must be an atom");
            break;
        }
        noun arm = noun_slot(core, noun_atom_val(axis));
        result = nock(core, arm);
        break;
    }

    case 10: /* edit: the first argument is a literal [axis formula] pair */
    {
        noun pair = arg(formula, 0);
        if (!noun_is_cell(pair)) {
            machine_crash("the edit argument must be a [axis formula] cell");
            break;
        }
        noun axis = noun_head(pair);
        if (!noun_is_atom(axis)) {
            machine_crash("an edit axis must be an atom");
            break;
        }
        noun value = nock(subject, noun_tail(pair));
        noun target = nock(subject, arg(formula, 1));
        if (machine_err)
            break;
        result = noun_edit(target, noun_atom_val(axis), value);
        break;
    }

    case 11: /* hint: run it, throw it away */
    {
        noun hint = arg(formula, 0);
        if (noun_is_cell(hint)) {
            /* Dynamic hint.  The specification requires evaluating the tail:
             * a real interpreter may not skip it, because it might crash. */
            noun product = nock(subject, noun_tail(hint));
            jet_maybe(noun_head(hint), product);
        }
        /* An atom head means a static hint, which is simply discarded. */
        result = nock(subject, arg(formula, 1));
        break;
    }

    default:
        machine_crash("not a Nock instruction: the opcode must be 0 to 11");
        break;
    }

done:
    call_depth--;
    return result;
}

int nock_run(noun subject, noun formula, noun *out)
{
    machine_reset_error();
    steps_left = step_limit;
    steps_used = 0;
    call_depth = 0;

    noun product = nock(subject, formula);

    if (machine_err == 2)
        return NOCK_STEPS_OUT;
    if (machine_err)
        return NOCK_CRASH;
    if (out != NULL)
        *out = product;
    return NOCK_OK;
}
