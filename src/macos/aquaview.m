/* aquaview.m — Aqua (Tiger/Leopard PPC) AppKit canvas-host backend.

   The T-tier window backend for the Phase 0/2 canvas seam: one programmatic
   NSWindow + NSView per shim window (no nibs), each displaying the portable
   shim's software framebuffer (sdlcompat.c) of the corresponding shim window.
   Native NSEvents are translated into shim SDL events through the same _canvas
   hook table sdlview.c uses and tagged with the originating window's shim id,
   so the widget kit routes input to the right (possibly extra) window.  10.4-era
   AppKit only: manual retain/release, old-style NSEvent constants, no
   blocks/properties, respondsToSelector: guards for anything newer.

   Multi-window: the shim tells us when a window is created/destroyed/titled and
   which window each present belongs to (setWindow{created,destroyed,title}Hook_
   + presentHook_, whose int arg is the presenting window's *windowID*).  We key
   the native-window table (gAqTable) by that shim windowID, so internal shim
   reordering never desyncs it.  Popup/menu windows (SDL_WINDOW_POPUP_MENU /
   SDL_WINDOW_SKIP_TASKBAR) carry no native window -- they are in-canvas in this
   host.  The first native window (stored at gAqTable[0]) is the primary window;
   closing it quits the app; closing an extra one closes that window.

   It deliberately does NOT define iPlatformApple or use macos.m: this host is
   menu-native (canvasmenu_impl_SDL.m registers the main menu) but keeps the
   shim's render/event/model behavior, exactly like the SDL2 canvas host.

   Copyright 2026 the lagrange port engine; distributed under the BSD-2-Clause
   license of the core it drives (see LICENSE.md). */

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <ApplicationServices/ApplicationServices.h>

#include "sdlcompat.h"   /* SDL event structs + the _canvas hook exports */
#include "aquaview.h"
#include "app.h"
#include "stb_image.h"   /* decodes the icon PNG robustly (impl lives in window.c) */

/* canvasScale_canvas() is implemented in sdlcompat.c but has no header prototype
   (it is a viewer-only helper, like sdlview.c uses). */
int canvasScale_canvas(void);

/* Forward declarations: the window table holds AquaCanvasView*, and the app
   delegate is referenced before its class is defined. */
@class AquaCanvasView;
@class AquaAppDelegate;
static AquaAppDelegate *gDelegate_;

/* ---------------------------------------------------- shim hook exports --- */
/* Declared in sdlcompat.h; the bodies live in sdlcompat.c (compiled into this
   app).  Only the subset the Aqua view needs is used here. */

/* ------------------------------------------------------------- globals --- */

#define kMaxAquaWindows 16  /* must be >= the shim's MAX_WINDOWS (16) */
typedef struct {
    Uint32 id;              /* shim SDL_WindowID this native window mirrors */
    NSWindow *win;
    AquaCanvasView *view;
} AquaWindowEntry;
static AquaWindowEntry gAqTable[kMaxAquaWindows];
static int            gAqNum;

/* Find the native window's table slot by shim windowID (present/title/destroy
   all key by windowID so internal shim reordering never desyncs it).  Returns
   -1 for popup/menu windows, which carry no native window. */
static int aqFind_(Uint32 id) {
    for (int i = 0; i < gAqNum; i++) {
        if (gAqTable[i].id == id) return i;
    }
    return -1;
}

static BOOL isPopupWindowFlags_(Uint32 flags) {
    return (flags & (SDL_WINDOW_POPUP_MENU | SDL_WINDOW_SKIP_TASKBAR)) != 0;
}

/* Idle-CPU fix (Tiger 2026-09-11): the widget kit is stepped from an AppKit
   timer (see AquaWidgetTimer), but on a single-core G4 a free-running 60Hz
   tick that always redraws burns ~25% CPU at idle.  Two backend-only levers:
   (1) dirty-gate the blit -- do not issue setNeedsDisplay + the synchronous
   displayIfNeeded (a full-canvas CGContextDrawImage) unless the framebuffer
   actually changed since the last present; (2) coalesce the tick -- a
   self-rescheduling one-shot that parks to a slow interval once the scene has
   been static for a couple of frames, and returns to 60Hz the moment a real
   change or a native input event needs prompt service.  Everything is driven
   from present output, so a re-arming widget ticker that redraws identical
   pixels collapses to near-zero dispatching cost instead of 55 blits/s. */
#define kFastInterval_Aqua  (1.0 / 60.0)
#define kIdleInterval_Aqua  0.25
#define kIdleStreak_Aqua    2

static iBool    gPresentedThisTick_;  /* did step_App present a frame just now? */
static iBool    gLastPresentDirty_;   /* ...and did its pixels differ from the prior frame? */
static int      gIdleStreak_;         /* consecutive static frames (drives parking) */
static iBool    gNeedFast_;           /* a native event wants prompt processing */
static iBool    gInTick_;             /* re-entrancy guard for the timer re-arm */
static id       gTimerTarget_;        /* owns the tick:; set by runAquaMainLoop */
static NSTimer *gPendingTimer_;       /* the one-shot tick currently scheduled */
static iBool    gShimReady_;          /* hooks installed (guard focus/close during teardown) */

static void invalidateAquaTick_(void) {
    if (gPendingTimer_) {
        [gPendingTimer_ invalidate];
        [gPendingTimer_ release];
        gPendingTimer_ = nil;
    }
}

/* Schedule a one-shot AppKit tick.  The timer is owned here (+1) and the run
   loop additionally retains it while scheduled; invalidate/release cleans up. */
static void armAquaTick_(NSTimeInterval dt) {
    invalidateAquaTick_();
    if (!gTimerTarget_) return;
    NSTimer *t = [NSTimer timerWithTimeInterval:dt
                                          target:gTimerTarget_
                                        selector:@selector(tick:)
                                        userInfo:nil
                                         repeats:NO];
    /* timerWithTimeInterval: returns an autoreleased (+0) timer; the run loop
       owns it via addTimer:.  Take our own +1 so gPendingTimer_ is a true
       reference and the matching release in invalidateAquaTick_ / tick: is
       balanced.  Without the retain, releasing a +0 autoreleased timer is a
       double free (seen on-device as a malloc double-free flood). */
    gPendingTimer_ = [t retain];
    [[NSRunLoop currentRunLoop] addTimer:t forMode:NSDefaultRunLoopMode];
 }

