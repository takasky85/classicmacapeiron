/*
 * game.c - the Apeiron game itself.
 *
 * Structure and numbers follow the decompiled PowerPC build:
 *   [NewGame]          sets up a game, runs [GameLoop], then [GameOver]
 *   [GameLoop]         one pass per tick: input, collisions, movement, draw
 *   [StartLevel]       builds the wave's centipede and schedules the wave's extras
 *   [GameHouseKeeping] spawn timers, shot limits, the player's death and respawn
 *   [ShotHitCheck]     what a shot does to each kind of target, and the score for it
 *   [FleaDescentCheck] flea / gecko / scorpion arrivals
 *   [HandleSideFeed]   extra centipede heads entering from the sides
 *
 * The original is a linked list of sprites across several layers; this uses one entity array, which
 * behaves the same but is easier to follow. Positions are 16.16 fixed point as in the original.
 */
#include "apeiron.h"

/* ---- tunables read from the decompile ------------------------------------------------------- */
#define PLAYER_ZONE_ROW  18        /* the player cannot go above this row */
#define RESPAWN_TICKS   180        /* [GameHouseKeeping]: 0xb4 ticks dead before respawning */
#define SHOT_SPEED      FIX(14)
/* [CompositeFrame] ends by spinning until its frame has lasted DAT_1001ddac ticks. The value is one
 * of the globals the decompile never resolves; two ticks (30 frames a second) is what the movement
 * speeds in [StartLevel] and [FleaDescentCheck] are scaled for. */
#define TICKS_PER_FRAME 2
#define MAX_ENT         600

typedef enum {
    E_FREE = 0, E_PEDE, E_SHOT, E_FLEA, E_GECKO, E_SCORP, E_YUMMIE, E_FODDER,
    E_COIN, E_PSYC, E_UPROOT, E_EXPL, E_POINTS, E_SCUM
} EType;

typedef struct { int16_t x, y; } Pt;

typedef struct Ent {
    EType type;
    bool   alive;
    fix_t  x, y, vx, vy;
    int    frame;              /* 1-based, as the original's frame index */
    int    anim;               /* animation counter */
    int    hits;               /* flea: 2 to kill; uproot: 3 */
    int    data;               /* points value, yummie kind, ... */
    int    timer;
    /* centipede */
    bool   head;
    int    dir;                /* +1 right, -1 left */
    int    vdir;               /* +1 descending, -1 ascending */
    Pt     q[12];
    int    qn;
    int    follower;           /* index of the segment behind this one, -1 for none */
    bool   counts;             /* made by [StartLevel], so the wave is not over until it dies */
    bool   poisoned;
} Ent;

static Ent ent[MAX_ENT];
static uint8_t shroom[GRID_W][GRID_H];     /* sprite frame index, 0 = no mushroom */
static uint8_t poisoned[GRID_W][GRID_H];   /* Gordon's trail; a centipede that hits one plummets */

/* ---- game state ----------------------------------------------------------------------------- */
static long score, next_extra;
static int  lives, wave, mult;
static int  ticks;
static int  pede_left;                     /* [GameLoop] ends the wave when this reaches 0 */
static int  shots_out, shot_limit, shot_delay, last_shot;
static int  player_speed;
static int  pede_speed;
static int  shroom_level;                  /* mushroom colour group: (wave-1) mod 11 */
static int  bg_index;                      /* [SetLevelBackground]: pixel pattern 128 + (wave-1)/11 */
static fix_t px, py;                       /* the player */
static int  player_anim;
static bool player_dead;
static int  death_tick;
static int  level_bonus;
static int  flea_chain;                    /* [ShotHitCheck]: doubles with each flea killed */
static int  flea_threshold;
static int  sidefeed_period, sidefeed_last;
static bool pede_hit_bottom;               /* "before he reaches the bottom... he'll call in his buddies" */
static int  psyc_at, mult_at, fodder_at, scorp_at, gecko_at;
static int  psyc_until;
static int  powerup[5];                    /* [UpdateWeapons]: ticks left on each of the five */
static bool quit_to_title;
static const char *asset_dir = "assets";

/* ---- helpers --------------------------------------------------------------------------------- */
static int rnd(int n) { return n > 0 ? (int)(SDL_rand(n)) : 0; }   /* [FastRandom] */

static Ent *ent_new(EType t)
{
    for (int i = 0; i < MAX_ENT; i++)
        if (!ent[i].alive) {
            memset(&ent[i], 0, sizeof ent[i]);
            ent[i].type = t; ent[i].alive = true; ent[i].frame = 1; ent[i].follower = -1;
            return &ent[i];
        }
    return NULL;
}

static int ent_index(const Ent *e) { return (int)(e - ent); }

static void ent_clear(EType t)
{
    for (int i = 0; i < MAX_ENT; i++) if (ent[i].alive && ent[i].type == t) ent[i].alive = false;
}

static bool in_grid(int c, int r) { return c >= 0 && c < GRID_W && r >= 0 && r < GRID_H; }

/* [PlantShroom] */
static void plant_shroom(int c, int r)
{
    if (!in_grid(c, r) || shroom[c][r]) return;
    shroom[c][r] = (uint8_t)((shroom_level + 1) * 4 + 1);
}

/* [PluckShroom] */
static void pluck_shroom(int c, int r) { if (in_grid(c, r)) { shroom[c][r] = 0; poisoned[c][r] = 0; } }

static int shrooms_in_player_zone(void)
{
    int n = 0;
    for (int c = 0; c < GRID_W; c++)
        for (int r = PLAYER_ZONE_ROW; r < GRID_H; r++)
            if (shroom[c][r]) n++;
    return n;
}

static void add_score(long n)
{
    score += n;
    if (score >= next_extra) { lives++; next_extra += 20000; sfx(SND_EXTRA_SHIP); }
}

/* [CreatePoints] / [CreateBonusPoints]: the little number that floats up from a kill */
static void create_points(fix_t x, fix_t y, int value, bool bonus)
{
    Ent *e = ent_new(E_POINTS);
    if (!e) return;
    e->x = x; e->y = y; e->vy = -FIX(1); e->data = value; e->timer = 40;
    e->hits = bonus ? 1 : 0;
}

/* [CreateExplosion] */
static void create_expl(fix_t x, fix_t y, int kind)
{
    Ent *e = ent_new(E_EXPL);
    if (!e) return;
    e->x = x; e->y = y; e->data = kind; e->frame = 1; e->timer = 0;
}

/* [CreateScumSplash]: the chips that fly off a mushroom when it is shot */
static void create_scum(fix_t x, fix_t y)
{
    for (int i = 0; i < 3; i++) {
        Ent *e = ent_new(E_SCUM);
        if (!e) return;
        e->x = x + FIX(6); e->y = y + FIX(6);
        e->vx = (fix_t)((rnd(9) - 4) * 16384);
        e->vy = (fix_t)(-(rnd(4) + 1) * 16384);
        e->timer = 24 + rnd(12);
        e->frame = 1 + rnd(A.scum.count);
    }
}

/* ---- the centipede --------------------------------------------------------------------------- */
static void pede_push(Ent *e, int x, int y)
{
    if (e->qn >= (int)(sizeof e->q / sizeof e->q[0])) return;
    e->q[e->qn].x = (int16_t)x; e->q[e->qn].y = (int16_t)y; e->qn++;
}

