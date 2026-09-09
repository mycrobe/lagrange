/* shimtext.c — focused test replicating lagrange's stb-text render path:
   INDEX8 glyph raster (palette alpha=ramp) -> blit into INDEX8 buffer -->
   surface texture -> RenderCopy into RGBA cache target texture (blendmode
   NONE) -> RenderCopy of cache with color/alpha mod + clip onto the canvas.

   Built against either the real SDL2 (SHIM=0) or the sdlcompat shim (SHIM=1).
   Output: PPM capture of the final canvas.
*/
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if SHIM
extern Uint64 presentCount_canvas(void);
extern void captureRender_canvas(SDL_Renderer *, const char *);
#endif

static void writePPM(const char *path, Uint8 *pix, int w, int h, int pitch) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            fwrite(pix + y * pitch + x * 4, 1, 3, f);
    fclose(f);
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "out.ppm";
    const int scale = argc > 2 ? atoi(argv[2]) : 1;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
        fprintf(stderr, "init failed: %s\n", SDL_GetError());
        return 1;
    }

    const int W = 320 * scale, H = 240 * scale;
    SDL_Window *win = SDL_CreateWindow("shimtext", 0, 0, W, H, SDL_WINDOW_SHOWN);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);

    /* ---- glyph cache texture, like initCache_StbText_ ---- */
    const int CW = 256, CH = 128;
    SDL_Texture *cache = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET, CW, CH);

    /* grayscale palette exactly like glyphPalette_() in text_stb.c */
    SDL_Palette *pal = SDL_AllocPalette(256);
    {
        SDL_Color colors[256];
        for (int i = 0; i < 256; ++i)
            colors[i] = (SDL_Color){ 255, 255, 255, (Uint8) (255.0f * (i / 255.0f) + 0.5f) };
        SDL_SetPaletteColors(pal, colors, 0, 256);
    }

    /* synthetic glyph raster 24x12 */
    const int gw = 24, gh = 12;
    static Uint8 bmp[24 * 12];
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++) {
            int ax = x >= 4 && x < 19 ? 255 : x * 255 / 4;
            int ay = y >= 2 && y < gh - 2 ? 255 : y * 255 / 2;
            bmp[y * gw + x] = (Uint8) ((ax * ay) / 255);
        }

    /* === stage 1: surface8 -blit-> buf (INDEX8->INDEX8, palette set) === */
    SDL_Surface *surface8 = SDL_CreateRGBSurfaceWithFormatFrom(bmp, gw, gh, 8, gw,
                                                               SDL_PIXELFORMAT_INDEX8);
    SDL_SetSurfaceBlendMode(surface8, SDL_BLENDMODE_NONE);
    SDL_SetSurfacePalette(surface8, pal);

    SDL_Surface *buf = SDL_CreateRGBSurfaceWithFormat(0, 64, 16, 8, SDL_PIXELFORMAT_INDEX8);
    SDL_SetSurfaceBlendMode(buf, SDL_BLENDMODE_NONE);
    SDL_SetSurfacePalette(buf, pal);
    SDL_BlitSurface(surface8, NULL, buf, &(SDL_Rect){ 4, 2, gw, gh });

    /* === stage 2: buf -> texture -> RenderCopy into cache target === */
    SDL_Texture *bufTex = SDL_CreateTextureFromSurface(ren, buf);
    SDL_SetTextureBlendMode(bufTex, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(ren, cache);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, bufTex, NULL, &(SDL_Rect){ 8, 6, gw, gh });
    SDL_DestroyTexture(bufTex);
    SDL_SetRenderTarget(ren, NULL);

    /* === stage 3: cache onto the canvas with color mod, alpha 160, clip,
         and a non-integer dst scale (2x canvas, half-size dst like app does
         when compositing prerendered text) === */
    SDL_SetTextureColorMod(cache, 0x46, 0x60, 0x80);
    SDL_SetTextureAlphaMod(cache, 160);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderClear(ren);
    SDL_Rect src = { 8, 6, gw, gh };
    SDL_Rect dst = { 16 * scale, 12 * scale, gw * scale / 2 + 4, gh * scale / 2 + 2 };
    SDL_Rect clip = { 4, 8, W - 8, H - 40 };
    SDL_RenderSetClipRect(ren, &clip);
    SDL_RenderCopy(ren, cache, &src, &dst);
    SDL_RenderSetClipRect(ren, NULL);

#if SHIM
    captureRender_canvas(ren, out);
#else
    Uint8 *readpix = malloc((size_t) W * 4 * H);
    if (SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ABGR8888,
                             readpix, W * 4) != 0) {
        fprintf(stderr, "readpixels failed: %s\n", SDL_GetError());
        return 2;
    }
    writePPM(out, readpix, W, H, W * 4);
    free(readpix);
#endif
    fprintf(stderr, "wrote %s (%dx%d scale=%d)\n", out, W, H, scale);
    return 0;
}
