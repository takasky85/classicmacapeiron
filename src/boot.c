/*
 * Apeiron (Ambrosia Software, 1.0.3) - startup sequence up to the title screen, ported to SDL3.
 *
 * What this reproduces (names in [brackets] are the functions in the decompiled PowerPC code):
 *   [ApeironIntro]   FunkyFade -> Ambrosia logo (fade in, jingle) -> FunkyFade ->
 *                    Apeiron logo (fade in, theme, palette cycling) -> FunkyFade
 *   [main]           title screen drawn while the palette is black, then FadeToClut(30, title palette)
 *
 * The original draws into an 8-bit window and does everything by editing the monitor's colour table
 * (SetEntries). We do the same on purpose: an 8-bit framebuffer + a 256-entry palette that is
 * converted to ARGB for display on every present(). Fades and the palette cycling only touch the palette.
 *
 * The window is 640x480 and, when it is made bigger (maximised), is scaled by a whole-number factor
 * and centred. The original ran in a window the size of the monitor, so the area around the 640x480
 * game screen is not black on the title screen: it is tiled with pixel pattern 400 and framed by seven
 * blue rings, exactly as [UpdateTitleScreen] does; during the intro it is black ([DrawPictInWindow]).
 *
 * Not ported: the loading work done while the logo is up (sounds, sprites, ReportStr status text),
 * the cicn icons on the title screen (menu bullets, volume/music arrows) and the menu itself.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_W 640
#define SCREEN_H 480
#define MAC_TICK_MS (1000.0 / 60.15)   /* TickCount() runs at 60.15 Hz */

typedef struct { int r, g, b; } Rgb;   /* 16-bit components, like the Mac's RGBColor */

typedef struct { int w, h; uint8_t *px; Rgb pal[256]; } Image;
typedef struct {                       /* a Mac bitmap font (NFNT) exported by tools/extract_assets.py */
    Image atlas;                       /* the glyph strip; ink is any non-zero index        */
    int ascent, descent, height;
    int x[256], w[256], left[256], adv[256];   /* per Mac Roman code: strip x, image width, pen->image offset, advance */
} Font;
enum { STYLE_BOLD = 1, STYLE_UNDERLINE = 4 };   /* QuickDraw face bits */
typedef struct { SDL_AudioSpec spec; Uint8 *data; Uint32 len; } Sound;
typedef struct { SDL_AudioStream *stream; Uint64 end_ms; } Voice;

/* ---- screen state -------------------------------------------------------------------------- */
static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture  *g_tex;
static uint8_t  fb[SCREEN_W * SCREEN_H];      /* 8-bit indexed framebuffer                       */
static uint32_t argb[SCREEN_W * SCREEN_H];
static Rgb table[256];                        /* the "colour table in memory" (what the game edits) */
static Rgb shown[256];                        /* what is actually on screen (after SetEntries)      */
static bool button_event;                     /* set by a mouse click or key press                  */
static bool audio_ok;
static const Image *margin_pat;               /* pattern tiled around the 640x480 area (NULL = black) */
static SDL_Texture *margin_tex;               /* ARGB copy of margin_pat in the current palette       */
static uint32_t margin_argb[256 * 256];
static bool    border_active;                 /* the seven blue rings around the screen              */
static uint8_t border_idx[7];

static void shutdown_and_exit(int code)
{
    if (g_tex) SDL_DestroyTexture(g_tex);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    SDL_Quit();
    exit(code);
}

static void pump(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) shutdown_and_exit(0);
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) shutdown_and_exit(0);
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_KEY_DOWN) button_event = true;
    }
}

static bool button(void)                       /* Button() - true once per click / key press */
{
    pump();
    bool b = button_event;
    button_event = false;
    return b;
}

static void set_draw_color(const Rgb *c)
{
    SDL_SetRenderDrawColor(g_ren, (Uint8)(c->r >> 8), (Uint8)(c->g >> 8), (Uint8)(c->b >> 8), 255);
}