/* A native event arrived (mouse/key/scroll/URL/quit).  It must be processed
   promptly even if the tick is parked, so ask for a fast tick immediately.
   Re-entrancy: a native event can be delivered from inside step_App via the
   pump hook (pumpHookAqua_ sends the queued NSEvent while we are in the tick),
   so only touch the pending timer outside the tick body. */
static void wakeAquaTick_(void) {
    gNeedFast_ = iTrue;
    gIdleStreak_ = 0;
    if (!gInTick_ && gTimerTarget_) {
        armAquaTick_(kFastInterval_Aqua);
    }
}

/* ------------------------------ pixel blit + drawRect -------------------- */

/* The target window index for a native event: we route mouse/key/scroll by the
   window that originated the event.  Where the dragging/segmented translation
   always used the single window, here each view tags its own shim window id. */

/* AQUA_MOUSEDBG: log every translated mouse event with both the raw view
   geometry and the letterbox-mapped canvas point, so an on-device trace shows
   whether the shim events carry the coordinates the widget kit expects. */
static int mouseDbg_(void) {
    static int enabled = -1;
    if (enabled < 0) enabled = getenv("AQUA_MOUSEDBG") ? 1 : 0;
    return enabled;
}

/* key-translation helpers, defined below (used by the view's keyDown/keyUp and
   the AquaWindow key-equivalent forwarder). */
static Uint32 keySymFromEvent_(NSEvent *e, int *scancode);
static Uint16 keyModFromFlags_(unsigned long f);

/* NSWindow subclass: forwards Cmd-key equivalents to the widget kit, keeps the
   shim window id for event tagging, and reports key/close so the widget kit can
   switch the active window / close an extra window.  macOS calls
   performKeyEquivalent: (a window) before keyDown:; the native menu bar renders
   its items but its NSMenuItem key equivalents do not fire under this host's
   [NSApp run] + timer model, so a Cmd-combo would otherwise be swallowed (beep)
   and never reach the view's keyDown:.  Translating it into an SDL key event
   lets the widget kit's own shortcut table (Cmd+L/T/C/V/X …) dispatch it. */
@interface AquaWindow : NSWindow {
@public
    Uint32 shimWindowID_;
}
- (void)setShimWindowID:(Uint32)wid;
- (void)postWindowEvent:(Uint8)event;
- (BOOL)performKeyEquivalent:(NSEvent *)event;
@end

@implementation AquaWindow

- (void)setShimWindowID:(Uint32)wid { shimWindowID_ = wid; }

- (void)postWindowEvent:(Uint8)event {
    if (!gShimReady_ || shimWindowID_ == 0) return;
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.window.type = SDL_WINDOWEVENT;
    ev.window.windowID = shimWindowID_;
    ev.window.event = event;
    SDL_PushEvent(&ev);
    wakeAquaTick_();
}

/* Report key/focus transitions so the widget kit routes input/activation to
   this window (extra windows set the active window on focus-gained). */
- (void)becomeKeyWindow {
    [super becomeKeyWindow];
    [self postWindowEvent:SDL_WINDOWEVENT_FOCUS_GAINED];
}
- (void)resignKeyWindow {
    [super resignKeyWindow];
    [self postWindowEvent:SDL_WINDOWEVENT_FOCUS_LOST];
}

- (BOOL)performKeyEquivalent:(NSEvent *)event {
    /* Let the native menu's key equivalents win (Quit, Preferences, menu items
       with shortcut labels).  Under this host's [NSApp run] + timer model the
       menu is not consulted automatically for key presses, so ask it here. */
    wakeAquaTick_();
    NSMenu *mainMenu = [[NSApplication sharedApplication] mainMenu];
    if (mainMenu && [mainMenu performKeyEquivalent:event]) {
        return YES;
    }
    /* Not a menu key equivalent: forward Cmd-combos to the widget kit so its
       own shortcut table (Cmd+L/T/C/V/X …) dispatches them. */
    if ([event type] == NSKeyDown) {
        int sc = 0;
        const Uint32 sym = keySymFromEvent_(event, &sc);
        const Uint16 mods = keyModFromFlags_([event modifierFlags]);
        if ((mods & KMOD_GUI) && sym != SDLK_UNKNOWN) {
            SDL_Event ev;
            memset(&ev, 0, sizeof(ev));
            ev.key.type = SDL_KEYDOWN;
            ev.key.state = SDL_PRESSED;
            ev.key.repeat = [event isARepeat] ? 1 : 0;
            ev.key.keysym.scancode = (SDL_Scancode) sc;
            ev.key.keysym.sym = (SDL_Keycode) sym;
            ev.key.keysym.mod = mods;
            ev.key.windowID = shimWindowID_;
            SDL_PushEvent(&ev);
            SDL_Event up;
            memset(&up, 0, sizeof(up));
            up.key.type = SDL_KEYUP;
            up.key.state = SDL_RELEASED;
            up.key.keysym.scancode = (SDL_Scancode) sc;
            up.key.keysym.sym = (SDL_Keycode) sym;
            up.key.keysym.mod = mods;
            up.key.windowID = shimWindowID_;
            SDL_PushEvent(&up);
            return YES; /* consumed: the widget kit's shortcut table owns it */
        }
    }
    return NO;
}

@end

@interface AquaCanvasView : NSView {
@public
    unsigned long buttons_; /* SDL_BUTTON() bitmask, tracked from mouse events */
    Uint32   shimWindowID_;   /* the shim window this view presents */
    int      canvasW_, canvasH_; /* shim renderer pixel size for this window */
    int      canvasPitch_;
    const Uint8 *canvasPx_;      /* current framebuffer (valid until next present) */
    Uint8   *lastPresented_;     /* copy of the buffer as last blitted */
    int      lastPresentedBytes_;
}
- (void)setShimWindowID:(Uint32)wid;
- (Uint32)shimWindowID;
- (void)setCanvasPixels:(const Uint8 *)px width:(int)w height:(int)h pitch:(int)pitch;
- (void)pushMouse:(NSEvent *)event down:(BOOL)down;
@end

