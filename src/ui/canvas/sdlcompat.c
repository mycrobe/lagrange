/* sdlcompat.c — portable implementation of the SDL API surface used by lagrange.

   Canvas-host backend for Phase 0: no window system, no device open.
   All drawing rasterizes into an RGBA8888 software framebuffer. Per-OS backends
   (Aqua, Toolbox/QuickDraw) replace this file's bodies, not the call sites. */

#include "sdlcompat.h"

#include <the_Foundation/array.h>
#include <the_Foundation/mutex.h>

#include <time.h>
#include <errno.h>
#include <execinfo.h>
#include <sys/stat.h>

/* ------------------------------------------------------------ shared state */

typedef struct sdl_Filter_s {
    SDL_EventFilter filter;
    void *userdata;
} sdl_Filter;

typedef struct sdl_Timer_s {
    int    active;
    SDL_TimerID id;
    Uint32 interval;
    Uint32 dueMs;
    SDL_TimerCallback cb;
    void *param;
} sdl_Timer;

static iArray *g_events;        /* SDL_Event items */
static iArray *g_eventFilters;  /* sdl_Filter items */
static iArray *g_timers;        /* sdl_Timer items */
static iMutex *g_eventMutex;
static SDL_bool g_videoInit;
static SDL_bool g_audioInit;
static int g_nextWindowId;
static SDL_bool g_mainReady;
static Uint64 g_presentCount;

#define MAX_WINDOWS 16
static SDL_Window *g_windows[MAX_WINDOWS];
static int g_numWindows;

#define NUM_SCANCODES 512
static Uint8 g_keys[NUM_SCANCODES];
static SDL_Keymod g_modState;
static int g_mouseX, g_mouseY;
static Uint32 g_mouseButtons;
static char *g_clipboard;

#define MAX_HINTS 64
static struct { int id; char value[256]; } g_hints[MAX_HINTS];
static int g_numHints;

#define MAX_AUDIO_DEVICES 4
static int g_audioStatus[MAX_AUDIO_DEVICES];
static SDL_AudioSpec g_audioSpec[MAX_AUDIO_DEVICES];

static SDL_Cursor g_cursors[SDL_NUM_SYSTEM_CURSORS];
static SDL_Cursor *g_currentCursor;
static void (*g_cursorHook)(int cursorId, void *unused);
static int g_lastCursorId = -1;

static int g_userEventBase = -1;
static int g_canvasScale = 1;
static void (*presentHook_)(int);
static void (*pumpHook_)(void);
static int g_userEventsUsed;
static SDL_TimerID g_nextTimerId;

static Uint32 nowMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (Uint32) ((ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000L));
}

/* --------------------------------------------------------------- epilogue */

/* --------------------------------------------------------- init and errors */

/* SDL subsystem flag values (subset of SDL_init.h). */

/* SDL subsystem flag values (subset of SDL_init.h). */
#define SDL_INIT_TIMER  0x00000001u
#define SDL_INIT_AUDIO  0x00000010u
#define SDL_INIT_VIDEO  0x00000020u

static void ensureEventQueue(void) {
    if (!g_events) {
        g_events       = new_Array(sizeof(SDL_Event));
        g_eventFilters = new_Array(sizeof(sdl_Filter));
        g_timers       = new_Array(sizeof(sdl_Timer));
        g_eventMutex   = new_Mutex();
    }
}

int SDL_InitSubSystem(Uint32 flags) {
    const char *cs = getenv("CANVAS_SCALE");
    if (cs) g_canvasScale = atoi(cs);
    if (g_canvasScale < 1) g_canvasScale = 1;
    if (flags & SDL_INIT_VIDEO) g_videoInit = SDL_TRUE;
    if (flags & SDL_INIT_AUDIO) g_audioInit = SDL_TRUE;
    return 0;
}

int SDL_Init(Uint32 flags) {
    ensureEventQueue();
    return SDL_InitSubSystem(flags);
}

Uint32 SDL_WasInit(Uint32 flags) {
    Uint32 have = 0;
    if (g_videoInit) have |= SDL_INIT_VIDEO;
    if (g_audioInit) have |= SDL_INIT_AUDIO;
    return have & flags;
}

void SDL_Quit(void) {
    lock_Mutex(g_eventMutex);
    clear_Array(g_events);
    unlock_Mutex(g_eventMutex);
}

void SDL_SetMainReady(void) { g_mainReady = SDL_TRUE; }
void SDL_ClearError(void) {}
const char *SDL_GetError(void) { return "sdlcompat: no error info"; }

int SDL_SetHint(int name, const char *value) {
    for (int i = 0; i < g_numHints; i++) {
        if (g_hints[i].id == name) {
            snprintf(g_hints[i].value, sizeof(g_hints[i].value), "%s", value ? value : "");
            return 1;
        }
    }
    if (g_numHints < MAX_HINTS) {
        g_hints[g_numHints].id = name;
        snprintf(g_hints[g_numHints].value, sizeof(g_hints[g_numHints].value), "%s",
                 value ? value : "");
        g_numHints++;
        return 1;
    }
    return 0;
}

const char *SDL_GetHint(int name) {
    for (int i = 0; i < g_numHints; i++) {
        if (g_hints[i].id == name) return g_hints[i].value;
    }
    return NULL;
}

void SDL_EnableScreenSaver(void) {}
void SDL_free(void *p) { free(p); }
void *SDL_malloc(size_t n) { return malloc(n); }

Uint32 SDL_GetTicks(void)   { return nowMs(); }
Uint64 SDL_GetTicks64(void) { return (Uint64) nowMs(); }
Sint64 SDL_GetPerformanceCounter(void)   { return (Sint64) nowMs(); }
Sint64 SDL_GetPerformanceFrequency(void) { return 1000; }

void SDL_Delay(Uint32 ms) {
    struct timespec req = { ms / 1000, (long) (ms % 1000) * 1000000L };
    while (nanosleep(&req, &req) == -1 && errno == EINTR) {
        /* interrupted, finish remaining time */
    }
}

/* ------------------------------------------------------------------ timers */

static void pumpTimers(void) {
    const Uint32 now = nowMs();
    iArray *timers = g_timers; /* callbacks may add/remove */
    for (size_t i = 0; i < size_Array(timers) && i < size_Array(timers); /* size stable */) {
        sdl_Timer *t = at_Array(timers, i);
        if (t->active && t->dueMs <= now) {
            t->dueMs = now + t->interval;
            const Uint32 ret = t->cb(t->interval, t->param);
            if (ret == 0) {
                t->active = 0;
            } else {
                t->interval = ret;
            }
        }
        i++;
    }
}

SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_TimerCallback cb, void *param) {
    sdl_Timer t;
    memset(&t, 0, sizeof(t));
    t.id = ++g_nextTimerId;
    t.interval = interval;
    t.dueMs = nowMs() + interval;
    t.cb = cb;
    t.param = param;
    t.active = 1;
    pushBack_Array(g_timers, &t);
    return t.id;
}

