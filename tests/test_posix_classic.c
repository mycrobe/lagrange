/*
 * test_posix_classic.c -- host unit tests for mac/posix/posix_classic.c: the
 * process-level POSIX bits Retro68 lacks (clock_gettime / nanosleep /
 * sched_yield).
 *
 * gettimeofday and <time.h> are the host's; TickCount() and YieldToAnyThread()
 * come from tests/host/fake_threads.c, where a yield advances the fake tick
 * counter (see tests/host/Events.h).  nanosleep is therefore fully
 * deterministic: ticks == seconds*60 (nsecs*60/1e9), one yield per tick.
 */

#include "pthread.h"   /* sched_yield decl (mac/posix/pthread.h) */
#include "Events.h"    /* hostTickCount (fake TickCount control) */
#include "test.h"

#include <time.h>
#include <sys/time.h>

static void test_sched_yield(void) {
    unsigned long before = hostTickCount();
    TEST_CHECK_EQ(sched_yield(), 0);
    TEST_CHECK_EQ(hostTickCount(), before + 1);
}

static void test_clock_gettime(void) {
    struct timespec ts;
    struct timeval  tv;
    TEST_CHECK_EQ(clock_gettime(CLOCK_REALTIME, &ts), 0);
    gettimeofday(&tv, NULL);
    /* serviced from the wall clock (within a second), sane nsec range */
    TEST_CHECK((long) ts.tv_sec - (long) tv.tv_sec <= 1 &&
               (long) tv.tv_sec - (long) ts.tv_sec <= 1);
    TEST_CHECK(ts.tv_nsec >= 0 && ts.tv_nsec < 1000000000L);
    TEST_CHECK((long) ts.tv_sec > 1600000000L);   /* after 2020-09 */
}

/* CLOCK_MONOTONIC (and anything else) is served by the same wall clock. */
static void test_clock_gettime_monotonic(void) {
    struct timespec a, b;
    TEST_CHECK_EQ(clock_gettime(CLOCK_MONOTONIC, &a), 0);
    TEST_CHECK_EQ(clock_gettime(CLOCK_REALTIME, &b), 0);
    TEST_CHECK((long) a.tv_sec - (long) b.tv_sec <= 1 &&
               (long) b.tv_sec - (long) a.tv_sec <= 1);
}

static void test_nanosleep_one_second(void) {
    struct timespec req = { 1, 0 };
    struct timespec rem = { 7, 7 };
    unsigned long   before = hostTickCount();
    TEST_CHECK_EQ(nanosleep(&req, &rem), 0);
    TEST_CHECK(hostTickCount() - before >= 60);
    TEST_CHECK_EQ(rem.tv_sec, 0);      /* rem is zeroed on completion */
    TEST_CHECK_EQ(rem.tv_nsec, 0);
}

static void test_nanosleep_half_second(void) {
    struct timespec req = { 0, 500000000 };
    unsigned long   before = hostTickCount();
    TEST_CHECK_EQ(nanosleep(&req, NULL), 0);
    TEST_CHECK(hostTickCount() - before >= 30);
}

static void test_nanosleep_zero(void) {
    struct timespec req = { 0, 0 };
    struct timespec rem = { 5, 5 };
    unsigned long   before = hostTickCount();
    TEST_CHECK_EQ(nanosleep(&req, &rem), 0);
    TEST_CHECK_EQ(hostTickCount() - before, 1);   /* do-while yields once */
    TEST_CHECK_EQ(rem.tv_sec, 0);
    TEST_CHECK_EQ(rem.tv_nsec, 0);
}

int main(void) {
    TEST_RUN(test_sched_yield);
    TEST_RUN(test_clock_gettime);
    TEST_RUN(test_clock_gettime_monotonic);
    TEST_RUN(test_nanosleep_one_second);
    TEST_RUN(test_nanosleep_half_second);
    TEST_RUN(test_nanosleep_zero);
    return TEST_SUMMARY();
}