@implementation AquaCanvasView

- (void)setShimWindowID:(Uint32)wid { shimWindowID_ = wid; }
- (Uint32)shimWindowID { return shimWindowID_; }

- (void)setCanvasPixels:(const Uint8 *)px width:(int)w height:(int)h pitch:(int)pitch {
    canvasPx_ = px;
    canvasW_ = (w < 0) ? 0 : w;
    canvasH_ = (h < 0) ? 0 : h;
    canvasPitch_ = pitch;
}

- (BOOL)isFlipped { return NO; }
- (BOOL)acceptsFirstResponder { return YES; }
/* Tiger swallows the first click on a window that is not *key* as an
   activation click and never delivers it to the content view (acceptsFirstMouse
   defaults to NO).  Under the [NSApp run] + timer model the window's key status
   is often not established before the user's first click, so every click can be
   consumed as an activation click -- no mouse interaction at all.  Opt the view
   in so the first click also gets through. */
- (BOOL)acceptsFirstMouse:(NSEvent *)event { (void) event; return YES; }

/* The shim canvas (canvasW_ x canvasH_) is letterboxed into the view: this is
   the rect it occupies (in the view's bottom-left origin -- the view is not
   flipped) together with the integer/fractional scale.  Letterbox instead of
   stretch, preserving the canvas aspect so content is never distorted (mirror
   the SDL2 host's behaviour); whole-number scale when it fits, fractional only
   if the window is too small.  The draw path AND the mouse-coordinate mapping
   both use this so they can never diverge. */
- (double)canvasScaleInView {
    const int cw = canvasW_, ch = canvasH_;
    if (cw <= 0 || ch <= 0) return 1.0;
    const double sx = (double) NSWidth([self bounds]) / (double) cw;
    const double sy = (double) NSHeight([self bounds]) / (double) ch;
    const double sf = (sx < sy) ? sx : sy;
    return (sf >= 1.0) ? (double) ((int) sf) : sf;
}

- (NSRect)canvasRectInView {
    const double s = [self canvasScaleInView];
    const double dw = (double) canvasW_ * s, dh = (double) canvasH_ * s;
    const double dx = (NSWidth([self bounds]) - dw) / 2.0;
    const double dy = (NSHeight([self bounds]) - dh) / 2.0; /* bottom-left origin */
    return NSMakeRect(dx, dy, dw, dh);
}

- (void)drawCanvasInto:(NSRect)bounds {
    if (!canvasPx_ || canvasW_ <= 0 || canvasH_ <= 0) {
        [[NSColor colorWithCalibratedRed:0.08 green:0.08 blue:0.08 alpha:1.0] setFill];
        NSRectFill(bounds);
        return;
    }
    const int cw = canvasW_, ch = canvasH_;
    NSRect dst = [self canvasRectInView];

    /* dark clears the letterbox bars */
    [[NSColor colorWithCalibratedRed:0.08 green:0.08 blue:0.08 alpha:1.0] setFill];
    NSRectFill(bounds);

    /* Draw the shim's RGBA8888 buffer straight to the view context as a CGImage
       with an explicit byte order.  32-bit big-endian + alpha-skip-last is the
       deterministic memory order for R,G,B,A on PPC (the rep approach left the
       byte order to the rep's endianness, a real risk on a big-endian target). */
    CGDataProviderRef prov = CGDataProviderCreateWithData(
        NULL, canvasPx_, (size_t) ch * canvasPitch_, NULL);
    CGImageRef img = CGImageCreate(cw, ch, 8, 32, canvasPitch_,
                                   CGColorSpaceCreateDeviceRGB(),
                                   (kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big),
                                   prov, NULL, false, kCGRenderingIntentDefault);
    if (img) {
        CGContextRef ctx = (CGContextRef) [[NSGraphicsContext currentContext] graphicsPort];
        CGContextSaveGState(ctx);
        CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
        CGContextDrawImage(ctx, *(CGRect *) &dst, img);
        CGContextRestoreGState(ctx);
        CGImageRelease(img);
    }
    CGDataProviderRelease(prov);
}

- (void)drawRect:(NSRect)r { (void) r; [self drawCanvasInto:[self bounds]]; }

/* Map an NSEvent location to a canvas pixel.  The view is bottom-left origin
   (unflipped) and the shim canvas is letterboxed into it (canvasRectInView), so
   invert that transform: subtract the letterbox origin, divide by the scale,
   and flip the vertical axis (canvas rows are top-left/y-down like SDL).  At a
   1:1 default window/view size this reduces to the simple y flip. */
- (NSPoint)sdlPoint:(NSEvent *)event {
    NSPoint vp = [self convertPoint:[event locationInWindow] fromView:nil];
    const int cw = canvasW_, ch = canvasH_;
    NSRect r = [self canvasRectInView];
    if (cw <= 0 || ch <= 0 || NSWidth(r) <= 0.0 || NSHeight(r) <= 0.0) {
        return NSMakePoint(vp.x, NSHeight([self bounds]) - vp.y);
    }
    const double s = [self canvasScaleInView];
    const double cx = (vp.x - NSMinX(r)) / s;
    const double cy = (NSMaxY(r) - vp.y) / s; /* canvas top-left/y-down */
    return NSMakePoint(cx, cy);
}

static NSEvent *gPopupEvent_; /* most recent mouse-down NSEvent (context menus) */

void setAquaPopupEvent_Aqua(void *event) {
    if (gPopupEvent_) { [gPopupEvent_ release]; gPopupEvent_ = nil; }
    if (event) { gPopupEvent_ = [(NSEvent *) event retain]; }
}

void *currentAquaPopupEvent_Aqua(void) { return gPopupEvent_; }

void *aquaMainView_Aqua(void) { return (gAqNum > 0) ? gAqTable[0].view : nil; }