static void pede_pop(Ent *e)
{
    for (int i = 1; i < e->qn; i++) e->q[i - 1] = e->q[i];
    if (e->qn) e->qn--;
}

/* The head picks the next cell: straight on unless a mushroom or the wall is there, in which case it
 * drops a row and reverses. At the bottom of the player's zone it turns back upward. */
static void pede_choose(Ent *e)
{
    int cx = FIX_WHOLE(e->x), cy = FIX_WHOLE(e->y);
    int col = (cx + 8) / CELL, row = (cy + 8) / CELL;
    int nc = col + e->dir;
    bool blocked = nc < 0 || nc >= GRID_W || (in_grid(nc, row) && shroom[nc][row]);
    if (blocked) {
        int nr = row + e->vdir;
        if (nr >= GRID_H) { e->vdir = -1; nr = row - 1; }
        if (nr < PLAYER_ZONE_ROW && e->vdir < 0) { e->vdir = 1; nr = row + 1; }
        if (nr < 0) nr = 0;
        if (nr >= GRID_H) nr = GRID_H - 1;
        e->dir = -e->dir;
        pede_push(e, col * CELL, nr * CELL);
        /* a poisoned mushroom drops him straight to the bottom (bestiary, page 1) */
        if (in_grid(nc, row) && poisoned[nc][row]) {
            e->poisoned = true;
            for (int r = row + 1; r < GRID_H; r++) pede_push(e, col * CELL, r * CELL);
        }
    } else {
        pede_push(e, nc * CELL, row * CELL);
    }
}

static void pede_kill(Ent *e, bool by_shot);
static void start_level(void);

static void pede_update(Ent *e)
{
    if (e->qn == 0) {
        if (e->head) pede_choose(e);
        else {
            /* a body waits for its leader, but if that leader is gone it leads itself rather than
             * standing still for ever (which would leave the wave unfinishable) */
            if (++e->timer > 60) { e->head = true; e->timer = 0; }
            return;
        }
    }
    e->timer = 0;
    int tx = e->q[0].x, ty = e->q[0].y;
    fix_t fx = FIX(tx), fy = FIX(ty);
    fix_t step = FIX(pede_speed);
    bool arrived = true;
    if (e->x < fx) { e->x += step; if (e->x >= fx) e->x = fx; else arrived = false; }
    else if (e->x > fx) { e->x -= step; if (e->x <= fx) e->x = fx; else arrived = false; }
    if (e->y < fy) { e->y += step; if (e->y >= fy) e->y = fy; else arrived = false; }
    else if (e->y > fy) { e->y -= step; if (e->y <= fy) e->y = fy; else arrived = false; }
    if (arrived) {
        pede_pop(e);
        if (e->follower >= 0 && ent[e->follower].alive && ent[e->follower].type == E_PEDE)
            pede_push(&ent[e->follower], tx, ty);
    }
    e->anim++;
    int base = e->head ? (e->dir > 0 ? 1 : 17) : (e->dir > 0 ? 33 : 49);
    e->frame = base + (e->anim / 3) % 8;
}

/* A segment dies: it leaves a mushroom, and the segment behind it becomes a new head. */
static void pede_kill(Ent *e, bool by_shot)
{
    int col = (FIX_WHOLE(e->x) + 8) / CELL, row = (FIX_WHOLE(e->y) + 8) / CELL;
    if (col < 0) col = 0;
    if (col >= GRID_W) col = GRID_W - 1;
    if (row < 0) row = 0;
    if (row >= GRID_H) row = GRID_H - 1;

    if (by_shot) {
        long v = e->head ? 100L * mult : 10L * mult;
        add_score(v);
        create_points(e->x, e->y, (int)v, false);
        create_expl(e->x, e->y, 0);
        sfx(SND_SQUISH);
        if (!shroom[col][row]) plant_shroom(col, row);
    }
    if (e->follower >= 0 && ent[e->follower].alive && ent[e->follower].type == E_PEDE) {
        Ent *f = &ent[e->follower];
        f->head = true;
        f->qn = 0;                          /* it finds its own way from here */
    }
    e->alive = false;
    if (e->counts) pede_left--;
}

/* [HandleSideFeed]: a lone head walks in from one side, low down */
static void side_feed(void)
{
    sfx(SND_BOING1);
    Ent *e = ent_new(E_PEDE);
    if (!e) return;
    int left = rnd(2) == 1;
    e->head = true; e->dir = left ? 1 : -1; e->vdir = 1;
    e->x = FIX(left ? 0 : (GRID_W - 1) * CELL);
    e->y = FIX(0x160);                      /* y = 352 in the original */
    e->follower = -1;
    e->counts = false;
}

/* ---- the other creatures --------------------------------------------------------------------- */
static void create_flea(void)               /* [CreateFlea] */
{
    int n = 1;
    if (rnd(100) == 1 && wave >= 11) n = rnd(10) + 3;
    for (int i = 0; i < n; i++) {
        Ent *e = ent_new(E_FLEA);
        if (!e) return;
        e->x = FIX(rnd(GRID_W) * CELL);
        e->y = 0;
        e->vy = FIX(rnd(10) == 0 ? 8 : 5);
        e->hits = 0;
        if (wave > 5 && rnd(15) == 1) {     /* some fleas drift sideways */
            e->vx = (fix_t)((rnd(50) + 20) * 4096);
            if (rnd(2) == 0) e->vx = -e->vx;
        }
    }
    sfx(SND_BOING2);
}

static void create_gecko(void)              /* the gecko's entry in [FleaDescentCheck] */
{
    Ent *e = ent_new(E_GECKO);
    if (!e) return;
    bool from_left = rnd(4) != 0;
    e->x = from_left ? 0 : FIX(0x198);
    e->y = FIX((player_speed + rnd(28 - player_speed)) * CELL);
    if (e->y > FIX(FIELD_H - 32)) e->y = FIX(FIELD_H - 32);
    int sp = score < 5000 ? 3 : 4;
    e->vx = FIX(from_left ? sp : -sp);
    e->vy = FIX(sp);
    sfx(SND_SCORP_APPEARS);
}

static void create_scorpion(void)           /* the scorpion's entry in [FleaDescentCheck] */
{
    Ent *e = ent_new(E_SCORP);
    if (!e) return;
    e->x = FIX(0x186);
    e->y = FIX(rnd(0x11) * CELL);
    e->vx = -FIX(score > 20000 && rnd(4) != 1 ? 5 : 3);
    sfx(SND_GECKO_APPEARS);
}

static void create_yummie(fix_t x, fix_t y) /* [CreateYummie] */
{
    Ent *e = ent_new(E_YUMMIE);
    if (!e) return;
    e->x = x; e->y = y; e->vy = FIX(1);
    sfx(SND_YUMMIE_APPEARS);
}

/* [CreateFodder]: usually one spaceship, occasionally a flight of them, entering from either side
 * at a random height and flying straight across. */
