#ifndef LAGRANGE_HOST_TEST_EVENTS_H
#define LAGRANGE_HOST_TEST_EVENTS_H

/*
 * Fake Mac event/time bits used by mac/posix/posix_classic.c: the nanosleep
 * shim polls TickCount() (60 Hz) and yields between polls.  In this host fake,
 * TickCount() only advances when YieldToAnyThread() is called, i.e. time passes
 * as the sleep yields -- enough to drive nanosleep deterministically.
 * Declarations only; implementation in host/fake_threads.c.
 */

typedef unsigned int UInt32;

UInt32 TickCount(void);

/* Test hook: the current fake tick counter. */
unsigned long hostTickCount(void);

#endif /* LAGRANGE_HOST_TEST_EVENTS_H */
