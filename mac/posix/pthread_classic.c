/* mac/posix/pthread_classic.c -- POSIX threads over the Mac OS Thread Manager.
 *
 * See mac/posix/pthread.h for why this exists.  The implementation is
 * deliberately small and cooperative:
 *
 *   - the native pthread_* types from <sys/_pthreadtypes.h> are 32-bit opaque
 *     words, so real mutex/cond state lives in fixed side tables here and the
 *     pthread_* value is a 1-based handle into them (0 == lazily allocate),
 *   - threads are kCooperativeThread, so context switches happen only at
 *     YieldToAnyThread(); the spin loops are therefore race-free on a single
 *     CPU without disabling interrupts,
 *   - GCC __sync builtins (lwarx/stwcx on PPC) still guard the acquire/release
 *     edges and the table allocators,
 *   - condition variables use a seqlock-style `seq` counter (snapshot then wait
 *     for a bump); this avoids posix9's single shared `signaled` flag, which
 *     loses wakeups with more than one waiter,
 *   - thread-specific data is a fixed [thread][key] table because the Thread
 *     Manager has no per-thread key store of its own,
 *   - everything that blocks yields.
 */

#include "pthread.h"

#include <Threads.h>     /* Thread Manager: NewThread/YieldToAnyThread/... */
#include <MacMemory.h>   /* GetZone/SetZone: NewPtr (malloc) needs the app zone */
#include <string.h>
#include <errno.h>

#define CLASSIC_MAX_THREADS 32
#define CLASSIC_MAX_KEYS    64
#define CLASSIC_MAX_MUTEXES 64
#define CLASSIC_MAX_CONDS   64
#define CLASSIC_DEFAULT_STACK (256 * 1024)  /* the_Foundation worker threads;
                                               the Thread Manager stack comes
                                               from the app heap.  TLS no longer
                                               runs on a worker (see the seam's
                                               CN_WITH_OT submit path). */

typedef struct {
    ThreadID     tid;
    int          inUse;
    int          isMain;
    int          detached;
    int          disposed;
    volatile int finished;
    void *       result;
    void *       (*start)(void *);
    void *       arg;
} ClassicThread;

typedef struct {
    volatile int locked;
    pthread_t    owner;
    int          count;
    int          recursive;
    int          inUse;
} ClassicMutex;

typedef struct {
    volatile unsigned int seq;
    int                   inUse;
} ClassicCond;

static ClassicThread gThreads[CLASSIC_MAX_THREADS];
static ClassicMutex  gMutexes[CLASSIC_MAX_MUTEXES];
static ClassicCond   gConds[CLASSIC_MAX_CONDS];
static void *        gTls[CLASSIC_MAX_THREADS][CLASSIC_MAX_KEYS];
static void          (*gTlsDtor[CLASSIC_MAX_KEYS])(void *);
static int           gTlsUsed[CLASSIC_MAX_KEYS];
static int           gInited;
static THz           gAppZone;   /* the main thread's heap zone */

/*----------------------------------------------------------------------------*/
/* Side-table allocator guard (cooperative). */

static volatile int gTableLock;

static void lockTable_(void) {
    while (__sync_lock_test_and_set(&gTableLock, 1)) {
        YieldToAnyThread();
    }
}

static void unlockTable_(void) {
    __sync_lock_release(&gTableLock);
}

/*----------------------------------------------------------------------------*/

static void initThreads_(void) {
    int i, j;
    if (gInited) {
        return;
    }
    for (i = 0; i < CLASSIC_MAX_THREADS; i++) {
        gThreads[i].tid = kNoThreadID;
        gThreads[i].inUse = 0;
        gThreads[i].isMain = 0;
        gThreads[i].detached = 0;
        gThreads[i].disposed = 0;
        gThreads[i].finished = 1;
        gThreads[i].result = NULL;
        for (j = 0; j < CLASSIC_MAX_KEYS; j++) {
            gTls[i][j] = NULL;
        }
    }
    for (i = 0; i < CLASSIC_MAX_KEYS; i++) {
        gTlsUsed[i] = 0;
        gTlsDtor[i] = NULL;
    }
    /* Slot 0 is the application (main) thread. */
    gThreads[0].inUse = 1;
    gThreads[0].isMain = 1;
    GetCurrentThread(&gThreads[0].tid);
    gAppZone = GetZone();   /* capture the app heap zone for worker threads */
    gInited = 1;
}

