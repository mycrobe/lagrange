/*
 * mac/posix/pthread.h -- minimal POSIX threads for the lagrange Classic
 * (Mac OS 8/9 PPC, Retro68) build, implemented over the Mac OS Thread Manager.
 *
 * WHY THIS EXISTS
 * ---------------
 * Retro68 ships newlib's RTEMS-flavored <pthread.h>, but NO library implements
 * the pthreads API: the header gates every declaration on `_POSIX_THREADS`
 * (which Retro68's <sys/features.h> never defines for this target), and no
 * static lib defines `pthread_create`/`pthread_mutex_*` etc. (libThreadsLib.a
 * only provides NewThread/DisposeThread/GetCurrentThread/YieldToAnyThread).
 * the_Foundation, however, is built on a real thread backend: `iThread`/
 * `iMutex`/`iCondition` go through C11 threads, which `src/c11threads.c`
 * implements on top of pthreads, and the ClassicNet iTlsRequest seam drives
 * its fetch on a worker thread.
 *
 * So the Classic flavor provides its own pthread API here, mapped onto the
 * Thread Manager, and lets this header win the <pthread.h> search via
 * `include_directories(BEFORE ...)` in mac/CMakeLists.txt.  Same shape as
 * Scottcjn/posix9's pthread layer (and the historical GUSI library), but with
 * a correct cooperative mutex/condvar implementation.
 *
 * TYPE STRATEGY: newlib's <sys/types.h> already defines the pthread_* types
 * (via <sys/_pthreadtypes.h>, under the default __POSIX_VISIBLE), so this
 * header REUSES them rather than redefining them (which would conflict).  The
 * mutex/cond types are 32-bit opaque words, so the real state lives in small
 * side tables in pthread_classic.c and the pthread_* value is a 1-based handle
 * into that table.  0 means "statically/default initialized, allocate lazily".
 *
 * Deliberately NOT defined, so the_Foundation's feature probes pick the
 * conservative paths:
 *   - no PTHREAD_MUTEX_TIMED_NP      -> C11THREADS_NO_TIMED_MUTEX (trylock poll)
 *   - no pthread_cancel declaration  -> iHavePThreadCancel stays unset
 *
 * Threads are kCooperativeThread: the scheduler only switches at
 * YieldToAnyThread(), so the spin-wait primitives below are race-free without
 * disabling interrupts (GCC __sync ops are used for the acquire/release edges
 * anyway).  All wait loops yield.
 */
#ifndef LAGRANGE_CLASSIC_PTHREAD_H
#define LAGRANGE_CLASSIC_PTHREAD_H

#if defined (LAGRANGE_PTHREAD_HOSTTEST)
/* Host unit-test build (tests/test_pthread_classic.c): the real pthread_* types
   are not usable here (on the target they are newlib words from
   <sys/_pthreadtypes.h>; on a modern host <time.h>/<sys/types.h> do not define
   them at all).  Provide integer stand-ins so the pure logic in
   pthread_classic.c can run against the fake Thread Manager.  Never defined for
   real builds. */
typedef unsigned long pthread_t;
typedef unsigned int  pthread_key_t;
typedef unsigned long pthread_mutex_t;
typedef unsigned long pthread_cond_t;
typedef struct { int is_initialized; int recursive; } pthread_mutexattr_t;
typedef int           pthread_condattr_t;
typedef struct { int is_initialized; int init_executed; } pthread_once_t;
typedef int           pthread_attr_t;
#include <stddef.h>
#include <time.h>        /* struct timespec / time() on the host */
#else
#include <sys/types.h>   /* native pthread_t/pthread_mutex_t/... (opaque words) */
#include <stddef.h>
#include <time.h>        /* struct timespec for the timed calls */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------
 * Constants / initializers (types come from the system header)
 *--------------------------------------------------------------------------*/

#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_RECURSIVE  1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT    PTHREAD_MUTEX_NORMAL

/* NB: PTHREAD_CREATE_* come from <sys/_pthreadtypes.h>, and on Retro68/newlib
   their values are the opposite of POSIX (JOINABLE 1, DETACHED 0).  This layer
   ignores the attr entirely (every thread is joinable), so the value is moot. */

#define PTHREAD_MUTEX_INITIALIZER ((pthread_mutex_t) 0)
#define PTHREAD_COND_INITIALIZER  ((pthread_cond_t) 0)
#define PTHREAD_ONCE_INIT         { 0, 0 }

/*----------------------------------------------------------------------------
 * Threads
 *--------------------------------------------------------------------------*/

int  pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                    void *(*start_routine)(void *), void *arg);
int  pthread_join(pthread_t thread, void **retval);
int  pthread_detach(pthread_t thread);
void pthread_exit(void *retval);
pthread_t pthread_self(void);
int  pthread_equal(pthread_t t1, pthread_t t2);
int  pthread_yield(void);

/*----------------------------------------------------------------------------
 * Mutexes
 *--------------------------------------------------------------------------*/

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);
int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type);

/*----------------------------------------------------------------------------
 * Condition variables
 *--------------------------------------------------------------------------*/

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);

/*----------------------------------------------------------------------------
 * One-time initialization and thread-specific data
 *--------------------------------------------------------------------------*/

int  pthread_once(pthread_once_t *once_control, void (*init_routine)(void));

int  pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int  pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int  pthread_setspecific(pthread_key_t key, const void *value);

/*----------------------------------------------------------------------------
 * Process-level bits the C11-threads shim calls but Retro68 lacks.
 * (clock_gettime/nanosleep come from <time.h> once the build defines
 * _POSIX_TIMERS; sched_yield has no declaration at all.)
 *--------------------------------------------------------------------------*/

int sched_yield(void);

#ifdef __cplusplus
}
#endif

#endif /* LAGRANGE_CLASSIC_PTHREAD_H */
