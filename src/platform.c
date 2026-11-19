/*
 * platform.c - the 8-bit indexed screen, the Mac colour-table operations, sprite/font/image drawing
 * and sound. The original draws into an 8-bit window and does its fades and flashes by rewriting the
 * monitor's colour table (SetEntries); this keeps that model, converting the indexed framebuffer to
 * ARGB only at present() time.
 */
#include "apeiron.h"

uint8_t fb[SCREEN_W * SCREEN_H];
Rgb table[256], shown[256];
const Rgb RGB_BLACK = { 0, 0, 0 };
const Rgb RGB_WHITE = { 0xFFFF, 0xFFFF, 0xFFFF };

static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture  *g_tex;
static uint32_t argb[SCREEN_W * SCREEN_H];
static bool button_event;
static int  click_x, click_y;
static bool have_click;
static int  last_scale = 1, last_gx, last_gy;
static bool audio_ok;

/* ---- game controllers (#14) -----------------------------------------------------------------
 * SDL opens whatever is plugged in; the first one that answers steers the shooter. The analogue
 * stick is read past a dead zone and the d-pad as a full deflection, so either works. */
static SDL_Gamepad *g_pad;

bool pad_present(void) { return g_pad != NULL; }
const char *pad_name(void) { return g_pad ? SDL_GetGamepadName(g_pad) : "none"; }

static void pad_rescan(void)
{
    if (g_pad && !SDL_GamepadConnected(g_pad)) { SDL_CloseGamepad(g_pad); g_pad = NULL; }
    if (g_pad) return;
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    if (ids) {
        for (int i = 0; i < n && !g_pad; i++) g_pad = SDL_OpenGamepad(ids[i]);
        SDL_free(ids);
    }
}

bool pad_move(float *x, float *y)
{
    *x = *y = 0;
    if (!g_pad || !opt_pad) return false;
    float dz = (float)opt_pad_deadzone / 100.0f;
    float ax = SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    float ay = SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
    if (ax > dz || ax < -dz) *x = ax;
    if (ay > dz || ay < -dz) *y = ay;
    if (SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))  *x = -1;
    if (SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) *x =  1;
    if (SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_UP))    *y = -1;
    if (SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))  *y =  1;
    return *x != 0 || *y != 0;
}

bool pad_fire(void)
{
    if (!g_pad || !opt_pad) return false;
    return SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_SOUTH) ||
           SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_EAST) ||
           SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_WEST) ||
           SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_NORTH);
}

bool pad_start(void)
{
    if (!g_pad || !opt_pad) return false;
    return SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_START);
}
static SDL_AudioDeviceID audio_dev;
static bool has_focus = true;
static char typed[16];
static int  ntyped;
static const Image *margin_pat;
static SDL_Texture *margin_tex;
static uint32_t margin_argb[256 * 256];
static bool    border_active;
static uint8_t border_idx[7];
static int      cursor_mode = CURSOR_NONE;
static int      cur_w, cur_h, cur_hx, cur_hy;
static uint32_t cur_px[32 * 32];          /* 0 = transparent, else 0xFFrrggbb */

/* ---- window / event pump ------------------------------------------------------------------- */
void plat_shutdown(int code)
{
    if (margin_tex) SDL_DestroyTexture(margin_tex);
    if (g_tex) SDL_DestroyTexture(g_tex);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    SDL_Quit();
    exit(code);
}

void window_set_maximized(bool on);

