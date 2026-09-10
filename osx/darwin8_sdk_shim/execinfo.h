/*
 * darwin8_sdk_shim/execinfo.h -- minimal execinfo shim for Tiger (10.4u SDK).
 * Tiger has no <execinfo.h>.  The canvas shim (src/ui/canvas/sdlcompat.c) uses
 * backtrace()/backtrace_symbols() only for its crash-dump debugging path, so a
 * frame-pointer walk over __builtin_return_address is more than enough (the
 * host build's libexecinfo backtrace is not a correctness dependency; it is a
 * diagnostics convenience).  Static inline so nothing is emitted unless used;
 * backtrace_symbols() returns a malloc'd array the caller frees (as the real
 * one does).
 *
 * Scoped to the darwin8 Aqua canvas host build; see docs/arcana.md for the
 * migration target (the_Foundation apple platform layer).
 */
#ifndef DARWIN8_EXECINFO_SHIM_H
#define DARWIN8_EXECINFO_SHIM_H

#include <stdio.h>
#include <stdlib.h>

static inline int backtrace(void **buffer, int size) {
    /* Walk the PPC32 frame-pointer chain (r30), up to `size` entries, exactly
       like libexecinfo's backtrace.  Unwind past the first frame only if the
       chain is intact; fall back to the few callers we can address directly. */
    void **fp = NULL;
#if defined (__ppc__) || defined (__powerpc__)
    __asm__ volatile ("mr %0, r30" : "=r" (fp));
#endif
    int n = 0;
    while (fp && n < size) {
        /* frame pointer points at the saved r30; return address is at fp[1]. */
        void *ra = fp[1];
        if (ra == 0 || ra == (void *) -1) break;
        buffer[n++] = ra;
        fp = (void **) fp[0];
        if (fp <= (void **) ra && n > 1) break; /* corrupt chain guard */
    }
    if (n == 0 && size > 0) {
        /* no reliable frame chain: at least report the immediate callers */
        buffer[0] = __builtin_return_address(0);
        if (size > 1) buffer[1] = __builtin_return_address(1);
        n = size > 1 ? 2 : 1;
    }
    return n;
}

static inline char **backtrace_symbols(void *const *buffer, int size) {
    /* Format each frame as "N  <addr>" (hex, no symbol resolution -- we have
       no dlsym()/nm at runtime here).  Caller free()s the returned array. */
    char **sym = (char **) malloc(((size_t) size + 1) * sizeof(char *) +
                                  (size_t) size * 32);
    if (!sym) return NULL;
    char *base = (char *) (sym + size + 1);
    for (int i = 0; i < size; i++) {
        unsigned long addr = (unsigned long) buffer[i];
        int len = snprintf(base, 32, "%2d  %p", i, (void *) addr);
        sym[i] = base;
        base += (size_t) len + 1;
    }
    sym[size] = NULL;
    return sym;
}

#endif /* DARWIN8_EXECINFO_SHIM_H */
