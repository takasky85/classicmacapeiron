/*
 * intro.c - [ApeironIntro], [FunkyFade] and the title screen ([UpdateTitleScreen], [TitleClick]).
 */
#include "apeiron.h"

/* [FunkyFade]: the transition sound, the picture turned grey, 5 ticks, then white, then black. */
static void funky_fade(void)
{
    sfx(SND_TRANSITION);
    hue_scale_grey();
    delay_ticks(5);
    fade_to_color(30, RGB_WHITE);
    fade_to_color(30, RGB_BLACK);
    while (sfx_playing(SND_TRANSITION)) { pump(); present(); SDL_Delay(2); }
}

static void draw_pict_in_window(const Image *im)     /* [DrawPictInWindow] */
{
    memset(fb, find_color(0, 0, 0), sizeof fb);
    draw_image(im, (SCREEN_W - im->w) / 2, (SCREEN_H - im->h) / 2);
}

void apeiron_intro(void)                             /* [ApeironIntro] */
{
    set_margin_pattern(NULL);
    set_border_rings(NULL, false);
    funky_fade();

    /* Ambrosia logo (PICT 999 + clut 999) with the "Level Clear" jingle (snd 144) */
    set_monitor_clut(A.ambrosia.pal, false);
    sfx(SND_LEVEL_CLEAR);
    draw_pict_in_window(&A.ambrosia);
    set_clut_to_color(RGB_BLACK, false);
    fade_to(A.ambrosia.pal, 30);
    while (sfx_playing(SND_LEVEL_CLEAR)) { pump(); present(); SDL_Delay(2); }

    funky_fade();

    /* Apeiron logo (PICT 889 + clut 889) with the intro theme (snd 143) */
    set_monitor_clut(A.intro_logo.pal, false);
    draw_pict_in_window(&A.intro_logo);
    sfx(SND_INTRO_THEME);
    delay_ticks(30);
    set_clut_to_color(RGB_BLACK, false);
    fade_to(A.intro_logo.pal, 30);

    /* 180 iterations of at least 3 ticks (~9 s): entries 1..246 rotate down by one each pass.
     * A click skips it, and it ends early when the theme finishes. */
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
        if (clicked) { sfx_halt_all(); break; }
        if (!sfx_playing(SND_INTRO_THEME)) break;
    }
    button();                                        /* FlushEvents */
    funky_fade();
    set_monitor_clut(A.title_clut, false);
}

/* ---- title screen ---------------------------------------------------------------------------
 * [UpdateTitleScreen]. Coordinates are those of the decompiled code (origin: top-left of the screen).
 */
static const int MENU_X = 410, MENU_ICON = 32, MENU_Y0 = 26, MENU_DY = 34;

static void draw_hline(int x0, int x1, int y, uint8_t c)
{ for (int x = x0; x <= x1; x++) if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) fb[y * SCREEN_W + x] = c; }
static void draw_vline(int x, int y0, int y1, uint8_t c)
{ for (int y = y0; y <= y1; y++) if (y >= 0 && y < SCREEN_H && x >= 0 && x < SCREEN_W) fb[y * SCREEN_W + x] = c; }

