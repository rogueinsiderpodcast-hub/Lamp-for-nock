/* Nouns: atoms, cells, and the three tree operators Nock is defined in terms
 * of (slot, edit, equality).
 *
 * Representation, in full:
 *
 *     noun  = <1: cell flag> <63: payload>
 *
 *   cell flag 0  ->  atom, payload is the natural number
 *   cell flag 1  ->  cell, payload is an index into `arena`
 *
 * The arena is a flat array of { head, tail } pairs that only ever grows.
 * Consing appends.  Nothing is ever overwritten, nothing is ever freed, and
 * no noun ever changes value.  That is the whole point: the state of the
 * machine is a list, and the history of the machine is the list of lists it
 * has already built.
 *
 * Atoms are 63 bits.  A cell index is 63 bits, which is far more cells than
 * 256 MiB of RAM can hold, so the index is never actually the limit.
 */

#include "kernel.h"

struct noun_cell {
    noun head;
    noun tail;
};

static struct noun_cell *arena;
static u64               arena_count;
static u64               arena_cap;

void noun_init(void)
{
    u64 usable = mem_heap_end() - mem_heap_start();

    /* Give the arena half the heap and leave the rest spare.  Step 1 has no
     * reason to want more; if this ever gets tight it is a real ceiling and
     * the machine should crash rather than quietly reuse memory. */
    arena_cap  = (usable / 2) / sizeof(struct noun_cell);
    arena      = (struct noun_cell *)mem_alloc(arena_cap * sizeof(struct noun_cell), 16);
    arena_count = 0;

    if (arena == NULL)
        return;
}

u64 noun_cell_count(void)
{
    return arena_count;
}

static int cell_index_ok(noun n)
{
    u64 index = n >> 1;
    if (index >= arena_count) {
        machine_crash("cell index is outside the noun arena");
        return 0;
    }
    return 1;
}

noun noun_cons(noun head, noun tail)
{
    if (machine_err)
        return 0;
    if (arena_count == arena_cap) {
        machine_crash("noun arena exhausted");
        return 0;
    }
    arena[arena_count].head = head;
    arena[arena_count].tail = tail;
    /* The noun is the index of the cell just written, so it has to be formed
     * before the count moves on.  Forming it afterwards would name the cell
     * after this one. */
    noun cell = (arena_count << 1) | 1;
    arena_count++;
    return cell;
}

noun noun_head(noun n)
{
    if (!noun_is_cell(n))
        return 0;
    if (!cell_index_ok(n))
        return 0;
    return arena[n >> 1].head;
}

noun noun_tail(noun n)
{
    if (!noun_is_cell(n))
        return 0;
    if (!cell_index_ok(n))
        return 0;
    return arena[n >> 1].tail;
}

int noun_equal(noun a, noun b)
{
    /* Identical words are identical nouns.  This also makes shared structure
     * cheap, which matters because the interpreter shares a lot. */
    if (a == b)
        return 1;
    if (noun_is_atom(a) != noun_is_atom(b))
        return 0;
    if (noun_is_atom(a))
        return noun_atom_val(a) == noun_atom_val(b);

    /* Different cells, so compare structurally.  Two structurally equal
     * nouns can have different arena indices; that is expected and it is why
     * this is not a pointer comparison. */
    noun ah = noun_head(a), bh = noun_head(b);
    if (machine_err)
        return 0;
    if (!noun_equal(ah, bh))
        return 0;
    return noun_equal(noun_tail(a), noun_tail(b));
}

/* A tree address is a path from the root written as a binary number: the
 * leading 1 is the root, and each bit after it is one step down, 0 for the
 * head and 1 for the tail.  So 6 is 1-1-0, the head of the tail.
 *
 * That means the bits have to be read from the most significant end down.
 * Peeling the least significant bit first and descending as it goes walks the
 * same steps in the opposite order, which for a deep address names a different
 * part of the noun entirely. */
static int axis_depth(u64 axis)
{
    int depth = 0;
    while (axis > 1) {
        depth++;
        axis >>= 1;
    }
    return depth;
}

static u64 axis_path(u64 axis)
{
    return axis & ((1ULL << axis_depth(axis)) - 1);
}

/* /[axis noun] -- the subtree at a tree address.
 *
 * The root is 1, the left child of n is 2n, the right child is 2n+1.  Address
 * 0 does not name anything and asking for it is a crash, as is asking for a
 * child of an atom. */
noun noun_slot(noun n, u64 axis)
{
    if (machine_err)
        return 0;
    if (axis == 0) {
        machine_crash("tree address 0 does not name a noun");
        return 0;
    }
    if (axis == 1)
        return n;
    if (!noun_is_cell(n)) {
        machine_crash("tree address descends into an atom");
        return 0;
    }

    int depth = axis_depth(axis);
    u64 path  = axis_path(axis);
    u64 top   = 1ULL << (depth - 1);
    /* The address of the subtree one step down is the leading 1 followed by
     * whatever is left of the path. */
    u64 below = top | (path & (top - 1));
    noun step = (path & top) ? noun_tail(n) : noun_head(n);

    return noun_slot(step, below);
}

/* #[axis value noun] -- the noun at a tree address, with that one part
 * replaced.
 *
 * Nouns are immutable, so this builds a new spine instead of writing through
 * the old one.  The old noun keeps its value forever, which is what makes the
 * arena append-only in practice and not just by convention. */
noun noun_edit(noun n, u64 axis, u64 value)
{
    if (machine_err)
        return 0;
    if (axis == 0) {
        machine_crash("tree address 0 does not name a noun");
        return 0;
    }
    if (axis == 1)
        return value;
    if (!noun_is_cell(n)) {
        machine_crash("tree address descends into an atom");
        return 0;
    }
    int depth = axis_depth(axis);
    u64 path  = axis_path(axis);
    u64 top   = 1ULL << (depth - 1);
    /* The address of the subtree one step down is the leading 1 followed by
     * whatever is left of the path.  Stripping the leading 1 would leave a
     * bare path, which is not a tree address at all. */
    u64 below = top | (path & (top - 1));

    if (path & top) {
        noun new_tail = noun_edit(noun_tail(n), below, value);
        if (machine_err)
            return 0;
        return noun_cons(noun_head(n), new_tail);
    }
    noun new_head = noun_edit(noun_head(n), below, value);
    if (machine_err)
        return 0;
    return noun_cons(new_head, noun_tail(n));
}

static void print_noun(noun n)
{
    if (machine_err)
        return;
    if (noun_is_atom(n)) {
        serial_put_dec(noun_atom_val(n));
        return;
    }
    serial_putc('[');
    print_noun(noun_head(n));
    serial_putc(' ');
    print_noun(noun_tail(n));
    serial_putc(']');
}

void noun_print(noun n)
{
    print_noun(n);
}
