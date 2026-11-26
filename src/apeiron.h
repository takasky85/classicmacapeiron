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
/* [DrawLives] / DAT_1001daf8: eight slots at {top 340, left 597}, twenty pixels apart going left */
#define LIVES_X   597
#define LIVES_Y   340
#define LIVES_DX  20

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
bool any_input(void);
bool take_key(char *c);                    /* one typed character, for the title screen keys */
bool window_focused(void);
bool caps_lock_down(void);                /* [CapsLockDown]: GetKeys bit 0x3e */
void mouse_pos(int *x, int *y);           /* in 640x480 screen coordinates */
void mouse_relative(bool on);
void mouse_abs_changed(void);             /* take the pointer over, so steering has no edges */

/* game controllers (#14): SDL gamepads, opened as they appear */
bool pad_present(void);
const char *pad_name(void);
bool pad_move(float *x, float *y);        /* left stick or d-pad, past the dead zone */
bool pad_fire(void);
bool pad_start(void);                     /* Start: menu accept */
bool pause_requested(void);               /* Caps Lock on, or a pause button / key / click / tap toggled on */
void pause_clear(void);
bool touch_active(void);                  /* in a game, touch controls on, and the last input was a finger */
bool touch_fire(void);                    /* a finger on the on-screen fire button */
void input_play(bool on);                 /* a game is (not) running: touch controls live, menu pointer off */
/* The menu pointer a remote or controller drives. Screens with a few places to go list them. */
typedef struct { int x, y, w, h; } NavTarget;
void nav_targets(const NavTarget *t, int n, int first);   /* n = 0 clears */
void nav_keys(bool on);
void mouse_delta(float *dx, float *dy);   /* movement since the last call, in screen pixels */
bool take_click(int *x, int *y);
void flush_clicks(void);
bool take_click_only(void);                /* Button(): mouse only */          /* a click since the last call, with its position */
void present(void);
void toggle_fullscreen(void);
void wait_ms(Uint64 ms);
void delay_ticks(int ticks);
void set_margin_pattern(const Image *pat);
void set_border_rings(const uint8_t idx[7], bool on);

/* the pointing-hand cursor (crsr 128) the original sets with SetCCursor, drawn by present() so it
 * scales with the rest of the 640x480 screen; [HandleInitializations] hides it inside the game */
enum { CURSOR_NONE = 0, CURSOR_HAND = 1 };
bool cursor_load(const char *dir);
void cursor_set(int mode);

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
void draw_image_remap(const Image *im, int x0, int y0, bool skip0);
void fill_round_rect(int x, int y, int w, int h, int r, uint8_t c);
void window_set_maximized(bool on);
void draw_image_clipped(const Image *im, int x0, int y0, int cx, int cy, int cw, int ch);
void draw_sprite(const Sprite *s, int frame, int x, int y);        /* frame is 1-based, as in the original */
void draw_sprite_key(const Sprite *s, int frame, int x, int y, uint8_t key);
void draw_sprite_clipped(const Sprite *s, int frame, int x, int y, int cx, int cy, int cw, int ch);
void draw_sprite_disc(const Sprite *s, int frame, int x, int y, int cx, int cy, int cw, int ch);

/* The font built into the executable, and the folder browser that uses it. Both have to work
 * before a single asset is loaded, which is the whole reason they exist. */
void mini_text(int x, int y, const char *s, uint8_t ink);
void mini_text2x(int x, int y, const char *s, uint8_t ink);
void mini_char(int x, int y, char c, uint8_t ink);
int  mini_text_width(const char *s);
/* MSVC has no <strings.h> and spells this one with an underscore. */
#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

/* Walking a folder without caring which platform this is - see dirs.c. MSVC has no <dirent.h>
 * and MinGW's struct dirent has no d_type, so neither is assumed here. */
typedef struct { void *h; int err; bool pending; char name[260]; } DirScan;
bool dir_open(DirScan *s, const char *path);     /* false, and s->err says why, on failure */
bool dir_next(DirScan *s);                       /* fills s->name; false at the end */
void dir_close(DirScan *s);
bool dir_is_dir(const char *path);
const char *dir_why(int err);

int  assets_browse(char *out, size_t n);          /* 0 nothing, 1 a folder of artwork, 2 the game's zip */
/* ---- convert.c: turning the player's own copy of Apeiron into the artwork folder ----------------
 * A port of tools/extract_assets.py, so the game can do it itself - on a TV with no computer
 * about, as much as anywhere. It reads the game's .zip, takes the resource forks out of it and
 * writes the same folder the Python tool does. The two stand-in fonts are baked in (fontdata.c). */
typedef struct { const char *stem; int w, h; const unsigned char *bits; const unsigned char *fnt; int fnt_len; } BakedFont;
extern const BakedFont baked_fonts[2];
extern const unsigned char *baked_credits, *baked_ofl;
extern const int baked_credits_len, baked_ofl_len;

