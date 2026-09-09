/* sdlcompat.h — portable stand-in for the SDL 2 API surface used by lagrange.

   This is the Phase 0 host seam: the widget kit keeps its SDL-shaped event
   structs and Paint-pipeline calls, but SDL is provided by this single
   implementation instead of libSDL. Backends (Aqua, Toolbox/QuickDraw)
   replace this file's bodies, not the call sites. Software framebuffer
   rasterizer: every render call draws into a locked RGBA8888 canvas. */

#pragma once

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

typedef int SDL_bool;
#define SDL_TRUE 1
#define SDL_FALSE 0

typedef unsigned char Uint8;
typedef unsigned short Uint16;
typedef unsigned int Uint32;
typedef signed char Sint8;
typedef short Sint16;
typedef int Sint32;
typedef long long Sint64;
typedef unsigned long long Uint64;

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_Cursor SDL_Cursor;
typedef struct SDL_RWops SDL_RWops;
typedef Uint32 SDL_WindowID;
typedef Uint32 SDL_AudioDeviceID;
typedef Uint32 SDL_FingerID;
typedef Sint64 SDL_TimerID;
typedef Sint64 SDL_TouchID;
typedef Uint32 SDL_Keycode;
typedef int SDL_Scancode;
typedef int SDL_KeymodAlias;

struct SDL_Point {
    int x, y;
};
typedef struct SDL_Point SDL_Point;

struct SDL_Rect {
    int x, y, w, h;
};
typedef struct SDL_Rect SDL_Rect;

struct SDL_Color {
    Uint8 r, g, b, a;
};
typedef struct SDL_Color SDL_Color;

struct SDL_Palette {
    int ncolors;
    SDL_Color *colors;
    Uint32 version;
    int refcount;
};
typedef struct SDL_Palette SDL_Palette;

typedef struct SDL_PixelFormat SDL_PixelFormat;
struct SDL_PixelFormat {
    Uint32 format;
    Uint8 BitsPerPixel;
    Uint8 BytesPerPixel;
    Uint8 Rbits, Gbits, Bbits, Abits;
    Uint8 Rshift, Gshift, Bshift, Ashift;
    Uint32 Rmask, Gmask, Bmask, Amask;
    SDL_Palette *palette;
};

struct SDL_Surface {
    Uint32 flags;
    SDL_PixelFormat *format;
    int w, h, pitch;
    void *pixels;
    int refcount;
    int locked;
};
typedef struct SDL_Surface SDL_Surface;

struct SDL_Texture {
    int w, h;
    Uint32 format, access, blendMode, scaleMode;
    Uint8 r, g, b, a;
    int pitch;
    void *pixels; /* RGBA8888 buffer of w*h*4, converted at creation */
};
typedef struct SDL_Texture SDL_Texture;

struct SDL_RendererInfo {
    const char *name;
    int max_texture_width;
    int max_texture_height;
    Uint32 flags;
    Uint32 num_texture_formats;
};
typedef struct SDL_RendererInfo SDL_RendererInfo;

struct SDL_Renderer {
    int w, h; /* output size */
    SDL_Window *window;
    SDL_Texture *target; /* NULL -> window canvas */
    Uint8 r, g, b, a; /* current draw color */
    Uint32 blendMode;
    SDL_Rect clip;
    SDL_bool clipEnabled;
    Uint8 *canvas; /* window framebuffer RGBA8888, w*h*4 */
    int canvasPitch;
    SDL_RendererInfo info;
};
typedef struct SDL_Renderer SDL_Renderer; /* (dupe-safe) */

struct SDL_Window {
    SDL_WindowID id;
    int x, y, w, h;
    Uint32 flags;
    char title[256];
    SDL_Renderer *renderer;
    int displayIndex;
    void *hitTest;
};

struct SDL_Cursor {
    int id;
};
typedef struct SDL_Cursor SDL_Cursor;

struct SDL_RWops {
    void *ctx;
    int (*read)(SDL_RWops *, void *, int, int);
    int (*seek)(SDL_RWops *, int, int);
    int (*write)(SDL_RWops *, const void *, int, int);
    Sint64 (*size)(SDL_RWops *);
    Uint32 type;
    int (*close)(SDL_RWops *);
    void *fp;
};

/* Pixel formats (SDL2 values). */
enum SDL_PixelFormatEnum {
    SDL_PIXELFORMAT_UNKNOWN = 0,
    SDL_PIXELFORMAT_INDEX8 = 0x13000801,
    SDL_PIXELFORMAT_RGB888 = 0x16161804,
    SDL_PIXELFORMAT_RGBA8888 = 0x16462004,
    SDL_PIXELFORMAT_ABGR8888 = 0x16762004,
    SDL_PIXELFORMAT_RGBA32 = SDL_PIXELFORMAT_ABGR8888,
    SDL_PIXELFORMAT_RGBA4444 = 0x21520402, /* 4444 variant */
};

