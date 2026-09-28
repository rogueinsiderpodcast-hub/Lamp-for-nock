/* Lamp kernel: shared declarations.
 *
 * Freestanding C.  No libc, no host headers, no operating system.  The only
 * hardware this machine talks to is the 16550 serial UART, and the only
 * memory it uses is what QEMU's PVH memory map hands us.
 */

#ifndef KERNEL_H
#define KERNEL_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed long long   i64;

#define NULL ((void *)0)

#define KB (1024ULL)
#define MB (1024ULL * 1024ULL)

/* --- machine-wide error state ----------------------------------------- */
/* Any module may call machine_crash().  The crash is not fatal to the
 * machine: nock_run() notices, returns a non-zero status and the caller may
 * carry on.  Nothing is unwound, because nothing is ever unwound -- the noun
 * arena is append-only, so an aborted evaluation simply stops growing it. */

extern int         machine_err;      /* 0 healthy, 1 crashed, 2 out of steps */
extern const char *machine_err_msg;

void machine_crash(const char *msg);
void machine_crash_code(int code, const char *msg);
void machine_reset_error(void);

/* --- serial.c ---------------------------------------------------------- */

void serial_init(void);
void serial_putc(char c);
u8   serial_getc(void);
void serial_puts(const char *s);
void serial_put_dec(u64 v);
void serial_put_hex(u64 v);
void serial_put_nl(void);

/* --- memory.c ---------------------------------------------------------- */

extern u64 kernel_start_phys;
extern u64 kernel_end_phys;

void  mem_init(u64 boot_params_phys);
void *mem_alloc(u64 size, u64 align);
u64   mem_heap_start(void);
u64   mem_heap_end(void);
u64   mem_heap_used(void);
u64   mem_largest_free_region(void);
u64   mem_map_unreadable(void);

/* --- noun.c ------------------------------------------------------------ */
/* A noun is one 64-bit word.
 *
 *   bit 0 == 0  ->  atom,  value = noun >> 1        (63-bit natural number)
 *   bit 0 == 1  ->  cell,  index = noun >> 1         (index into the arena)
 *
 * The arena is append-only.  Consing never overwrites and never frees, which
 * makes the whole machine state a single growing list of nouns: a log.
 * Two structurally identical nouns may have different indices, so equality
 * is structural rather than a pointer comparison. */

typedef u64 noun;

#define NOUN_ATOM_MAX 0x7FFFFFFFFFFFFFFFULL

#define noun_is_atom(n)  (((n) & 1) == 0)
#define noun_is_cell(n)  (((n) & 1) == 1)
#define noun_atom(v)     (((u64)(v)) << 1)
#define noun_atom_val(n) ((n) >> 1)

void  noun_init(void);
noun  noun_cons(noun head, noun tail);
noun  noun_head(noun n);
noun  noun_tail(noun n);
int   noun_equal(noun a, noun b);
noun  noun_slot(noun n, u64 axis);
noun  noun_edit(noun n, u64 axis, u64 value);
u64   noun_cell_count(void);
void  noun_print(noun n);

/* --- nock.c ------------------------------------------------------------ */

#define NOCK_OK        0
#define NOCK_CRASH     1
#define NOCK_STEPS_OUT 2

#define NOCK_DEFAULT_STEP_LIMIT 10000000ULL
#define NOCK_MAX_DEPTH          10000u

void        nock_init(u64 step_limit);
int         nock_run(noun subject, noun formula, noun *out);
/* Run a formula on a subject inside a run that has already started, sharing its
 * step and depth budgets and its step tally.  A jet that answers by running
 * the interpreter -- a Step 5 rule -- must cost its caller real steps, or a
 * definition could hide its work from the budget the enclosing formula paid
 * for.  See decisions.md item 26. */
noun        nock_apply(noun subject, noun formula);
const char *nock_crash_reason(void);
u64         nock_steps_used(void);
u64         nock_jet_fires(void);
u64         nock_jet_declines(void);
void        nock_jet_hooks(int enable);
void        nock_jet_last(u64 *index, u64 *a, u64 *b, u64 *result);
u64         nock_opcode(noun formula);   /* 0..11, or NOCK_NO_OPCODE */
#define NOCK_NO_OPCODE 0xFFFFFFFFFFFFFFFFULL

/* --- book.c -------------------------------------------------------------
 *
 * The session, and the one formula that carries it from a line to the next.
 * Everything the guest book knows is in these nouns; the C is only I/O.  See
 * the file for the derivation the code is a transcription of. */