static int indexOfThread_(ThreadID tid) {
    int i;
    for (i = 0; i < CLASSIC_MAX_THREADS; i++) {
        if (gThreads[i].inUse && gThreads[i].tid == tid) {
            return i;
        }
    }
    return -1;
}

static int allocThread_(void) {
    int i;
    initThreads_();
    for (i = 1; i < CLASSIC_MAX_THREADS; i++) {   /* 0 == main */
        if (!gThreads[i].inUse) {
            memset(&gThreads[i], 0, sizeof(gThreads[i]));
            gThreads[i].tid = kNoThreadID;
            gThreads[i].inUse = 1;
            gThreads[i].finished = 0;
            return i;
        }
    }
    return -1;
}

static void runDtors_(int idx) {
    int k;
    if (idx < 0) {
        return;
    }
    for (k = 0; k < CLASSIC_MAX_KEYS; k++) {
        void *val = gTls[idx][k];
        if (val && gTlsUsed[k] && gTlsDtor[k]) {
            gTls[idx][k] = NULL;
            gTlsDtor[k](val);
        }
    }
}

/* Thread Manager entry point.  ThreadEntryTPP on classic PPC is a raw function
   pointer (CALLBACK_API/pascal); the cast at NewThread matches. */
static pascal void *classicThreadEntry_(void *param) {
    ClassicThread *t = (ClassicThread *) param;
    int idx = (int) (t - gThreads);
    THz savedZone = GetZone();
    void *result;
    /* Retro68's malloc is NewPtr, which allocates from the thread's current
       heap zone.  A Thread Manager thread must be pointed at the application
       heap zone or worker allocations (mbedTLS etc.) fail. */
    if (gAppZone) {
        SetZone(gAppZone);
    }
    result = t->start(t->arg);
    t->result = result;
    t->finished = 1;
    runDtors_(idx);
    if (savedZone) {
        SetZone(savedZone);
    }
    /* The thread is now stopping; pthread_join() (or a later pthread_detach()
       observing finished) reclaims it.  Never DisposeThread() the current
       thread from inside itself.  Detached threads simply keep their slot
       until process exit -- the_Foundation always joins its iThreads. */
    return result;
}

/*----------------------------------------------------------------------------*/

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
    int idx = allocThread_();
    ClassicThread *t;
    OSErr err;
    (void) attr;

    if (idx < 0) {
        return EAGAIN;
    }
    t = &gThreads[idx];
    t->start = start_routine;
    t->arg = arg;
    err = NewThread(kCooperativeThread, (ThreadEntryTPP) classicThreadEntry_,
                    t, CLASSIC_DEFAULT_STACK, kCreateIfNeeded, NULL, &t->tid);
    if (err != noErr) {
        t->inUse = 0;
        return EAGAIN;
    }
    if (thread) {
        *thread = (pthread_t) (idx + 1);
    }
    return 0;
}

int pthread_join(pthread_t thread, void **retval) {
    int idx = (int) thread - 1;
    ClassicThread *t;
    if (idx < 0 || idx >= CLASSIC_MAX_THREADS || !gThreads[idx].inUse) {
        return ESRCH;
    }
    t = &gThreads[idx];
    if (t->detached) {
        return EINVAL;
    }
    while (!t->finished) {
        YieldToAnyThread();
    }
    if (retval) {
        *retval = t->result;
    }
    if (!t->disposed) {
        DisposeThread(t->tid, t->result, 0);
        t->disposed = 1;
    }
    t->inUse = 0;
    return 0;
}

int pthread_detach(pthread_t thread) {
    int idx = (int) thread - 1;
    ClassicThread *t;
    if (idx < 0 || idx >= CLASSIC_MAX_THREADS || !gThreads[idx].inUse) {
        return ESRCH;
    }
    t = &gThreads[idx];
    t->detached = 1;
    if (t->finished && !t->disposed) {
        DisposeThread(t->tid, t->result, 0);
        t->disposed = 1;
        t->inUse = 0;
    }
    return 0;
}