void draw_title_screen(void)
{
    uint8_t black = find_color(0, 0, 0);
    uint8_t white = find_color(0xFFFF, 0xFFFF, 0xFFFF);
    uint8_t yellow = find_color(0xFC00, 0xF37D, 0x0526);
    uint8_t blue = find_color(30000, 30000, 65535);
    uint8_t gray = find_color(30000, 30000, 30000);

    set_margin_pattern(&A.pat_title);
    memset(fb, black, sizeof fb);

    static const int ring_rgb[7][3] = {
        {30000,30000,65535},{30000,30000,65535},{40000,40000,65535},{40000,40000,65535},
        {50000,50000,65535},{40000,40000,65535},{30000,30000,65535} };
    uint8_t rings[7];
    for (int k = 0; k < 7; k++) rings[k] = find_color(ring_rgb[k][0], ring_rgb[k][1], ring_rgb[k][2]);
    set_border_rings(rings, true);

    draw_hline(0, 380, 150, blue);
    draw_hline(0, 380, SCREEN_H - 75, blue);
    draw_vline(380, 0, SCREEN_H, blue);
    draw_hline(380, SCREEN_W, 250, blue);
    draw_hline(380, SCREEN_W, 345, blue);

    draw_image(&A.title_logo, 0, 0);
    draw_text(&A.geneva9, 2, 148, A.str[32], white, 0);

    for (int i = 0; i < 6; i++) {
        draw_image(&A.btn_up, MENU_X, MENU_Y0 + i * MENU_DY);          /* cicn 1000 */
        draw_text(&A.newyork18, MENU_X + MENU_ICON + 7, MENU_Y0 + MENU_ICON + i * MENU_DY - 9, A.str[8 + i], white, 0);
    }

    for (int i = 0; i < 2; i++) {
        int left = MENU_X, top = 263 + i * 39;
        draw_text(&A.geneva9, left - 10, top - 2, A.str[15 + i], blue, STYLE_BOLD);
        draw_image(&A.btn_down, left, top);                            /* cicn 1002, decrease */
        draw_image(&A.btn_up, left + 120, top);                        /* cicn 1000, increase */
        const char *val = i == 0 ? A.str[23 + A.pref_volume] : A.str[29 + A.pref_music];
        int w = text_width(&A.geneva9, val, STYLE_BOLD);
        draw_text(&A.geneva9, left + (120 - w) / 2 + MENU_ICON / 2, top + MENU_ICON - 12, val, white, STYLE_BOLD);
    }

    int x = 10;
    x += draw_text(&A.geneva9, x, SCREEN_H - 0x32, A.str[2], blue, STYLE_BOLD) + 10;
    draw_text(&A.geneva9, x, SCREEN_H - 0x32, A.str[33], white, STYLE_BOLD);
    x = 10 + draw_text(&A.geneva9, 10, SCREEN_H - 0x26, A.str[3], blue, STYLE_BOLD) + 10;
    draw_text(&A.geneva9, x, SCREEN_H - 0x26, A.str[34], white, STYLE_BOLD);
    draw_text(&A.geneva9, 2, SCREEN_H - 2, A.str[1], white, STYLE_BOLD);

    draw_text(&A.newyork18, 12, 185, A.str[6], yellow, STYLE_UNDERLINE);
    draw_text(&A.newyork18, 232, 185, A.str[7], yellow, STYLE_UNDERLINE);
    draw_text(&A.newyork18, 315, 185, A.str[14], yellow, STYLE_UNDERLINE);

    for (int i = 0; i < 10; i++) {
        int base = (i + 1) * 20 + 185;
        char name[80], num[32];
        snprintf(name, sizeof name, "%.63s", A.score_row[i].name);
        size_t len = strlen(name);
        while (len > 2 && 20 + text_width(&A.newyork18, name, 0) > 212) {
            len -= 2; name[len - 1] = (char)0xC9; name[len] = 0;      /* Mac Roman ellipsis */
        }
        draw_text(&A.newyork18, 20, base, name, gray, 0);
        snprintf(num, sizeof num, "%ld", A.score_row[i].score);
        draw_text(&A.newyork18, 282 - text_width(&A.newyork18, num, 0), base, num, gray, 0);
        snprintf(num, sizeof num, "%d", A.score_row[i].wave);
        draw_text(&A.newyork18, 365 - text_width(&A.newyork18, num, 0), base, num, gray, 0);
    }
    draw_image(&A.title_morrison, 381, 346);
}

/* [TrackCIcon]: swap in the pressed icon while the button is held, then act on release. */
static void flash_button(const Image *hit, int x, int y)
{
    draw_image(hit, x, y);
    set_entries();
    delay_ticks(7);
}