bool plat_init(const char *title)
{
    /* Windows will not let a new process steal the foreground unless it is asked to; these are
     * the three hints SDL offers for it, and they have to be set before the window exists. */
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "1");
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "1");
    SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return false; }
    if (SDL_InitSubSystem(SDL_INIT_GAMEPAD)) pad_rescan();
    audio_ok = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (audio_ok) {
        audio_dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
        if (!audio_dev) { audio_ok = false; SDL_Log("no audio device (%s)", SDL_GetError()); }
    }
    if (!audio_ok) SDL_Log("no audio (%s) - continuing silently", SDL_GetError());
    /* SDL_WINDOW_MAXIMIZED at creation is the only form of the request a window manager cannot
     * quietly drop, which asking an already-open window to maximize plainly can be. */
    SDL_WindowFlags wf = SDL_WINDOW_RESIZABLE | (opt_maximize ? SDL_WINDOW_MAXIMIZED : 0);
    if (!SDL_CreateWindowAndRenderer(title, SCREEN_W, SCREEN_H, wf, &g_win, &g_ren)) {
        SDL_Log("window creation failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(g_win, SCREEN_W, SCREEN_H);
    /* Every frame is a full repaint of a 256-colour screen, and the psychedelic mushroom swaps the
     * whole colour table three times a second, so a torn frame shows half of each table. Waiting
     * for the vertical blank is what the original got from the Mac's own screen. */
    SDL_SetRenderVSync(g_ren, 1);
    SDL_StartTextInput(g_win);                /* the title screen listens for its letter keys */
    SDL_HideCursor();                         /* [HandleInitializations]: the game draws its own */
    SDL_ShowWindow(g_win);                    /* come to the front and take the keyboard */
    SDL_RaiseWindow(g_win);
    SDL_SetWindowFocusable(g_win, true);
    SDL_FlashWindow(g_win, SDL_FLASH_BRIEFLY);
    if (opt_maximize) window_set_maximized(true);
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    SDL_SetTextureScaleMode(g_tex, SDL_SCALEMODE_NEAREST);
    return true;
}

void pump(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) plat_shutdown(0);
        if (e.type == SDL_EVENT_GAMEPAD_ADDED || e.type == SDL_EVENT_GAMEPAD_REMOVED) pad_rescan();
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            button_event = true; have_click = true;
            click_x = (int)((e.button.x - last_gx) / last_scale);
            click_y = (int)((e.button.y - last_gy) / last_scale);
        }
        if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) has_focus = false;
        if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) has_focus = true;
        if (e.type == SDL_EVENT_TEXT_INPUT && ntyped < (int)sizeof typed - 1)
            for (const char *t = e.text.text; *t; t++)
                if ((unsigned char)*t >= 32 && ntyped < (int)sizeof typed - 1) typed[ntyped++] = *t;
        if (e.type == SDL_EVENT_KEY_DOWN) {
            if (e.key.key == SDLK_F11) toggle_fullscreen();
            else button_event = true;
        }
    }
}

bool button(void) { pump(); bool b = button_event; button_event = false; return b; }

bool window_focused(void) { pump(); return has_focus; }

/* [CapsLockDown] is GetKeys + BitTst(.., 0x3e). SDL reports Caps Lock as a modifier that is on while
 * the light is on, which is the same thing the original is testing for. */
bool caps_lock_down(void)
{
    pump();
    return (SDL_GetModState() & SDL_KMOD_CAPS) != 0;
}

bool take_key(char *c)          /* one typed character, oldest first */
{
    pump();
    if (!ntyped) return false;
    *c = typed[0];
    memmove(typed, typed + 1, (size_t)(--ntyped));
    return true;
}

bool key_down(SDL_Scancode sc)
{
    pump();
    int n = 0;
    const bool *st = SDL_GetKeyboardState(&n);
    return st && (int)sc < n && st[sc];
}

/* Any key or mouse button at all - the title screen's idle timer only needs to know that
 * somebody is there, not what they pressed. */
bool any_input(void)
{
    pump();
    int n = 0;
    const bool *st = SDL_GetKeyboardState(&n);
    for (int i = 0; st && i < n; i++) if (st[i]) return true;
    return SDL_GetMouseState(NULL, NULL) != 0;
}

void mouse_pos(int *x, int *y)
{
    float mx, my;
    SDL_GetMouseState(&mx, &my);
    *x = (int)((mx - last_gx) / last_scale);
    *y = (int)((my - last_gy) / last_scale);
}

void flush_clicks(void)         /* FlushEvents(everyEvent, 0) */
{
    pump();
    have_click = false;
    button_event = false;
    ntyped = 0;
}

/* The shooter used to be steered by the absolute pointer, which stopped dead the moment the pointer
 * left the 432-wide playfield - easy to do in a maximised window, and it read as the mouse sticking.
 * During a game the pointer is taken over instead and read as movement, so it has no edges at all. */
void mouse_relative(bool on)
{
    SDL_SetWindowRelativeMouseMode(g_win, on);
    if (!on) SDL_GetRelativeMouseState(NULL, NULL);    /* drop whatever built up */
}

/* The accumulated movement since the last call, in 640x480 screen pixels. */
void mouse_delta(float *dx, float *dy)
{
    pump();
    float rx = 0, ry = 0;
    SDL_GetRelativeMouseState(&rx, &ry);
    float s = last_scale > 0 ? (float)last_scale : 1.0f;
    *dx = rx / s;
    *dy = ry / s;
}

bool take_click_only(void)      /* Button(): the mouse, not the keyboard */
{
    pump();
    bool b = have_click;
    have_click = false;
    return b;
}

bool take_click(int *x, int *y)
{
    pump();
    if (!have_click) return false;
    have_click = false; button_event = false;
    if (x) *x = click_x;
    if (y) *y = click_y;
    return true;
}

/* ---- presentation -------------------------------------------------------------------------- */
static void set_draw_color(const Rgb *c)
{
    SDL_SetRenderDrawColor(g_ren, (Uint8)(c->r >> 8), (Uint8)(c->g >> 8), (Uint8)(c->b >> 8), 255);
}

/* crsr 128 as tools/extract_assets.py wrote it: the colours are kept as RGB so the hand looks the
 * same over the title screen's colour table and the game's. */
