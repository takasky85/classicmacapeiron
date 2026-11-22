/* assets.c - loads everything tools/extract_assets.py produced from the game's own resource forks. */
#include "apeiron.h"

Assets A;
void sfx_load_bank(const char *dir);     /* platform.c */

/* ---- artwork and sound packs -----------------------------------------------------------------
 * Three of them ship alongside the game in folders of their own, and tools/extract_assets.py puts
 * what each replaces under assets/packs/<name>/. Nothing else about them is special: a pack is
 * looked in first and the stock assets fill in whatever it does not carry.
 */
const char *const PACK_NAME[PACK_COUNT] = { "", "simpsons", "muppets", "startrek" };
const char *const PACK_LABEL[PACK_COUNT] = {
    "Apeiron", "The Simpsons", "The Muppets", "Star Trek"
};
static char asset_root[768];

/* Settings the Options screen owns. The defaults are how the game played before it existed. */
bool enh[ENH_COUNT] = { true, true, true, false, true, true, true };
int  opt_scale = SCALE_INTEGER;
bool opt_mouse = true, opt_keyboard = true, opt_pad = true;
bool opt_mouse_abs = false;        /* read the pointer's position instead of SDL's deltas */
int  opt_pad_deadzone = 25;
/* Mouse movement arrives in screen pixels, and the game divides it by how much the 640x480 picture
 * is magnified - which is right for a pointer you can see and far too slow for the shooter on a
 * 4K television, where that is 4.5. A TV also gets raw mouse counts with none of the pointer
 * acceleration a desktop adds. So Android starts faster; Options changes it. */
#ifdef __ANDROID__
int  opt_mouse_speed = 300;
#else
int  opt_mouse_speed = 100;
#endif
bool opt_maximize = false;         /* the window opens at a 2x whole-number scale instead (#17) */
int  opt_start_wave = 1;
static int  cur_pack;

static bool have(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return true; }
    return false;
}

/* The directory a given sprite or sound should come from: the pack if it has one, else the stock. */
static const char *pack_dir_for(char *buf, size_t n, const char *leaf)
{
    if (cur_pack > 0) {
        snprintf(buf, n, "%s/packs/%s", asset_root, PACK_NAME[cur_pack]);
        char probe[1024];
        snprintf(probe, sizeof probe, "%s/%s", buf, leaf);
        if (have(probe)) return buf;
    }
    snprintf(buf, n, "%s", asset_root);
    return buf;
}

static bool sprite_load_packed(Sprite *sp, const char *name)
{
    char dir[900], leaf[128];
    snprintf(leaf, sizeof leaf, "spr/%s.bmp", name);
    free(sp->img.px);
    sp->img.px = NULL;
    return sprite_load(sp, pack_dir_for(dir, sizeof dir, leaf), name);
}

int  assets_pack(void) { return cur_pack; }

/* Re-reads every sprite and the whole sound bank through the chosen pack. */
void assets_set_pack(int pack)
{
    if (pack < 0 || pack >= PACK_COUNT) pack = PACK_NONE;
    cur_pack = pack;
    assets_load_sprites();
    char dir[900];
    sfx_load_bank(pack_dir_for(dir, sizeof dir, "snd/music_loop.wav"));
    /* a pack with only some of the sounds still wants the rest of the game's own */
}


static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *p = (char *)malloc(n); memcpy(p, s, n); return p; }

/* The palette of an extracted BMP is the clut it was mapped to, so the game's two colour tables can
 * be read straight back out of the images the extractor wrote. */
static void clut_from_image(const Image *im, Rgb *out) { memcpy(out, im->pal, sizeof(Rgb) * 256); }

/* Where the game's own writes go: scores.txt and options.txt.
 *
 * On a desktop that is the artwork folder, as it always was - the folder beside the executable is
 * the player's own and is writable. On Android it cannot be. The artwork is usually somewhere in
 * Download, which this app can at best read: writing there needs a permission that a TV may not
 * even offer a way to grant, and without it every high score and every changed option would be
 * dropped on the floor with nothing to say so. The app's own private folder needs no permission
 * at all and is always writable, so that is where they go.
 *
 * Reading goes there first and then falls back to the artwork folder, because scores.txt is also
 * the table of starting scores the extractor puts in the artwork, and loading the game needs it
 * before anything has ever been saved. */