static void create_fodder(void)
{
    int n = 1;
    if (rnd(0x32) == 1) n = rnd(8) + 2;
    for (int i = 0; i < n; i++) {
        Ent *e = ent_new(E_FODDER);
        if (!e) return;
        int row = rnd(0x17);
        int sp = rnd(4) + 2;
        if (rnd(2) == 1) { e->x = FIX(FIELD_W - 20); e->vx = -FIX(sp); }
        else             { e->x = 0;                 e->vx =  FIX(sp); }
        e->y = FIX(row * CELL);
        e->vy = 0;
    }
    sfx(SND_BONUS_APPEARS);
}

/* [CreateMultiplier]: a multiplier tile appears on an empty cell and stays put until it is shot. */
static void create_multiplier(void)
{
    Ent *e = ent_new(E_COIN);
    if (!e) return;
    int c, r, tries = 0;
    do { c = rnd(GRID_W); r = rnd(0x14); } while (shroom[c][r] && ++tries < 200);
    e->x = FIX(c * CELL); e->y = FIX(r * CELL);
    e->data = rnd(4) + 1;                   /* FastRandom(4) + 1: which x2..x5 tile */
    sfx(SND_MULT_APPEARS);
}

/* [CreatePsychadelic]: the flashing mushroom, planted on an empty cell. Shooting it is what the
 * hints mean by "if you see a flashing mushroom, shoot it". */
static void create_psychadelic(void)
{
    Ent *e = ent_new(E_PSYC);
    if (!e) return;
    int c, r, tries = 0;
    do { c = rnd(GRID_W); r = rnd(0x14); } while (shroom[c][r] && ++tries < 200);
    e->x = FIX(c * CELL); e->y = FIX(r * CELL);
    e->frame = 5;
    sfx(SND_GOT_FUNKY);
}

static void create_uproot(int c, int r)     /* [CreateUproot]: a mushroom that pulls itself up */
{
    Ent *e = ent_new(E_UPROOT);
    if (!e) return;
    e->x = FIX(c * CELL); e->y = FIX(r * CELL);
    e->hits = 0;
    e->vx = (fix_t)((rnd(0x1e) + 10) * 0x1000) * (rnd(2) ? 1 : -1);   /* (FastRandom(30)+10) << 12 */
    e->vy = -(fix_t)((rnd(0x14) + 10) * 0x8000);                      /* (FastRandom(20)+10) * -0x8000 */
    e->frame = (shroom_level + 1) * 4 + 1;
    sfx(SND_KILLED_ROOT);
}

/* ---- the player ------------------------------------------------------------------------------ */
static int  flash_until;                   /* [GameHouseKeeping]: SetEntries to the red table */

static void player_die(void)                /* [GameHouseKeeping]'s death path */
{
    if (player_dead) return;
    player_dead = true;
    death_tick = ticks;
    /* [GameHouseKeeping] swaps the whole colour table for the one [HueScale](1) builds - the game's
     * palette mapped onto pure red - and puts the game's own table back 20 ticks later. */
    set_monitor_clut(A.game_clut, false);
    Rgb red = { 0xFFFF, 0, 0 };
    hue_scale_to(red, true);
    flash_until = ticks + 0x14;
    music_halt();
    sfx(SND_SHIP_EXPLOSION);
    sfx(SND_OH_NO);
    create_expl(px, py, 1);
    for (int i = 0; i < 12; i++) {          /* [CreateFlak] */
        Ent *e = ent_new(E_SCUM);
        if (!e) break;
        e->x = px + FIX(8); e->y = py + FIX(8);
        e->vx = (fix_t)((rnd(17) - 8) * 16384);
        e->vy = (fix_t)((rnd(17) - 8) * 16384);
        e->timer = 40 + rnd(20);
        e->data = 1;                        /* draw with the flak sprite */
        e->frame = 1 + rnd(A.flak.count);
    }
}

static void fire_shot(void)                 /* [ShroomShotCheck] */
{
    if (shots_out >= shot_limit) return;
    if (ticks - last_shot < shot_delay) return;
    Ent *e = ent_new(E_SHOT);
    if (!e) return;
    shots_out++;
    last_shot = ticks;
    e->x = px + FIX(8) - FIX(2);
    e->y = py - FIX(5);
    e->vy = -SHOT_SPEED;
    sfx(SND_SHIP_LASER);
}

static void clear_spawn_area(void)      /* never leave the player wedged inside a mushroom */
{
    int c0 = FIX_WHOLE(px) / CELL, r0 = FIX_WHOLE(py) / CELL;
    for (int c = c0 - 1; c <= c0 + 1; c++)
        for (int r = r0 - 1; r <= r0 + 1; r++)
            if (in_grid(c, r)) pluck_shroom(c, r);
}

/* The player cannot move into a mushroom - that is what the DJARUM cheat turns off. */
static bool player_blocked(fix_t nx, fix_t ny)
{
    int x0 = FIX_WHOLE(nx) + 3, y0 = FIX_WHOLE(ny) + 3;      /* a little smaller than the sprite */
    int x1 = x0 + 9, y1 = y0 + 9;
    for (int c = x0 / CELL; c <= x1 / CELL; c++)
        for (int r = y0 / CELL; r <= y1 / CELL; r++)
            if (in_grid(c, r) && shroom[c][r]) return true;
    return false;
}

static bool hit_box(const Ent *e, int w, int h, fix_t ox, fix_t oy, int ow, int oh)
{
    int ax = FIX_WHOLE(e->x), ay = FIX_WHOLE(e->y);
    int bx = FIX_WHOLE(ox), by = FIX_WHOLE(oy);
    return ax < bx + ow && ax + w > bx && ay < by + oh && ay + h > by;
}

/* ---- level setup ------------------------------------------------------------------------------ */
static void create_shroom_field(void)       /* [CreateShroomField]: 30 mushrooms at random */
{
    memset(shroom, 0, sizeof shroom);
    memset(poisoned, 0, sizeof poisoned);
    for (int i = 0; i < 30; i++) {
        int c, r;
        do { c = rnd(GRID_W); r = rnd(GRID_H - 1); } while (shroom[c][r]);
        plant_shroom(c, r);
    }
}

