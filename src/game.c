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
#include <math.h>
#include "apeiron.h"

/* ---- tunables read from the decompile ------------------------------------------------------- */
#define PLAYER_ZONE_ROW  18        /* the lower field: what [FleaDescentCheck] and [WasteShrooms] count */
/* [InitializeAnimation] builds the shooter's own rect: bottom = field bottom less the shooter's
 * height, and top = bottom - 96. So the shooter has six rows to move in, not twelve. The same
 * rect is what [PedeMoveProc] bounces off on the way back up. */
#define SHOOTER_YMAX (FIELD_H - 16)
#define SHOOTER_TOP  (SHOOTER_YMAX - 96)
/* [ShieldFrameProc] times the shield out at 1800 ticks and warns at 1620; two ticks to a frame. */
#define POWERUP_FRAMES  900
#define FLYICON_FRAMES  20
#define POWERUP_WARN    (900 - 810)
#define RESPAWN_TICKS   180        /* [GameHouseKeeping]: 0xb4 ticks dead before respawning */
#define SHOT_SPEED      FIX(16)   /* [ShotMoveProc] subtracts exactly 16 from y each frame */
/* [CompositeFrame] ends by spinning until its frame has lasted DAT_1001ddac ticks. The value is one
 * of the globals the decompile never resolves; two ticks (30 frames a second) is what the movement
 * speeds in [StartLevel] and [FleaDescentCheck] are scaled for. */
#define TICKS_PER_FRAME 2
#define MAX_ENT         600

typedef enum {
    E_FREE = 0, E_PEDE, E_SHOT, E_FLEA, E_SCORP, E_GECKO, E_YUMMIE, E_FODDER, E_FLYICON,
    E_COIN, E_PSYC, E_UPROOT, E_EXPL, E_POINTS, E_SCUM, E_RICOCHET
} EType;

/* One frame of a centipede segment's trail. The movement routine keeps ten of these per segment;
 * the body behind reads them back one a frame, which is what makes the chain follow its head. */
typedef struct { int16_t x, y, targ; uint8_t dir, ndir; } Pt;

typedef struct Ent {
    EType type;
    bool   alive;
    fix_t  x, y, vx, vy;
    int    frame;              /* 1-based, as the original's frame index */
    int    anim;               /* animation counter */
    int    hits;               /* flea: 2 to kill; uproot: 3 */
    int    data;               /* points value, yummie kind, ... */
    int    timer;
    /* centipede, following the routine the sprite's +0x0c handler runs */
    bool    head;
    int     ix, iy;            /* whole-pixel position, as the routine keeps it */
    uint8_t dir, ndir;         /* 1 left, 2 right, 3 up, 4 down (+0x84, +0x85) */
    int     targ;              /* the row edge a vertical step stops at (+0x86) */
    Pt      q[10];             /* the trail: qr is the read index (+0x00), qw the write (+0x02) */
    int     qr, qw;
    int     follower;          /* index of the segment behind this one, -1 for none */
    int     leader;            /* index of the one it follows, -1 for a head */
    bool    counts;            /* made by [StartLevel], so the wave is not over until it dies */
    bool    poisoned;
} Ent;

static Ent ent[MAX_ENT];
static uint8_t shroom[GRID_W][GRID_H];     /* sprite frame index, 0 = no mushroom */

/* ---- game state ----------------------------------------------------------------------------- */
static long score, next_extra;
static int  lives, wave, mult;
static bool cheated;              /* a cheat other than PERNTS or SCUMDOG has been used */
static int  ticks;
static int  pede_left;                     /* [GameLoop] ends the wave when this reaches 0 */
static int  shots_out, shot_limit, shot_delay, last_shot;
static int  player_speed;
static int  shooter_frame = 1;    /* DAT_1001db18, turned by [ShooterMoveProc] */
static int  pede_speed;
static int  shroom_level;                  /* mushroom colour group: (wave-1) mod 11 */
static int  bg_index;                      /* [SetLevelBackground]: pixel pattern 128 + (wave-1)/11 */
static fix_t px, py;                       /* the player */
static int  player_anim;
static int  pede_anim;     /* [PedeFrameProc]'s counter lives on the centipede, not the segment */
static bool player_dead;
static int  death_tick;
static int  level_bonus;
static int  flea_chain;                    /* [ShotHitCheck]: doubles with each flea killed */
static long flea_next;                     /* DAT_1001dab8: ten ticks after the last one died */
static int  flea_threshold;
static int  sidefeed_period, sidefeed_last;
static bool pede_hit_bottom;               /* armed when a head first reaches the floor */
static int  psyc_at, mult_at, fodder_at, scorp_at, gecko_at;
static bool scorp_warned;                  /* his arrival is announced thirty ticks early */
static int  psyc_until;
static int  psyc_phase;                    /* DAT_1001da50: which of the three tables is up */
static int  psyc_next;                     /* DAT_1001da58: the tick the next swap is due */

/* [TransmogrifyClut] rotates every entry's components one place - red takes green's value, green
 * takes blue's and blue takes red's - and [ApeironIntro] builds three tables with it from the
 * game's own: one rotation, two, and three, which lands back on the original. [GameLoop] walks
 * those three while the psychedelic mushroom is up, so the screen cycles GBR, BRG, normal. */
static void psyc_clut(int rot, Rgb *out)
{
    for (int i = 0; i < 256; i++) {
        Rgb c = A.game_clut[i];
        for (int k = 0; k < rot; k++) {
            Rgb t = c; c.r = t.g; c.g = t.b; c.b = t.r;
        }
        out[i] = c;
    }
}

static void psyc_apply(void)
{
    Rgb tbl[256];
    psyc_clut(psyc_phase + 1, tbl);        /* phase 2 is three rotations, i.e. the game's own */
    set_monitor_clut(tbl, true);
}
/* [UpdateWeapons]'s five slots, in the order the Yummiary page describes them. */
enum { PW_CURVE = 0, PW_FREEMOVE, PW_MACHINEGUN, PW_SHIELD, PW_LOCK };
static int  powerup[5];                    /* ticks left on each */
/* Enhancement bookkeeping for the icons that fly from the coin to their box: while one is in the
 * air its destination stays empty, so the icon is only ever in one place at a time. A slot that was
 * already counting down is not hidden - topping it up should not blank it. */
/* What a box is holding back is worked out from the icons actually in the air rather than kept in
 * a counter: a counter has to be unwound by whichever event happens first, and dying mid-flight or
 * picking up a second coin got it out of step with the box. An icon that should leave its slot
 * empty until it lands carries anim = 1; one that is only topping up a slot already counting down
 * carries anim = 0 and hides nothing. */
static bool respawn_flying;      /* a replacement shooter is on its way in from the box */
/* Where a flying icon is headed. e->hits carries one of these; e->data carries the power-up slot
 * for FLY_WEAPON and the multiplier's face for FLY_MULT. */
enum { FLY_WEAPON = 0, FLY_LIFE, FLY_MULT, FLY_RESPAWN };

/* The slot the nth shooter sits in. [DrawLives] starts at DAT_1001daf8's left edge and steps
 * twenty pixels left for each of its eight slots, so slot 1 is the rightmost. */
static int lives_slot_x(int n) { if (n > 8) n = 8; if (n < 1) n = 1; return LIVES_X - (n - 1) * LIVES_DX; }

static bool quit_to_title;
static const char *asset_dir = "assets";

/* ---- helpers --------------------------------------------------------------------------------- */
static int rnd(int n) { return n > 0 ? (int)(SDL_rand(n)) : 0; }   /* [FastRandom] */

static Ent *ent_new(EType t)
{
    for (int i = 0; i < MAX_ENT; i++)
        if (!ent[i].alive) {
            memset(&ent[i], 0, sizeof ent[i]);
            ent[i].type = t; ent[i].alive = true; ent[i].frame = 1;
            ent[i].follower = ent[i].leader = -1;
            return &ent[i];
        }
    return NULL;
}

static int ent_index(const Ent *e) { return (int)(e - ent); }

static Ent *fly_icon(int kind, int data, fix_t fx, fix_t fy, bool hides)
{
    Ent *f = ent_new(E_FLYICON);
    if (!f) return NULL;
    f->x = fx; f->y = fy;
    f->vx = fx; f->vy = fy;              /* where it started, for the interpolation */
    f->hits = kind; f->data = data; f->timer = 0;
    f->anim = hides ? 1 : 0;
    return f;
}

static int fly_count(int kind, int data)
{
    int n = 0;
    for (int i = 0; i < MAX_ENT; i++) {
        const Ent *e = &ent[i];
        if (e->alive && e->type == E_FLYICON && e->hits == kind && e->anim &&
            (data < 0 || e->data == data)) n++;
    }
    return n;
}
static int lives_in_flight(void) { return fly_count(FLY_LIFE, -1); }


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
static void pluck_shroom(int c, int r) { if (in_grid(c, r)) shroom[c][r] = 0; }

/* The weapon icons are cut for the black boxes on the score panel, so what surrounds them is the
 * clut's black rather than a sheet transparency; flying one across the playfield means keying that
 * out. In the game clut black is the last entry, but it is worth looking rather than assuming. */
static uint8_t panel_black(void)
{
    static int idx = -1;
    if (idx < 0) {
        idx = 255;
        for (int i = 255; i >= 0; i--)
            if (!A.game_clut[i].r && !A.game_clut[i].g && !A.game_clut[i].b) { idx = i; break; }
    }
    return (uint8_t)idx;
}

static int shrooms_in_player_zone(void)
{
    int n = 0;
    for (int c = 0; c < GRID_W; c++)
        for (int r = PLAYER_ZONE_ROW; r < GRID_H; r++)
            if (shroom[c][r]) n++;
    return n;
}

/* Declared ahead of [add_score] so the twenty-thousand-point shooter can be sent on its way from
 * there; the icons themselves are further down with the rest of the entity work. */
static Ent *fly_icon(int kind, int data, fix_t fx, fix_t fy, bool hides);
static int lives_slot_x(int n);

static void add_score(long n)
{
    score += n;
    if (score >= next_extra) {
        lives++; next_extra += 20000; sfx(SND_EXTRA_SHIP);
        /* Enhancement: a shooter earned off the scoreboard leaves the Score box and flies down to
         * the Lives box, the same way one picked up off a coin flies out of the playfield. The
         * score reads out of a 168-wide box whose top is at y 133, so it starts from the middle
         * of that. */
        if (enh[ENH_WEAPON_FLY] && lives <= 8)
            fly_icon(FLY_LIFE, 0, FIX(PANEL_X - FIELD_X + 100), FIX(145), lives <= 8);
    }
}

/* [CreatePoints] / [CreateBonusPoints]: the little number that floats up from a kill */
/* [CreatePoints] / [CreateBonusPoints]. Every digit of the value is a sprite of its own, started
 * at the middle of whatever was hit and given its own sideways speed: the leftmost digit drifts
 * left fastest, the rightmost right, two pixels a frame apart. [PointsMoveProc] then subtracts
 * those from the position each frame - so the number rises at a pixel a frame and spreads out as
 * it goes - and takes them all away after twenty frames. */