/* [TitleClick]: the six menu entries, plus the sound-volume and music settings. */
int title_loop(void)
{
    flush_clicks();                          /* whatever got us here must not press a button */
    draw_title_screen();
    set_clut_to_color(RGB_BLACK, false);
    fade_to(A.title_clut, 30);

    for (;;) {
        int mx, my;
        if (take_click(&mx, &my)) {
            for (int i = 0; i < 6; i++) {
                int top = MENU_Y0 + i * MENU_DY;
                if (my >= top && my < top + MENU_ICON && mx >= MENU_X && mx < SCREEN_W) {
                    flash_button(&A.btn_up_hit, MENU_X, top);
                    return i;
                }
            }
            for (int i = 0; i < 2; i++) {            /* arrows either side of the value */
                int top = 263 + i * 39;
                if (my >= top && my < top + MENU_ICON && mx >= MENU_X && mx < MENU_X + 152) {
                    int *v = i == 0 ? &A.pref_volume : &A.pref_music;
                    int hi = i == 0 ? 5 : 1;
                    bool down = mx < MENU_X + 76;
                    flash_button(down ? &A.btn_down_hit : &A.btn_up_hit,
                                 down ? MENU_X : MENU_X + 120, top);
                    *v += down ? -1 : 1;
                    if (*v < 0) *v = 0;
                    if (*v > hi) *v = hi;
                    sfx(SND_BONK);
                    draw_title_screen();
                    set_entries();
                }
            }
        }
        if (key_down(SDL_SCANCODE_RETURN) || key_down(SDL_SCANCODE_SPACE)) return TITLE_PLAY;
        if (key_down(SDL_SCANCODE_ESCAPE)) return TITLE_QUIT;
        pump(); present(); SDL_Delay(8);
    }
}

/* ---- the instruction pages -------------------------------------------------------------------
 * [DoHelp] / [AboutWindowUpdate]: three pages (PICT 4000-4002) of QuickDraw text, each with live
 * animated creatures placed by [CreateSprites]. A click moves on; after the third it returns.
 */
typedef struct { const Sprite *s; int x, y, frame; int vx, vy, fx, fy; } PageSprite;

static int page_sprites(int page, PageSprite *out, int max)
{
    int n = 0;
    memset(out, 0, sizeof(PageSprite) * (size_t)max);   /* the bounce fields must start at zero */
    if (page == 0) {                       /* the bestiary, in the order the text describes them */
        const struct { const Sprite *s; int x, y, f; } row[] = {
            { &A.shooter,  24,  54, 1 }, { &A.shot,     30, 106, 1 },
            { &A.shroom,   24, 163, 5 },
            { &A.pede,      6, 225, 38 }, { &A.pede,    22, 225, 33 }, { &A.pede, 38, 225, 1 },
            { &A.flea,     24, 285, 1 },
            { &A.scorpion, 20, 330, 1 },   /* Larry  - the one that eats mushrooms */
            { &A.gecko,    12, 380, 1 },   /* Gordon - the one that poisons them   */
            { &A.fodder,   22, 440, 1 },   /* the spaceship                        */
        };
        for (size_t i = 0; i < sizeof row / sizeof row[0] && n < max; i++, n++) {
            out[n].s = row[i].s; out[n].x = row[i].x; out[n].y = row[i].y; out[n].frame = row[i].f;
        }
    } else if (page == 1) {
        const struct { const Sprite *s; int x, y, f; } row[] = {
            { &A.yummie, 24,  54, 1 },      /* the multiplier tile the first paragraph describes */
            { &A.shroom, 24, 112, 5 },      /* the psychedelic shroom                            */
            { &A.coin,   24, 163, 1 },      /* the coin that pops out of shrooms                 */
        };
        for (size_t i = 0; i < sizeof row / sizeof row[0] && n < max; i++, n++) {
            out[n].s = row[i].s; out[n].x = row[i].x; out[n].y = row[i].y; out[n].frame = row[i].f;
        }
    } else {                                /* [CreateSprites] makes 5-14 coins with [MakeCoin] */
        int k = 5 + SDL_rand(10);
        for (int i = 0; i < k && n < max; i++, n++) {
            out[n].s = &A.coin;
            out[n].x = SDL_rand(0x30);      /* x = FastRandom(48): up the left-hand side */
            out[n].y = SDL_rand(300);
            out[n].frame = 1 + SDL_rand(A.coin.count);
            out[n].vx = SDL_rand(0x46) * 4096 * (SDL_rand(2) ? 1 : -1);   /* FastRandom(70) << 12 */
            out[n].vy = -(SDL_rand(0x14) + 10) * 32768;                    /* (FastRandom(20)+10) * -0x8000 */
        }
    }
    return n;
}

