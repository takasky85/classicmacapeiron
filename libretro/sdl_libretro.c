/*
 * sdl_libretro.c - the SDL3 calls the game makes, answered by libretro instead of by SDL.
 *
 * The game uses eighty SDL functions and most of them are windowing and device management that a
 * core has no business doing: a frontend owns the window, the vsync, the fullscreen state and the
 * audio device. What is left is a software renderer writing into one buffer, a clock, a keyboard,
 * a pointer, a gamepad and a sound mixer - all of which libretro provides.
 *
 * So rather than rewrite platform.c for libretro, this answers the SDL calls it already makes.
 * The same trick the headless test harness in tools/test uses, which is why the SDL header is
 * shared with it: CI compiles the game against that header on every push, so it cannot drift.
 *
 * Nothing in src/ changes to build the core.
 */
#include <SDL3/SDL.h>
#include "core.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the renderer ---------------------------------------------------------------------------- */
struct SDL_Texture { int w, h; uint32_t *px; };
static uint32_t outbuf[LR_W * LR_H];
static uint32_t drawcol = 0xFF000000u;

bool SDL_CreateWindowAndRenderer(const char *t, int w, int h, SDL_WindowFlags f,
                                 SDL_Window **win, SDL_Renderer **ren)
{ (void)t; (void)w; (void)h; (void)f; *win = (SDL_Window *)1; *ren = (SDL_Renderer *)1; return true; }

bool SDL_GetRenderOutputSize(SDL_Renderer *r, int *w, int *h)
{ (void)r; *w = LR_W; *h = LR_H; return true; }

bool SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 R, Uint8 G, Uint8 B, Uint8 A)
{ (void)r; (void)A; drawcol = 0xFF000000u | ((uint32_t)R << 16) | ((uint32_t)G << 8) | B; return true; }

SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, SDL_PixelFormat f, SDL_TextureAccess a, int w, int h)
{
    (void)r; (void)f; (void)a;
    SDL_Texture *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->w = w; t->h = h; t->px = calloc((size_t)w * h, 4);
    return t;
}
bool SDL_SetTextureScaleMode(SDL_Texture *t, SDL_ScaleMode m) { (void)t; (void)m; return true; }

bool SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *r, const void *p, int pitch)
{
    (void)r;
    for (int y = 0; y < t->h; y++)
        memcpy(t->px + (size_t)y * t->w, (const char *)p + (size_t)y * pitch, (size_t)t->w * 4);
    return true;
}

bool SDL_RenderClear(SDL_Renderer *r)
{ (void)r; for (int i = 0; i < LR_W * LR_H; i++) outbuf[i] = drawcol; return true; }

bool SDL_RenderFillRect(SDL_Renderer *r, const SDL_FRect *d)
{
    (void)r;
    for (int y = (int)d->y; y < (int)(d->y + d->h); y++)
        for (int x = (int)d->x; x < (int)(d->x + d->w); x++)
            if (x >= 0 && y >= 0 && x < LR_W && y < LR_H) outbuf[y * LR_W + x] = drawcol;
    return true;
}

bool SDL_RenderTexture(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *s, const SDL_FRect *d)
{
    (void)r; (void)s;
    for (int y = (int)d->y; y < (int)(d->y + d->h); y++)
        for (int x = (int)d->x; x < (int)(d->x + d->w); x++) {
            if (x < 0 || y < 0 || x >= LR_W || y >= LR_H) continue;
            int u = (int)((x - d->x) * t->w / d->w), v = (int)((y - d->y) * t->h / d->h);
            if (u < 0) u = 0;
            if (u >= t->w) u = t->w - 1;
            if (v < 0) v = 0;
            if (v >= t->h) v = t->h - 1;
            outbuf[y * LR_W + x] = t->px[v * t->w + u];
        }
    return true;
}

bool SDL_RenderTextureTiled(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *s, float sc,
                            const SDL_FRect *d)
{
    (void)r; (void)s;
    if (sc <= 0) sc = 1;
    for (int y = (int)d->y; y < (int)(d->y + d->h); y++)
        for (int x = (int)d->x; x < (int)(d->x + d->w); x++) {
            if (x < 0 || y < 0 || x >= LR_W || y >= LR_H) continue;
            int u = (int)floorf((x - d->x) / sc) % t->w, v = (int)floorf((y - d->y) / sc) % t->h;
            if (u < 0) u += t->w;
            if (v < 0) v += t->h;
            outbuf[y * LR_W + x] = t->px[v * t->w + u];
        }
    return true;
}