void pthread_exit(void *retval) {
    ThreadID tid = kNoThreadID;
    int idx;
    ClassicThread *t;
    GetCurrentThread(&tid);
    idx = indexOfThread_(tid);
    if (idx >= 0) {
        t = &gThreads[idx];
        t->result = retval;
        t->finished = 1;
        runDtors_(idx);
    }
    /* Do NOT DisposeThread() here, and do NOT yield: mark finished and return
       to the entry wrapper so the thread stops at the Thread Manager before a
       waiter can run and DisposeThread() it.  (Disposing the *currently
       running* thread from inside itself is the classic Thread Manager trap;
       disposing it from another thread while it is still running is equally
       unsafe.)  pthread_join()/detach() reclaims it once it has stopped. */
}

pthread_t pthread_self(void) {
    ThreadID tid = kNoThreadID;
    int idx;
    initThreads_();
    GetCurrentThread(&tid);
    idx = indexOfThread_(tid);
    return (idx >= 0 ? (pthread_t) (idx + 1) : 0);
}

int pthread_equal(pthread_t t1, pthread_t t2) {
    return t1 == t2;
}

int pthread_yield(void) {
    YieldToAnyThread();
    return 0;
}

/*----------------------------------------------------------------------------*/

static ClassicMutex *mutexFor_(pthread_mutex_t *m, int create) {
    int idx = (int) *m - 1;
    int i;
    if (idx >= 0 && idx < CLASSIC_MAX_MUTEXES && gMutexes[idx].inUse) {
        return &gMutexes[idx];
    }
    if (!create) {
        return NULL;
    }
    lockTable_();
    idx = (int) *m - 1;
    if (idx >= 0 && idx < CLASSIC_MAX_MUTEXES && gMutexes[idx].inUse) {
        unlockTable_();
        return &gMutexes[idx];
    }
    for (i = 0; i < CLASSIC_MAX_MUTEXES; i++) {
        if (!gMutexes[i].inUse) {
            memset(&gMutexes[i], 0, sizeof(gMutexes[i]));
            gMutexes[i].inUse = 1;
            *m = (pthread_mutex_t) (i + 1);
            unlockTable_();
            return &gMutexes[i];
        }
    }
    unlockTable_();
    return NULL;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    ClassicMutex *mx;
    *mutex = 0;                 /* discard any stale handle */
    mx = mutexFor_(mutex, 1);
    if (!mx) {
        return ENOMEM;
    }
    mx->recursive = (attr ? attr->recursive : 0);
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    ClassicMutex *mx = mutexFor_(mutex, 0);
    if (mx) {
        mx->inUse = 0;
    }
    *mutex = 0;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    ClassicMutex *mx = mutexFor_(mutex, 1);
    pthread_t self;
    if (!mx) {
        return EINVAL;
    }
    self = pthread_self();
    for (;;) {
        if (__sync_bool_compare_and_swap(&mx->locked, 0, 1)) {
            mx->owner = self;
            mx->count = 1;
            return 0;
        }
        if (mx->owner == self && mx->recursive) {
            mx->count++;
            return 0;
        }
        YieldToAnyThread();
    }
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    ClassicMutex *mx = mutexFor_(mutex, 1);
    pthread_t self;
    if (!mx) {
        return EINVAL;
    }
    self = pthread_self();
    if (__sync_bool_compare_and_swap(&mx->locked, 0, 1)) {
        mx->owner = self;
        mx->count = 1;
        return 0;
    }
    if (mx->owner == self && mx->recursive) {
        mx->count++;
        return 0;
    }
    return EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    ClassicMutex *mx = mutexFor_(mutex, 0);
    if (!mx) {
        return EINVAL;
    }
    if (mx->recursive && mx->count > 1) {
        mx->count--;
        return 0;
    }
    mx->count = 0;
    mx->owner = 0;
    __sync_lock_release(&mx->locked);
    return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr) {
    attr->is_initialized = 1;
    attr->recursive = 0;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) {
    attr->is_initialized = 0;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) {
    attr->recursive = (type == PTHREAD_MUTEX_RECURSIVE);
    return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type) {
    *type = (attr->recursive ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_NORMAL);
    return 0;
}

