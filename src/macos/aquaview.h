/* aquaview.h — Aqua (Tiger/Leopard PPC) AppKit canvas-host backend, C surface.

   Mirrors the sdlview.c role but against AppKit instead of real SDL2: this file
   owns the single NSWindow + NSView and the NSApplication wiring.  All
   rendering stays in the portable shim's software framebuffer (sdlcompat.c);
   this layer blits pixels and translates native NSEvents into shim SDL events
   via the same _canvas hook table (setPresentHook/setPumpHook/setCursorHook +
   SDL_PushEvent).  The lagrange main() (aquamain.c) calls these two functions.

   Copyright 2026 the lagrange port.  Distributed under the BSD-2-Clause license
   of the core this host drives. */

#pragma once

int  initAquaView_app(int width, int height);
void deinitAquaView_app(void);
void runAquaMainLoop(void);

/* Autorelease-pool frame helpers.  aquamain.c is plain C, so it cannot create an
   NSAutoreleasePool itself, but the whole [NSApp run] lifecycle autoreleases on
   the main thread (menu building, run-loop event/window/timer churn) and on
   Tiger AppKit does not supply a pool for every event the way modern macOS does
   -- without an enclosing pool every such object lands in _NSAutoreleaseNoPool
   and leaks.  main() wraps its body in begin/end so every main-thread
   autorelease has a home; the widget-kit tick (aquaview.m) additionally uses a
   per-frame pool so the timer-driven render churn drains each frame. */
void *beginAutoreleasePool_Aqua(void);
void  endAutoreleasePool_Aqua(void *pool);

/* Register the kAEGetURL AppleEvent handler so a Finder
   `open gemini://host/path` delivers the URL to the widget kit as a
   `~open newtab:1 url:` command (Tiger's `open` has no --args, so a
   bundle-launched app cannot take a positional URL arg). */
void registerUrlHandler_Aqua(void);

/* Context-menu event plumbing.  Tiger has no `popUpMenuPositioningItem:
   atLocation:inView:` (10.6+), so the native menu backend must show a
   context menu through +[NSMenu popUpContextMenu:withEvent:forView:], which
   needs the originating NSEvent.  The Aqua view remembers the most recent
   mouse-down NSEvent here; the menu backend (canvasmenu_impl_aqua.m) asks for
   it.  Handled as void* so this header stays C-compatible (like the pool
   helpers); the ObjC caller casts back to NSEvent and NSView. */
void setAquaPopupEvent_Aqua(void *event);
void *currentAquaPopupEvent_Aqua(void);
void *aquaMainView_Aqua(void);