/* The frame is finished: hand it over and park until the frontend wants another. */
bool SDL_RenderPresent(SDL_Renderer *r) { (void)r; lr_frame(outbuf); return true; }

void SDL_DestroyTexture(SDL_Texture *t) { if (t) { free(t->px); free(t); } }
void SDL_DestroyRenderer(SDL_Renderer *r) { (void)r; }
void SDL_DestroyWindow(SDL_Window *w) { (void)w; }

/* ---- the clock ------------------------------------------------------------------------------- */
Uint64 SDL_GetTicks(void) { return lr_ticks(); }
void   SDL_Delay(Uint32 ms) { lr_delay(ms); }

/* ---- input ------------------------------------------------------------------------------------
 * The joypad is handed to the game as an SDL gamepad, so platform.c's own pad_move, pad_fire and
 * dead-zone handling work unchanged. In the menus, where there is nothing to steer, the same stick
 * drives a pointer instead - the game draws its own hand cursor at the mouse position, so a TV
 * remote gets something to aim with and the existing click handling does the rest.
 */
static bool  keys[SDL_NUM_SCANCODES];
static float ptr_x = LR_W / 2.0f, ptr_y = LR_H / 2.0f;
static bool  relative_mode;            /* true while the game is steering the shooter */
static bool  click_pending, click_was_down;
static float pending_x, pending_y;
static int   motion_dx, motion_dy;
static bool  motion_pending;

static void read_input(void)
{
    /* Once per frame, not once per poll. The game drains events with `while (SDL_PollEvent(&e))`,
     * and recomputing here would hand it a fresh mouse-motion event every time round that loop -
     * a drain that never drains, which would wedge the frontend solid. */
    static uint64_t seen = (uint64_t)-1;
    if (seen == lr_frame_no()) return;
    seen = lr_frame_no();

    const LrInput *in = lr_input();
    memset(keys, 0, sizeof keys);

    /* Escape and pause, so a pad can leave a screen without a keyboard. */
    keys[SDL_SCANCODE_ESCAPE] = in->button[RETRO_PAD_SELECT];
    keys[SDL_SCANCODE_RETURN] = in->button[RETRO_PAD_START];
    keys[SDL_SCANCODE_SPACE]  = in->button[RETRO_PAD_B] || in->button[RETRO_PAD_A];

    int dx = in->mdx, dy = in->mdy;         /* a real mouse, if the frontend has one */

    if (!relative_mode) {
        /* menus: the stick and the d-pad move the cursor */
        float sx = in->lx / 32767.0f, sy = in->ly / 32767.0f;
        if (fabsf(sx) < 0.2f) sx = 0;
        if (fabsf(sy) < 0.2f) sy = 0;
        if (in->button[RETRO_PAD_LEFT])  sx -= 1;
        if (in->button[RETRO_PAD_RIGHT]) sx += 1;
        if (in->button[RETRO_PAD_UP])    sy -= 1;
        if (in->button[RETRO_PAD_DOWN])  sy += 1;
        ptr_x += sx * 6.0f + dx;
        ptr_y += sy * 6.0f + dy;
        if (in->pointer) { ptr_x = (float)in->px; ptr_y = (float)in->py; }
        if (ptr_x < 0) ptr_x = 0;
        if (ptr_y < 0) ptr_y = 0;
        if (ptr_x > LR_W - 1) ptr_x = LR_W - 1;
        if (ptr_y > LR_H - 1) ptr_y = LR_H - 1;

        bool down = in->button[RETRO_PAD_A] || in->button[RETRO_PAD_B] ||
                    in->button[RETRO_PAD_START] || in->mouse_down || in->pointer;
        if (down && !click_was_down) {
            click_pending = true; pending_x = ptr_x; pending_y = ptr_y;
        }
        click_was_down = down;
        motion_pending = false;
    } else {
        /* play: the stick steers through the gamepad path, so only a real mouse or a dragged
         * finger produces motion here */
        static int last_px = -1, last_py = -1;
        if (in->pointer) {
            if (last_px >= 0) { dx += in->px - last_px; dy += in->py - last_py; }
            last_px = in->px; last_py = in->py;
        } else {
            last_px = last_py = -1;
        }
        motion_dx = dx; motion_dy = dy;
        motion_pending = (dx || dy);
        bool down = in->mouse_down;
        if (down && !click_was_down) { click_pending = true; pending_x = ptr_x; pending_y = ptr_y; }
        click_was_down = down;
    }
}