void SDL_RemoveTimer(SDL_TimerID id) {
    if (!g_timers) return;
    for (size_t i = 0; i < size_Array(g_timers); i++) {
        sdl_Timer *t = at_Array(g_timers, i);
        if (t->id == id) {
            removeN_Array(g_timers, i, 1);
            return;
        }
    }
}

/* ------------------------------------------------------------------ events */

int SDL_PollEvent(SDL_Event *event) {
    ensureEventQueue();
    pumpTimers();
    if (pumpHook_) pumpHook_();
    lock_Mutex(g_eventMutex);
    if (isEmpty_Array(g_events)) {
        unlock_Mutex(g_eventMutex);
        return 0;
    }
    *event = *(SDL_Event *) front_Array(g_events);
    removeN_Array(g_events, 0, 1);
    unlock_Mutex(g_eventMutex);
    /* keep the emulated mouse state in sync for SDL_GetMouseState consumers */
    if (getenv("CANVAS_LOG_EVENTS") && event->type >= SDL_QUIT &&
        event->type <= SDL_MOUSEWHEEL) {
        fprintf(stderr, "[shim] poll type=0x%x", event->type);
        if (event->type == SDL_MOUSEMOTION)
            fprintf(stderr, " motion %d,%d", event->motion.x, event->motion.y);
        if (event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP)
            fprintf(stderr, " button %d at %d,%d", event->button.button, event->button.x,
                    event->button.y);
        if (event->type == SDL_MOUSEWHEEL)
            fprintf(stderr, " wheel %d,%d", event->wheel.x, event->wheel.y);
        fprintf(stderr, "\n");
    }
    switch (event->type) {
        case SDL_MOUSEMOTION:
            g_mouseX = event->motion.x;
            g_mouseY = event->motion.y;
            g_mouseButtons = event->motion.state;
            break;
        case SDL_MOUSEBUTTONDOWN:
            g_mouseX = event->button.x;
            g_mouseY = event->button.y;
            g_mouseButtons |= SDL_BUTTON(event->button.button);
            g_modState = event->key.keysym.mod;
            break;
        case SDL_MOUSEBUTTONUP:
            g_mouseX = event->button.x;
            g_mouseY = event->button.y;
            g_mouseButtons &= ~SDL_BUTTON(event->button.button);
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            g_modState = event->key.keysym.mod;
            g_keys[event->key.keysym.scancode & (NUM_SCANCODES - 1)] =
                (event->type == SDL_KEYDOWN) ? 1 : 0;
            break;
    }
    return 1;
}

int SDL_WaitEvent(SDL_Event *event) {
    while (!SDL_PollEvent(event)) {
        SDL_Delay(4);
    }
    return 1;
}

int SDL_WaitEventTimeout(SDL_Event *event, int timeoutMs) {
    const Uint64 deadline = SDL_GetTicks64() + (Uint64) (timeoutMs < 0 ? 100 : timeoutMs);
    while (SDL_GetTicks64() < deadline) {
        if (SDL_PollEvent(event)) return 1;
        SDL_Delay(1);
    }
    return SDL_PollEvent(event);
}

int SDL_PushEvent(SDL_Event *event) {
    ensureEventQueue();
    SDL_Event copy;
    memcpy(&copy, event, sizeof(copy));
    /* Synthetic/forwarded events may lack a window id; the widget kit needs it
       for hover tracking (e.g., document links only open with hover set). */
    if ((copy.type == SDL_MOUSEMOTION || copy.type == SDL_MOUSEBUTTONDOWN ||
         copy.type == SDL_MOUSEBUTTONUP || copy.type == SDL_WINDOWEVENT) &&
        copy.button.windowID == 0) {
        copy.button.windowID = g_nextWindowId; /* last created window */
    }
    lock_Mutex(g_eventMutex);
    pushBack_Array(g_events, &copy);
    unlock_Mutex(g_eventMutex);
    return 1;
}

int SDL_RegisterEvents(int numevents) {
    if (g_userEventBase < 0) g_userEventBase = SDL_USEREVENT + 1;
    const int first = g_userEventBase + g_userEventsUsed;
    g_userEventsUsed += numevents;
    return first;
}

int SDL_AddEventWatch(SDL_EventFilter filter, void *userdata) {
    ensureEventQueue();
    const sdl_Filter f = { filter, userdata };
    pushBack_Array(g_eventFilters, &f);
    return 1;
}

void SDL_DelEventWatch(SDL_EventFilter filter, void *userdata) {
    if (!g_eventFilters) return;
    for (size_t i = 0; i < size_Array(g_eventFilters); i++) {
        sdl_Filter *f = at_Array(g_eventFilters, i);
        if (f->filter == filter && f->userdata == userdata) {
            removeN_Array(g_eventFilters, i, 1);
            return;
        }
    }
}

void SDL_SetEventFilter(SDL_EventFilter filter, void *userdata) {
    (void) filter; (void) userdata;
}

Uint8 SDL_GetEventState(Uint32 type) {
    (void) type;
    return SDL_ENABLE;
}

int SDL_EventState(Uint32 type, int state) {
    (void) type; (void) state;
    return SDL_ENABLE;
}

void SDL_SetWindowsMessageHook(void *hook, void *userdata) {
    (void) hook; (void) userdata;
}

void SDL_PumpEvents(void) {
    SDL_Event e;
    (void) e;
}

/* ---------------------------------------------------------------- surfaces */

SDL_Palette *SDL_AllocPalette(int ncolors) {
    SDL_Palette *p = calloc(1, sizeof(SDL_Palette));
    p->ncolors = ncolors;
    p->colors = calloc((size_t) ncolors, sizeof(SDL_Color));
    for (int i = 0; i < ncolors; i++) {
        p->colors[i].r = p->colors[i].g = p->colors[i].b = (Uint8) i;
        p->colors[i].a = 255;
    }
    return p;
}

void SDL_FreePalette(SDL_Palette *p) {
    if (p) {
        free(p->colors);
        free(p);
    }
}

int SDL_SetPaletteColors(SDL_Palette *p, const SDL_Color *colors, int first, int ncolors) {
    if (!p) return -1;
    for (int i = 0; i < ncolors; i++) {
        p->colors[first + i] = colors[i];
    }
    return 0;
}

int SDL_SetSurfacePalette(SDL_Surface *s, SDL_Palette *p) {
    if (!s) return -1;
    /* like SDL, copy the palette contents; the surface owns its copy */
    if (s->format->palette) SDL_FreePalette(s->format->palette);
    SDL_Palette *cp = SDL_AllocPalette(p ? p->ncolors : 256);
    if (p && p->ncolors > 0) {
        SDL_Color *colors = p->colors;
        SDL_SetPaletteColors(cp, colors, 0, p->ncolors);
    }
    s->format->palette = cp;
    return 0;
}

SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int w, int h, int depth,
                                            Uint32 format) {
    (void) flags; (void) depth;
    SDL_Surface *s = calloc(1, sizeof(SDL_Surface));
    s->format = calloc(1, sizeof(SDL_PixelFormat));
    s->format->format = format;
    s->format->BitsPerPixel = (format == SDL_PIXELFORMAT_INDEX8) ? 8 : 32;
    s->format->BytesPerPixel = (format == SDL_PIXELFORMAT_INDEX8) ? 1 : 4;
    s->w = w;
    s->h = h;
    s->pitch = w * s->format->BytesPerPixel;
    s->pixels = calloc(1, (size_t) h * s->pitch);
    return s;
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int depth, Uint32 rmask,
                                  Uint32 gmask, Uint32 bmask, Uint32 amask) {
    (void) rmask; (void) gmask; (void) bmask; (void) amask;
    return SDL_CreateRGBSurfaceWithFormat(flags, w, h, depth, SDL_PIXELFORMAT_RGBA8888);
}

SDL_Surface *SDL_CreateRGBSurfaceWithFormatFrom(void *pixels, int w, int h, int depth,
                                                int pitch, Uint32 format) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, depth, format);
    free(s->pixels);
    s->pixels = pixels;
    s->pitch = pitch;
    s->flags = 0x00000100u; /* PREALLOC */
    return s;
}

static Uint32 getPixel32(const SDL_Surface *s, int x, int y) {
    const Uint8 *p = (const Uint8 *) s->pixels + y * s->pitch + x * s->format->BytesPerPixel;
    if (s->format->BytesPerPixel == 4) {
        return (Uint32) (p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
    }
    if (s->format->palette) {
        const SDL_Color *c = &s->format->palette->colors[p[0]];
        return (Uint32) (c->r | (c->g << 8) | (c->b << 16) | (c->a << 24));
    }
    return 0xFF000000u;
}

void SDL_FreeSurface(SDL_Surface *surface) {
    if (surface) {
        if (!(surface->flags & 0x00000100u)) free(surface->pixels);
        if (surface->format && surface->format->palette) SDL_FreePalette(surface->format->palette);
        free(surface->format);
        free(surface);
    }
}

SDL_Surface *SDL_ConvertSurfaceFormat(SDL_Surface *src, Uint32 fmt, Uint32 flags) {
    (void) flags;
    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, fmt);
    for (int y = 0; y < src->h; y++) {
        Uint8 *dp = (Uint8 *) dst->pixels + y * dst->pitch;
        for (int x = 0; x < src->w; x++) {
            const Uint32 px = getPixel32(src, x, y);
            dp[x * 4 + 0] = (Uint8) (px & 0xFF);
            dp[x * 4 + 1] = (Uint8) ((px >> 8) & 0xFF);
            dp[x * 4 + 2] = (Uint8) ((px >> 16) & 0xFF);
            dp[x * 4 + 3] = (Uint8) ((px >> 24) & 0xFF);
        }
    }
    return dst;
}

int SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcRectIn, SDL_Surface *dst,
                    const SDL_Rect *dstRectIn) {
    if (!src || !dst) return -1;
    if (dst->format->BytesPerPixel == 1 && src->format->BytesPerPixel == 1) {
        /* palette-to-palette: copy by palette index (glyph cache path) */
        SDL_Rect srcR = { 0, 0, src->w, src->h };
        SDL_Rect dstR = { 0, 0, dst->w, dst->h };
        if (srcRectIn) srcR = *srcRectIn;
        if (dstRectIn) dstR = *dstRectIn;
        if (dstR.x < 0) {
            srcR.x -= dstR.x;
            srcR.w += dstR.x;
            dstR.w += dstR.x;
            dstR.x = 0;
        }
        if (dstR.y < 0) {
            srcR.y -= dstR.y;
            srcR.h += dstR.y;
            dstR.h += dstR.y;
            dstR.y = 0;
        }
        if (dstR.x + dstR.w > dst->w) dstR.w = dst->w - dstR.x;
        if (dstR.y + dstR.h > dst->h) dstR.h = dst->h - dstR.y;
        for (int sy = 0; sy < srcR.h; sy++) {
            const int dy = dstR.y + sy;
            if (dy < 0 || dy >= dst->h) continue;
            for (int sx = 0; sx < srcR.w; sx++) {
                const int dx = dstR.x + sx;
                if (dx < 0 || dx >= dst->w) continue;
                ((Uint8 *) dst->pixels)[dy * dst->pitch + dx] =
                    ((const Uint8 *) src->pixels)[(srcR.y + sy) * src->pitch + srcR.x + sx];
            }
        }
        return 0;
    }
    if (dst->format->BytesPerPixel != 4) return -1;
    SDL_Rect srcR = { 0, 0, src->w, src->h };
    SDL_Rect dstR = { 0, 0, dst->w, dst->h };
    if (srcRectIn) srcR = *srcRectIn;
    if (dstRectIn) dstR = *dstRectIn;
    for (int sy = 0; sy < srcR.h; sy++) {
        const int dy = dstR.y + sy;
        if (dy < 0 || dy >= dst->h) continue;
        for (int sx = 0; sx < srcR.w; sx++) {
            const int dx = dstR.x + sx;
            if (dx < 0 || dx >= dst->w) continue;
            const Uint32 px = getPixel32(src, srcR.x + sx, srcR.y + sy);
            Uint8 *dp = (Uint8 *) dst->pixels + dy * dst->pitch + dx * 4;
            const Uint8 a = (Uint8) ((px >> 24) & 0xFF);
            if (a == 255) {
                dp[0] = (Uint8) (px & 0xFF);
                dp[1] = (Uint8) ((px >> 8) & 0xFF);
                dp[2] = (Uint8) ((px >> 16) & 0xFF);
                dp[3] = 255;
            } else if (a > 0) {
                dp[0] = (Uint8) ((dp[0] * (255 - a) + (px & 0xFF) * a) / 255);
                dp[1] = (Uint8) ((dp[1] * (255 - a) + ((px >> 8) & 0xFF) * a) / 255);
                dp[2] = (Uint8) ((dp[2] * (255 - a) + ((px >> 16) & 0xFF) * a) / 255);
                dp[3] = (Uint8) ((dp[3] * (255 - a) + a) / 255);
            }
        }
    }
    return 0;
}

int SDL_SetSurfaceBlendMode(SDL_Surface *s, Uint32 mode) {
    (void) s; (void) mode;
    return 0;
}

const char *SDL_GetPixelFormatName(Uint32 format) {
    (void) format;
    return "sdlcompat";
}

/* ----------------------------------------------------------------- windows */

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags) {
    ensureEventQueue();
    if (g_numWindows >= MAX_WINDOWS) return NULL;
    SDL_Window *win = calloc(1, sizeof(SDL_Window));
    win->id = (Uint32) ++g_nextWindowId;
    /* normalize CENTERED/UNDEFINED markers to sane logical positions */
    win->x = (x == (int) 0x2FFF0000u || x == (int) 0x1FFF0000u) ? 0 : x;
    win->y = (y == (int) 0x2FFF0000u || y == (int) 0x1FFF0000u) ? 0 : y;
    win->w = w;
    win->h = h;
    win->flags = SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS;
    snprintf(win->title, sizeof(win->title), "%s", title ? title : "lagrange");
    g_windows[g_numWindows++] = win;
    if (getenv("CANVAS_LOG_EVENTS")) {
        void *bt[8] = {0};
        int n = backtrace(bt, 8);
        char **sy = backtrace_symbols(bt, n);
        fprintf(stderr, "[shim] CreateWindow id=%u '%s' %dx%d\n", win->id, win->title, w, h);
        for (int k = 0; k < n && k < 6; k++) fprintf(stderr, "  bt[%d] %s\n", k, sy[k]);
        free(sy);
    }
    return win;
}

