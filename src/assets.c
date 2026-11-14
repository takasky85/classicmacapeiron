/* assets.c - loads everything tools/extract_assets.py produced from the game's own resource forks. */
#include "apeiron.h"

Assets A;
void sfx_load_bank(const char *dir);     /* platform.c */

static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *p = (char *)malloc(n); memcpy(p, s, n); return p; }

/* The palette of an extracted BMP is the clut it was mapped to, so the game's two colour tables can
 * be read straight back out of the images the extractor wrote. */
static void clut_from_image(const Image *im, Rgb *out) { memcpy(out, im->pal, sizeof(Rgb) * 256); }

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

    snprintf(p, sizeof p, "%s/scores.txt", dir);
    f = fopen(p, "rb");
    if (!f) { SDL_Log("cannot open %s", p); return false; }
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
static bool load_about_page(const char *dir, int n)
{
    char p[1024], line[1024];
    snprintf(p, sizeof p, "%s/about_%d.txt", dir, n);
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
    A.about[n].op = ops;
    A.about[n].n = cnt;
    for (int i = 0; i < 8; i++) {           /* the icons drawn inline with the text */
        snprintf(p, sizeof p, "%s/about_%d_img%d.bmp", dir, n, i);
        FILE *t = fopen(p, "rb");
        if (!t) break;
        fclose(t);
        if (!image_load(&A.about[n].img[i], p)) break;
        A.about[n].nimg = i + 1;
    }
    return cnt > 0;
}

void assets_save_scores(const char *dir)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/scores.txt", dir);
    FILE *f = fopen(p, "wb");
    if (!f) return;
    fprintf(f, "volume %d\nmusic %d\n", A.pref_volume, A.pref_music);
    for (int i = 0; i < 10; i++)
        fprintf(f, "%s\t%ld\t%d\n", A.score_row[i].name, A.score_row[i].score, A.score_row[i].wave);
    fclose(f);
}

bool assets_load(const char *dir)
{
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

    ok = sprite_load(&A.shroom, dir, "shroom") && ok;
    ok = sprite_load(&A.pede, dir, "pede") && ok;
    ok = sprite_load(&A.shooter, dir, "shooter") && ok;
    ok = sprite_load(&A.shot, dir, "shot") && ok;
    ok = sprite_load(&A.flea, dir, "flea") && ok;
    ok = sprite_load(&A.gecko, dir, "gecko") && ok;
    ok = sprite_load(&A.scorpion, dir, "scorpion") && ok;
    ok = sprite_load(&A.scum, dir, "scum") && ok;
    ok = sprite_load(&A.points, dir, "points") && ok;
    ok = sprite_load(&A.explode_shooter, dir, "explode_shooter") && ok;
    ok = sprite_load(&A.flak, dir, "flak") && ok;
    ok = sprite_load(&A.splat, dir, "splat") && ok;
    ok = sprite_load(&A.splat_red, dir, "splat_red") && ok;
    ok = sprite_load(&A.coin, dir, "coin") && ok;
    ok = sprite_load(&A.scorpsplat, dir, "scorpsplat") && ok;
    ok = sprite_load(&A.yummie, dir, "yummie") && ok;
    ok = sprite_load(&A.ricochet, dir, "ricochet") && ok;
    ok = sprite_load(&A.fodder, dir, "fodder") && ok;
    ok = sprite_load(&A.bonuspoints, dir, "bonuspoints") && ok;
    ok = sprite_load(&A.shield, dir, "shield") && ok;
    ok = sprite_load(&A.geckosplat, dir, "geckosplat") && ok;
    ok = sprite_load(&A.numbers, dir, "numbers") && ok;
    ok = sprite_load(&A.wpn_none, dir, "wpn_none") && ok;
    static const char *wn[5] = { "wpn_curve", "wpn_freemove", "wpn_machinegun", "wpn_shield", "wpn_lock" };
    for (int i = 0; i < 5; i++) ok = sprite_load(&A.wpn[i], dir, wn[i]) && ok;

    ok = load_text_data(dir) && ok;
    for (int i = 0; i < 3; i++) ok = load_about_page(dir, i) && ok;
    sfx_load_bank(dir);
#undef PATH
    return ok;
}