/* Event types */
enum SDL_EventType {
    SDL_FIRSTEVENT = 0,
    SDL_QUIT = 0x100,
    SDL_APP_TERMINATING = 0x101,
    SDL_APP_LOWMEMORY = 0x102,
    SDL_APP_WILLENTERBACKGROUND = 0x103,
    SDL_APP_DIDENTERBACKGROUND = 0x104,
    SDL_APP_WILLENTERFOREGROUND = 0x105,
    SDL_APP_DIDENTERFOREGROUND = 0x106,
    SDL_DISPLAYEVENT = 0x150,
    SDL_WINDOWEVENT = 0x200,
    SDL_SYSWMEVENT = 0x201,
    SDL_KEYDOWN = 0x300,
    SDL_KEYUP = 0x301,
    SDL_TEXTEDITING = 0x302,
    SDL_TEXTINPUT = 0x303,
    SDL_TEXTEDITING_EXT = 0x304,
    SDL_MOUSEMOTION = 0x400,
    SDL_MOUSEBUTTONDOWN = 0x401,
    SDL_MOUSEBUTTONUP = 0x402,
    SDL_MOUSEWHEEL = 0x403,
    SDL_JOYSTICKAXISMOTION = 0x600,
    SDL_JOYSTICKBUTTONDOWN = 0x603,
    SDL_JOYSTICKBUTTONUP = 0x604,
    SDL_JOYSTICKDEVICEADDED = 0x605,
    SDL_JOYSTICKDEVICEREMOVED = 0x606,
    SDL_CONTROLLERAXISMOTION = 0x650,
    SDL_CONTROLLERBUTTONDOWN = 0x651,
    SDL_CONTROLLERBUTTONUP = 0x652,
    SDL_CONTROLLERDEVICEADDED = 0x653,
    SDL_CONTROLLERDEVICEREMOVED = 0x654,
    SDL_CONTROLLERTOUCHPADDOWN = 0x655,
    SDL_CONTROLLERTOUCHPADMOTION = 0x656,
    SDL_CONTROLLERTOUCHPADUP = 0x657,
    SDL_FINGERDOWN = 0x700,
    SDL_FINGERUP = 0x701,
    SDL_FINGERMOTION = 0x702,
    SDL_DROPFILE = 0x1000,
    SDL_DROPTEXT = 0x1001,
    SDL_DROPBEGIN = 0x1002,
    SDL_DROPCOMPLETE = 0x1003,
    SDL_AUDIODEVICEADDED = 0x1100,
    SDL_AUDIODEVICEREMOVED = 0x1101,
    SDL_RENDER_TARGETS_RESET = 0x2000,
    SDL_RENDER_DEVICE_RESET = 0x2001,
    SDL_USEREVENT = 0x8000,
    SDL_LASTEVENT = 0xFFFF
};

/* Scancodes (subset mirroring SDL2 values) */
enum SDL_ScancodeEnum {
    SDL_SCANCODE_UNKNOWN = 0,
    SDL_SCANCODE_UP = 82,
    SDL_SCANCODE_DOWN = 81,
    SDL_SCANCODE_LEFT = 80,
    SDL_SCANCODE_RIGHT = 79,
    SDL_SCANCODE_RETURN = 40,
    SDL_SCANCODE_ESCAPE = 41,
    SDL_SCANCODE_BACKSPACE = 42,
    SDL_SCANCODE_TAB = 43,
    SDL_SCANCODE_SPACE = 44,
    SDL_SCANCODE_DELETE = 76,
    SDL_SCANCODE_END = 77,
    SDL_SCANCODE_PAGEDOWN = 78,
    SDL_SCANCODE_PAGEUP = 75,
    SDL_SCANCODE_HOME = 74,
    SDL_SCANCODE_INSERT = 73,
    SDL_SCANCODE_GRAVE = 53,
    SDL_SCANCODE_MINUS = 45,
    SDL_SCANCODE_EQUALS = 46,
    SDL_SCANCODE_F1 = 58,
    SDL_SCANCODE_F2 = 59,
    SDL_SCANCODE_F3 = 60,
    SDL_SCANCODE_F4 = 61,
    SDL_SCANCODE_F5 = 62,
    SDL_SCANCODE_F6 = 63,
    SDL_SCANCODE_F7 = 64,
    SDL_SCANCODE_F8 = 65,
    SDL_SCANCODE_F9 = 66,
    SDL_SCANCODE_F10 = 67,
    SDL_SCANCODE_F11 = 68,
    SDL_SCANCODE_F12 = 69,
    SDL_SCANCODE_LSHIFT = 224,
    SDL_SCANCODE_LCTRL = 224,
    SDL_SCANCODE_RCTRL = 228,
    SDL_SCANCODE_LALT = 226,
    SDL_SCANCODE_RALT = 230,
    SDL_SCANCODE_LGUI = 227,
    SDL_SCANCODE_RGUI = 231,
    SDL_SCANCODE_CAPSLOCK = 57,
    SDL_SCANCODE_KP_ENTER = 88,
    SDL_SCANCODE_KP_0 = 58 + 100, /* unique in this shim */
    SDL_SCANCODE_KP_1 = 89,
    SDL_SCANCODE_KP_2 = 90,
    SDL_SCANCODE_KP_3 = 91,
    SDL_SCANCODE_KP_4 = 92,
    SDL_SCANCODE_KP_5 = 93,
    SDL_SCANCODE_KP_6 = 94,
    SDL_SCANCODE_KP_7 = 95,
    SDL_SCANCODE_KP_8 = 96,
    SDL_SCANCODE_KP_9 = 97,
    SDL_SCANCODE_LMETA = 260,
};

/* Keycodes (subset mirroring SDL2 values) */
#define SDL_PRESSED 1
#define SDL_RELEASED 0