/*----------------------------------------------------------------------------*/

static ClassicCond *condFor_(pthread_cond_t *c, int create) {
    int idx = (int) *c - 1;
    int i;
    if (idx >= 0 && idx < CLASSIC_MAX_CONDS && gConds[idx].inUse) {
        return &gConds[idx];
    }
    if (!create) {
        return NULL;
    }
    lockTable_();
    idx = (int) *c - 1;
    if (idx >= 0 && idx < CLASSIC_MAX_CONDS && gConds[idx].inUse) {
        unlockTable_();
        return &gConds[idx];
    }
    for (i = 0; i < CLASSIC_MAX_CONDS; i++) {
        if (!gConds[i].inUse) {
            memset(&gConds[i], 0, sizeof(gConds[i]));
            gConds[i].inUse = 1;
            *c = (pthread_cond_t) (i + 1);
            unlockTable_();
            return &gConds[i];
        }
    }
    unlockTable_();
    return NULL;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    (void) attr;
    *cond = 0;
    return condFor_(cond, 1) ? 0 : ENOMEM;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
    ClassicCond *c = condFor_(cond, 0);
    if (c) {
        c->inUse = 0;
    }
    *cond = 0;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    ClassicCond *c = condFor_(cond, 1);
    unsigned int seq;
    if (!c) {
        return EINVAL;
    }
    seq = c->seq;
    pthread_mutex_unlock(mutex);
    while (c->seq == seq) {
        YieldToAnyThread();
    }
    pthread_mutex_lock(mutex);
    return 0;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime) {
    ClassicCond *c = condFor_(cond, 1);
    unsigned int seq;
    if (!c) {
        return EINVAL;
    }
    seq = c->seq;
    pthread_mutex_unlock(mutex);
    while (c->seq == seq) {
        if (time(NULL) >= abstime->tv_sec) {
            pthread_mutex_lock(mutex);
            return ETIMEDOUT;
        }
        YieldToAnyThread();
    }
    pthread_mutex_lock(mutex);
    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond) {
    ClassicCond *c = condFor_(cond, 1);
    if (!c) {
        return EINVAL;
    }
    c->seq++;
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
    ClassicCond *c = condFor_(cond, 1);
    if (!c) {
        return EINVAL;
    }
    c->seq++;
    return 0;
}

/*----------------------------------------------------------------------------*/

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void)) {
    for (;;) {
        if (once_control->init_executed) {
            return 0;
        }
        if (!once_control->is_initialized) {
            if (__sync_bool_compare_and_swap(&once_control->is_initialized, 0, 1)) {
                init_routine();
                once_control->init_executed = 1;
                return 0;
            }
        }
        YieldToAnyThread();   /* another thread is running the init */
    }
}

/*----------------------------------------------------------------------------*/

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    int i;
    initThreads_();
    for (i = 0; i < CLASSIC_MAX_KEYS; i++) {
        if (!gTlsUsed[i]) {
            gTlsUsed[i] = 1;
            gTlsDtor[i] = destructor;
            *key = (pthread_key_t) i;
            return 0;
        }
    }
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key) {
    if (key >= CLASSIC_MAX_KEYS) {
        return EINVAL;
    }
    gTlsUsed[key] = 0;
    gTlsDtor[key] = NULL;
    return 0;
}

void *pthread_getspecific(pthread_key_t key) {
    ThreadID tid = kNoThreadID;
    int idx;
    if (key >= CLASSIC_MAX_KEYS) {
        return NULL;
    }
    GetCurrentThread(&tid);
    idx = indexOfThread_(tid);
    if (idx < 0) {
        return NULL;
    }
    return gTls[idx][key];
}

int pthread_setspecific(pthread_key_t key, const void *value) {
    ThreadID tid = kNoThreadID;
    int idx;
    if (key >= CLASSIC_MAX_KEYS) {
        return EINVAL;
    }
    GetCurrentThread(&tid);
    idx = indexOfThread_(tid);
    if (idx < 0) {
        return EINVAL;
    }
    gTls[idx][key] = (void *) value;
    return 0;
}