void SDL_PumpEvents(void) { read_input(); }

bool SDL_PollEvent(SDL_Event *e)
{
    read_input();
    if (motion_pending) {
        motion_pending = false;
        memset(e, 0, sizeof *e);
        e->type = SDL_EVENT_MOUSE_MOTION;
        e->motion.x = ptr_x; e->motion.y = ptr_y;
        e->motion.xrel = (float)motion_dx; e->motion.yrel = (float)motion_dy;
        return true;
    }
    if (click_pending) {
        click_pending = false;
        memset(e, 0, sizeof *e);
        e->type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        e->button.x = pending_x; e->button.y = pending_y;
        e->button.button = 1;
        return true;
    }
    return false;
}

const bool *SDL_GetKeyboardState(int *n) { if (n) *n = SDL_NUM_SCANCODES; return keys; }
SDL_MouseButtonFlags SDL_GetMouseState(float *x, float *y)
{ if (x) *x = ptr_x; if (y) *y = ptr_y; return click_was_down ? 1u : 0u; }
SDL_Window *SDL_GetKeyboardFocus(void) { return (SDL_Window *)1; }
SDL_Keymod SDL_GetModState(void) { return 0; }
bool SDL_SetWindowRelativeMouseMode(SDL_Window *w, bool on) { (void)w; relative_mode = on; return true; }
bool SDL_GetWindowRelativeMouseMode(SDL_Window *w) { (void)w; return relative_mode; }
void SDL_WarpMouseInWindow(SDL_Window *w, float x, float y) { (void)w; ptr_x = x; ptr_y = y; }
unsigned int SDL_GetRelativeMouseState(float *x, float *y) { if (x) *x = 0; if (y) *y = 0; return 0; }

/* The joypad, presented as one connected SDL gamepad. The list is allocated because SDL's is,
 * and the caller hands it back to SDL_free. */
SDL_JoystickID *SDL_GetGamepads(int *n)
{
    SDL_JoystickID *ids = malloc(2 * sizeof *ids);
    if (!ids) { if (n) *n = 0; return NULL; }
    ids[0] = 1; ids[1] = 0;
    if (n) *n = 1;
    return ids;
}
SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID i) { (void)i; return (SDL_Gamepad *)1; }
void SDL_CloseGamepad(SDL_Gamepad *g) { (void)g; }
bool SDL_GamepadConnected(SDL_Gamepad *g) { return g != NULL; }
const char *SDL_GetGamepadName(SDL_Gamepad *g) { (void)g; return "RetroPad"; }

short SDL_GetGamepadAxis(SDL_Gamepad *g, SDL_GamepadAxis a)
{
    (void)g;
    const LrInput *in = lr_input();
    switch (a) {
    case SDL_GAMEPAD_AXIS_LEFTX:  return in->lx;
    case SDL_GAMEPAD_AXIS_LEFTY:  return in->ly;
    case SDL_GAMEPAD_AXIS_RIGHTX: return in->rx;
    case SDL_GAMEPAD_AXIS_RIGHTY: return in->ry;
    default: return 0;
    }
}

bool SDL_GetGamepadButton(SDL_Gamepad *g, SDL_GamepadButton b)
{
    (void)g;
    const LrInput *in = lr_input();
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return in->button[RETRO_PAD_B];
    case SDL_GAMEPAD_BUTTON_EAST:  return in->button[RETRO_PAD_A];
    case SDL_GAMEPAD_BUTTON_WEST:  return in->button[RETRO_PAD_Y];
    case SDL_GAMEPAD_BUTTON_NORTH: return in->button[RETRO_PAD_X];
    case SDL_GAMEPAD_BUTTON_START: return in->button[RETRO_PAD_START];
    case SDL_GAMEPAD_BUTTON_BACK:  return in->button[RETRO_PAD_SELECT];
    case SDL_GAMEPAD_BUTTON_DPAD_UP:    return in->button[RETRO_PAD_UP];
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:  return in->button[RETRO_PAD_DOWN];
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:  return in->button[RETRO_PAD_LEFT];
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return in->button[RETRO_PAD_RIGHT];
    default: return false;
    }
}