enum SDL_KeycodeEnum {
    SDLK_LSHIFT = 0x400000e1,
    SDLK_RSHIFT = 0x400000e5,
    SDLK_RGUI = 0x400000e7,
    SDLK_a = 'a', SDLK_b = 'b', SDLK_c = 'c', SDLK_d = 'd', SDLK_e = 'e',
    SDLK_f = 'f', SDLK_g = 'g', SDLK_h = 'h', SDLK_i = 'i', SDLK_j = 'j',
    SDLK_k = 'k', SDLK_l = 'l', SDLK_m = 'm', SDLK_n = 'n', SDLK_o = 'o',
    SDLK_p = 'p', SDLK_q = 'q', SDLK_r = 'r', SDLK_s = 's', SDLK_t = 't',
    SDLK_u = 'u', SDLK_v = 'v', SDLK_w = 'w', SDLK_x = 'x', SDLK_y = 'y',
    SDLK_z = 'z', SDLK_0 = '0', SDLK_1 = '1', SDLK_2 = '2', SDLK_3 = '3',
    SDLK_4 = '4', SDLK_5 = '5', SDLK_6 = '6', SDLK_7 = '7', SDLK_8 = '8',
    SDLK_9 = '9',
    SDLK_UNKNOWN = 0,
    SDLK_SPACE = ' ',
    SDLK_COMMA = ',',
    SDLK_MINUS = '-',
    SDLK_PERIOD = '.',
    SDLK_ESCAPE = 27,
    SDLK_RETURN = 13,
    SDLK_KP_ENTER = 0x400000AC,
    SDLK_BACKSPACE = 8,
    SDLK_DELETE = 127,
    SDLK_TAB = 9,
    SDLK_PLUS = '+',
    SDLK_SLASH = '/',
    SDLK_LEFTPAREN = '(',
    SDLK_RIGHTPAREN = ')',
    SDLK_LEFTBRACKET = '[',
    SDLK_RIGHTBRACKET = ']',
    SDLK_COLON = ':',
    SDLK_SEMICOLON = ';',
    SDLK_UNDERSCORE = '_',
    SDLK_CAPSLOCK = 0x40000039,
    SDLK_QUOTEDBL = '"',
    SDLK_APOSTROPHE = '\'',
    SDLK_LALT = 0x400000e2,
    SDLK_RALT = 0x400000e6,
    SDLK_LGUI = 0x400000e3,
    SDLK_LCTRL = 0x400000e0,
    SDLK_RCTRL = 0x400000e4,
    SDLK_LEFT = 0x40000050,
    SDLK_RIGHT = 0x4000004F,
    SDLK_UP = 0x40000052,
    SDLK_DOWN = 0x40000051,
    SDLK_HOME = 0x4000004A,
    SDLK_END = 0x4000004D,
    SDLK_PAGEUP = 0x4000004B,
    SDLK_PAGEDOWN = 0x4000004E,
    SDLK_F1 = 0x4000003A,
    SDLK_F2 = 0x4000003B,
    SDLK_F3 = 0x4000003C,
    SDLK_F4 = 0x4000003D,
    SDLK_F5 = 0x4000003E,
    SDLK_F6 = 0x4000003F,
    SDLK_F7 = 0x40000040,
    SDLK_F8 = 0x40000041,
    SDLK_F9 = 0x40000042,
    SDLK_F10 = 0x40000043,
    SDLK_F11 = 0x40000044,
    SDLK_F12 = 0x40000045,
    SDLK_EQUALS = '=',
    SDLK_AC_BACK = 0x40000108,
    SDLK_AC_FORWARD = 0x40000109,
    SDLK_AC_REFRESH = 0x4000010A,
    SDLK_AC_STOP = 0x4000010B,
    SDLK_AC_SEARCH = 0x4000010C,
    SDLK_AC_HOME = 0x4000010D,
    SDLK_AC_BOOKMARKS = 0x4000010E,
    SDLK_INSERT = 0x40000049,
};

enum SDL_Keymod {
    KMOD_NONE = 0x0000,
    KMOD_LSHIFT = 0x0001,
    KMOD_RSHIFT = 0x0002,
    KMOD_SHIFT = 0x0003,
    KMOD_LCTRL = 0x0040,
    KMOD_RCTRL = 0x0080,
    KMOD_CTRL = 0x00C0,
    KMOD_LALT = 0x0100,
    KMOD_RALT = 0x0200,
    KMOD_ALT = 0x0300,
    KMOD_LGUI = 0x0400,
    KMOD_RGUI = 0x0800,
    KMOD_GUI = 0x0C00,
    KMOD_NUM = 0x1000,
    KMOD_CAPS = 0x2000,
    KMOD_MODE = 0x4000,
};

enum SDL_SystemCursor {
    SDL_SYSTEM_CURSOR_ARROW = 0,
    SDL_SYSTEM_CURSOR_IBEAM = 1,
    SDL_SYSTEM_CURSOR_WAIT = 2,
    SDL_SYSTEM_CURSOR_CROSSHAIR = 3,
    SDL_SYSTEM_CURSOR_WAITARROW = 4,
    SDL_SYSTEM_CURSOR_SIZENWSE = 5,
    SDL_SYSTEM_CURSOR_SIZENESW = 6,
    SDL_SYSTEM_CURSOR_SIZEWE = 7,
    SDL_SYSTEM_CURSOR_SIZENS = 8,
    SDL_SYSTEM_CURSOR_SIZEALL = 9,
    SDL_SYSTEM_CURSOR_NO = 10,
    SDL_SYSTEM_CURSOR_HAND = 11,
    SDL_NUM_SYSTEM_CURSORS = 12
};

