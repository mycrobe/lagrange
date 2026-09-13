/* mac/posix/posix_classic.c -- the process-level POSIX bits Retro68 lacks but
 * the_Foundation's Classic build needs.  Paired with mac/posix/pthread.h.
 *
 *   clock_gettime(CLOCK_REALTIME) -> gettimeofday (µs resolution)
 *   nanosleep                     -> TickCount() poll with YieldToAnyThread
 *   sched_yield                   -> YieldToAnyThread
 *
 * The declarations for clock_gettime/nanosleep come from Retro68's <time.h>,
 * which only exposes them under `_POSIX_TIMERS`; mac/CMakeLists.txt defines
 * that for the Classic targets instead of force-including a shim header (the
 * old `-include` approach).  sched_yield has no declaration anywhere on this
 * target and is declared in mac/posix/pthread.h.
 *
 * Resolution caveat: CLOCK_MONOTONIC and friends are all served by the wall
 * clock, and nanosleep is bounded by the 60Hz TickCount (≈16ms granularity).
 * The seam's timeouts are multi-second, so that is ample.
 */

#include "pthread.h"     /* sched_yield */

#include <time.h>
#include <sys/time.h>
#include <Events.h>      /* TickCount */
#include <Threads.h>     /* YieldToAnyThread */

int clock_gettime(clockid_t clock_id, struct timespec *tp) {
    struct timeval tv;
    (void) clock_id;
    if (gettimeofday(&tv, NULL) != 0) {
        return -1;
    }
    tp->tv_sec = (time_t) tv.tv_sec;
    tp->tv_nsec = (long) tv.tv_usec * 1000L;
    return 0;
}

int nanosleep(const struct timespec *rqtp, struct timespec *rmtp) {
    UInt32 start = TickCount();
    UInt32 ticks = (UInt32) (rqtp->tv_sec * 60L) +
                   (UInt32) ((rqtp->tv_nsec * 60L) / 1000000000L);
    do {
        YieldToAnyThread();
    } while ((UInt32) (TickCount() - start) < ticks);
    if (rmtp) {
        rmtp->tv_sec = 0;
        rmtp->tv_nsec = 0;
    }
    return 0;
}

int sched_yield(void) {
    YieldToAnyThread();
    return 0;
}
