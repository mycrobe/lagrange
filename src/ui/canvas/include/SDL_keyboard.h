#pragma once
#if defined (LAGRANGE_CANVAS_SDL_BACKEND)
#include_next <SDL_keyboard.h>
#else
#include "../sdlcompat.h"
#endif