static bool state_dir(char *out, size_t n)
{
#ifdef __ANDROID__
    const char *p = SDL_GetAndroidInternalStoragePath();
    if (p && *p) { snprintf(out, n, "%s", p); return true; }
#endif
    (void)out; (void)n;
    return false;
}

static FILE *state_open(const char *dir, const char *name, const char *mode)
{
    char p[1300], sd[1100];
    if (state_dir(sd, sizeof sd)) {
        snprintf(p, sizeof p, "%s/%s", sd, name);
        FILE *f = fopen(p, mode);
        if (f || mode[0] != 'r') return f;     /* a write has nowhere else to go */
    }
    snprintf(p, sizeof p, "%s/%s", dir, name);
    return fopen(p, mode);
}

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
        A.str[++n] = xstrdup(line);
    }
    fclose(f);
    if (n < 34) { SDL_Log("strings.txt is incomplete"); return false; }

    /* STR# 150: the line after "thanks:" in the About dialog, which [DoAbout]'s userItem works
     * through one entry at a time. */
    snprintf(p, sizeof p, "%s/thanks.txt", dir);
    f = fopen(p, "rb");
    if (f) {
        while (A.nthanks < 128 && fgets(line, sizeof line, f)) {
            size_t l = strlen(line);
            while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
            A.thanks[A.nthanks++] = xstrdup(line);
        }
        fclose(f);
    }

    f = state_open(dir, "scores.txt", "rb");
    if (!f) { SDL_Log("cannot open scores.txt"); return false; }
    bool ok = fgets(line, sizeof line, f) && sscanf(line, "volume %d", &A.pref_volume) == 1;
    ok = ok && fgets(line, sizeof line, f) && sscanf(line, "music %d", &A.pref_music) == 1;
    for (int i = 0; ok && i < 10; i++) {
        if (!fgets(line, sizeof line, f)) { ok = false; break; }
        char *t1 = strchr(line, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
        if (!t2) { ok = false; break; }
        *t1 = 0;
        snprintf(A.score_row[i].name, sizeof A.score_row[i].name, "%.63s", line);
        A.score_row[i].score = atol(t1 + 1);
        A.score_row[i].wave = atoi(t2 + 1);
    }
    fclose(f);
    if (A.pref_volume < 0 || A.pref_volume > 5) A.pref_volume = 2;
    if (A.pref_music < 0 || A.pref_music > 1) A.pref_music = 1;
    return ok;
}

/* about_N.txt: one line per drawing command, as tools/extract_assets.py flattened PICT 4000+N. */
static bool load_draw_page(const char *dir, const char *stem, AboutPage *page)
{
    char p[1024], line[1024];
    snprintf(p, sizeof p, "%s/%s.txt", dir, stem);
    FILE *f = fopen(p, "rb");
    if (!f) { SDL_Log("cannot open %s", p); return false; }
    int cap = 64, cnt = 0;
    DrawOp *ops = (DrawOp *)malloc(sizeof(DrawOp) * cap);
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        if (!l) continue;
        if (cnt == cap) { cap *= 2; ops = (DrawOp *)realloc(ops, sizeof(DrawOp) * cap); }
        DrawOp *o = &ops[cnt];
        memset(o, 0, sizeof *o);
        o->kind = line[0];
        if (line[0] == 'T') {
            int used = 0;
            if (sscanf(line + 2, "%d %d %d %d %d %d %n", &o->x, &o->y, &o->font, &o->size,
                       &o->face, &o->color, &used) < 6) continue;
            o->text = xstrdup(line + 2 + used);
        } else if (line[0] == 'B') {
            if (sscanf(line + 2, "%d %d %d %d %d", &o->x, &o->y, &o->x2, &o->font, &o->y2) < 5)
                continue;
        } else if (sscanf(line + 2, "%d %d %d %d %d", &o->x, &o->y, &o->x2, &o->y2, &o->color) < 5) {
            continue;
        }
        cnt++;
    }
    fclose(f);
    page->op = ops;
    page->n = cnt;
    for (int i = 0; i < 8; i++) {           /* the icons drawn inline with the text */
        snprintf(p, sizeof p, "%s/%s_img%d.bmp", dir, stem, i);
        FILE *t = fopen(p, "rb");
        if (!t) break;
        fclose(t);
        if (!image_load(&page->img[i], p)) break;
        page->nimg = i + 1;
    }
    return cnt > 0;
}

