/* aquaview.m — Aqua (Tiger/Leopard PPC) AppKit canvas-host backend.

   The T-tier window backend for the Phase 0/2 canvas seam: a single programmatic
   NSWindow (no nibs) whose content NSView displays the portable shim's software
   framebuffer (sdlcompat.c).  Native NSEvents are translated into shim SDL events
   through the same _canvas hook table sdlview.c uses, so the widget kit and the
   whole portable core are untouched.  10.4-era AppKit only: manual retain/release,
   old-style NSEvent constants, no blocks/properties, respondsToSelector: guards
   for anything newer.

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

/* canvasScale_canvas() is implemented in sdlcompat.c but has no header prototype
   (it is a viewer-only helper, like sdlview.c uses). */
int canvasScale_canvas(void);

/* ---------------------------------------------------- shim hook exports --- */
/* Declared in sdlcompat.h; the bodies live in sdlcompat.c (compiled into this
   app).  Only the subset the Aqua view needs is used here. */

/* ------------------------------------------------------------- globals --- */

static NSWindow *gWin_;
static NSView   *gView_;
static const Uint8 *gCanvasPx_; /* shim framebuffer (RGBA8888), valid until next present */
static int gCanvasW_, gCanvasH_, gCanvasPitch_;

/* ------------------------------ pixel blit + drawRect -------------------- */

static void captureCanvas_(void) {
    gCanvasPx_ = canvasPixels_canvas(0, &gCanvasW_, &gCanvasH_, &gCanvasPitch_);
    if (gCanvasW_ < 0) gCanvasW_ = 0;
    if (gCanvasH_ < 0) gCanvasH_ = 0;
}

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

/* NSWindow subclass: forwards Cmd-key equivalents to the widget kit.  macOS
   calls performKeyEquivalent: (a window) before keyDown:; the native menu bar
   renders its items but its NSMenuItem key equivalents do not fire under this
   host's [NSApp run] + timer model, so a Cmd-combo would otherwise be swallowed
   (beep) and never reach the view's keyDown:.  Translating it into an SDL key
   event lets the widget kit's own shortcut table (Cmd+L/T/C/V/X …) dispatch it. */
@interface AquaWindow : NSWindow
- (BOOL)performKeyEquivalent:(NSEvent *)event;
@end

@implementation AquaWindow

- (BOOL)performKeyEquivalent:(NSEvent *)event {
    /* Let the native menu's key equivalents win (Quit, Preferences, menu items
       with shortcut labels).  Under this host's [NSApp run] + timer model the
       menu is not consulted automatically for key presses, so ask it here. */
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
            SDL_PushEvent(&ev);
            SDL_Event up;
            memset(&up, 0, sizeof(up));
            up.key.type = SDL_KEYUP;
            up.key.state = SDL_RELEASED;
            up.key.keysym.scancode = (SDL_Scancode) sc;
            up.key.keysym.sym = (SDL_Keycode) sym;
            up.key.keysym.mod = mods;
            SDL_PushEvent(&up);
            return YES; /* consumed: the widget kit's shortcut table owns it */
        }
    }
    return NO;
}

@end

@interface AquaCanvasView : NSView {
@private
    unsigned long buttons_; /* SDL_BUTTON() bitmask, tracked from mouse events */
}
- (void)pushMouse:(NSEvent *)event down:(BOOL)down;
@end

@implementation AquaCanvasView

- (BOOL)isFlipped { return NO; }
- (BOOL)acceptsFirstResponder { return YES; }
/* Tiger swallows the first click on a window that is not *key* as an
   activation click and never delivers it to the content view (acceptsFirstMouse
   defaults to NO).  Under the [NSApp run] + timer model the window's key status
   is often not established before the user's first click, so every click can be
   consumed as an activation click -- no mouse interaction at all.  Opt the view
   in so the first click also gets through. */
- (BOOL)acceptsFirstMouse:(NSEvent *)event { (void) event; return YES; }

/* The shim canvas (gCanvasW_ x gCanvasH_) is letterboxed into the view: this is
   the rect it occupies (in the view's bottom-left origin -- the view is not
   flipped) together with the integer/fractional scale.  Letterbox instead of
   stretch, preserving the canvas aspect so content is never distorted (mirror
   the SDL2 host's behaviour); whole-number scale when it fits, fractional only
   if the window is too small.  The draw path AND the mouse-coordinate mapping
   both use this so they can never diverge. */
- (double)canvasScaleInView {
    const int cw = gCanvasW_, ch = gCanvasH_;
    if (cw <= 0 || ch <= 0) return 1.0;
    const double sx = (double) NSWidth([self bounds]) / (double) cw;
    const double sy = (double) NSHeight([self bounds]) / (double) ch;
    const double sf = (sx < sy) ? sx : sy;
    return (sf >= 1.0) ? (double) ((int) sf) : sf;
}

