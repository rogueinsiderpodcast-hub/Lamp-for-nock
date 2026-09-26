/* Lamp kernel: shared declarations.
 *
 * Freestanding C.  No libc, no host headers, no operating system.  The only
 * hardware this machine talks to is the 16550 serial UART, and the only
 * memory it uses is what QEMU's multiboot memory map hands us.
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
void serial_puts(const char *s);
void serial_put_dec(u64 v);
void serial_put_hex(u64 v);
void serial_put_nl(void);

/* --- memory.c ---------------------------------------------------------- */

extern u64 kernel_start_phys;
extern u64 kernel_end_phys;

void  mem_init(u64 mb_info_phys);
void *mem_alloc(u64 size, u64 align);
u64   mem_heap_start(void);
u64   mem_heap_end(void);
u64   mem_heap_used(void);
u64   mem_largest_free_region(void);

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
const char *nock_crash_reason(void);
u64         nock_steps_used(void);
u64         nock_jet_fires(void);
void        nock_jet_hooks(int enable);
u64         nock_opcode(noun formula);   /* 0..11, or NOCK_NO_OPCODE */
#define NOCK_NO_OPCODE 0xFFFFFFFFFFFFFFFFULL

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
 * The interpreter evaluates <formula>, hands the result to the primitive and
 * throws the result away; the hint's answer is the real formula.  Hints are
 * semantically transparent, which is exactly what the tests check.
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

/* --- main.c ------------------------------------------------------------ */

void kmain(u64 mb_info_phys);

#endif /* KERNEL_H */