static bool load_about_page(const char *dir, int n)
{
    char stem[32];
    snprintf(stem, sizeof stem, "about_%d", n);
    return load_draw_page(dir, stem, &A.about[n]);
}

void assets_save_scores(const char *dir)
{
    FILE *f = state_open(dir, "scores.txt", "wb");
    if (!f) return;
    fprintf(f, "volume %d\nmusic %d\n", A.pref_volume, A.pref_music);
    for (int i = 0; i < 10; i++)
        fprintf(f, "%s\t%ld\t%d\n", A.score_row[i].name, A.score_row[i].score, A.score_row[i].wave);
    fclose(f);
}

/* options.txt - the options screen's own settings, so a run starts where the last one left off.
 * The game's original prefs (volume and music) live in scores.txt with the high scores, because
 * that is where the original put them; everything on the options screen is new, so it gets a file
 * of its own and a plainly readable one. An absent or partial file just leaves the defaults. */
void assets_load_options(const char *dir)
{
    /* Called twice: once before the window exists, to learn whether it should open maximized, and
     * again after the sheets are in so the artwork pack can be switched. The first pass must not
     * touch the pack - there would be nothing to reload. */
    static bool have_sheets = false;
    char line[256];
    FILE *f = state_open(dir, "options.txt", "rb");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char key[64]; int v;
        if (sscanf(line, "%63s %d", key, &v) != 2) continue;
        if      (!strcmp(key, "scale"))    opt_scale = (v >= 0 && v < SCALE_COUNT) ? v : SCALE_INTEGER;
        else if (!strcmp(key, "mouse"))    opt_mouse = v != 0;
        else if (!strcmp(key, "mouseabs")) opt_mouse_abs = v != 0;
        else if (!strcmp(key, "keyboard")) opt_keyboard = v != 0;
        else if (!strcmp(key, "pad"))      opt_pad = v != 0;
        else if (!strcmp(key, "mousespeed")) opt_mouse_speed = (v >= 25 && v <= 1600) ? v : opt_mouse_speed;
        else if (!strcmp(key, "deadzone")) opt_pad_deadzone = (v >= 0 && v <= 90) ? v : 25;
        else if (!strcmp(key, "pack"))     { if (have_sheets && v > 0 && v < PACK_COUNT) assets_set_pack(v); }
        else if (!strcmp(key, "debug"))    debug_spawn = v != 0;
        else if (!strcmp(key, "maximize")) opt_maximize = v != 0;
        else if (!strcmp(key, "startwave")) opt_start_wave = (v >= 1 && v <= 30) ? v : 1;
        else if (!strcmp(key, "cheat")) {
            int i; if (sscanf(line, "%63s %d %d", key, &i, &v) == 3 && i >= 0 && i < CH_COUNT)
                cheat[i] = v != 0;
        } else if (!strcmp(key, "enh")) {
            int i; if (sscanf(line, "%63s %d %d", key, &i, &v) == 3 && i >= 0 && i < ENH_COUNT)
                enh[i] = v != 0;
        }
    }
    fclose(f);
    have_sheets = true;
}

void assets_save_options(const char *dir)
{
    FILE *f = state_open(dir, "options.txt", "wb");
    if (!f) return;
    fprintf(f, "scale %d\nmouse %d\nkeyboard %d\npad %d\ndeadzone %d\npack %d\ndebug %d\n"
               "maximize %d\nstartwave %d\nmouseabs %d\nmousespeed %d\n",
            opt_scale, opt_mouse, opt_keyboard, opt_pad, opt_pad_deadzone,
            assets_pack(), debug_spawn, opt_maximize, opt_start_wave, opt_mouse_abs, opt_mouse_speed);
    for (int i = 0; i < CH_COUNT; i++) fprintf(f, "cheat %d %d\n", i, cheat[i] ? 1 : 0);
    for (int i = 0; i < ENH_COUNT; i++) fprintf(f, "enh %d %d\n", i, enh[i] ? 1 : 0);
    fclose(f);
}