/* ---- audio -------------------------------------------------------------------------------------
 * SDL mixed for us; here we do it. Every sound the game loads is 8-bit unsigned mono at 22255Hz,
 * but the format is read from the file rather than assumed, so a replacement sound pack at some
 * other rate still plays at the right pitch.
 */
struct SDL_AudioStream {
    Uint8 *data;
    int    len, cap;
    double pos;              /* in source frames */
    int    freq, channels, bytes;
    bool   playing;
};
#define MAX_STREAMS 24
static SDL_AudioStream *streams[MAX_STREAMS];
static int n_streams;

SDL_AudioDeviceID SDL_OpenAudioDevice(SDL_AudioDeviceID d, const SDL_AudioSpec *sp)
{ (void)d; (void)sp; return 1; }

SDL_AudioStream *SDL_CreateAudioStream(const SDL_AudioSpec *src, const SDL_AudioSpec *dst)
{
    (void)dst;
    SDL_AudioStream *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->freq = src && src->freq ? src->freq : 22255;
    s->channels = src && src->channels ? src->channels : 1;
    s->bytes = src ? (int)SDL_AUDIO_BYTESIZE(src->format) : 1;
    if (s->bytes < 1) s->bytes = 1;
    if (n_streams < MAX_STREAMS) streams[n_streams++] = s;
    return s;
}

bool SDL_BindAudioStream(SDL_AudioDeviceID d, SDL_AudioStream *s) { (void)d; (void)s; return true; }

bool SDL_SetAudioStreamFormat(SDL_AudioStream *s, const SDL_AudioSpec *src, const SDL_AudioSpec *dst)
{
    (void)dst;
    if (!s || !src) return true;
    s->freq = src->freq ? src->freq : s->freq;
    s->channels = src->channels ? src->channels : s->channels;
    s->bytes = (int)SDL_AUDIO_BYTESIZE(src->format);
    if (s->bytes < 1) s->bytes = 1;
    return true;
}

bool SDL_ClearAudioStream(SDL_AudioStream *s)
{ if (s) { s->len = 0; s->pos = 0; s->playing = false; } return true; }

bool SDL_PutAudioStreamData(SDL_AudioStream *s, const void *p, int n)
{
    if (!s || !p || n <= 0) return true;
    if (n > s->cap) {
        Uint8 *d = realloc(s->data, (size_t)n);
        if (!d) return false;
        s->data = d; s->cap = n;
    }
    memcpy(s->data, p, (size_t)n);
    s->len = n; s->pos = 0; s->playing = true;
    return true;
}

bool SDL_FlushAudioStream(SDL_AudioStream *s) { (void)s; return true; }

int SDL_GetAudioStreamQueued(SDL_AudioStream *s)
{
    if (!s || !s->playing) return 0;
    int frames = s->len / (s->channels * s->bytes);
    int left = frames - (int)s->pos;
    return left > 0 ? left * s->channels * s->bytes : 0;
}

void SDL_DestroyAudioStream(SDL_AudioStream *s)
{
    if (!s) return;
    for (int i = 0; i < n_streams; i++)
        if (streams[i] == s) { streams[i] = streams[--n_streams]; break; }
    free(s->data);
    free(s);
}

/* One sample from a stream's source data, as a signed 16-bit value. */
static int sample_at(const SDL_AudioStream *s, int frame, int ch)
{
    int i = frame * s->channels + (ch < s->channels ? ch : 0);
    if (s->bytes == 1) return ((int)s->data[i] - 128) << 8;        /* unsigned 8-bit, as the Mac's */
    const int16_t *p = (const int16_t *)(const void *)s->data;
    return p[i];
}

