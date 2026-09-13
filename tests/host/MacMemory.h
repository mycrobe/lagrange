#ifndef LAGRANGE_HOST_TEST_MACMEMORY_H
#define LAGRANGE_HOST_TEST_MACMEMORY_H

/*
 * Fake Mac Memory Manager bits used by mac/posix/pthread_classic.c (heap zone
 * capture/restore in the thread entry).  Declarations only; implementation in
 * host/fake_threads.c.
 */

typedef void *THz;

THz  GetZone(void);
void SetZone(THz zone);

#endif /* LAGRANGE_HOST_TEST_MACMEMORY_H */