- (void)pushMouse:(NSEvent *)event down:(BOOL)down {
    wakeAquaTick_();
    if (down) {
        setAquaPopupEvent_Aqua(event); /* remember for a possible context menu */
    }
    NSPoint p = [self sdlPoint:event];
    Uint8 button = SDL_BUTTON_LEFT;
    int btn = [event buttonNumber];
    if (btn == 1) button = SDL_BUTTON_RIGHT;
    else if (btn == 2) button = SDL_BUTTON_MIDDLE;
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.button.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    ev.button.button = button;
    ev.button.state = down ? SDL_PRESSED : SDL_RELEASED;
    ev.button.clicks = 1;
    ev.button.windowID = shimWindowID_;
    ev.button.x = (Sint32) p.x;
    ev.button.y = (Sint32) p.y;
    SDL_PushEvent(&ev);
    if (mouseDbg_()) {
        NSRect r = [self canvasRectInView];
        fprintf(stderr, "[aqua] mouse %s at=%d,%d wid=%u view=%dx%d canvas=%dx%d "
                        "rect=%.0f,%.0f %.0fx%.0f s=%.2f\n",
                down ? "down" : "up", (int) p.x, (int) p.y, (unsigned) shimWindowID_,
                (int) NSWidth([self bounds]), (int) NSHeight([self bounds]),
                canvasW_, canvasH_, r.origin.x, r.origin.y, r.size.width, r.size.height,
                [self canvasScaleInView]);
    }
    if (down) {
        buttons_ |= SDL_BUTTON(button);
    }
    else {
        buttons_ &= ~(SDL_BUTTON(button));
    }
}

- (void)mouseDown:(NSEvent *)e  { [self pushMouse:e down:YES]; }
- (void)mouseUp:(NSEvent *)e    { [self pushMouse:e down:NO]; }
- (void)rightMouseDown:(NSEvent *)e { [self pushMouse:e down:YES]; }
- (void)rightMouseUp:(NSEvent *)e   { [self pushMouse:e down:NO]; }
- (void)otherMouseDown:(NSEvent *)e { [self pushMouse:e down:YES]; }
- (void)otherMouseUp:(NSEvent *)e   { [self pushMouse:e down:NO]; }

- (void)mouseMoved:(NSEvent *)e {
    wakeAquaTick_();
    NSPoint p = [self sdlPoint:e];
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.motion.type = SDL_MOUSEMOTION;
    ev.motion.windowID = shimWindowID_;
    ev.motion.x = (Sint32) p.x;
    ev.motion.y = (Sint32) p.y;
    ev.motion.state = (Uint32) buttons_;
    SDL_PushEvent(&ev);
    if (mouseDbg_()) {
        fprintf(stderr, "[aqua] mouse move at=%d,%d wid=%u view=%dx%d canvas=%dx%d\n",
                (int) p.x, (int) p.y, (unsigned) shimWindowID_,
                (int) NSWidth([self bounds]), (int) NSHeight([self bounds]),
                canvasW_, canvasH_);
    }
}

- (void)mouseDragged:(NSEvent *)e { [self mouseMoved:e]; }
- (void)scrollWheel:(NSEvent *)e {
    wakeAquaTick_();
    NSPoint p = [self sdlPoint:e];
    const int scale = canvasScale_canvas();
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.wheel.type = SDL_MOUSEWHEEL;
    ev.wheel.windowID = shimWindowID_;
    ev.wheel.which = 1;            /* notched/discrete wheel (not precise trackpad) */
    ev.wheel.direction = 0;
    ev.wheel.x = (Sint32) ([e deltaX] * scale);
    ev.wheel.y = (Sint32) ([e deltaY] * scale);
    ev.wheel.preciseX = (float) [e deltaX];
    ev.wheel.preciseY = (float) [e deltaY];
    ev.wheel.mouseX = (Sint32) p.x;
    ev.wheel.mouseY = (Sint32) p.y;
    SDL_PushEvent(&ev);
}

/* ---- key translation ---- */

static Uint32 keySymFromEvent_(NSEvent *e, int *scancode) {
    const Uint16 kc = (Uint16) [e keyCode];
    switch (kc) {
        case 0x24: *scancode = SDL_SCANCODE_RETURN; return SDLK_RETURN;
        case 0x30: *scancode = SDL_SCANCODE_TAB;    return SDLK_TAB;
        case 0x31: *scancode = SDL_SCANCODE_SPACE;  return SDLK_SPACE;
        case 0x33: *scancode = SDL_SCANCODE_BACKSPACE; return SDLK_BACKSPACE;
        case 0x35: *scancode = SDL_SCANCODE_ESCAPE; return SDLK_ESCAPE;
        case 0x75: *scancode = SDL_SCANCODE_DELETE; return SDLK_DELETE;
        case 0x7B: *scancode = SDL_SCANCODE_LEFT;  return SDLK_LEFT;
        case 0x7C: *scancode = SDL_SCANCODE_RIGHT; return SDLK_RIGHT;
        case 0x7D: *scancode = SDL_SCANCODE_DOWN;  return SDLK_DOWN;
        case 0x7E: *scancode = SDL_SCANCODE_UP;    return SDLK_UP;
        case 0x73: *scancode = SDL_SCANCODE_HOME;  return SDLK_HOME;
        case 0x77: *scancode = SDL_SCANCODE_END;   return SDLK_END;
        case 0x74: *scancode = SDL_SCANCODE_PAGEUP; return SDLK_PAGEUP;
        case 0x79: *scancode = SDL_SCANCODE_PAGEDOWN; return SDLK_PAGEDOWN;
    }
    if (kc >= 0x7A && kc <= 0x85) {
        *scancode = SDL_SCANCODE_F1 + (kc - 0x7A);
        return SDLK_F1 + (kc - 0x7A);
    }
    /* fall back to the printable character (a-z, 0-9, punctuation) */
    NSString *s = [e charactersIgnoringModifiers];
    if ([s length] == 1) {
        *scancode = SDL_SCANCODE_UNKNOWN;
        return (Uint32) [s characterAtIndex:0];
    }
    *scancode = SDL_SCANCODE_UNKNOWN;
    return SDLK_UNKNOWN;
}

