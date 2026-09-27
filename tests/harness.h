#ifndef HARNESS_H
#define HARNESS_H

/* The test harness, shared by every suite.
 *
 * It exists so that a test is one line: a description, an expectation, and a
 * derivation of the expected value in a comment beside it.  Everything the
 * suites share -- counting, printing, and the three ways to expect something --
 * is here, so that a new suite does not have to reinvent the counting and
 * cannot accidentally disagree with it.
 *
 * The three expectations differ in what they claim, and the difference is the
 * point:
 *
 *   expect_noun    this noun is the answer, whatever its shape
 *   expect_atom    this number is the answer
 *   expect_code    this is how it stopped, and not any other way
 *
 * All three of them leave the machine clean afterwards, which is a property
 * rather than a convenience.  A crashed evaluation leaves machine_err set, and
 * noun_cons refuses to build a noun while it is, so a formula assembled after an
 * unreset crash comes out as the atom 0 and the next test fails for the wrong
 * reason.  That is not hypothetical: it was 29 of the 37 failures that had to be
 * cleared to finish Step 1. */

void group(const char *name);
void pass(const char *what);
void check(int condition, const char *what);

void expect_noun(const char *what, noun subject, noun formula, noun expected);
void expect_atom(const char *what, noun subject, noun formula, u64 expected);
void expect_code(const char *what, int code, noun subject, noun formula);

int  tests_run_count(void);
int  tests_failed_count(void);

/* The primitive expectations, which talk about numbers and crashes rather than
 * nouns.  They live here for the same reason the others do: the counters. */
void expect_prim(const char *name, u64 a, u64 b, u64 expected);
void expect_prim_crash(const char *name, u64 a, u64 b);

/* Checks that run no Nock at all, and so cannot use the three above. */
void expect_true(const char *what, int condition);
void expect_equal_u64(const char *what, u64 got, u64 expected);

#endif /* HARNESS_H */