void SDL_DestroyWindow(SDL_Window *window) {
    if (!window) return;
    for (int i = 0; i < g_numWindows; i++) {
        if (g_windows[i] == window) {
            g_windows[i] = g_windows[--g_numWindows];
            break;
        }
    }
    if (window->renderer) {
        free(window->renderer->canvas);
        free(window->renderer);
    }
    free(window);
}

SDL_WindowID SDL_GetWindowID(SDL_Window *win) { return win ? win->id : 0; }

Uint32 SDL_GetWindowFlags(SDL_Window *win) {
    return win ? win->flags : 0;
}

void SDL_SetWindowTitle(SDL_Window *win, const char *title) {
    (void) win; (void) title;
}

void SDL_GetWindowSize(SDL_Window *win, int *w, int *h) {
    if (getenv("CANVAS_LOG_EVENTS") && win) {
        fprintf(stderr, "[shim] GetWindowSize -> %dx%d\n", win->w, win->h);
    }
    if (win) {
        if (w) *w = win->w;
        if (h) *h = win->h;
    } else {
        if (w) *w = 0;
        if (h) *h = 0;
    }
}

void SDL_SetWindowSize(SDL_Window *win, int w, int h) {
    (void) win; (void) w; (void) h;
}

void SDL_GetWindowPosition(SDL_Window *win, int *x, int *y) {
    if (x) *x = win ? win->x : 0;
    if (y) *y = win ? win->y : 0;
}

void SDL_SetWindowPosition(SDL_Window *win, int x, int y) {
    (void) win; (void) x; (void) y;
}

static void pushWindowEvent_(SDL_Window *win, Uint8 evType) {
    SDL_Event ev = { 0 };
    ev.window.type = SDL_WINDOWEVENT;
    ev.window.windowID = win ? win->id : 0;
    ev.window.event = evType;
    SDL_PushEvent(&ev);
}

void SDL_ShowWindow(SDL_Window *win) {
    if (win && (win->flags & SDL_WINDOW_HIDDEN)) {
        win->flags &= ~SDL_WINDOW_HIDDEN;
        /* like SDL: showing generates shown + exposed events; the app draws
           its widget tree only after exposure */
        pushWindowEvent_(win, SDL_WINDOWEVENT_SHOWN);
        pushWindowEvent_(win, SDL_WINDOWEVENT_EXPOSED);
    }
}
void SDL_HideWindow(SDL_Window *win)   { if (win) win->flags |= SDL_WINDOW_HIDDEN; }
void SDL_RaiseWindow(SDL_Window *win)  { (void) win; }
void SDL_MinimizeWindow(SDL_Window *win) { if (win) win->flags |= SDL_WINDOW_MINIMIZED; }
void SDL_MaximizeWindow(SDL_Window *win) { if (win) win->flags |= SDL_WINDOW_MAXIMIZED; }
void SDL_RestoreWindow(SDL_Window *win) { (void) win; }
void SDL_SetWindowResizable(SDL_Window *win, SDL_bool resizable) {
    (void) win; (void) resizable;
}
void SDL_SetWindowMinimumSize(SDL_Window *win, int w, int h) { (void) win; (void) w; (void) h; }
void SDL_SetWindowMaximumSize(SDL_Window *win, int w, int h) { (void) win; (void) w; (void) h; }
void SDL_SetWindowFullscreen(SDL_Window *win, Uint32 flags) { (void) win; (void) flags; }
void SDL_SetWindowIcon(SDL_Window *win, SDL_Surface *icon) { (void) win; (void) icon; }
int SDL_GetWindowDisplayIndex(SDL_Window *win) { (void) win; return 0; }

int SDL_GetWindowBordersSize(SDL_Window *win, int *top, int *left, int *bottom, int *right) {
    (void) win;
    if (top) *top = 0;
    if (left) *left = 0;
    if (bottom) *bottom = 0;
    if (right) *right = 0;
    return 0;
}

int SDL_GetNumVideoDisplays(void) { return 1; }

int SDL_GetDisplayUsableBounds(int displayIndex, SDL_Rect *r) {
    (void) displayIndex;
    if (r) { r->x = 0; r->y = 0; r->w = 1638; r->h = 980; }
    return 0;
}

int SDL_GetDisplayBounds(int di, SDL_Rect *r) { return SDL_GetDisplayUsableBounds(di, r); }

int SDL_GetDesktopDisplayMode(int di, SDL_DisplayMode *dm) {
    (void) di;
    memset(dm, 0, sizeof(*dm));
    dm->format = SDL_PIXELFORMAT_RGB888;
    dm->w = 1638;
    dm->h = 980;
    dm->refresh_rate = 60;
    return 0;
}

int SDL_GetDisplayMode(int di, int mi, SDL_DisplayMode *dm) {
    (void) mi;
    return SDL_GetDesktopDisplayMode(di, dm);
}

int SDL_GetDisplayDPI(int di, float *ddpi, float *hdpi, float *vdpi) {
    (void) di;
    if (ddpi) *ddpi = 96.0f * g_canvasScale;
    if (hdpi) *hdpi = 96.0f * g_canvasScale;
    if (vdpi) *vdpi = 96.0f * g_canvasScale;
    return 0;
}

const char *SDL_GetCurrentVideoDriver(void) { return "canvas"; }

SDL_bool SDL_GetWindowWMInfo(SDL_Window *win, void *info) {
    (void) win; (void) info;
    return SDL_FALSE;
}

/* --------------------------------------------------------------- renderer */

SDL_Renderer *SDL_CreateRenderer(SDL_Window *win, int index, Uint32 flags) {
    (void) index; (void) flags;
    if (!win) return NULL;
    if (win->renderer) return win->renderer;
    SDL_Renderer *d = calloc(1, sizeof(SDL_Renderer));
    d->w = win->w * g_canvasScale;
    d->h = win->h * g_canvasScale;
    d->canvas = calloc(1, (size_t) d->w * d->h * 4);
    d->canvasPitch = d->w * 4;
    d->a = 255;
    win->renderer = d;
    return d;
}

void SDL_DestroyRenderer(SDL_Renderer *r) { /* renderer/glue owned by window */
    (void) r;
}

int SDL_GetRendererOutputSize(SDL_Renderer *r, int *w, int *h) {
    if (getenv("CANVAS_LOG_EVENTS")) {
        fprintf(stderr, "[shim] GetRendererOutputSize -> %dx%d (target=%d)\n",
                r->target ? r->target->w : r->w, r->target ? r->target->h : r->h,
                !!r->target);
    }
    if (r->target) {
        if (w) *w = r->target->w;
        if (h) *h = r->target->h;
    } else {
        if (w) *w = r->w;
        if (h) *h = r->h;
    }
    return 0;
}