static Uint16 keyModFromFlags_(unsigned long f) {
    Uint16 m = 0;
    if (f & NSShiftKeyMask)     m |= KMOD_SHIFT;
    if (f & NSControlKeyMask)   m |= KMOD_CTRL;
    if (f & NSAlternateKeyMask) m |= KMOD_ALT;
    if (f & NSCommandKeyMask)   m |= KMOD_GUI;
    if (f & NSAlphaShiftKeyMask) m |= KMOD_CAPS;
    if (f & NSNumericPadKeyMask) m |= KMOD_NUM;
    return m;
}

- (void)keyDown:(NSEvent *)e {
    wakeAquaTick_();
    int sc = 0;
    const Uint32 sym = keySymFromEvent_(e, &sc);
    const Uint16 mods = keyModFromFlags_([e modifierFlags]);
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.key.type = SDL_KEYDOWN;
    ev.key.state = SDL_PRESSED;
    ev.key.repeat = [e isARepeat] ? 1 : 0;
    ev.key.windowID = shimWindowID_;
    ev.key.keysym.scancode = (SDL_Scancode) sc;
    ev.key.keysym.sym = (SDL_Keycode) sym;
    ev.key.keysym.mod = mods;
    SDL_PushEvent(&ev);
    /* text input (skip command/ctrl-key equivalents so menu/keyword combos
       do not also insert into a focused field). */
    NSString *chars = [e characters];
    if (chars && [chars length] > 0 && !(mods & (KMOD_GUI | KMOD_CTRL))) {
        SDL_Event tx;
        memset(&tx, 0, sizeof(tx));
        tx.text.type = SDL_TEXTINPUT;
        tx.text.windowID = shimWindowID_;
        strncpy(tx.text.text, [chars UTF8String], sizeof(tx.text.text) - 1);
        tx.text.text[sizeof(tx.text.text) - 1] = '\0';
        SDL_PushEvent(&tx);
    }
}

- (void)keyUp:(NSEvent *)e {
    wakeAquaTick_();
    int sc = 0;
    const Uint32 sym = keySymFromEvent_(e, &sc);
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.key.type = SDL_KEYUP;
    ev.key.state = SDL_RELEASED;
    ev.key.windowID = shimWindowID_;
    ev.key.keysym.scancode = (SDL_Scancode) sc;
    ev.key.keysym.sym = (SDL_Keycode) sym;
    ev.key.keysym.mod = keyModFromFlags_([e modifierFlags]);
    SDL_PushEvent(&ev);
}

@end

/* ------------------------------------------------- AQUA_MOUSEDBG self-test --- */
/* CGEvent posts from an SSH session are dropped by Tiger's WindowServer, so a
   headless diagnostic cannot drive the app with a synthetic system cursor.
   AQUA_SELFTEST="cx,cy" instead feeds synthesized NSEvents straight into the
   view's own mouse methods (AQUA_SELFTEST_IMMEDIATE=1 puts the click in the
   same callback as the move), which exercises the exact sdlPoint -> SDL_PushEvent
   -> widget-kit path the real cursor would. */
static void sendSyntheticMouse_(AquaCanvasView *view, int type, int cx, int cy) {
    const NSRect r = [view canvasRectInView];
    const double s = [view canvasScaleInView];
    const NSPoint vp = NSMakePoint(NSMinX(r) + (double) cx * s, NSMaxY(r) - (double) cy * s);
    const NSPoint wl = [view convertPoint:vp toView:nil];
    NSEvent *e = [NSEvent mouseEventWithType:(NSEventType) type
                                    location:wl
                               modifierFlags:0
                                   timestamp:[NSDate timeIntervalSinceReferenceDate]
                                windowNumber:[[view window] windowNumber]
                                      context:nil
                                  eventNumber:0
                                   clickCount:1
                                     pressure:1.0];
    if (type == NSMouseMoved) {
        [view mouseMoved:e];
    }
    else if (type == NSLeftMouseDown) {
        [view mouseDown:e];
    }
    else if (type == NSLeftMouseUp) {
        [view mouseUp:e];
    }
}

static int gSelfTestX_, gSelfTestY_;

@interface AquaSelfTest : NSObject
- (void)move:(NSTimer *)timer;
- (void)click:(NSTimer *)timer;
@end

@implementation AquaSelfTest
- (void)move:(NSTimer *)timer {
    (void) timer;
    AquaCanvasView *view = (gAqNum > 0) ? gAqTable[0].view : nil;
    if (!view) return;
    fprintf(stderr, "[aqua] selftest move at %d,%d\n", gSelfTestX_, gSelfTestY_);
    sendSyntheticMouse_(view, NSMouseMoved, gSelfTestX_, gSelfTestY_);
    if (getenv("AQUA_SELFTEST_IMMEDIATE")) {
        [self click:nil];
    }
}
- (void)click:(NSTimer *)timer {
    (void) timer;
    AquaCanvasView *view = (gAqNum > 0) ? gAqTable[0].view : nil;
    if (!view) return;
    fprintf(stderr, "[aqua] selftest click at %d,%d\n", gSelfTestX_, gSelfTestY_);
    sendSyntheticMouse_(view, NSLeftMouseDown, gSelfTestX_, gSelfTestY_);
    sendSyntheticMouse_(view, NSLeftMouseUp, gSelfTestX_, gSelfTestY_);
}
@end