bool cursor_load(const char *dir)
{
    char p[1024], line[64];
    snprintf(p, sizeof p, "%s/cursor_hand.cur", dir);
    FILE *f = fopen(p, "r");
    if (!f) { SDL_Log("cannot open %s", p); return false; }
    int ncol = 0;
    uint32_t col[32];
    bool ok = fscanf(f, "CURSOR %d %d %d %d", &cur_w, &cur_h, &cur_hx, &cur_hy) == 4;
    ok = ok && fscanf(f, " COLORS %d", &ncol) == 1 && ncol > 0 && ncol <= 32;
    ok = ok && cur_w > 0 && cur_w <= 32 && cur_h > 0 && cur_h <= 32;
    for (int i = 0; ok && i < ncol; i++) {
        int r, g, b;
        ok = fscanf(f, "%d %d %d", &r, &g, &b) == 3;
        col[i] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
    /* Skip to the end of the last colour line. Casting the call to void does not silence fgets's
     * warn_unused_result - the result has to land somewhere - so it is taken and then discarded.
     * Hitting the end of the file here only means the rows below are missing, and the loop that
     * reads them catches that. */
    if (ok) {
        char *rest = fgets(line, sizeof line, f);
        (void)rest;
    }
    for (int y = 0; ok && y < cur_h; y++) {
        if (!fgets(line, sizeof line, f)) { ok = false; break; }
        for (int x = 0; x < cur_w; x++) {
            char c = line[x];
            cur_px[y * cur_w + x] = (c >= 'a' && c < 'a' + ncol) ? col[c - 'a'] : 0;
        }
    }
    fclose(f);
    if (!ok) SDL_Log("bad cursor file %s", p);
    return ok;
}

/* The system arrow is off throughout ([HandleInitializations] calls HideCursor); CURSOR_HAND draws
 * the game's own hand into the frame instead, and CURSOR_NONE leaves the pointer invisible. */
void cursor_set(int mode) { cursor_mode = mode; }

void present(void)
{
    uint32_t lut[256];
    for (int i = 0; i < 256; i++)
        lut[i] = 0xFF000000u | ((uint32_t)(shown[i].r >> 8) << 16)
               | ((uint32_t)(shown[i].g >> 8) << 8) | (uint32_t)(shown[i].b >> 8);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) argb[i] = lut[fb[i]];

    /* whole-number scale, centred; outside the 640x480 area is "the rest of the monitor" */
    int ow, oh;
    SDL_GetRenderOutputSize(g_ren, &ow, &oh);
    int s = ow / SCREEN_W < oh / SCREEN_H ? ow / SCREEN_W : oh / SCREEN_H;
    if (s < 1) s = 1;
    int gw = SCREEN_W * s, gh = SCREEN_H * s;
    if (opt_scale == SCALE_FIT) {        /* fill the window, keeping the 4:3 shape */
        gh = oh; gw = oh * SCREEN_W / SCREEN_H;
        if (gw > ow) { gw = ow; gh = ow * SCREEN_H / SCREEN_W; }
    }
    int gx = (ow - gw) / 2, gy = (oh - gh) / 2;
    last_scale = gw / SCREEN_W > 0 ? gw / SCREEN_W : 1;
    last_gx = gx; last_gy = gy;

    /* Asked of the window rather than remembered from an event: a focus change that arrives while
     * something else is draining the queue used to leave the flag stuck off, and the hand with it. */
    bool lit = (SDL_GetWindowFlags(g_win) & (SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS)) != 0;
    if (cursor_mode == CURSOR_HAND && cur_w && lit) {          /* [SetCCursor](crsr 128) */
        int mx, my;
        mouse_pos(&mx, &my);
        for (int y = 0; y < cur_h; y++) {
            int dy = my - cur_hy + y;
            if (dy < 0 || dy >= SCREEN_H) continue;
            for (int x = 0; x < cur_w; x++) {
                int dx = mx - cur_hx + x;
                uint32_t c = cur_px[y * cur_w + x];
                if (c && dx >= 0 && dx < SCREEN_W) argb[dy * SCREEN_W + dx] = c;
            }
        }
    }
    SDL_UpdateTexture(g_tex, NULL, argb, SCREEN_W * 4);

    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);
    if (gx > 0 || gy > 0) {
        if (margin_pat && margin_tex) {
            for (int i = 0; i < margin_pat->w * margin_pat->h; i++) margin_argb[i] = lut[margin_pat->px[i]];
            SDL_UpdateTexture(margin_tex, NULL, margin_argb, margin_pat->w * 4);
            SDL_FRect all = { 0, 0, (float)ow, (float)oh };
            SDL_RenderTextureTiled(g_ren, margin_tex, NULL, (float)s, &all);
        }
        if (border_active) {
            for (int k = 1; k <= 7; k++) {
                set_draw_color(&shown[border_idx[k - 1]]);
                float x0 = (float)(gx - k * s), y0 = (float)(gy - k * s), t = (float)s;
                float w = (float)((SCREEN_W + 2 * k) * s), h = (float)((SCREEN_H + 2 * k) * s);
                SDL_FRect top = { x0, y0, w, t }, bot = { x0, y0 + h - t, w, t };
                SDL_FRect lef = { x0, y0, t, h }, rig = { x0 + w - t, y0, t, h };
                SDL_RenderFillRect(g_ren, &top); SDL_RenderFillRect(g_ren, &bot);
                SDL_RenderFillRect(g_ren, &lef); SDL_RenderFillRect(g_ren, &rig);
            }
        }
    }
    SDL_FRect dst = { (float)gx, (float)gy, (float)gw, (float)gh };
    SDL_RenderTexture(g_ren, g_tex, NULL, &dst);
    SDL_RenderPresent(g_ren);
}