enum SDL_WindowEventID {
    SDL_WINDOWEVENT_NONE = 0,
    SDL_WINDOWEVENT_SHOWN = 1,
    SDL_WINDOWEVENT_HIDDEN = 2,
    SDL_WINDOWEVENT_EXPOSED = 3,
    SDL_WINDOWEVENT_MOVED = 4,
    SDL_WINDOWEVENT_RESIZED = 5,
    SDL_WINDOWEVENT_SIZE_CHANGED = 6,
    SDL_WINDOWEVENT_MINIMIZED = 7,
    SDL_WINDOWEVENT_MAXIMIZED = 8,
    SDL_WINDOWEVENT_RESTORED = 9,
    SDL_WINDOWEVENT_ENTER = 10,
    SDL_WINDOWEVENT_LEAVE = 11,
    SDL_WINDOWEVENT_FOCUS_GAINED = 12,
    SDL_WINDOWEVENT_FOCUS_LOST = 13,
    SDL_WINDOWEVENT_CLOSE = 14,
    SDL_WINDOWEVENT_TAKE_FOCUS = 15,
    SDL_WINDOWEVENT_HIT_TEST = 16,
    SDL_WINDOWEVENT_DISPLAY_CHANGED = 17
};

enum SDL_DisplayEventID {
    SDL_DISPLAYEVENT_NONE = 0,
    SDL_DISPLAYEVENT_ORIENTATION = 1,
};

enum {
    SDL_HITTEST_NORMAL = 0,
    SDL_HITTEST_DRAGGABLE = 1,
    SDL_HITTEST_RESIZE_TOPLEFT = 2,
    SDL_HITTEST_RESIZE_TOP = 3,
    SDL_HITTEST_RESIZE_TOPRIGHT = 4,
    SDL_HITTEST_RESIZE_RIGHT = 5,
    SDL_HITTEST_RESIZE_BOTTOMRIGHT = 6,
    SDL_HITTEST_RESIZE_BOTTOM = 7,
    SDL_HITTEST_RESIZE_BOTTOMLEFT = 8,
    SDL_HITTEST_RESIZE_LEFT = 9,
};

#define SDL_BUTTON(X)            (1 << ((X)-1))
#define SDL_TOUCH_MOUSEID ((Uint32) -1) /* real SDL2 value; 0 would match every real mouse */
#define SDL_PREALLOC 0x00000100u
#define AUDIO_U8     SDL_AUDIO_U8
#define AUDIO_S16    SDL_AUDIO_S16LSB
#define AUDIO_S16SYS SDL_AUDIO_S16SYS
#define AUDIO_S16LSB SDL_AUDIO_S16LSB
#define AUDIO_S32    0x1020
#define AUDIO_F32    SDL_AUDIO_F32SYS
#define AUDIO_F32SYS SDL_AUDIO_F32SYS
#define AUDIO_F32LSB SDL_AUDIO_F32LSB

enum {
    SDL_BUTTON_LEFT = 1,
    SDL_BUTTON_MIDDLE = 2,
    SDL_BUTTON_RIGHT = 3,
    SDL_BUTTON_X1 = 4,
    SDL_BUTTON_X2 = 5,
    SDL_BUTTON_LMASK = SDL_BUTTON(SDL_BUTTON_LEFT),
    SDL_BUTTON_MMASK = SDL_BUTTON(SDL_BUTTON_MIDDLE),
    SDL_BUTTON_RMASK = SDL_BUTTON(SDL_BUTTON_RIGHT),
};

enum SDL_BlendMode {
    SDL_BLENDMODE_NONE = 0x00000001,
    SDL_BLENDMODE_BLEND = 0x00000002,
    SDL_BLENDMODE_ADD = 0x00000004,
    SDL_BLENDMODE_MUL = 0x00000008,
};

enum SDL_ScaleMode {
    SDL_ScaleModeNearest = 0,
    SDL_ScaleModeLinear = 1,
    SDL_ScaleModeBest = SDL_ScaleModeLinear,
};

enum SDL_TextureAccess {
    SDL_TEXTUREACCESS_STATIC = 0,
    SDL_TEXTUREACCESS_STREAMING = 1,
    SDL_TEXTUREACCESS_TARGET = 2
};

enum SDL_RendererFlags {
    SDL_RENDERER_SOFTWARE = 0x00000001,
    SDL_RENDERER_ACCELERATED = 0x00000002,
    SDL_RENDERER_PRESENTVSYNC = 0x00000004,
    SDL_RENDERER_TARGETTEXTURE = 0x00000008
};

enum SDL_WindowFlags {
    SDL_WINDOW_FULLSCREEN = 0x00000001,
    SDL_WINDOW_OPENGL = 0x00000002,
    SDL_WINDOW_SHOWN = 0x00000004,
    SDL_WINDOW_HIDDEN = 0x00000008,
    SDL_WINDOW_BORDERLESS = 0x00000010,
    SDL_WINDOW_RESIZABLE = 0x00000020,
    SDL_WINDOW_MINIMIZED = 0x00000040,
    SDL_WINDOW_MAXIMIZED = 0x00000080,
    SDL_WINDOW_INPUT_GRABBED = 0x00000100,
    SDL_WINDOW_INPUT_FOCUS = 0x00000200,
    SDL_WINDOW_MOUSE_FOCUS = 0x00000400,
    SDL_WINDOW_FULLSCREEN_DESKTOP = (SDL_WINDOW_FULLSCREEN | 0x00001000),
    SDL_WINDOW_FOREIGN = 0x00000800,
    SDL_WINDOW_ALLOW_HIGHDPI = 0x00002000,
    SDL_WINDOW_MOUSE_CAPTURE = 0x00004000,
    SDL_WINDOW_ALWAYS_ON_TOP = 0x00008000,
    SDL_WINDOW_SKIP_TASKBAR = 0x00010000,
    SDL_WINDOW_POPUP_MENU = 0x00020000,
    SDL_WINDOW_METAL = 0x20000000,
};

#define SDL_WINDOWPOS_CENTERED 0x2FFF0000
#define SDL_WINDOWPOS_UNDEFINED 0x1FFF0000