/* ------------------------------------------------------------------ hooks --- */
static void presentHookAqua_(int winID) {
    const int idx = aqFind_((Uint32) winID);
    if (idx < 0) return; /* popup/menu windows have no native window here */
    AquaCanvasView *view = gAqTable[idx].view;
    NSWindow *win = gAqTable[idx].win;
    if (!view || !win) return;
    int cw, ch, pitch;
    const Uint8 *px = canvasPixelsByWindowId_canvas((Uint32) winID, &cw, &ch, &pitch);
    if (!px || cw <= 0 || ch <= 0) {
        gPresentedThisTick_ = iFalse;
        return;
    }
    [view setCanvasPixels:px width:cw height:ch pitch:pitch];
    gPresentedThisTick_ = iTrue;
    /* Only issue a redraw if the frame actually differs from the previous one.
       The widget kit may call SDL_RenderPresent every tick (a re-arming ticker
       keeps the refresh gate open even at rest), but on a static page the pixels
       are identical -- a full setNeedsDisplay + synchronous displayIfNeeded
       (an entire CGContextDrawImage of the canvas) is pure waste on a G4. */
    const int bytes = ch * pitch;
    const iBool dirty = !view->lastPresented_ || view->lastPresentedBytes_ != bytes ||
                        memcmp(view->lastPresented_, px, (size_t) bytes) != 0;
    gLastPresentDirty_ = dirty;
    if (!dirty) {
        return;
    }
    if (!view->lastPresented_ || view->lastPresentedBytes_ != bytes) {
        free(view->lastPresented_);
        view->lastPresented_ = (Uint8 *) malloc((size_t) bytes);
        view->lastPresentedBytes_ = bytes;
    }
    memcpy(view->lastPresented_, px, (size_t) bytes);
    [view setNeedsDisplay:YES];
    [view displayIfNeeded];
}

static void windowCreatedAqua_(SDL_Window *win) {
    /* Popup/dropdown windows are in-canvas in this host (the native menu code
       handles top/context menus); they get no native window, so the widget kit
       neither shows nor blits them as separate windows. */
    if (isPopupWindowFlags_(win->flags)) return;
    if (gAqNum >= kMaxAquaWindows) return;
    int w = win->w, h = win->h;
    if (w <= 0) w = 900;
    if (h <= 0) h = 560;

    NSWindow *aq = [[AquaWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, w, h)
                  styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                              NSMiniaturizableWindowMask | NSResizableWindowMask)
                    backing:NSBackingStoreBuffered defer:NO];
    [aq setTitle:@"Lagrange"];
    [aq setAcceptsMouseMovedEvents:YES];
    [aq setDelegate:gDelegate_];
    /* Do NOT let AppKit deallocate the window when the user closes it via the
       title-bar button (releasedWhenClosed defaults to YES).  The backend holds
       the owning reference and only releases it in windowDestroyedAqua_, when the
       widget kit destroys the shim window.  With releasedWhenClosed:YES a
       user-clicked close frees the window first; the later destroy hook then
       messages a freed object (objc holds a dangling pointer in gAqTable)->crash. */
    [aq setReleasedWhenClosed:NO];
    [(AquaWindow *) aq setShimWindowID:win->id];

    AquaCanvasView *view = [[AquaCanvasView alloc] initWithFrame:NSMakeRect(0, 0, w, h)];
    [view setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [view setShimWindowID:win->id];
    [aq setContentView:view];
    [aq makeFirstResponder:view];

    const int idx = gAqNum;
    gAqTable[idx].id = win->id;
    gAqTable[idx].win = aq;
    gAqTable[idx].view = view;
    gAqNum++;

    /* The first native window is the app's primary window. */
    if (idx == 0) {
        [NSApp activateIgnoringOtherApps:YES];
        [aq display];
    }
    [aq makeKeyAndOrderFront:nil];
}

static void windowDestroyedAqua_(Uint32 winID) {
    const int idx = aqFind_(winID);
    if (idx < 0) return;
    if (gAqTable[idx].win) {
        /* The backend owns a +1 reference (from alloc in windowCreatedAqua_); this
           is the one balanced release.  Use orderOut:, NOT close: -- close: fires
           the windowWillClose: delegate, which re-enters our AquaAppDelegate and
           for the primary window calls quitRequested -> [NSApp terminate:] from
           right here (we are inside a timer-tick frame that is tearing down the
           window model), which double-releases the window (EXC_BAD_ACCESS).  We
           are already destroying the shim window, so just take it off screen and
           drop our reference; AppKit releases its own refs when the window goes. */
        [gAqTable[idx].win orderOut:nil];
        [gAqTable[idx].win release];
    }
    if (gAqTable[idx].view) {
        if (gAqTable[idx].view->lastPresented_) {
            free(gAqTable[idx].view->lastPresented_);
            gAqTable[idx].view->lastPresented_ = NULL;
            gAqTable[idx].view->lastPresentedBytes_ = 0;
        }
        [gAqTable[idx].view release];
    }
    /* remove the entry; swap-last-into-place keeps the table compact, and always
       clear the moved-out slot so a stale aqFind_ for a freed id never re-hits a
       released window/view. */
    const int last = gAqNum - 1;
    if (idx != last) {
        gAqTable[idx] = gAqTable[last];
    }
    memset(&gAqTable[last], 0, sizeof(gAqTable[last]));
    gAqNum--;
}

static void windowTitleAqua_(Uint32 winID, const char *title) {
    const int idx = aqFind_(winID);
    if (idx < 0 || !gAqTable[idx].win) return;
    [gAqTable[idx].win setTitle:title ? [NSString stringWithUTF8String:title] : @"Lagrange"];
}

static void pumpHookAqua_(void) {
    if (gAqNum == 0) return;
    for (;;) {
        NSEvent *ev = [NSApp nextEventMatchingMask:NSAnyEventMask
                                         untilDate:[NSDate dateWithTimeIntervalSinceNow:0.0]
                                            inMode:NSDefaultRunLoopMode
                                           dequeue:YES];
        if (!ev) break;
        if ([ev type] == NSApplicationDefined) continue; /* queued quit handled elsewhere */
        [NSApp sendEvent:ev];
    }
}

