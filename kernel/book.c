/* The guest book itself: what a session is, and the one formula that carries it
 * from one line to the next.
 *
 * Everything in this file is a noun, and nothing in it is a C function that
 * decides anything.  The C here is the transcription of a Nock derivation into
 * cons cells; the derivation is in the comment, and where the two differ the
 * comment is what this file means.  A person can read the rule below and then
 * read the code and see the same thing twice, which is the only reason a
 * hand-written formula is worth having at all.
 *
 * THE SESSION
 *
 *     session = [log last count]
 *
 * A log is a list of entries, newest first, and an entry is the line and its
 * answer:
 *
 *     entry = [line [answer 0]]
 *     log   = [entry log]        or 0, when nothing has been run yet
 *
 * Newest first because that makes an append one cons onto the front: nothing
 * already written is touched, so the history of a session is still exactly
 * where it was, which is the same reason the arena only grows.  The list
 * terminates in 0, so an empty log is the atom 0 and the walk ends there.
 *
 * `last` is the answer to the most recent line.  It is kept separately from the
 * log because a formula should be able to ask what the last answer was without
 * walking the log, and because the log's own shape is about remembering, not
 * about evaluating.
 *
 * `count` is the number of entries.  It is a noun rather than a C variable so
 * that "how long have we been going" is a question a formula can ask of the
 * machine rather than a fact only the host can know.
 *
 * ADDRESSES
 *
 * Addresses are arithmetic, and over a right-nested list it punishes a
 * plausible guess.  A list [a b] is not two leaves but three: C(a, C(b, 0)),
 * because the tail of a list is another list and the last one ends in the atom
 * 0.  Reading an address is reading its bits from the right: an even axis takes
 * the head and halves, an odd axis takes the tail and halves.  So each part of a
 * list is at twice the last plus two --
 *
 *     [a b]      /2  /6
 *     [a b c]    /2  /6  /14
 *     [a b c d]  /2  /6  /14  /30
 *
 * -- and so, within a session [log last count]:
 *
 *     /[2]  the log        /[6]  the last answer      /[14] the count
 *
 * The book's subject is [line session], so the line is at /[2] and the session
 * at /[3].  A part *of* the session is then reached by putting one tail-step in
 * front of the session's own path, and the old path is what gets pushed along
 * rather than the number:
 *
 *     /[2] in the session  ->  /[6]  in the subject
 *     /[6] in the session  ->  /[14]
 *     /[14] in the session ->  /[30]
 *
 * So, in the subject:
 *
 *     /[2]  the line       /[3]  the session
 *     /[6]  the log        /[14] the last answer      /[30] the count
 *
 * /[5], /[13] and /[29] are the tempting wrong answers here: they are the same
 * steps in the other order, one that goes to the head before the tail.  So are
 * doubling and trebling a session address, which is arithmetic that looks like
 * it should work and does not.  Getting an address wrong gives a different noun
 * rather than an error, which is why the accessors below are functions and not
 * constants, and why the tests pin every one of these numbers down.
 *
 *
 * THE STEP
 *
 * The book is one formula.  Its subject is [line session] and its answer is the
 * new session.  In the shorthand this machine's tests use -- where [1 n] is the
 * constant n, [0 k] is the subject at address k, and [10 [b c] d] puts c's
 * value at address b of d's value -- it reads:
 *
 *     answer     =  *[[0 3] [0 2]]                     the line, on the session
 *     entry      =  #[2 [0 2] #[6 answer [0 0 0]]]     [line [answer 0]]
 *     newlog     =  #[2 entry #[6 [0 6] [0 0 0]]]      [entry [oldlog 0]]
 *     newcount   =  [4 [0 30]]                         the count, plus one
 *     new        =  #[2 newlog #[6 answer #[14 newcount [0 0 0 0]]]]
 *
 * The subject a formula is run on is the whole previous session, so a line can
 * read the log without going through C: [0 14 0] answers how many lines there
 * have been, [0 6 0] the last answer, [0 8 0] the line most recently typed and
 * [0 18 0] that line's answer.  That is the session being useful rather than
 * merely remembered.
 *
 * Nock cannot build a noun, only edit one -- there is no cons in the rules --
 * so every noun here is a canned all-atoms template with its parts replaced.
 * Three zeros are a two-element list and four are a three-element one, and the
 * parts come out of the deepest slot first.
 *
 * The one trap worth naming: an edit at axis 1 is not a cons, it is a
 * replacement of everything.  #[1 x T] is x, whatever T was, because axis 1 is
 * the whole noun.  The first version of this derivation wrote every edit at axis
 * 1 as if it were a cons, and every one of them quietly returned its own value,
 * so the book answered with the line it had been given.  Consing onto a noun is
 * done by editing axes 2 and 6 of a template, like everything else.
 *
 * WHY NOT C
 *
 * The obvious version of this file is: cons the entry onto the log in C, add one
 * to the count in C, and call the interpreter only for the evaluation.  It is
 * half the code and it is the wrong shape, because a session is then a noun the
 * interpreter cannot see, so a formula could not read it and the guest book
 * would be a C program with a Nock evaluator bolted on.  Everything the session
 * knows here is in the noun, and the whole of the step is one formula.
 */

