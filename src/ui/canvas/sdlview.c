/* sdlview.c — SDL2 windowed I/O for the canvas shim (interactive debug only).

   All rendering stays in the shim's software framebuffer; this file moves
   pixels to the screen and translates native SDL2 events into shim events.
   It is the Phase 0 stand-in for per-OS window backends (Aqua, Toolbox).

   Compiled against the real SDL2 dylib in its own translation unit. Every
   SDL2 symbol is looked up through dlsym on the real dylib's handle, NOT by
   name, because the shim TU in the same binary exports identically named
   SDL symbols and static linking binds all references to the local object. */

#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* --- minimal hand-rolled SDL2 ABI surface (values match SDL 2.26) --------- */

typedef void *SDL2_Window;
typedef void *SDL2_Renderer;
typedef void *SDL2_Texture;

#define SDL_INIT_VIDEO 0x00000020u
#define SDL_WINDOWPOS_CENTERED 0x2FFF0000
#define SDL_WINDOW_ALLOW_HIGHDPI 0x00002000u
#define SDL_WINDOW_RESIZABLE 0x00000020u
#define SDL_RENDERER_SOFTWARE 0x00000001u
#define SDL_RENDERER_ACCELERATED 0x00000002u
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_PIXELFORMAT_ABGR8888 0x16762004u /* real SDL2 value */
#define SDL_PIXELFORMAT_RGBA8888 0x16462004u

enum {
    EVT_QUIT = 0x100,
    EVT_WINDOWEVENT = 0x200, /* real SDL2 value */
    EVT_KEYDOWN = 0x300,
    EVT_KEYUP = 0x301,
    EVT_TEXTINPUT = 0x303,
    EVT_MOUSEMOTION = 0x400,
    EVT_MOUSEBUTTONDOWN = 0x401,
    EVT_MOUSEBUTTONUP = 0x402,
    EVT_MOUSEWHEEL = 0x403,
};

/* lagrange's per-pixel/inertia wheel flags, carried in MouseWheelEvent.direction
   above the SDL_MOUSEWHEEL_NORMAL/FLIPPED values (see src/ui/util.h; iBit(9..11)). */
#define WHEEL_FLAG_PERPIXEL (1u << 8)  /* iBit(9):  finger/trackpad (precise) scroll */
#define WHEEL_FLAG_INERTIA  (1u << 9)  /* iBit(10): momentum/inertia phase */
#define WHEEL_FLAG_FINISHED (1u << 10) /* iBit(11): scroll ended */

typedef struct {
    unsigned type, timestamp, windowID;
    unsigned char state, repeat, pad1, pad2;
    int keysym_scancode, keysym_sym;
    unsigned short keysym_mod;
    unsigned keysym_unused;
} SDL2_KeyEvent;

typedef struct {
    unsigned type, timestamp, windowID, which, state;
    int x, y, xrel, yrel;
} SDL2_MotionEvent;

typedef struct {
    unsigned type, timestamp, windowID, which;
    unsigned char button, state, clicks, padding1;
    int x, y;
} SDL2_ButtonEvent;

typedef struct {
    unsigned type, timestamp, windowID, which;
    int x, y;
    unsigned direction;
    float preciseX, preciseY;
    int mouseX, mouseY;
} SDL2_WheelEvent;

typedef struct {
    unsigned type, timestamp, windowID;
    char text[32];
} SDL2_TextEvent;

typedef struct {
    unsigned type, timestamp;
} SDL2_QuitEvent;

typedef struct {
    unsigned type, timestamp, windowID;
    unsigned char event, pad1, pad2, pad3;
    int data1, data2;
} SDL2_WindowEvent;

/* union large enough for the SDL2 event set (56 bytes in SDL 2.26) */
typedef union {
    unsigned type;
    SDL2_QuitEvent quit;
    SDL2_KeyEvent key;
    SDL2_MotionEvent motion;
    SDL2_ButtonEvent button;
    SDL2_WheelEvent wheel;
    SDL2_TextEvent text;
    SDL2_WindowEvent window;
    unsigned char bytes[64];
} SDL2_Event;

typedef struct { int x, y, w, h; } SDL2_Rect; /* matches SDL_Rect ABI for RenderCopy */

/* --- shim-side ABI, hand-declared (see sdlcompat.h) ----------------------- */

typedef unsigned char shim_Uint8;

