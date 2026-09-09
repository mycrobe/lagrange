/* canvasmain.c — headless canvas harness main for the Phase 0 seam.

   Runs the full lagrange app (portable core + widget kit) against the
   framebuffer sdlcompat backend: no window system, no device. Options:

   --canvas-frames N     quit after roughly N present cycles (default 120)
   --canvas-out PATH     write the final canvas as an RGB PPM to PATH

   Also accepted as env vars CANVAS_FRAMES / CANVAS_OUT. Stock src/main.c SDL
   remains the shipped app; this file only builds into canvasapp. */

#include "app.h"
#include <the_Foundation/tlsrequest.h>

#include <SDL.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

extern Uint64 presentCount_canvas(void);
extern void captureRender_canvas(SDL_Renderer *, const char *); /* returns 0 on success */
extern int windowCount_canvas(void);
extern SDL_Renderer *rendererOfWindow_canvas(int index);
extern void dumpTexture_canvas(const char *prefix, const char *path);
/* viewer backend (sdlview.c) is linked into canvaswin only; the headless
   canvasapp build compiles this file with LAGRANGE_CANVAS_VIEWER=0 and the
   calls compile out */
#if LAGRANGE_CANVAS_VIEWER
int  initSdlView_view(int width, int height);
void deinitSdlView_view(void);
#endif

static const char *outPath_;
static int frameLimit_;
static Uint32 quitAtMs_;
static int clickX_[8], clickY_[8], clickCount_;

static Uint32 clickTick_(Uint32 interval, void *param) {
    (void) interval;
    int *idx = (int *) param;
    if (!idx || *idx >= clickCount_) return 0;
    const int x = clickX_[*idx], y = clickY_[*idx];
    (*idx)++;
    if (*idx < clickCount_) {
        SDL_AddTimer(2000, clickTick_, param); /* next click in the sequence */
    }
    SDL_Event mv = { 0 }, dn = { 0 }, up = { 0 };
    mv.motion.type = SDL_MOUSEMOTION;
    mv.motion.x = x;
    mv.motion.y = y;
    SDL_PushEvent(&mv);
    dn.button.type = SDL_MOUSEBUTTONDOWN;
    dn.button.button = SDL_BUTTON_LEFT;
    dn.button.state = SDL_PRESSED;
    dn.button.x = x;
    dn.button.y = y;
    SDL_PushEvent(&dn);
    up.button.type = SDL_MOUSEBUTTONUP;
    up.button.button = SDL_BUTTON_LEFT;
    up.button.state = SDL_RELEASED;
    up.button.x = x;
    up.button.y = y;
    SDL_PushEvent(&up);
    return 0;
}

static Uint32 quitTick_(Uint32 interval, void *param) {
    /* Called from the shim's timer pump inside the event loop. */
    (void) param;
    (void) interval;
    if (outPath_) {
        const SDL_Renderer *r = rendererOfWindow_canvas(0);
        if (r) {
            SDL_Renderer *nc = (SDL_Renderer *) r;
            captureRender_canvas(nc, outPath_);
        }
    }
    const char *dp = getenv("CANVAS_DUMP_GLYPHCACHE");
    if (dp) {
        dumpTexture_canvas("gly", dp);
    }
    SDL_Event quit;
    memset(&quit, 0, sizeof(quit));
    quit.type = SDL_QUIT;
    SDL_PushEvent(&quit);
    return 0;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    init_Foundation();
    {
        const char *errLog = getenv("CANVAS_ERRLOG");
        if (errLog) {
            FILE *f = freopen(errLog, "wb", stderr);
            if (!f) fprintf(stdout, "cannot open %s\n", errLog);
        }
    }

    const char *envFrames = getenv("CANVAS_FRAMES");
    frameLimit_ = envFrames ? atoi(envFrames) : 120;
    outPath_ = getenv("CANVAS_OUT");
    int cleanArgc = 1;
    char **cleanArgv = calloc((size_t) argc + 1, sizeof(char *));
    cleanArgv[0] = argv[0];
    int windowed = 0;
    int vw = 900, vh = 560; /* start at shim canvas size: 1:1 pixel density */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--canvas-out") && i + 1 < argc) {
            outPath_ = argv[++i];
        } else if (!strcmp(argv[i], "--canvas-frames") && i + 1 < argc) {
            frameLimit_ = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--canvas-window")) {
            windowed = 1;
        } else if (!strcmp(argv[i], "--canvas-view") && i + 1 < argc) {
            sscanf(argv[++i], "%dx%d", &vw, &vh);
            windowed = 1;
        } else {
            cleanArgv[cleanArgc++] = argv[i];
        }
    }

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
#if LAGRANGE_CANVAS_VIEWER
    if (windowed && !getenv("CANVAS_SCALE")) {
        /* must be set before the shim's SDL_Init reads it. Default to 1x so the
           viewer exercises the real non-retina rasterization (matching the 1x
           Cocoa/classic targets this disk is validating); set CANVAS_SCALE=2 to
           get the 2x render instead. */
        setenv("CANVAS_SCALE", "1", 1);
    }
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
        fprintf(stderr, "[canvas] init failed: %s\n", SDL_GetError());
        return -1;
    }
#if LAGRANGE_CANVAS_VIEWER
    if (windowed) {
        if (initSdlView_view(vw, vh)) {
            fprintf(stderr, "[canvas] viewer init failed\n");
        }
    }
#endif
    quitAtMs_ = SDL_AddTimer((Uint32) (frameLimit_ * 16), quitTick_, NULL); /* ~60 fps */
    if (frameLimit_ < 0) {
        quitAtMs_ = 0; /* never quit */
    }
    {
        const char *click = getenv("CANVAS_CLICK");
        if (click) {
            /* sequence of points: "x1,y1;x2,y2;..." clicked 2s apart */
            const char *p = click;
            clickCount_ = 0;
            while (*p && clickCount_ < 8) {
                if (sscanf(p, "%d,%d", &clickX_[clickCount_], &clickY_[clickCount_]) == 2) {
                    clickCount_++;
                }
                while (*p && *p != ';') p++;
                if (*p == ';') p++;
            }
            const char *at = getenv("CANVAS_CLICK_AT_MS");
            static int clickIdx;
            SDL_AddTimer(at ? (Uint32) atoi(at) : 3000, clickTick_, &clickIdx);
        }
    }

    run_App(cleanArgc, cleanArgv);

    SDL_RemoveTimer(quitAtMs_);
#if LAGRANGE_CANVAS_VIEWER
    if (windowed) deinitSdlView_view();
#endif
    SDL_Quit();
    deinit_Foundation();
    fprintf(stderr, "[canvas] frames=%d out=%s\n", frameLimit_, outPath_ ? outPath_ : "-");
    return 0;
}
