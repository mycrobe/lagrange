/*
 * mac/posix/classic_posix.h -- declarations for the process-level POSIX
 * functions Retro68 implements nowhere and that <time.h> will not declare on
 * this target: clock_gettime/nanosleep live behind `_POSIX_TIMERS`, but
 * switching that on makes <time.h> pull <signal.h>, which in Retro68/Multiverse
 * drags in <OpenTransport.h> and collides with the Unix signal constants (and
 * <MacTypes.h>'s true/false) in every TU -- including the ones that never touch
 * Open Transport.  So instead of a feature-test macro this header declares just
 * the two functions, and mac/CMakeLists.txt force-includes it (`-include`) for
 * the Classic seam targets.  `<time.h>` is safe to include here (without
 * _POSIX_TIMERS it does not pull signal.h).
 *
 * The implementations are in posix_classic.c.
 */
#ifndef LAGRANGE_CLASSIC_POSIX_H
#define LAGRANGE_CLASSIC_POSIX_H

#include <time.h>   /* struct timespec; clockid_t via <sys/types.h> */

#ifdef __cplusplus
extern "C" {
#endif

int clock_gettime(clockid_t clock_id, struct timespec *tp);
int nanosleep(const struct timespec *rqtp, struct timespec *rmtp);

#ifdef __cplusplus
}
#endif

#endif /* LAGRANGE_CLASSIC_POSIX_H */