/* Every sprite sheet, through whichever pack is on. */
bool assets_load_sprites(void)
{
    bool ok = true;
    ok = sprite_load_packed(&A.shroom, "shroom") && ok;
    ok = sprite_load_packed(&A.pede, "pede") && ok;
    ok = sprite_load_packed(&A.shooter, "shooter") && ok;
    ok = sprite_load_packed(&A.shot, "shot") && ok;
    ok = sprite_load_packed(&A.flea, "flea") && ok;
    ok = sprite_load_packed(&A.gecko, "gecko") && ok;
    ok = sprite_load_packed(&A.scorpion, "scorpion") && ok;
    ok = sprite_load_packed(&A.scum, "scum") && ok;
    ok = sprite_load_packed(&A.points, "points") && ok;
    ok = sprite_load_packed(&A.explode_shooter, "explode_shooter") && ok;
    ok = sprite_load_packed(&A.flak, "flak") && ok;
    ok = sprite_load_packed(&A.splat, "splat") && ok;
    ok = sprite_load_packed(&A.splat_red, "splat_red") && ok;
    ok = sprite_load_packed(&A.coin, "coin") && ok;
    ok = sprite_load_packed(&A.scorpsplat, "scorpsplat") && ok;
    ok = sprite_load_packed(&A.yummie, "yummie") && ok;
    ok = sprite_load_packed(&A.ricochet, "ricochet") && ok;
    ok = sprite_load_packed(&A.life, "life") && ok;
    ok = sprite_load_packed(&A.fodder, "fodder") && ok;
    ok = sprite_load_packed(&A.bonuspoints, "bonuspoints") && ok;
    ok = sprite_load_packed(&A.shield, "shield") && ok;
    ok = sprite_load_packed(&A.geckosplat, "geckosplat") && ok;
    ok = sprite_load_packed(&A.numbers, "numbers") && ok;
    ok = sprite_load_packed(&A.wpn_none, "wpn_none") && ok;
    static const char *wn[5] = { "wpn_curve", "wpn_freemove", "wpn_machinegun", "wpn_shield", "wpn_lock" };
    for (int i = 0; i < 5; i++) ok = sprite_load_packed(&A.wpn[i], wn[i]) && ok;
    return ok;
}

/* ---- finding the artwork ---------------------------------------------------------------------
 * The game is shipped without it, so the folder could be anywhere. These are the places worth
 * looking, in the order worth looking in them; the pointer file in the preferences directory is
 * where the answer goes once somebody has had to tell us.
 */
static bool has_assets(const char *dir)
{
    if (!dir || !*dir) return false;
    char p[1200];
    snprintf(p, sizeof p, "%s/spr/shooter.bmp", dir);
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static void pointer_file(char *out, size_t n)
{
    char *pref = SDL_GetPrefPath("apeiron-sdl3", "apeiron");
    if (!pref) { out[0] = 0; return; }
    snprintf(out, n, "%sassets-path.txt", pref);
    SDL_free(pref);
}

void assets_remember_dir(const char *dir)
{
    char p[1200];
    pointer_file(p, sizeof p);
    if (!p[0]) return;
    FILE *f = fopen(p, "wb");
    if (!f) return;
    fprintf(f, "%s\n", dir);
    fclose(f);
}

/* The two names the folder goes by: what tools/extract_assets.py makes now, and what it used to. */
static const char *const LEAF[] = { "apeiron_classic_assets", "assets" };

const char *assets_find_dir(char *out, size_t n)
{
    const char *base = SDL_GetBasePath();         /* inside a .app this is Contents/Resources/ */
    const char *tries[48];
    static char store[40][1200];
    int t = 0, ns = 0;
    char remembered[1200];
#define ADDP(...) do { if (t < 46 && ns < 40) { snprintf(store[ns], sizeof store[0], __VA_ARGS__); \
                                               tries[t++] = store[ns++]; } } while (0)
#ifdef __ANDROID__
    const char *ext = SDL_GetAndroidExternalStoragePath();
    const char *inr = SDL_GetAndroidInternalStoragePath();
#endif
    for (int l = 0; l < (int)(sizeof LEAF / sizeof LEAF[0]); l++) {
        const char *leaf = LEAF[l];
        ADDP("%s", leaf);                                        /* beside whatever launched us */
        if (base) {
            ADDP("%s%s", base, leaf);
            ADDP("%s../Resources/%s", base, leaf);
            ADDP("%s../../../%s", base, leaf);                   /* beside the .app */
        }
#ifdef __ANDROID__
        /* Places the artwork can be on a TV, nearest the player first. The app's own folders need
         * no permission, so a folder put there with adb is found with nothing asked or granted;
         * Download is what a person with a file manager or the Downloader app will have used, and
         * it is readable only once the storage permission has been given - which is why this is
         * asked again after it is, not once before the window exists. */
        if (ext && *ext) ADDP("%s/%s", ext, leaf);
        if (inr && *inr) ADDP("%s/%s", inr, leaf);
        ADDP("/sdcard/Download/%s", leaf);
        ADDP("/storage/emulated/0/Download/%s", leaf);
#endif
    }
