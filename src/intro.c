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
    cursor_set(CURSOR_NONE);                         /* [ApeironIntro] calls HideCursor */
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
/* The original starts the six menu entries at y 26 and puts the two settings at 263 and 302, with
 * rules at 250 and 345. Shifting that whole column up by sixteen leaves room for the Options entry
 * on the end of it without crowding anything. */
static const int MENU_X = 410, MENU_ICON = 32, MENU_Y0 = 6, MENU_DY = 34;
static const int SET_Y0 = 232, SET_DY = 39;        /* Sound Volume, then Music */
static const int RULE_TOP = 218, RULE_BOT = 345;   /* the two rules that bracket the settings */

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
    draw_hline(380, SCREEN_W, RULE_TOP, blue);
    draw_hline(380, SCREEN_W, RULE_BOT, blue);

    draw_image(&A.title_logo, 0, 0);
    draw_text(&A.geneva9, 2, 148, A.str[32], white, 0);

    /* The six entries of STR# 133, with "Options" standing in for the original's "Controls" -
     * the controls themselves are a page of the options screen now. */
    for (int i = 0; i < 6; i++) {
        const char *label = i == TITLE_CONTROLS ? "Options" : A.str[8 + i];
        draw_image(&A.btn_up, MENU_X, MENU_Y0 + i * MENU_DY);          /* cicn 1000 */
        draw_text(&A.newyork18, MENU_X + MENU_ICON + 7, MENU_Y0 + MENU_ICON + i * MENU_DY - 9, label, white, 0);
    }

    for (int i = 0; i < 2; i++) {
        int left = MENU_X, top = SET_Y0 + i * SET_DY;
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

    /* Which build this is, in the same yymmddhhmm shape the delivered zip is named with, so a
     * screenshot can be matched to the archive it came from. A.str[1] above is the original's own
     * version line; this sits to the right of it, short of the rule at x 380. */
    {
        static const char mons[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
        const char *d = __DATE__, *t = __TIME__;      /* "Nov 14 2026", "17:49:00" */
        int mon = 0;
        for (int i = 0; i < 12; i++)
            if (!strncmp(mons + i * 3, d, 3)) { mon = i + 1; break; }
        char stamp[64];
        snprintf(stamp, sizeof stamp, "SDL3 port  %c%c%02d%c%c%c%c%c%c",
                 d[9], d[10], mon, d[4] == ' ' ? '0' : d[4], d[5], t[0], t[1], t[3], t[4]);
        int sw = text_width(&A.geneva9, stamp, 0);
        draw_text(&A.geneva9, 376 - sw, SCREEN_H - 2, stamp, gray, 0);
    }

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
    cursor_set(CURSOR_HAND);                 /* [main]: SetCCursor(crsr 128) */
    draw_title_screen();
    set_clut_to_color(RGB_BLACK, false);
    fade_to(A.title_clut, 30);

    Uint64 idle_since = SDL_GetTicks();
    for (;;) {
        /* left alone for twenty seconds, the game shows itself off; coming back from the demo
         * puts the title screen up again as if nothing had happened */
        if (enh[ENH_ATTRACT] && SDL_GetTicks() - idle_since > 20000) {
            run_attract();
            set_monitor_clut(A.title_clut, false);
            flush_clicks();
            cursor_set(CURSOR_HAND);
            draw_title_screen();
            set_clut_to_color(RGB_BLACK, false);
            fade_to(A.title_clut, 20);
            idle_since = SDL_GetTicks();
            continue;
        }
        int mx, my;
        if (take_click(&mx, &my)) {
            idle_since = SDL_GetTicks();
            bool hit = false;
            for (int i = 0; i < 6; i++) {
                int top = MENU_Y0 + i * MENU_DY;
                if (my >= top && my < top + MENU_ICON && mx >= MENU_X && mx < SCREEN_W) {
                    flash_button(&A.btn_up_hit, MENU_X, top);
                    return i;
                }
            }
            for (int i = 0; i < 2; i++) {            /* arrows either side of the value */
                int top = SET_Y0 + i * SET_DY;
                if (my >= top && my < top + MENU_ICON && mx >= MENU_X && mx < MENU_X + 152) {
                    int *v = i == 0 ? &A.pref_volume : &A.pref_music;
                    int hi = i == 0 ? 5 : 1;
                    bool down = mx < MENU_X + 76;
                    flash_button(down ? &A.btn_down_hit : &A.btn_up_hit,
                                 down ? MENU_X : MENU_X + 120, top);
                    *v += down ? -1 : 1;
                    if (*v < 0) *v = 0;
                    if (*v > hi) *v = hi;
                    sfx(SND_YUMMIE_APPEARS);     /* [TitleClick]: *(TOC+0x169c) = snd 134 */
                    draw_title_screen();
                    set_entries();
                    hit = true;
                }
            }
            /* [TitleClick] ends with an else that plays DAT_1001dc0c - snd 138, "Groucho Ouch" -
             * at priority 2 for a click that lands on none of its items. */
            if (!hit) sfx(SND_GROUCHO_OUCH);
        }
        char k;
        if (any_input()) idle_since = SDL_GetTicks();
        while (take_key(&k)) {               /* [DoKey]: a c i p q x z */
            idle_since = SDL_GetTicks();
            switch (k) {
            /* [DoKey] plays DAT_1001dc04 - snd 130 "Squish" - on every letter it knows, and
             * DAT_1001dc0c - snd 138 "Groucho Ouch" - on one it does not. */
            case 'A': case 'a': sfx(SND_SQUISH); do_about(); break;
            case 'I': case 'i': sfx(SND_SQUISH); return TITLE_INSTRUCTIONS;
            case 'C': case 'c': sfx(SND_SQUISH); return TITLE_OPTIONS;
            case 'P': case 'p': sfx(SND_SQUISH); return TITLE_PLAY;
            case 'Q': case 'q': sfx(SND_SQUISH); return TITLE_QUIT;
            case 'Z': case 'z': sfx(SND_SQUISH); return TITLE_CLEAR_SCORES;
            case 'O': case 'o': sfx(SND_SQUISH); return TITLE_OPTIONS;
            case 'X': case 'x': sfx(SND_SQUISH); zappa_tribute(); break;
            default: sfx(SND_GROUCHO_OUCH); break;
            }
        }
        if (key_down(SDL_SCANCODE_RETURN)) return TITLE_PLAY;
        if (key_down(SDL_SCANCODE_ESCAPE)) return TITLE_QUIT;
        pump(); present(); SDL_Delay(8);
    }
}

/* ---- the instruction pages -------------------------------------------------------------------
 * [DoHelp] / [AboutWindowUpdate]: three pages (PICT 4000-4002) of QuickDraw text, each with live
 * animated creatures placed by [CreateSprites]. A click moves on; after the third it returns.
 */
typedef struct {
    const Sprite *s;
    int x, y, frame;        /* frame is the first of this sprite's own run */
    /* The frame routine [CreateSprites] gives this sprite, as a first/last/step: the original's
     * procs are all "frame += step; if (frame > last) frame = first". last == 0 means the sprite's
     * own frame count, and step == 0 means it never animates at all (NoFrameProc). */
    int first, last, step;
    bool thrown;            /* a Hintiary coin, on BounceMoveProc rather than standing still */
    int vx, vy, fx, fy;
} PageSprite;

static void coin_launch(PageSprite *c);

static int page_sprites(int page, PageSprite *out, int max)
{
    /* [CreateSprites] with the frame routine each sprite is given, read out of the binary:
     *   GenericFrameProc  frame++, wrap to 1 at the sprite's own count
     *   NoFrameProc       never changes frame
     *   myGeckoFrameProc  frame++, wrap to 1 past 8   - Gordon uses only the first eight
     *   HeadFrameProc     frame++, wrap to 1 past 14
     *   LinkFrameProc     frame++, wrap to 32 past 45
     *   MushFrameProc     frame += 4, wrap to 5 at the count - the psychedelic shroom's colours
     */
    int n = 0;
    memset(out, 0, sizeof(PageSprite) * (size_t)max);   /* the bounce fields must start at zero */
    typedef struct { const Sprite *s; int x, y, f, first, last, step; } PageRow;
    const PageRow *row;
    size_t nrow;
    if (page == 0) {                       /* the bestiary, in the order the text describes them */
        static const PageRow bestiary[] = {
            { &A.shooter,  24,  54,  1,  1,  0, 1 },   /* GenericFrameProc            */
            { &A.shot,     30, 106,  1,  1,  0, 1 },
            { &A.shroom,   24, 163,  5,  0,  0, 0 },   /* NoFrameProc - it just sits  */
            { &A.pede,      6, 225, 38, 32, 45, 1 },   /* LinkFrameProc: two bodies.. */
            { &A.pede,     22, 225, 33, 32, 45, 1 },
            { &A.pede,     38, 225,  1,  1, 14, 1 },   /* ..behind a HeadFrameProc    */
            { &A.flea,     24, 285,  1,  1,  0, 1 },
            { &A.scorpion, 20, 330,  1,  1,  0, 1 },   /* Larry  - eats mushrooms     */
            { &A.gecko,    12, 380,  1,  1,  8, 1 },   /* Gordon - myGeckoFrameProc   */
            { &A.fodder,   22, 440,  1,  1,  0, 1 },   /* the spaceship               */
        };
        row = bestiary; nrow = sizeof bestiary / sizeof bestiary[0];
    } else if (page == 1) {
        /* [CreateSprites]'s second branch. The coin is on GenericFrameProc with a fixed position,
         * so it spins where it stands rather than being thrown the way [MakeCoin] throws the
         * Hintiary ones; the multiplier tile does not animate at all. */
        static const PageRow yummiary[] = {
            { &A.yummie, 24,  54, 1, 0, 0, 0 },   /* NoFrameProc: the x2 tile, held still */
            { &A.shroom, 24, 112, 1, 5, 0, 4 },   /* MushFrameProc: 1, then 5, 9, 13, ... */
            { &A.coin,   24, 163, 1, 1, 0, 1 },   /* GenericFrameProc: its whole spin     */
        };
        row = yummiary; nrow = sizeof yummiary / sizeof yummiary[0];
    } else {                                /* [CreateSprites] makes 5-14 coins with [MakeCoin] */
        int k = 5 + SDL_rand(10);
        for (int i = 0; i < k && n < max; i++, n++) {
            out[n].s = &A.coin;
            out[n].x = out[n].y = 0;        /* the coin carries its own position in fx/fy */
            out[n].thrown = true;
            out[n].first = 1; out[n].last = 0; out[n].step = 1;
            coin_launch(&out[n]);
        }
        return n;
    }
    for (size_t i = 0; i < nrow && n < max; i++, n++) {
        out[n].s = row[i].s; out[n].x = row[i].x; out[n].y = row[i].y;
        out[n].frame = row[i].f;
        out[n].first = row[i].first; out[n].last = row[i].last; out[n].step = row[i].step;
    }
    return n;
}

/* The coin's own per-frame routine, at 0x10001828 - the one [MakeCoin] installs at sprite+0x0c:
 *
 *     vy += 0x10000;  y += vy;  x += vx;
 *     if ((y >> 16) + height > 479) {          // the floor
 *         y = (480 - height) << 16;
 *         if ((vy >> 16) < 4 && vy >= 0) { hide the coin; MakeCoin(); }   // too slow to bounce
 *         else vy = -((vy >> 16) - 1) << 16;   // bounce, a pixel a frame slower each time
 *     }
 *     if (y < 0) y = 0;
 *     if (x < 0)          { vx = -vx; x = 0; }
 *     if ((x >> 16) > 48) { vx = -vx; x = 48 << 16; }
 *
 * So gravity is exactly one pixel per frame per frame, and the coins are held inside the 48-pixel
 * strip they spawn in - which is why they bounce down the left-hand side.
 */
static void coin_launch(PageSprite *c)          /* [MakeCoin]'s own starting values */
{
    c->vx = SDL_rand(0x46) * 4096 * (SDL_rand(2) ? 1 : -1);
    c->vy = -(SDL_rand(0x14) + 10) * 32768;
    c->fx = SDL_rand(0x30) * 65536;
    c->fy = SDL_rand(300) * 65536;
    c->frame = 1 + SDL_rand(A.coin.count);
    if (!c->vx) c->vx = 0x1000;             /* never let it come to a dead stop in mid-air */
}

static void bounce_coins(PageSprite *ps, int n)
{
    for (int i = 0; i < n; i++) {
        if (!ps[i].thrown) continue;        /* only the Hintiary coins are on BounceMoveProc */
        int h = ps[i].s->h;
        ps[i].vy += 0x10000;
        ps[i].fy += ps[i].vy;
        ps[i].fx += ps[i].vx;
        if ((ps[i].fy >> 16) + h > SCREEN_H - 1) {
            ps[i].fy = (SCREEN_H - h) << 16;
            int sp = ps[i].vy >> 16;
            if (sp < 4 && ps[i].vy >= 0) coin_launch(&ps[i]);   /* it is thrown again */
            else ps[i].vy = -(sp - 1) << 16;
        }
        if (ps[i].fy < 0) ps[i].fy = 0;
        if (ps[i].fx < 0) { ps[i].vx = -ps[i].vx; ps[i].fx = 0; }
        if ((ps[i].fx >> 16) > 0x30) { ps[i].vx = -ps[i].vx; ps[i].fx = 0x30 << 16; }
    }
}

const Font *page_font(int font, int size)
{
    if (font == 3) return size >= 12 ? &A.chicago12 : &A.geneva9;       /* 3 = Geneva */
    if (size >= 22) return &A.newyork24;
    if (size >= 16) return &A.newyork18;
    return &A.newyork14;
}

/* One step of this sprite's own frame routine, run once per page frame exactly as [AnimateFrame]
 * runs the original's. Each of them is the same shape - add the step, and wrap back to `first`
 * once past `last` - so one line covers the lot. */
static void page_step(PageSprite *p)
{
    if (p->step == 0) return;                       /* NoFrameProc */
    int last = p->last ? p->last : p->s->count;     /* 0 means the sprite's own frame count */
    p->frame += p->step;
    if (p->frame > last) p->frame = p->first;
}

static void draw_about_page(int page, const PageSprite *ps, int nps)
{
    const AboutPage *p = &A.about[page];
    memset(fb, find_color(0, 0, 0), sizeof fb);
    for (int i = 0; i < p->n; i++) {
        const DrawOp *o = &p->op[i];
        if (o->kind == 'T')
            draw_text(page_font(o->font, o->size), o->x, o->y, o->text, (uint8_t)o->color, o->face);
        else if (o->kind == 'R')
            fill_rect(o->x, o->y, o->x2 - o->x, o->y2 - o->y, (uint8_t)o->color);
        else if (o->kind == 'L')            /* the red rule the page title's line ends with */
            draw_line(o->x, o->y, o->x2, o->y2, (uint8_t)o->color);
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
        draw_sprite(ps[i].s, ps[i].frame,
                    ps[i].x + (ps[i].fx >> 16), ps[i].y + (ps[i].fy >> 16));
}

void do_help(void)
{
    set_margin_pattern(NULL);
    set_border_rings(NULL, false);
    cursor_set(CURSOR_HAND);                 /* [AboutWindowUpdate]: SetCCursor(crsr 128) */
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
        draw_about_page(page, ps, nps);
        set_clut_to_color(RGB_BLACK, false);
        fade_to(A.game_clut, 20);

        button();                            /* FlushEvents */
        for (;;) {
            /* [AnimateFrame] runs every sprite's frame routine once a frame, then its move one */
            for (int i = 0; i < nps; i++) page_step(&ps[i]);
            bounce_coins(ps, nps);
            draw_about_page(page, ps, nps);
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

/* ---- the modal dialogs ------------------------------------------------------------------------
 * [DoAbout] puts up DLOG 150: a 268x284 window holding PICT 151 (the logo) at 77,15, PICT 150 (the
 * credits) at 46,84, PICT 153 at 2,274 and an OK button at 197,260.
 * [ZappaTribute] puts up DLOG 162: 233x357 holding PICT 5555 - which is drawn text, not a picture -
 * at 4,5 with an OK button at 83,333. It is what pressing x on the title screen shows.
 */
/* A dBoxProc frame sits OUTSIDE the content rect, which is what DLOG 150 and 162 measure and what
 * their DITL items are placed against - drawing it inside ate four pixels off every edge, which is
 * what pushed the copyright strip (DITL item 4, flush with the bottom at y 274) onto the border. */
void dialog_frame(int bx, int by, int bw, int bh, uint8_t face, uint8_t edge)
{
    fill_rect(bx - 4, by - 4, bw + 8, bh + 8, edge);
    fill_rect(bx - 3, by - 3, bw + 6, bh + 6, face);
    fill_rect(bx - 1, by - 1, bw + 2, bh + 2, edge);
    fill_rect(bx, by, bw, bh, face);
}

/* The OK button at the DITL's own rect (197,260 in DLOG 150; 83,333 in 162), 67x20. A Mac push
 * button is a round rect with ovalWidth and ovalHeight 16, and SetDialogDefaultItem frames a second
 * one three pixels thick, four pixels outside it. `hilite` is the pressed state: HiliteControl
 * inverts the button's own area while the mouse is held in it. */
void dialog_ok_hilite(int x, int y, int w, uint8_t face, uint8_t edge, bool hilite)
{
    uint8_t body = hilite ? edge : face, ink = hilite ? face : edge;
    fill_round_rect(x - 4, y - 4, w + 8, 28, 12, edge);           /* the default item's ring */
    fill_round_rect(x - 1, y - 1, w + 2, 22, 9, face);
    fill_round_rect(x, y, w, 20, 8, edge);
    fill_round_rect(x + 1, y + 1, w - 2, 18, 8, body);
    const char *ok = "OK";
    draw_text(&A.chicago12, x + (w - text_width(&A.chicago12, ok, 0)) / 2, y + 14, ok, ink, 0);
}

void dialog_ok(int x, int y, int w, uint8_t face, uint8_t edge)
{
    dialog_ok_hilite(x, y, w, face, edge, false);
}

/* The press: the button inverts for eight ticks, the way it does when you click one and hold. */
void dialog_ok_press(int x, int y, int w, uint8_t face, uint8_t edge)
{
    dialog_ok_hilite(x, y, w, face, edge, true);
    set_entries();
    present();
    SDL_Delay(120);
}

/* ModalDialog: OK, Return or a click closes it. The button flashes on the way out. */
static int ok_x, ok_y, ok_w; static uint8_t ok_face, ok_edge;
static bool dialog_wait(void)
{
    for (;;) {
        pump();
        present();
        if (take_click_only() || key_down(SDL_SCANCODE_RETURN) || key_down(SDL_SCANCODE_SPACE) ||
            key_down(SDL_SCANCODE_ESCAPE)) {
            if (ok_w) dialog_ok_press(ok_x, ok_y, ok_w, ok_face, ok_edge);
            return true;
        }
        SDL_Delay(10);
    }
}

/* [ZoomShowWindow]: fifteen expanding outlines, a tick apart, before the dialog appears - and the
 * same run backwards when it goes away. The original draws them through the window manager's port;
 * here they are drawn over the title screen, which is what is behind the dialog anyway. */
static void dialog_zoom(int bx, int by, int bw, int bh, bool opening, uint8_t edge)
{
    for (int k = 0; k < 15; k++) {
        int n = opening ? k + 1 : 15 - k;
        int w = bw * n / 15, h = bh * n / 15;
        int x = bx + (bw - w) / 2, y = by + (bh - h) / 2;
        draw_title_screen();
        draw_line(x, y, x + w - 1, y, edge);
        draw_line(x, y + h - 1, x + w - 1, y + h - 1, edge);
        draw_line(x, y, x, y + h - 1, edge);
        draw_line(x + w - 1, y, x + w - 1, y + h - 1, edge);
        set_entries();
        delay_ticks(1);
    }
}

/* The sound handles turned out to be resolvable after all: they are a block loaded in order from
 * TOC+0x1674 up, so an offset names a sound. [DoAbout] plays *(TOC+0x1698) = DAT_1001dc18 =
 * ST_LoadSound(0x87) = snd 135 "Extra Ship". Closing is silent - the dialog is just HideWindow()n,
 * with no zoom back - and [ZappaTribute] has no sound of its own at all; what you hear opening
 * either of them is [DoKey]'s click on the letter that opened it. */
#define SND_ABOUT_OPEN  135

static void dialog_close(int bx, int by, int bw, int bh, uint8_t edge)
{
    (void)bx; (void)by; (void)bw; (void)bh; (void)edge;   /* HideWindow: no reverse zoom, no sound */
    draw_title_screen();
    set_entries();
    flush_clicks();
}

void do_about(void)                  /* [DoAbout]: DLOG 150, a plain white dialog */
{
    uint8_t face = find_color(0xFFFF, 0xFFFF, 0xFFFF);
    uint8_t edge = find_color(0, 0, 0);
    int bw = 268, bh = 284, bx = (SCREEN_W - bw) / 2, by = (SCREEN_H - bh) / 2;

    sfx(SND_ABOUT_OPEN);
    dialog_zoom(bx, by, bw, bh, true, edge);
    draw_title_screen();
    dialog_frame(bx, by, bw, bh, face, edge);
    draw_image(&A.about_logo, bx + 77, by + 15);
    draw_image(&A.about_credits, bx + 46, by + 84);
    draw_image(&A.about_strip, bx + 2, by + 274);
    dialog_ok(bx + 197, by + 260, 67, face, edge);
    ok_x = bx + 197; ok_y = by + 260; ok_w = 67; ok_face = face; ok_edge = edge;
    set_entries();
    flush_clicks();

    /* DITL item 6 is a userItem at (118,225)-(268,241) - the line after "thanks:" in the credits
     * picture - and [DoAbout] hangs a draw proc on it. STR# 150 is what it works through. */
    for (int t = 0;; t++) {
        if (A.nthanks > 0) {
            const char *s = A.thanks[(t / 40) % A.nthanks];
            fill_rect(bx + 118, by + 225, 150, 16, face);
            if (*s) draw_text(&A.chicago12, bx + 118, by + 237, s, edge, 0);
            set_entries();
        }
        pump();
        present();
        if (take_click_only() || key_down(SDL_SCANCODE_RETURN) ||
            key_down(SDL_SCANCODE_SPACE) || key_down(SDL_SCANCODE_ESCAPE)) {
            if (ok_w) dialog_ok_press(ok_x, ok_y, ok_w, ok_face, ok_edge);
            break;
        }
        delay_ticks(1);
    }
    dialog_close(bx, by, bw, bh, edge);
}

void zappa_tribute(void)             /* [ZappaTribute] */
{
    uint8_t face = find_color(0xFFFF, 0xFFFF, 0xFFFF);
    uint8_t edge = find_color(0, 0, 0);
    int bw = 233, bh = 357, bx = (SCREEN_W - bw) / 2, by = (SCREEN_H - bh) / 2;

    dialog_zoom(bx, by, bw, bh, true, edge);
    draw_title_screen();
    dialog_frame(bx, by, bw, bh, face, edge);
    for (int i = 0; i < A.poem.n; i++) {
        const DrawOp *o = &A.poem.op[i];
        if (o->kind != 'T') continue;
        draw_text(page_font(o->font, o->size), bx + o->x, by + o->y, o->text, edge, o->face);
    }
    dialog_ok(bx + 83, by + 333, 68, face, edge);
    ok_x = bx + 83; ok_y = by + 333; ok_w = 68; ok_face = face; ok_edge = edge;
    set_entries();
    flush_clicks();
    /* Holding the x down keeps the gun going: snd 133, the ship's laser, retriggered as soon as it
     * finishes, which is what the machine-gun power-up sounds like. */
    while (key_down(SDL_SCANCODE_X)) {
        if (!sfx_playing(SND_SHIP_LASER)) sfx(SND_SHIP_LASER);
        pump();
        present();
        delay_ticks(1);
    }
    flush_clicks();
    dialog_wait();
    dialog_close(bx, by, bw, bh, edge);
}

/* ---- the options screen ------------------------------------------------------------------------
 * Not in the original. Five pages reached from the title screen, where Controls used to be (the
 * original's Controls text is on the Controls page here) or with the o key.
 */
bool cheat[CH_COUNT];

static const struct { const char *code, *what; } CHEATS[CH_COUNT] = {
    { "DJARUM",   "pass through mushrooms" },
    { "HECTOR",   "eight shooters" },
    { "NALA",     "invincible" },
    { "NICE BOX", "the curve gun" },
    { "SQUISH",   "the machine gun" },
    { "SNAPPLE",  "the padlock" },
    { "PERNTS",   "50,000 points to start  (high score still counts)" },
    { "USMC",     "5,000 on the bonus each wave" },
    { "SCUMDOG",  "keep the high score even when cheating" },
    { "MORRISON", "press M in play to finish the wave" },
    { "NUKE EM",  "press N in play to clear the mushrooms around you" },
};

enum { PG_GRAPHICS = 0, PG_CONTROLS, PG_ASSETS, PG_ENHANCE, PG_DEBUG, PG_COUNT };
static const char *PAGE_NAME[PG_COUNT] = {
    "Graphics", "Controls", "Assets and cheats", "Enhancements", "Debug"
};

/* One line on a page: a box to click, a label, a note, and what it is bound to. */
enum { ROW_RADIO, ROW_CHECK, ROW_VALUE, ROW_TEXT, ROW_NOTE };
typedef struct { int kind, group, index; const char *label, *note; } Row;

static int page_rows(int page, Row *r, int max)
{
    int n = 0;
    #define ADD(k, g, i, l, nt) do { if (n < max) { r[n].kind=(k); r[n].group=(g); r[n].index=(i); \
                                     r[n].label=(l); r[n].note=(nt); n++; } } while (0)
    switch (page) {
    case PG_GRAPHICS:
        ADD(ROW_TEXT, 0, 0, "Scaling", NULL);
        ADD(ROW_RADIO, 1, SCALE_INTEGER, "Whole numbers", "sharp pixels, black bars - what the game expects");
        ADD(ROW_RADIO, 1, SCALE_FIT,     "Fill the window", "no bars, uneven pixels");
        ADD(ROW_CHECK, 8, 0, "Open maximized", "fill the screen on the way in, letterboxed");
        ADD(ROW_NOTE, 0, 0, "F11 toggles fullscreen at any time.", NULL);
        break;
    case PG_CONTROLS:
        ADD(ROW_TEXT, 0, 0, "Devices", NULL);
        ADD(ROW_CHECK, 2, 0, "Mouse",    "move the shooter; the button fires");
        ADD(ROW_CHECK, 2, 1, "Keyboard", "arrows or WASD; space or control fires");
        ADD(ROW_CHECK, 2, 2, "Controller", NULL);
        ADD(ROW_VALUE, 3, 0, "Stick dead zone", NULL);
        ADD(ROW_NOTE, 0, 0, "In play: Caps Lock or Start pauses, Escape quits to the title.", NULL);
        break;
    case PG_ASSETS:
        ADD(ROW_TEXT, 0, 0, "Artwork and sound", NULL);
        for (int i = 0; i < PACK_COUNT; i++) {
            static const char *note[PACK_COUNT] = {
                "the game as it shipped", "a complete replacement sprite set",
                "four replaced sprites, the rest as it shipped", "its own music",
            };
            ADD(ROW_RADIO, 4, i, PACK_LABEL[i], note[i]);
        }
        ADD(ROW_TEXT, 0, 0, "Cheats", "any but PERNTS and SCUMDOG forfeit the high score");
        for (int i = 0; i < CH_COUNT; i++) ADD(ROW_CHECK, 5, i, CHEATS[i].code, CHEATS[i].what);
        break;
    case PG_ENHANCE:
        ADD(ROW_TEXT, 0, 0, "Beyond the original", NULL);
        ADD(ROW_CHECK, 6, ENH_WEAPON_FLY,  "Weapon flies to its box",
            "the icon arcs from the coin over to the score panel");
        ADD(ROW_CHECK, 6, ENH_WEAPON_ANIM, "Weapon box animates",
            "the shield spins, the spare shooters turn, a slot flashes as it ends");
        ADD(ROW_CHECK, 6, ENH_ATTRACT, "Attract mode",
            "the game plays itself if the title is left alone");
        ADD(ROW_CHECK, 6, ENH_NO_PSYC, "No screen flashing",
            "keeps the psychedelic mushroom's colour cycling off");
        ADD(ROW_VALUE, 9, 0, "Starting wave", NULL);
        ADD(ROW_NOTE, 0, 0, "Starting past the first forfeits the high score.", NULL);
        ADD(ROW_NOTE, 0, 0, "Still to come: a Mac OS boot screen on the way in.", NULL);
        break;
    case PG_DEBUG:
        ADD(ROW_TEXT, 0, 0, "Spawning", NULL);
        ADD(ROW_CHECK, 7, 0, "Spawn keys", "the function keys call things up in play");
        ADD(ROW_NOTE, 0, 0, "F1 flea, F2 Larry, F3 Gordon, F4 the UFO, F5 a coin,", NULL);
        ADD(ROW_NOTE, 0, 0, "F6 a multiplier tile, F7 a psychedelic shroom, F8 an uprooted one.", NULL);
        ADD(ROW_NOTE, 0, 0, "Each one arrives at the top of the field and behaves as it", NULL);
        ADD(ROW_NOTE, 0, 0, "normally would.", NULL);
        break;
    }
    #undef ADD
    return n;
}

bool debug_spawn;

static bool row_state(const Row *r)
{
    switch (r->group) {
    case 1: return opt_scale == r->index;
    case 2: return r->index == 0 ? opt_mouse : (r->index == 1 ? opt_keyboard : opt_pad);
    case 4: return assets_pack() == r->index;
    case 5: return cheat[r->index];
    case 6: return enh[r->index];
    case 7: return debug_spawn;
    case 8: return opt_maximize;
    default: return false;
    }
}

static void row_toggle(const Row *r)
{
    switch (r->group) {
    case 1: opt_scale = r->index; break;
    case 2:
        if (r->index == 0) opt_mouse = !opt_mouse;
        else if (r->index == 1) opt_keyboard = !opt_keyboard;
        else opt_pad = !opt_pad;
        break;
    case 8: opt_maximize = !opt_maximize; break;
    case 9:
        opt_start_wave++;
        if (opt_start_wave > 30) opt_start_wave = 1;
        break;
    case 3:
        opt_pad_deadzone += 5;
        if (opt_pad_deadzone > 50) opt_pad_deadzone = 5;
        break;
    case 4: assets_set_pack(r->index); break;
    case 5: cheat[r->index] = !cheat[r->index]; break;
    case 6: enh[r->index] = !enh[r->index]; break;
    case 7: debug_spawn = !debug_spawn; break;
    }
}

#define OPT_MAX_ROWS 24
static const int TAB_Y = 56, TAB_H = 20, ROW_Y0 = 96, ROW_DY = 20, ROW_X = 40;

static void draw_options(int page, const Row *row, int nrow)
{
    uint8_t black = find_color(0, 0, 0);
    uint8_t white = find_color(0xFFFF, 0xFFFF, 0xFFFF);
    uint8_t red   = find_color(0xFFFF, 0, 0);
    uint8_t grey  = find_color(0x8000, 0x8000, 0x8000);
    uint8_t blue  = find_color(30000, 30000, 65535);

    memset(fb, black, sizeof fb);
    set_margin_pattern(NULL);
    set_border_rings(NULL, false);

    int w = draw_text(&A.newyork24, 24, 40, "Apeiron Options", white, 0);
    draw_line(24 + w + 12, 34, SCREEN_W - 24, 34, red);    /* the instruction pages' red rule */

    int x = 24;                                            /* the page tabs */
    for (int i = 0; i < PG_COUNT; i++) {
        int tw = text_width(&A.geneva9, PAGE_NAME[i], STYLE_BOLD) + 16;
        if (i == page) {
            fill_rect(x, TAB_Y, tw, TAB_H, blue);
            draw_text(&A.geneva9, x + 8, TAB_Y + 14, PAGE_NAME[i], white, STYLE_BOLD);
        } else {
            fill_rect(x, TAB_Y, tw, TAB_H, white);
            fill_rect(x + 1, TAB_Y + 1, tw - 2, TAB_H - 2, black);
            draw_text(&A.geneva9, x + 8, TAB_Y + 14, PAGE_NAME[i], white, 0);
        }
        x += tw + 6;
    }
    draw_line(24, TAB_Y + TAB_H + 3, SCREEN_W - 24, TAB_Y + TAB_H + 3, grey);

    for (int i = 0; i < nrow; i++) {
        int y = ROW_Y0 + i * ROW_DY;
        const Row *r = &row[i];
        if (r->kind == ROW_TEXT) {                 /* a section heading */
            int lw = draw_text(&A.newyork18, ROW_X - 16, y + 11, r->label, blue, 0);
            if (r->note) draw_text(&A.geneva9, ROW_X - 10 + lw, y + 11, r->note, grey, 0);
            continue;
        }
        if (r->kind == ROW_NOTE) {                 /* a sentence, in the page's own small face */
            draw_text(&A.geneva9, ROW_X - 16, y + 11, r->label, grey, 0);
            continue;
        }
        if (r->kind == ROW_VALUE) {
            char buf[64];
            if (r->group == 9) snprintf(buf, sizeof buf, "%s:  %d", r->label, opt_start_wave);
            else snprintf(buf, sizeof buf, "%s:  %d%%", r->label, opt_pad_deadzone);
            draw_text(&A.geneva9, ROW_X + 22, y + 11, buf, white, 0);
            draw_text(&A.geneva9, ROW_X + 190, y + 11, "click to change", grey, 0);
            continue;
        }
        bool on = row_state(r);
        fill_rect(ROW_X, y, 13, 13, white);
        fill_rect(ROW_X + 1, y + 1, 11, 11, black);
        if (on) fill_rect(ROW_X + 3, y + 3, 7, 7, r->kind == ROW_RADIO ? blue : white);
        draw_text(&A.geneva9, ROW_X + 22, y + 11, r->label, white, 0);
        if (r->note) {
            /* the notes sit in a column of their own; one long enough to reach the right margin is
             * cut back word by word rather than running off the edge of the screen */
            char buf[160];
            snprintf(buf, sizeof buf, "%.159s", r->note);
            int room = SCREEN_W - 24 - (ROW_X + 150);
            while (*buf && text_width(&A.geneva9, buf, 0) > room) {
                char *sp = strrchr(buf, ' ');
                if (!sp) { buf[strlen(buf) - 1] = 0; continue; }
                *sp = 0;
            }
            draw_text(&A.geneva9, ROW_X + 150, y + 11, buf, grey, 0);
        }
    }

    if (page == PG_CONTROLS) {
        char buf[128];
        snprintf(buf, sizeof buf, "Controller detected: %s", pad_present() ? pad_name() : "none");
        draw_text(&A.geneva9, ROW_X, SCREEN_H - 40, buf, grey, 0);
    }
    const char *fin = "Click a tab to change pages, or anywhere else to go back";
    w = text_width(&A.geneva9, fin, 0);
    draw_text(&A.geneva9, (SCREEN_W - w) / 2, SCREEN_H - 20, fin, white, 0);
}

void do_options(void)
{
    Row row[OPT_MAX_ROWS];
    int page = PG_GRAPHICS, nrow = page_rows(page, row, OPT_MAX_ROWS);

    sfx(SND_SHOT_GECKO);
    cursor_set(CURSOR_HAND);
    set_monitor_clut(A.game_clut, false);
    draw_options(page, row, nrow);
    set_clut_to_color(RGB_BLACK, false);
    fade_to(A.game_clut, 15);
    flush_clicks();

    for (;;) {
        int mx, my;
        if (take_click(&mx, &my)) {
            if (my >= TAB_Y && my < TAB_Y + TAB_H) {       /* the page tabs */
                int x = 24, hit = -1;
                for (int i = 0; i < PG_COUNT; i++) {
                    int tw = text_width(&A.geneva9, PAGE_NAME[i], STYLE_BOLD) + 16;
                    if (mx >= x && mx < x + tw) { hit = i; break; }
                    x += tw + 6;
                }
                if (hit >= 0) {
                    page = hit;
                    nrow = page_rows(page, row, OPT_MAX_ROWS);
                    sfx(SND_SQUISH);
                    draw_options(page, row, nrow);
                    set_entries();
                    continue;
                }
                break;
            }
            int hit = -1;
            for (int i = 0; i < nrow; i++) {
                if (row[i].kind == ROW_TEXT || row[i].kind == ROW_NOTE) continue;
                int y = ROW_Y0 + i * ROW_DY;
                if (my >= y - 2 && my < y + 15 && mx >= ROW_X && mx < ROW_X + 420) { hit = i; break; }
            }
            if (hit < 0) break;
            row_toggle(&row[hit]);
            sfx(row_state(&row[hit]) ? SND_MYHOOHA : SND_GROUCHO_OUCH);
            draw_options(page, row, nrow);
            set_entries();
        }
        char k;
        while (take_key(&k)) { }
        if (key_down(SDL_SCANCODE_ESCAPE)) break;
        pump();
        present();
        SDL_Delay(10);
    }
    sfx(SND_TRANSITION);
    fade_to_color(15, RGB_BLACK);
    set_monitor_clut(A.title_clut, false);
    flush_clicks();
}