#include "kernel.h"

/* --- the derivation, transcribed ---------------------------------------- */
/* The same three lines the test suite uses to write a formula, because these
 * are a formula.  Nothing here is special: a formula is a noun, and a noun is
 * made of two halves. */

static noun A(u64 v)                     { return noun_atom(v); }
static noun C(noun a, noun b)            { return noun_cons(a, b); }
static noun f1(u64 op, noun a)         { return C(A(op), C(a, A(0))); }
static noun f2(u64 op, noun a, noun b) { return C(A(op), C(a, C(b, A(0)))); }

/* The three ways to say something as a formula.
 *
 *   lit(n)      [1 n]          this literal noun, whatever the subject is
 *   at(k)       [0 k]          /[k subject]
 *   edit(b, c, d) [10 [b c] d]  put c's value at address b of d's value
 *
 * The last one is opcode 10, and it is the only way Nock can make a noun.  The
 * first is opcode 1, and it is how a literal becomes a formula: a noun used
 * where a formula is expected is wrapped, or it would be interpreted instead.
 *
 * The pair [b c] is a cell of exactly two things and not a two-element list, so
 * the C is C(A(b), c) and not f2(b, c, 0).  The interpreter takes the axis from
 * the head of the pair and the value formula from its tail, so a trailing zero
 * turns the tail into [c 0 0] -- a formula whose opcode is whatever c is, and a
 * crash further down that says nothing about what went wrong. */

static noun lit(noun n)                 { return f1(1, n); }
static noun at(u64 axis)                { return f1(0, A(axis)); }
static noun edit(u64 b, noun c, noun d) { return f2(10, C(A(b), c), d); }

/* Addresses within the subject [line session], as above. */
#define AX_LINE    2
#define AX_SESSION 3
#define AX_LOG     6
#define AX_LAST    14
#define AX_COUNT   30