void toggle_fullscreen(void)
{
    static bool full;
    full = !full;
    SDL_SetWindowFullscreen(g_win, full);
}

void set_margin_pattern(const Image *pat)
{
    margin_pat = pat;
    if (pat && !margin_tex) {
        margin_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, pat->w, pat->h);
        SDL_SetTextureScaleMode(margin_tex, SDL_SCALEMODE_NEAREST);
    }
}

void set_border_rings(const uint8_t idx[7], bool on)
{
    if (idx) memcpy(border_idx, idx, 7);
    border_active = on;
}

void wait_ms(Uint64 ms)
{
    Uint64 end = SDL_GetTicks() + ms;
    do { pump(); present(); SDL_Delay(1); } while (SDL_GetTicks() < end);
}

void delay_ticks(int ticks) { wait_ms((Uint64)(ticks * MAC_TICK_MS + 0.5)); }

/* ---- colour table (the Mac "ClutTool" routines) --------------------------------------------- */
void set_entries(void) { memcpy(shown, table, sizeof table); present(); }

void set_monitor_clut(const Rgb *clut, bool apply)
{
    memcpy(table, clut, sizeof(Rgb) * 256);
    if (apply) set_entries();
}

void set_clut_to_color(Rgb c, bool apply)
{
    for (int i = 0; i < 256; i++) table[i] = c;
    if (apply) set_entries();
}

void fade_to(const Rgb *target, int steps)
{
    Rgb start[256];
    memcpy(start, table, sizeof start);
    for (int s = 1; s <= steps; s++) {
        Uint64 t0 = SDL_GetTicks();
        for (int i = 0; i < 256; i++) {
            table[i].r = start[i].r + (int)((int64_t)(target[i].r - start[i].r) * s / steps);
            table[i].g = start[i].g + (int)((int64_t)(target[i].g - start[i].g) * s / steps);
            table[i].b = start[i].b + (int)((int64_t)(target[i].b - start[i].b) * s / steps);
        }
        set_entries();
        while (SDL_GetTicks() - t0 < 7) { pump(); SDL_Delay(1); }
    }
}

void fade_to_color(int steps, Rgb c)
{
    Rgb t[256];
    for (int i = 0; i < 256; i++) t[i] = c;
    fade_to(t, steps);
}

/* [HueScale] / [MapToHue]: every entry becomes the target colour scaled by the entry's own HSL
 * lightness - result = target * (L_entry / L_target). HueScale(7) is grey, HueScale(1) is red. */
void hue_scale_to(Rgb target, bool apply)
{
    int lt = 0;
    {
        int mx = target.r, mn = target.r;
        if (target.g > mx) mx = target.g;
        if (target.b > mx) mx = target.b;
        if (target.g < mn) mn = target.g;
        if (target.b < mn) mn = target.b;
        lt = (mx + mn) / 2;
    }
    if (lt <= 0) lt = 1;
    for (int i = 0; i < 256; i++) {
        int mx = table[i].r, mn = table[i].r;
        if (table[i].g > mx) mx = table[i].g;
        if (table[i].b > mx) mx = table[i].b;
        if (table[i].g < mn) mn = table[i].g;
        if (table[i].b < mn) mn = table[i].b;
        int l = (mx + mn) / 2;
        long r = (long)target.r * l / lt, g = (long)target.g * l / lt, b = (long)target.b * l / lt;
        table[i].r = (int)(r > 65535 ? 65535 : r);
        table[i].g = (int)(g > 65535 ? 65535 : g);
        table[i].b = (int)(b > 65535 ? 65535 : b);
    }
    if (apply) set_entries();
}

void hue_scale_grey(void)      /* HueScale(7, 1) */
{
    Rgb half = { 0x7FFF, 0x7FFF, 0x7FFF };
    hue_scale_to(half, true);
}