static void present(void)
{
    uint32_t lut[256];
    for (int i = 0; i < 256; i++)
        lut[i] = 0xFF000000u | ((uint32_t)(shown[i].r >> 8) << 16) | ((uint32_t)(shown[i].g >> 8) << 8) | (uint32_t)(shown[i].b >> 8);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) argb[i] = lut[fb[i]];
    SDL_UpdateTexture(g_tex, NULL, argb, SCREEN_W * 4);

    /* whole-number scale, centred; anything outside the 640x480 area is the "rest of the monitor" */
    int ow, oh;
    SDL_GetRenderOutputSize(g_ren, &ow, &oh);
    int s = ow / SCREEN_W < oh / SCREEN_H ? ow / SCREEN_W : oh / SCREEN_H;
    if (s < 1) s = 1;
    int gw = SCREEN_W * s, gh = SCREEN_H * s, gx = (ow - gw) / 2, gy = (oh - gh) / 2;

    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);
    if (gx > 0 || gy > 0) {
        if (margin_pat && margin_tex) {               /* FillCRect(window, pixpat 400) */
            for (int i = 0; i < margin_pat->w * margin_pat->h; i++) margin_argb[i] = lut[margin_pat->px[i]];
            SDL_UpdateTexture(margin_tex, NULL, margin_argb, margin_pat->w * 4);
            SDL_FRect all = { 0, 0, (float)ow, (float)oh };
            SDL_RenderTextureTiled(g_ren, margin_tex, NULL, (float)s, &all);
        }
        if (border_active) {                          /* FrameRect x7 with InsetRect(-1,-1) between */
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

static void set_margin_pattern(const Image *pat)
{
    margin_pat = pat;
    if (pat && !margin_tex) {
        margin_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, pat->w, pat->h);
        SDL_SetTextureScaleMode(margin_tex, SDL_SCALEMODE_NEAREST);
    }
}

static void wait_ms(Uint64 ms)
{
    Uint64 end = SDL_GetTicks() + ms;
    do { pump(); present(); SDL_Delay(1); } while (SDL_GetTicks() < end);
}

static void delay_ticks(int ticks) { wait_ms((Uint64)(ticks * MAC_TICK_MS + 0.5)); }   /* Delay(n) */

/* ---- palette operations (the Mac "ClutTool" routines) --------------------------------------- */
static void set_entries(void)                  /* SetEntries(0, 255, table): table -> screen */
{
    memcpy(shown, table, sizeof table);
    present();
}

static void set_monitor_clut(const Rgb *clut, bool apply)   /* SetMonitorClut(clut, apply) */
{
    memcpy(table, clut, sizeof table);
    if (apply) set_entries();
}

static void set_clut_to_color(Rgb c, bool apply)            /* SetClutToColor(color, apply) */
{
    for (int i = 0; i < 256; i++) table[i] = c;
    if (apply) set_entries();
}

/* FadeToClut / FadeToColor: `steps` linear steps from the current table to `target`, each step
 * lasting at least 7 ms (the original busy-waits on FastMilliseconds). */
static void fade_to(const Rgb *target, int steps)
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

static void fade_to_color(int steps, Rgb c)
{
    Rgb t[256];
    for (int i = 0; i < 256; i++) t[i] = c;
    fade_to(t, steps);
}

/* HueScale(7, 1): maps every colour to a grey equal to its HSL lightness, applied immediately. */
static void hue_scale_grey(void)
{
    for (int i = 0; i < 256; i++) {
        int mx = table[i].r, mn = table[i].r;
        if (table[i].g > mx) mx = table[i].g;
        if (table[i].b > mx) mx = table[i].b;
        if (table[i].g < mn) mn = table[i].g;
        if (table[i].b < mn) mn = table[i].b;
        int l = (mx + mn) / 2;
        table[i].r = table[i].g = table[i].b = l;
    }
    set_entries();
}

/* QuickDraw maps a drawing colour to the closest entry of the current colour table. */
static uint8_t find_color(int r, int g, int b)
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

/* ---- images and text -------------------------------------------------------------------------- */
static bool image_load(Image *im, const char *path)
{
    SDL_Surface *s = SDL_LoadBMP(path);
    if (!s) { SDL_Log("cannot load %s: %s", path, SDL_GetError()); return false; }
    if (s->format != SDL_PIXELFORMAT_INDEX8) { SDL_Log("%s is not an 8-bit indexed BMP", path); SDL_DestroySurface(s); return false; }
    SDL_Palette *p = SDL_GetSurfacePalette(s);
    memset(im->pal, 0, sizeof im->pal);
    for (int i = 0; p && i < p->ncolors && i < 256; i++) {
        im->pal[i].r = p->colors[i].r * 257;       /* 8 -> 16 bit */
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

static void draw_image(const Image *im, int x0, int y0)    /* copies indices, ignores the image palette */
{
    for (int y = 0; y < im->h; y++) {
        int dy = y0 + y;
        if (dy < 0 || dy >= SCREEN_H) continue;
        for (int x = 0; x < im->w; x++) {
            int dx = x0 + x;
            if (dx >= 0 && dx < SCREEN_W) fb[dy * SCREEN_W + dx] = im->px[y * im->w + x];
        }
    }
}

static void draw_pict_in_window(const Image *im)           /* [DrawPictInWindow]: paint black, draw centred */
{
    memset(fb, find_color(0, 0, 0), sizeof fb);
    draw_image(im, (SCREEN_W - im->w) / 2, (SCREEN_H - im->h) / 2);
}

static bool font_load(Font *f, const char *dir, const char *stem)
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

/* Draws Mac Roman text with its baseline at `base`, like DrawString after MoveTo(x, base).
 * Bold is synthesised the way QuickDraw does it (glyph drawn twice, 1 px apart, advance + 1). */
static int text_width(const Font *f, const char *str, int style)
{
    int w = 0;
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) w += f->adv[*p] + ((style & STYLE_BOLD) ? 1 : 0);
    return w;
}

static int draw_text(const Font *f, int x, int base, const char *str, uint8_t color, int style)
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


/* ---- sound ([ST_LoadSound] / [ST_SimplePlay] / [ST_IsSoundPlaying]) ------------------------------ */
static bool sound_load(Sound *s, const char *path)
{
    if (!SDL_LoadWAV(path, &s->spec, &s->data, &s->len)) { SDL_Log("cannot load %s: %s", path, SDL_GetError()); return false; }
    return true;
}

static Voice sound_play(const Sound *s)
{
    Voice v = { NULL, 0 };
    if (!s->data) return v;
    int bytes_per_sec = s->spec.freq * s->spec.channels * (int)SDL_AUDIO_BYTESIZE(s->spec.format);
    v.end_ms = SDL_GetTicks() + (Uint64)s->len * 1000 / (Uint64)bytes_per_sec;   /* keeps timing right with no audio device */
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

static bool sound_playing(const Voice *v)
{
    if (SDL_GetTicks() < v->end_ms) return true;
    return v->stream && SDL_GetAudioStreamQueued(v->stream) > 0;
}

static void sound_halt(Voice *v)                                  /* ST_HaltSound */
{
    if (v->stream) SDL_DestroyAudioStream(v->stream);
    v->stream = NULL; v->end_ms = 0;
}

static void sound_wait(Voice *v)                                  /* do {} while (ST_IsSoundPlaying) */
{
    while (sound_playing(v)) { pump(); present(); SDL_Delay(2); }
    if (v->stream) wait_ms(60);                                   /* let the device buffer drain */
    sound_halt(v);
}

/* ---- the game's assets ------------------------------------------------------------------------ */
static Image img_ambrosia, img_intro, img_title, img_morrison, pat_title;
static Font  font_geneva9, font_newyork18;
static char *strs[40];                         /* strings.txt: [1..31] = STR# 133, [32] version, [33] "NOT REGISTERED", [34] "" */
typedef struct { char name[256]; long score; int wave; } ScoreRow;
static ScoreRow rows[10];                      /* scores.txt: the table shipped in "Apeiron Scores" */
static int pref_volume, pref_music;
static Sound snd_transition, snd_logo, snd_theme;

static const Rgb BLACK = { 0, 0, 0 };
static const Rgb WHITE = { 0xFFFF, 0xFFFF, 0xFFFF };

/* [FunkyFade]: play the transition sound, turn the picture grey, pause 5 ticks,
 * fade to white, fade to black, then wait for the sound to finish. */
static void funky_fade(void)
{
    Voice v = sound_play(&snd_transition);
    hue_scale_grey();
    delay_ticks(5);
    fade_to_color(30, WHITE);
    fade_to_color(30, BLACK);
    sound_wait(&v);
}

/* [ApeironIntro] */
static void apeiron_intro(void)
{
    funky_fade();

    /* --- screen 1: Ambrosia logo (PICT 999, clut 999) + the "Level Clear" jingle (snd 144) --- */
    set_monitor_clut(img_ambrosia.pal, false);        /* table only; the screen stays black */
    Voice jingle = sound_play(&snd_logo);
    draw_pict_in_window(&img_ambrosia);               /* drawn while the screen is black */
    set_clut_to_color(BLACK, false);
    fade_to(img_ambrosia.pal, 30);                    /* FadeToClut(30, clut 999) */
    /* the original loads sounds + sprites here, then waits for the jingle */
    sound_wait(&jingle);

    funky_fade();

    /* --- screen 2: Apeiron logo (PICT 889, clut 889) + intro theme (snd 143) --- */
    set_monitor_clut(img_intro.pal, false);
    draw_pict_in_window(&img_intro);
    Voice theme = sound_play(&snd_theme);
    delay_ticks(30);
    set_clut_to_color(BLACK, false);
    fade_to(img_intro.pal, 30);

    /* Palette cycling: 180 iterations, at least 3 ticks each (~9 s), a click skips it, and it ends
     * early if the theme has finished. Colour entries 1..246 are rotated down by one each time:
     * entry[s] = entry[s+1] for s = 1..245, entry[246] = old entry[1]. */
    for (int n = 0; n < 180; n++) {
        Uint64 t0 = SDL_GetTicks();
        Rgb first = table[1];
        for (int s = 1; s < 246; s++) table[s] = table[s + 1];
        table[246] = first;
        set_entries();
        bool clicked = false;
        while ((double)(SDL_GetTicks() - t0) < 3 * MAC_TICK_MS) {
            if (button()) { clicked = true; break; }
            SDL_Delay(1);
        }
        if (clicked) { sound_halt(&theme); break; }
        if (!sound_playing(&theme)) break;
    }
    button_event = false;                             /* FlushEvents */

    funky_fade();
    set_monitor_clut(img_title.pal, false);           /* SetMonitorClut(Title Clut, 0) */
    sound_wait(&theme);
}

/* [UpdateTitleScreen]. Coordinates are those of the decompiled code (origin = top-left of the 640x480 area). */
static void draw_hline(int x0, int x1, int y, uint8_t c) { for (int x = x0; x <= x1; x++) if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) fb[y * SCREEN_W + x] = c; }
static void draw_vline(int x, int y0, int y1, uint8_t c) { for (int y = y0; y <= y1; y++) if (y >= 0 && y < SCREEN_H && x >= 0 && x < SCREEN_W) fb[y * SCREEN_W + x] = c; }

static void draw_title_screen(void)
{
    uint8_t black = find_color(0, 0, 0);
    uint8_t white = find_color(0xFFFF, 0xFFFF, 0xFFFF);             /* ForeColor(whiteColor)  */
    uint8_t yellow = find_color(0xFC00, 0xF37D, 0x0526);            /* ForeColor(yellowColor) */
    uint8_t blue = find_color(30000, 30000, 65535);                 /* RGBForeColor 4c */
    uint8_t gray = find_color(30000, 30000, 30000);

    /* FillCRect(window, pixpat 400) fills the whole window; PaintRect then blacks out the 640x480 area,
     * so the pattern only shows where the window is bigger than that. */
    set_margin_pattern(&pat_title);
    memset(fb, black, sizeof fb);

    /* seven FrameRects, each one pixel further out: colours 4c 4c 52 52 58 52 4c */
    static const int ring_rgb[7][3] = { {30000, 30000, 65535}, {30000, 30000, 65535}, {40000, 40000, 65535},
                                        {40000, 40000, 65535}, {50000, 50000, 65535}, {40000, 40000, 65535}, {30000, 30000, 65535} };
    for (int k = 0; k < 7; k++) border_idx[k] = find_color(ring_rgb[k][0], ring_rgb[k][1], ring_rgb[k][2]);
    border_active = true;

    draw_hline(0, 380, 150, blue);                                  /* below the logo                   */
    draw_hline(0, 380, SCREEN_H - 75, blue);                        /* above the licence lines          */
    draw_vline(380, 0, SCREEN_H, blue);                             /* panel divider                    */
    draw_hline(380, SCREEN_W, 250, blue);                           /* below the six menu entries       */
    draw_hline(380, SCREEN_W, 345, blue);                           /* below the two settings           */

    draw_image(&img_title, 0, 0);                                   /* PICT 400                         */
    draw_text(&font_geneva9, 2, 148, strs[32], white, 0);           /* version                          */

    for (int i = 0; i < 6; i++)                                     /* menu: 32x32 bullet icon at x=410, label 7 px to its right */
        draw_text(&font_newyork18, 410 + 32 + 7, 26 + 32 + i * 34 - 9, strs[8 + i], white, 0);

    for (int i = 0; i < 2; i++) {                                   /* sound volume / music, between two 32x32 arrow icons 120 px apart */
        int left = 410, top = 263 + i * 39;
        draw_text(&font_geneva9, left - 10, top - 2, strs[15 + i], blue, STYLE_BOLD);
        const char *val = i == 0 ? strs[23 + pref_volume] : strs[29 + pref_music];
        int w = text_width(&font_geneva9, val, STYLE_BOLD);
        draw_text(&font_geneva9, left + (120 - w) / 2 + 32 / 2, top + 32 - 12, val, white, STYLE_BOLD);
    }

    int x = 10;                                                     /* licence lines (unregistered copy) */
    x += draw_text(&font_geneva9, x, SCREEN_H - 0x32, strs[2], blue, STYLE_BOLD) + 10;
    draw_text(&font_geneva9, x, SCREEN_H - 0x32, strs[33], white, STYLE_BOLD);
    x = 10 + draw_text(&font_geneva9, 10, SCREEN_H - 0x26, strs[3], blue, STYLE_BOLD) + 10;
    draw_text(&font_geneva9, x, SCREEN_H - 0x26, strs[34], white, STYLE_BOLD);
    draw_text(&font_geneva9, 2, SCREEN_H - 2, strs[1], white, STYLE_BOLD);   /* copyright */

    draw_text(&font_newyork18, 12, 185, strs[6], yellow, STYLE_UNDERLINE);   /* Name:  */
    draw_text(&font_newyork18, 232, 185, strs[7], yellow, STYLE_UNDERLINE);  /* Score: */
    draw_text(&font_newyork18, 315, 185, strs[14], yellow, STYLE_UNDERLINE); /* Wave:  */

    for (int i = 0; i < 10; i++) {                                  /* high scores */
        int base = (i + 1) * 20 + 185;
        char name[256], num[32];
        snprintf(name, sizeof name, "%s", rows[i].name);
        size_t len = strlen(name);
        while (len > 2 && 20 + text_width(&font_newyork18, name, 0) > 212) {   /* shorten and add an ellipsis (Mac Roman 0xC9) */
            len -= 2; name[len - 1] = (char)0xC9; name[len] = 0;
        }
        draw_text(&font_newyork18, 20, base, name, gray, 0);
        snprintf(num, sizeof num, "%ld", rows[i].score);
        draw_text(&font_newyork18, 282 - text_width(&font_newyork18, num, 0), base, num, gray, 0);
        snprintf(num, sizeof num, "%d", rows[i].wave);
        draw_text(&font_newyork18, 365 - text_width(&font_newyork18, num, 0), base, num, gray, 0);
    }

    draw_image(&img_morrison, 381, 346);                            /* PICT 402 */
}

static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *p = (char *)malloc(n); memcpy(p, s, n); return p; }

static bool load_text_data(const char *dir)
{
    char p[1024], line[600];
    snprintf(p, sizeof p, "%s/strings.txt", dir);
    FILE *f = fopen(p, "rb");
    if (!f) { SDL_Log("cannot open %s", p); return false; }
    int n = 0;
    while (n < 34 && fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        strs[++n] = xstrdup(line);
    }
    fclose(f);
    if (n < 34) { SDL_Log("strings.txt is incomplete"); return false; }

    snprintf(p, sizeof p, "%s/scores.txt", dir);
    f = fopen(p, "rb");
    if (!f) { SDL_Log("cannot open %s", p); return false; }
    if (!fgets(line, sizeof line, f) || sscanf(line, "volume %d", &pref_volume) != 1) return false;
    if (!fgets(line, sizeof line, f) || sscanf(line, "music %d", &pref_music) != 1) return false;
    for (int i = 0; i < 10; i++) {
        if (!fgets(line, sizeof line, f)) return false;
        char *t1 = strchr(line, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
        if (!t2) return false;
        *t1 = 0;
        snprintf(rows[i].name, sizeof rows[i].name, "%.255s", line);
        rows[i].score = atol(t1 + 1);
        rows[i].wave = atoi(t2 + 1);
    }
    fclose(f);
    if (pref_volume < 0 || pref_volume > 5 || pref_music < 0 || pref_music > 1) return false;
    return true;
}

static bool load_all(const char *dir)
{
    char p[1024];
#define PATH(n) (snprintf(p, sizeof p, "%s/%s", dir, n), p)
    bool ok = image_load(&img_ambrosia, PATH("ambrosia_logo.bmp"));
    ok = image_load(&img_intro, PATH("apeiron_intro.bmp")) && ok;
    ok = image_load(&img_title, PATH("title_logo.bmp")) && ok;
    ok = image_load(&img_morrison, PATH("title_morrison.bmp")) && ok;
    ok = image_load(&pat_title, PATH("pattern_400.bmp")) && ok;
    ok = font_load(&font_geneva9, dir, "font_geneva9") && ok;
    ok = font_load(&font_newyork18, dir, "font_newyork18") && ok;
    ok = load_text_data(dir) && ok;
    ok = sound_load(&snd_transition, PATH("transition.wav")) && ok;
    ok = sound_load(&snd_logo, PATH("level_clear.wav")) && ok;
    ok = sound_load(&snd_theme, PATH("intro_theme.wav")) && ok;
#undef PATH
    return ok;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "assets";

    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return 1; }
    audio_ok = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!audio_ok) SDL_Log("no audio (%s) - continuing silently", SDL_GetError());

    if (!SDL_CreateWindowAndRenderer("Apeiron - startup (SDL3 port)", SCREEN_W, SCREEN_H, SDL_WINDOW_RESIZABLE, &g_win, &g_ren)) {
        SDL_Log("window creation failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(g_win, SCREEN_W, SCREEN_H);   /* never smaller than 1x; larger sizes scale by whole numbers */
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    SDL_SetTextureScaleMode(g_tex, SDL_SCALEMODE_NEAREST);

    if (!load_all(dir)) { SDL_Log("missing assets - run tools/extract_assets.py first (looked in '%s')", dir); return 1; }

    /* Stand-in for the Mac desktop that the very first FunkyFade fades out (the game window is
     * not shown yet at that point in the original): a flat light grey screen. */
    for (int i = 0; i < 256; i++) shown[i] = table[i] = (Rgb){ 0xC000, 0xC000, 0xC000 };
    memset(fb, 0, sizeof fb);

    apeiron_intro();

    /* [main]: UpdateWindow draws the title screen while the palette is still black, then fades in. */
    draw_title_screen();
    set_clut_to_color(BLACK, false);
    fade_to(img_title.pal, 30);                          /* FadeToClut(30, Title Clut) */

    /* The original now shows the shareware notice and enters MainLoop; we just idle here. */
    for (;;) { pump(); present(); SDL_Delay(16); }
}