/* hints */
enum {
    SDL_HINT_VIDEODRIVER = 1,
    SDL_HINT_RENDER_BATCHING,
    SDL_HINT_RENDER_VSYNC,
    SDL_HINT_RENDER_SCALE_QUALITY,
    SDL_HINT_RENDER_DRIVER,
    SDL_HINT_VIDEO_ALLOW_SCREENSAVER,
    SDL_HINT_MOUSE_TOUCH_EVENTS,
    SDL_HINT_TOUCH_MOUSE_EVENTS,
    SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR,
    SDL_HINT_WINDOWS_DPI_AWARENESS,
    SDL_HINT_MAC_BACKGROUND_APP,
    SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK,
    SDL_HINT_VIDEO_CURSES_SIMPLE_CHARACTERS,
    SDL_HINT_ANDROID_BLOCK_ON_PAUSE,
    SDL_HINT_ANDROID_BLOCK_ON_PAUSE_PAUSEAUDIO,
    SDL_HINT_ANDROID_TRAP_BACK_BUTTON,
};

enum {
    SDL_DISABLE = 0,
    SDL_ENABLE = 1,
    SDL_QUERY = -1,
};

/* audio */
typedef Uint16 SDL_AudioFormat;
typedef enum SDL_Keymod SDL_Keymod;
enum SDL_AudioFormatEnum {
    SDL_AUDIO_U8 = 0x0008,
    SDL_AUDIO_S16LSB = 0x8010,
    SDL_AUDIO_S16SYS = SDL_AUDIO_S16LSB,
    SDL_AUDIO_S16MSB = 0x9010,
    SDL_AUDIO_F32LSB = 0x8120,
    SDL_AUDIO_F32SYS = SDL_AUDIO_F32LSB,
};
#define SDL_AUDIO_BITSIZE(x) ((x) & 0x00FF)
#define SDL_AUDIO_ISFLOAT(x) ((x) & 0x1000)
#define SDL_AUDIO_ISLITTLEENDIAN(x) ((x) & 0x8000)
#define SDL_AUDIO_ISUNSIGNED(x) (!((x) & 0x0008))

enum SDL_AudioDeviceStatus {
    SDL_AUDIO_STOPPED = 0,
    SDL_AUDIO_PLAYING = 1,
};
#define SDL_AUDIO_PAUSED SDL_AUDIO_PLAYING

typedef struct SDL_AudioSpec SDL_AudioSpec;
typedef void (*SDL_AudioCallback)(void *userdata, Uint8 *stream, int len);
struct SDL_AudioSpec {
    int freq;
    SDL_AudioFormat format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;
    Uint16 padding;
    Uint32 size;
    SDL_AudioCallback callback;
    void *userdata;
};

/* rwops */
struct SDL_RWops;

/* syswm */
typedef struct SDL_SysWMinfo SDL_SysWMinfo;
struct SDL_SysWMinfo {
    int version;
    void *window;
};


/* events */
struct SDL_Keysym {
    SDL_Scancode scancode;
    SDL_Keycode sym;
    Uint16 mod;
    Uint32 fps;
};
typedef struct SDL_Keysym SDL_Keysym;

typedef struct SDL_KeyboardEvent SDL_KeyboardEvent;
struct SDL_KeyboardEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint8 state;
    Uint8 repeat;
    Uint8 padding2;
    Uint8 padding3;
    struct SDL_Keysym keysym;
};

typedef struct SDL_MouseMotionEvent SDL_MouseMotionEvent;
struct SDL_MouseMotionEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Uint32 state;
    Sint32 x, y;
    Sint32 xrel, yrel;
};

typedef struct SDL_MouseButtonEvent SDL_MouseButtonEvent;
struct SDL_MouseButtonEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Uint8 button;
    Uint8 state;
    Uint8 clicks;
    Uint8 padding1;
    Sint32 x, y;
};

typedef struct SDL_MouseWheelEvent SDL_MouseWheelEvent;
struct SDL_MouseWheelEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Sint32 x, y;
    Uint32 direction;
    float preciseX, preciseY;
    Sint32 mouseX, mouseY;
};

typedef struct SDL_TextInputEvent SDL_TextInputEvent;
struct SDL_TextInputEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    char text[32];
};

typedef struct SDL_TextEditingEvent SDL_TextEditingEvent;
struct SDL_TextEditingEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 start;
    Uint32 length;
    char text[32];
};

typedef struct SDL_TextEditingExtEvent SDL_TextEditingExtEvent;
struct SDL_TextEditingExtEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    char *text;
    Uint32 start;
    Uint32 length;
};

typedef struct SDL_WindowEvent SDL_WindowEvent;
struct SDL_WindowEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint8 event;
    Uint8 padding1, padding2, padding3;
    Sint32 data1, data2;
};

typedef struct SDL_UserEvent SDL_UserEvent;
struct SDL_UserEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Sint32 code;
    void *data1;
    void *data2;
};

typedef struct SDL_TouchFingerEvent SDL_TouchFingerEvent;
struct SDL_TouchFingerEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    SDL_TouchID touchId;
    SDL_FingerID fingerId;
    float x, y;
    float dx, dy;
    float pressure;
};

typedef struct SDL_QuitEvent SDL_QuitEvent;
struct SDL_QuitEvent {
    Uint32 type;
    Uint32 timestamp;
};

typedef struct SDL_DropEvent SDL_DropEvent;
struct SDL_DropEvent {
    Uint32 type;
    Uint32 timestamp;
    char *file;
};