/* Build the book.  Called once; see gb_book. */
static noun book_build(void)
{
    /* The templates every noun here is edited out of, because Nock has no cons
     * and only edits.  Three atoms is a two-element list and four is a
     * three-element one, since a list ends in 0 and that 0 is a leaf too. */
    noun two   = C(A(0), C(A(0), A(0)));
    noun three = C(A(0), C(A(0), C(A(0), A(0))));

    /* answer = *[[0 3] [0 2]] -- the line, run on the old session.  This is the
     * one part of the step that is the interpreter's work rather than data
     * arranging, and it is two cells: an opcode and two addresses. */
    noun answer = f2(2, at(AX_SESSION), at(AX_LINE));

    /* entry = #[2 [0 2] #[6 answer [0 0 0]]] -- [line [answer 0]]
     *
     * Axis 2 is the head and axis 6 is the tail's head, because the tail is a
     * list of one and so is a cell of its own. */
    noun entry  = edit(2, at(AX_LINE), edit(6, answer, lit(two)));

    /* newlog = #[2 entry #[6 [0 6] [0 0 0]]] -- [entry [oldlog 0]]
     *
     * The old log goes at axis 6 and the 0 stays where the template has it, so
     * an empty log -- the atom 0 -- becomes a list of one with no case for it,
     * and the walk below still ends at 0 rather than at a cell. */
    noun newlog = edit(2, entry, edit(6, at(AX_LOG), lit(two)));

    /* newcount = [4 [0 30]] -- the count at /[30], incremented.  Opcode 4 takes
     * one argument, so the formula is [4 argument 0] and there is nothing to
     * put in the second slot. */
    noun newcount = f1(4, at(AX_COUNT));

    /* new = #[2 newlog #[6 answer #[14 newcount [0 0 0 0]]]]
     *
     * Outermost first, so the edits read the other way round from the sentence
     * above: the count goes into a four-atom template, then the answer beside
     * it, then the log at the front.  Every part's subject is the same old
     * subject, which is why one run of one formula can do all of it -- every
     * address in it is an address into the session as it was, never into
     * anything half-built. */
    return edit(2, newlog, edit(6, answer, edit(14, newcount, lit(three))));
}

static noun book_formula;

noun gb_book(void)
{
    /* A formula is a cell, so it is never the atom 0, and an unbuilt book is
     * 0.  That is the whole test and there is no flag to go stale. */
    if (book_formula == 0)
        book_formula = book_build();
    return book_formula;
}

/* --- the session, named ------------------------------------------------- */
/* A session is [log last count], so these three are the only things anyone
 * outside this file needs to know about it.  They are functions rather than
 * constants because the addresses are arithmetic and easy to get wrong, and
 * /[8] for the count -- doubling along the list, 2 then 4 then 8 -- is the
 * mistake this file exists to prevent. */

/* Addresses within a session, which is not the same as the addresses within
 * the book's subject: the session sits at /[3] there. */
#define S_LOG   2
#define S_LAST  6
#define S_COUNT 14

/* [0 0 0] as a list, which is four zeros: the three fields and the 0 that ends
 * every list. */
noun gb_empty_session(void)  { return C(A(0), C(A(0), C(A(0), A(0)))); }
noun gb_log(noun s)           { return noun_slot(s, S_LOG); }
noun gb_last(noun s)          { return noun_slot(s, S_LAST); }
noun gb_count(noun s)         { return noun_slot(s, S_COUNT); }

/* One entry, and the two halves of it.  An entry is [line [answer 0]], so the
 * answer is at /6 and not /3: the tail of a list is a list, and so a cell. */
noun gb_entry_line(noun e)        { return noun_slot(e, 2); }
noun gb_entry_answer(noun e)      { return noun_slot(e, 6); }

/* The log is [entry [rest 0]], so these two walk it.  A log of 0 is empty and
 * the walk stops there; nothing else can be the end of one. */
noun gb_log_front(noun l)     { return noun_slot(l, 2); }
noun gb_log_rest(noun l)      { return noun_slot(l, 6); }

/* --- the step ----------------------------------------------------------- */
/* Run one line against one session and hand back the next session.  The only
 * thing this does that a person would not have written is the pair: the book
 * expects a subject of [line session], and the caller has a line and a session.
 *
 * A crash here leaves the caller's session exactly as it was, because the book
 * builds a new noun and never edits the one it was given.  The caller does not
 * have to undo anything, and cannot. */
int gb_step(noun line, noun session, noun *out)
{
    return nock_run(C(line, session), gb_book(), out);
}