static void create_points(fix_t x, fix_t y, int value, bool bonus)
{
    int digit[12], n = 0;
    while (value > 0 && n < 12) { digit[n++] = value % 10; value /= 10; }
    if (!n) return;
    int spread = ((n - 1) >> 1) * 2;
    if (n % 2 == 0) spread += 1;
    const Sprite *ds = bonus ? &A.bonuspoints : &A.points;
    for (int i = n - 1; i >= 0; i--, spread -= 2) {
        Ent *e = ent_new(E_POINTS);
        if (!e) return;
        e->x = x + FIX(8);                  /* the original adds the target's own width/2... */
        e->y = y + FIX(8);                  /* ...and height/2; all but a few targets are 16x16 */
        e->frame = digit[i] + 1;
        e->vx = FIX(spread);
        e->vy = FIX(1);
        e->timer = 0;
        e->hits = bonus ? 1 : 0;
        (void)ds;
    }
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

/* ---- the centipede ----------------------------------------------------------------------------
 * A direct port of the routine the segment's +0x0c handler runs each frame (at 0x10008c18).
 *
 * Direction is a byte: 1 left, 2 right, 3 up, 4 down, held in `dir` with the one to take after the
 * current vertical step in `ndir`, and the row edge that step stops at in `targ`.
 *
 * A head walks horizontally until the wall, a mushroom or an oncoming segment stops it; then it takes
 * `dir = ndir` (the vertical direction), sets `ndir` to the opposite horizontal, and sets `targ` one
 * row along. The vertical step runs until it passes `targ`, at which point it takes `ndir` and aims
 * vertically again. At the floor it bounces back up and - the first time it happens in a wave - arms
 * the side feed, which is `[HandleSideFeed]`'s missing flag.
 *
 * A mushroom whose frame is 1..4 is poisoned (that colour group is reserved for them, which is why
 * [PlantShroom] starts at frame 5). Touching one sets `poisoned`, and a poisoned head keeps heading
 * down instead of turning until it reaches the floor.
 *
 * A body never runs any of this: it reads one frame of its leader's trail per frame and copies it.
 * [StartLevel] pre-fills 16/speed entries, so each segment trails its leader by exactly 16 pixels.
 */
enum { D_LEFT = 1, D_RIGHT = 2, D_UP = 3, D_DOWN = 4 };

#define PEDE_XMIN 0                         /* DAT_1001db3e */
#define PEDE_XMAX (FIELD_W - CELL)          /* DAT_1001db42 */
#define PEDE_YMIN 0                         /* DAT_1001db3c */
#define PEDE_YMAX (FIELD_H - CELL)          /* DAT_1001db40 */
#define PEDE_YTOP SHOOTER_TOP               /* DAT_1001db2c.top: how far back up a bounce carries it */

static void pede_kill(Ent *e, bool by_shot);
static void start_level(void);
static void take_yummie(Ent *e);

static void pede_trail_push(Ent *e)
{
    e->q[e->qw].x = (int16_t)e->ix;
    e->q[e->qw].y = (int16_t)e->iy;
    e->q[e->qw].dir = e->dir;
    e->q[e->qw].ndir = e->ndir;
    e->q[e->qw].targ = (int16_t)e->targ;
    if (++e->qw > 9) e->qw = 0;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* Which explosion artwork [CreateExplosion]'s second argument picks, by the sprite definition each
 * caller passes: DAT_1001db80 is PICT 1009 (the big one), db88 PICT 1011, db8c PICT 1012,
 * db94 PICT 1014 and dba8 PICT 1020. */
enum { EX_SPLAT = 0, EX_BIG, EX_SCORP, EX_GECKO, EX_FLEA };
static const Sprite *expl_sprite(int kind)
{
    switch (kind) {
    case EX_BIG:   return &A.explode_shooter;   /* the shooter, the fodder, and what the fodder hits */
    case EX_SCORP: return &A.scorpsplat;
    case EX_GECKO: return &A.geckosplat;
    case EX_FLEA:  return &A.splat_red;
    default:       return &A.splat;             /* a centipede segment */
    }
}

/* a mushroom in colour group 0 (frames 1..4) is a poisoned one */
static bool shroom_poisoned(int c, int r)
{
    return in_grid(c, r) && shroom[c][r] > 0 && shroom[c][r] < 5;
}

static void pede_poison(Ent *e)
{
    if (!e->poisoned) sfx(SND_POISONED);
    e->poisoned = true;
}

/* the turn a head makes when something stops it going sideways */
static void pede_turn(Ent *e, uint8_t new_horizontal)
{
    e->dir = e->ndir;
    /* "if a Pentipede hits a poisoned shroom, he plummets immediately to the bottom of the screen" -
     * the routine achieves that by never stopping at the row edge while poisoned; forcing the turn
     * downward as well makes it immediate however it was travelling. */
    if (e->poisoned) e->dir = D_DOWN;
    e->ndir = new_horizontal;
    e->targ = (e->dir == D_DOWN) ? e->iy + CELL : e->iy - CELL;
}

/* The head of whichever centipede this segment belongs to. */
static const Ent *pede_head_of(const Ent *e)
{
    for (int guard = 0; guard < MAX_ENT; guard++) {
        if (e->leader < 0) break;
        const Ent *ld = &ent[e->leader];
        if (!ld->alive || ld->type != E_PEDE) break;
        e = ld;
    }
    return e;
}

/* Another segment coming the other way turns this one around - [PedeMoveProc] runs a
 * [CollisionCheck] against DAT_1001db48 and then tests the direction held in the OTHER sprite's
 * record at +0x58. That record is shared by every segment of one centipede, so a piece of this
 * same centipede always reads back this segment's own direction, never the opposing one, and is
 * skipped without the routine needing to say so. This port keeps a direction per segment instead,
 * and a follower's lags its head's by a frame: the moment a head reverses, the segment right behind
 * it is still carrying the old direction and looks exactly like something coming the other way. The
 * head would turn again, and again, bouncing a row at a time up and down on the spot with its one
 * follower - with no mushroom and no wall in sight. Comparing chains is what the shared record was
 * doing for free. */
static Ent *pede_facing(Ent *e, uint8_t their_dir)
{
    const Ent *mine = pede_head_of(e);
    for (int i = 0; i < MAX_ENT; i++) {
        Ent *o = &ent[i];
        if (!o->alive || o->type != E_PEDE || o == e) continue;
        if (o->dir != their_dir) continue;
        if (pede_head_of(o) == mine) continue;        /* one of my own */
        if (abs(o->ix - e->ix) < CELL && abs(o->iy - e->iy) < CELL) return o;
    }
    return NULL;
}

static void pede_head_step(Ent *e)
{
    int x = e->ix, y = e->iy, sp = pede_speed;

    /* [PedeMoveProc] keeps two directions: +132 is what the head is doing now and +133 is what it
     * does next, and every transition out of a vertical move is `dir = dir2`. That only terminates
     * because dir2 holds a *horizontal* whenever dir is vertical. If the pair is ever vertical and
     * vertical together, `dir = dir2` is a no-op and x can never change again: the head bobs up and
     * down on the spot until the wave ends, which is what sticking to a side wall looks like (#26).
     * Walking the reachable states from the three birth states says the pair cannot come apart on
     * its own, but a chain relinked by a kill copies dir and dir2 out of a dead leader's trail, so
     * the invariant is restored here rather than trusted. The head then carries on away from
     * whichever wall it is nearer, which is where its next turn would have sent it anyway. */
    if ((e->dir == D_UP || e->dir == D_DOWN) && (e->ndir == D_UP || e->ndir == D_DOWN))
        e->ndir = (x <= (PEDE_XMIN + PEDE_XMAX) / 2) ? D_RIGHT : D_LEFT;

    switch (e->dir) {
    case D_UP:
        y -= sp;
        if (y < PEDE_YTOP) { y = PEDE_YTOP; e->dir = e->ndir; e->ndir = D_DOWN; }
        else if (y < e->targ) { y = e->targ; e->dir = e->ndir; e->ndir = D_UP; }
        break;

    case D_LEFT: {
        x -= sp;
        int col = clampi(x >> 4, 0, GRID_W - 1), row = clampi(y >> 4, 0, GRID_H - 1);
        if (x < PEDE_XMIN) { x = PEDE_XMIN; e->iy = y; pede_turn(e, D_RIGHT); y = e->iy; }
        else if (shroom[col][row]) {
            if (shroom_poisoned(col, row)) pede_poison(e);
            e->iy = y; pede_turn(e, D_RIGHT); y = e->iy;
        } else {
            Ent *o = pede_facing(e, D_RIGHT);
            if (o) {
                x = clampi(o->ix + CELL, PEDE_XMIN, PEDE_XMAX);
                e->iy = y; pede_turn(e, D_RIGHT); y = e->iy;
                o->iy = o->iy; pede_turn(o, D_LEFT);
            }
        }
        break;
    }

    case D_RIGHT: {
        x += sp;
        int col = clampi((x + CELL) >> 4, 0, GRID_W - 1), row = clampi(y >> 4, 0, GRID_H - 1);
        if (x > PEDE_XMAX) { x = PEDE_XMAX; e->iy = y; pede_turn(e, D_LEFT); y = e->iy; }
        else if (shroom[col][row]) {
            if (shroom_poisoned(col, row)) pede_poison(e);
            e->iy = y; pede_turn(e, D_LEFT); y = e->iy;
        } else {
            Ent *o = pede_facing(e, D_LEFT);
            if (o) {
                x = clampi(o->ix - CELL, PEDE_XMIN, PEDE_XMAX);
                e->iy = y; pede_turn(e, D_LEFT); y = e->iy;
                pede_turn(o, D_RIGHT);
            }
        }
        break;
    }

    default: {                              /* D_DOWN */
        y += sp;
        int col = clampi(x >> 4, 0, GRID_W - 1), row = clampi(y >> 4, 0, GRID_H - 1);
        if (shroom_poisoned(col, row)) pede_poison(e);
        if (e->poisoned) e->ndir = D_RIGHT;
        if (y > PEDE_YMAX) {
            y = PEDE_YMAX;
            e->dir = e->ndir;
            if (!e->poisoned) {
                e->ndir = D_UP;
            } else {
                e->dir = D_UP;
                e->ndir = D_RIGHT;
                e->targ = PEDE_YMAX - CELL;
            }
            /* the first segment to reach the floor is what lets [HandleSideFeed] start */
            if (!pede_hit_bottom && !e->poisoned) {
                pede_hit_bottom = true;
                sidefeed_last = ticks;
            }
            e->poisoned = false;
        } else if (e->targ < y && !e->poisoned) {
            y = e->targ;
            e->dir = e->ndir;
            e->ndir = D_DOWN;
        }
        break;
    }
    }
    e->ix = x;
    e->iy = y;
}

static void pede_update(Ent *e)
{
    if (e->head) {
        pede_head_step(e);
    } else {
        Ent *ld = (e->leader >= 0 && ent[e->leader].alive && ent[e->leader].type == E_PEDE)
                ? &ent[e->leader] : NULL;
        if (!ld) { e->head = true; e->leader = -1; pede_head_step(e); }
        else {
            const Pt *p = &ld->q[ld->qr];
            e->ix = clampi(p->x, PEDE_XMIN, PEDE_XMAX);
            e->iy = clampi(p->y, PEDE_YMIN, PEDE_YMAX);
            e->dir = p->dir; e->ndir = p->ndir; e->targ = p->targ;
            if (++ld->qr > 9) ld->qr = 0;
        }
    }
    pede_trail_push(e);
    e->x = FIX(e->ix);
    e->y = FIX(e->iy);

    e->anim++;
    int base = e->head ? (e->dir == D_RIGHT ? 1 : 17) : (e->dir == D_RIGHT ? 33 : 49);
    e->frame = base + (e->anim / 3) % 8;
}

/* A segment dies: it leaves a mushroom, and the segment behind it becomes a new head. */
static void pede_kill(Ent *e, bool by_shot)
{
    int col = clampi((e->ix + 8) / CELL, 0, GRID_W - 1);
    int row = clampi((e->iy + 8) / CELL, 0, GRID_H - 1);

    if (by_shot) {
        long v = e->head ? 100L * mult : 10L * mult;
        add_score(v);
        create_points(e->x, e->y, (int)v, false);
        create_expl(e->x, e->y, EX_SPLAT);
        sfx(SND_SQUISH);
        if (!shroom[col][row]) plant_shroom(col, row);
    }
    if (e->follower >= 0 && ent[e->follower].alive && ent[e->follower].type == E_PEDE) {
        Ent *f = &ent[e->follower];
        f->head = true;
        f->leader = -1;
    }
    if (e->leader >= 0 && ent[e->leader].alive && ent[e->leader].type == E_PEDE)
        ent[e->leader].follower = -1;
    e->alive = false;
    if (e->counts) pede_left--;
}

/* [HandleSideFeed]: a lone head walks in from one side, low down */
static void side_feed(void)
{
    /* [HandleSideFeed] opens by playing DAT_1001dc44, which [HandleInitializations] loaded from
     * snd 0x98 - 152, filed under the name "Killed Root" but used here for the hiccup a new head
     * makes coming in from the side. */
    sfx(SND_KILLED_ROOT);
    Ent *e = ent_new(E_PEDE);
    if (!e) return;
    int left = rnd(2) == 1;
    e->head = true;
    e->dir = left ? D_RIGHT : D_LEFT;       /* [HandleSideFeed] sets +0x84 to 2 or 1 */
    e->ndir = D_DOWN;
    e->ix = left ? 0 : PEDE_XMAX;
    e->iy = 0x160;                          /* y = 352 in the original */
    e->x = FIX(e->ix); e->y = FIX(e->iy);
    e->follower = -1; e->leader = -1;
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
    sfx(SND_GROUCHO_LAUGH);           /* [FleaDescentCheck] plays snd 140 with [CreateFlea] */
}

/* Larry, who drifts up and down eating mushrooms - the 'scor' sprite at DAT_1001dc8c. Which side
 * he comes in from is decided by the wave's parity: on an even wave one roll of four in his favour
 * sends him in from the left, and on an odd wave three of them do, so he comes from the left three
 * times in four for most of a game. */
static void create_scorpion(void)
{
    Ent *e = ent_new(E_SCORP);
    if (!e) return;
    int row = player_speed + rnd(28 - player_speed);    /* DAT_1001dad0 + FastRandom(0x1c - it) */
    bool from_left = (wave % 2 == 0) ? (rnd(4) == 0) : (rnd(4) != 0);
    e->x = from_left ? 0 : FIX(0x198);
    e->y = FIX(row * CELL);
    int sp = score < 5000 ? 3 : 4;
    e->vx = FIX(from_left ? sp : -sp);
    e->vy = FIX(sp);
}

static void create_gecko(void)              /* Gordon, who walks across poisoning what he crosses */
{
    Ent *e = ent_new(E_GECKO);
    if (!e) return;
    e->x = FIX(0x186);
    e->y = FIX(rnd(0x11) * CELL);
    /* [GeckoMoveProc] does x -= vx, so a positive vx walks him leftwards off the field. The second
     * roll is made whatever the score is - it only has an effect above twenty thousand, but it
     * comes out of the same random sequence either way, so it is made either way. */
    int v = score > 20000 ? 5 : 3;
    if (rnd(4) == 1) v = 3;
    e->vx = FIX(v);
    sfx(SND_GECKO_APPEARS);
}

/* [CreateYummie]. One coin in seven is thrown rather than dropped, and once in twenty a whole
 * handful of three to fifteen comes out, all of them thrown. A dropped one falls at four pixels a
 * frame under no gravity at all - [YummieMoveProc] only applies gravity to a coin with sideways
 * speed - which is why dropping it at one looked like it was hanging in the air. */
static void create_yummie(fix_t x, fix_t y)
{
    bool thrown = rnd(7) == 1;
    int n = 1;
    if (rnd(0x14) == 1) { n = rnd(0xd) + 3; thrown = true; }
    for (int i = 0; i < n; i++) {
        Ent *e = ent_new(E_YUMMIE);
        if (!e) break;
        e->x = x; e->y = y;
        e->anim = rnd(A.coin.count);
        if (thrown) {
            e->vx = (fix_t)(rnd(0x46) << 12);
            if (rnd(2) == 0) e->vx = -e->vx;
            e->vy = -(fix_t)((rnd(0x14) + 10) * 0x8000);
        } else {
            e->vy = FIX(4);
            e->vx = 0;
        }
    }
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
    /* The multiplier the tile is worth, 2 to 5 - which is what the original keeps too: [UpdateWeapons]
     * calls [DrawMult](value) and [DrawMult] asks [FindFaceRect] for frame value-1, and that routine
     * is 1-based, so a stored 5 draws the fourth tile, the one reading "5". */
    e->data = rnd(4) + 2;
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
    sfx(SND_RIFF);      /* [GameHouseKeeping] plays snd 153 with [CreatePsychadelic] */
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
    /* no sound of its own: [ShotHitCheck] plays snd 149 as it calls [CreateUproot] */
}

/* ---- the player ------------------------------------------------------------------------------ */
static void count_off_shrooms(int snd, bool wave_clear);
static int  flash_until;                   /* [GameHouseKeeping]: SetEntries to the red table */

static void player_die(void)                /* [GameHouseKeeping]'s death path */
{
    if (player_dead) return;
    if (powerup[PW_SHIELD]) { sfx(SND_BONK); return; }   /* "invincible to harm, for a limited time" */
    player_dead = true;
    death_tick = ticks;
    /* [GameHouseKeeping] swaps the whole colour table for the one [HueScale](1) builds - the game's
     * palette mapped onto pure red - and puts the game's own table back 20 ticks later. */
    set_monitor_clut(A.game_clut, false);
    Rgb red = { 0xFFFF, 0, 0 };
    hue_scale_to(red, true);
    flash_until = ticks + 0x14;
    music_halt();
    sfx(SND_SHIP_EXPLOSION);        /* [GameHouseKeeping]'s death branch plays snd 132; snd 145
                                     * "Oh no" belongs to the Clear Scores dialog, not to dying */
    create_expl(px, py, EX_BIG);
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
    if (powerup[PW_CURVE]) e->data = 1;     /* "you can curve 'em into your enemies" */
    /* [ShroomShotCheck] ends on the machine-gun flag: with it up each shot is snd 167 "Bam!",
     * otherwise snd 133 "Ship Laser". */
    sfx(powerup[PW_MACHINEGUN] ? SND_BAM : SND_SHIP_LASER);
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
    for (int i = 0; i < 30; i++) {
        int c, r;
        do { c = rnd(GRID_W); r = rnd(GRID_H - 1); } while (shroom[c][r]);
        plant_shroom(c, r);
    }
}

static void start_level(void)
{
    /* [StartLevel] clears DAT_1001da52 and DAT_1001da50 right after InitOnTheFlyFade, so a
     * psychedelic mushroom still running when the wave ends does not carry into the next one. It
     * matters more here than it does there: this port restarts `ticks` at zero for each wave, so a
     * psyc_until left standing from the old wave is unreachable and the rotated table would stay on
     * the monitor until the next death put it back. */
    if (psyc_until) { psyc_until = 0; mult = 1; if (A.pref_music) music_start(); }
    psyc_phase = 2; psyc_next = 0;
    set_monitor_clut(A.game_clut, true);

    ticks = 0;
    pede_left = 0;
    shots_out = 0;
    last_shot = 0;
    flea_chain = 200;
    flea_next = 60;
    sidefeed_last = 0;
    pede_hit_bottom = false;
    ent_clear(E_PEDE); ent_clear(E_SHOT); ent_clear(E_FLEA); ent_clear(E_SCORP);
    ent_clear(E_GECKO); ent_clear(E_YUMMIE); ent_clear(E_FODDER); ent_clear(E_COIN);
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
        e->dir = D_RIGHT; e->ndir = D_DOWN;     /* [StartLevel] sets +0x84 = 2, +0x85 = 4 */
        e->ix = startx; e->iy = 0;
        e->x = FIX(e->ix); e->y = FIX(e->iy);
        e->anim = i * 2;
        e->counts = true;
        e->leader = prev;
        if (prev >= 0) ent[prev].follower = ent_index(e);
        /* [StartLevel] pre-fills 16/speed trail entries, so each body trails its leader by a cell */
        for (int k = 0; k < CELL / pede_speed; k++) pede_trail_push(e);
        prev = ent_index(e);
        pede_left++;
    }
    for (int i = 0; i < extra_heads; i++) {
        Ent *e = ent_new(E_PEDE);
        if (!e) break;
        int c;
        do { c = rnd(GRID_W); } while (false);
        e->head = true;
        e->dir = rnd(2) ? D_LEFT : D_RIGHT; e->ndir = D_DOWN;
        e->ix = c * CELL; e->iy = 0;
        e->x = FIX(e->ix); e->y = FIX(e->iy);
        e->leader = -1;
        e->counts = true;
        pede_left++;
    }

    /* [StartLevel] schedules the wave's extras with the same dice the original rolls, in the order
     * it rolls them - FastRandom is one sequence, so a roll skipped here moves every later one.
     * Larry is deliberately not among them: DAT_1001dacc survives a wave change. */
    psyc_at = mult_at = fodder_at = gecko_at = 0;
    if (rnd(10) == 1) psyc_at = rnd(30) * 60 + 120;
    if (rnd(3) == 1) mult_at = rnd(30) * 60 + 120;
    if (rnd(3) == 1) fodder_at = rnd(30) * 60 + 120;
    /* Gordon's roll is made whatever the mushroom colour is and thrown away afterwards if the
     * colour is one of the first three of its group, which is how the original does it. */
    if (rnd(4) == 1) gecko_at = rnd(20) * 60 + 120;
    if (shroom_level < 3) gecko_at = 0;
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

/* [PedeFrameProc]: the frames a segment shows depend on the way it is facing - one horizontal
 * direction runs 1..14, the other 15..28, and a vertical step uses 29..31. A body segment ('pbod')
 * is the same thing 31 frames further along the strip, which is where the 32..45 the instruction
 * page shows comes from. */
static int pede_frame(const Ent *e)
{
    int base, n;
    switch (e->dir) {
    case D_LEFT:  base = 15; n = 14; break;
    case D_UP: case D_DOWN: base = 29; n = 3; break;
    default:      base =  1; n = 14; break;      /* D_RIGHT */
    }
    if (!e->head) base += 31;
    /* [PedeFrameProc] keeps its counter in the shared record at +0x58, not on the segment, so every
     * piece of a centipede shows the same frame - the eyes blink in unison, once every fourteen
     * frames of the thirty a second. A per-segment counter puts the pieces out of step as soon as
     * one splits off. */
    return base + pede_anim % n;
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
        /* Each creature's own frame routine, which [AnimateFrame] runs once a game frame:
         * [GenericFrameProc] walks the whole strip, [GeckoFrameProc] stops at 8, [ScorpianFrameProc]
         * at 20, [MultFrameProc] and [RootFrameProc] do nothing at all, and [PsychFrameProc] steps
         * four at a time from 5, which is what makes the psychedelic shroom flash colours. */
        case E_PEDE:    spr(&A.pede, pede_frame(e), e->x, e->y); break;
        case E_SHOT:    spr(&A.shot, 1, e->x, e->y); break;
        case E_FLEA:    spr(&A.flea, 1 + e->anim % A.flea.count, e->x, e->y); break;
        case E_SCORP:   spr(&A.scorpion, 1 + e->anim % 20, e->x, e->y); break;     /* Larry  */
        case E_GECKO:   spr(&A.gecko, 1 + e->anim % 8, e->x, e->y); break;         /* Gordon */
        case E_YUMMIE:  spr(&A.coin, 1 + e->anim % A.coin.count, e->x, e->y); break;   /* the coin */
        case E_FODDER:  spr(&A.fodder, 1 + e->anim % A.fodder.count, e->x, e->y); break;
        case E_COIN:    spr(&A.yummie, e->data - 1, e->x, e->y); break;      /* the multiplier tile */
        case E_PSYC:    spr(&A.shroom, 5 + (e->anim * 4) % (A.shroom.count - 4), e->x, e->y); break;
        case E_UPROOT:  spr(&A.shroom, e->frame, e->x, e->y); break;
        case E_SCUM:    spr(e->data ? &A.flak : &A.scum, e->frame, e->x, e->y); break;
        case E_EXPL:
            spr(expl_sprite(e->data),
                e->frame, e->x, e->y);
            break;
        case E_RICOCHET: spr(&A.ricochet, 1, e->x, e->y); break;
        case E_FLYICON: break;  /* drawn last of all, over the panel - see the end of this function */
        case E_POINTS:          /* one digit, its frame set when it was made */
            draw_sprite_clipped(e->hits ? &A.bonuspoints : &A.points, e->frame,
                                FIELD_X + FIX_WHOLE(e->x), FIELD_Y + FIX_WHOLE(e->y), CLIP);
            break;
        default: break;
        }
    }

    if (!player_dead) {
        /* [ShooterFrameProc] just reads the frame out of a global that [ShooterMoveProc] turns by
         * half the sideways movement each frame, wrapping round the strip - so the crystal spins
         * while you move and holds still when you stop. */
        if (!powerup[PW_SHIELD])      /* with the shell up it is all you see of the ship */
            draw_sprite_clipped(&A.shooter, shooter_frame,
                                FIELD_X + FIX_WHOLE(px), FIELD_Y + FIX_WHOLE(py), CLIP);
        if (powerup[PW_SHIELD])             /* the sprinkly shield wrapped round the shooter */
            /* [ShieldMoveProc] snaps the shell to the shooter's own position every frame - the
             * five-pixel lift in [MakeInvincible] is only where it starts - and [ShieldFrameProc]
             * steps it a frame a frame, so it sits exactly over the crystal and covers it. */
            draw_sprite_disc(&A.shield, 1 + player_anim % A.shield.count,
                             FIELD_X + FIX_WHOLE(px), FIELD_Y + FIX_WHOLE(py), CLIP);
    }

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

    /* The panel's own display boxes, measured out of PICT 401 itself: the score, bonus and lives
     * boxes are 168x32 at y 133, 233 and 332, and the wave box is 59x31 at y 433. A digit is 24x32,
     * so each number's top is the box's own top - a pixel lower and it spills over the bevel. */
    draw_digits(score, PANEL_X + 186, 133, 0, 0);
    draw_digits(level_bonus, PANEL_X + 186, 233, 0, 0);
    /* [DrawLives] always lays out EIGHT slots - the same cap the extra-shooter roll uses - twenty
     * pixels apart, starting at DAT_1001daf8's {top 340, left 597} and stepping left. The first
     * `lives` of them get PICT 1101, a shooter in hand; the rest get PICT 1102, the same empty
     * socket the weapons box uses. Nothing shifts along as the count changes: a life won or lost
     * just lights or clears the next slot. */
    int nships = lives - lives_in_flight();     /* one in the air is not in its slot yet */
    /* Enhancement: the spares turn on the spot, slowly - a sixth of the rate the shooter itself
     * turns under the mouse. The original draws them all on frame 1. */
    bool turn = enh[ENH_WEAPON_ANIM];
    int ship_f = turn ? 1 + (player_anim / 6) % A.shooter.count : 1;
    for (int i = 0; i < 8; i++) {
        int lx = LIVES_X - i * LIVES_DX;
        if (i < nships) {
            if (turn) draw_sprite(&A.shooter, ship_f, lx, LIVES_Y);
            else      draw_sprite(&A.life, 1, lx, LIVES_Y);
        } else {
            draw_sprite(&A.wpn_none, 1, lx, LIVES_Y);   /* PICT 1102, the empty socket */
        }
    }
    /* [UpdateWeapons]: six 14x14 icons, 15 apart ([DrawWeapon] is called at +0, +0xf, +0x1e, +0x2d,
     * +0x3c, +0x4b), the multiplier in the first. The red box's black interior runs x 59..149 and
     * y 380..397, so the run of six sits one in from each side of it. */
    for (int i = 0; i < 6; i++) {
        int wx = PANEL_X + 60 + i * 15, wy = 382;
        if (i == 0) {
            if (mult >= 2 && mult <= 5 && !fly_count(FLY_MULT, -1))
                draw_sprite(&A.yummie, mult - 1, wx, wy);  /* [DrawMult] */
        } else if (powerup[i - 1] > 0 && !fly_count(FLY_WEAPON, i - 1)) {
            const Sprite *w = &A.wpn[i - 1];
            int f = 1, dx = 0, dy = 0;
            bool show = true;
            if (enh[ENH_WEAPON_ANIM]) {
                /* Each of the five icons is a single 14x14 cell in the original, so there is
                 * nothing in the icon art to animate. The shield is the one power-up that has a
                 * playfield animation to borrow: its six 16x16 frames, pulled back a pixel on each
                 * side so the extra two land inside the slot's own 15-pixel stride. */
                if (i - 1 == PW_SHIELD && A.shield.count > 1) {
                    w = &A.shield;
                    f = 1 + player_anim % A.shield.count;
                    dx = dy = -1;
                } else if (w->count > 1) {
                    f = 1 + (player_anim / 3) % w->count;
                }
                if (powerup[i - 1] <= POWERUP_WARN) show = (player_anim / 6) % 2 == 0;
            }
            if (show) draw_sprite(w, f, wx + dx, wy + dy);
        }
    }
    /* [GameHouseKeeping] calls [DrawNumDisplay](wave, .., pad) with pad set below ten, which makes
     * [DrawNumDisplay] put the strip's eleventh (blank) tile to the left of the digit - so the wave
     * is drawn at full size in its 59-wide box, two digits' worth, not shrunk. */
    {
        int w = wave;
        while (w > 99) w -= 99;              /* the same wrap [GameHouseKeeping] does */
        draw_digits(w, PANEL_X + 126, 432, 0, 0);   /* two digits' worth, centred in the box */
    }
    /* The icons on their way to the panel go on last, so they pass over the score boxes rather
     * than behind them, and with the panel's black keyed out rather than flown as a black tile. */
    for (int i = 0; i < MAX_ENT; i++) {
        Ent *e = &ent[i];
        if (!e->alive || e->type != E_FLYICON) continue;
        const Sprite *s; int f;
        switch (e->hits) {
        case FLY_MULT: s = &A.yummie; f = e->data - 1; break;
        case FLY_LIFE: case FLY_RESPAWN: s = &A.shooter; f = shooter_frame; break;
        default:       s = &A.wpn[e->data]; f = 1; break;
        }
        draw_sprite_key(s, f, FIELD_X + FIX_WHOLE(e->x), FIELD_Y + FIX_WHOLE(e->y), panel_black());
    }

    (void)white; (void)yellow;
}