/* linfo */
SDL_bool SDL_GetRendererInfo(SDL_Renderer *r, SDL_RendererInfo *info) {
    memset(info, 0, sizeof(*info));
    info->name = "canvas";
    info->flags = SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE | SDL_RENDERER_PRESENTVSYNC;
    info->num_texture_formats = 0;
    (void) r;
    return SDL_TRUE;
}

static SDL_Texture *g_lastGlyphSource;
static struct { void *tex; int w, h; } g_bigTexs[8];
static SDL_Rect g_cacheWriteLog_[64];
static int g_dumpTextureIndex = -1;

SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, Uint32 format, int access, int w, int h) {
    (void) r;
    SDL_Texture *t = calloc(1, sizeof(SDL_Texture));
    t->w = w;
    t->h = h;
    t->format = (format == SDL_PIXELFORMAT_RGBA32) ? SDL_PIXELFORMAT_RGBA8888 : format;
    t->access = access;
    t->pitch = w * 4;
    t->pixels = calloc(1, (size_t) w * h * 4);
    t->r = t->g = t->b = t->a = 255;
    if (w >= 800 && h >= 1000) {
        for (int k = 0; k < 8; k++) {
            if (!g_bigTexs[k].tex) { g_bigTexs[k].tex = t; g_bigTexs[k].w = w; g_bigTexs[k].h = h; break; }
        }
    }
    if (getenv("CANVAS_LOG_TEXTURES") &&
        (t->w > 900 || t->format != SDL_PIXELFORMAT_RGBA8888)) {
        fprintf(stderr, "[shim] CreateTexture fmt=0x%x (RGBA8888=0x%x) access=%d %dx%d\n",
                t->format, SDL_PIXELFORMAT_RGBA8888, access, w, h);
    }
    return t;
}


void markTextureDump_canvas(int index) { g_dumpTextureIndex = index; }

SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *r, SDL_Surface *surface) {
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC,
                                       surface->w, surface->h);
    for (int y = 0; y < surface->h; y++) {
        Uint8 *dp = (Uint8 *) t->pixels + y * t->pitch;
        for (int x = 0; x < surface->w; x++) {
            const Uint32 px = getPixel32(surface, x, y);
            dp[x * 4 + 0] = (Uint8) (px & 0xFF);
            dp[x * 4 + 1] = (Uint8) ((px >> 8) & 0xFF);
            dp[x * 4 + 2] = (Uint8) ((px >> 16) & 0xFF);
            dp[x * 4 + 3] = (Uint8) ((px >> 24) & 0xFF);
        }
    }
    return t;
}

int SDL_QueryTexture(SDL_Texture *t, Uint32 *format, int *access, int *w, int *h) {
    if (!t) return -1;
    if (format) *format = t->format;
    if (access) *access = t->access;
    if (w) *w = t->w;
    if (h) *h = t->h;
    return 0;
}

void SDL_DestroyTexture(SDL_Texture *t) {
    if (t) {
        free(t->pixels);
        free(t);
    }
}

int SDL_SetRenderTarget(SDL_Renderer *r, SDL_Texture *t) {
    r->target = t;
    if (t && t->access == SDL_TEXTUREACCESS_STATIC) t->access = SDL_TEXTUREACCESS_TARGET;
    /* like SDL: switching the target resets the clip rect */
    r->clipEnabled = SDL_FALSE;
    return 0;
}

SDL_Texture *SDL_GetRenderTarget(SDL_Renderer *r) { return r->target; }

int SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 R, Uint8 G, Uint8 B, Uint8 A) {
    r->r = R;
    r->g = G;
    r->b = B;
    r->a = A;
    return 0;
}

int SDL_SetRenderDrawBlendMode(SDL_Renderer *r, Uint32 blendMode) {
    r->blendMode = blendMode;
    return 0;
}

static SDL_Rect effectiveClip(const SDL_Renderer *r) {
    SDL_Rect clip;
    if (r->clipEnabled) {
        clip = r->clip;
    } else {
        clip.x = clip.y = 0;
        clip.w = r->target ? r->target->w : r->w;
        clip.h = r->target ? r->target->h : r->h;
    }
    return clip;
}

static void blendFill(SDL_Renderer *r, const SDL_Rect *rectIn) {
    SDL_Rect rect = *rectIn;
    const SDL_Rect clip = effectiveClip(r);
    const int clampedX = iMax(rect.x, clip.x);
    const int clampedY = iMax(rect.y, clip.y);
    const int width = iMin(rect.x + rect.w, clip.x + clip.w) - clampedX;
    const int height = iMin(rect.y + rect.h, clip.y + clip.h) - clampedY;
    if (width <= 0 || height <= 0) return;
    rect.x = clampedX;
    rect.y = clampedY;
    int dw, dh, dpitch;
    Uint8 *dstPix = (Uint8 *) (r->target ? r->target->pixels : r->canvas);
    dpitch = r->target ? r->target->pitch : r->canvasPitch;
    dw = r->target ? r->target->w : r->w;
    dh = r->target ? r->target->h : r->h;
    (void) dw; (void) dh;
    const Uint32 blendMode = r->blendMode ? r->blendMode : SDL_BLENDMODE_NONE;
    for (int y = rect.y; y < rect.y + height; y++) {
        if (y < 0) continue;
        Uint8 *px = dstPix + y * dpitch + rect.x * 4;
        if (blendMode == SDL_BLENDMODE_NONE || r->a == 255) {
            for (int x = 0; x < width; x++) {
                px[0] = r->r;
                px[1] = r->g;
                px[2] = r->b;
                px[3] = r->a;
                px += 4;
            }
        } else {
            const Uint32 inv = 255 - r->a;
            for (int x = 0; x < width; x++) {
                px[0] = (Uint8) ((r->r * r->a + px[0] * inv) / 255);
                px[1] = (Uint8) ((r->g * r->a + px[1] * inv) / 255);
                px[2] = (Uint8) ((r->b * r->a + px[2] * inv) / 255);
                px[3] = (Uint8) ((r->a * r->a + px[3] * inv) / 255);
                px += 4;
            }
        }
    }
}

int SDL_RenderClear(SDL_Renderer *r) {
    SDL_Rect full;
    full.x = 0;
    full.y = 0;
    full.w = r->target ? r->target->w : r->w;
    full.h = r->target ? r->target->h : r->h;
    /* like SDL: use the current draw color and blend mode; alpha is honored
       (prerendered TextBufs clear to (255,255,255,0) and must stay transparent) */
    blendFill(r, &full);
    return 0;
}

int SDL_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rect) {
    if (!rect) return 0;
    blendFill(r, rect);
    return 0;
}

