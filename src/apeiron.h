/*
 * Apeiron (Ambrosia Software, 1.0.3) ported to SDL3 - shared declarations.
 *
 * Names in [brackets] through this port are the functions in the decompiled PowerPC build
 * (Apeiron.pef, exported from Ghidra); they are the reference for the behaviour implemented here.
 */
#ifndef APEIRON_H
#define APEIRON_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_W 640
#define SCREEN_H 480
#define MAC_TICK_MS (1000.0 / 60.15)      /* TickCount() runs at 60.15 Hz */

/* The game area is a 432x480 field of 16x16 cells, centred in the 640x480 screen
 * ([HandleInitializations] centres it; the 27x30 grid comes from [CreateShroomField]). */
#define GRID_W   27
#define GRID_H   30
#define CELL     16
#define FIELD_W  (GRID_W * CELL)          /* 432 */
#define FIELD_H  (GRID_H * CELL)          /* 480 */
/* [HandleInitializations] puts the 432x480 field at the game window's own origin and the 208-wide
 * ScoreMatt panel (PICT 401) in the rest of the 640-wide window, so the field is flush left. */
#define FIELD_X  0
#define FIELD_Y  0
#define PANEL_X  FIELD_W                      /* 432; the panel is 208 wide, to 640 */

/* 16.16 fixed point, as the sprite x/y at +0x10 / +0x14 in the original. */
typedef int32_t fix_t;
#define FIX(n)      ((fix_t)((n) * 65536))
#define FIX_WHOLE(f) ((int)((f) >> 16))

typedef struct { int r, g, b; } Rgb;      /* 16-bit components, like the Mac's RGBColor */

typedef struct { int w, h; uint8_t *px; Rgb pal[256]; } Image;

typedef struct {                          /* a sprite strip: `count` frames of w x h, frame f at y = f*h */
    Image img;
    int count, w, h;
    uint8_t trans;                        /* the index drawn as transparent */
} Sprite;

typedef struct {                          /* a Mac bitmap font (NFNT) */
    Image atlas;
    int ascent, descent, height;
    int x[256], w[256], left[256], adv[256];
} Font;
enum { STYLE_BOLD = 1, STYLE_UNDERLINE = 4 };    /* QuickDraw face bits */

typedef struct { SDL_AudioSpec spec; Uint8 *data; Uint32 len; bool loaded; } Sound;
typedef struct { SDL_AudioStream *stream; Uint64 end_ms; } Voice;

/* ---- platform.c ---------------------------------------------------------------------------- */
extern uint8_t fb[SCREEN_W * SCREEN_H];   /* the 8-bit indexed framebuffer */
extern Rgb table[256];                    /* colour table in memory (what the game edits) */
extern Rgb shown[256];                    /* what is on screen (after SetEntries) */
extern const Rgb RGB_BLACK, RGB_WHITE;

bool plat_init(const char *title);
void plat_shutdown(int code);
void pump(void);
bool button(void);                        /* Button(): true once per click / key press */
bool key_down(SDL_Scancode sc);
void mouse_pos(int *x, int *y);           /* in 640x480 screen coordinates */
bool take_click(int *x, int *y);
void flush_clicks(void);          /* a click since the last call, with its position */
void present(void);
void toggle_fullscreen(void);
void wait_ms(Uint64 ms);
void delay_ticks(int ticks);
void set_margin_pattern(const Image *pat);
void set_border_rings(const uint8_t idx[7], bool on);

void set_entries(void);
void set_monitor_clut(const Rgb *clut, bool apply);
void set_clut_to_color(Rgb c, bool apply);
void fade_to(const Rgb *target, int steps);
void fade_to_color(int steps, Rgb c);
void hue_scale_grey(void);
void hue_scale_to(Rgb target, bool apply);
uint8_t find_color(int r, int g, int b);  /* QuickDraw's nearest entry in the current table */

bool image_load(Image *im, const char *path);
bool sprite_load(Sprite *s, const char *dir, const char *name);
void draw_image(const Image *im, int x0, int y0);
void draw_image_clipped(const Image *im, int x0, int y0, int cx, int cy, int cw, int ch);
void draw_sprite(const Sprite *s, int frame, int x, int y);        /* frame is 1-based, as in the original */
void draw_sprite_clipped(const Sprite *s, int frame, int x, int y, int cx, int cy, int cw, int ch);
void fill_rect(int x, int y, int w, int h, uint8_t c);
void tile_pattern(const Image *pat, int x, int y, int w, int h);