void lr_audio_render(int16_t *out, int frames)
{
    memset(out, 0, (size_t)frames * 2 * sizeof *out);
    for (int i = 0; i < n_streams; i++) {
        SDL_AudioStream *s = streams[i];
        if (!s || !s->playing || !s->data || s->len <= 0) continue;
        int total = s->len / (s->channels * s->bytes);
        double step = (double)s->freq / (double)LR_RATE;
        double pos = s->pos;
        for (int f = 0; f < frames; f++) {
            int a = (int)pos;
            if (a >= total - 1) { s->playing = false; break; }
            double frac = pos - a;
            for (int c = 0; c < 2; c++) {
                int s0 = sample_at(s, a, c), s1 = sample_at(s, a + 1, c);
                /* Three quarters, so a handful of overlapping effects have somewhere to go before
                 * the sum clips. SDL's own mixer left the same sort of headroom. */
                int v = out[f * 2 + c] + (int)((s0 + (s1 - s0) * frac) * 0.75);
                out[f * 2 + c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
            pos += step;
        }
        s->pos = pos;
        if ((int)s->pos >= total - 1) s->playing = false;
    }
}

/* ---- loading files ----------------------------------------------------------------------------
 * Only the two shapes this game's own extractor writes: 8-bit indexed BMPs and PCM WAVs.
 */
SDL_Surface *SDL_LoadBMP(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    Uint8 h[54];
    if (fread(h, 1, 54, f) != 54) { fclose(f); return NULL; }
    uint32_t off, hs, clr;
    int32_t w, ht;
    uint16_t bpp;
    memcpy(&off, h + 10, 4); memcpy(&hs, h + 14, 4);
    memcpy(&w, h + 18, 4);   memcpy(&ht, h + 22, 4);
    memcpy(&bpp, h + 28, 2); memcpy(&clr, h + 46, 4);
    if (bpp != 8 || hs < 40) { fclose(f); return NULL; }
    if (clr == 0) clr = 256;
    SDL_Surface *s = calloc(1, sizeof *s);
    if (!s) { fclose(f); return NULL; }
    s->format = SDL_PIXELFORMAT_INDEX8;
    s->w = w; s->h = ht < 0 ? -ht : ht; s->pitch = w;
    s->pixels = malloc((size_t)w * s->h);
    s->pal = calloc(1, sizeof(SDL_Palette));
    s->pal->ncolors = (int)clr;
    s->pal->colors = calloc(256, sizeof(SDL_Color));
    fseek(f, (long)(14 + hs), SEEK_SET);
    for (uint32_t i = 0; i < clr && i < 256; i++) {
        Uint8 q[4];
        if (fread(q, 1, 4, f) != 4) break;
        s->pal->colors[i].r = q[2]; s->pal->colors[i].g = q[1];
        s->pal->colors[i].b = q[0]; s->pal->colors[i].a = 255;
    }
    int rowb = (w + 3) & ~3;
    Uint8 *row = malloc((size_t)rowb);
    fseek(f, (long)off, SEEK_SET);
    for (int y = 0; y < s->h; y++) {
        if (fread(row, 1, (size_t)rowb, f) != (size_t)rowb) break;
        int dy = ht > 0 ? s->h - 1 - y : y;          /* bottom-up unless the height is negative */
        memcpy((Uint8 *)s->pixels + (size_t)dy * w, row, (size_t)w);
    }
    free(row);
    fclose(f);
    return s;
}

SDL_Palette *SDL_GetSurfacePalette(SDL_Surface *s) { return s ? s->pal : NULL; }
void SDL_DestroySurface(SDL_Surface *s)
{ if (!s) return; free(s->pixels); if (s->pal) { free(s->pal->colors); free(s->pal); } free(s); }

bool SDL_LoadWAV(const char *path, SDL_AudioSpec *sp, Uint8 **buf, Uint32 *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    Uint8 h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
    { fclose(f); return false; }
    int bits = 8;
    for (;;) {
        Uint8 ch[8];
        if (fread(ch, 1, 8, f) != 8) { fclose(f); return false; }
        uint32_t sz;
        memcpy(&sz, ch + 4, 4);
        if (!memcmp(ch, "fmt ", 4)) {
            Uint8 b[16];
            if (fread(b, 1, 16, f) != 16) { fclose(f); return false; }
            uint16_t chans, bps;
            uint32_t rate;
            memcpy(&chans, b + 2, 2); memcpy(&rate, b + 4, 4); memcpy(&bps, b + 14, 2);
            sp->channels = chans; sp->freq = (int)rate; bits = bps;
            sp->format = bits == 8 ? SDL_AUDIO_U8 : 0x8010u;      /* U8 or signed 16 little-endian */
            if (sz > 16) fseek(f, (long)(sz - 16), SEEK_CUR);
        } else if (!memcmp(ch, "data", 4)) {
            *buf = malloc(sz ? sz : 1);
            if (!*buf) { fclose(f); return false; }
            *len = (Uint32)fread(*buf, 1, sz, f);
            fclose(f);
            return true;
        } else {
            fseek(f, (long)(sz + (sz & 1)), SEEK_CUR);
        }
    }
}

/* ---- the rest: things a frontend owns, or that mean nothing here -------------------------------- */
bool SDL_Init(SDL_InitFlags f) { (void)f; return true; }
bool SDL_InitSubSystem(SDL_InitFlags f) { (void)f; return true; }
void SDL_Quit(void) { }
const char *SDL_GetError(void) { return "libretro"; }
void SDL_Log(const char *fmt, ...)
{
    char buf[1024];
    va_list a;
    va_start(a, fmt);
    vsnprintf(buf, sizeof buf, fmt, a);
    va_end(a);
    lr_log("%s", buf);
}
void SDL_free(void *p) { free(p); }
Sint32 SDL_rand(Sint32 n)
{ static unsigned seed = 12345; seed = seed * 1103515245u + 12345u; return n > 0 ? (Sint32)((seed >> 16) % (unsigned)n) : 0; }

bool SDL_SetWindowMinimumSize(SDL_Window *w, int a, int b) { (void)w; (void)a; (void)b; return true; }
bool SDL_MaximizeWindow(SDL_Window *w) { (void)w; return true; }
bool SDL_RestoreWindow(SDL_Window *w) { (void)w; return true; }
bool SDL_SetWindowFullscreen(SDL_Window *w, bool f) { (void)w; (void)f; return true; }
bool SDL_SetWindowPosition(SDL_Window *w, int x, int y) { (void)w; (void)x; (void)y; return true; }
bool SDL_SetWindowSize(SDL_Window *w, int x, int y) { (void)w; (void)x; (void)y; return true; }
bool SDL_GetWindowSize(SDL_Window *w, int *x, int *y) { (void)w; if (x) *x = LR_W; if (y) *y = LR_H; return true; }
bool SDL_ShowWindow(SDL_Window *w) { (void)w; return true; }
bool SDL_RaiseWindow(SDL_Window *w) { (void)w; return true; }
bool SDL_SetWindowFocusable(SDL_Window *w, bool b) { (void)w; (void)b; return true; }
bool SDL_FlashWindow(SDL_Window *w, SDL_FlashOperation o) { (void)w; (void)o; return true; }
bool SDL_SyncWindow(SDL_Window *w) { (void)w; return true; }
SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *w)
{ (void)w; return SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS; }
float SDL_GetWindowPixelDensity(SDL_Window *w) { (void)w; return 1.0f; }
SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window *w) { (void)w; return 1; }
SDL_DisplayID SDL_GetPrimaryDisplay(void) { return 1; }
bool SDL_GetDisplayUsableBounds(SDL_DisplayID d, SDL_Rect *r)
{ (void)d; if (r) { r->x = r->y = 0; r->w = LR_W; r->h = LR_H; } return true; }
bool SDL_SetRenderVSync(SDL_Renderer *r, int v) { (void)r; (void)v; return true; }
bool SDL_SetHint(const char *a, const char *b) { (void)a; (void)b; return true; }
bool SDL_HideCursor(void) { return true; }
bool SDL_ShowCursor(void) { return true; }
bool SDL_StartTextInput(SDL_Window *w) { (void)w; return true; }
bool SDL_StopTextInput(SDL_Window *w) { (void)w; return true; }
const char *SDL_GetCurrentVideoDriver(void) { return "libretro"; }
bool SDL_ShowSimpleMessageBox(Uint32 f, const char *t, const char *m, SDL_Window *w)
{ (void)f; (void)w; lr_log("%s - %s", t ? t : "", m ? m : ""); return true; }
void SDL_ShowOpenFolderDialog(SDL_DialogFileCallback cb, void *ud, SDL_Window *w,
                              const char *d, bool many)
{ (void)w; (void)d; (void)many; if (cb) cb(ud, NULL, -1); }   /* a frontend has its own browser */

/* The game writes its options and high scores beside the artwork; when that is somewhere
 * read-only, the frontend's save directory is the place for them. */
char *SDL_GetPrefPath(const char *org, const char *app)
{
    (void)org; (void)app;
    const char *d = lr_system_dir();
    size_t n = strlen(d) + 16;
    char *p = malloc(n);
    if (p) snprintf(p, n, "%s/apeiron/", d);
    return p;
}
const char *SDL_GetBasePath(void)
{
    static char p[1100];
    snprintf(p, sizeof p, "%s/apeiron/", lr_system_dir());
    return p;
}
