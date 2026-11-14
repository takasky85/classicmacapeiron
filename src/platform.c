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
static const Image *margin_pat;
static SDL_Texture *margin_tex;
static uint32_t margin_argb[256 * 256];
static bool    border_active;
static uint8_t border_idx[7];

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

bool plat_init(const char *title)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return false; }
    audio_ok = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!audio_ok) SDL_Log("no audio (%s) - continuing silently", SDL_GetError());
    if (!SDL_CreateWindowAndRenderer(title, SCREEN_W, SCREEN_H, SDL_WINDOW_RESIZABLE, &g_win, &g_ren)) {
        SDL_Log("window creation failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(g_win, SCREEN_W, SCREEN_H);
    SDL_MaximizeWindow(g_win);                /* start filling the screen; F11 goes fullscreen */
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    SDL_SetTextureScaleMode(g_tex, SDL_SCALEMODE_NEAREST);
    return true;
}

void pump(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) plat_shutdown(0);
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            button_event = true; have_click = true;
            click_x = (int)((e.button.x - last_gx) / last_scale);
            click_y = (int)((e.button.y - last_gy) / last_scale);
        }
        if (e.type == SDL_EVENT_KEY_DOWN) {
            if (e.key.key == SDLK_F11) toggle_fullscreen();
            else button_event = true;
        }
    }
}

bool button(void) { pump(); bool b = button_event; button_event = false; return b; }

bool key_down(SDL_Scancode sc)
{
    pump();
    int n = 0;
    const bool *st = SDL_GetKeyboardState(&n);
    return st && (int)sc < n && st[sc];
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

void present(void)
{
    uint32_t lut[256];
    for (int i = 0; i < 256; i++)
        lut[i] = 0xFF000000u | ((uint32_t)(shown[i].r >> 8) << 16)
               | ((uint32_t)(shown[i].g >> 8) << 8) | (uint32_t)(shown[i].b >> 8);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) argb[i] = lut[fb[i]];
    SDL_UpdateTexture(g_tex, NULL, argb, SCREEN_W * 4);

    /* whole-number scale, centred; outside the 640x480 area is "the rest of the monitor" */
    int ow, oh;
    SDL_GetRenderOutputSize(g_ren, &ow, &oh);
    int s = ow / SCREEN_W < oh / SCREEN_H ? ow / SCREEN_W : oh / SCREEN_H;
    if (s < 1) s = 1;
    int gw = SCREEN_W * s, gh = SCREEN_H * s, gx = (ow - gw) / 2, gy = (oh - gh) / 2;
    last_scale = s; last_gx = gx; last_gy = gy;

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

Voice sound_play(const Sound *s)
{
    Voice v = { NULL, 0 };
    if (!s || !s->loaded || !s->data) return v;
    int bps = s->spec.freq * s->spec.channels * (int)SDL_AUDIO_BYTESIZE(s->spec.format);
    if (bps <= 0) return v;
    v.end_ms = SDL_GetTicks() + (Uint64)s->len * 1000 / (Uint64)bps;   /* keeps timing right with no device */
    if (audio_ok) {
        v.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &s->spec, NULL, NULL);
        if (v.stream) {
            SDL_PutAudioStreamData(v.stream, s->data, (int)s->len);
            SDL_FlushAudioStream(v.stream);
            SDL_ResumeAudioStreamDevice(v.stream);
        }
    }
    return v;
}

bool sound_playing(const Voice *v)
{
    if (SDL_GetTicks() < v->end_ms) return true;
    return v->stream && SDL_GetAudioStreamQueued(v->stream) > 0;
}

void sound_halt(Voice *v)
{
    if (v->stream) SDL_DestroyAudioStream(v->stream);
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
static struct { Voice v; int id; } voices[VOICES];
static Voice music_voice;
static int  music_state;                  /* 0 off, 1 playing "start", 2 playing the loop */
static Sound music_start_snd, music_loop_snd;

void sfx_load_bank(const char *dir)       /* called by assets_load */
{
    char p[1024];
    for (int i = 0; i < MAX_SFX; i++) {
        snprintf(p, sizeof p, "%s/snd/%d.wav", dir, i);
        FILE *f = fopen(p, "rb");
        if (!f) continue;
        fclose(f);
        sound_load(&sfx_bank[i], p);
    }
    snprintf(p, sizeof p, "%s/snd/music_start.wav", dir); sound_load(&music_start_snd, p);
    snprintf(p, sizeof p, "%s/snd/music_loop.wav", dir);  sound_load(&music_loop_snd, p);
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
        if (voices[i].v.stream && !sound_playing(&voices[i].v)) sound_halt(&voices[i].v);
}

void music_start(void)                     /* [StartMusic]: "Music Start" then "Music Loop" */
{
    music_halt();
    if (music_start_snd.loaded) { music_voice = sound_play(&music_start_snd); music_state = 1; }
    else if (music_loop_snd.loaded) { music_voice = sound_play(&music_loop_snd); music_state = 2; }
}

void music_halt(void) { sound_halt(&music_voice); music_state = 0; }

void music_pump(void)
{
    voices_reap();
    if (!music_state) return;
    if (sound_playing(&music_voice)) return;
    sound_halt(&music_voice);
    if (music_loop_snd.loaded) { music_voice = sound_play(&music_loop_snd); music_state = 2; }
    else music_state = 0;
}