static void start_level(void)
{
    ticks = 0;
    pede_left = 0;
    shots_out = 0;
    last_shot = 0;
    flea_chain = 200;
    sidefeed_last = 0;
    pede_hit_bottom = false;
    ent_clear(E_PEDE); ent_clear(E_SHOT); ent_clear(E_FLEA); ent_clear(E_GECKO);
    ent_clear(E_SCORP); ent_clear(E_YUMMIE); ent_clear(E_FODDER); ent_clear(E_COIN);
    ent_clear(E_PSYC); ent_clear(E_UPROOT);

    /* [StartLevel]: the wave's mushroom colour, and its centipede speed */
    int w1 = wave - 1;
    shroom_level = w1 % 11;                 /* [StartLevel]'s DAT_1001da8a */
    bg_index     = (w1 / 11) % 11;          /* [StartLevel]'s DAT_1001da8c */
    int extra_heads = shroom_level;         /* the same count, as in the original */
    pede_speed = (wave % 2 == 0) ? 3 : 5;
    if (score > 40000) pede_speed = 5;
    for (int c = 0; c < GRID_W; c++)
        for (int r = 0; r < GRID_H; r++)
            if (shroom[c][r]) shroom[c][r] = (uint8_t)((shroom_level + 1) * 4 + 1);

    /* the side-feed period starts at 240 ticks and shortens through the wave */
    sidefeed_period = 0xF0 - 3;
    if (sidefeed_period < 0x1F) sidefeed_period = 0xF0;

    /* one chain of (11 - extra_heads) segments entering at the top, then `extra_heads` loose heads */
    int startx = (rnd(3) + 14) * CELL;
    int prev = -1;
    for (int i = 0; i < 11 - extra_heads; i++) {
        Ent *e = ent_new(E_PEDE);
        if (!e) break;
        e->head = (i == 0);
        e->dir = -1; e->vdir = 1;
        e->x = FIX(startx); e->y = 0;
        e->anim = i * 2;
        e->counts = true;
        if (prev >= 0) ent[prev].follower = ent_index(e);
        prev = ent_index(e);
        pede_left++;
    }
    for (int i = 0; i < extra_heads; i++) {
        Ent *e = ent_new(E_PEDE);
        if (!e) break;
        int c;
        do { c = rnd(GRID_W); } while (false);
        e->head = true;
        e->dir = rnd(2) ? 1 : -1; e->vdir = 1;
        e->x = FIX(c * CELL); e->y = 0;
        e->counts = true;
        pede_left++;
    }

    /* [StartLevel] schedules the wave's extras with the same dice the original rolls */
    psyc_at = mult_at = fodder_at = scorp_at = gecko_at = 0;
    if (rnd(10) == 1) psyc_at = rnd(30) * 60 + 120;
    if (rnd(3) == 1) mult_at = rnd(30) * 60 + 120;
    if (rnd(3) == 1) fodder_at = rnd(30) * 60 + 120;
    if (rnd(4) == 1 && extra_heads >= 3) scorp_at = rnd(20) * 60 + 120;
    gecko_at = rnd(360) + 360;
}

/* ---- drawing ---------------------------------------------------------------------------------- */
#define CLIP FIELD_X, FIELD_Y, FIELD_W, FIELD_H
static void spr(const Sprite *s, int frame, fix_t x, fix_t y)
{
    draw_sprite_clipped(s, frame, FIELD_X + FIX_WHOLE(x), FIELD_Y + FIX_WHOLE(y), CLIP);
}

/* [DrawNum] / [DrawNumDisplay]: the score digits from PICT 1100, right-aligned at `right`.
 * `scale` 1 draws them at half size, which is how the small boxes on the panel are filled. */
static void draw_digits(long v, int right, int top, int pad, int small)
{
    char buf[24];
    snprintf(buf, sizeof buf, "%ld", v < 0 ? 0 : v);
    int n = (int)strlen(buf);
    int dw = small ? A.numbers.w / 2 : A.numbers.w;
    int dh = small ? A.numbers.h / 2 : A.numbers.h;
    int x = right - n * dw;
    for (int i = 0; i < n; i++) {
        int d = buf[i] - '0';
        if (small) {                        /* nearest-neighbour halving, so it stays crisp */
            const uint8_t *src = A.numbers.img.px + (size_t)d * A.numbers.h * A.numbers.w;
            for (int yy = 0; yy < dh; yy++)
                for (int xx = 0; xx < dw; xx++) {
                    uint8_t c = src[(yy * 2) * A.numbers.w + xx * 2];
                    int sx = x + i * dw + xx, sy = top + yy;
                    if (c != A.numbers.trans && sx >= 0 && sx < SCREEN_W && sy >= 0 && sy < SCREEN_H)
                        fb[sy * SCREEN_W + sx] = c;
                }
        } else {
            draw_sprite(&A.numbers, d + 1, x + i * dw, top);
        }
    }
    (void)pad;
}

static void draw_frame(void)
{
    uint8_t white = find_color(0xFFFF, 0xFFFF, 0xFFFF);
    uint8_t yellow = find_color(0xFC00, 0xF37D, 0x0526);

    /* [SetLevelBackground]: the wave's pixel pattern tiled over the field */
    tile_pattern(&A.pat_level[bg_index % 11], FIELD_X, FIELD_Y, FIELD_W, FIELD_H);

    for (int c = 0; c < GRID_W; c++)
        for (int r = 0; r < GRID_H; r++)
            if (shroom[c][r])
                draw_sprite_clipped(&A.shroom, shroom[c][r], FIELD_X + c * CELL, FIELD_Y + r * CELL, CLIP);

    for (int i = 0; i < MAX_ENT; i++) {
        Ent *e = &ent[i];
        if (!e->alive) continue;
        switch (e->type) {
        case E_PEDE:    spr(&A.pede, e->frame, e->x, e->y); break;
        case E_SHOT:    spr(&A.shot, 1, e->x, e->y); break;
        case E_FLEA:    spr(&A.flea, 1 + (e->anim / 4) % A.flea.count, e->x, e->y); break;
        /* E_GECKO is Larry (enters from a side at a random height, bounces vertically, eats
         * mushrooms) and E_SCORP is Gordon the Gecko (crosses horizontally, poisons mushrooms) -
         * the names follow the decompile's sprite tags, the artwork follows the bestiary page. */
        case E_GECKO:   spr(&A.scorpion, 1 + (e->anim / 3) % A.scorpion.count, e->x, e->y); break;
        case E_SCORP:   spr(&A.gecko, 1 + (e->anim / 3) % A.gecko.count, e->x, e->y); break;
        case E_YUMMIE:  spr(&A.coin, 1 + (e->anim / 2) % A.coin.count, e->x, e->y); break;   /* the coin */
        case E_FODDER:  spr(&A.fodder, 1 + (e->anim / 2) % A.fodder.count, e->x, e->y); break;
        case E_COIN:    spr(&A.yummie, e->data, e->x, e->y); break;                          /* the multiplier */
        case E_PSYC:    spr(&A.shroom, 1 + ((e->anim / 3) % 11) * 4 + 1, e->x, e->y); break;  /* the flashing shroom */
        case E_UPROOT:  spr(&A.shroom, e->frame, e->x, e->y); break;
        case E_SCUM:    spr(e->data ? &A.flak : &A.scum, e->frame, e->x, e->y); break;
        case E_EXPL:
            spr(e->data == 1 ? &A.explode_shooter : (e->data == 2 ? &A.scorpsplat : &A.splat),
                e->frame, e->x, e->y);
            break;
        case E_POINTS: {
            /* the little rising number, drawn with the points digit strip */
            const Sprite *ds = e->hits ? &A.bonuspoints : &A.points;
            char buf[16];
            snprintf(buf, sizeof buf, "%d", e->data);
            int n = (int)strlen(buf);
            for (int k = 0; k < n; k++) {
                int d = buf[k] - '0';
                draw_sprite_clipped(ds, d + 1, FIELD_X + FIX_WHOLE(e->x) + k * (ds->w + 1),
                                    FIELD_Y + FIX_WHOLE(e->y), CLIP);
            }
            break;
        }
        default: break;
        }
    }

    if (!player_dead)
        draw_sprite_clipped(&A.shooter, 1 + (player_anim / 4) % 8,
                            FIELD_X + FIX_WHOLE(px), FIELD_Y + FIX_WHOLE(py), CLIP);

    /* [GameWindowUpdate]: pattern 401 behind, the ScoreMatt panel (PICT 401) beside the field, and
     * the seven rings framing the playfield. The boxes on the panel hold the numbers. */
    tile_pattern(&A.pat_game, PANEL_X, 0, SCREEN_W - PANEL_X, SCREEN_H);
    draw_image(&A.scorematt, PANEL_X, 0);

    static const int ring_rgb[7][3] = {
        {30000,30000,65535},{30000,30000,65535},{40000,40000,65535},{40000,40000,65535},
        {50000,50000,65535},{40000,40000,65535},{30000,30000,65535} };
    uint8_t rings[7];
    for (int k = 0; k < 7; k++) rings[k] = find_color(ring_rgb[k][0], ring_rgb[k][1], ring_rgb[k][2]);
    set_border_rings(rings, true);

    /* the panel's own display boxes, measured from PICT 401, filled with the game's digit sprite */
    draw_digits(score, PANEL_X + 186, 134, 0, 0);
    draw_digits(level_bonus, PANEL_X + 186, 235, 0, 0);
    for (int i = 0; i < lives && i < 5; i++)
        draw_sprite(&A.shooter, 1, PANEL_X + 25 + i * 32, 340);
    for (int i = 0; i < 6; i++) {
        int wx = PANEL_X + 59 + i * 15, wy = 381;
        if (i == 0) {
            if (mult > 1) draw_sprite(&A.yummie, mult - 1, wx, wy);
            else draw_sprite(&A.wpn_none, 1, wx - 1, wy - 1);
        } else if (powerup[i - 1] > 0) {
            draw_sprite(&A.wpn[i - 1], 1, wx, wy);
        } else {
            draw_sprite(&A.wpn_none, 1, wx - 1, wy - 1);
        }
    }
    draw_digits(wave, PANEL_X + 118, 434, 0, 1);
    (void)white; (void)yellow;
}