/* [WasteShrooms] walks the mushroom layer and, for every mushroom whose row is past 18 - the rows
 * the shooter itself can reach - blows it up with the big explosion (PICT 1009), plucks it and
 * plays snd 162. It scores nothing: it is a clearing of your own back yard, not a kill. */
static void waste_shrooms(void)
{
    for (int c = 0; c < GRID_W; c++)
        for (int r = PLAYER_ZONE_ROW + 1; r < GRID_H; r++) {
            if (!shroom[c][r]) continue;
            create_expl(FIX(c * CELL), FIX(r * CELL), EX_BIG);
            pluck_shroom(c, r);
            sfx(SND_EXPLOSION);
        }
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
            if (t->hits < 2) { sfx(SND_GROUCHO_OUCH); t->vy += FIX(3); }
            else {
                sfx(SND_GROUCHO_DEATH);         /* [ShotHitCheck]'s 'flea' branch: 137 then 130 */
                sfx(SND_SQUISH);
                add_score((long)flea_chain * mult);
                create_points(t->x, t->y, flea_chain * mult, false);
                create_expl(t->x, t->y, EX_FLEA);   /* [ShotHitCheck]'s 'flea' branch: PICT 1012 */
                flea_chain <<= 1;
                flea_next = ticks + 10;             /* DAT_1001dab8 = DAT_1001da90 + 10 */
                t->alive = false;
            }
            break;
        case E_SCORP: {                 /* Larry: worth more the closer he is */
            sfx(SND_SCORP_DIES);
            sfx(SND_SQUISH);
            int dx = FIX_WHOLE(px) - FIX_WHOLE(t->x);
            if (dx < 0) dx = -dx;
            int v = (1500 - dx * 10) / 100 * 100;
            if (v < 300) v = 300;
            add_score((long)v * mult);
            create_points(t->x, t->y, v * mult, false);
            create_expl(t->x, t->y, EX_SCORP);
            scorp_at = ticks + rnd(360) + 360;
            t->alive = false;
            break;
        }
        case E_GECKO:                   /* Gordon the Gecko: a flat 1000 */
            sfx(SND_SHOT_GECKO);
            add_score(1000L * mult);
            create_points(t->x, t->y, 1000 * mult, false);
            create_expl(t->x, t->y, EX_GECKO);
            t->alive = false;
            break;
        case E_YUMMIE:                          /* the coin: knocked about, not killed */
            sfx(SND_BOING1);
            t->vy = -(fix_t)((rnd(0x14) + 10) * 0x8000);
            t->vx = (fix_t)((rnd(40) + 10) * 4096) * (rnd(2) ? 1 : -1);
            break;
        case E_FODDER: {
            int v = (rnd(6) + 1) * 500;
            create_points(t->x, t->y, v, true);
            create_expl(t->x, t->y, EX_BIG);     /* the fodder uses PICT 1009, the big one */
            sfx(SND_EXPLOSION);             /* [ShotHitCheck]'s 'fodr' branch: 162 then 156 */
            sfx(SND_MYHOOHA);
            level_bonus += v;
            t->alive = false;
            break;
        }
        case E_COIN:
            sfx(SND_MOAN);      /* [ShotHitCheck]'s 'mult' branch */
            mult = t->data;
            t->alive = false;
            if (enh[ENH_WEAPON_FLY] && mult >= 2 && mult <= 5) {
                fix_t fx = t->x, fy = t->y;
                fly_icon(FLY_MULT, mult, fx, fy, true);
            }
            break;
        case E_PSYC:
            sfx(SND_GOT_FUNKY);
            mult = 10;                     /* [ShotHitCheck] sets the points multiplier to ten */
            psyc_until = ticks + 900;
            psyc_next = ticks; psyc_phase = 0;
            music_halt();
            t->alive = false;
            break;
        case E_UPROOT:
            t->hits++;
            if (t->hits < 3) {
                sfx(SND_SHROOM_POP);            /* [ShotHitCheck]'s 'root' branch: 131 then 164 */
                sfx(SND_HEY);
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
            /* the routine clamps a segment inside the field, so nothing can wander off it */
            break;

        case E_SHOT: {
            /* A shot covers more than a cell each tick, so it is stepped a pixel at a time: sampling
             * only where it ends up let it pass straight through a mushroom directly above. */
            /* [ShotMoveProc]: with the Z-145 transflogulator up, the shot's x is simply assigned
             * the shooter's x plus eight every frame - it does not drift towards it, it is locked
             * to it, so moving while a shot is in the air bends its whole path. */
            if (e->data) e->x = px + FIX(8);
            int steps = -FIX_WHOLE(e->vy);
            if (steps < 1) steps = 1;
            for (int k = 0; k < steps; k++) {
                e->y -= FIX(1);
                if (!e->data) e->x += e->vx / steps;
                if (FIX_WHOLE(e->y) < -12) { e->alive = false; shots_out--; break; }
                int col = (FIX_WHOLE(e->x) + 2) / CELL, row = FIX_WHOLE(e->y) / CELL;
                if (in_grid(col, row) && shroom[col][row]) {
                    create_scum(FIX(col * CELL), FIX(row * CELL));
                    sfx(SND_HIT);               /* [ShotHitCheck]: a shroom taking a hit is snd 129 */
                    int f = shroom[col][row];
                    /* [ShotHitCheck] branches on the damage frame. On a multiple of four the
                     * mushroom is finished; on frame % 4 == 1 it can uproot; on anything else it
                     * can bounce the shot. Both of those odds are counted off DAT_1001da2c, which
                     * [NewGame] sets to 1 and [EnterHighScore] files beside the score - the wave -
                     * and both clamps are floors rather than ceilings, so the odds lengthen with
                     * the wave until they settle. */
                    if (f % 4 == 0) {                           /* last damage state: destroyed */
                        if (rnd(15) == 0) create_yummie(FIX(col * CELL), FIX(row * CELL));
                        long v = mult * (f < 5 ? 5 : 1);
                        create_points(FIX(col * CELL), FIX(row * CELL), (int)v, false);
                        pluck_shroom(col, row);
                        add_score(v);
                    } else if (f % 4 == 1) {
                        int n = 50 - wave;                      /* 1 in 40 at wave 10, 1 in 10 past 40 */
                        if (n < 10) n = 10;
                        if (wave > 11 && rnd(n) == 1) {
                            sfx(SND_DIEHL_BELCH);  /* snd 149, played with [CreateUproot] */
                            create_uproot(col, row);
                            pluck_shroom(col, row);
                        } else {
                            shroom[col][row] = (uint8_t)(f + 1);
                        }
                    } else {
                        /* The bounce: past wave 22, in row 18 or above, a one-in-n roll sends the
                         * shot back down the screen instead of damaging anything. n is 70 less the
                         * wave with a floor of 20 - 1 in 47 the wave it starts, and not as short as
                         * 1 in 20 until wave 50. */
                        int n = 70 - wave;
                        if (n < 20) n = 20;
                        if (wave > 22 && row <= 18 && rnd(n) == 1) {
                            sfx(SND_RICOCHET);
                            Ent *r2 = ent_new(E_RICOCHET);
                            if (r2) {
                                r2->x = e->x; r2->y = e->y;
                                r2->vy = FIX(rnd(6) + 8);  /* [CreateRicochet]: 8 to 13 a frame */
                            }
                        } else {
                            shroom[col][row] = (uint8_t)(f + 1);
                        }
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
                    case E_SCORP:  hit = hit_box(e, 4, 12, t->x, t->y, 24, 24); break;
                    case E_GECKO:  hit = hit_box(e, 4, 12, t->x, t->y, 42, 20); break;
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

        /* [FleaMoveProc]. A flea with sideways speed is a bouncing one: half a pixel of gravity a
         * frame up to a terminal sixteen, a floor bounce two pixels a frame slower each time, and
         * it dies once the bounce would be under ten. One with no sideways speed just falls at a
         * steady rate and is gone at the floor. Either way it plants a mushroom in one row out of
         * four that it enters, and may suddenly drop at eight pixels a frame. */
        case E_FLEA: {
            if (e->vx) {
                e->vy += FIX(1) / 2;
                if (FIX_WHOLE(e->vy) > 16) e->vy = FIX(16);
            }
            e->y += e->vy; e->x += e->vx;
            int ix = FIX_WHOLE(e->x), iy = FIX_WHOLE(e->y);
            int row = iy / CELL, col = ix / CELL;
            if (e->data - 1 != row) {
                e->data = row + 1;                 /* sprite+0x58 remembers the row it is in */
                if (rnd(4) == 0 && row < 29) {
                    sfx(SND_SPLUT);
                    plant_shroom(col, row);
                    if (rnd(10) == 0 && !e->vx) e->vy = FIX(8);
                }
            }
            if (iy + 16 > FIELD_H) {
                if (!e->vx) { e->alive = false; break; }
                int sp = FIX_WHOLE(e->vy);
                if (sp >= 0 && sp < 10) { e->alive = false; break; }
                if (sp > 0) {
                    e->y = FIX(464);
                    e->vy = -FIX(sp - 2);
                    sfx(SND_BOING2);
                }
            }
            if (iy < 0) e->y = 0;
            if (ix < 0) { e->x = 0; e->vx = -e->vx; sfx(SND_BOING2); }
            if (ix > FIELD_W - 16) { e->x = FIX(FIELD_W - 16); e->vx = -e->vx; sfx(SND_BOING2); }
            break;
        }

        /* [ScorpianMoveProc]. Larry drifts up and down, and only moves sideways while a flag of his
         * own is set; every so often he flips his vertical direction and re-rolls the flag. He eats
         * - PluckShroom - whatever he is standing on, with one of the two munching sounds. */
        case E_SCORP: {
            if (rnd(score >= 5000 ? 30 : 18) == 1) {
                if (rnd(2) == 0) e->vy = -e->vy;
                e->data = rnd(4) == 0;                 /* sprite+0x58: "walk sideways too" */
            }
            if (e->data) e->x += e->vx;
            e->y += e->vy;
            int ix = FIX_WHOLE(e->x), iy = FIX_WHOLE(e->y);
            if (ix < 0 || ix + 24 >= FIELD_W) { e->alive = false; scorp_at = ticks + rnd(360) + 180; break; }
            if (iy < 0) { e->vy = -e->vy; e->y = 0; }
            if (iy + 24 > FIELD_H) { e->vy = -e->vy; e->y = FIX(FIELD_H - 24); }
            int col = clampi((FIX_WHOLE(e->x) + 12) / CELL, 0, GRID_W - 1);
            int row = clampi((FIX_WHOLE(e->y) + 12) / CELL, 0, GRID_H - 1);
            if (shroom[col][row]) {
                pluck_shroom(col, row);
                sfx(rnd(2) == 0 ? SND_MUNCH1 : SND_MUNCH2);
            }
            break;
        }

        /* [GeckoMoveProc]. Gordon walks one way across the field and poisons the first healthy
         * mushroom under him - frame > 4 becomes frame 1, which is what [PedeMoveProc] tests for -
         * then vanishes when he reaches the far side. */
        case E_GECKO: {
            e->x -= e->vx;
            int col = (FIX_WHOLE(e->x) + 10) / CELL, row = (FIX_WHOLE(e->y) + 10) / CELL;
            if (in_grid(col, row) && shroom[col][row] > 4) {
                shroom[col][row] = 1;
                sfx(SND_SPLUT);
            }
            /* [GeckoMoveProc] only hides him at the far side - he gets no new appointment, so
             * Gordon crosses at most once in a wave. */
            if (FIX_WHOLE(e->x) <= 0) e->alive = false;
            break;
        }

        /* [YummieMoveProc]: gravity of a pixel a frame per frame, a floor bounce a pixel slower
         * each time, and it is gone once the bounce drops below four. */
        case E_YUMMIE: {
            if (e->vx) e->vy += FIX(1);
            e->y += e->vy; e->x += e->vx;
            int ix = FIX_WHOLE(e->x), iy = FIX_WHOLE(e->y);
            if (iy + 16 >= FIELD_H) {
                e->y = FIX(FIELD_H - 16);
                int sp = FIX_WHOLE(e->vy);
                if (!e->vx || (sp >= 0 && sp < 4)) { e->alive = false; sfx(SND_MISSED_IT); break; }
                e->vy = -FIX(sp - 1);
                sfx(SND_BOING1);
            }
            if (iy < 0) { e->y = 0; if (e->vy < 0) e->vy = -e->vy; e->vx = FIX(1); }
            if (ix < 0 || ix > FIELD_W - 16) { e->alive = false; sfx(SND_MISSED_IT); }
            break;
        }

        /* [FodderMoveProc]: she blows up any mushroom she passes over, and once in ten frames picks
         * a new vertical drift out of {-3, 0, +3}. Leaving either side is a "Missed It". */
        case E_FODDER: {
            e->x += e->vx; e->y += e->vy;
            int ix = FIX_WHOLE(e->x), iy = FIX_WHOLE(e->y);
            int col = (ix + 10) / CELL, row = (iy + 10) / CELL;
            if (col < GRID_W - 1 && in_grid(col, row) && shroom[col][row]) {
                create_expl(FIX(col * CELL), FIX(row * CELL), EX_BIG);
                pluck_shroom(col, row);
                sfx(SND_EXPLOSION);
            }
            if (rnd(10) == 1) {
                int r = rnd(3);
                e->vy = r == 0 ? -FIX(3) : (r == 2 ? FIX(3) : 0);
            }
            if (ix + 20 > FIELD_W || ix < 0) {
                sfx(SND_MISSED_IT);
                e->alive = false;
                fodder_at = ticks + rnd(600) + 300;
                break;
            }
            if (iy < 0) { e->y = 0; e->vy = 0; }
            if (iy + 20 > FIELD_H) { e->y = FIX(FIELD_H - 20); e->vy = 0; }
            break;
        }

        /* [MultMoveProc] and [PsychMoveProc] are the same routine: both sit where they were put for
         * three hundred frames - ten seconds - and then give up with a "Missed It". */
        case E_COIN:
        case E_PSYC:
            if (++e->timer >= 300) { sfx(SND_MISSED_IT); e->alive = false; }
            break;

        /* [RootMoveProc]: it falls under the same gravity, bounces off the side walls rather than
         * reversing like a coin, and is simply gone when it reaches the floor. */
        case E_UPROOT: {
            e->vy += FIX(1);
            e->y += e->vy; e->x += e->vx;
            int ix = FIX_WHOLE(e->x), iy = FIX_WHOLE(e->y);
            if (iy > FIELD_H - 16) { e->alive = false; break; }
            if (iy < 0) e->y = 0;
            if (ix < 0) { e->x = 0; e->vx = -e->vx; }
            if (ix > FIELD_W - 16) { e->x = FIX(FIELD_W - 16); e->vx = -e->vx; }
            break;
        }

        /* An enhancement, not in the original: the power-up's icon flies from where the coin was
         * picked up over to its box on the score panel, in a shallow arc over twenty frames. */
        case E_FLYICON: {
            e->timer++;
            if (e->timer > FLYICON_FRAMES) {       /* arrived: let the box show it */
                if (e->hits == FLY_RESPAWN) {
                    player_dead = false;           /* the shooter is back on the field */
                    respawn_flying = false;
                    px = e->x; py = e->y;
                    { float a, b; mouse_delta(&a, &b); }   /* drop what the mouse did meanwhile */
                }
                e->alive = false;
                if (e->hits != FLY_RESPAWN) sfx(SND_HIT);
                break;
            }
            float t = (float)e->timer / FLYICON_FRAMES;
            fix_t tx, ty;
            switch (e->hits) {
            case FLY_MULT:  tx = FIX(PANEL_X - FIELD_X + 60); ty = FIX(382); break;
            case FLY_LIFE:  tx = FIX(lives_slot_x(lives) - FIELD_X); ty = FIX(LIVES_Y); break;
            case FLY_RESPAWN: tx = FIX(FIELD_W / 2 - 8); ty = FIX(SHOOTER_YMAX - 16); break;
            default:        tx = FIX(PANEL_X - FIELD_X + 60 + (e->data + 1) * 15);
                            ty = FIX(382); break;
            }
            e->x = e->vx + (fix_t)((tx - e->vx) * t);
            e->y = e->vy + (fix_t)((ty - e->vy) * t) - (fix_t)(FIX(28) * t * (1.0f - t) * 4);
            break;
        }

        /* [RicochetMoveProc]: straight down at its own speed until it is off the bottom. It goes
         * into DAT_1001db50 rather than the shot layer, so your own bounced shot can take you. */
        case E_RICOCHET:
            e->y += e->vy;
            if (FIX_WHOLE(e->y) + 12 >= FIELD_H) e->alive = false;
            break;

        /* [ScumMoveProc]: half a pixel of gravity a frame, and gone the moment it leaves the field */
        case E_SCUM:
            e->vy += FIX(1) / 2;
            e->x += e->vx; e->y += e->vy;
            if (FIX_WHOLE(e->x) < 0 || FIX_WHOLE(e->y) < 0 ||
                FIX_WHOLE(e->x) > FIELD_W - 4 || FIX_WHOLE(e->y) > FIELD_H - 4) e->alive = false;
            break;

        case E_EXPL: {          /* [ExploFrameProc]: one frame a frame, gone after the last */
            const Sprite *s = expl_sprite(e->data);
            e->frame++;
            if (e->frame > s->count) e->alive = false;
            break;
        }

        /* [PointsMoveProc]: the number drifts against its velocity and lasts exactly 20 frames */
        case E_POINTS:
            e->y -= e->vy; e->x -= e->vx;
            if (++e->timer >= 20) e->alive = false;
            if (FIX_WHOLE(e->y) < 0 || FIX_WHOLE(e->x) < 0 || FIX_WHOLE(e->x) > 424) e->alive = false;
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
    shot_delay = 1;             /* [NewGame]: DAT_1001daac = 0, and the gate is a strict > */

    if (flash_until && ticks > flash_until) { flash_until = 0; set_monitor_clut(A.game_clut, true); }
    if (psyc_until) {
        /* [GameLoop]: a swap every six ticks, and the whole thing lasts 900 ticks from the hit.
         * The music is off throughout and comes back with the normal palette at the end. */
        if (ticks - psyc_next > 5) {
            psyc_next = ticks;
            if (++psyc_phase > 2) psyc_phase = 0;
            if (enh[ENH_NO_PSYC]) psyc_phase = 2;      /* the flashing turned off: stay on normal */
            if (ticks > psyc_until) {
                psyc_until = 0; psyc_phase = 2; mult = 1;
                if (A.pref_music) music_start();
            }
            if (!enh[ENH_NO_PSYC]) psyc_apply();
        }
    }
    for (int i = 0; i < 5; i++) {
        if (powerup[i] <= 0) continue;
        powerup[i]--;
        /* [ShieldFrameProc] warns at 1620 of its 1800 ticks, and only if it is not already saying
         * so. The original has the timer on the shield alone; here every power-up gets the warning,
         * since you cannot see any of them counting down. */
        /* [ShieldFrameProc] tests `elapsed > 1620` every single frame and plays snd 168 whenever it
         * is not already playing, so over the last 180 ticks it is a rapid stutter rather than one
         * beep. snd 168 is 0.146s long, which at 30Hz retriggers it every four or five frames. */
        if (powerup[i] > 0 && powerup[i] <= POWERUP_WARN && !sfx_playing(SND_WARNING))
            sfx(SND_WARNING);
        if (powerup[i] == 0) sfx(SND_MISSED_IT);
    }
    /* the standing cheats top their power-up back up every frame, so they never run out */
    if (cheat[CH_NALA])    powerup[PW_SHIELD] = POWERUP_FRAMES;
    if (cheat[CH_NICEBOX]) powerup[PW_CURVE] = POWERUP_FRAMES;
    if (cheat[CH_SQUISH])  powerup[PW_MACHINEGUN] = POWERUP_FRAMES;
    if (cheat[CH_SNAPPLE]) powerup[PW_LOCK] = POWERUP_FRAMES;
    if (cheat[CH_DJARUM])  powerup[PW_FREEMOVE] = POWERUP_FRAMES;
    /* [GameHouseKeeping]'s machine-gun roll sets DAT_1001daa6 (how many shots may be in flight) to
     * 10 and DAT_1001daac (the gap between them) to 9, against the 1 and 0 that [NewGame] starts
     * with. [ShroomShotCheck] gates on `tick - DAT_1001daa8 > DAT_1001daac`, and that tick counter
     * advances once per Mac tick at 60.15Hz, so the gap is strictly more than nine ticks - about
     * six shots a second. The delays below are one greater than the binary's because the test here
     * is `<` rather than `>`. */
    shot_limit = powerup[PW_MACHINEGUN] ? 10 : 1;
    shot_delay = powerup[PW_MACHINEGUN] ? 10 : 1;

    if (psyc_at && ticks > psyc_at) { psyc_at = 0; create_psychadelic(); }
    if (mult_at && ticks > mult_at) { mult_at = 0; create_multiplier(); }
    if (fodder_at && ticks > fodder_at) { fodder_at = 0; create_fodder(); }
    /* Gordon's one appearance per wave, if the wave rolled him one at all: DAT_1001dabc, zeroed as
     * it fires, and only ever set on a wave whose mushroom colour is at least the fourth of its
     * group. This port had the two of them crossed - the dice below were driving Larry and
     * Gordon was on Larry's schedule, which is why the scobster hardly ever turned up (#25). */
    if (gecko_at && ticks > gecko_at) { gecko_at = 0; create_gecko(); }

    /* Larry is not scheduled by the wave at all. DAT_1001dacc is only ever written where he is
     * shot, as `now + FastRandom(0x168) + 0x168`, and nothing initialises it or clears it between
     * waves - so he is there from the first frame of a new game, and twelve to twenty-four seconds
     * after each time he is killed, for as long as there is no scobster on the field already. The
     * sound is his arrival being announced thirty ticks early, not the arrival itself. */
    bool scorp_out = false;
    for (int i = 0; i < MAX_ENT; i++)
        if (ent[i].alive && ent[i].type == E_SCORP) { scorp_out = true; break; }
    if (!scorp_out) {
        if (ticks > scorp_at - 30 && !scorp_warned) { scorp_warned = true; sfx(SND_SCORP_APPEARS); }
        if (ticks > scorp_at) { scorp_warned = false; create_scorpion(); }
    }

    /* [FleaDescentCheck]: a flea when the player's zone is running out of mushrooms. All four
     * conditions, in the order the original tests them:
     *     DAT_1001da88 < DAT_1001dab4   mushrooms in rows 18..29 under the score's threshold
     *     DAT_1001dab8 < DAT_1001da90   ten ticks since the last one was killed
     *     DAT_1001dab2 == 0             none already out
     *     DAT_1001da8a != 0             and never on a wave whose mushroom colour is the first
     * of its group - waves 1, 12, 23 and so on have no fleas at all, which this port was missing. */
    if (!player_dead && shrooms_in_player_zone() < flea_threshold &&
        ticks > flea_next && shroom_level != 0) {
        bool flea_out = false;
        for (int i = 0; i < MAX_ENT; i++) if (ent[i].alive && ent[i].type == E_FLEA) { flea_out = true; break; }
        if (!flea_out) { flea_chain = 200; create_flea(); }
    }

    /* [HandleSideFeed]. The original gates this on a flag; without it the loose heads pile up
     * faster than they can be shot and the wave can never end, so at most a few are out at once. */
    int loose = 0;
    for (int i = 0; i < MAX_ENT; i++)
        if (ent[i].alive && ent[i].type == E_PEDE && !ent[i].counts) loose++;
    if (pede_hit_bottom && loose < 3 && pede_left > 0 && ticks - sidefeed_last > sidefeed_period) {
        sidefeed_last = ticks;
        sidefeed_period -= 4;
        if (sidefeed_period < 0x1F) sidefeed_period = 0x1E;
        side_feed();
    }

    /* [GameLoop] walks the sprite list every frame and runs [ShotHitCheck] on the 'shot' sprites -
     * and on the 'shel' one, the shield, with the flag that says it is not consumed by the hit. So
     * the sprinkly shield really is "quite lethal as well": anything it touches is shot. */
    if (!player_dead && powerup[PW_SHIELD]) {
        fix_t sx = px, sy = py - FIX(5);
        for (int i = 0; i < MAX_ENT; i++) {
            Ent *t = &ent[i];
            if (!t->alive) continue;
            int w = 16, h = 16;
            switch (t->type) {
            case E_SCORP: w = 24; h = 24; break;
            case E_GECKO: w = 42; h = 20; break;
            case E_YUMMIE: w = 14; h = 14; break;
            case E_FODDER: w = 20; h = 20; break;
            case E_POINTS: case E_SCUM: case E_EXPL: case E_SHOT: case E_FLYICON:
            case E_RICOCHET: continue;
            default: break;
            }
            if (!hit_box(t, w, h, sx, sy, 16, 16)) continue;
            shot_hits(t);
        }
        int c0 = (FIX_WHOLE(px)) / CELL, r0 = (FIX_WHOLE(py) - 5) / CELL;
        for (int c = c0; c <= c0 + 1; c++)
            for (int r = r0; r <= r0 + 1; r++)
                if (in_grid(c, r) && shroom[c][r]) {
                    create_scum(FIX(c * CELL), FIX(r * CELL));
                    sfx(SND_HIT);
                    if (shroom[c][r] % 4 == 0) {
                        long v = mult * (shroom[c][r] < 5 ? 5 : 1);
                        create_points(FIX(c * CELL), FIX(r * CELL), (int)v, false);
                        pluck_shroom(c, r);
                        add_score(v);
                    } else {
                        shroom[c][r]++;
                    }
                }
    }

    if (player_dead) {
        if (respawn_flying) return;             /* the replacement is in the air */
        if (ticks - death_tick > RESPAWN_TICKS) {
            /* [GameLoop]'s respawn runs CountOffShrooms(0x91) - "Oh no" first, waited out, then
             * the mushrooms restored one at a time - so nothing overlaps the death sound. */
            count_off_shrooms(SND_OH_NO, false);
            lives--;
            mult = 1; psyc_until = 0; psyc_phase = 2; set_monitor_clut(A.game_clut, true);
            if (!powerup[PW_LOCK]) memset(powerup, 0, sizeof powerup);  /* unless the Yummielock is up */
            respawn_flying = false;
            px = FIX(FIELD_W / 2 - 8);
            py = FIX(FIELD_H - 32);
            clear_spawn_area();
            if (lives >= 0) {
                start_level();                  /* [GameHouseKeeping] rebuilds the wave */
                if (A.pref_music) music_start();
            }
            /* Enhancement: rather than appearing, the next shooter leaves the Lives box and flies
             * down to its starting place, and the box is one short until it gets there. */
            /* lives has already come down, so zero still means one shooter left to put out - the
             * rightmost slot is the one it leaves from. Only -1, which is game over, has none. */
            if (enh[ENH_WEAPON_FLY] && lives >= 0 &&
                /* It leaves from the slot the death just put out - lives has already come down by
                 * one, so that is slot number lives+1 - and it hides nothing on the way: the box is
                 * already showing the new count, and darkening a second slot would be a life short
                 * until it landed. */
                fly_icon(FLY_RESPAWN, 0, FIX(lives_slot_x(lives + 1) - FIELD_X),
                         FIX(LIVES_Y), false)) {
                respawn_flying = true;
            } else {
                player_dead = false;
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
        case E_SCORP: w = 24; h = 24; break;
        case E_GECKO: w = 42; h = 20; break;
        case E_YUMMIE: w = 14; h = 14; break;
        case E_FODDER: w = 20; h = 20; break;
        default: break;
        }
        bool touching = hit_box(e, w, h, px, py, 16, 16);
        if (!touching) continue;
        if (e->type == E_YUMMIE) {
            take_yummie(e);
            continue;
        }
        /* [GameHouseKeeping] tests the shooter against two layers. DAT_1001db48 holds 'phed' and
         * 'pbod' and kills outright. DAT_1001db50 holds everything else that moves, and its handler
         * reads the tag twice: 'yumy' is the pickup above, and then at 0x10003e0c anything that is
         * NOT 'psyc' falls into a second copy of the death path. So the flea, the fodder, Larry,
         * Gordon, an uprooted mushroom and a multiplier tile all take a shooter; only the coin and
         * the psychedelic mushroom are safe to touch. */
        if (e->type == E_PSYC) continue;
        if (e->type == E_POINTS || e->type == E_SCUM || e->type == E_EXPL ||
            e->type == E_SHOT || e->type == E_FLYICON) continue;   /* not in either layer */
        if (e->type == E_RICOCHET) {            /* a thin thing coming back down at you */
            if (!hit_box(e, 4, 12, px, py, 16, 16)) continue;
            player_die();
            break;
        }
        player_die();
        break;
    }
}

/* The pickup itself, split out of the collision pass so the end-of-wave tally can run it too: a
 * coin that reaches the shooter while the bonus is counting off used to fall straight through it
 * (#26). The original does not check for it there - [TallyBonus] keeps animating and compositing
 * but never calls [GameHouseKeeping] - so this is on the Enhancements page. */
static void take_yummie(Ent *e)
{
    /* [GameHouseKeeping] runs FastRandom(7) and jumps through a table of seven: the five
     * power-ups, an extra shooter, and WasteShrooms. "Got Yummie" plays for all but the
     * extra shooter, which has a sound of its own. */
    e->alive = false;
    int roll = rnd(7);
    if (roll != 2) sfx(SND_GOT_YUMMIE);
    int slot = -1, box = -1;
    switch (roll) {
    case 0: slot = PW_CURVE; break;
    case 1: slot = PW_FREEMOVE; break;
    case 2:                                   /* an extra shooter, capped at eight */
        if (lives < 8) { lives++; sfx(SND_EXTRA_SHIP); box = FLY_LIFE; }
        else sfx(SND_MISSED_IT);
        break;
    case 3: slot = PW_MACHINEGUN; break;
    case 4: slot = PW_SHIELD; break;          /* [MakeInvincible] */
    case 5: waste_shrooms(); break;           /* [WasteShrooms] */
    default: slot = PW_LOCK; break;
    }
    if (slot >= 0) box = FLY_WEAPON;
    /* the icon arcs over to whichever box it belongs in, and only lands in that box when
     * it gets there - unless one of the same kind was already running */
    if (enh[ENH_WEAPON_FLY] && box >= 0) {
        /* the coin's slot is free now, so ent_new can hand back this very entity with its
         * position already wiped - read where it was before asking for a new one */
        fix_t fx = e->x, fy = e->y;
        /* a sixth life changes nothing in a box that only shows five */
        bool room = box != FLY_LIFE || lives <= 8;
        bool hides = box != FLY_WEAPON || powerup[slot] <= 0;
        if (room) fly_icon(box, slot, fx, fy, hides);
    }
    if (slot >= 0) powerup[slot] = POWERUP_FRAMES;
}

/* The coins that are still falling when the wave ends. Only the pickup runs here - nothing in the
 * tally can take a shooter, which is why this is not the whole collision pass. */
static void tally_pickups(void)
{
    if (!enh[ENH_TALLY_PICKUP] || player_dead) return;
    for (int i = 0; i < MAX_ENT; i++) {
        Ent *e = &ent[i];
        if (!e->alive || e->type != E_YUMMIE) continue;
        if (hit_box(e, 14, 14, px, py, 16, 16)) take_yummie(e);
    }
}

/* The demo's hand on the mouse. It picks a point to be at and gives back the pixel movement
 * towards it, with a little momentum, so the shooter drifts and overshoots the way a hand does
 * rather than stepping along the axes. */
static void demo_move(float *mx, float *my)
{
    static float vx, vy;
    static float reach_sx, reach_sy, reach_gx, reach_gy, reach_t, reach_dur = 1.0f;
    static float wander_y;
    static long  reach_hold, wander_at;
    int ix = FIX_WHOLE(px), iy = FIX_WHOLE(py);
    float wantx = (float)ix, wanty = (float)(SHOOTER_YMAX - 8);
    int best = 1 << 30, coin = 1 << 30, flee = 0;

    for (int i = 0; i < MAX_ENT; i++) {
        Ent *e = &ent[i];
        if (!e->alive) continue;
        int ex = FIX_WHOLE(e->x), ey = FIX_WHOLE(e->y);
        int dx = ex - ix, dy = ey - iy;
        switch (e->type) {
        case E_YUMMIE:                        /* the one thing worth walking into */
            if (abs(dx) + abs(dy) < coin) {
                coin = abs(dx) + abs(dy);
                wantx = (float)ex;
                wanty = (float)(ey < SHOOTER_TOP ? SHOOTER_TOP : ey);
            }
            break;
        case E_PEDE: case E_FLEA: case E_SCORP: case E_GECKO: case E_FODDER: case E_COIN:
            if (abs(dx) < 24 && dy > -64 && dy < 24) flee = dx < 0 ? 1 : -1;
            if (dy > 16) break;               /* below us: nothing to aim at */
            if (coin == 1 << 30 && abs(dx) < best) { best = abs(dx); wantx = (float)ex; }
            break;
        default: break;
        }
    }
    if (flee) wantx = (float)ix + flee * 70.0f;

    /* Nobody parks on the bottom edge for a whole wave. With nothing to chase, the hand wanders
     * slowly through the band the shooter is allowed, which is 96 pixels tall. */
    if (coin == 1 << 30 && !flee) {
        if (ticks > wander_at) {
            wander_at = ticks + 90 + rnd(150);
            wander_y = (float)(SHOOTER_TOP + rnd(SHOOTER_YMAX - SHOOTER_TOP));
        }
        wanty = wander_y;
    }
    if (wantx < 0) wantx = 0;
    if (wantx > FIELD_W - 16) wantx = FIELD_W - 16;

    /* A hand on a mouse does not track a target continuously - it flicks, stops, corrects, and
     * sits still between moves, and the flick itself starts slow, runs fast and eases out. So the
     * motion is a reach: a new one starts when the target has moved away from where this one was
     * going, or when the dwell after the last one has run out, and it lasts longer the further it
     * has to go. Inside a reach the speed follows the derivative of a smoothstep, which is what
     * gives the ease at both ends; between reaches there is only a little jitter, because a hand
     * resting on a mouse is never quite still. The spring this replaces moved all the time and at
     * a speed that only depended on the distance left, which is what read as mechanical (#26). */
    float distx = wantx - reach_gx, disty = wanty - reach_gy;
    bool stale = distx * distx + disty * disty > 20.0f * 20.0f;
    if (reach_t >= reach_dur && (stale || ticks > reach_hold)) {
        reach_sx = (float)ix; reach_sy = (float)iy;
        reach_gx = wantx;     reach_gy = wanty;
        float d = fabsf(wantx - ix) + fabsf(wanty - iy);
        reach_dur = 4.0f + d * 0.055f;                 /* further is slower, as a hand is */
        if (flee) reach_dur *= 0.45f;                  /* except when getting out of the way */
        reach_t = 0.0f;
        reach_hold = ticks + 3 + rnd(flee ? 2 : 14);   /* the pause after arriving */
    } else if (stale && flee) {
        reach_gx = wantx; reach_gy = wanty;            /* a dodge redirects a reach in flight */
    }

    float was_x = reach_sx, was_y = reach_sy, prev = reach_t / reach_dur;
    reach_t += 1.0f;
    if (reach_t > reach_dur) reach_t = reach_dur;
    float now = reach_t / reach_dur;
    /* smoothstep between the two ends, differenced to get this frame's movement */
    float p0 = prev * prev * (3.0f - 2.0f * prev), p1 = now * now * (3.0f - 2.0f * now);
    vx = (reach_gx - was_x) * (p1 - p0);
    vy = (reach_gy - was_y) * (p1 - p0);
    if (reach_t >= reach_dur) {                        /* resting: just the tremor */
        vx += (float)(rnd(3) - 1) * 0.5f;
        vy += (float)(rnd(3) - 1) * 0.4f;
    }
    *mx = vx; *my = vy;
}

/* [HandleInput] plus [ShooterMoveProc]: where the shooter goes this frame. It is split out
 * because [CountOffShrooms] and [TallyBonus] call [AnimateFrame] too, so the shooter still follows
 * the mouse through the end-of-wave count-up - but not [GameHouseKeeping], which is what makes the
 * shots, so you cannot fire until the next wave starts. */
static void move_player(void)
{
        fix_t was_x = px;
        int dx = 0, dy = 0;
        float padx = 0, pady = 0;
        bool padding = pad_move(&padx, &pady);    /* a controller, if one is plugged in */
        if (demo_mode) {
            padding = false;
        } else if (opt_keyboard) {
            if (key_down(SDL_SCANCODE_LEFT) || key_down(SDL_SCANCODE_A)) dx -= 1;
            if (key_down(SDL_SCANCODE_RIGHT) || key_down(SDL_SCANCODE_D)) dx += 1;
            if (key_down(SDL_SCANCODE_UP) || key_down(SDL_SCANCODE_W)) dy -= 1;
            if (key_down(SDL_SCANCODE_DOWN) || key_down(SDL_SCANCODE_S)) dy += 1;
        }
        int sp = player_speed / 2;
        bool ghost = powerup[PW_FREEMOVE] != 0;      /* "pass through the shrooms" */
        if (dx) { fix_t nx = px + FIX(sp) * dx; if (ghost || !player_blocked(nx, py)) px = nx; }
        if (dy) { fix_t ny = py + FIX(sp) * dy; if (ghost || !player_blocked(px, ny)) py = ny; }

        /* a controller's stick moves at the full player speed, scaled by how far it is pushed */
        if (padding && dx == 0 && dy == 0) {
            fix_t nx = px + (fix_t)(padx * FIX(player_speed));
            fix_t ny = py + (fix_t)(pady * FIX(player_speed));
            if (ghost || !player_blocked(nx, py)) px = nx;
            if (ghost || !player_blocked(px, ny)) py = ny;
        }

        /* The mouse is read as movement, not position: the pointer is captured for the duration
         * of a game, so there is no field edge for it to get stuck against. */
        float rdx = 0, rdy = 0;
        if (demo_mode) demo_move(&rdx, &rdy);      /* the demo steers through the mouse path */
        else if (opt_mouse) mouse_delta(&rdx, &rdy);
        if ((rdx != 0 || rdy != 0) && dx == 0 && dy == 0 && !padding) {
            fix_t lim = FIX(player_speed);
            fix_t mvx = (fix_t)(rdx * 65536.0f), mvy = (fix_t)(rdy * 65536.0f);
            if (mvx > lim) mvx = lim;
            if (mvx < -lim) mvx = -lim;
            if (mvy > lim) mvy = lim;
            if (mvy < -lim) mvy = -lim;
            fix_t tx = px + mvx, ty = py + mvy;
            if (ghost || !player_blocked(tx, py)) px = tx;
            if (ghost || !player_blocked(px, ty)) py = ty;
        }
        /* [ShooterMoveProc]: frame += dx/2, wrapped round the strip. dx is the whole-pixel
         * movement this frame, which it clamps to +/-13 before halving. */
        {
            int moved = FIX_WHOLE(px) - FIX_WHOLE(was_x);
            if (moved > 13) moved = 13;
            if (moved < -13) moved = -13;
            shooter_frame += moved / 2;
            while (shooter_frame < 1) shooter_frame += A.shooter.count;
            while (shooter_frame > A.shooter.count) shooter_frame -= A.shooter.count;
        }
        if (px < 0) px = 0;
        if (px > FIX(FIELD_W - 16)) px = FIX(FIELD_W - 16);
        if (py < FIX(SHOOTER_TOP)) py = FIX(SHOOTER_TOP);
        if (py > FIX(SHOOTER_YMAX)) py = FIX(SHOOTER_YMAX);
}

/* [CountOffShrooms] / [TallyBonus]: the end-of-wave count-up */
/* One frame of whatever is on screen while a count-off or the tally runs: the original keeps
 * calling [AnimateFrame] and [CompositeFrame] through both, so the creatures keep moving. */
static void idle_frame(void)
{
    ticks += TICKS_PER_FRAME;
    player_anim++; pede_anim++;
    pump();
    if (!player_dead) move_player();
    else { float a, b; mouse_delta(&a, &b); }   /* keep the deltas from piling up */
    update_entities();
    tally_pickups();
    draw_frame();
    set_entries();
    if (key_down(SDL_SCANCODE_ESCAPE)) quit_to_title = true;
    SDL_Delay((int)(2 * MAC_TICK_MS));
}

/* [CountOffShrooms](sound). It plays the sound it is given and waits it out, then walks the
 * mushrooms: every one that is not already a full-health shroom of this wave's colour is restored
 * - or, at the end of a wave, a poisoned one (frame under 5) is set back to frame 1 - with a
 * splash, snd 131 "Shroom Pop" and five points times the multiplier, seven ticks apart.
 * [GameLoop] runs it with snd 144 "Level Clear" when a wave is cleared and snd 145 "Oh no" when
 * the shooter is lost. */
static void count_off_shrooms(int snd, bool wave_clear)
{
    (void)wave_clear;
    music_halt();
    sfx(snd);
    while (sfx_playing(snd) && !quit_to_title) idle_frame();

    uint8_t full = (uint8_t)((shroom_level + 1) * 4 + 1);
    for (int c = 0; c < GRID_W && !quit_to_title; c++)
        for (int r = 0; r < GRID_H && !quit_to_title; r++) {
            if (!shroom[c][r] || shroom[c][r] == full) continue;
            /* the original leaves a poisoned one poisoned at the end of a wave (frame < 5 goes
             * back to 1); here it is cleaned up with the rest, which is what you would expect */
            shroom[c][r] = full;
            create_scum(FIX(c * CELL), FIX(r * CELL));
            sfx(SND_SHROOM_POP);
            add_score(5L * mult);
            create_points(FIX(c * CELL), FIX(r * CELL), 5 * mult, false);
            idle_frame();            /* the original waits seven ticks on each; this is two */
        }
}

/* [TallyBonus] is a little state machine: the multiplier is cashed in first (snd 154, then snd 151
 * once it has finished, which is when the bonus is multiplied), then the bonus is counted down
 * ninety points at a time on snd 159 "Bonk", and snd 156 "myHooHa" closes it out. */
static void tally_bonus(void)
{
    if (mult > 1) {
        sfx(SND_MULT_APPEARS);
        while (sfx_playing(SND_MULT_APPEARS) && !quit_to_title) idle_frame();
        sfx(SND_MOAN);
        level_bonus *= mult;
        while (sfx_playing(SND_MOAN) && !quit_to_title) idle_frame();
        mult = 1;
    }
    while (level_bonus > 0 && !quit_to_title) {
        level_bonus -= 90;
        if (level_bonus < 0) level_bonus = 0;
        add_score(90);
        sfx(SND_BONK);
        idle_frame();
    }
    sfx(SND_MYHOOHA);
    while (sfx_playing(SND_MYHOOHA) && !quit_to_title) idle_frame();
    level_bonus = 2000 + (cheat[CH_USMC] ? 5000 : 0);   /* USMC: "adds 5,000 points to the bonus" */
}

/* ---- pause ------------------------------------------------------------------------------------
 * [PauseGame]: the music stops, snd 139 ("Pause") plays, the mode goes to 3 and the game sits there
 * until Caps Lock is up *and* the game window is at the front again. [PauseUpdate] paints the window
 * out and draws PICT 3130 - "PAUSED (Hit the Caps Lock key to continue)" - centred across the window
 * and a third of the way down it.
 */
static void pause_game(void)
{
    bool music_was_on = A.pref_music != 0;
    if (music_was_on) music_halt();
    sfx(SND_PAUSE);
    flush_clicks();
    mouse_relative(false);                   /* give the pointer back while the game is held */
    cursor_set(CURSOR_HAND);                 /* [PauseGame] calls InitCursor */

    /* [PauseUpdate] paints the whole game window out and puts the picture up on its own, so the
     * rings that frame the playfield go with it until the game comes back. */
    set_border_rings(NULL, false);
    memset(fb, find_color(0, 0, 0), sizeof fb);
    draw_image(&A.paused, (SCREEN_W - A.paused.w) / 2, (SCREEN_H - A.paused.h) / 3);
    set_entries();

    while (caps_lock_down() || pad_start()) {
        if (key_down(SDL_SCANCODE_ESCAPE)) { quit_to_title = true; break; }
        pump();
        present();
        SDL_Delay(16);
    }

    flush_clicks();
    cursor_set(CURSOR_NONE);
    mouse_relative(true);
    draw_frame();
    set_entries();
    if (music_was_on) music_start();
}

/* ---- attract mode ------------------------------------------------------------------------------
 * There is no demo in the original: nothing in the binary plays the game by itself, and the title
 * screen waits for a click for as long as you leave it. This is an invention, so it is kept to
 * what someone watching an arcade cabinet would see - the game playing itself until a key or the
 * mouse button takes over. The shooter is steered by the plainest rules that still look human:
 * line up under whatever is worth shooting, get out from under anything about to land, go and
 * fetch a coin, and hold the trigger down.
 */
bool demo_mode;
static Uint64 demo_start;      
/* ---- the main game loop ------------------------------------------------------------------------ */
/* [GameLoop]: returns true when the player got far enough for the special ending. */
static bool game_loop(void)
{
    Uint64 next = SDL_GetTicks();
    for (;;) {
        pump();
        if (key_down(SDL_SCANCODE_ESCAPE)) { quit_to_title = true; return false; }
        if (demo_mode) {                     /* anything at all hands the game back */
            char k;
            bool pressed = false;
            while (take_key(&k)) pressed = true;
            if (pressed || take_click_only() || key_down(SDL_SCANCODE_RETURN) ||
                key_down(SDL_SCANCODE_SPACE)) { quit_to_title = true; return false; }
            if (SDL_GetTicks() - demo_start > 75000) { quit_to_title = true; return false; }
        }

        Uint64 now = SDL_GetTicks();
        if (now < next) { SDL_Delay(1); continue; }
        next += (Uint64)(TICKS_PER_FRAME * MAC_TICK_MS + 0.5);
        if (now > next + 200) next = now;    /* do not try to catch up after a long stall */

        /* [GameLoop] bumps the counter once per tick change and [GameHouseKeeping] once per frame,
         * so it advances by two a frame - which keeps the tick-based timers in real 60ths. */
        /* [GameHouseKeeping] checks [CapsLockDown] every frame and calls [PauseGame] on it. */
        if (caps_lock_down() || pad_start()) {
            pause_game();
            if (quit_to_title) return false;
            next = SDL_GetTicks();
            continue;
        }

        ticks += TICKS_PER_FRAME;
        player_anim++; pede_anim++;
        music_pump();

        /* [HandleInput]: the keyboard, plus the mouse when it is inside the field */
        if (player_dead) { float a, b; mouse_delta(&a, &b); }   /* nothing to apply it to yet */
        if (!player_dead) {
            move_player();

            /* the Debug page's spawn keys */
            if (debug_spawn) {
                static int last_spawn;
                int want = 0;
                if (key_down(SDL_SCANCODE_F1)) want = 1;
                else if (key_down(SDL_SCANCODE_F2)) want = 2;
                else if (key_down(SDL_SCANCODE_F3)) want = 3;
                else if (key_down(SDL_SCANCODE_F4)) want = 4;
                else if (key_down(SDL_SCANCODE_F5)) want = 5;
                else if (key_down(SDL_SCANCODE_F6)) want = 6;
                else if (key_down(SDL_SCANCODE_F7)) want = 7;
                else if (key_down(SDL_SCANCODE_F8)) want = 8;
                if (want && want != last_spawn) {
                    int col = rnd(GRID_W), row = rnd(6);
                    switch (want) {
                    case 1: create_flea(); break;
                    case 2: create_scorpion(); break;
                    case 3: create_gecko(); break;
                    case 4: create_fodder(); break;
                    case 5: create_yummie(FIX(col * CELL), FIX(row * CELL)); break;
                    case 6: create_multiplier(); break;
                    case 7: create_psychadelic(); break;
                    case 8: create_uproot(col, row); break;
                    }
                }
                last_spawn = want;
            }

            /* MORRISON and NUKE EM are momentary in the original, typed into the paused game;
             * with them switched on here they are a keypress while playing. */
            if (cheat[CH_MORRISON] && key_down(SDL_SCANCODE_M)) {
                for (int i = 0; i < MAX_ENT; i++)
                    if (ent[i].alive && ent[i].type == E_PEDE) ent[i].alive = false;
                pede_left = 0;
            }
            if (cheat[CH_NUKEEM] && key_down(SDL_SCANCODE_N)) {
                int c0 = FIX_WHOLE(px) / CELL, r0 = FIX_WHOLE(py) / CELL;
                for (int c = c0 - 4; c <= c0 + 4; c++)
                    for (int r = r0 - 4; r <= r0 + 4; r++)
                        if (in_grid(c, r) && shroom[c][r]) {
                            create_scum(FIX(c * CELL), FIX(r * CELL));
                            pluck_shroom(c, r);
                        }
                sfx(SND_EXPLOSION);
            }
            if (demo_mode || pad_fire() || key_down(SDL_SCANCODE_SPACE) ||
                key_down(SDL_SCANCODE_LCTRL) || key_down(SDL_SCANCODE_RCTRL) ||
                (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK))
                fire_shot();
        }

        house_keeping();
        update_entities();

        if (lives < 0 && !player_dead) return wave > 15;

        if (pede_left <= 0) {                /* the wave is cleared */
            ent_clear(E_PEDE);
            count_off_shrooms(SND_LEVEL_CLEAR, true);   /* [GameLoop]: CountOffShrooms(0x90) */
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
    if (take_click_only()) { *cut = true; sfx_halt_all(); }   /* [GameOver] polls Button() */
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
/* [EnterHighScore] puts up DLOG 161 (349x116, centred) with DITL 161's items: cicn 128 at (15,15),
 * the two-line prompt at (56,15), the name field at (61,58)-(318,74) pre-filled with the last name
 * and selected, and OK at (276,91) as the default item. Accepting plays snd 144 and waits it out.
 * The field and button colours are the ones the original shows on screen. */
static void enter_high_score(int slot, const Image *bg)
{
    uint8_t white = find_color(0xFFFF, 0xFFFF, 0xFFFF);
    uint8_t black = find_color(0, 0, 0);
    uint8_t field = find_color(217 << 8, 239 << 8, 178 << 8);   /* the pale green of the edit box */
    uint8_t btn   = find_color(201 << 8, 236 << 8, 226 << 8);   /* and the pale teal of the button */

    for (int i = 9; i > slot; i--) A.score_row[i] = A.score_row[i - 1];
    A.score_row[slot].score = score;
    A.score_row[slot].wave = wave;

    char name[32];
    snprintf(name, sizeof name, "%.24s", A.score_row[slot].name);
    int n = (int)strlen(name);
    bool selected = true;          /* SelectDialogItemText: the first keystroke replaces it */

    const int BW = 349, BH = 116;
    int bx = (SCREEN_W - BW) / 2, by = (SCREEN_H - BH) / 2;
    const int OKX = bx + 276, OKY = by + 91, OKW = 68;

    SDL_StartTextInput(SDL_GetKeyboardFocus());
    cursor_set(CURSOR_HAND);
    flush_clicks();
    bool done = false;
    while (!done && !quit_to_title) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) plat_shutdown(0);
            if (e.type == SDL_EVENT_TEXT_INPUT) {
                if (selected) { n = 0; name[0] = 0; selected = false; }
                for (const char *p = e.text.text; *p && n < 24; p++)
                    if ((unsigned char)*p >= 32) name[n++] = *p;
                name[n] = 0;
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                float mx = 0, my = 0;
                SDL_GetMouseState(&mx, &my);
                int gx, gy;
                mouse_pos(&gx, &gy);
                if (gx >= OKX - 4 && gx < OKX + OKW + 4 && gy >= OKY - 4 && gy < OKY + 28) {
                    dialog_ok_press(OKX, OKY, OKW, btn, black);
                    done = true;
                }
                selected = false;
                (void)mx; (void)my;
            }
            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_BACKSPACE) {
                    if (selected) { n = 0; name[0] = 0; selected = false; }
                    else if (n > 0) name[--n] = 0;
                }
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                    dialog_ok_press(OKX, OKY, OKW, btn, black);
                    done = true;
                }
                if (e.key.key == SDLK_ESCAPE) { done = true; quit_to_title = true; }
            }
        }
        draw_image(bg, (SCREEN_W - bg->w) / 2, (SCREEN_H - bg->h) / 2);
        dialog_frame(bx, by, BW, BH, white, black);
        draw_image_remap(&A.dialog_icon, bx + 15, by + 15, true);

        draw_text(&A.chicago12, bx + 56, by + 27, "Congratulations, you got a high score!!!", black, 0);
        draw_text(&A.chicago12, bx + 56, by + 43, "Enter your name for the hall of fame:", black, 0);

        fill_rect(bx + 60, by + 57, 259, 18, black);          /* the field's one-pixel frame */
        fill_rect(bx + 61, by + 58, 257, 16, field);
        int tw = text_width(&A.chicago12, name, 0);
        if (selected && n) {                                   /* SelectDialogItemText's highlight */
            fill_rect(bx + 64, by + 59, tw + 1, 14, black);
            draw_text(&A.chicago12, bx + 65, by + 70, name, field, 0);
        } else {
            draw_text(&A.chicago12, bx + 65, by + 70, name, black, 0);
            if ((SDL_GetTicks() / 400) % 2)
                fill_rect(bx + 65 + tw, by + 60, 1, 12, black);
        }

        dialog_ok(OKX, OKY, OKW, btn, black);
        set_entries();
        present();
        SDL_Delay(16);
    }
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
    while (sfx_playing(SND_TRANSITION)) { pump(); present(); SDL_Delay(2); }

    cursor_set(CURSOR_HAND);
    mouse_relative(false);                       /* the high-score dialog needs the pointer back */
    /* The original swaps in PICT 667 past wave 15 - the shareware notice asking to be paid for -
     * which is no use in a port of the full game, so the caterpillar runs however the game ended. */
    const Image *bg = &A.gameover_logo;
    (void)finished;
    sfx(150);                                    /* "Hooka Music" */
    set_monitor_clut(bg->pal, false);
    memset(fb, find_color(0, 0, 0), sizeof fb);
    draw_image(bg, (SCREEN_W - bg->w) / 2, (SCREEN_H - bg->h) / 2);
    set_clut_to_color(RGB_BLACK, false);
    fade_to(bg->pal, 60);

    bool cut = false;
    flush_clicks();
    go_wait(60, &cut);
    for (int f = 0; f < 8; f++) {
        go_wait(8, &cut);
        const Image *im = &A.gameover_anim[f];
        draw_image(im, 225, 99);                 /* [GameOver] places each frame at (225, 99) */
        set_entries();
    }
    while (sfx_playing(150) && !cut) { go_button(&cut); present(); SDL_Delay(8); }

    /* "Using a cheat code will forfeit your chance for a high score, except when you use PERNTS
     * and SCUMDOG" - the shipped Cheats file. */
    int slot = -1;
    if (!cheated || cheat[CH_SCUMDOG])
        for (int i = 0; i < 10; i++) if (score > A.score_row[i].score) { slot = i; break; }
    if (slot >= 0) {
        enter_high_score(slot, bg);
    } else {
        sfx(SND_MISSED_IT);                      /* and let it finish before anything else starts */
        while (sfx_playing(SND_MISSED_IT)) { pump(); present(); SDL_Delay(8); }
    }

    sfx(SND_TRANSITION);                         /* FunkyFade back to the title screen */
    hue_scale_grey();
    delay_ticks(5);
    fade_to_color(20, RGB_WHITE);
    fade_to_color(20, RGB_BLACK);
    /* [FunkyFade] waits the transition sound out; cutting back to the title before it has finished
     * is what was clipping the last sound of the game-over sequence. */
    while (sfx_playing(SND_TRANSITION)) { pump(); present(); SDL_Delay(2); }
    set_monitor_clut(A.title_clut, false);
}

/* The same set-up as a game, played by [demo_move] and thrown away at the end: no score is kept,
 * no high score is offered, and the game-over sequence is skipped. */
void run_attract(void)
{
    bool saved_cheat[CH_COUNT];
    memcpy(saved_cheat, cheat, sizeof cheat);
    memset(cheat, 0, sizeof cheat);          /* the demo plays the plain game */

    demo_mode = true;
    demo_start = SDL_GetTicks();
    quit_to_title = false;
    score = 0; next_extra = 20000;
    lives = 2; wave = 1; mult = 1;
    cheated = false;
    level_bonus = 2000;
    player_speed = 0x12;
    shot_limit = 1; shot_delay = 1;
    psyc_until = 0; psyc_phase = 2; psyc_next = 0;
    flash_until = 0;
    memset(powerup, 0, sizeof powerup);
    respawn_flying = false;
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
    cursor_set(CURSOR_NONE);
    draw_frame();
    set_clut_to_color(RGB_BLACK, false);
    fade_to(A.game_clut, 20);
    if (A.pref_music) music_start();

    game_loop();

    demo_mode = false;
    memcpy(cheat, saved_cheat, sizeof cheat);
    music_halt();
    sfx_halt_all();
    fade_to_color(15, RGB_BLACK);
    cursor_set(CURSOR_HAND);
}

void new_game(void)                          /* [NewGame] */
{
    scorp_at = 0;                            /* DAT_1001dacc: Larry is due from the first frame */
    scorp_warned = false;
    quit_to_title = false;
    score = 0; next_extra = 20000;
    lives = 3; mult = 1;               /* [NewGame]: DAT_1001da70 = 3, plus the one on the field */
    wave = opt_start_wave < 1 ? 1 : opt_start_wave;   /* the Enhancements page's starting wave */
    /* the cheats the options screen turned on, applied the way the Cheats file describes them */
    cheated = wave > 1;            /* starting past the first wave forfeits the score */
    for (int i = 0; i < CH_COUNT; i++)
        if (cheat[i] && i != CH_PERNTS && i != CH_SCUMDOG) cheated = true;
    if (cheat[CH_HECTOR]) lives = 8;
    if (cheat[CH_PERNTS]) score = 50000;
    level_bonus = 2000 + (cheat[CH_USMC] ? 5000 : 0);   /* USMC: "adds 5,000 points to the bonus" */
    player_speed = 0x12;
    shot_limit = 1; shot_delay = 1;
    psyc_until = 0; psyc_phase = 2; psyc_next = 0;
    flash_until = 0;
    memset(powerup, 0, sizeof powerup);
    respawn_flying = false;
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
    cursor_set(CURSOR_NONE);                 /* the pointer steers the shooter; it is not drawn */
    mouse_relative(true);
    draw_frame();
    set_clut_to_color(RGB_BLACK, false);
    fade_to(A.game_clut, 30);
    if (A.pref_music) music_start();

    bool finished = game_loop();
    game_over_sequence(finished);
    cursor_set(CURSOR_HAND);
    mouse_relative(false);

    music_halt();
    sfx_halt_all();
}