- (NSRect)canvasRectInView {
    const double s = [self canvasScaleInView];
    const double dw = (double) gCanvasW_ * s, dh = (double) gCanvasH_ * s;
    const double dx = (NSWidth([self bounds]) - dw) / 2.0;
    const double dy = (NSHeight([self bounds]) - dh) / 2.0; /* bottom-left origin */
    return NSMakeRect(dx, dy, dw, dh);
}

- (void)drawCanvasInto:(NSRect)bounds {
    if (!gCanvasPx_ || gCanvasW_ <= 0 || gCanvasH_ <= 0) {
        [[NSColor colorWithCalibratedRed:0.08 green:0.08 blue:0.08 alpha:1.0] setFill];
        NSRectFill(bounds);
        return;
    }
    const int cw = gCanvasW_, ch = gCanvasH_;
    NSRect dst = [self canvasRectInView];

    /* dark clears the letterbox bars */
    [[NSColor colorWithCalibratedRed:0.08 green:0.08 blue:0.08 alpha:1.0] setFill];
    NSRectFill(bounds);

    /* Draw the shim's RGBA8888 buffer straight to the view context as a CGImage
       with an explicit byte order.  32-bit big-endian + alpha-skip-last is the
       deterministic memory order for R,G,B,A on PPC (the rep approach left the
       byte order to the rep's endianness, a real risk on a big-endian target). */
    CGDataProviderRef prov = CGDataProviderCreateWithData(
        NULL, gCanvasPx_, (size_t) ch * gCanvasPitch_, NULL);
    CGImageRef img = CGImageCreate(cw, ch, 8, 32, gCanvasPitch_,
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
    const int cw = gCanvasW_, ch = gCanvasH_;
    NSRect r = [self canvasRectInView];
    if (cw <= 0 || ch <= 0 || NSWidth(r) <= 0.0 || NSHeight(r) <= 0.0) {
        return NSMakePoint(vp.x, NSHeight([self bounds]) - vp.y);
    }
    const double s = [self canvasScaleInView];
    const double cx = (vp.x - NSMinX(r)) / s;
    const double cy = (NSMaxY(r) - vp.y) / s; /* canvas top-left/y-down */
    return NSMakePoint(cx, cy);
}

- (void)pushMouse:(NSEvent *)event down:(BOOL)down {
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
    ev.button.x = (Sint32) p.x;
    ev.button.y = (Sint32) p.y;
    SDL_PushEvent(&ev);
    if (mouseDbg_()) {
        NSRect r = [self canvasRectInView];
        fprintf(stderr, "[aqua] mouse %s at=%d,%d view=%dx%d canvas=%dx%d rect=%.0f,%.0f %.0fx%.0f s=%.2f\n",
                down ? "down" : "up", (int) p.x, (int) p.y,
                (int) NSWidth([self bounds]), (int) NSHeight([self bounds]),
                gCanvasW_, gCanvasH_, r.origin.x, r.origin.y, r.size.width, r.size.height,
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
    NSPoint p = [self sdlPoint:e];
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.motion.type = SDL_MOUSEMOTION;
    ev.motion.x = (Sint32) p.x;
    ev.motion.y = (Sint32) p.y;
    ev.motion.state = (Uint32) buttons_;
    SDL_PushEvent(&ev);
    if (mouseDbg_()) {
        fprintf(stderr, "[aqua] mouse move at=%d,%d view=%dx%d canvas=%dx%d\n",
                (int) p.x, (int) p.y,
                (int) NSWidth([self bounds]), (int) NSHeight([self bounds]),
                gCanvasW_, gCanvasH_);
    }
}

- (void)mouseDragged:(NSEvent *)e { [self mouseMoved:e]; }
- (void)scrollWheel:(NSEvent *)e {
    NSPoint p = [self sdlPoint:e];
    const int scale = canvasScale_canvas();
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.wheel.type = SDL_MOUSEWHEEL;
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
    int sc = 0;
    const Uint32 sym = keySymFromEvent_(e, &sc);
    const Uint16 mods = keyModFromFlags_([e modifierFlags]);
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.key.type = SDL_KEYDOWN;
    ev.key.state = SDL_PRESSED;
    ev.key.repeat = [e isARepeat] ? 1 : 0;
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
        strncpy(tx.text.text, [chars UTF8String], sizeof(tx.text.text) - 1);
        tx.text.text[sizeof(tx.text.text) - 1] = '\0';
        SDL_PushEvent(&tx);
    }
}

- (void)keyUp:(NSEvent *)e {
    int sc = 0;
    const Uint32 sym = keySymFromEvent_(e, &sc);
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.key.type = SDL_KEYUP;
    ev.key.state = SDL_RELEASED;
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
                                windowNumber:[gWin_ windowNumber]
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
    AquaCanvasView *view = (AquaCanvasView *) gView_;
    if (!view) return;
    fprintf(stderr, "[aqua] selftest move at %d,%d\n", gSelfTestX_, gSelfTestY_);
    sendSyntheticMouse_(view, NSMouseMoved, gSelfTestX_, gSelfTestY_);
    if (getenv("AQUA_SELFTEST_IMMEDIATE")) {
        [self click:nil];
    }
}
- (void)click:(NSTimer *)timer {
    (void) timer;
    AquaCanvasView *view = (AquaCanvasView *) gView_;
    if (!view) return;
    fprintf(stderr, "[aqua] selftest click at %d,%d\n", gSelfTestX_, gSelfTestY_);
    sendSyntheticMouse_(view, NSLeftMouseDown, gSelfTestX_, gSelfTestY_);
    sendSyntheticMouse_(view, NSLeftMouseUp, gSelfTestX_, gSelfTestY_);
}
@end

/* ------------------------------------------------------------------ hooks --- */
static void presentHookAqua_(int winIndex) {
    (void) winIndex;
    if (!gView_) return;
    captureCanvas_();
    if (!gCanvasPx_ || gCanvasW_ <= 0 || gCanvasH_ <= 0) return;
    [gView_ setNeedsDisplay:YES];
    [gView_ displayIfNeeded];
}

static void pumpHookAqua_(void) {
    if (!gWin_) return;
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
- (void)windowWillClose:(NSNotification *)n { (void) n; [self quitRequested]; }
- (void)quitRequested {
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
    /* Each tick must be an autorelease island: on Tiger AppKit does not provide
       an automatic pool for every run-loop event, and step_App drives the widget
       kit (event dispatch + render + present + autolayout) which autoreleases a
       steady stream of Foundation/AppKit objects.  Without a per-frame pool they
       hit _NSAutoreleaseNoPool and never release. */
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    step_App(postedEventsOnly_AppEventMode);
    /* The widget kit may have quit (isRunning = false) on SDL_QUIT / a "quit"
       command; unwind AppKit's loop so the process actually exits. */
    if (!isAppRunning()) {
        [[NSApplication sharedApplication] terminate:nil];
    }
    [pool drain];
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
    static AquaWidgetTimer *timerTarget;
    if (!timerTarget) {
        timerTarget = [[AquaWidgetTimer alloc] init];
    }
    [NSTimer scheduledTimerWithTimeInterval:1.0 / 60.0
                                     target:timerTarget
                                   selector:@selector(tick:)
                                   userInfo:nil
                                    repeats:YES];
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

static AquaAppDelegate *gDelegate_;

int initAquaView_app(int width, int height) {
    if (gWin_) return 0;
    if (width <= 0) width = 900;
    if (height <= 0) height = 560;

    [NSApplication sharedApplication];
    gDelegate_ = [[AquaAppDelegate alloc] init];
    [NSApp setDelegate:gDelegate_];

    /* The window is resizable; the shim canvas is a fixed 900x560 and is letterboxed
       into the resized content view (drawCanvasInto + sdlPoint share the same
       canvasRectInView transform), so mouse coordinates keep mapping to widgets even
       when the window is resized. */
    gWin_ = [[AquaWindow alloc]
             initWithContentRect:NSMakeRect(0, 0, width, height)
             styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                         NSMiniaturizableWindowMask | NSResizableWindowMask)
             backing:NSBackingStoreBuffered defer:NO];
    [gWin_ setTitle:@"Lagrange"];
    [gWin_ setAcceptsMouseMovedEvents:YES];
    [gWin_ setDelegate:gDelegate_];

    gView_ = [[[AquaCanvasView alloc] initWithFrame:NSMakeRect(0, 0, width, height)] autorelease];
    [gView_ setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [gWin_ setContentView:gView_];
    [gWin_ makeFirstResponder:gView_];

    setPresentHook_canvas(presentHookAqua_, NULL);
    setPumpHook_canvas(pumpHookAqua_, NULL);
    setCursorHook_canvas(cursorHookAqua_, NULL);

    [NSApp activateIgnoringOtherApps:YES];
    [gWin_ makeKeyAndOrderFront:nil];
    [gWin_ display];
    return 0;
}

void deinitAquaView_app(void) {
    if (gView_) { [gView_ release]; gView_ = nil; }
    if (gWin_)  { [gWin_ release];  gWin_  = nil; }
    if (gDelegate_) { [gDelegate_ release]; gDelegate_ = nil; }
    gCanvasPx_ = NULL;
}
