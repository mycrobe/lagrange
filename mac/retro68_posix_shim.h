/*
 * retro68_posix_shim.h -- build-time POSIX compatibility for Retro68 (classic
 * Mac OS 8/9) the_Foundation builds.  The portable core assumes modern POSIX
 * in a few spots that Retro68 does not provide; each shim is a `static inline`
 * or harmless `#define` so nothing leaks into the final link, and it is
 * `-include`d into ONLY the the_Foundation's own Classic TUs (via
 * -DiPlatformClassic + -include in mac/CMakeLists.txt), analogous to the
 * darwin8 shim (osx/darwin8_posix_shim.h) but covering Retro68's deeper void.
 *
 * Retro68 facts (verified 2026-09-12):
 *   - time.h declares clock_gettime/clockid_t ONLY under #if defined(_POSIX_TIMERS),
 *     and no clock_gettime binding exists in the Retro68 system static libs.
 *     struct timespec + CLOCK_REALTIME/CLOCK_MONOTONIC *are* available
 *     unconditionally, so provide a realtime clock over time() (second
 *     resolution; the seam's cert-time validation uses ClassicNet's cn_mac_time,
 *     not this, and the OT watchdogs are multi-second, so the coarseness is
 *     acceptable for the seam smoke).
 *   - pthread.h defines PTHREAD_ONCE_INIT as _PTHREAD_ONCE_INIT but never
 *     defines that token (c11threads.h uses it for a static initializer).
 *   - struct tm has no tm_gmtoff -- handled by guarding the two uses in
 *     src/time.c for iPlatformClassic (that's a struct member, not a header
 *     shim; see docs/arcana.md).
 */
#ifndef RETRO68_POSIX_SHIM_H
#define RETRO68_POSIX_SHIM_H

#include <time.h>   /* struct timespec, CLOCK_REALTIME/MONOTONIC, time() */

/* Retro68's time.h hides clock_gettime behind _POSIX_TIMERS (and has no
   binding).  Provide a static inline realtime clock over time(); second
   resolution is fine for the seam smoke (see the header comment). */
#ifndef _POSIX_TIMERS
static inline int clock_gettime(int clock_id, struct timespec *tp) {
    (void) clock_id;
    tp->tv_sec  = (time_t) time(NULL);
    tp->tv_nsec = 0;
    return 0;
}
#endif

/* pthread.h: PTHREAD_ONCE_INIT expands to _PTHREAD_ONCE_INIT, which is never
   defined.  NOTE: do NOT #define either here -- a `{0}` macro pre-defined
   before the Classic Mac headers (MacTypes.h, pulled by cn_ot.h) collides
   ("expected identifier before numeric constant" at MacTypes.h:301) and
   derails pthread.h's declarations.  The c11threads once init + the
   POSIX-threads-vs-Classic-Mac-headers interop is the open Classic port item;
   see docs/arcana.md "the_Foundation on Retro68". */

#endif /* RETRO68_POSIX_SHIM_H */
