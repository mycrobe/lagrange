/*
 * test_pthread_classic.c -- host unit tests for the pure logic of
 * mac/posix/pthread_classic.c (the Classic Mac POSIX-threads layer over the
 * Thread Manager).
 *
 * The Thread Manager and the heap-zone calls are replaced by
 * tests/host/fake_threads.c (see tests/host/Threads.h for the model), so the
 * mutex/condvar/once/TLS/thread-table logic runs natively.  Nothing here needs
 * a Mac or a PPC toolchain -- `cmake -S tests -B build-host-tests && ctest`.
 */

#include "pthread.h"   /* mac/posix/pthread.h (LAGRANGE_PTHREAD_HOSTTEST) */
#include "test.h"

#include <errno.h>
#include <string.h>

/*------------------------------------------------------------------*/
/* Shared observation state for the worker routines.                */
static int       gRan;
static pthread_t gWorkerSelf;

static void *workerReturnArg(void *arg) {
    gRan++;
    gWorkerSelf = pthread_self();
    return arg;
}

static void *noop(void *arg) {
    (void) arg;
    return NULL;
}

/*------------------------------------------------------------------*/

static void test_equal_self(void) {
    TEST_CHECK(pthread_self() != 0);
    TEST_CHECK(pthread_equal(pthread_self(), pthread_self()));
    TEST_CHECK(!pthread_equal(pthread_self(), (pthread_t) 0xdead));
}

static void test_create_join_result(void) {
    pthread_t th = 0;
    void     *ret = NULL;
    gRan = 0;
    gWorkerSelf = 0;
    TEST_CHECK_EQ(pthread_create(&th, NULL, workerReturnArg, (void *) 0x1234), 0);
    TEST_CHECK(th != 0);
    TEST_CHECK_EQ(pthread_join(th, &ret), 0);
    TEST_CHECK_EQ((long) ret, 0x1234);
    TEST_CHECK_EQ(gRan, 1);
    TEST_CHECK(gWorkerSelf != 0);
    TEST_CHECK(gWorkerSelf != pthread_self());
}

static void test_mutex_default(void) {
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    TEST_CHECK_EQ(pthread_mutex_lock(&m), 0);
    TEST_CHECK_EQ(pthread_mutex_trylock(&m), EBUSY);   /* not recursive */
    TEST_CHECK_EQ(pthread_mutex_unlock(&m), 0);
    TEST_CHECK_EQ(pthread_mutex_lock(&m), 0);          /* re-acquire after free */
    TEST_CHECK_EQ(pthread_mutex_unlock(&m), 0);
    TEST_CHECK_EQ(pthread_mutex_destroy(&m), 0);
}

static void test_mutex_recursive(void) {
    pthread_mutex_t     m = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutexattr_t a;
    TEST_CHECK_EQ(pthread_mutexattr_init(&a), 0);
    TEST_CHECK_EQ(pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE), 0);
    TEST_CHECK_EQ(pthread_mutex_init(&m, &a), 0);
    TEST_CHECK_EQ(pthread_mutex_lock(&m), 0);
    TEST_CHECK_EQ(pthread_mutex_lock(&m), 0);          /* depth 2 */
    TEST_CHECK_EQ(pthread_mutex_trylock(&m), 0);       /* depth 3 */
    TEST_CHECK_EQ(pthread_mutex_unlock(&m), 0);        /* depth 2 */
    TEST_CHECK_EQ(pthread_mutex_trylock(&m), 0);       /* depth 3 again */
    TEST_CHECK_EQ(pthread_mutex_unlock(&m), 0);        /* depth 2 */
    TEST_CHECK_EQ(pthread_mutex_unlock(&m), 0);        /* depth 1 */
    TEST_CHECK_EQ(pthread_mutex_unlock(&m), 0);        /* depth 0 -> free */
    TEST_CHECK_EQ(pthread_mutex_trylock(&m), 0);       /* free again */
    TEST_CHECK_EQ(pthread_mutex_unlock(&m), 0);
    TEST_CHECK_EQ(pthread_mutex_destroy(&m), 0);
}

/* Condition variables: the waiter snapshots the sequence counter under the
   mutex, unlocks, and yields until the signaller bumps it.  The fake scheduler
   runs the signaller on that first yield. */
static pthread_cond_t gCond;

static void *condSignaller(void *arg) {
    (void) arg;
    pthread_cond_signal(&gCond);
    return NULL;
}

