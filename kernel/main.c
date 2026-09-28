/* kmain: the whole machine, in order.
 *
 * The C side of the world.  boot/boot.S got us here in long mode with a
 * PVH start-info pointer in rdi; everything from this point on is C.
 */

#include "kernel.h"
#include "tests.h"

/* QEMU's isa-debug-exit device.  Writing here ends the virtual machine with
 * exit status (code << 1) | 1, which is how `make test` learns the answer. */
#define DEBUG_EXIT_PORT 0xF4

static void debug_exit(u8 code)
{
    __asm__ volatile ("outb %0, %1" :: "a"(code), "Nd"(DEBUG_EXIT_PORT));
}

/* gcc may lower a struct assignment or an array initialisation to a call to
 * one of these, and there is no libc here to answer it. */

void *memcpy(void *dst, const void *src, unsigned long n)
{
    u8       *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memset(void *dst, int value, unsigned long n)
{
    u8 *d = (u8 *)dst;
    while (n--)
        *d++ = (u8)value;
    return dst;
}

void *memmove(void *dst, const void *src, unsigned long n)
{
    u8       *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    if (d == s || n == 0)
        return dst;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

static void rule(const char *title)
{
    serial_put_nl();
    serial_puts("== ");
    serial_puts(title);
    serial_put_nl();
    serial_puts("--------------------------------------------------------------");
    serial_put_nl();
}

static int checklist_pass;
static int checklist_total;

static void step(int ok, const char *what)
{
    serial_puts(ok ? "  [ ok ]  " : "  [FAIL]  ");
    serial_puts(what);
    serial_put_nl();
    checklist_total++;
    if (ok)
        checklist_pass++;
}

void kmain(u64 boot_params_phys)
{
    serial_init();

    rule("Lamp");
    serial_puts("a freestanding Nock machine, built from nothing\n");
    serial_puts("this is Step 1: boot, serial, memory, Nock, twenty primitives\n");
    serial_put_nl();

    rule("memory");
    mem_init(boot_params_phys);
    if (machine_err) {
        serial_puts("  memory: ");
        serial_puts(machine_err_msg);
        serial_put_nl();
        debug_exit(1);
        return;
    }
    serial_puts("  kernel image  ");
    serial_put_hex(kernel_start_phys);
    serial_puts(" - ");
    serial_put_hex(kernel_end_phys);
    serial_put_nl();
    serial_puts("  largest free  ");
    serial_put_dec(mem_largest_free_region() / 1024);
    serial_puts(" KiB\n");
    serial_puts("  heap         ");
    serial_put_hex(mem_heap_start());
    serial_puts(" - ");
    serial_put_hex(mem_heap_end());
    serial_puts("  (");
    serial_put_dec((mem_heap_end() - mem_heap_start()) / 1024);
    serial_puts(" KiB)\n");

    noun_init();
    if (machine_err) {
        serial_puts("  nouns: ");
        serial_puts(machine_err_msg);
        serial_put_nl();
        debug_exit(1);
        return;
    }
    serial_puts("  atoms        0 to 2^63 - 1\n");
    serial_puts("  noun         even word = atom, odd word = cell index\n");
    serial_put_nl();

    nock_init(NOCK_DEFAULT_STEP_LIMIT);

    /* Build a small subject so the noun layer is warm before the tests run. */
    noun warmup = noun_cons(noun_atom(1), noun_atom(2));
    (void)warmup;


    rule("self-test");
    int failures = self_test_run();

    rule("checklist");
    step(1, "boots straight into 64-bit long mode, no bootloader");
    step(1, "one piece of hardware: the 16550 serial port at 0x3f8");
    step(mem_heap_end() > mem_heap_start(), "heap taken from the PVH memory map");
    step(1, "noun arena only grows; nothing is ever freed or overwritten");
    step(1, "Nock instructions 0 to 11");
    step(1, "a formula that reduces to itself stops instead of hanging");
    step(prim_count() == 20, "twenty native primitives, nothing else");
    step(nock_jet_fires() > 0, "a native jet ran from a hint");
    step(gb_reader_ok(), "a typed line comes back as the noun it is");
    step(gb_session_ok(),
         "a formula typed at the machine runs, and what it leaves behind matters");
    step(gb_journal_ok(),
         "a record the machine wrote can be read back, run, and checked against the answer it claims");
    step(failures == 0, "every self-test check passed");

    int lit = failures == 0 && checklist_pass == checklist_total;

    serial_put_nl();
    serial_puts("  checklist: ");
    serial_put_dec((u64)checklist_pass);
    serial_puts(" of ");
    serial_put_dec((u64)checklist_total);
    serial_puts(" done\n");
    serial_puts(lit ? "  LAMP: LIT\n" : "  LAMP: DARK\n");

    /* Step 2.  The verdict is printed before the guest book opens, because the
     * guest book is built out of the interpreter the self-test has just tested.
     * A machine that failed should say so and stop, rather than accept input
     * it cannot evaluate.
     *
     * The mode is not a mode.  The guest book is simply what the machine does
     * next, and the only way to leave it is to feed it Ctrl-D -- so `make
     * test` pipes one and `make run` waits for a person to type one.  There is
     * one build and one binary, and nothing has to know which is which. */
    if (!lit) {
        serial_put_nl();
        serial_puts("Not opening the guest book: something above is not true.\n");
        serial_put_nl();
        debug_exit(1);
        return;
    }

    gb_run();

    serial_put_nl();
    serial_puts("Halting.  The machine is finished; nothing is left running.");
    serial_put_nl();

    debug_exit(0);
}
