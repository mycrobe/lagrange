/*
 * fake_threads.c -- host implementation of the fake Thread Manager and Memory
 * Manager bits declared in tests/host/{Threads,MacMemory}.h.
 *
 * See Threads.h for the cooperative model.  This is test scaffolding only; it
 * is never linked into a target build.
 */

#include "Threads.h"
#include "MacMemory.h"
#include "Events.h"

#include <string.h>

enum { HOST_MAX_THREADS = 32, kThreadReady = 1, kThreadDone = 2 };

typedef struct {
    int          used;
    int          state;
    ThreadID     tid;
    ThreadEntryTPP entry;
    void *       param;
} HostThread;

static HostThread  gThreads[HOST_MAX_THREADS];
static ThreadID    gNextId = 2;   /* 1 == the main thread */
static ThreadID    gCurrent = 1;
static unsigned long gCreated = 0;
static UInt32      gTicks = 0;    /* advances on every yield (time passing) */

OSErr NewThread(ThreadStyle style, ThreadEntryTPP entry, void *param,
                Size stackSize, ThreadOptions options, void **threadResult,
                ThreadID *threadMade) {
    int i;
    (void) style;
    (void) stackSize;
    (void) options;
    (void) threadResult;
    for (i = 0; i < HOST_MAX_THREADS; i++) {
        if (!gThreads[i].used) {
            gThreads[i].used = 1;
            gThreads[i].state = kThreadReady;
            gThreads[i].tid = gNextId++;
            gThreads[i].entry = entry;
            gThreads[i].param = param;
            gCreated++;
            if (threadMade) {
                *threadMade = gThreads[i].tid;
            }
            return noErr;
        }
    }
    return -1;   /* memFullErr-ish */
}

OSErr DisposeThread(ThreadID threadToDump, void *threadResult, Boolean recycle) {
    int i;
    (void) threadResult;
    (void) recycle;
    for (i = 0; i < HOST_MAX_THREADS; i++) {
        if (gThreads[i].used && gThreads[i].tid == threadToDump) {
            gThreads[i].used = 0;
            gThreads[i].state = 0;
        }
    }
    return noErr;
}

OSErr GetCurrentThread(ThreadID *currentThreadID) {
    if (currentThreadID) {
        *currentThreadID = gCurrent;
    }
    return noErr;
}

OSErr YieldToAnyThread(void) {
    int i;
    gTicks++;   /* yielding lets time pass (drives the nanosleep shim) */
    for (i = 0; i < HOST_MAX_THREADS; i++) {
        if (gThreads[i].used && gThreads[i].state == kThreadReady) {
            ThreadID saved = gCurrent;
            gThreads[i].state = kThreadDone;
            gCurrent = gThreads[i].tid;
            (void) gThreads[i].entry(gThreads[i].param);
            gCurrent = saved;
            return noErr;
        }
    }
    return noErr;
}

unsigned long hostThreadsCreated(void) {
    return gCreated;
}

UInt32 TickCount(void) {
    return gTicks;
}

unsigned long hostTickCount(void) {
    return gTicks;
}

/*------------------------------------------------------------------*/
/* Memory Manager zone stubs (the layer only saves and restores it). */

static THz gZone = (THz) 1;

THz GetZone(void) {
    return gZone;
}

void SetZone(THz zone) {
    gZone = zone;
}