typedef void (*ConvSay)(void *ud, const char *line);
bool convert_zip_ok(const char *zip);                       /* does this zip hold the game? */
bool convert_zip(const char *zip, const char *out, ConvSay say, void *ud, char *err, size_t errn);
bool convert_find_zip(char *out, size_t n);                 /* the usual places for a download */
bool convert_out_dir(char *out, size_t n);                  /* somewhere the artwork can be written */
bool assets_make_from_zip(const char *zip, char *out, size_t n);   /* browse.c: with a progress screen */

/* The places the game looks for its artwork, so the diagnostics screen can report on each. */
int  assets_candidate_count(void);
const char *assets_candidate(int i);
/* SDL_Log plus a copy kept for the diagnostics screen, which is the only way to read it on a TV. */
void diag_note(const char *fmt, ...);
bool droid_ask_storage(void);        /* Android: ask to read Download; true if granted. No-op elsewhere */
const char *droid_storage_state(void);

/* What kind of input has actually arrived, counted as it comes, so the diagnostics screen can say
 * whether a mouse is being heard - and whether what is heard is a mouse or a touch screen. */
typedef struct { unsigned mouse_move, mouse_btn, touch_move, touch_btn, key, pad; } InputCounts;
extern InputCounts in_count;
void diag_screen(void);
void fill_rect(int x, int y, int w, int h, uint8_t c);
void draw_line(int x0, int y0, int x1, int y1, uint8_t c);
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
    char kind;          /* 'T' text, 'R' paint rect, 'F' frame rect, 'L' line, 'B' inline icon */
    int x, y, x2, y2;   /* 'B': x2 is the icon's index, font/y2 its width and height */
    int font, size, face, color;
    char *text;
} DrawOp;
typedef struct { DrawOp *op; int n; Image img[8]; int nimg; } AboutPage;

typedef struct {
    Image ambrosia, intro_logo, title_logo, title_morrison, scorematt;
    Image btn_up, btn_up_hit, btn_down, btn_down_hit;
    Image gameover_logo, gameover_anim[8], ending, paused;
    Image about_logo, about_credits, about_strip, dialog_icon;
    AboutPage poem;                    /* [ZappaTribute]: PICT 5555, drawn text */
    AboutPage about[3];
    Image pat_title, pat_level[11], pat_game;
    Font  geneva9, newyork14, newyork18, newyork24, chicago12;
    Sprite shroom, pede, shooter, shot, flea, gecko, scorpion, scum, points, explode_shooter;
    Sprite flak, splat, splat_red, coin, scorpsplat, yummie, ricochet, fodder, bonuspoints;
    Sprite life;              /* PICT 1101: a shooter in hand, in the Lives box */
    Sprite shield, geckosplat, numbers;
    Sprite wpn_none, wpn[5];           /* [UpdateWeapons]: the empty slot and the five power-ups */
    Rgb game_clut[256], title_clut[256];
    char *str[40];                        /* strings.txt */
    char *thanks[128]; int nthanks;       /* thanks.txt: STR# 150, the About dialog's credit line */
    struct { char name[64]; long score; int wave; } score_row[10];
    int pref_volume, pref_music;
} Assets;
extern Assets A;
bool assets_load(const char *dir);
const char *assets_find_dir(char *out, size_t n);   /* the usual places, and the remembered one */
void assets_remember_dir(const char *dir);
bool assets_pick_dir(char *out, size_t n);         /* the system's folder picker */
bool assets_pick_zip(char *out, size_t n);                    /* the system's file picker, for the game's zip */
bool plat_import_zip(const char *uri, char *out, size_t n);   /* a content:// URI from Android's picker, copied to a file */
int  assets_start(char *out, size_t n);                       /* Android / TV: the artwork start screen; 1 = out is the artwork folder */
bool assets_offer_zip(const char *zip);            /* browse.c: "found this - make the artwork from it?" */
void plat_message(const char *title, const char *body);
void audio_volume_changed(void);   /* the Sound Volume setting changed: apply it to what is playing */
void plat_keyboard(bool show);   /* the on-screen keyboard, where there is one (Android) */
bool mouse_fire(void);           /* any mouse button, or a touch, is down right now */
bool plat_headless(void);   /* no real display: nothing may wait for a person */
bool assets_load_sprites(void);

/* the artwork/sound packs that ship with the game, and the cheats of STR# 132 */
enum { PACK_NONE = 0, PACK_SIMPSONS, PACK_MUPPETS, PACK_STARTREK, PACK_COUNT };
extern const char *const PACK_NAME[PACK_COUNT];
extern const char *const PACK_LABEL[PACK_COUNT];
void assets_set_pack(int pack);
int  assets_pack(void);