/* [MakeCoin] gives each coin an upward kick and lets it fall back; they bounce off the floor. */
static void bounce_coins(PageSprite *ps, int n)
{
    for (int i = 0; i < n; i++) {
        if (!ps[i].vx && !ps[i].vy) continue;
        ps[i].fx += ps[i].vx; ps[i].fy += ps[i].vy;
        ps[i].vy += 16384;                      /* a quarter of a pixel a tick, so they arc */
        int x = ps[i].x + ps[i].fx / 65536, y = ps[i].y + ps[i].fy / 65536;
        if (y > SCREEN_H - 20) {                /* bounce off the floor, losing a little each time */
            ps[i].fy = (SCREEN_H - 20 - ps[i].y) * 65536;
            ps[i].vy = -(SDL_rand(0x14) + 10) * 32768;
        }
        /* [MakeCoin] starts them in x = FastRandom(48); they stay in that strip down the left */
        if (x < 0 || x > 48) ps[i].vx = -ps[i].vx;
    }
}

static const Font *page_font(int font, int size)
{
    if (font == 3) return size >= 12 ? &A.chicago12 : &A.geneva9;       /* 3 = Geneva */
    if (size >= 22) return &A.newyork24;
    if (size >= 16) return &A.newyork18;
    return &A.newyork14;
}

static void draw_about_page(int page, const PageSprite *ps, int nps, int anim)
{
    const AboutPage *p = &A.about[page];
    memset(fb, find_color(0, 0, 0), sizeof fb);
    for (int i = 0; i < p->n; i++) {
        const DrawOp *o = &p->op[i];
        if (o->kind == 'T')
            draw_text(page_font(o->font, o->size), o->x, o->y, o->text, (uint8_t)o->color, o->face);
        else if (o->kind == 'R')
            fill_rect(o->x, o->y, o->x2 - o->x, o->y2 - o->y, (uint8_t)o->color);
        else if (o->kind == 'B' && o->x2 < p->nimg) {
            /* the stored icon is padded out to a whole number of bytes, so it is clipped to the
             * destination rect the PICT asks for and its white backdrop is keyed out */
            const Image *im = &p->img[o->x2];
            for (int yy = 0; yy < o->y2 && yy < im->h; yy++)
                for (int xx = 0; xx < o->font && xx < im->w; xx++) {
                    uint8_t c = im->px[yy * im->w + xx];
                    int dx = o->x + xx, dy = o->y + yy;
                    if (c != 0 && dx >= 0 && dx < SCREEN_W && dy >= 0 && dy < SCREEN_H)
                        fb[dy * SCREEN_W + dx] = c;
                }
        }
    }
    for (int i = 0; i < nps; i++)
        draw_sprite(ps[i].s, ps[i].frame + anim % ps[i].s->count,
                    ps[i].x + ps[i].fx / 65536, ps[i].y + ps[i].fy / 65536);
}

void do_help(void)
{
    set_margin_pattern(NULL);
    set_border_rings(NULL, false);
    sfx(SND_SHOT_GECKO);                     /* [DoHelp] opens with this one */
    for (int page = 0; page < 3; page++) {
        if (page) sfx(SND_GECKO_APPEARS);    /* [AboutWindowClick] plays this on each page turn */
        sfx(SND_TRANSITION);
        hue_scale_grey();
        delay_ticks(5);
        fade_to_color(20, RGB_WHITE);
        fade_to_color(20, RGB_BLACK);

        PageSprite ps[24] = { 0 };
        int nps = page_sprites(page, ps, 24);
        draw_about_page(page, ps, nps, 0);
        set_clut_to_color(RGB_BLACK, false);
        fade_to(A.game_clut, 20);

        button();                            /* FlushEvents */
        for (int anim = 0; ; anim++) {
            bounce_coins(ps, nps);
            draw_about_page(page, ps, nps, anim);
            set_entries();
            if (button()) break;
            SDL_Delay((int)(2 * MAC_TICK_MS));   /* the same 2-tick frame the game runs at */
        }
    }
    sfx(SND_TRANSITION);
    fade_to_color(20, RGB_BLACK);
    set_monitor_clut(A.title_clut, false);
    flush_clicks();                          /* [AboutWindowClick]'s FlushEvents */
}