typedef struct SDL_ControllerButtonEvent SDL_ControllerButtonEvent;
struct SDL_ControllerButtonEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 which;
    Uint8 button;
    Uint8 state;
    Uint8 padding1, padding2;
};

typedef struct SDL_ControllerAxisEvent SDL_ControllerAxisEvent;
struct SDL_ControllerAxisEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 which;
    Uint8 axis;
    Uint8 padding1, padding2, padding3;
    Sint16 value;
    Uint16 padding4;
};

typedef struct SDL_ControllerDeviceEvent SDL_ControllerDeviceEvent;
struct SDL_ControllerDeviceEvent {
    Uint32 type;
    Uint32 timestamp;
    Sint32 which;
};

typedef struct SDL_ControllerTouchpadEvent SDL_ControllerTouchpadEvent;
struct SDL_ControllerTouchpadEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 which;
    Sint32 touchpad;
    Sint32 finger;
    float x, y;
    float pressure;
};

typedef struct SDL_DisplayEvent SDL_DisplayEvent;
struct SDL_DisplayEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 display;
    Uint8 event;
    Uint8 padding1, padding2, padding3;
    Sint32 data1;
};

typedef union SDL_Event {
    Uint32 type;
    SDL_QuitEvent quit;
    SDL_KeyboardEvent key;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    SDL_TextInputEvent text;
    SDL_TextEditingEvent edit;
    SDL_TextEditingExtEvent editExt;
    SDL_TextEditingExtEvent editingExt;
    SDL_WindowEvent window;
    SDL_UserEvent user;
    SDL_TouchFingerEvent tfinger;
    SDL_DropEvent drop;
    SDL_ControllerButtonEvent cbutton;
    SDL_ControllerAxisEvent caxis;
    SDL_ControllerDeviceEvent cdevice;
    SDL_ControllerTouchpadEvent ctouchpad;
    SDL_DisplayEvent display;
} SDL_Event;

typedef struct SDL_DisplayMode SDL_DisplayMode;
struct SDL_DisplayMode {
    int displayIndex;
    int format;
    int w, h;
    int refresh_rate;
};

typedef int (*SDL_EventFilter)(void *userdata, SDL_Event *event);
typedef int (*SDL_HitTest)(SDL_Window *win, const SDL_Point *area, void *data);
typedef Uint32 (*SDL_TimerCallback)(Uint32 interval, void *param);

/* controller consts */
enum {
#if 0 /* SDL2 values (kept layout-compatible) */
    SDL_BUTTON_LEFT = 1,
#endif
    SDL_JOYSTICK_AXIS_MAX = 32767,
};

/* ----------------------------------------------------------------------------- */
/* Function API surface                                                          */
/* ----------------------------------------------------------------------------- */

/* init / misc */
#define SDL_INIT_TIMER  0x00000001u
#define SDL_INIT_AUDIO  0x00000010u
#define SDL_INIT_VIDEO  0x00000020u

int SDL_Init(Uint32 flags);
int SDL_InitSubSystem(Uint32 flags);
Uint32 SDL_WasInit(Uint32 flags);
void SDL_Quit(void);
void SDL_SetMainReady(void);
const char *SDL_GetError(void);
void SDL_ClearError(void);
int SDL_SetHint(int name, const char *value);
const char *SDL_GetHint(int name);
void SDL_PumpEvents(void);
void SDL_Delay(Uint32 ms);
Uint32 SDL_GetTicks(void);
Uint64 SDL_GetTicks64(void);
Sint64 SDL_GetPerformanceCounter(void);
Sint64 SDL_GetPerformanceFrequency(void);
SDL_bool SDL_TICKS_PASSED(Uint32 b, Uint32 a);
Sint32 SDL_TICKS_PASSED_CHECK(Uint32 newer, Uint32 older); /* (unused helper) */
SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_TimerCallback cb, void *param);
void SDL_RemoveTimer(SDL_TimerID id);
void SDL_EnableScreenSaver(void);
void SDL_free(void *p);
void *SDL_malloc(size_t n);

#define SDL_COMPILEDVERSION SDL_VERSIONNUM(2, 26, 2)
#define SDL_VERSIONNUM(X, Y, Z) ((X) * 1000 + (Y) * 100 + (Z))
#define SDL_VERSION_ATLEAST(X, Y, Z) (SDL_COMPILEDVERSION >= SDL_VERSIONNUM(X, Y, Z))

/* audio */
int SDL_OpenAudioDevice(const char *dev, int iscapture, const SDL_AudioSpec *want,
                        SDL_AudioSpec *got, int allowChange);
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev);
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause);
int SDL_GetAudioDeviceStatus(SDL_AudioDeviceID dev);

/* rwops */
SDL_RWops *SDL_RWFromFile(const char *file, const char *mode);
SDL_RWops *SDL_RWFromMem(void *mem, int size);
Sint64 SDL_RWsize(SDL_RWops *ctx);
int SDL_RWread(SDL_RWops *ctx, void *ptr, int size, int maxnum);
int SDL_RWseek(SDL_RWops *ctx, int offset, int whence);
int SDL_RWwrite(SDL_RWops *ctx, const void *ptr, int size, int num);
int SDL_RWclose(SDL_RWops *ctx);

/* surfaces */
SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int width, int height, int depth,
                                            Uint32 format);
SDL_Surface *SDL_CreateRGBSurfaceWithFormatFrom(void *pixels, int width, int height, int depth,
                                                int pitch, Uint32 format);
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth, Uint32 rmask,
                                  Uint32 gmask, Uint32 bmask, Uint32 amask);