noun gb_book(void);
noun gb_empty_session(void);
noun gb_log(noun s);
noun gb_last(noun s);
noun gb_count(noun s);
noun gb_entry_line(noun entry);
noun gb_entry_answer(noun entry);
noun gb_log_front(noun log);
noun gb_log_rest(noun log);
int  gb_step(noun line, noun session, noun *out);

/* --- guestbook.c -------------------------------------------------------- */
/* The guest book: the first thing on the machine that answers.
 *
 * gb_parse() is the reader, and it is the only part of the guest book with a
 * grammar rather than a job.  It turns typed ASCII into a noun, or says why it
 * could not.  GB_PARSE_EMPTY is not an error: a line with nothing on it is a
 * request to be shown the session, not a mistake. */

/* The reader's limits, which are part of its contract: a line longer than this
 * is refused by name rather than walked off the end of an array, and so is a
 * line with too many open brackets or too many items.  They are here rather than
 * in guestbook.c because the host compiler has to know the transport budget it
 * compiles into, and because a test that hard-codes 32 and 40 is a test that
 * rots the moment a limit moves.  See docs/decisions.md item 21 for why they are
 * this size, which was a person's typing and is now a compiler's. */
#define GB_LINE_MAX         4096
#define GB_PARSE_MAX_DEPTH  256
#define GB_PARSE_MAX_ITEMS  4096

#define GB_PARSE_OK     0
#define GB_PARSE_EMPTY  1
#define GB_PARSE_ERROR  2

int  gb_parse(const char *text, u64 len, noun *out, const char **why);
int  gb_reader_ok(void);
int  gb_session_ok(void);
/* A record the machine printed can be read back, run, and checked against the
 * answer it claims (Step 4; decisions.md item 25). */
int  gb_journal_ok(void);
u64  noun_capacity(void);
void gb_run(void);

/* --- primitives.c ------------------------------------------------------ */
/* The native primitive bank: a fixed, enumerable list of integer operations.
 * Nothing else in the machine computes with nouns directly, so this list is
 * the entire native trust base.
 *
 * Step 1 jets are dispatched from a dynamic hint (opcode 11).  Our own
 * convention, documented in docs/decisions.md:
 *
 *     [11 [<atom: primitive index> <formula: argument>] <real formula>]
 *
 * The argument is an atom, or a pair of two atoms: the one operand, or the two
 * of them.  The interpreter hands them to the primitive and throws the result
 * away; the hint's answer is the real formula.  Hints are semantically
 * transparent, which is exactly what the tests check.  An argument of any other
 * shape is not ours, and is not jetted at all -- see jet_maybe() in nock.c for
 * why that has to be so, and for what happens when the native stops.
 *
 * Every primitive announces itself on the serial line so that a jet firing is
 * visible.  Real Urbit jets are silent; this is a Step 1 debugging choice. */

#define PRIM_MAX_ARGS 2

typedef struct {
    const char *name;
    u64 (*fn)(u64 a, u64 b);
    u8  arity;
    u8  verbose;
} prim_entry;

int             prim_count(void);
const prim_entry *prim_get(int index);
const prim_entry *prim_find(const char *name);
int             prim_index(const char *name);
u64             prim_call(int index, u64 a, u64 b);

/* Step 5: a primitive may sometimes be answered by a Nock definition the
 * machine checked exhaustively over a bounded domain, instead of by the C
 * native.  The domain lives here and is the machine's own contract: a rule is
 * only accepted for a primitive the machine knows how to bound, and is only
 * used where the bound says the check ran.  decisions.md item 26. */
int  prim_rule_domain(int index, u64 *limit); /* 1 if a bounded domain exists */
int  prim_rule_state(int index, noun *def);   /* 1 if a rule is installed */
void prim_rule_set(int index, noun def);      /* installed after the battery */
void prim_rule_gate(int index);               /* battery mode: index is being checked */
void prim_rule_ungate(void);
int  prim_rule_probe(int index, u64 a, u64 b, u64 *result);
u64  prim_rule_runs(int index);               /* probes a rule's definition answered */
u64  prim_native_runs(int index);             /* probes the C native answered */

/* The one certified domain there is (decisions.md item 26): the machine's +add
 * is a definition on the triangle a + b < RULE_ADD_LIMIT, chosen as the largest
 * whose whole battery still settles in the guest's arena. */
#define RULE_ADD_LIMIT 64u

/* --- main.c ------------------------------------------------------------ */

void kmain(u64 boot_params_phys);

#endif /* KERNEL_H */