/* [ShotHitCheck]: what a shot does to each kind of target, and what it scores. */
static void shot_hits(Ent *t)
{
        switch (t->type) {
        case E_PEDE:
            pede_kill(t, true);
            break;
        case E_FLEA:
            t->hits++;
            if (t->hits < 2) { sfx(SND_HIT); t->vy += FIX(3); }
            else {
                sfx(SND_SQUISH);
                add_score((long)flea_chain * mult);
                create_points(t->x, t->y, flea_chain * mult, false);
                create_expl(t->x, t->y, 0);
                flea_chain <<= 1;
                t->alive = false;
            }
            break;
        case E_GECKO: {                 /* Larry: worth more the closer he is */
            sfx(SND_SCORP_DIES);
            int dx = FIX_WHOLE(px) - FIX_WHOLE(t->x);
            if (dx < 0) dx = -dx;
            int v = (1500 - dx * 10) / 100 * 100;
            if (v < 300) v = 300;
            add_score((long)v * mult);
            create_points(t->x, t->y, v * mult, false);
            create_expl(t->x, t->y, 2);
            gecko_at = ticks + rnd(360) + 360;
            t->alive = false;
            break;
        }
        case E_SCORP:                   /* Gordon the Gecko: a flat 1000 */
            sfx(SND_SHOT_GECKO);
            add_score(1000L * mult);
            create_points(t->x, t->y, 1000 * mult, false);
            create_expl(t->x, t->y, 3);
            t->alive = false;
            break;
        case E_YUMMIE:                          /* the coin: knocked about, not killed */
            sfx(SND_BOING1);
            t->vy = -FIX(10);
            t->vx = (fix_t)((rnd(40) + 10) * 4096) * (rnd(2) ? 1 : -1);
            break;
        case E_FODDER: {
            int v = (rnd(6) + 1) * 500;
            create_points(t->x, t->y, v, true);
            create_expl(t->x, t->y, 0);
            sfx(SND_GOT_YUMMIE);
            level_bonus += v;
            t->alive = false;
            break;
        }
        case E_COIN:
            sfx(SND_MULT_APPEARS);
            mult = t->data;
            t->alive = false;
            break;
        case E_PSYC:
            sfx(SND_GOT_FUNKY);
            mult = 10;
            psyc_until = ticks + 900;
            music_halt();
            t->alive = false;
            break;
        case E_UPROOT:
            t->hits++;
            if (t->hits < 3) {
                sfx(SND_HIT);
                t->vy = -FIX(5);
                t->vx = (fix_t)((rnd(30) + 10) * 4096) * (rnd(2) ? 1 : -1);
            } else {
                sfx(SND_KILLED_ROOT);
                long v = (t->frame < 5 ? 9000L : 3000L) * mult;
                add_score(v);
                create_points(t->x, t->y, (int)v, false);
                t->alive = false;
            }
            break;
        default: break;
        }
}