extern int windowCount_canvas(void);
extern const shim_Uint8 *canvasPixels_canvas(int winIndex, int *w, int *h, int *pitch);
extern void setPresentHook_canvas(void (*cb)(int winIndex), void *unused);
extern void setPumpHook_canvas(void (*cb)(void), void *unused);
extern void setCursorHook_canvas(void (*cb)(int cursorId, void *unused), void *unused);
extern int SDL_PushEvent(void *event); /* shim's SDL_PushEvent */
extern int canvasScale_canvas(void);   /* app pixel ratio (= shim g_canvasScale) */

/* --- dlsym table ---------------------------------------------------------- */

static int (*p_Init)(unsigned);
static int (*p_PollEvent)(SDL2_Event *);
static SDL2_Window (*p_CreateWindow)(const char *, int, int, int, int, unsigned);
static void (*p_DestroyWindow)(SDL2_Window);
static SDL2_Renderer (*p_CreateRenderer)(SDL2_Window, int, unsigned);
static void (*p_DestroyRenderer)(SDL2_Renderer);
static void (*p_DestroyTexture)(SDL2_Texture);
static int (*p_GetWindowSize)(SDL2_Window, int *, int *);
static int (*p_SetRenderDrawColor)(SDL2_Renderer, unsigned char, unsigned char,
                                   unsigned char, unsigned char);
static int (*p_RenderClear)(SDL2_Renderer);
static int (*p_RenderCopy)(SDL2_Renderer, SDL2_Texture, const void *, const void *);
static int (*p_RenderPresent)(SDL2_Renderer);
static int (*p_UpdateTexture)(SDL2_Texture, const void *, const void *, int);
static SDL2_Texture (*p_CreateTexture)(SDL2_Renderer, unsigned, int, int, int);
static void (*p_Quit)(void);
static const char *(*p_GetError)(void);
static int (*p_GetRendererOutputSize)(void *, int *, int *);
static void *(*p_CreateSystemCursor)(int);
static void (*p_SetCursor)(void *);

static int loadFuncs_(void) {
    if (p_Init) return 0;
    void *h = dlopen("libSDL2-2.0.0.dylib", RTLD_NOW | RTLD_GLOBAL);
    if (!h) h = dlopen("@rpath/libSDL2-2.0.0.dylib", RTLD_NOW | RTLD_GLOBAL);
    if (!h) h = dlopen("/tmp/kilo/sdl2/lib/libSDL2-2.0.0.dylib", RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        fprintf(stderr, "[sdlview] no SDL2 dylib\n");
        return -1;
    }
#define GET(SYM) p_##SYM = dlsym(h, "SDL_" #SYM)
    GET(Init);
    GET(PollEvent);
    GET(CreateWindow);
    GET(DestroyWindow);
    GET(CreateRenderer);
    GET(DestroyRenderer);
    GET(DestroyTexture);
    GET(GetWindowSize);
    GET(SetRenderDrawColor);
    GET(RenderClear);
    GET(RenderCopy);
    GET(RenderPresent);
    GET(UpdateTexture);
    GET(CreateTexture);
    GET(Quit);
    GET(GetError);
    GET(GetRendererOutputSize);
    GET(CreateSystemCursor);
    GET(SetCursor);
#undef GET
    return 0;
}

/* --- viewer state --------------------------------------------------------- */

static SDL2_Window view_win;
static SDL2_Renderer view_ren;
static SDL2_Texture view_tex;
static int view_texW, view_texH;
static float view_scaleX = 1.0f, view_scaleY = 1.0f;
static float wheelAccumX_, wheelAccumY_; /* fractional trackpad scroll */

static long presentCount_;

static void syncCanvasTexture_(void) {
    int cw, ch, cpitch;
    const shim_Uint8 *px = canvasPixels_canvas(0, &cw, &ch, &cpitch);
    if (!px) return;
    if (!view_tex || view_texW != cw || view_texH != ch) {
        if (view_tex) p_DestroyTexture(view_tex);
        view_tex = p_CreateTexture(view_ren, SDL_PIXELFORMAT_ABGR8888,
                                   SDL_TEXTUREACCESS_STREAMING, cw, ch);
        view_texW = cw;
        view_texH = ch;
        int vw, vh;
        p_GetWindowSize(view_win, &vw, &vh);
        view_scaleX = (float) vw / (float) cw;
        view_scaleY = (float) vh / (float) ch;
    }
    int retUpd = p_UpdateTexture(view_tex, NULL, px, cpitch);
    if (presentCount_ < 3) {
        fprintf(stderr, "[sdlview] tex=%p dims=%dx%d upd=%d sample=(%d,%d,%d,%d)\n",
                (void *) view_tex, view_texW, view_texH, retUpd,
                px[0], px[1], px[2], px[3]);
    }
}