uint8_t find_color(int r, int g, int b)
{
    int best = 0;
    int64_t bd = INT64_MAX;
    for (int i = 0; i < 256; i++) {
        int64_t dr = table[i].r - r, dg = table[i].g - g, db = table[i].b - b;
        int64_t d = dr * dr + dg * dg + db * db;
        if (d < bd) { bd = d; best = i; }
    }
    return (uint8_t)best;
}

/* ---- images and sprites --------------------------------------------------------------------- */
bool image_load(Image *im, const char *path)
{
    SDL_Surface *s = SDL_LoadBMP(path);
    if (!s) { SDL_Log("cannot load %s: %s", path, SDL_GetError()); return false; }
    if (s->format != SDL_PIXELFORMAT_INDEX8) {
        SDL_Log("%s is not an 8-bit indexed BMP", path); SDL_DestroySurface(s); return false;
    }
    SDL_Palette *p = SDL_GetSurfacePalette(s);
    memset(im->pal, 0, sizeof im->pal);
    for (int i = 0; p && i < p->ncolors && i < 256; i++) {
        im->pal[i].r = p->colors[i].r * 257;
        im->pal[i].g = p->colors[i].g * 257;
        im->pal[i].b = p->colors[i].b * 257;
    }
    im->w = s->w; im->h = s->h;
    im->px = (uint8_t *)malloc((size_t)im->w * im->h);
    for (int y = 0; y < im->h; y++)
        memcpy(im->px + (size_t)y * im->w, (const uint8_t *)s->pixels + (size_t)y * s->pitch, (size_t)im->w);
    SDL_DestroySurface(s);
    return true;
}

bool sprite_load(Sprite *s, const char *dir, const char *name)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/spr/%s.bmp", dir, name);
    if (!image_load(&s->img, p)) return false;
    snprintf(p, sizeof p, "%s/spr/%s.spr", dir, name);
    FILE *f = fopen(p, "r");
    if (!f) { SDL_Log("cannot open %s", p); return false; }
    int tr = 0;
    bool ok = fscanf(f, "SPRITE %d %d %d %d", &s->count, &s->w, &s->h, &tr) == 4;
    fclose(f);
    s->trans = (uint8_t)tr;
    if (!ok) SDL_Log("bad sprite file %s", p);
    return ok;
}

void draw_image_clipped(const Image *im, int x0, int y0, int cx, int cy, int cw, int ch)
{
    for (int y = 0; y < im->h; y++) {
        int dy = y0 + y;
        if (dy < cy || dy >= cy + ch || dy < 0 || dy >= SCREEN_H) continue;
        for (int x = 0; x < im->w; x++) {
            int dx = x0 + x;
            if (dx < cx || dx >= cx + cw || dx < 0 || dx >= SCREEN_W) continue;
            fb[dy * SCREEN_W + dx] = im->px[y * im->w + x];
        }
    }
}

void draw_image(const Image *im, int x0, int y0)
{
    draw_image_clipped(im, x0, y0, 0, 0, SCREEN_W, SCREEN_H);
}

/* Draw an image through its own colour table rather than the screen's. cicn 128 - the grinning
 * face on the high-score dialog - is the one picture in the game that appears over a palette that
 * is not the one it was cut for, so its colours are matched to whatever is on the monitor at the
 * moment it is drawn instead of being baked in by the extractor. */
/* The options screen's Graphics page, applied once the saved settings have been read.
 *
 * SDL_MaximizeWindow alone is not enough: the request has to reach a window that is already mapped,
 * and even then some window managers drop it. So the window is pumped and synced first, and then
 * sized to the display's usable bounds by hand as well - which looks the same either way, and
 * leaves a window that is really that size even where the maximized state did not take. */
void window_set_maximized(bool on)
{
    if (!g_win) return;
    if (!on) { SDL_RestoreWindow(g_win); SDL_RaiseWindow(g_win); return; }

    /* Give the window manager a chance to act on the state the window was CREATED with before
     * asking again: SDL_SyncWindow blocks until a pending state change has actually landed. */
    SDL_PumpEvents();
    SDL_SyncWindow(g_win);
    if (!(SDL_GetWindowFlags(g_win) & SDL_WINDOW_MAXIMIZED)) {
        SDL_MaximizeWindow(g_win);
        SDL_SyncWindow(g_win);
    }

    /* Only if the state really did not take is the window sized by hand, and then on the display
     * it is actually on rather than the primary one - with two monitors those differ, and the
     * usable bounds of the wrong one would throw the window across to the other screen. The test
     * is the MAXIMIZED flag, not the size: asking for the size before the change has landed
     * reports the old one, and sizing a window that IS maximized is exactly what un-maximizes it
     * on Windows. Acting on that stale size was undoing the thing it was meant to guarantee. */
    if (!(SDL_GetWindowFlags(g_win) & SDL_WINDOW_MAXIMIZED)) {
        SDL_DisplayID d = SDL_GetDisplayForWindow(g_win);
        SDL_Rect use;
        if (d && SDL_GetDisplayUsableBounds(d, &use) && use.w >= SCREEN_W && use.h >= SCREEN_H) {
            SDL_SetWindowPosition(g_win, use.x, use.y);
            SDL_SetWindowSize(g_win, use.w, use.h);
        }
    }
    SDL_RaiseWindow(g_win);
}

