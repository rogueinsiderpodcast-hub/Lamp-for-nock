#ifndef TESTS_H
#define TESTS_H

/* The self-test: every suite, run at boot before anything else is trusted.
 *
 * Both suites share one set of counters, so the number reported at the end is
 * the total across the machine rather than a total per file.  Returns the number
 * of failures, which is what decides LAMP: LIT or LAMP: DARK. */
int self_test_run(void);

/* The Nock suite: the noun layer, the interpreter, the primitives. */
int nock_tests_run(void);

/* The guest book suite: the reader, and what the session is made of. */
int guestbook_tests_run(void);

#endif /* TESTS_H */