enum {
    CH_DJARUM = 0, CH_HECTOR, CH_NALA, CH_NICEBOX, CH_SQUISH, CH_SNAPPLE,
    CH_PERNTS, CH_USMC, CH_SCUMDOG, CH_MORRISON, CH_NUKEEM, CH_COUNT
};
extern bool cheat[CH_COUNT];

/* enhancements beyond the original, each switchable on the Options screen */
enum { ENH_WEAPON_FLY = 0, ENH_WEAPON_ANIM, ENH_ATTRACT, ENH_NO_PSYC, ENH_TALLY_PICKUP,
       ENH_SHIELD_COINS, ENH_PSYC_GREY, ENH_COUNT };
/* Stamped into the title screen so a build can be told apart from the one before it. */
#define APEIRON_BUILD (__DATE__ " " __TIME__)
extern bool enh[ENH_COUNT];
extern bool debug_spawn;                  /* the Debug page's F-key creature spawns */

/* graphics and control settings, likewise */
enum { SCALE_INTEGER = 0, SCALE_FIT, SCALE_COUNT };
extern int  opt_scale;
extern bool opt_mouse, opt_keyboard, opt_pad, opt_mouse_abs, opt_touch;
extern int  opt_pad_deadzone;      /* percent */
extern int  opt_mouse_speed;       /* percent: how far the shooter goes for a given mouse movement */
extern bool opt_maximize;          /* open filling the screen */
extern int  opt_start_wave;        /* 1 unless the Enhancements page says otherwise */
void assets_save_scores(const char *dir);
void assets_load_options(const char *dir);
void assets_save_options(const char *dir);

/* ---- intro.c ------------------------------------------------------------------------------- */
void apeiron_intro(void);                 /* [ApeironIntro] */
void do_help(void);                       /* [DoHelp]: the game's three instruction pages */
void do_about(void);                      /* [DoAbout]: DLOG 150 */
void do_options(void);                    /* not in the original: packs and cheats */
void dialog_frame(int bx, int by, int bw, int bh, uint8_t face, uint8_t edge);
void dialog_button(int x, int y, int w, const char *label, uint8_t face, uint8_t edge,
                   bool dflt, bool hilite);
void dialog_ok(int x, int y, int w, uint8_t face, uint8_t edge);
void dialog_ok_hilite(int x, int y, int w, uint8_t face, uint8_t edge, bool hilite);
void dialog_ok_press(int x, int y, int w, uint8_t face, uint8_t edge);
void zappa_tribute(void);                 /* [ZappaTribute]: the poem, shown on x */
const Font *page_font(int font, int size);
void draw_title_screen(void);             /* [UpdateTitleScreen] */
int  title_loop(void);                    /* returns a TITLE_* action */
enum { TITLE_PLAY = 0, TITLE_INSTRUCTIONS, TITLE_CONTROLS, TITLE_CLEAR_SCORES, TITLE_ABOUT, TITLE_QUIT,
       TITLE_OPTIONS };

/* ---- game.c -------------------------------------------------------------------------------- */
void new_game(void);
void run_attract(void);        /* the title screen's demo, left idle long enough */
extern bool demo_mode;                      /* [NewGame]: runs a whole game, then the game-over sequence */

/* sounds used by name, by their 'Apeiron Sounds' resource ids */
enum {
    SND_HIT = 129, SND_SQUISH = 130, SND_SHROOM_POP = 131, SND_SHIP_EXPLOSION = 132,
    SND_SHIP_LASER = 133, SND_YUMMIE_APPEARS = 134, SND_EXTRA_SHIP = 135, SND_GOT_YUMMIE = 136,
    SND_GROUCHO_DEATH = 137, SND_GROUCHO_OUCH = 138, SND_OH_NO_DIALOG = 145,
    SND_MUNCH1 = 141, SND_MUNCH2 = 142, SND_WARNING = 168,
    SND_GROUCHO_LAUGH = 140, SND_DIEHL_BELCH = 149, SND_MOAN = 151, SND_RIFF = 153,
    SND_MYHOOHA = 156, SND_HEY = 164, SND_BAM = 167,
    SND_PAUSE = 139, SND_LEVEL_CLEAR = 144, SND_OH_NO = 145, SND_SCORP_DIES = 146,
    SND_SCORP_APPEARS = 147, SND_MISSED_IT = 148, SND_KILLED_ROOT = 152, SND_MULT_APPEARS = 154,
    SND_RICOCHET = 155, SND_BOING1 = 157, SND_BOING2 = 158, SND_BONK = 159, SND_SPLUT = 160,
    SND_BONUS_APPEARS = 161, SND_EXPLOSION = 162, SND_SHOT_GECKO = 163, SND_GECKO_APPEARS = 165,
    SND_GOT_FUNKY = 166, SND_POISONED = 169, SND_TRANSITION = 128, SND_INTRO_THEME = 143
};

#endif