static void cursorHookAqua_(int cursorId, void *unused) {
    (void) unused;
    NSCursor *cur = nil;
    switch (cursorId) {
        case SDL_SYSTEM_CURSOR_ARROW:    cur = [NSCursor arrowCursor]; break;
        case SDL_SYSTEM_CURSOR_IBEAM:    cur = [NSCursor IBeamCursor]; break;
        case SDL_SYSTEM_CURSOR_CROSSHAIR:cur = [NSCursor crosshairCursor]; break;
        case SDL_SYSTEM_CURSOR_HAND:     cur = [NSCursor pointingHandCursor]; break;
        case SDL_SYSTEM_CURSOR_SIZENS:
            if ([NSCursor respondsToSelector:@selector(resizeUpDownCursor)])
                cur = [NSCursor resizeUpDownCursor];
            break;
        case SDL_SYSTEM_CURSOR_SIZEWE:
            if ([NSCursor respondsToSelector:@selector(resizeLeftRightCursor)])
                cur = [NSCursor resizeLeftRightCursor];
            break;
        case SDL_SYSTEM_CURSOR_WAIT:
        case SDL_SYSTEM_CURSOR_WAITARROW: cur = [NSCursor arrowCursor]; break;
        default:                         cur = [NSCursor arrowCursor]; break;
    }
    [cur set];
}

/* ------------------------------------------------------------ app glue --- */

@interface AquaAppDelegate : NSObject
- (void)quitRequested;
@end

@implementation AquaAppDelegate
/* Closing the *primary* window (shim index 0) quits the app -- the established
   single-window behaviour.  Closing an *extra* window is forwarded to the widget
   kit as SDL_WINDOWEVENT_CLOSE so it can close the window model (which destroys
   the shim window and, via windowDestroyedAqua_, tears down the NSWindow). */
- (void)windowWillClose:(NSNotification *)n {
    NSWindow *win = [n object];
    if (!win || ![win isKindOfClass:[AquaWindow class]]) return;
    const Uint32 wid = ((AquaWindow *) win)->shimWindowID_;
    if (wid == 0) return;
    const int idx = aqFind_(wid);
    if (idx == 0) {
        [self quitRequested];  /* the primary window: quit the app */
    } else {
        wakeAquaTick_();
        SDL_Event ev;
        memset(&ev, 0, sizeof(ev));
        ev.window.type = SDL_WINDOWEVENT;
        ev.window.windowID = wid;
        ev.window.event = SDL_WINDOWEVENT_CLOSE;
        SDL_PushEvent(&ev);  /* widget kit closes the extra window model */
    }
}
- (void)quitRequested {
    wakeAquaTick_();
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.quit.type = SDL_QUIT;
    SDL_PushEvent(&ev);
    /* This host runs the widget kit inside [NSApp run], so closing the window
       must also unwind AppKit's main loop or the process never exits.  The
       widget kit's SDL_QUIT is handled by the nest tick. */
    [[NSApplication sharedApplication] terminate:nil];
}
@end

/* Drives the widget kit (step_App) from a repeating timer so it can coexist
   with a running [NSApp run] on the main thread. */
@interface AquaWidgetTimer : NSObject
- (void)tick:(NSTimer *)timer;
@end

@implementation AquaWidgetTimer

- (void)tick:(NSTimer *)timer {
    (void) timer;
    if (gPendingTimer_) {           /* the fired one-shot is spent */
        [gPendingTimer_ release];
        gPendingTimer_ = nil;
    }
    gInTick_ = iTrue;
    /* Each tick must be an autorelease island: on Tiger AppKit does not provide
       an automatic pool for every run-loop event, and step_App drives the widget
       kit (event dispatch + render + present + autolayout) which autoreleases a
       steady stream of Foundation/AppKit objects.  Without a per-frame pool they
       hit _NSAutoreleaseNoPool and never release. */
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    gPresentedThisTick_ = iFalse;
    gLastPresentDirty_ = iFalse;
    step_App(postedEventsOnly_AppEventMode);
    /* The widget kit may have quit (isRunning = false) on SDL_QUIT / a "quit"
       command; unwind AppKit's loop so the process actually exits. */
    if (!isAppRunning()) {
        [[NSApplication sharedApplication] terminate:nil];
        [pool drain];
        gInTick_ = iFalse;
        return;
    }
    /* Coalesce: park to the slow interval once the scene has been static for a
       couple of frames (either nothing was presented, or it matched the prior
       frame), and stay at 60Hz while frames are actually changing or a native
       event needs prompt service.  This is the backend half of the idle-CPU fix;
       the widget kit may still be ticking a re-armed animation, but if it draws
       identical pixels we stop paying for it.  (The tick always calls step_App
       even when parked, so queued SDL events -- network completions, posted
       commands -- are still drained at the slow rate and any resulting change
       immediately bumps us back to 60Hz.) */
    if (gPresentedThisTick_ && gLastPresentDirty_) {
        gIdleStreak_ = 0;         /* a real rendering change: stay fast */
    }
    else if (!gNeedFast_) {
        gIdleStreak_++;
    }
    const NSTimeInterval next =
        (gNeedFast_ || gIdleStreak_ < kIdleStreak_Aqua) ? kFastInterval_Aqua
                                                        : kIdleInterval_Aqua;
    gNeedFast_ = iFalse;
    [pool drain];
    gInTick_ = iFalse;
    armAquaTick_(next);
}
@end

/* ------------------------------------------------------------------ URLs --- */
/* A Finder `open gemini://host/path` (or `open /tmp/L4.app gemini://...`) sends
   a kAEGetURL AppleEvent to the app.  The real macos.m host registers the same
   handler; the Aqua host needs it too, because launch bundles cannot take a
   positional URL arg: Tiger's `open` has no --args, and the widget kit's argv
   path only sees -psn_ stripped args.  Registering the scheme here -- and
   declaring it in Info.plist CFBundleURLTypes -- lets a launch URL reach the
   widget kit as a `~open newtab:1 url:` command, exactly like macos.m does. */
@interface AquaURLHandler : NSObject
- (void)handleURLEvent:(NSAppleEventDescriptor *)event
        withReplyEvent:(NSAppleEventDescriptor *)replyEvent;
@end

@implementation AquaURLHandler
- (void)handleURLEvent:(NSAppleEventDescriptor *)event
        withReplyEvent:(NSAppleEventDescriptor *)replyEvent {
    (void) replyEvent;
    NSString *url = [[event paramDescriptorForKeyword:keyDirectObject] stringValue];
    if (!url || [url length] == 0) {
        return;
    }
    wakeAquaTick_();
    iString *str = newCStr_String([url cStringUsingEncoding:NSUTF8StringEncoding]);
    str = urlDecodeExclude_String(collect_String(str), "/#?:");
    postCommandf_App("~open newtab:1 url:%s", cstr_String(str));
    delete_String(str);
}
@end

