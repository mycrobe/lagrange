/* aquamain.c — main() for the Aqua (Tiger/Leopard PPC) canvas host.

   The thin driver over the portable core: run_App() is the widget-kit/event
   loop; the Aqua window backend (aquaview.m) supplies the NSApplication, the
   single NSWindow/NSView and the shim event translation.  This main only
   initialises the_Foundation + the shim SDL layer, starts the view, and hands
   control to run_App() — no nibs, no Xcode, no harness auto-quit timer (the
   user closes the window to quit, which pushes SDL_QUIT).

   Copyright 2026 the lagrange port.  Distributed under the BSD-2-Clause license
   of the core it drives (see LICENSE.md). */

#include "app.h"
#include "aquaview.h"
#include <the_Foundation/tlsrequest.h>

#include <SDL.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

/* Mac OS X LaunchServices injects a "-psn_<process serial number>" argument
   into GUI apps launched from Finder/`open` (it identifies the process to the
   WindowServer).  lagrange's command-line parser doesn't know it, so drop it
   before handing argv to the portable core -- otherwise `run_App` would report
   "Unknown option: p" and terminate.  In-place: rewritten argv is never longer. */
static void stripPsnArgs_(int *argc, char ***argv) {
    int keep = 0, i;
    for (i = 0; i < *argc; i++) {
        if (strncmp((*argv)[i], "-psn_", 5) == 0) {
            continue;
        }
        (*argv)[keep++] = (*argv)[i];
    }
    *argc = keep;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    init_Foundation();
    /* This host exits via AppKit ([NSApp run] -> terminate:), which bypasses
       main()'s normal teardown, so deinit_Foundation() never runs and the
       Foundation's atexit check asserts.  Registering it here (atexit runs last-
       registered-first) lets it clean up before that check at process exit. */
    atexit(deinit_Foundation);
    {
        const char *errLog = getenv("AQUA_ERRLOG");
        if (errLog) {
            FILE *f = freopen(errLog, "wb", stderr);
            if (!f) fprintf(stdout, "cannot open %s\n", errLog);
            else    setvbuf(stderr, NULL, _IONBF, 0);   /* the log is our post-mortem tool */
        }
        else if (getenv("AQUA_DEBUG")) {
            FILE *f = freopen("/tmp/L4.log", "wb", stderr);
            if (f) {
                setvbuf(stderr, NULL, _IONBF, 0);
                fprintf(stderr, "[aqua] stderr->/tmp/L4.log\n");
            }
        }
    }

    /* Enclosing autorelease pool for the whole main-thread lifecycle (menu + UI
       assembly, SDL_Init, and everything [NSApp run] autoreleases on Tiger).
       aquamain.c is C, so the pool frame lives in aquaview.m; see aquaview.h. */
    void *pool = beginAutoreleasePool_Aqua();

    setCiphers_TlsRequest("ECDHE-ECDSA-AES256-GCM-SHA384:"
                          "ECDHE-ECDSA-CHACHA20-POLY1305:"
                          "ECDHE-ECDSA-AES128-GCM-SHA256:"
                          "ECDHE-RSA-AES256-GCM-SHA384:"
                          "ECDHE-RSA-CHACHA20-POLY1305:"
                          "ECDHE-RSA-AES128-GCM-SHA256:"
                          "DHE-RSA-AES256-GCM-SHA384");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    SDL_EnableScreenSaver();
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    /* 1x rasterization: this is a non-retina Aqua target, verified against the
       real hardware's pixel density (mirror the canvas host's default). */
    if (!getenv("CANVAS_SCALE")) {
        setenv("CANVAS_SCALE", "1", 1);
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
        fprintf(stderr, "[aqua] SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    fprintf(stderr, "[aqua] SDL_Init ok\n");

    if (initAquaView_app(900, 560)) {
        fprintf(stderr, "[aqua] view init failed\n");
        return -1;
    }
    fprintf(stderr, "[aqua] view init ok\n");

    fprintf(stderr, "[aqua] entering AppKit main loop\n");
    stripPsnArgs_(&argc, &argv);
    init_App(argc, argv);
    beginAppEventLoop_App();
    runAquaMainLoop();     /* steps the widget kit from a timer inside [NSApp run] */
    fprintf(stderr, "[aqua] AppKit main loop returned\n");
    deinit_App_Instance();

    deinitAquaView_app();
    SDL_Quit();
    deinit_Foundation();
    endAutoreleasePool_Aqua(pool);
    return 0;
}