void draw_image_remap(const Image *im, int x0, int y0, bool skip0)
{
    uint8_t map[256];
    bool have[256] = { false };
    for (int y = 0; y < im->h; y++) {
        int dy = y0 + y;
        if (dy < 0 || dy >= SCREEN_H) continue;
        for (int x = 0; x < im->w; x++) {
            int dx = x0 + x;
            if (dx < 0 || dx >= SCREEN_W) continue;
            uint8_t i = im->px[y * im->w + x];
            if (skip0 && i == 0) continue;
            if (!have[i]) { map[i] = find_color(im->pal[i].r, im->pal[i].g, im->pal[i].b); have[i] = true; }
            fb[dy * SCREEN_W + dx] = map[i];
        }
    }
}

/* A QuickDraw round rect: FrameRoundRect/PaintRoundRect with ovalWidth and ovalHeight 16, which is
 * what every Mac push button and the default item's ring are drawn with - a radius of eight. */
void fill_round_rect(int x, int y, int w, int h, int r, uint8_t c)
{
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int dy = 0; dy < h; dy++) {
        int ty = dy < r ? r - dy : (dy >= h - r ? dy - (h - r) + 1 : 0);
        int inset = 0;
        if (ty > 0) {                       /* inside the corner's quarter circle */
            while (inset < r) {
                int ax = r - inset, ay = ty;
                if (ax * ax + ay * ay <= r * r) break;
                inset++;
            }
        }
        int yy = y + dy;
        if (yy < 0 || yy >= SCREEN_H) continue;
        for (int dx = inset; dx < w - inset; dx++) {
            int xx = x + dx;
            if (xx < 0 || xx >= SCREEN_W) continue;
            fb[yy * SCREEN_W + xx] = c;
        }
    }
}

void draw_sprite_clipped(const Sprite *s, int frame, int x, int y, int cx, int cy, int cw, int ch)
{
    if (s->count <= 0) return;
    if (frame < 1) frame = 1;
    if (frame > s->count) frame = ((frame - 1) % s->count) + 1;
    const uint8_t *src = s->img.px + (size_t)(frame - 1) * s->h * s->w;
    for (int row = 0; row < s->h; row++) {
        int dy = y + row;
        if (dy < cy || dy >= cy + ch || dy < 0 || dy >= SCREEN_H) continue;
        for (int col = 0; col < s->w; col++) {
            int dx = x + col;
            if (dx < cx || dx >= cx + cw || dx < 0 || dx >= SCREEN_W) continue;
            uint8_t v = src[row * s->w + col];
            if (v != s->trans) fb[dy * SCREEN_W + dx] = v;
        }
    }
}

/* Like draw_sprite, but keying out a colour of the caller's choosing rather than the sheet's own
 * transparent index - the weapon icons are drawn on black for the score panel's boxes, so flying
 * one across the playfield needs the black treated as nothing. */
void draw_sprite_key(const Sprite *s, int frame, int x, int y, uint8_t key)
{
    if (!s->img.px || frame < 1 || frame > s->count) return;
    const uint8_t *src = s->img.px + (size_t)(frame - 1) * s->h * s->w;
    for (int yy = 0; yy < s->h; yy++) {
        int dy = y + yy;
        if (dy < 0 || dy >= SCREEN_H) continue;
        for (int xx = 0; xx < s->w; xx++) {
            int dx = x + xx;
            uint8_t v = src[yy * s->w + xx];
            if (v == key || v == s->trans || dx < 0 || dx >= SCREEN_W) continue;
            fb[dy * SCREEN_W + dx] = v;
        }
    }
}

void draw_sprite(const Sprite *s, int frame, int x, int y)
{
    draw_sprite_clipped(s, frame, x, y, 0, 0, SCREEN_W, SCREEN_H);
}

void fill_rect(int x, int y, int w, int h, uint8_t c)
{
    for (int dy = y; dy < y + h; dy++) {
        if (dy < 0 || dy >= SCREEN_H) continue;
        for (int dx = x; dx < x + w; dx++)
            if (dx >= 0 && dx < SCREEN_W) fb[dy * SCREEN_W + dx] = c;
    }
}