void SDL_FreeSurface(SDL_Surface *surface);
SDL_Surface *SDL_ConvertSurfaceFormat(SDL_Surface *src, Uint32 fmt, Uint32 flags);
int SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst,
                    const SDL_Rect *dstrect);
int SDL_SetSurfaceBlendMode(SDL_Surface *surface, Uint32 mode);
int SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette);
SDL_Palette *SDL_AllocPalette(int ncolors);
void SDL_FreePalette(SDL_Palette *palette);
int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors, int firstcolor, int ncolors);
const char *SDL_GetPixelFormatName(Uint32 format);

/* events */
int SDL_PollEvent(SDL_Event *event);
int SDL_WaitEvent(SDL_Event *event);
int SDL_WaitEventTimeout(SDL_Event *event, int timeout);
int SDL_PushEvent(SDL_Event *event);
int SDL_RegisterEvents(int numevents);
void SDL_PumpEvents_void(void);
int SDL_AddEventWatch(SDL_EventFilter filter, void *userdata);
void SDL_DelEventWatch(SDL_EventFilter filter, void *userdata);
void SDL_SetEventFilter(SDL_EventFilter filter, void *userdata);
Uint8 SDL_GetEventState(Uint32 type);
int SDL_EventState(Uint32 type, int state); /* only queries/toggles */
void SDL_SetWindowsMessageHook(void *hook, void *userdata);

/* input */
const Uint8 *SDL_GetKeyboardState(int *numkeys);
SDL_Keymod SDL_GetModState(void);
void SDL_SetModState(SDL_Keymod mods);
SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode sc);
SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode key);
const char *SDL_GetKeyName(SDL_Keycode key);
const char *SDL_GetJoystickNameForIndex(int index); /* joystick/gamepad */
SDL_bool SDL_IsGameController(int index);
const char *SDL_GameControllerNameForIndex(int index);
const char *SDL_GameControllerGetStringForButton(int button);
typedef struct SDL_GameController SDL_GameController;
SDL_GameController *SDL_GameControllerOpen(int index);
void SDL_GameControllerClose(SDL_GameController *gc);
SDL_bool SDL_GameControllerGetAttached(SDL_GameController *gc);
void SDL_GameControllerEventState(int state);
typedef struct SDL_JoystickGUID {
    Uint8 data[16];
} SDL_JoystickGUID;
SDL_JoystickGUID SDL_JoystickGetDeviceGUID(int device);
void SDL_JoystickGetGUIDString(SDL_JoystickGUID guid, char *buf, int size);
int SDL_NumJoysticks(void);
int SDL_GameControllerAddMapping(const char *mapping);
void SDL_CaptureMouse(SDL_bool enabled);
Uint32 SDL_GetMouseState(int *x, int *y);
Uint32 SDL_GetGlobalMouseState(int *x, int *y);
void SDL_StartTextInput(void);
void SDL_StopTextInput(void);
void SDL_SetTextInputRect(const SDL_Rect *rect);
int SDL_SetWindowHitTest(SDL_Window *window, SDL_HitTest cb, void *cb_data);

char *SDL_GetPrefPath(const char *org, const char *app);
char *SDL_GetBasePath(void);
void SDL_SetWindowInputFocus(SDL_Window *win);

/* clipboard */
int SDL_HasClipboardText(void);
char *SDL_GetClipboardText(void);
int SDL_SetClipboardText(const char *text);

/* urls */
int SDL_OpenURL(const char *url);

/* windows */
SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags) /* may be NULL outside canvas build */
;
void SDL_DestroyWindow(SDL_Window *window);
void SDL_SetWindowTitle(SDL_Window *win, const char *title);
void SDL_GetWindowSize(SDL_Window *win, int *w, int *h);
void SDL_SetWindowSize(SDL_Window *win, int w, int h);
void SDL_GetWindowPosition(SDL_Window *win, int *x, int *y);
void SDL_SetWindowPosition(SDL_Window *win, int x, int y);
void SDL_SetWindowSizePrefix(SDL_Window *win, int w, int h, void *unused);
void SDL_ShowWindow(SDL_Window *win);
void SDL_HideWindow(SDL_Window *win);
void SDL_RaiseWindow(SDL_Window *win);
void SDL_MinimizeWindow(SDL_Window *win);
void SDL_MaximizeWindow(SDL_Window *win);
void SDL_RestoreWindow(SDL_Window *win);
Uint32 SDL_GetWindowFlags(SDL_Window *win);
SDL_WindowID SDL_GetWindowID(SDL_Window *win);
void SDL_SetWindowResizable(SDL_Window *win, SDL_bool resizable);
void SDL_SetWindowMinimumSize(SDL_Window *win, int w, int h);
void SDL_SetWindowMaximumSize(SDL_Window *win, int w, int h);
void SDL_SetWindowFullscreen(SDL_Window *win, Uint32 flags);
void SDL_SetWindowIcon(SDL_Window *win, SDL_Surface *icon);
int SDL_GetWindowDisplayIndex(SDL_Window *win);
int SDL_GetWindowBordersSize(SDL_Window *win, int *top, int *left, int *bottom, int *right);
int SDL_GetDisplayUsableBounds(int displayIndex, SDL_Rect *r);
int SDL_GetDisplayBounds(int displayIndex, SDL_Rect *r);
int SDL_GetNumVideoDisplays(void);
int SDL_GetDesktopDisplayMode(int displayIndex, SDL_DisplayMode *dm);
int SDL_GetCurrentDisplayMode(int displayIndex, SDL_DisplayMode *dm);
int SDL_GetDisplayMode(int displayIndex, int index, SDL_DisplayMode *dm);
int SDL_GetDisplayDPI(int displayIndex, float *dpi, float *dpiX, float *dpiY);
const char *SDL_GetCurrentVideoDriver(void);
SDL_bool SDL_GetWindowWMInfo(SDL_Window *win, void *info);

