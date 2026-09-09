#pragma once
#if defined (LAGRANGE_CANVAS_SDL_BACKEND)
#include_next <SDL_rwops.h>
#else
#include "../sdlcompat.h"
#endif
