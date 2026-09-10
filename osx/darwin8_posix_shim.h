/*
 * darwin8_posix_shim.h -- Tiger (Darwin 8) POSIX conveniences for the_Foundation.
 *
 * the_Foundation targets modern macOS/POSIX, which have strnlen(), clock_gettime()
 * and pthread_setname_np().  Tiger (OS X 10.4, Mac OS X 10.4u SDK) predates all
 * three (strnlen landed in 10.7, clock_gettime in 10.12, pthread_setname_np's
 * macOS form in 10.6).  This header is force-included (-include) into ONLY the
 * darwin8 the_Foundation build (guarded by -DDARWIN8) so its core sources
 * (string.c, time.c, thread.c) compile unchanged.  Each shim is static inline
 * so nothing is emitted into the archive; the final link binds none of them.
 *
 * This is the build-time stand-in for what should eventually live in the
 * the_Foundation apple platform layer (src/platform/apple.c) behind an OS
 * version check.  Track its migration in docs/arcana.md.
 */
#ifndef DARWIN8_POSIX_SHIM_H
#define DARWIN8_POSIX_SHIM_H

#if defined (DARWIN8)

#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>

/* strnlen(): Tiger's <string.h> does not declare it. */
static inline size_t strnlen(const char *s, size_t maxlen) {
    const char *end = (const char *) memchr(s, '\0', maxlen);
    return end ? (size_t) (end - s) : maxlen;
}

/* clock_gettime(CLOCK_REALTIME): Tiger has gettimeofday(), not clock_gettime(). */
#ifndef CLOCK_REALTIME
#  define CLOCK_REALTIME 1 /* only value the shim accepts */
#endif
static inline int clock_gettime(int clk_id, struct timespec *ts) {
    struct timeval tv;
    (void) clk_id;
    if (gettimeofday(&tv, NULL) != 0) {
        return -1;
    }
    ts->tv_sec  = tv.tv_sec;
    ts->tv_nsec = tv.tv_usec * 1000;
    return 0;
}

/* pthread_setname_np() (macOS single-arg form): not on Tiger.  No-op. */
static inline int pthread_setname_np(const char *name) {
    (void) name;
    return 0;
}

#endif /* DARWIN8 */
#endif /* DARWIN8_POSIX_SHIM_H */
