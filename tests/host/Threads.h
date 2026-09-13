#ifndef LAGRANGE_HOST_TEST_THREADS_H
#define LAGRANGE_HOST_TEST_THREADS_H

/*
 * Fake Mac OS Thread Manager for the host unit tests of mac/posix/pthread.c.
 *
 * The model is deliberately cooperative and minimal: NewThread() records a
 * thread but does not run it; the first YieldToAnyThread() runs the earliest
 * ready thread's entry to completion.  That is enough to exercise
 * create/join/self/once/condvar/TLS logic deterministically, because the real
 * layer's wait loops yield exactly once (or a bounded number of times) before
 * an event makes them stop.  Worker entries in the tests must not yield
 * themselves (that would recurse) -- none do.
 *
 * Declarations only; the implementation is host/fake_threads.c.
 */

typedef unsigned long  ThreadID;
enum { kNoThreadID = 0 };

typedef unsigned long  ThreadStyle;
enum { kCooperativeThread = 1UL << 0 };

typedef unsigned long  ThreadOptions;
enum { kCreateIfNeeded = 1UL << 2 };

typedef unsigned long  Size;
typedef int            OSErr;
enum { noErr = 0 };

typedef unsigned char  Boolean;

typedef void *(*ThreadEntryTPP)(void *);

#ifndef pascal
#  define pascal   /* no calling-convention keyword on the host */
#endif

OSErr NewThread(ThreadStyle style, ThreadEntryTPP entry, void *param,
                Size stackSize, ThreadOptions options, void **threadResult,
                ThreadID *threadMade);
OSErr DisposeThread(ThreadID threadToDump, void *threadResult, Boolean recycle);
OSErr GetCurrentThread(ThreadID *currentThreadID);
OSErr YieldToAnyThread(void);

/* Test hook: number of threads created so far (monotonic). */
unsigned long hostThreadsCreated(void);

#endif /* LAGRANGE_HOST_TEST_THREADS_H */