/* QuickDraw LineTo with the default 1x1 pen - Bresenham, both ends included. */
void draw_line(int x0, int y0, int x1, int y1, uint8_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < SCREEN_W && y0 >= 0 && y0 < SCREEN_H) fb[y0 * SCREEN_W + x0] = c;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void tile_pattern(const Image *pat, int x, int y, int w, int h)
{
    if (!pat || !pat->px || pat->w <= 0 || pat->h <= 0) return;
    for (int dy = y; dy < y + h; dy++) {
        if (dy < 0 || dy >= SCREEN_H) continue;
        const uint8_t *row = pat->px + (size_t)(((dy % pat->h) + pat->h) % pat->h) * pat->w;
        for (int dx = x; dx < x + w; dx++) {
            if (dx < 0 || dx >= SCREEN_W) continue;
            fb[dy * SCREEN_W + dx] = row[((dx % pat->w) + pat->w) % pat->w];
        }
    }
}

/* ---- text -------------------------------------------------------------------------------- */
bool font_load(Font *f, const char *dir, const char *stem)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s.bmp", dir, stem);
    if (!image_load(&f->atlas, p)) return false;
    snprintf(p, sizeof p, "%s/%s.fnt", dir, stem);
    FILE *fp = fopen(p, "r");
    if (!fp) { SDL_Log("cannot open %s", p); return false; }
    bool ok = fscanf(fp, "FONT %d %d %d", &f->ascent, &f->descent, &f->height) == 3;
    for (int c = 0; ok && c < 256; c++)
        ok = fscanf(fp, "%d %d %d %d", &f->x[c], &f->w[c], &f->left[c], &f->adv[c]) == 4;
    fclose(fp);
    if (!ok) SDL_Log("bad font file %s", p);
    return ok;
}

int text_width(const Font *f, const char *str, int style)
{
    int w = 0;
    for (const unsigned char *p = (const unsigned char *)str; *p; p++)
        w += f->adv[*p] + ((style & STYLE_BOLD) ? 1 : 0);
    return w;
}

/* Mac Roman text with its baseline at `base`, like DrawString after MoveTo(x, base).
 * Bold is synthesised as QuickDraw does it: the glyph drawn twice, 1 px apart, advance + 1. */
int draw_text(const Font *f, int x, int base, const char *str, uint8_t color, int style)
{
    int x0 = x;
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        int c = *p;
        for (int pass = 0; pass < ((style & STYLE_BOLD) ? 2 : 1); pass++)
            for (int row = 0; row < f->height; row++) {
                int dy = base - f->ascent + row;
                if (dy < 0 || dy >= SCREEN_H) continue;
                for (int col = 0; col < f->w[c]; col++) {
                    int dx = x + f->left[c] + col + pass;
                    if (dx >= 0 && dx < SCREEN_W && f->atlas.px[row * f->atlas.w + f->x[c] + col])
                        fb[dy * SCREEN_W + dx] = color;
                }
            }
        x += f->adv[c] + ((style & STYLE_BOLD) ? 1 : 0);
    }
    if (style & STYLE_UNDERLINE)
        for (int dx = x0; dx < x; dx++)
            if (dx >= 0 && dx < SCREEN_W && base + 1 < SCREEN_H) fb[(base + 1) * SCREEN_W + dx] = color;
    return x - x0;
}

/* ---- sound ([ST_*] in the original) --------------------------------------------------------- */
bool sound_load(Sound *s, const char *path)
{
    s->loaded = SDL_LoadWAV(path, &s->spec, &s->data, &s->len);
    if (!s->loaded) SDL_Log("cannot load %s: %s", path, SDL_GetError());
    return s->loaded;
}

/* The audio device is opened once and every voice is a stream bound to it for the life of the
 * program. Opening a device stream per sound effect - which is what this used to do - costs the
 * best part of a frame or more each time the hardware is renegotiated, and with a sound on nearly
 * every mushroom hit that is exactly the stutter it produced. */
Voice sound_play(const Sound *s)
{
    Voice v = { NULL, 0 };
    if (!s || !s->loaded || !s->data) return v;
    int bps = s->spec.freq * s->spec.channels * (int)SDL_AUDIO_BYTESIZE(s->spec.format);
    if (bps <= 0) return v;
    v.end_ms = SDL_GetTicks() + (Uint64)s->len * 1000 / (Uint64)bps;   /* keeps timing right with no device */
    return v;                       /* the caller's own stream carries the sound; see voice_play */
}

/* Puts `s` into an already-open stream, converting on the way in. */
static void voice_play(SDL_AudioStream **slot, Voice *v, const Sound *s)
{
    if (!audio_ok || !s || !s->loaded || !s->data) return;
    if (!*slot) {
        *slot = SDL_CreateAudioStream(&s->spec, NULL);
        if (!*slot) return;
        if (!SDL_BindAudioStream(audio_dev, *slot)) {
            SDL_DestroyAudioStream(*slot);
            *slot = NULL;
            return;
        }
    }
    SDL_ClearAudioStream(*slot);
    SDL_SetAudioStreamFormat(*slot, &s->spec, NULL);
    SDL_PutAudioStreamData(*slot, s->data, (int)s->len);
    SDL_FlushAudioStream(*slot);
    v->stream = *slot;
}