#undef ADDP
    char pf[1200];
    pointer_file(pf, sizeof pf);
    remembered[0] = 0;
    if (pf[0]) {
        FILE *f = fopen(pf, "rb");
        if (f) {
            if (fgets(remembered, sizeof remembered, f)) {
                size_t len = strlen(remembered);
                while (len && (remembered[len - 1] == '\n' || remembered[len - 1] == '\r'))
                    remembered[--len] = 0;
                if (remembered[0]) tries[t++] = remembered;
            }
            fclose(f);
        }
    }
    for (int i = 0; i < t; i++)
        if (has_assets(tries[i])) { snprintf(out, n, "%s", tries[i]); return out; }
    snprintf(out, n, "%s", "assets");             /* nothing found; the caller will ask */
    return out;
}

bool assets_load(const char *dir)
{
    snprintf(asset_root, sizeof asset_root, "%s", dir);
    char p[1024];
#define PATH(n) (snprintf(p, sizeof p, "%s/%s", dir, n), p)
    bool ok = image_load(&A.ambrosia, PATH("ambrosia_logo.bmp"));
    ok = image_load(&A.intro_logo, PATH("apeiron_intro.bmp")) && ok;
    ok = image_load(&A.title_logo, PATH("title_logo.bmp")) && ok;
    ok = image_load(&A.title_morrison, PATH("title_morrison.bmp")) && ok;
    ok = image_load(&A.pat_title, PATH("pattern_400.bmp")) && ok;
    ok = image_load(&A.scorematt, PATH("scorematt.bmp")) && ok;
    ok = image_load(&A.btn_up, PATH("btn_up.bmp")) && ok;
    ok = image_load(&A.btn_up_hit, PATH("btn_up_hit.bmp")) && ok;
    ok = image_load(&A.btn_down, PATH("btn_down.bmp")) && ok;
    ok = image_load(&A.btn_down_hit, PATH("btn_down_hit.bmp")) && ok;
    ok = image_load(&A.gameover_logo, PATH("gameover_logo.bmp")) && ok;
    ok = image_load(&A.ending, PATH("ending.bmp")) && ok;
    ok = image_load(&A.paused, PATH("paused.bmp")) && ok;    /* PICT 3130, [PauseUpdate] */
    for (int i = 0; i < 8; i++) {
        char nm[64];
        snprintf(nm, sizeof nm, "gameover_anim%d.bmp", i);
        ok = image_load(&A.gameover_anim[i], PATH(nm)) && ok;
    }
    ok = image_load(&A.pat_game, PATH("pattern_401.bmp")) && ok;
    for (int i = 0; i < 11; i++) {
        char nm[64];
        snprintf(nm, sizeof nm, "pattern_%d.bmp", 128 + i);
        ok = image_load(&A.pat_level[i], PATH(nm)) && ok;
    }
    if (ok) { clut_from_image(&A.title_logo, A.title_clut); clut_from_image(&A.pat_level[0], A.game_clut); }

    ok = font_load(&A.geneva9, dir, "font_geneva9") && ok;
    ok = font_load(&A.newyork14, dir, "font_newyork14") && ok;
    ok = font_load(&A.newyork18, dir, "font_newyork18") && ok;
    ok = font_load(&A.newyork24, dir, "font_newyork24") && ok;
    ok = font_load(&A.chicago12, dir, "font_chicago12") && ok;

    ok = assets_load_sprites() && ok;

    ok = load_text_data(dir) && ok;
    for (int i = 0; i < 3; i++) ok = load_about_page(dir, i) && ok;
    ok = load_draw_page(dir, "poem", &A.poem) && ok;
    ok = image_load(&A.about_logo, PATH("about_logo.bmp")) && ok;
    ok = image_load(&A.about_credits, PATH("about_credits.bmp")) && ok;
    ok = image_load(&A.about_strip, PATH("about_strip.bmp")) && ok;
    ok = image_load(&A.dialog_icon, PATH("dialog_icon.bmp")) && ok;   /* cicn 128, DITL 161 */
    ok = cursor_load(dir) && ok;                             /* crsr 128, the pointing hand */
    sfx_load_bank(dir);
#undef PATH
    return ok;
}
