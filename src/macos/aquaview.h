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