static void test_cond_signal(void) {
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_t       th;
    TEST_CHECK_EQ(pthread_cond_init(&gCond, NULL), 0);
    TEST_CHECK_EQ(pthread_mutex_lock(&m), 0);
    TEST_CHECK_EQ(pthread_create(&th, NULL, condSignaller, NULL), 0);
    TEST_CHECK_EQ(pthread_cond_wait(&gCond, &m), 0);
    TEST_CHECK_EQ(pthread_mutex_unlock(&m), 0);
    TEST_CHECK_EQ(pthread_join(th, NULL), 0);
    TEST_CHECK_EQ(pthread_cond_destroy(&gCond), 0);
}

static void test_cond_timedwait_timeout(void) {
    pthread_mutex_t   m = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t    c = PTHREAD_COND_INITIALIZER;
    struct timespec   ts;
    ts.tv_sec = time(NULL) - 1;   /* already past */
    ts.tv_nsec = 0;
    TEST_CHECK_EQ(pthread_mutex_lock(&m), 0);
    TEST_CHECK_EQ(pthread_cond_timedwait(&c, &m, &ts), ETIMEDOUT);
    TEST_CHECK_EQ(pthread_mutex_unlock(&m), 0);
    TEST_CHECK_EQ(pthread_cond_destroy(&c), 0);
}

static int  gOnceCount;
static void onceInit(void) {
    gOnceCount++;
}

static void test_once(void) {
    pthread_once_t once = PTHREAD_ONCE_INIT;
    gOnceCount = 0;
    TEST_CHECK_EQ(pthread_once(&once, onceInit), 0);
    TEST_CHECK_EQ(pthread_once(&once, onceInit), 0);
    TEST_CHECK_EQ(gOnceCount, 1);
}

static void test_tls(void) {
    pthread_key_t key;
    TEST_CHECK_EQ(pthread_key_create(&key, NULL), 0);
    TEST_CHECK(pthread_getspecific(key) == NULL);
    TEST_CHECK_EQ(pthread_setspecific(key, (void *) 0x55), 0);
    TEST_CHECK_EQ((long) pthread_getspecific(key), 0x55);
    TEST_CHECK_EQ(pthread_key_delete(key), 0);
}

/* TLS destructors run for the worker when it finishes (runDtors_ in the
   entry). */
static int           gDtorRan;
static pthread_key_t gDtorKey;

static void tlsDtor(void *value) {
    (void) value;
    gDtorRan++;
}

static void *tlsSetter(void *arg) {
    (void) arg;
    pthread_setspecific(gDtorKey, (void *) 0x9);
    return NULL;
}

static void test_tls_destructor(void) {
    pthread_t th;
    gDtorRan = 0;
    TEST_CHECK_EQ(pthread_key_create(&gDtorKey, tlsDtor), 0);
    TEST_CHECK_EQ(pthread_create(&th, NULL, tlsSetter, NULL), 0);
    TEST_CHECK_EQ(pthread_join(th, NULL), 0);
    TEST_CHECK_EQ(gDtorRan, 1);
    TEST_CHECK_EQ(pthread_key_delete(gDtorKey), 0);
}

static void test_detach(void) {
    pthread_t th;
    TEST_CHECK_EQ(pthread_create(&th, NULL, noop, NULL), 0);
    TEST_CHECK_EQ(pthread_detach(th), 0);
    pthread_yield();                              /* run it; it is not reaped */
    TEST_CHECK_EQ(pthread_join(th, NULL), EINVAL);   /* detached -> invalid */
}

/* Two concurrent worker slots must not collide in the thread table. */
static void *stampSelf(void *arg) {
    *(pthread_t *) arg = pthread_self();
    return NULL;
}

static void test_two_threads(void) {
    pthread_t   a, b, ta = 0, tb = 0;
    TEST_CHECK_EQ(pthread_create(&a, NULL, stampSelf, &ta), 0);
    TEST_CHECK_EQ(pthread_create(&b, NULL, stampSelf, &tb), 0);
    TEST_CHECK_EQ(pthread_join(a, NULL), 0);
    TEST_CHECK_EQ(pthread_join(b, NULL), 0);
    TEST_CHECK(ta != 0 && tb != 0);
    TEST_CHECK(ta != tb);
}

int main(void) {
    TEST_RUN(test_equal_self);
    TEST_RUN(test_create_join_result);
    TEST_RUN(test_mutex_default);
    TEST_RUN(test_mutex_recursive);
    TEST_RUN(test_cond_signal);
    TEST_RUN(test_cond_timedwait_timeout);
    TEST_RUN(test_once);
    TEST_RUN(test_tls);
    TEST_RUN(test_tls_destructor);
    TEST_RUN(test_two_threads);
    TEST_RUN(test_detach);
    return TEST_SUMMARY();
}