int SDL_RenderDrawRect(SDL_Renderer *r, const SDL_Rect *rect) {
    if (!rect) return 0;
    blendFill(r, &(SDL_Rect){ rect->x, rect->y, rect->w, 1 });
    blendFill(r, &(SDL_Rect){ rect->x, rect->y + rect->h - 1, rect->w, 1 });
    blendFill(r, &(SDL_Rect){ rect->x, rect->y, 1, rect->h });
    blendFill(r, &(SDL_Rect){ rect->x + rect->w - 1, rect->y, 1, rect->h });
    return 0;
}

int SDL_RenderDrawLine(SDL_Renderer *r, int x1, int y1, int x2, int y2) {
    if (y1 == y2 || x1 == x2) {
        SDL_Rect rr = { iMin(x1, x2), iMin(y1, y2), iAbs(x2 - x1) + 1, iAbs(y2 - y1) + 1 };
        blendFill(r, &rr);
        return 0;
    }
    int dx = iAbs(x2 - x1);
    int dy = iAbs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    while (1) {
        blendFill(r, &(SDL_Rect){ x1, y1, 1, 1 });
        if (x1 == x2 && y1 == y2) break;
        const int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx)  { err += dx; y1 += sy; }
    }
    return 0;
}

int SDL_RenderDrawLines(SDL_Renderer *r, const SDL_Point *points, int count) {
    for (int i = 1; i < count; i++) {
        SDL_RenderDrawLine(r, points[i - 1].x, points[i - 1].y, points[i].x, points[i].y);
    }
    return 0;
}

int SDL_RenderSetClipRect(SDL_Renderer *r, const SDL_Rect *rect) {
    if (rect) {
        r->clip = *rect;
        r->clipEnabled = SDL_TRUE;
    } else {
        r->clipEnabled = SDL_FALSE;
    }
    return 0;
}

int SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *texture, const SDL_Rect *srcIn,
                   const SDL_Rect *dstIn) {
    if (!texture || !texture->pixels) return 0;
    SDL_Rect src = { 0, 0, texture->w, texture->h };
    SDL_Rect dst = { 0, 0, texture->w, texture->h };
    if (srcIn) src = *srcIn;
    if (dstIn) dst = *dstIn;
    else if (srcIn) dst = *srcIn;
    if (getenv("CANVAS_LOG_TEXTURES") && (r->target || texture->w >= 800)) {
        const int tgtW = r->target ? r->target->w : r->w;
        const int tgtH = r->target ? r->target->h : r->h;
        fprintf(stderr, "[shim] RenderCopy tgt=%s%dx%d tex=%dx%d fmt=0x%x src=%d,%d %dx%d "
                        "dst=%d,%d %dx%d blend=%d a=%d\n",
                r->target ? "" : "canvas ",
                tgtW, tgtH, texture->w, texture->h, texture->format,
                src.x, src.y, src.w, src.h, dst.x, dst.y, dst.w, dst.h,
                texture->blendMode, texture->a);
    }
    if (dst.w <= 0 || dst.h <= 0) return 0;
    if (texture->w >= 800 && texture->h >= 1000) {
        if (g_lastGlyphSource != texture) {
            fprintf(stderr, "[shim] glyphcache tex %dx%d\n", texture->w, texture->h);
        }
        if (texture->format == SDL_PIXELFORMAT_RGBA4444 && texture->access == SDL_TEXTUREACCESS_TARGET) g_lastGlyphSource = texture;
        for (int k = 0; k < 64; k++) {
            if (g_cacheWriteLog_[k].w == 0) {
                g_cacheWriteLog_[k] = (SDL_Rect){ dst.x, dst.y, dst.w, dst.h };
                break;
            }
        }
    }

    const SDL_Rect clip = effectiveClip(r);
    const int srcPitch = texture->pitch;
    const Uint8 *texPix = (const Uint8 *) texture->pixels;
    const Uint32 blend = texture->blendMode ? texture->blendMode : SDL_BLENDMODE_NONE;
    for (int y = dst.y; y < dst.y + dst.h; y++) {
        if (y < clip.y || y >= clip.y + clip.h) continue;
        Uint8 *dp;
        if (r->target) {
            dp = r->target->pixels + y * r->target->pitch;
        } else {
            dp = r->canvas + y * r->canvasPitch;
        }
        const int sry = src.y + (int) (((y - dst.y) * src.h) / dst.h);
        for (int x = dst.x; x < dst.x + dst.w; x++) {
            if (x < clip.x || x >= clip.x + clip.w) continue;
            const int srx = src.x + (int) (((x - dst.x) * src.w) / dst.w);
            if (srx < 0 || srx >= texture->w || sry < 0 || sry >= texture->h) continue;
            const Uint8 *sp = texPix + sry * srcPitch + srx * 4;
            Uint8 *px = dp + x * 4;
            const Uint32 tR = (Uint32) (sp[0] * texture->r / 255);
            const Uint32 tG = (Uint32) (sp[1] * texture->g / 255);
            const Uint32 tB = (Uint32) (sp[2] * texture->b / 255);
            if (blend == SDL_BLENDMODE_NONE) {
                /* like SDL software: straight copy, color mod applied,
                   destination alpha replaced (alpha mod has no effect) */
                px[0] = (Uint8) tR;
                px[1] = (Uint8) tG;
                px[2] = (Uint8) tB;
                px[3] = sp[3];
                continue;
            }
            const Uint32 sA = (Uint32) (sp[3] * texture->a / 255);
            if (sA == 0) continue;
            const Uint32 inv = 255 - sA;
            px[0] = (Uint8) ((tR * sA + px[0] * inv) / 255);
            px[1] = (Uint8) ((tG * sA + px[1] * inv) / 255);
            px[2] = (Uint8) ((tB * sA + px[2] * inv) / 255);
            px[3] = (Uint8) ((sA * 255 + px[3] * inv) / 255);
        }
    }
    return 0;
}

int SDL_RenderPresent(SDL_Renderer *r) {
    (void) r;
    g_presentCount++;
    if (presentHook_) presentHook_(0);
    return 0;
}

int SDL_SetTextureColorMod(SDL_Texture *t, Uint8 r, Uint8 g, Uint8 b) {
    if (!t) return -1;
    t->r = r; t->g = g; t->b = b;
    return 0;
}

int SDL_SetTextureAlphaMod(SDL_Texture *t, Uint8 a) {
    if (!t) return -1;
    t->a = a;
    return 0;
}
int SDL_SetTextureBlendMode(SDL_Texture *t, Uint32 blendMode) {
    if (!t) return -1;
    t->blendMode = blendMode;
    return 0;
}
int SDL_SetTextureScaleMode(SDL_Texture *t, Uint32 scaleMode) {
    if (!t) return -1;
    t->scaleMode = scaleMode;
    return 0;
}

/* ------------------------------------------------------------------- audio */