/* renderer */
SDL_Renderer *SDL_CreateRenderer(SDL_Window *win, int index, Uint32 flags);
void SDL_DestroyRenderer(SDL_Renderer *r);
void SDL_DestroyTexture(SDL_Texture *t);
int SDL_GetRendererOutputSize(SDL_Renderer *r, int *w, int *h);
SDL_bool SDL_GetRendererInfo(SDL_Renderer *r, SDL_RendererInfo *info);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, Uint32 format, int access, int w, int h);
SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *r, SDL_Surface *surface);
int SDL_QueryTexture(SDL_Texture *tex, Uint32 *format, int *access, int *w, int *h);
int SDL_SetRenderTarget(SDL_Renderer *r, SDL_Texture *t);
SDL_Texture *SDL_GetRenderTarget(SDL_Renderer *r);
int SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 R, Uint8 G, Uint8 B, Uint8 A);
int SDL_SetRenderDrawBlendMode(SDL_Renderer *r, Uint32 mode);
int SDL_RenderClear(SDL_Renderer *r);
int SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *srcrect,
                   const SDL_Rect *dstrect);
int SDL_RenderCopyExPort(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *srcrect,
                         const SDL_Rect *dstrect, double angle, void *center, int flip);
int SDL_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rect);
int SDL_RenderDrawRect(SDL_Renderer *r, const SDL_Rect *rect);
int SDL_RenderDrawLine(SDL_Renderer *r, int x1, int y1, int x2, int y2);
int SDL_RenderDrawLines(SDL_Renderer *r, const SDL_Point *points, int count);
int SDL_RenderSetClipRect(SDL_Renderer *r, const SDL_Rect *rect);
int SDL_RenderPresent(SDL_Renderer *r);
int SDL_SetTextureColorMod(SDL_Texture *t, Uint8 r, Uint8 g, Uint8 b);
int SDL_SetTextureAlphaMod(SDL_Texture *t, Uint8 a);
int SDL_SetTextureBlendMode(SDL_Texture *t, Uint32 blendMode);
int SDL_SetTextureScaleMode(SDL_Texture *t, Uint32 scaleMode);
int SDL_RenderSetIntegerScale(SDL_Renderer *r, int enabled);

/* render text utils (sealcurses remits, no-op here) */
enum {
    SDL_TEXT_ATTRIBUTE_BOLD = 1,
    SDL_TEXT_ATTRIBUTE_ITALIC = 2,
    SDL_TEXT_ATTRIBUTE_UNDERLINE = 4,
};

/* cursors */
SDL_Cursor *SDL_CreateSystemCursor(int id);
void SDL_FreeCursor(SDL_Cursor *cursor);
void SDL_SetCursor(SDL_Cursor *cursor);
SDL_bool SDL_IsCursorVisibleState(void);

/* misc */
int SDL_OpenURLCompat(const char *url);
#define SDL_OpenURL SDL_OpenURLCompat

/* OS-specific (unused in canvas build): */
#if !defined (LAGRANGE_CANVAS_BUILD)
typedef void *SDL_sys;
#endif

/* gamepad constants (SDL2 values) */
enum {
    SDL_CONTROLLER_BUTTON_A = 0,
    SDL_CONTROLLER_BUTTON_B = 1,
    SDL_CONTROLLER_BUTTON_X = 2,
    SDL_CONTROLLER_BUTTON_Y = 3,
    SDL_CONTROLLER_BUTTON_BACK = 4,
    SDL_CONTROLLER_BUTTON_GUIDE = 5,
    SDL_CONTROLLER_BUTTON_START = 6,
    SDL_CONTROLLER_BUTTON_LEFTSTICK = 7,
    SDL_CONTROLLER_BUTTON_RIGHTSTICK = 8,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER = 9,
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER = 10,
    SDL_CONTROLLER_BUTTON_DPAD_UP = 11,
    SDL_CONTROLLER_BUTTON_DPAD_DOWN = 12,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT = 13,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT = 14,
    SDL_CONTROLLER_AXIS_LEFTX = 0,
    SDL_CONTROLLER_AXIS_LEFTY = 1,
    SDL_CONTROLLER_AXIS_RIGHTY = 3,
    SDL_CONTROLLER_AXIS_TRIGGERRIGHT = 5,
};

/* joystick / gamepad types */
typedef struct SDL_Joystick SDL_Joystick;
typedef struct SDL_GameController SDL_GameController;

/* main */
#define SDL_MAIN_HANDLED 1
int SDL_main(int argc, char *argv[]);

/* directions for wheel (SDL 2.0.18+) */
enum SDL_MouseWheelDirection {
    SDL_MOUSEWHEEL_NORMAL = 0,
    SDL_MOUSEWHEEL_FLIPPED = 1,
};

/* canvas-host-only introspection (not part of the SDL surface) */
Uint64 presentCount_canvas(void);
void captureRender_canvas(SDL_Renderer *, const char *path);
int windowCount_canvas(void);
SDL_Renderer *rendererOfWindow_canvas(int index);
void setPresentHook_canvas(void (*cb)(int winIndex), void *unused);
void setPumpHook_canvas(void (*cb)(void), void *unused);
void setCursorHook_canvas(void (*cb)(int cursorId, void *unused), void *unused);
const Uint8 *canvasPixels_canvas(int winIndex, int *w, int *h, int *pitch);