/* ---- per-tick update --------------------------------------------------------------------------- */
static void update_entities(void)
{
    for (int i = 0; i < MAX_ENT; i++) {
        Ent *e = &ent[i];
        if (!e->alive) continue;
        e->anim++;
        switch (e->type) {
        case E_PEDE:
            pede_update(e);
            if (FIX_WHOLE(e->x) < -CELL * 2 || FIX_WHOLE(e->x) > FIELD_W + CELL ||
                FIX_WHOLE(e->y) > FIELD_H + CELL) {
                if (e->follower >= 0 && ent[e->follower].alive) ent[e->follower].head = true;
                e->alive = false;
                if (e->counts) pede_left--;
            }
            break;

        case E_SHOT: {
            /* A shot covers more than a cell each tick, so it is stepped a pixel at a time: sampling
             * only where it ends up let it pass straight through a mushroom directly above. */
            int steps = -FIX_WHOLE(e->vy);
            if (steps < 1) steps = 1;
            for (int k = 0; k < steps; k++) {
                e->y -= FIX(1);
                e->x += e->vx / steps;
                if (FIX_WHOLE(e->y) < -12) { e->alive = false; shots_out--; break; }
                int col = (FIX_WHOLE(e->x) + 2) / CELL, row = FIX_WHOLE(e->y) / CELL;
                if (in_grid(col, row) && shroom[col][row]) {
                    /* [ShotHitCheck]'s mushroom branch */
                    create_scum(FIX(col * CELL), FIX(row * CELL));
                    sfx(SND_SHROOM_POP);
                    int f = shroom[col][row];
                    if (f % 4 == 0) {                           /* last damage state: destroyed */
                        if (rnd(15) == 0) create_yummie(FIX(col * CELL), FIX(row * CELL));
                        long v = mult * (f < 5 ? 5 : 1);
                        create_points(FIX(col * CELL), FIX(row * CELL), (int)v, false);
                        pluck_shroom(col, row);
                        add_score(v);
                    } else if (f % 4 == 1 && wave > 11 && rnd(50 - wave < 10 ? 10 : 50 - wave) == 1) {
                        create_uproot(col, row);
                        pluck_shroom(col, row);
                    } else {
                        shroom[col][row] = (uint8_t)(f + 1);
                    }
                    e->alive = false; shots_out--;
                    break;
                }
                bool done = false;
                for (int j = 0; j < MAX_ENT && !done; j++) {
                    Ent *t = &ent[j];
                    if (!t->alive || t == e) continue;
                    bool hit = false;
                    switch (t->type) {
                    case E_PEDE:   hit = hit_box(e, 4, 12, t->x, t->y, 16, 16); break;
                    case E_FLEA:   hit = hit_box(e, 4, 12, t->x, t->y, 16, 16); break;
                    case E_GECKO:  hit = hit_box(e, 4, 12, t->x, t->y, 24, 24); break;
                    case E_SCORP:  hit = hit_box(e, 4, 12, t->x, t->y, 42, 20); break;
                    case E_YUMMIE: hit = hit_box(e, 4, 12, t->x, t->y, 16, 16); break;
                    case E_FODDER: hit = hit_box(e, 4, 12, t->x, t->y, 20, 20); break;
                    case E_COIN:   hit = hit_box(e, 4, 12, t->x, t->y, 14, 14); break;
                    case E_PSYC:   hit = hit_box(e, 4, 12, t->x, t->y, 16, 16); break;
                    case E_UPROOT: hit = hit_box(e, 4, 12, t->x, t->y, 16, 16); break;
                    default: break;
                    }
                    if (!hit) continue;
                    shot_hits(t);
                    e->alive = false; shots_out--; done = true;
                }
                if (done || !e->alive) break;
            }
            break;
        }

        case E_FLEA:
            e->x += e->vx; e->y += e->vy;
            {   /* a flea leaves a trail: at most one mushroom per cell it passes through */
                int col = (FIX_WHOLE(e->x) + 8) / CELL, row = (FIX_WHOLE(e->y) + 8) / CELL;
                if (row != e->data - 1) {
                    e->data = row + 1;
                    if (in_grid(col, row) && !shroom[col][row] && row > 2 && rnd(3) == 0)
                        plant_shroom(col, row);
                }
            }
            if (FIX_WHOLE(e->y) > FIELD_H) e->alive = false;
            break;

        case E_GECKO:
            e->x += e->vx; e->y += e->vy;
            if (FIX_WHOLE(e->y) > FIELD_H - 24 || FIX_WHOLE(e->y) < PLAYER_ZONE_ROW * CELL - 40) e->vy = -e->vy;
            if (FIX_WHOLE(e->x) < -48 || FIX_WHOLE(e->x) > FIELD_W + 8) e->alive = false;
            {   /* geckos eat the mushrooms they pass over */
                int col = (FIX_WHOLE(e->x) + 20) / CELL, row = (FIX_WHOLE(e->y) + 10) / CELL;
                if (in_grid(col, row) && shroom[col][row] && rnd(3) == 0) pluck_shroom(col, row);
            }
            break;

        case E_SCORP:
            e->x += e->vx;
            if (FIX_WHOLE(e->x) < -32) e->alive = false;
            {   /* Gordon poisons the mushrooms he passes through */
                int col = (FIX_WHOLE(e->x) + 12) / CELL, row = (FIX_WHOLE(e->y) + 12) / CELL;
                if (in_grid(col, row) && shroom[col][row]) poisoned[col][row] = 1;
            }
            break;

        case E_YUMMIE:
            e->x += e->vx; e->y += e->vy;
            e->vy += FIX(1) / 4;
            if (FIX_WHOLE(e->x) < 0 || FIX_WHOLE(e->x) > FIELD_W - 14) e->vx = -e->vx;
            if (FIX_WHOLE(e->y) > FIELD_H - 14) { e->vy = -FIX(6); e->y = FIX(FIELD_H - 14); }
            if (++e->timer > 480) { e->alive = false; sfx(SND_MISSED_IT); }
            break;

        case E_FODDER:
            e->x += e->vx;
            {   /* "she tends to annihilate any shrooms she happens to pass over" */
                int col = (FIX_WHOLE(e->x) + 10) / CELL, row = (FIX_WHOLE(e->y) + 10) / CELL;
                if (in_grid(col, row) && shroom[col][row]) pluck_shroom(col, row);
            }
            if (FIX_WHOLE(e->x) < -24 || FIX_WHOLE(e->x) > FIELD_W + 4) e->alive = false;
            break;

        case E_COIN:
        case E_PSYC:
            break;                          /* both stay where they were planted */

        case E_UPROOT:                      /* thrown up, falls back, bounces off the floor */
            e->x += e->vx; e->y += e->vy;
            e->vy += FIX(1) / 4;
            if (FIX_WHOLE(e->x) < 0 || FIX_WHOLE(e->x) > FIELD_W - 16) e->vx = -e->vx;
            if (FIX_WHOLE(e->y) > FIELD_H - 16) {
                e->y = FIX(FIELD_H - 16);
                e->vy = -(fix_t)((rnd(0x14) + 10) * 0x8000) / 2;
            }
            break;

        case E_SCUM:
            e->x += e->vx; e->y += e->vy;
            e->vy += FIX(1) / 3;
            if (--e->timer <= 0) e->alive = false;
            break;

        case E_EXPL: {
            const Sprite *s = e->data == 1 ? &A.explode_shooter : (e->data == 2 ? &A.scorpsplat : &A.splat);
            if (e->anim % 2 == 0) e->frame++;
            if (e->frame > s->count) e->alive = false;
            break;
        }

        case E_POINTS:
            e->y += e->vy;
            if (--e->timer <= 0) e->alive = false;
            break;

        default: break;
        }
    }
}