int SDL_OpenAudioDevice(const char *device, int iscapture, const SDL_AudioSpec *want,
                        SDL_AudioSpec *got, int allowChange) {
    (void) device; (void) allowChange;
    ensureEventQueue();
    for (int i = 0; i < MAX_AUDIO_DEVICES; i++) {
        if (!g_audioStatus[i]) {
            g_audioSpec[i] = *want;
            g_audioSpec[i].callback = want->callback;
            g_audioSpec[i].userdata = want->userdata;
            g_audioSpec[i].silence = (want->format & 0x0008) ? 0x80 : 0;
            if (got) *got = g_audioSpec[i];
            g_audioStatus[i] = SDL_AUDIO_PAUSED;
            return (SDL_AudioDeviceID) (i + 1);
        }
    }
    return 0;
}

void SDL_CloseAudioDevice(SDL_AudioDeviceID dev) {
    if (dev >= 1 && dev <= MAX_AUDIO_DEVICES) {
        g_audioStatus[dev - 1] = 0;
    }
}

void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause) {
    (void) pause;
    (void) dev;
}

int SDL_GetAudioDeviceStatus(SDL_AudioDeviceID dev) {
    if (dev >= 1 && dev <= MAX_AUDIO_DEVICES) {
        return g_audioStatus[dev - 1] ? SDL_AUDIO_PAUSED : 0;
    }
    return 0;
}

/* ------------------------------------------------------------------- rwops */

typedef struct sdl_RWSource_s {
    FILE *file;
    void *mem;
    size_t memSize;
    size_t memPos;
    int isMem;
} sdl_RWSource;

static Sint64 rwSize(SDL_RWops *ctx) {
    const sdl_RWSource *s = (sdl_RWSource *) ctx->fp;
    if (s->isMem) return (Sint64) s->memSize;
    const long pos = ftell(s->file);
    fseek(s->file, 0, SEEK_END);
    const long end = ftell(s->file);
    fseek(s->file, pos, SEEK_SET);
    return (Sint64) end;
}

static int rwRead(SDL_RWops *ctx, void *ptr, int size, int maxnum) {
    sdl_RWSource *s = (sdl_RWSource *) ctx->fp;
    if (s->isMem) {
        size_t want = (size_t) size * (size_t) maxnum;
        if (want > s->memSize - s->memPos) {
            want = s->memSize - s->memPos;
        }
        memcpy(ptr, (const Uint8 *) s->mem + s->memPos, want);
        s->memPos += want;
        return (int) (want / (size_t) size);
    }
    return (int) fread(ptr, (size_t) size, (size_t) maxnum, s->file);
}

static int rwSeek(SDL_RWops *ctx, int offset, int whence) {
    sdl_RWSource *s = (sdl_RWSource *) ctx->fp;
    if (s->isMem) {
        switch (whence) {
            case 0: s->memPos = (size_t) offset; break;
            case 1: s->memPos += (size_t) offset; break;
            case 2: s->memPos = s->memSize + (size_t) offset; break;
        }
        return (int) s->memPos;
    }
    return fseek(s->file, offset, whence);
}

static int rwClose(SDL_RWops *ctx) {
    if (ctx) {
        sdl_RWSource *s = (sdl_RWSource *) ctx->fp;
        if (!s->isMem && s->file) fclose(s->file);
        free(s);
        free(ctx);
    }
    return 0;
}

static SDL_bool canvas_fileExists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? SDL_TRUE : SDL_FALSE;
}

const char *execPath_compat(void);

static const char *rwFindFile(const char *file) {
    if (canvas_fileExists(file)) return file;
    /* same directory as the running executable (relative mode) */
    return NULL;
}

SDL_RWops *SDL_RWFromFile(const char *file, const char *mode) {
    SDL_RWops *ops = calloc(1, sizeof(SDL_RWops));
    sdl_RWSource *src = calloc(1, sizeof(sdl_RWSource));
    src->file = fopen(file, mode);
    if (!src->file) {
        SDL_RWops *mem = NULL;
        (void) mem;
        free(src);
        free(ops);
        return NULL;
    }
    ops->fp = src;
    ops->size = rwSize;
    ops->read = rwRead;
    ops->seek = rwSeek;
    ops->close = rwClose;
    return ops;
}

SDL_RWops *SDL_RWFromMem(void *mem, int size) {
    SDL_RWops *ops = calloc(1, sizeof(SDL_RWops));
    sdl_RWSource *src = calloc(1, sizeof(sdl_RWSource));
    src->mem = mem;
    src->memSize = (size_t) size;
    src->isMem = 1;
    ops->fp = src;
    ops->size = rwSize;
    ops->read = rwRead;
    ops->seek = rwSeek;
    ops->close = rwClose;
    return ops;
}

Sint64 SDL_RWsize(SDL_RWops *ctx) { return ctx->size(ctx); }
int SDL_RWread(SDL_RWops *ctx, void *ptr, int size, int maxnum) {
    return ctx->read(ctx, ptr, size, maxnum);
}
int SDL_RWseek(SDL_RWops *ctx, int offset, int whence) {
    return ctx->seek(ctx, offset, whence);
}
int SDL_RWwrite(SDL_RWops *ctx, const void *ptr, int size, int num) {
    (void) ctx; (void) ptr;
    return size * num;
}
int SDL_RWclose(SDL_RWops *ctx) { return ctx->close(ctx); }

/* --------------------------------------------------------------- keyboard */

const Uint8 *SDL_GetKeyboardState(int *numkeys) {
    if (numkeys) *numkeys = NUM_SCANCODES;
    return g_keys;
}

SDL_Keymod SDL_GetModState(void) { return g_modState; }
void SDL_SetModState(SDL_Keymod mods) { g_modState = mods; }

SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode sc) {
    return (SDL_Keycode) sc; /* compatibility value; headless keys are synthetic */
}

SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode key) {
    return (SDL_Scancode) key;
}

const char *SDL_GetKeyName(SDL_Keycode key) {
    static char name[2];
    if (key >= ' ' && key < 127) {
        name[0] = (char) key;
        name[1] = 0;
        return name;
    }
    return "";
}

/* ---------------------------------------------------------- gamepad/joystick */

int SDL_NumJoysticks(void) { return 0; }
SDL_bool SDL_IsGameController(int index) { (void) index; return SDL_FALSE; }
const char *SDL_GameControllerNameForIndex(int index) { (void) index; return NULL; }
const char *SDL_GameControllerGetStringForButton(int button) { (void) button; return ""; }
SDL_GameController *SDL_GameControllerOpen(int index) { (void) index; return NULL; }
void SDL_GameControllerClose(SDL_GameController *gc) { (void) gc; }
SDL_bool SDL_GameControllerGetAttached(SDL_GameController *gc) { (void) gc; return SDL_FALSE; }
void SDL_GameControllerEventState(int state) { (void) state; }
int SDL_GameControllerAddMapping(const char *mapping) { (void) mapping; return 1; }

SDL_JoystickGUID SDL_JoystickGetDeviceGUID(int device) {
    SDL_JoystickGUID g;
    memset(&g, 0, sizeof(g));
    (void) device;
    return g;
}

void SDL_JoystickGetGUIDString(SDL_JoystickGUID guid, char *buf, int size) {
    (void) guid;
    memset(buf, 0, size);
}

