/* Minimal stand-in for the SDL3 API this port uses - for headless logic testing only. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint8_t Uint8; typedef uint16_t Uint16; typedef uint32_t Uint32; typedef uint64_t Uint64;
typedef int32_t Sint32;
typedef struct SDL_Window SDL_Window; typedef struct SDL_Renderer SDL_Renderer; typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_AudioStream SDL_AudioStream;
typedef Uint32 SDL_InitFlags; typedef Uint32 SDL_WindowFlags; typedef int SDL_PixelFormat; typedef int SDL_TextureAccess;
typedef int SDL_ScaleMode; typedef int SDL_RendererLogicalPresentation; typedef Uint32 SDL_Keycode; typedef Uint32 SDL_AudioFormat;
typedef Uint32 SDL_AudioDeviceID; typedef Uint32 SDL_MouseButtonFlags;
#define SDL_INIT_VIDEO 0x20u
#define SDL_INIT_AUDIO 0x10u
#define SDL_WINDOW_RESIZABLE 0x20u
#define SDL_PIXELFORMAT_ARGB8888 1
#define SDL_PIXELFORMAT_INDEX8 2
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_SCALEMODE_NEAREST 0
#define SDL_AUDIO_U8 0x0008u
#define SDL_AUDIO_BYTESIZE(x) (((x) & 0xFFu) / 8)
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK 0xFFFFFFFFu
#define SDL_BUTTON_LMASK 1u
#define SDLK_ESCAPE 0x1bu
#define SDLK_RETURN 0x0du
#define SDLK_KP_ENTER 0x0eu
#define SDLK_BACKSPACE 0x08u
#define SDLK_F11 0x4000003Au
typedef enum {
    SDL_SCANCODE_UNKNOWN = 0, SDL_SCANCODE_A = 4, SDL_SCANCODE_D = 7, SDL_SCANCODE_F1 = 58, SDL_SCANCODE_F2 = 59, SDL_SCANCODE_F3 = 60, SDL_SCANCODE_F4 = 61,
    SDL_SCANCODE_F5 = 62, SDL_SCANCODE_F6 = 63, SDL_SCANCODE_F7 = 64, SDL_SCANCODE_F8 = 65,
    SDL_SCANCODE_M = 16, SDL_SCANCODE_N = 17, SDL_SCANCODE_S = 22,
    SDL_SCANCODE_W = 26, SDL_SCANCODE_X = 27, SDL_SCANCODE_RETURN = 40, SDL_SCANCODE_KP_ENTER = 88, SDL_SCANCODE_ESCAPE = 41,
    SDL_SCANCODE_SPACE = 44, SDL_SCANCODE_RIGHT = 79, SDL_SCANCODE_LEFT = 80,
    SDL_SCANCODE_DOWN = 81, SDL_SCANCODE_UP = 82, SDL_SCANCODE_LCTRL = 224, SDL_SCANCODE_RCTRL = 228,
    SDL_NUM_SCANCODES = 512
} SDL_Scancode;
enum { SDL_EVENT_QUIT = 0x100, SDL_EVENT_KEY_DOWN = 0x300, SDL_EVENT_TEXT_INPUT = 0x303,
       SDL_EVENT_MOUSE_MOTION = 0x400, SDL_EVENT_MOUSE_BUTTON_DOWN = 0x401,
       SDL_EVENT_WINDOW_FOCUS_GAINED = 0x20d, SDL_EVENT_WINDOW_FOCUS_LOST = 0x20e };
typedef struct { Uint8 r, g, b, a; } SDL_Color;
typedef struct { int ncolors; SDL_Color *colors; } SDL_Palette;
typedef struct SDL_Surface { SDL_PixelFormat format; int w, h, pitch; void *pixels; SDL_Palette *pal; } SDL_Surface;
typedef struct { SDL_AudioFormat format; int channels; int freq; } SDL_AudioSpec;
typedef struct { Uint32 type; Uint32 pad; Uint64 ts; Uint32 wid, which; Uint32 scancode; SDL_Keycode key; } SDL_KeyboardEvent;
typedef struct { Uint32 type; Uint32 pad; Uint64 ts; Uint32 wid, which; Uint8 button, down; Uint8 clicks, pad2; float x, y; } SDL_MouseButtonEvent;
typedef struct { Uint32 type; Uint32 pad; Uint64 ts; Uint32 wid, which; Uint32 state; float x, y, xrel, yrel; } SDL_MouseMotionEvent;
typedef struct { Uint32 type; Uint32 pad; Uint64 ts; Uint32 wid; const char *text; } SDL_TextInputEvent;
typedef union SDL_Event { Uint32 type; SDL_KeyboardEvent key; SDL_MouseButtonEvent button; SDL_MouseMotionEvent motion; SDL_TextInputEvent text; Uint8 padding[128]; } SDL_Event;
typedef struct { int x, y, w, h; } SDL_Rect; typedef struct { float x, y, w, h; } SDL_FRect;
bool SDL_Init(SDL_InitFlags); bool SDL_InitSubSystem(SDL_InitFlags); void SDL_Quit(void); const char *SDL_GetError(void);
void SDL_Log(const char *fmt, ...);
bool SDL_CreateWindowAndRenderer(const char *, int, int, SDL_WindowFlags, SDL_Window **, SDL_Renderer **);
bool SDL_SetWindowMinimumSize(SDL_Window *, int, int);
bool SDL_MaximizeWindow(SDL_Window *);
bool SDL_RestoreWindow(SDL_Window *);
bool SDL_SetWindowFullscreen(SDL_Window *, bool);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *, SDL_PixelFormat, SDL_TextureAccess, int, int);
bool SDL_SetTextureScaleMode(SDL_Texture *, SDL_ScaleMode);
bool SDL_UpdateTexture(SDL_Texture *, const SDL_Rect *, const void *, int);
bool SDL_GetRenderOutputSize(SDL_Renderer *, int *, int *);
bool SDL_SetRenderDrawColor(SDL_Renderer *, Uint8, Uint8, Uint8, Uint8);
bool SDL_RenderClear(SDL_Renderer *);
bool SDL_RenderFillRect(SDL_Renderer *, const SDL_FRect *);
bool SDL_RenderTexture(SDL_Renderer *, SDL_Texture *, const SDL_FRect *, const SDL_FRect *);
bool SDL_RenderTextureTiled(SDL_Renderer *, SDL_Texture *, const SDL_FRect *, float, const SDL_FRect *);
bool SDL_RenderPresent(SDL_Renderer *);
void SDL_DestroyTexture(SDL_Texture *); void SDL_DestroyRenderer(SDL_Renderer *); void SDL_DestroyWindow(SDL_Window *);
bool SDL_PollEvent(SDL_Event *); Uint64 SDL_GetTicks(void); void SDL_Delay(Uint32);
const bool *SDL_GetKeyboardState(int *);
typedef unsigned short SDL_Keymod;
#define SDL_KMOD_CAPS 0x2000u
SDL_Keymod SDL_GetModState(void);
bool SDL_SetWindowRelativeMouseMode(SDL_Window *, bool);
void SDL_WarpMouseInWindow(SDL_Window *, float, float);
bool SDL_GetWindowRelativeMouseMode(SDL_Window *);
unsigned int SDL_GetRelativeMouseState(float *, float *);
void SDL_free(void *);
typedef unsigned int SDL_AudioDeviceID;
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK 0xFFFFFFFFu
SDL_AudioDeviceID SDL_OpenAudioDevice(SDL_AudioDeviceID, const SDL_AudioSpec *);
SDL_AudioStream *SDL_CreateAudioStream(const SDL_AudioSpec *, const SDL_AudioSpec *);
bool SDL_BindAudioStream(SDL_AudioDeviceID, SDL_AudioStream *);
bool SDL_ClearAudioStream(SDL_AudioStream *);
bool SDL_SetAudioStreamFormat(SDL_AudioStream *, const SDL_AudioSpec *, const SDL_AudioSpec *);
bool SDL_RaiseWindow(SDL_Window *);
#define SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED "SDL_WINDOW_ACTIVATE_WHEN_RAISED"
#define SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN "SDL_WINDOW_ACTIVATE_WHEN_SHOWN"
#define SDL_HINT_FORCE_RAISEWINDOW "SDL_FORCE_RAISEWINDOW"
typedef enum { SDL_FLASH_BRIEFLY = 1 } SDL_FlashOperation;
bool SDL_SetHint(const char *, const char *);
bool SDL_ShowWindow(SDL_Window *);
bool SDL_SetWindowFocusable(SDL_Window *, bool);
bool SDL_FlashWindow(SDL_Window *, SDL_FlashOperation);
typedef struct SDL_Gamepad SDL_Gamepad;
typedef int SDL_JoystickID;
#define SDL_INIT_GAMEPAD 0x2000u
#define SDL_EVENT_GAMEPAD_ADDED 1619
#define SDL_EVENT_GAMEPAD_REMOVED 1620
typedef enum { SDL_GAMEPAD_AXIS_LEFTX=0, SDL_GAMEPAD_AXIS_LEFTY=1,
  SDL_GAMEPAD_AXIS_RIGHTX=2, SDL_GAMEPAD_AXIS_RIGHTY=3 } SDL_GamepadAxis;
typedef enum { SDL_GAMEPAD_BUTTON_SOUTH=0, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
  SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_DPAD_UP,
  SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
  SDL_GAMEPAD_BUTTON_BACK } SDL_GamepadButton;
SDL_JoystickID *SDL_GetGamepads(int *);
SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID);
void SDL_CloseGamepad(SDL_Gamepad *);
bool SDL_GamepadConnected(SDL_Gamepad *);
const char *SDL_GetGamepadName(SDL_Gamepad *);
short SDL_GetGamepadAxis(SDL_Gamepad *, SDL_GamepadAxis);
bool SDL_GetGamepadButton(SDL_Gamepad *, SDL_GamepadButton);
bool SDL_HideCursor(void);
bool SDL_ShowCursor(void);
SDL_MouseButtonFlags SDL_GetMouseState(float *, float *);
SDL_Window *SDL_GetKeyboardFocus(void);
bool SDL_StartTextInput(SDL_Window *); bool SDL_StopTextInput(SDL_Window *);
Sint32 SDL_rand(Sint32 n);
SDL_Surface *SDL_LoadBMP(const char *); SDL_Palette *SDL_GetSurfacePalette(SDL_Surface *); void SDL_DestroySurface(SDL_Surface *);
bool SDL_LoadWAV(const char *, SDL_AudioSpec *, Uint8 **, Uint32 *);
SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID, const SDL_AudioSpec *, void *, void *);
bool SDL_PutAudioStreamData(SDL_AudioStream *, const void *, int); bool SDL_FlushAudioStream(SDL_AudioStream *);
bool SDL_ResumeAudioStreamDevice(SDL_AudioStream *); int SDL_GetAudioStreamQueued(SDL_AudioStream *);
void SDL_DestroyAudioStream(SDL_AudioStream *);
typedef unsigned int SDL_DisplayID;
bool SDL_SetRenderVSync(SDL_Renderer *, int);
bool SDL_SyncWindow(SDL_Window *);
SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window *);
SDL_DisplayID SDL_GetPrimaryDisplay(void);
bool SDL_GetDisplayUsableBounds(SDL_DisplayID, SDL_Rect *);
bool SDL_SetWindowPosition(SDL_Window *, int, int);
bool SDL_SetWindowSize(SDL_Window *, int, int);
bool SDL_GetWindowSize(SDL_Window *, int *, int *);
void SDL_PumpEvents(void);
#define SDL_WINDOW_MAXIMIZED 0x80u
#define SDL_WINDOW_INPUT_FOCUS 0x200u
#define SDL_WINDOW_MOUSE_FOCUS 0x400u
SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *);

/* folder picker, message box, prefs and DPI - enough of them for the headless build to link */
#define SDLCALL
typedef void (SDLCALL *SDL_DialogFileCallback)(void *, const char * const *, int);
void SDL_ShowOpenFolderDialog(SDL_DialogFileCallback, void *, SDL_Window *, const char *, bool);
bool SDL_ShowSimpleMessageBox(Uint32, const char *, const char *, SDL_Window *);
#define SDL_MESSAGEBOX_INFORMATION 0x00000040u
char *SDL_GetPrefPath(const char *, const char *);
const char *SDL_GetBasePath(void);
const char *SDL_GetCurrentVideoDriver(void);
float SDL_GetWindowPixelDensity(SDL_Window *);
#define SDL_WINDOW_HIGH_PIXEL_DENSITY 0x0000000000002000ULL
