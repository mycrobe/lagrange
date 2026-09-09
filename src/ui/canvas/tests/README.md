# shimtext — focused shim/real-SDL2 text-pipeline test

Replicates the app's stb-text render path (INDEX8 glyph raster with palette
alpha ramp -> SDL_BlitSurface INDEX8→INDEX8 -> surface texture -> RenderCopy
with blendmode NONE into an RGBA cache *target* texture -> RenderCopy of the
cache with color/alpha mod + clip onto the canvas) and captures the final
pixels as PPM. Build it twice — once against the sdlcompat shim, once against
the real SDL2 — and byte-compare the outputs.

## Build (from the repo root)

    # shim variant
    cc -DSHIM=1 -I src/ui/canvas/include -I src/ui/canvas \
       -I lib/the_Foundation/include -I build-canvas/lib/the_Foundation \
       src/ui/canvas/tests/shimtext.c src/ui/canvas/sdlcompat.c \
       -o /tmp/shimtext-shim -L build-canvas/lib/the_Foundation -l_Foundation

    # real SDL2 variant (patched SDL2; -rpath so it runs)
    cc -O2 -DSHIM=0 -I /tmp/kilo/sdl2/include -I /tmp/kilo/sdl2/include/SDL2 \
       src/ui/canvas/tests/shimtext.c \
       -o /tmp/shimtext-real -L /tmp/kilo/sdl2/lib -lSDL2 -Wl,-rpath,/tmp/kilo/sdl2/lib

## Run + diff

    /tmp/shimtext-shim shim.ppm 1 && /tmp/shimtext-real real.ppm 1
    /tmp/shimtext-shim shim2.ppm 2 && /tmp/shimtext-real real2.ppm 2

Byte-compare the PPM payloads (after the 3-line header). Expected: maxdiff 0
at both scales. Any nonzero diff = shim render semantics diverge from real
SDL2 — see sdlcompat.c SDL_RenderCopy (blend NONE vs BLEND handling).

Note the pixel-format trap: SDL_PIXELFORMAT_RGBA8888 has *byte order*
A,B,G,R in memory on little-endian; use SDL_PIXELFORMAT_ABGR8888 with
SDL_RenderReadPixels so the PPM gets R,G,B,A. The shim canvas is R,G,B,A.