static void cursorHook_(int cursorId, void *unused) {
    (void) unused;
    if (!p_SetCursor || !p_CreateSystemCursor) return;
    static void *cursors[12];
    if (cursorId < 0 || cursorId > 11) cursorId = 0;
    if (!cursors[cursorId]) {
        cursors[cursorId] = p_CreateSystemCursor(cursorId);
    }
    if (cursors[cursorId]) {
        p_SetCursor(cursors[cursorId]);
    }
}

static void presentHook_(int winIndex) {
    (void) winIndex;
    if (!view_ren) return;
    if (presentCount_ < 3) {
        int cw, ch, cp;
        const shim_Uint8 *cpv = canvasPixels_canvas(0, &cw, &ch, &cp);
        fprintf(stderr, "[sdlview] present #%ld canvas=%dx%d pitch=%d ptr=%p\n",
                presentCount_, cw, ch, cp, (const void *) cpv);
    }
    presentCount_++;
    syncCanvasTexture_();
    p_SetRenderDrawColor(view_ren, 20, 20, 20, 255); /* dark clears the letterbox bars */
    p_RenderClear(view_ren);
    /* Letterbox instead of stretch: when the shim canvas aspect (the app window
       content) differs from the view window, preserve the former so content is
       never distorted (e.g. the in-window menubar is gone -> different aspect).
       Use a whole-number scale when it fits so the 2x Retina canvas maps to
       whole window pixels (every canvas pixel -> 1 window pixel), keeping text
       crisp even though SDL_SetWindowSize is a no-op in the shim (the canvas is
       fixed at the app window's logical size). A fractional scale (window smaller
       than the canvas) is only used if it cannot show at native resolution. */
    int rc = -1;
    if (view_tex) {
        SDL2_Rect dst = { 0, 0, 0, 0 };
        int ow = 0, oh = 0;
        if (p_GetRendererOutputSize) p_GetRendererOutputSize(view_ren, &ow, &oh);
        if (ow > 0 && oh > 0 && view_texW > 0 && view_texH > 0) {
            const float sx = (float) ow / (float) view_texW;
            const float sy = (float) oh / (float) view_texH;
            const float sFit = (sx < sy) ? sx : sy;
            const float s = (sFit >= 1.0f) ? (float) ((int) sFit) : sFit;
            dst.w = (int) (view_texW * s);
            dst.h = (int) (view_texH * s);
            dst.x = (ow - dst.w) / 2;
            dst.y = (oh - dst.h) / 2;
        }
        rc = p_RenderCopy(view_ren, view_tex, NULL, &dst);
    }
    int rp = p_RenderPresent(view_ren);
    if (presentCount_ < 3) {
        int ow = 0, oh = 0;
        if (p_GetRendererOutputSize) p_GetRendererOutputSize(view_ren, &ow, &oh);
        fprintf(stderr, "[sdlview] copy=%d present=%d err=%s viewOut=%dx%d tex=%dx%d\n",
                rc, rp, p_GetError(), ow, oh, view_texW, view_texH);
    }
}