void registerUrlHandler_Aqua(void) {
    static AquaURLHandler *handler;
    if (handler) {
        return;
    }
    handler = [[AquaURLHandler alloc] init];
    [[NSAppleEventManager sharedAppleEventManager]
        setEventHandler:handler
            andSelector:@selector(handleURLEvent:withReplyEvent:)
          forEventClass:kInternetEventClass
             andEventID:kAEGetURL];
}

/* C-visible autorelease-pool helpers for aquamain.c (which is C). */
void *beginAutoreleasePool_Aqua(void) {
    return [[NSAutoreleasePool alloc] init];
}
void endAutoreleasePool_Aqua(void *pool) {
    [(NSAutoreleasePool *)pool drain];
}
void runAquaMainLoop(void) {
    /* Run the AppKit main loop and step the widget kit from a 60Hz timer.  A
       bare NSApplication never populates the OS menu bar unless [NSApp run]
       owns the main thread; this is what makes the native menu bar (and other
       run-loop-maintained UI) actually appear. */
    /* Dock icon: setApplicationIconImage: drives the DOCK on Tiger (only the About
       panel ignores it, resolving the icon from the registered bundle instead).
       The bundled .icns isn't decodable by Tiger's NSImage (reps=0) and the
       resource archive isn't populated at startup, so read the bundled PNG as raw
       bytes, decode it with stb_image (format-agnostic, same decoder the app uses
       elsewhere) and wrap the RGBA in an NSBitmapImageRep. */
    {
        NSString *pngPath = [[NSBundle mainBundle] pathForResource:@"lagrange-64" ofType:@"png"];
        if (pngPath) {
            NSData *data = [NSData dataWithContentsOfFile:pngPath];
            int w = 0, h = 0, n = 4;
            stbi_uc *px = (data && [data length] > 0)
                ? stbi_load_from_memory([data bytes], (int) [data length], &w, &h, &n, STBI_rgb_alpha)
                : NULL;
            if (px && w > 0 && h > 0) {
                unsigned char *planes = px;
                NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
                    initWithBitmapDataPlanes:&planes
                                   pixelsWide:w pixelsHigh:h
                                bitsPerSample:8 samplesPerPixel:4
                                     hasAlpha:YES isPlanar:NO
                              colorSpaceName:NSCalibratedRGBColorSpace
                                 bytesPerRow:w * 4 bitsPerPixel:32];
                NSImage *icon = [[NSImage alloc] initWithSize:NSMakeSize(w, h)];
                [icon addRepresentation:rep];
                [NSApp setApplicationIconImage:icon];
                [icon release];
                [rep release];
                /* px is referenced by rep (planes not copied); don't free it. */
            }
        }
    }
    static AquaWidgetTimer *timerTarget;
    if (!timerTarget) {
        timerTarget = [[AquaWidgetTimer alloc] init];
    }
    gTimerTarget_ = timerTarget;
    armAquaTick_(kFastInterval_Aqua);
    {
        const char *st = getenv("AQUA_SELFTEST");
        if (st && sscanf(st, "%d,%d", &gSelfTestX_, &gSelfTestY_) == 2) {
            static AquaSelfTest *selfTest;
            const char *delay = getenv("AQUA_SELFTEST_DELAY");
            const double at = delay ? atof(delay) : 4.0;
            selfTest = [[AquaSelfTest alloc] init];
            [NSTimer scheduledTimerWithTimeInterval:at
                                              target:selfTest
                                            selector:@selector(move:)
                                            userInfo:nil
                                             repeats:NO];
            [NSTimer scheduledTimerWithTimeInterval:at + 0.6
                                              target:selfTest
                                            selector:@selector(click:)
                                            userInfo:nil
                                             repeats:NO];
        }
    }
    [[NSApplication sharedApplication] run];
}

int initAquaView_app(int width, int height) {
    if (gShimReady_) return 0;
    if (width <= 0) width = 900;
    if (height <= 0) height = 560;

    [NSApplication sharedApplication];
    gDelegate_ = [[AquaAppDelegate alloc] init];
    [NSApp setDelegate:gDelegate_];

    /* No NSWindow is created here: the widget kit (init_App) creates the shim
       windows via SDL_CreateWindow and windowCreatedAqua_ makes the matching
       NSWindow (so extra windows get their own native window too).  Window 0,
       the primary, is created the moment init_App builds the main window. */
    setPresentHook_canvas(presentHookAqua_, NULL);
    setPumpHook_canvas(pumpHookAqua_, NULL);
    setCursorHook_canvas(cursorHookAqua_, NULL);
    setWindowCreatedHook_canvas(windowCreatedAqua_, NULL);
    setWindowDestroyedHook_canvas(windowDestroyedAqua_, NULL);
    setWindowTitleHook_canvas(windowTitleAqua_, NULL);
    gShimReady_ = iTrue;
    return 0;
}

void deinitAquaView_app(void) {
    gShimReady_ = iFalse;
    invalidateAquaTick_();
    gTimerTarget_ = nil; /* not owned here: the static AquaWidgetTimer owns it */
    setAquaPopupEvent_Aqua(NULL); /* release the retained context-menu event */
    for (int i = 0; i < gAqNum; i++) {
        if (gAqTable[i].view) {
            if (gAqTable[i].view->lastPresented_) {
                free(gAqTable[i].view->lastPresented_);
                gAqTable[i].view->lastPresented_ = NULL;
                gAqTable[i].view->lastPresentedBytes_ = 0;
            }
            [gAqTable[i].view release];
        }
        if (gAqTable[i].win) {
            [gAqTable[i].win release];
        }
        gAqTable[i].view = nil;
        gAqTable[i].win = nil;
        gAqTable[i].id = 0;
    }
    gAqNum = 0;
    if (gDelegate_) { [gDelegate_ release]; gDelegate_ = nil; }
}