bool font_load(Font *f, const char *dir, const char *stem);
int  text_width(const Font *f, const char *str, int style);
int  draw_text(const Font *f, int x, int base, const char *str, uint8_t color, int style);

bool sound_load(Sound *s, const char *path);
Voice sound_play(const Sound *s);
bool sound_playing(const Voice *v);
void sound_halt(Voice *v);
void sound_wait(Voice *v);
void sfx(int id);                         /* play a sound by its 'Apeiron Sounds' resource id */
bool sfx_playing(int id);
void music_start(void);                   /* [StartMusic]: "Music Start" then "Music Loop" */
void music_halt(void);                    /* [HaltMusic] */
void music_pump(void);                    /* call once a tick: hands the loop over when the start ends */
void sfx_halt_all(void);

/* ---- assets.c ------------------------------------------------------------------------------ */
/* one drawing command from an instruction page (PICT 4000-4002, which are QuickDraw commands) */
typedef struct {
    char kind;          /* 'T' text, 'R' paint rect, 'F' frame rect, 'B' inline icon */
    int x, y, x2, y2;   /* 'B': x2 is the icon's index, font/y2 its width and height */
    int font, size, face, color;
    char *text;
} DrawOp;
typedef struct { DrawOp *op; int n; Image img[8]; int nimg; } AboutPage;

typedef struct {
    Image ambrosia, intro_logo, title_logo, title_morrison, scorematt;
    Image btn_up, btn_up_hit, btn_down, btn_down_hit;
    Image gameover_logo, gameover_anim[8], ending;
    AboutPage about[3];
    Image pat_title, pat_level[11], pat_game;
    Font  geneva9, newyork14, newyork18, newyork24, chicago12;
    Sprite shroom, pede, shooter, shot, flea, gecko, scorpion, scum, points, explode_shooter;
    Sprite flak, splat, splat_red, coin, scorpsplat, yummie, ricochet, fodder, bonuspoints;
    Sprite shield, geckosplat, numbers;
    Sprite wpn_none, wpn[5];           /* [UpdateWeapons]: the empty slot and the five power-ups */
    Rgb game_clut[256], title_clut[256];
    char *str[40];                        /* strings.txt */
    struct { char name[64]; long score; int wave; } score_row[10];
    int pref_volume, pref_music;
} Assets;
extern Assets A;
bool assets_load(const char *dir);
void assets_save_scores(const char *dir);

/* ---- intro.c ------------------------------------------------------------------------------- */
void apeiron_intro(void);                 /* [ApeironIntro] */
void do_help(void);                       /* [DoHelp]: the game's three instruction pages */
void draw_title_screen(void);             /* [UpdateTitleScreen] */
int  title_loop(void);                    /* returns a TITLE_* action */
enum { TITLE_PLAY = 0, TITLE_INSTRUCTIONS, TITLE_CONTROLS, TITLE_CLEAR_SCORES, TITLE_ABOUT, TITLE_QUIT };

/* ---- game.c -------------------------------------------------------------------------------- */
void new_game(void);                      /* [NewGame]: runs a whole game, then the game-over sequence */

/* sounds used by name, by their 'Apeiron Sounds' resource ids */
enum {
    SND_HIT = 129, SND_SQUISH = 130, SND_SHROOM_POP = 131, SND_SHIP_EXPLOSION = 132,
    SND_SHIP_LASER = 133, SND_YUMMIE_APPEARS = 134, SND_EXTRA_SHIP = 135, SND_GOT_YUMMIE = 136,
    SND_PAUSE = 139, SND_LEVEL_CLEAR = 144, SND_OH_NO = 145, SND_SCORP_DIES = 146,
    SND_SCORP_APPEARS = 147, SND_MISSED_IT = 148, SND_KILLED_ROOT = 152, SND_MULT_APPEARS = 154,
    SND_RICOCHET = 155, SND_BOING1 = 157, SND_BOING2 = 158, SND_BONK = 159, SND_SPLUT = 160,
    SND_BONUS_APPEARS = 161, SND_EXPLOSION = 162, SND_SHOT_GECKO = 163, SND_GECKO_APPEARS = 165,
    SND_GOT_FUNKY = 166, SND_TRANSITION = 128, SND_INTRO_THEME = 143
};

#endif
