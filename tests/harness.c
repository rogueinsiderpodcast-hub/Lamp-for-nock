/* The test harness.  See harness.h for what these are for.
 *
 * The shape of this file is deliberate: every check ends by leaving the machine
 * in a known state, whether it passed or failed.  A harness that can leave
 * itself dirty is a harness that eventually reports a failure that is about the
 * harness.
 */

#include "kernel.h"
#include "harness.h"
#include "tests.h"

static int tests_run;
static int tests_failed;

int tests_run_count(void)    { return tests_run; }
int tests_failed_count(void) { return tests_failed; }

void group(const char *name)
{
    serial_put_nl();
    serial_puts("  ");
    serial_puts(name);
    serial_put_nl();
}

void pass(const char *what)
{
    tests_run++;
    serial_puts("  pass  ");
    serial_puts(what);
    serial_put_nl();
}

void check(int condition, const char *what)
{
    if (condition) {
        pass(what);
        return;
    }
    tests_run++;
    tests_failed++;
    serial_puts("  FAIL  ");
    serial_puts(what);
    serial_puts("   <<<< FAILED");
    serial_put_nl();
}

void expect_true(const char *what, int condition)
{
    check(condition, what);
}

void expect_equal_u64(const char *what, u64 got, u64 expected)
{
    if (got == expected) {
        pass(what);
        return;
    }
    tests_run++;
    tests_failed++;
    serial_puts("  FAIL  ");
    serial_puts(what);
    serial_puts("   got ");
    serial_put_dec(got);
    serial_puts(" want ");
    serial_put_dec(expected);
    serial_puts("   <<<< FAILED");
    serial_put_nl();
}

void expect_noun(const char *what, noun subject, noun formula, noun expected)
{
    noun got = 0;
    int rc = nock_run(subject, formula, &got);

    tests_run++;
    if (rc == NOCK_OK && noun_equal(got, expected)) {
        serial_puts("  pass  ");
        serial_puts(what);
        serial_put_nl();
        return;
    }
    serial_puts("  FAIL  ");
    serial_puts(what);
    if (rc != NOCK_OK) {
        serial_puts("   crashed: ");
        serial_puts(nock_crash_reason());
        serial_puts("  [subject word ");
        serial_put_dec((u64)subject);
        serial_puts(" formula word ");
        serial_put_dec((u64)formula);
        serial_puts("]");
    } else {
        serial_puts("   got ");
        noun_print(got);
        serial_puts(" want ");
        noun_print(expected);
    }
    serial_puts("   <<<< FAILED");
    serial_put_nl();
    tests_failed++;
    /* Leave the machine clean.  A crashed evaluation leaves machine_err set,
     * and noun_cons refuses to build while it is, so a formula assembled after
     * this point would come out as the atom 0 and every later test would fail
     * for the wrong reason.  Resetting here is also the point: the machine is
     * supposed to carry on after a crash. */
    machine_reset_error();
}

void expect_atom(const char *what, noun subject, noun formula, u64 expected)
{
    expect_noun(what, subject, formula, noun_atom(expected));
}

/* The self-test's entry point, and the only place the totals are reported.
 *
 * Every suite is run from here, in the order a reader would meet them: the
 * machine's own foundations first, and the parts built on top of those after.
 * The guest book runs last and is not optional -- a machine that can count and
 * a machine that can read a line are both claims, and neither is worth more
 * than the other. */
int self_test_run(void)
{
    tests_run = 0;
    tests_failed = 0;

    serial_puts("Lamp kernel self-test");
    serial_put_nl();

    nock_tests_run();
    guestbook_tests_run();

    serial_put_nl();
    serial_puts("  ");
    serial_put_dec((u64)tests_run);
    serial_puts(" checks, ");
    serial_put_dec((u64)tests_failed);
    serial_puts(" failed");
    serial_put_nl();

    return tests_failed;
}

void expect_code(const char *what, int code, noun subject, noun formula)
{
    noun got = 0;
    int rc = nock_run(subject, formula, &got);

    tests_run++;
    if (rc == code) {
        serial_puts("  pass  ");
        serial_puts(what);
        serial_put_nl();
        /* A crash that was expected still leaves machine_err set, and
         * noun_cons will not build while it is.  Clear it so the next formula
         * is assembled from a clean machine. */
        machine_reset_error();
        return;
    }
    tests_failed++;
    serial_puts("  FAIL  ");
    serial_puts(what);
    if (rc == NOCK_OK)
        serial_puts("   it did not stop at all");
    else {
        serial_puts(rc == NOCK_CRASH ? "   wrong kind of crash: " : "   wrong kind of stop: ");
        serial_puts(nock_crash_reason());
    }
    serial_puts("   <<<< FAILED");
    serial_put_nl();
    machine_reset_error();
}

static u64 prim_try(const char *name, u64 a, u64 b, int *crashed)
{
    int index = prim_index(name);
    if (index < 0) {
        *crashed = 1;
        return 0;
    }
    machine_reset_error();
    u64 result = prim_call(index, a, b);
    *crashed = machine_err;
    return result;
}

void expect_prim(const char *name, u64 a, u64 b, u64 expected)
{
    int crashed = 0;
    u64 got = prim_try(name, a, b, &crashed);

    if (!crashed && got == expected) {
        pass(name);
        return;
    }
    tests_run++;
    tests_failed++;
    serial_puts("  FAIL  ");
    serial_puts(name);
    serial_puts("(");
    serial_put_dec(a);
    serial_puts(", ");
    serial_put_dec(b);
    serial_puts(") gave ");
    serial_put_dec(got);
    if (crashed) {
        serial_puts(" and crashed: ");
        serial_puts(nock_crash_reason());
    }
    serial_puts("   <<<< FAILED");
    serial_put_nl();
}

void expect_prim_crash(const char *name, u64 a, u64 b)
{
    int crashed = 0;
    prim_try(name, a, b, &crashed);
    check(crashed, name);
    /* prim_try resets before it runs, not after, so the crash this expected
     * would otherwise be the state the next noun is built in. */
    machine_reset_error();
}