static void pumpHook_(void) {
    if (!view_ren) return;
    SDL2_Event ev;
    while (p_PollEvent(&ev)) {
        switch (ev.type) {
            case EVT_QUIT: {
                /* clean shutdown of the whole app */
                typedef struct { unsigned type, timestamp; } q;
                q s = { 0x100, 0};
                SDL_PushEvent((void *)&s);
                break;
            }
            case EVT_MOUSEMOTION: {
                typedef struct { unsigned type, timestamp, windowID, which, state; int x, y, xrel, yrel; } ev_m;
                ev_m s;
                memset(&s, 0, sizeof(s));
                s.type = EVT_MOUSEMOTION;
                s.x = ev.motion.x;
                s.y = ev.motion.y;
                s.state = ev.motion.state;
                SDL_PushEvent((void *)&s);
                break;
            }
            case EVT_MOUSEBUTTONDOWN:
            case EVT_MOUSEBUTTONUP: {
                typedef struct { unsigned type, timestamp, windowID, which; unsigned char button, state, clicks, padding1; int x, y; } ev_b;
                ev_b s;
                memset(&s, 0, sizeof(s));
                s.type = ev.type;
                s.state = (ev.type == EVT_MOUSEBUTTONDOWN ? 1 : 0);
                s.button = ev.button.button;
                s.clicks = ev.button.clicks;
                /* viewer events are in window points; the app multiplies by
                   its pixelRatio itself */
                s.x = ev.button.x;
                s.y = ev.button.y;
                SDL_PushEvent((void *)&s);
                break;
            }
            case EVT_MOUSEWHEEL: {
                typedef struct { unsigned type, timestamp, windowID, which; int x, y; unsigned direction; float preciseX, preciseY; int mouseX, mouseY; } ev_w;
                ev_w s;
                memset(&s, 0, sizeof(s));
                s.type = EVT_MOUSEWHEEL;
                s.windowID = ev.wheel.windowID;
                s.which = ev.wheel.which;
                s.direction = ev.wheel.direction;
                s.preciseX = ev.wheel.preciseX;
                s.preciseY = ev.wheel.preciseY;
                s.mouseX = ev.wheel.mouseX;
                s.mouseY = ev.wheel.mouseY;
                s.x = ev.wheel.x;
                s.y = ev.wheel.y;
                /* The patched SDL2 (sdl2.26-macos-ios.diff) sends precise trackpad scrolls
                   on mouseID 0 (with fractional preciseX/Y) and marks imprecise notched
                   wheels with mouseID 1. lagrange needs the per-pixel flag set (and the
                   delta scaled to canvas pixels, see src/platform/macos.m) so document and
                   list widgets take the fractional-delta path instead of treating every
                   tick as a notched 3*lineHeight step — the shoulder-mounted overspeed. */
                if (s.which == 0) {
                    s.direction |= WHEEL_FLAG_PERPIXEL;
                    const float scale = (float) canvasScale_canvas();
                    s.x = (int) (s.preciseX * scale);
                    s.y = (int) (s.preciseY * scale);
                }
                SDL_PushEvent((void *)&s);
                break;
            }
            case EVT_WINDOWEVENT: {
                fprintf(stderr, "[sdlview] winev code=%u\n", ev.window.event);
                /* forward exposure/focus/size events so the app reacts to
                   window show/hide/resize/move on the real display */
                typedef struct { unsigned type, timestamp, windowID, event; unsigned char pad[52]; } ev_win;
                ev_win s;
                memset(&s, 0, sizeof(s));
                s.type = EVT_WINDOWEVENT;
                s.event = ev.window.event;
                SDL_PushEvent((void *)&s);
                break;
            }
            case EVT_KEYDOWN:
            case EVT_KEYUP: {
                /* struct sizes match SDL 2.26 SDL_KeyboardEvent layout */
                SDL2_KeyEvent s = ev.key;
                s.type = ev.type;
                s.state = (ev.type == EVT_KEYDOWN ? 1 : 0);
                SDL_PushEvent((void *)&s);
                break;
            }
            case EVT_TEXTINPUT: {
                SDL2_TextEvent s;
                memset(&s, 0, sizeof(s));
                s.type = EVT_TEXTINPUT;
                memcpy(s.text, ev.text.text, sizeof(s.text));
                SDL_PushEvent((void *)&s);
                break;
            }
            default:
                break;
        }
    }
}

/* label cleanup: see the key/button translation blocks above */

int initSdlView_view(int width, int height) {
    if (loadFuncs_()) return -1;
    if (p_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "[sdlview] SDL_Init failed\n");
        return -1;
    }
    view_win = p_CreateWindow("lagrange — canvas shim", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, width, height,
                              SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    if (!view_win) return -1;
    view_ren = p_CreateRenderer(view_win, -1, SDL_RENDERER_ACCELERATED); /* Metal on macOS: 2x backing on Retina */
    if (!view_ren) return -1;
    setPresentHook_canvas(presentHook_, NULL);
    setPumpHook_canvas(pumpHook_, NULL);
    setCursorHook_canvas(cursorHook_, NULL);
    return 0;
}

void deinitSdlView_view(void) {
    if (view_tex) p_DestroyTexture(view_tex);
    if (view_ren) p_DestroyRenderer(view_ren);
    if (view_win) p_DestroyWindow(view_win);
    view_tex = NULL;
    view_ren = NULL;
    view_win = NULL;
    if (p_Quit) p_Quit();
}