SDL_JoystickGUID g_fakeGuid_placeholder; /* nothing else needs this */

void SDL_CaptureMouse(SDL_bool enabled) { (void) enabled; }

Uint32 SDL_GetMouseState(int *x, int *y) {
    if (getenv("CANVAS_LOG_EVENTS")) {
        fprintf(stderr, "[shim] GetMouseState -> %d,%d buttons=%x\n", g_mouseX, g_mouseY,
                g_mouseButtons);
    }
    if (x) *x = g_mouseX;
    if (y) *y = g_mouseY;
    return g_mouseButtons;
}

Uint32 SDL_GetGlobalMouseState(int *x, int *y) { return SDL_GetMouseState(x, y); }

void SDL_StartTextInput(void) {}
void SDL_StopTextInput(void) {}
void SDL_SetTextInputRect(const SDL_Rect *rect) { (void) rect; }

int SDL_SetWindowHitTest(SDL_Window *window, SDL_HitTest cb, void *cb_data) {
    if (window) window->hitTest = cb;
    (void) cb_data;
    return 0;
}

/* -------------------------------------------------------------- clipboard */

int SDL_HasClipboardText(void) { return g_clipboard && g_clipboard[0]; }

char *SDL_GetClipboardText(void) {
    return strdup(g_clipboard ? g_clipboard : "");
}

int SDL_SetClipboardText(const char *text) {
    free(g_clipboard);
    g_clipboard = strdup(text ? text : "");
    return 0;
}

int SDL_OpenURL(const char *url) {
    (void) url;
    return 0;
}

/* ----------------------------------------------------------------- cursors */

SDL_Cursor *SDL_CreateSystemCursor(int id) {
    g_cursors[id].id = id;
    return &g_cursors[id];
}

void SDL_FreeCursor(SDL_Cursor *cursor) { (void) cursor; }
void SDL_SetCursor(SDL_Cursor *cursor) {
    g_currentCursor = cursor;
    const int id = cursor ? cursor->id : SDL_SYSTEM_CURSOR_ARROW;
    if (id != g_lastCursorId && g_cursorHook) {
        g_cursorHook(id, NULL);
        g_lastCursorId = id;
    }
}

void setCursorHook_canvas(void (*cb)(int cursorId, void *unused), void *unused) {
    (void) unused;
    g_cursorHook = cb;
}

/* --------------------------------------------------------- base/paths+focus */

char *SDL_GetBasePath(void) {
    /* headless host: current working directory, always with a trailing separator */
    return strdup("./");
}

char *SDL_GetPrefPath(const char *org, const char *app) {
    (void) org; (void) app;
    /* Canvas builds get their own saved-state directory, isolated from the
       real user config; override with CANVAS_PREF_DIR. */
    const char *dir = getenv("CANVAS_PREF_DIR");
    if (!dir || !*dir) {
        dir = "/tmp/kilo/canvas-home";
    }
    /* expand a leading ~ */
    char buf[1024];
    if (dir[0] == '~') {
        const char *home = getenv("HOME");
        snprintf(buf, sizeof(buf), "%s%s", home ? home : "", dir + 1);
    }
    else {
        snprintf(buf, sizeof(buf), "%s", dir);
    }
    /* recursive mkdir so the app can drop files right away */
    {
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s", buf);
        const size_t len = strlen(tmp);
        for (size_t i = 1; i < len; i++) {
            if (tmp[i] == '/') {
                tmp[i] = 0;
                mkdir(tmp, 0755);
                tmp[i] = '/';
            }
        }
        mkdir(tmp, 0755);
    }
    /* caller expects a trailing separator */
    if (buf[strlen(buf) - 1] != '/') {
        strncat(buf, "/", sizeof(buf) - strlen(buf) - 1);
    }
    return strdup(buf);
}

void SDL_SetWindowInputFocus(SDL_Window *win) { (void) win; }

/* --------------------------------------------------- harness introspection */

Uint64 presentCount_canvas(void) { return g_presentCount; }

void dumpTexture_canvas(const char *prefix, const char *path) {
    SDL_Texture *t = g_lastGlyphSource;
    if (t) {
        char buf[512];
        for (int k = 0; k < 8; k++) {
            if (!g_bigTexs[k].tex) break;
            snprintf(buf, sizeof(buf), "%s.%d.raw", path, k);
            FILE *tf = fopen(buf, "wb");
            SDL_Texture *tx = g_bigTexs[k].tex;
            if (tx) {
                fprintf(tf, "P6\n%d %d\n255\n", tx->w, tx->h);
                for (int y = 0; y < tx->h; y++) {
                    const Uint8 *row = tx->pixels + (size_t) y * tx->pitch;
                    for (int x = 0; x < tx->w; x++)
                        fwrite(row + x * 4, 1, 3, tf);
                }
                fclose(tf);
                fprintf(stderr, "[shim] dumped tex[%d] %dx%d\n", k, tx->w, tx->h);
            }
        }
    }
    FILE *f = fopen(path, "wb");
    if (!f || !t) {
        fprintf(stderr, "[shim] dump: f=%p t=%p (last=%p)\n", (void *) f, (void *) t,
                (void *) g_lastGlyphSource);
        if (f) fclose(f);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", t->w, t->h);
    for (int y = 0; y < t->h; y++) {
        const Uint8 *row = t->pixels + (size_t) y * t->pitch;
        for (int x = 0; x < t->w; x++)
            fwrite(row + x * 4, 1, 3, f); /* RGBA layout → RGB */
    }
    fclose(f);
    fprintf(stderr, "[shim] dumped tex %dx%d\n", t->w, t->h);
}

/* Write the presentable canvas of a renderer as an RGBA PPM. Returns 0 on success. */
void captureRender_canvas(SDL_Renderer *r, const char *path) {
    if (!r || !r->canvas) return;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", r->w, r->h);
    for (int y = 0; y < r->h; y++) {
        const Uint8 *row = r->canvas + (size_t) y * r->canvasPitch;
        for (int x = 0; x < r->w; x++) {
            fwrite(row + x * 4, 1, 3, f); /* RGB = first three bytes */
        }
    }
    fclose(f);
}

int windowCount_canvas(void) { return g_numWindows; }

SDL_Renderer *rendererOfWindow_canvas(int index) {
    if (index < 0 || index >= g_numWindows) return NULL;
    return g_windows[index]->renderer;
}

/* -------------------------------------------------- canvas host introspection */

void setPresentHook_canvas(void (*cb)(int winIndex), void *unused) {
    presentHook_ = cb;
    (void) unused;
}

void setPumpHook_canvas(void (*cb)(void), void *unused) {
    pumpHook_ = cb;
    (void) unused;
}

const Uint8 *canvasPixels_canvas(int winIndex, int *w, int *h, int *pitch) {
    SDL_Renderer *r = rendererOfWindow_canvas(winIndex);
    if (!r) return NULL;
    if (w) *w = r->w;
    if (h) *h = r->h;
    if (pitch) *pitch = r->canvasPitch;
    return r->canvas;
}