/* [GameHouseKeeping]: shot limits, player speed, timed arrivals, the player's death and respawn */
static void house_keeping(void)
{
    if (score < 80000) player_speed = 0x12;
    else if (score < 100000) player_speed = 0x13;
    else if (score < 120000) player_speed = 0x14;
    else if (score < 140000) player_speed = 0x15;
    else if (score < 160000) player_speed = 0x16;
    else if (score < 180000) player_speed = 0x17;
    else player_speed = 0x12;

    if (score < 20000) flea_threshold = 5;
    else if (score < 120000) flea_threshold = 9;
    else if (score < 140000) flea_threshold = 15;
    else flea_threshold = 16 + (int)((score - 140000) / 30000);

    /* "you can only have one shot on the screen at a time" - the instruction pages and the
     * shipped Hints file both say so; the 5/9/15 band is the flea threshold, not the shot limit. */
    shot_limit = 1;
    shot_delay = 2;

    if (flash_until && ticks > flash_until) { flash_until = 0; set_monitor_clut(A.game_clut, true); }
    if (psyc_until && ticks > psyc_until) { psyc_until = 0; mult = 1; if (A.pref_music) music_start(); }
    for (int i = 0; i < 5; i++) if (powerup[i] > 0) powerup[i]--;
    shot_limit = powerup[2] ? 4 : 1;        /* the machine gun lifts the one-shot rule */

    if (psyc_at && ticks > psyc_at) { psyc_at = 0; create_psychadelic(); }
    if (mult_at && ticks > mult_at) { mult_at = 0; create_multiplier(); }
    if (fodder_at && ticks > fodder_at) { fodder_at = 0; create_fodder(); }
    if (scorp_at && ticks > scorp_at) { scorp_at = 0; create_scorpion(); }
    if (gecko_at && ticks > gecko_at) { gecko_at = ticks + rnd(600) + 300; create_gecko(); }

    /* [FleaDescentCheck]: a flea when the player's zone is running out of mushrooms */
    if (!player_dead && shrooms_in_player_zone() < flea_threshold && ticks > 60) {
        bool flea_out = false;
        for (int i = 0; i < MAX_ENT; i++) if (ent[i].alive && ent[i].type == E_FLEA) { flea_out = true; break; }
        if (!flea_out) { flea_chain = 200; create_flea(); }
    }

    /* [HandleSideFeed]. The original gates this on a flag; without it the loose heads pile up
     * faster than they can be shot and the wave can never end, so at most a few are out at once. */
    int loose = 0;
    for (int i = 0; i < MAX_ENT; i++)
        if (ent[i].alive && ent[i].type == E_PEDE && !ent[i].counts) loose++;
    for (int i = 0; i < MAX_ENT && !pede_hit_bottom; i++)
        if (ent[i].alive && ent[i].type == E_PEDE && FIX_WHOLE(ent[i].y) >= (GRID_H - 1) * CELL)
            pede_hit_bottom = true;
    if (pede_hit_bottom && loose < 3 && pede_left > 0 && ticks - sidefeed_last > sidefeed_period) {
        sidefeed_last = ticks;
        sidefeed_period -= 4;
        if (sidefeed_period < 0x1F) sidefeed_period = 0x1E;
        side_feed();
    }

    if (player_dead) {
        if (ticks - death_tick > RESPAWN_TICKS) {
            player_dead = false;
            lives--;
            mult = 1; psyc_until = 0;
            if (!powerup[4]) memset(powerup, 0, sizeof powerup);   /* unless the Yummielock is up */
            px = FIX(FIELD_W / 2 - 8);
            py = FIX(FIELD_H - 32);
            clear_spawn_area();
            if (lives >= 0) {
                start_level();                  /* [GameHouseKeeping] rebuilds the wave */
                if (A.pref_music) music_start();
            }
        }
        return;
    }

    /* the player hit by anything that kills */
    for (int i = 0; i < MAX_ENT; i++) {
        Ent *e = &ent[i];
        if (!e->alive) continue;
        int w = 16, h = 16;
        switch (e->type) {
        case E_GECKO: w = 42; h = 20; break;
        case E_SCORP: w = 24; h = 24; break;
        case E_YUMMIE: w = 14; h = 14; break;
        case E_FODDER: w = 20; h = 20; break;
        default: break;
        }
        bool touching = hit_box(e, w, h, px, py, 16, 16);
        if (!touching) continue;
        if (e->type == E_YUMMIE) {                       /* eaten: a random bonus */
            sfx(SND_GOT_YUMMIE);
            e->alive = false;
            powerup[rnd(5)] = 1800;         /* one of the five, for about 30 seconds */
            continue;
        }
        if (e->type == E_PSYC || e->type == E_COIN || e->type == E_FODDER) continue;
        if (e->type == E_POINTS || e->type == E_SCUM || e->type == E_EXPL || e->type == E_SHOT) continue;
        player_die();
        break;
    }
}

/* [CountOffShrooms] / [TallyBonus]: the end-of-wave count-up */
static void tally_bonus(void)
{
    sfx(SND_LEVEL_CLEAR);
    for (int c = 0; c < GRID_W && !quit_to_title; c++) {
        for (int r = 0; r < GRID_H; r++) {
            if (!shroom[c][r]) continue;
            if (shroom[c][r] % 4 != 1) {                 /* partly eaten ones are restored */
                shroom[c][r] = (uint8_t)((shroom_level + 1) * 4 + 1);
            }
        }
        add_score(level_bonus / 40);
        draw_frame();
        set_entries();
        pump();
        SDL_Delay(12);
    }
    level_bonus = 2000;
}

/* ---- the main game loop ------------------------------------------------------------------------ */
/* [GameLoop]: returns true when the player got far enough for the special ending. */
static bool game_loop(void)
{
    Uint64 next = SDL_GetTicks();
    for (;;) {
        pump();
        if (key_down(SDL_SCANCODE_ESCAPE)) { quit_to_title = true; return false; }

        Uint64 now = SDL_GetTicks();
        if (now < next) { SDL_Delay(1); continue; }
        next += (Uint64)(TICKS_PER_FRAME * MAC_TICK_MS + 0.5);
        if (now > next + 200) next = now;    /* do not try to catch up after a long stall */

        /* [GameLoop] bumps the counter once per tick change and [GameHouseKeeping] once per frame,
         * so it advances by two a frame - which keeps the tick-based timers in real 60ths. */
        ticks += TICKS_PER_FRAME;
        player_anim++;
        music_pump();

        /* [HandleInput]: the keyboard, plus the mouse when it is inside the field */
        if (!player_dead) {
            int dx = 0, dy = 0;
            if (key_down(SDL_SCANCODE_LEFT) || key_down(SDL_SCANCODE_A)) dx -= 1;
            if (key_down(SDL_SCANCODE_RIGHT) || key_down(SDL_SCANCODE_D)) dx += 1;
            if (key_down(SDL_SCANCODE_UP) || key_down(SDL_SCANCODE_W)) dy -= 1;
            if (key_down(SDL_SCANCODE_DOWN) || key_down(SDL_SCANCODE_S)) dy += 1;
            int sp = player_speed / 2;
            if (dx) { fix_t nx = px + FIX(sp) * dx; if (!player_blocked(nx, py)) px = nx; }
            if (dy) { fix_t ny = py + FIX(sp) * dy; if (!player_blocked(px, ny)) py = ny; }

            int mx, my;
            mouse_pos(&mx, &my);
            if (mx >= FIELD_X && mx < FIELD_X + FIELD_W && my >= 0 && my < SCREEN_H && dx == 0 && dy == 0) {
                fix_t tx = FIX(mx - FIELD_X - 8), ty = FIX(my - 8);
                fix_t lim = FIX(player_speed);
                if (tx - px > lim) tx = px + lim;
                if (px - tx > lim) tx = px - lim;
                if (ty - py > lim) ty = py + lim;
                if (py - ty > lim) ty = py - lim;
                if (!player_blocked(tx, py)) px = tx;
                if (!player_blocked(px, ty)) py = ty;
            }
            if (px < 0) px = 0;
            if (px > FIX(FIELD_W - 16)) px = FIX(FIELD_W - 16);
            if (py < FIX(PLAYER_ZONE_ROW * CELL)) py = FIX(PLAYER_ZONE_ROW * CELL);
            if (py > FIX(FIELD_H - 16)) py = FIX(FIELD_H - 16);

            if (key_down(SDL_SCANCODE_SPACE) || key_down(SDL_SCANCODE_LCTRL) ||
                key_down(SDL_SCANCODE_RCTRL) || (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK))
                fire_shot();
        }

        house_keeping();
        update_entities();

        if (lives < 0 && !player_dead) return wave > 15;

        if (pede_left <= 0) {                /* the wave is cleared */
            ent_clear(E_PEDE);
            tally_bonus();
            wave++;
            start_level();
            if (A.pref_music) music_start();
        }

        draw_frame();
        set_entries();
    }
}

/* [GameOver], in the original's order:
 *   HaltSounds -> FunkyFade -> colour table 888 -> load and play "Hooka Music" (snd 150)
 *   -> draw PICT 888 (or PICT 667 past wave 15) -> fade it up over 60 steps
 *   -> wait 60 ticks, then the eight frames of PICT 900-907 at (225, 99), 8 ticks apart
 *   -> wait for the music to end -> find the high-score slot
 *   -> [EnterHighScore]'s dialog, then snd 144 while the table is rewritten
 *   -> FunkyFade back to the title screen
 * A click at any point during the animation cuts the sounds short, as [GameOver] does.
 */