bool sound_playing(const Voice *v)
{
    if (SDL_GetTicks() < v->end_ms) return true;
    return v->stream && SDL_GetAudioStreamQueued(v->stream) > 0;
}

/* Stops what a voice is playing without throwing its stream away. */
void sound_halt(Voice *v)
{
    if (v->stream) SDL_ClearAudioStream(v->stream);
    v->stream = NULL; v->end_ms = 0;
}

void sound_wait(Voice *v)
{
    while (sound_playing(v)) { pump(); present(); SDL_Delay(2); }
    if (v->stream) wait_ms(60);
    sound_halt(v);
}

/* A small voice pool, so overlapping effects behave like the original's several sound channels. */
#define MAX_SFX 256
#define VOICES  12
static Sound sfx_bank[MAX_SFX];
static struct { Voice v; int id; SDL_AudioStream *stream; } voices[VOICES];
static Voice music_voice;
static int  music_state;                  /* 0 off, 1 playing "start", 2 playing the loop */
static Sound music_start_snd, music_loop_snd;
static SDL_AudioStream *music_stream;

/* Loads whatever sounds `dir` holds over whatever is loaded already, freeing what it replaces.
 * assets_set_pack() calls it twice - the game's own bank, then the pack's - so a pack carrying
 * only a few sounds leaves the rest of the game audible. */
void sfx_load_bank(const char *dir)
{
    char p[1024];
    static bool have[MAX_SFX];
    for (int i = 0; i < MAX_SFX; i++) {
        snprintf(p, sizeof p, "%s/snd/%d.wav", dir, i);
        FILE *f = fopen(p, "rb");
        if (!f) continue;
        fclose(f);
        if (have[i] && sfx_bank[i].data) SDL_free(sfx_bank[i].data);
        have[i] = sound_load(&sfx_bank[i], p);
    }
    struct { Sound *s; const char *stem; } mus[2] = {
        { &music_start_snd, "music_start" }, { &music_loop_snd, "music_loop" }
    };
    for (int i = 0; i < 2; i++) {
        snprintf(p, sizeof p, "%s/snd/%s.wav", dir, mus[i].stem);
        FILE *f = fopen(p, "rb");
        if (!f) continue;
        fclose(f);
        if (mus[i].s->loaded && mus[i].s->data) SDL_free(mus[i].s->data);
        sound_load(mus[i].s, p);
    }
}

void sfx(int id)
{
    if (id < 0 || id >= MAX_SFX || !sfx_bank[id].loaded) return;
    int slot = -1;
    for (int i = 0; i < VOICES; i++) {
        if (voices[i].v.stream || sound_playing(&voices[i].v)) continue;
        slot = i; break;
    }
    if (slot < 0) {                        /* steal the oldest */
        slot = 0;
        for (int i = 1; i < VOICES; i++)
            if (voices[i].v.end_ms < voices[slot].v.end_ms) slot = i;
        sound_halt(&voices[slot].v);
    }
    sound_halt(&voices[slot].v);
    voices[slot].v = sound_play(&sfx_bank[id]);
    voice_play(&voices[slot].stream, &voices[slot].v, &sfx_bank[id]);
    voices[slot].id = id;
}

bool sfx_playing(int id)
{
    for (int i = 0; i < VOICES; i++)
        if (voices[i].id == id && sound_playing(&voices[i].v)) return true;
    return false;
}

void sfx_halt_all(void)
{
    for (int i = 0; i < VOICES; i++) { sound_halt(&voices[i].v); voices[i].id = -1; }
}

/* Voices that have finished are released here, so the pool does not fill with dead streams. */
static void voices_reap(void)
{
    for (int i = 0; i < VOICES; i++)
        if (voices[i].v.stream && !sound_playing(&voices[i].v)) { voices[i].v.stream = NULL; voices[i].v.end_ms = 0; }
}

void music_start(void)                     /* [StartMusic]: "Music Start" then "Music Loop" */
{
    music_halt();
    if (music_start_snd.loaded) {
        music_voice = sound_play(&music_start_snd);
        voice_play(&music_stream, &music_voice, &music_start_snd);
        music_state = 1;
    }
    else if (music_loop_snd.loaded) { music_voice = sound_play(&music_loop_snd); music_state = 2; }
}

void music_halt(void) { sound_halt(&music_voice); music_state = 0; }

void music_pump(void)
{
    voices_reap();
    if (!music_state) return;
    if (sound_playing(&music_voice)) return;
    sound_halt(&music_voice);
    if (music_loop_snd.loaded) {
        music_voice = sound_play(&music_loop_snd);
        voice_play(&music_stream, &music_voice, &music_loop_snd);
        music_state = 2;
    }
    else music_state = 0;
}