static bool go_button(bool *cut)
{
    if (button()) { *cut = true; sfx_halt_all(); }
    return *cut;
}

static void go_wait(int ticks_to_wait, bool *cut)
{
    Uint64 t0 = SDL_GetTicks();
    while (SDL_GetTicks() - t0 < (Uint64)(ticks_to_wait * MAC_TICK_MS)) {
        go_button(cut);
        present();
        SDL_Delay(2);
    }
}

/* [EnterHighScore] puts up a modal dialog; this stands in for it with a plain centred box. */
static void enter_high_score(int slot, const Image *bg)
{
    /* the colour table in force is the game-over one, so the dialog is drawn in whatever white and
     * black it actually has rather than a grey that may not be in it at all */
    uint8_t white = find_color(0xFFFF, 0xFFFF, 0xFFFF);
    uint8_t black = find_color(0, 0, 0);

    for (int i = 9; i > slot; i--) A.score_row[i] = A.score_row[i - 1];
    A.score_row[slot].score = score;
    A.score_row[slot].wave = wave;

    char name[32];
    snprintf(name, sizeof name, "%.24s", A.score_row[slot].name);   /* the dialog pre-fills the last name */
    int n = (int)strlen(name);

    SDL_StartTextInput(SDL_GetKeyboardFocus());
    bool done = false;
    while (!done && !quit_to_title) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) plat_shutdown(0);
            if (e.type == SDL_EVENT_TEXT_INPUT && n < 24) {
                for (const char *p = e.text.text; *p && n < 24; p++)
                    if ((unsigned char)*p >= 32) name[n++] = *p;
                name[n] = 0;
            }
            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_BACKSPACE && n > 0) name[--n] = 0;
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) done = true;
                if (e.key.key == SDLK_ESCAPE) { done = true; quit_to_title = true; }
            }
        }
        draw_image(bg, (SCREEN_W - bg->w) / 2, (SCREEN_H - bg->h) / 2);
        int bw = 420, bh = 108, bx = (SCREEN_W - bw) / 2, by = (SCREEN_H - bh) / 2;
        fill_rect(bx, by, bw, bh, black);
        fill_rect(bx + 1, by + 1, bw - 2, bh - 2, white);
        fill_rect(bx + 4, by + 4, bw - 8, bh - 8, black);
        fill_rect(bx + 5, by + 5, bw - 10, bh - 10, white);

        const char *title = "You made the high scores!";
        draw_text(&A.newyork18, bx + (bw - text_width(&A.newyork18, title, 0)) / 2, by + 30, title, black, 0);
        draw_text(&A.geneva9, bx + 18, by + 58, A.str[6], black, STYLE_BOLD);      /* "Name:" */
        int fx = bx + 18 + text_width(&A.geneva9, A.str[6], STYLE_BOLD) + 8;
        fill_rect(fx, by + 46, bw - (fx - bx) - 18, 18, black);
        fill_rect(fx + 1, by + 47, bw - (fx - bx) - 20, 16, white);
        draw_text(&A.geneva9, fx + 4, by + 58, name, black, 0);
        if ((SDL_GetTicks() / 400) % 2)
            fill_rect(fx + 4 + text_width(&A.geneva9, name, 0), by + 48, 1, 13, black);
        const char *hint = "Return to accept";
        draw_text(&A.geneva9, bx + (bw - text_width(&A.geneva9, hint, 0)) / 2, by + 90, hint, black, 0);
        set_entries();
        SDL_Delay(16);
    }
    SDL_StopTextInput(SDL_GetKeyboardFocus());
    snprintf(A.score_row[slot].name, sizeof A.score_row[slot].name, "%s", n ? name : "Player");

    sfx(SND_LEVEL_CLEAR);                        /* [EnterHighScore] plays snd 144 on the way out */
    while (sfx_playing(SND_LEVEL_CLEAR) && !quit_to_title) { pump(); present(); SDL_Delay(8); }
    assets_save_scores(asset_dir);
}

static void game_over_sequence(bool finished)
{
    music_halt();
    sfx_halt_all();
    ent_clear(E_PEDE);
    set_margin_pattern(NULL);
    set_border_rings(NULL, false);

    sfx(SND_TRANSITION);                         /* FunkyFade */
    hue_scale_grey();
    delay_ticks(5);
    fade_to_color(30, RGB_WHITE);
    fade_to_color(30, RGB_BLACK);

    const Image *bg = finished ? &A.ending : &A.gameover_logo;
    sfx(150);                                    /* "Hooka Music" */
    set_monitor_clut(bg->pal, false);
    memset(fb, find_color(0, 0, 0), sizeof fb);
    draw_image(bg, (SCREEN_W - bg->w) / 2, (SCREEN_H - bg->h) / 2);
    set_clut_to_color(RGB_BLACK, false);
    fade_to(bg->pal, 60);

    bool cut = false;
    button();
    if (!finished) {
        go_wait(60, &cut);
        for (int f = 0; f < 8; f++) {
            go_wait(8, &cut);
            const Image *im = &A.gameover_anim[f];
            draw_image(im, 225, 99);             /* [GameOver] places each frame at (225, 99) */
            set_entries();
        }
    } else {
        go_wait(300, &cut);                      /* the ending picture just sits for 300 ticks */
    }
    while (sfx_playing(150) && !cut) { go_button(&cut); present(); SDL_Delay(8); }

    int slot = -1;
    for (int i = 0; i < 10; i++) if (score > A.score_row[i].score) { slot = i; break; }
    if (slot >= 0) enter_high_score(slot, bg);
    else { sfx(SND_MISSED_IT); delay_ticks(30); }

    sfx(SND_TRANSITION);                         /* FunkyFade back to the title screen */
    hue_scale_grey();
    delay_ticks(5);
    fade_to_color(20, RGB_WHITE);
    fade_to_color(20, RGB_BLACK);
    set_monitor_clut(A.title_clut, false);
}

void new_game(void)                          /* [NewGame] */
{
    quit_to_title = false;
    score = 0; next_extra = 20000;
    lives = 4; wave = 1; mult = 1;     /* "you only start out with 4 of 'em" */
    level_bonus = 2000;
    player_speed = 0x12;
    shot_limit = 1; shot_delay = 2;
    psyc_until = 0;
    flash_until = 0;
    memset(powerup, 0, sizeof powerup);
    player_dead = false;
    memset(ent, 0, sizeof ent);
    px = FIX(FIELD_W / 2 - 8);
    py = FIX(FIELD_H - 32);

    set_margin_pattern(&A.pat_game);
    set_border_rings(NULL, false);
    create_shroom_field();
    clear_spawn_area();
    start_level();

    set_monitor_clut(A.game_clut, false);
    draw_frame();
    set_clut_to_color(RGB_BLACK, false);
    fade_to(A.game_clut, 30);
    if (A.pref_music) music_start();

    bool finished = game_loop();
    game_over_sequence(finished);

    music_halt();
    sfx_halt_all();
}
