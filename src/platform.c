/*
 * platform.c - the 8-bit indexed screen, the Mac colour-table operations, sprite/font/image drawing
 * and sound. The original draws into an 8-bit window and does its fades and flashes by rewriting the
 * monitor's colour table (SetEntries); this keeps that model, converting the indexed framebuffer to
 * ARGB only at present() time.
 */
#include <math.h>
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
static float last_scale = 1.0f;     /* fractional under "Fill the window" */
/* Pixels per logical unit. One on an ordinary display, two on a Retina Mac. The renderer works in
 * pixels and SDL reports pointer positions in logical units, so every pointer coordinate is
 * multiplied by this before it is compared with anything the renderer produced. */
static float px_density = 1.0f;
static int  last_gx, last_gy;
static bool audio_ok;
static float rel_x, rel_y;          /* mouse movement since the last mouse_delta() */

/* ---- game controllers (#14) -----------------------------------------------------------------
 * SDL opens whatever is plugged in; the first one that answers steers the shooter. The analogue
 * stick is read past a dead zone and the d-pad as a full deflection, so either works. */
static SDL_Gamepad *g_pad;

bool pad_present(void) { return g_pad != NULL; }
const char *pad_name(void) { return g_pad ? SDL_GetGamepadName(g_pad) : "none"; }

static void pad_rescan(void)
{
    if (g_pad && !SDL_GamepadConnected(g_pad)) { SDL_CloseGamepad(g_pad); g_pad = NULL; }
    if (g_pad) return;
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    if (ids) {
        for (int i = 0; i < n && !g_pad; i++) g_pad = SDL_OpenGamepad(ids[i]);
        SDL_free(ids);
    }
}

static void detect_phone(void);

bool pad_move(float *x, float *y)
{
    *x = *y = 0;
    if (!g_pad || !opt_pad) return false;
    float dz = (float)opt_pad_deadzone / 100.0f;
    float ax = SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    float ay = SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
    if (ax > dz || ax < -dz) *x = ax;
    if (ay > dz || ay < -dz) *y = ay;
    if (SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))  *x = -1;
    if (SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) *x =  1;
    if (SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_UP))    *y = -1;
    if (SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))  *y =  1;
    return *x != 0 || *y != 0;
}

bool pad_fire(void)
{
    if (!g_pad || !opt_pad) return false;
    return SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_SOUTH) ||
           SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_EAST) ||
           SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_WEST) ||
           SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_NORTH);
}

bool pad_start(void)
{
    if (!g_pad || !opt_pad) return false;
    return SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_START);
}
static SDL_AudioDeviceID audio_dev;
static bool has_focus = true;
static char typed[16];
static int  ntyped;
static const Image *margin_pat;
static SDL_Texture *margin_tex;
static uint32_t margin_argb[256 * 256];
static bool    border_active;
static uint8_t border_idx[7];
static int      cursor_mode = CURSOR_NONE;
static int      cur_w, cur_h, cur_hx, cur_hy;
static uint32_t cur_px[32 * 32];          /* 0 = transparent, else 0xFFrrggbb */

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

void window_set_maximized(bool on);

static bool phone_mode;                 /* a phone or tablet held in the hand: the touch layout */
bool plat_phone(void) { return phone_mode; }

bool plat_init(const char *title)
{
    /* Windows will not let a new process steal the foreground unless it is asked to; these are
     * the three hints SDL offers for it, and they have to be set before the window exists. */
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "1");
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "1");
    SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");
#ifdef __ANDROID__
    /* Android is free to tear the activity down and build it again - a fold, a resolution change
     * on a TV, the system reclaiming memory. SDLActivity's default answer to being re-created is
     * System.exit(0), which from the sofa is an app that will not start. Saying yes here makes it
     * restart the game instead, which is the behaviour a player would expect. */
    SDL_SetHint(SDL_HINT_ANDROID_ALLOW_RECREATE_ACTIVITY, "1");
    /* The title screen listens for letter keys, which means text input is started for the whole
     * run, and on Android starting it pops the on-screen keyboard up over the game. Off here;
     * plat_keyboard() brings it up for the one place a name is typed. */
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init failed: %s", SDL_GetError()); return false; }
    if (SDL_InitSubSystem(SDL_INIT_GAMEPAD)) pad_rescan();
    audio_ok = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (audio_ok) {
        audio_dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
        if (!audio_dev) { audio_ok = false; SDL_Log("no audio device (%s)", SDL_GetError()); }
    }
    if (!audio_ok) SDL_Log("no audio (%s) - continuing silently", SDL_GetError());
    /* The window opens at a 2x whole-number scale - 1280x960 - rather than at the Mac's own
     * 640x480, which is a postage stamp on a modern display (#17). If the desktop cannot hold 2x
     * it steps back to 1x; it is resizable either way, and "Open maximized" in Options overrides
     * the size entirely. */
    int win_w = SCREEN_W * 2, win_h = SCREEN_H * 2;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.w > 0)
        while ((win_w > usable.w || win_h > usable.h) && win_w > SCREEN_W)
            { win_w -= SCREEN_W; win_h -= SCREEN_H; }

    /* SDL_WINDOW_MAXIMIZED at creation is the only form of the request a window manager cannot
     * quietly drop, which asking an already-open window to maximize plainly can be. */
    /* SDL_WINDOW_HIGH_PIXEL_DENSITY is what keeps the pixels square on a Retina Mac. Without it the
     * renderer draws at the window's logical size and the compositor stretches that to the display,
     * smoothly - so the picture arrives blurred however the textures are sampled, which is the
     * bilinear look the macOS build had. With it the renderer works at the real backing size and
     * the nearest-neighbour scaling below is the only scaling that happens. The catch is that mouse
     * coordinates stay in logical units, so everything that reads one multiplies by px_density. */
    SDL_WindowFlags wf = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                         (opt_maximize ? SDL_WINDOW_MAXIMIZED : 0);
#ifdef __ANDROID__
    /* Fullscreen is what makes SDL hide the status and navigation bars (immersive mode); without
     * it a phone keeps its clock and icons across the top of the game. */
    wf |= SDL_WINDOW_FULLSCREEN;
#endif
    if (!SDL_CreateWindowAndRenderer(title, win_w, win_h, wf, &g_win, &g_ren)) {
        SDL_Log("window creation failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(g_win, SCREEN_W, SCREEN_H);
#ifdef __ANDROID__
    SDL_SetWindowFullscreen(g_win, true);
#endif
    detect_phone();
    /* Every frame is a full repaint of a 256-colour screen, and the psychedelic mushroom swaps the
     * whole colour table three times a second, so a torn frame shows half of each table. Waiting
     * for the vertical blank is what the original got from the Mac's own screen. */
    SDL_SetRenderVSync(g_ren, 1);
    SDL_StartTextInput(g_win);                /* the title screen listens for its letter keys */
    SDL_HideCursor();                         /* [HandleInitializations]: the game draws its own */
    SDL_ShowWindow(g_win);                    /* come to the front and take the keyboard */
    SDL_RaiseWindow(g_win);
    SDL_SetWindowFocusable(g_win, true);
    SDL_FlashWindow(g_win, SDL_FLASH_BRIEFLY);
    if (opt_maximize) window_set_maximized(true);
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    /* A phone draws the picture at whatever size fits, not a whole number of times, and nearest
     * neighbour at a fractional scale makes some pixels fatter than their neighbours. */
    SDL_SetTextureScaleMode(g_tex, phone_mode ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    return true;
}

InputCounts in_count;
static int mouse_down_count;            /* left buttons and (outside play) fingers currently down */

/* ---- how the game is played without a mouse, and with only some of one -------------------------
 * Mouse: left fires, right pauses and middle is Escape / Back (#49). A gamepad's Start pauses, and a
 * TV remote's Menu or Play/Pause does (#48, #50); its arrows, centre button and Back are the arrow
 * keys, Select and Back that SDL reports, which the game treats as steering, fire and Escape.
 * Touch (#51): a floating stick, a fire button and pause / back buttons, drawn over the picture.
 * Pause is a toggle here, where Caps Lock is a switch: whichever is on, the game is held. */
static bool in_play;                    /* a game is running: the touch controls are live */
static bool pause_latch;                /* pause was asked for by a button, key or tap */
static bool mid_down;                   /* the middle mouse button is held: Escape */
static bool touch_vis;                  /* the last thing used was a finger */

bool mouse_fire(void) { pump(); return mouse_down_count > 0; }

bool pause_requested(void) { return caps_lock_down() || pause_latch; }
void pause_clear(void) { pause_latch = false; }

/* ---- the menu pointer for a remote or a controller ---------------------------------------------
 * Every screen but the game is written for a mouse. A TV remote and a gamepad have no pointer, so
 * their arrows and sticks drive one: screens that have a handful of obvious places to go list them
 * (nav_targets) and the arrows jump from one to the next, with a ring round the one that is
 * selected; any other screen gets a pointer that glides at the speed the key has been held for. The
 * centre button, Start and the A button click wherever it is. */
#define NAV_MAX 40
static NavTarget nav_t[NAV_MAX];
static int   nav_n, nav_cur = -1;
static float nav_px, nav_py;            /* the pointer, in 640x480 screen pixels */
static bool  nav_have;                  /* nav_px/py hold where the pointer is */
static bool  nav_mouse_seen;           /* a real mouse has been used */
static bool  nav_ring;                  /* the last move came from a key or stick: show the ring */
static bool  nav_keys_on = false;       /* the menus have turned remote/controller steering on */
static int   nav_last_dx, nav_last_dy;
static Uint64 nav_next_ms, nav_held_ms, nav_prev_ms;
static bool  nav_prev_click;

void nav_targets(const NavTarget *t, int n, int first)
{
    if (n > NAV_MAX) n = NAV_MAX;
    /* The same list again - a screen that re-registers every pass - leaves the selection alone. */
    if (n == nav_n && n > 0 && !memcmp(nav_t, t, sizeof(NavTarget) * (size_t)n)) return;
    nav_n = n;
    if (nav_n > 0) memcpy(nav_t, t, sizeof(NavTarget) * (size_t)nav_n);
    nav_cur = nav_n > 0 ? (first >= 0 && first < nav_n ? first : 0) : -1;
    nav_have = false;
    if (nav_n > 0 && nav_ring) { /* a remote is in use: put the pointer on the first target now */
        nav_px = nav_t[nav_cur].x + nav_t[nav_cur].w / 2.0f;
        nav_py = nav_t[nav_cur].y + nav_t[nav_cur].h / 2.0f;
        nav_have = true;
    }
}
void nav_keys(bool on) { nav_keys_on = on; }

static void nav_put(float x, float y)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > SCREEN_W - 1) x = SCREEN_W - 1;
    if (y > SCREEN_H - 1) y = SCREEN_H - 1;
    nav_px = x; nav_py = y; nav_have = true;
    float lx = ((float)last_gx + x * last_scale) / (px_density > 0 ? px_density : 1.0f);
    float ly = ((float)last_gy + y * last_scale) / (px_density > 0 ? px_density : 1.0f);
    SDL_WarpMouseInWindow(g_win, lx, ly);
}

static void nav_jump(int dx, int dy)
{
    if (nav_cur < 0) return;
    float ox = nav_t[nav_cur].x + nav_t[nav_cur].w / 2.0f, oy = nav_t[nav_cur].y + nav_t[nav_cur].h / 2.0f;
    int pick = -1;
    float best = 1e30f;
    for (int i = 0; i < nav_n; i++) {
        if (i == nav_cur) continue;
        float cx = nav_t[i].x + nav_t[i].w / 2.0f - ox, cy = nav_t[i].y + nav_t[i].h / 2.0f - oy;
        float along = dx ? cx * dx : cy * dy, cross = dx ? cy : cx;
        if (along <= 1.0f) continue;                     /* not in that direction */
        float score = along + 3.0f * (cross < 0 ? -cross : cross);
        if (score < best) { best = score; pick = i; }
    }
    if (pick >= 0) nav_cur = pick;
    nav_put(nav_t[nav_cur].x + nav_t[nav_cur].w / 2.0f, nav_t[nav_cur].y + nav_t[nav_cur].h / 2.0f);
}

/* Called from pump() whenever no game is running. */
static void nav_step(void)
{
    if (in_play || !g_win || !nav_keys_on) return;
    Uint64 now = SDL_GetTicks();
    Uint64 dt = nav_prev_ms ? now - nav_prev_ms : 0;
    nav_prev_ms = now;
    if (dt > 100) dt = 100;

    int n = 0;
    const bool *st = SDL_GetKeyboardState(&n);
    int dx = 0, dy = 0;
    bool click = false;
    if (st) {
        if (SDL_SCANCODE_LEFT  < n && st[SDL_SCANCODE_LEFT])  dx = -1;
        if (SDL_SCANCODE_RIGHT < n && st[SDL_SCANCODE_RIGHT]) dx = 1;
        if (SDL_SCANCODE_UP    < n && st[SDL_SCANCODE_UP])    dy = -1;
        if (SDL_SCANCODE_DOWN  < n && st[SDL_SCANCODE_DOWN])  dy = 1;
    }
    if (st && SDL_SCANCODE_SELECT < n && st[SDL_SCANCODE_SELECT]) click = true;
    if (g_pad) {
        float px, py;
        float dz = 0.5f;
        px = SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
        py = SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
        if (px < -dz) dx = -1; else if (px > dz) dx = 1;
        if (py < -dz) dy = -1; else if (py > dz) dy = 1;
        if (SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))  dx = -1;
        if (SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) dx = 1;
        if (SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_UP))    dy = -1;
        if (SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))  dy = 1;
        if (SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_SOUTH) ||
            SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_START)) click = true;
    }

    if (dx || dy) {
        nav_ring = true;
        if (nav_n > 0) {
            if (!nav_have) {
                /* The first press only shows where the selection is - the one under a mouse that
                 * was in use, otherwise the screen's own first choice. */
                if (nav_mouse_seen) {
                    int mx, my;
                    mouse_pos(&mx, &my);
                    float best = 1e30f;
                    for (int i = 0; i < nav_n; i++) {
                        float cx = nav_t[i].x + nav_t[i].w / 2.0f - (float)mx, cy = nav_t[i].y + nav_t[i].h / 2.0f - (float)my;
                        if (cx * cx + cy * cy < best) { best = cx * cx + cy * cy; nav_cur = i; }
                    }
                }
                nav_put(nav_t[nav_cur].x + nav_t[nav_cur].w / 2.0f, nav_t[nav_cur].y + nav_t[nav_cur].h / 2.0f);
                nav_next_ms = now + 350;
            } else if (dx != nav_last_dx || dy != nav_last_dy) { nav_jump(dx, dy); nav_next_ms = now + 350; }
            else if (now >= nav_next_ms)                        { nav_jump(dx, dy); nav_next_ms = now + 140; }
            nav_held_ms = 0;
        } else {
            if (!nav_have) { int mx, my; mouse_pos(&mx, &my); nav_px = (float)mx; nav_py = (float)my; nav_have = true; }
            nav_held_ms = (dx != nav_last_dx || dy != nav_last_dy) ? 0 : nav_held_ms + dt;
            float speed = 220.0f + (nav_held_ms > 1200 ? 1200 : (float)nav_held_ms) * 0.45f;   /* px a second */
            nav_put(nav_px + dx * speed * (float)dt / 1000.0f, nav_py + dy * speed * (float)dt / 1000.0f);
        }
    } else nav_held_ms = 0;
    nav_last_dx = dx; nav_last_dy = dy;

    if (click && !nav_prev_click) {
        nav_ring = true;
        if (!nav_have) { int mx, my; mouse_pos(&mx, &my); nav_px = (float)mx; nav_py = (float)my; nav_have = true; }
        click_x = (int)nav_px; click_y = (int)nav_py;
        have_click = true; button_event = true;
    }
    nav_prev_click = click;
}

/* A real mouse moved: its position wins, and the ring goes. */
static void nav_mouse_moved(void) { nav_ring = false; nav_have = false; nav_mouse_seen = true; }

/* ---- the touch controls (#51) --------------------------------------------------------------
 * A virtual trackball and a fire button. The original is played with a mouse, and a mouse is a
 * trackball turned over: a finger dragged anywhere in the left zone moves the shooter by how far
 * it moved, and lifting and putting it down again moves nothing - the same as picking a mouse up.
 * The right zone fires. On a phone held upright the playfield is on top, the score panel under
 * it and the two zones in what is left; held sideways they are the two margins either side of the
 * picture (and over it where there is no margin). */
enum { FR_NONE = 0, FR_TRACK, FR_FIRE, FR_PAUSE, FR_BACK };
#define MAX_FINGERS 8
static struct { SDL_FingerID id; int role; float x, y; bool used; } fingers[MAX_FINGERS];
static float trk_cx, trk_cy;            /* where the finger that is steering first came down */
static float trk_dx, trk_dy;            /* how far it has moved since the game last asked, in output pixels */
static bool  trk_on;
static bool  fire_touch, back_touch;
static float t_ow = 1, t_oh = 1;        /* the output size the pixels below are measured in */

/* Where the picture and the controls are. present() works it out each frame; the touch code reads it. */
typedef struct { SDL_FRect src, dst; } Blit;
static struct {
    Blit  b[6]; int n;                  /* the pieces of the frame to draw, and where */
    float scale;                        /* output pixels per game pixel, for the playfield */
    bool  portrait;                     /* phone upright, in a game: field, panel, controls stacked */
    float pic_x, pic_w;                 /* the picture's left edge and width (landscape) */
    float ctl_x, ctl_y, ctl_w, ctl_h;   /* the controls' own area (portrait) */
} lay;

static void detect_phone(void)
{
    const char *f = getenv("APEIRON_PHONE");       /* a way to try the phone layout on a desktop */
    if (f) phone_mode = atoi(f) != 0;
#ifdef __ANDROID__
    else {
        int n = 0;
        SDL_TouchID *t = SDL_GetTouchDevices(&n);
        if (t) SDL_free(t);
        phone_mode = n > 0 && !SDL_IsTV();         /* a TV box has no screen to touch */
    }
#endif
    if (phone_mode) touch_vis = true;
}

typedef struct {
    float u, rest_x, rest_y, ball_r, fire_x, fire_y, fire_r, pause_x, pause_y, back_x, back_y, btn_r;
    SDL_FRect track, fire;
} TouchLayout;

static TouchLayout touch_layout(void)
{
    TouchLayout L;
    if (lay.portrait) {
        float u = lay.ctl_w / 8.0f;
        if (lay.ctl_h / 5.0f < u) u = lay.ctl_h / 5.0f;
        if (u < 36) u = 36;
        L.u = u;
        L.ball_r = 1.5f * u;
        L.rest_x = lay.ctl_x + lay.ctl_w * 0.27f;  L.rest_y = lay.ctl_y + lay.ctl_h * 0.56f;
        L.fire_x = lay.ctl_x + lay.ctl_w * 0.74f;  L.fire_y = lay.ctl_y + lay.ctl_h * 0.56f;
        L.fire_r = 1.3f * u;
        L.btn_r = 0.5f * u;
        L.back_x = lay.ctl_x + lay.ctl_w - 0.9f * u;   L.back_y = lay.ctl_y + 0.8f * u;
        L.pause_x = lay.ctl_x + lay.ctl_w - 2.2f * u;  L.pause_y = L.back_y;
        L.track = (SDL_FRect){ lay.ctl_x, lay.ctl_y, lay.ctl_w * 0.5f, lay.ctl_h };
        L.fire  = (SDL_FRect){ lay.ctl_x + lay.ctl_w * 0.5f, lay.ctl_y, lay.ctl_w * 0.5f, lay.ctl_h };
        return L;
    }
    float u = t_oh / 8.0f;
    if (u < 36) u = 36;
    L.u = u;
    float ml = lay.pic_x, mr = t_ow - (lay.pic_x + lay.pic_w);
    float lx = ml * 0.5f, rx = mr * 0.5f;
    if (lx < 1.8f * u) lx = 1.8f * u;
    if (rx < 1.8f * u) rx = 1.8f * u;
    if (lx > t_ow * 0.24f) lx = t_ow * 0.24f;
    if (rx > t_ow * 0.24f) rx = t_ow * 0.24f;
    L.ball_r = 1.5f * u;
    L.rest_x = lx;          L.rest_y = t_oh * 0.62f;
    L.fire_x = t_ow - rx;   L.fire_y = t_oh * 0.62f; L.fire_r = 1.2f * u;
    L.btn_r = 0.5f * u;
    L.back_x = t_ow - 0.9f * u;  L.back_y = 0.8f * u;
    L.pause_x = t_ow - 2.2f * u; L.pause_y = 0.8f * u;
    L.track = (SDL_FRect){ 0, 0, t_ow * 0.45f, t_oh };
    L.fire  = (SDL_FRect){ t_ow * 0.55f, 0, t_ow * 0.45f, t_oh };
    return L;
}

static bool in_circle(float x, float y, float cx, float cy, float r)
{
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}
static bool in_rect(float x, float y, SDL_FRect r)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void fingers_clear(void)
{
    memset(fingers, 0, sizeof fingers);
    trk_on = fire_touch = back_touch = false;
    trk_dx = trk_dy = 0;
}

static void finger_event(const SDL_Event *e)
{
    SDL_FingerID id = e->tfinger.fingerID;
    float x = e->tfinger.x * t_ow, y = e->tfinger.y * t_oh;
    int slot = -1;
    for (int i = 0; i < MAX_FINGERS; i++) if (fingers[i].used && fingers[i].id == id) slot = i;

    if (e->type == SDL_EVENT_FINGER_DOWN) {
        touch_vis = true;
        in_count.touch_btn++;
        if (!in_play || !opt_touch) return;
        if (slot < 0) for (int i = 0; i < MAX_FINGERS; i++) if (!fingers[i].used) { slot = i; break; }
        if (slot < 0) return;
        TouchLayout L = touch_layout();
        int role = FR_NONE;
        if (in_circle(x, y, L.back_x, L.back_y, L.btn_r * 1.4f)) role = FR_BACK;
        else if (in_circle(x, y, L.pause_x, L.pause_y, L.btn_r * 1.4f)) role = FR_PAUSE;
        else if (in_rect(x, y, L.fire)) role = FR_FIRE;
        else if (in_rect(x, y, L.track) && !trk_on) role = FR_TRACK;
        fingers[slot].used = true; fingers[slot].id = id; fingers[slot].role = role;
        fingers[slot].x = x; fingers[slot].y = y;
        if (role == FR_PAUSE) pause_latch = !pause_latch;
        if (role == FR_BACK) back_touch = true;
        if (role == FR_FIRE) fire_touch = true;
        if (role == FR_TRACK) { trk_on = true; trk_cx = x; trk_cy = y; }
    } else if (e->type == SDL_EVENT_FINGER_MOTION) {
        if (slot >= 0) {
            if (fingers[slot].role == FR_TRACK) { trk_dx += x - fingers[slot].x; trk_dy += y - fingers[slot].y; }
            fingers[slot].x = x; fingers[slot].y = y;
        }
    } else {                                         /* UP or CANCELED */
        if (slot < 0) return;
        int role = fingers[slot].role;
        fingers[slot].used = false;
        if (role == FR_TRACK) trk_on = false;
        if (role == FR_FIRE) fire_touch = false;
        if (role == FR_BACK) back_touch = false;
    }
}

/* How far the trackball has been rolled since the last call, in game pixels. False when no finger
 * is on it, so a mouse or keys plugged into the phone still steer. */
bool touch_trackball(float *dx, float *dy)
{
    *dx = *dy = 0;
    if (!in_play || !opt_touch) return false;
    float g = 1.8f / (lay.scale > 0 ? lay.scale : 1.0f);      /* game pixels per output pixel, and then some */
    bool any = trk_on || trk_dx != 0 || trk_dy != 0;
    *dx = trk_dx * g; *dy = trk_dy * g;
    trk_dx = trk_dy = 0;
    return any;
}

bool touch_active(void) { return in_play && opt_touch && touch_vis; }
bool touch_fire(void) { pump(); return in_play && opt_touch && fire_touch; }

void input_play(bool on)
{
    in_play = on;
    pause_latch = false;
    fingers_clear();
    nav_n = 0; nav_cur = -1; nav_have = false;
    mouse_down_count = 0;
}

/* Drawn over the finished frame. Everything is filled rectangles - discs from rows of them - so it
 * needs nothing from the artwork and draws the same everywhere. */
static void disc(float cx, float cy, float r, Uint8 R, Uint8 G, Uint8 B, Uint8 A)
{
    SDL_SetRenderDrawColor(g_ren, R, G, B, A);
    float step = r > 60 ? 3.0f : 2.0f;
    for (float y = -r; y <= r; y += step) {
        float w = sqrtf(r * r - y * y);
        SDL_FRect row = { cx - w, cy + y, 2 * w, step };
        SDL_RenderFillRect(g_ren, &row);
    }
}

static void touch_draw(void)
{
    if (!in_play || !opt_touch || !touch_vis) return;
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
    TouchLayout L = touch_layout();
    float u = L.u;

    /* the trackball: a ball in its socket; while a finger rolls it the highlight leans the way it goes */
    disc(L.rest_x, L.rest_y, L.ball_r, 255, 255, 255, trk_on ? 50 : 34);
    disc(L.rest_x, L.rest_y, L.ball_r - 5, 20, 20, 30, trk_on ? 70 : 50);
    disc(L.rest_x, L.rest_y, L.ball_r * 0.82f, 120, 140, 200, trk_on ? 70 : 44);
    float kx = L.rest_x - L.ball_r * 0.28f, ky = L.rest_y - L.ball_r * 0.28f;
    for (int i = 0; i < MAX_FINGERS; i++)
        if (trk_on && fingers[i].used && fingers[i].role == FR_TRACK) {
            float dx = fingers[i].x - trk_cx, dy = fingers[i].y - trk_cy;
            float m = sqrtf(dx * dx + dy * dy), lim = L.ball_r * 0.55f;
            if (m > lim) { dx *= lim / m; dy *= lim / m; }
            kx = L.rest_x + dx; ky = L.rest_y + dy;
        }
    disc(kx, ky, 0.34f * u, 255, 255, 255, trk_on ? 150 : 80);

    /* fire */
    disc(L.fire_x, L.fire_y, L.fire_r, 255, 80, 60, fire_touch ? 150 : 70);
    disc(L.fire_x, L.fire_y, L.fire_r * 0.55f, 255, 255, 255, fire_touch ? 130 : 60);

    /* pause: two bars; back: a cross */
    disc(L.pause_x, L.pause_y, L.btn_r, 255, 255, 255, 60);
    disc(L.back_x, L.back_y, L.btn_r, 255, 255, 255, back_touch ? 130 : 60);
    SDL_SetRenderDrawColor(g_ren, 255, 255, 255, 190);
    float bw = 0.12f * u, bh = 0.42f * u;
    SDL_FRect b1 = { L.pause_x - 0.17f * u, L.pause_y - bh / 2, bw, bh }, b2 = { L.pause_x + 0.05f * u, L.pause_y - bh / 2, bw, bh };
    SDL_RenderFillRect(g_ren, &b1); SDL_RenderFillRect(g_ren, &b2);
    for (int i = -3; i <= 3; i++) {
        float o = i * 0.06f * u, sq = 0.11f * u;
        SDL_FRect d1 = { L.back_x + o - sq / 2, L.back_y + o - sq / 2, sq, sq };
        SDL_FRect d2 = { L.back_x + o - sq / 2, L.back_y - o - sq / 2, sq, sq };
        SDL_RenderFillRect(g_ren, &d1); SDL_RenderFillRect(g_ren, &d2);
    }
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_NONE);
}

/* The ring round whatever the remote has selected. */
static void nav_draw(float gx, float gy, float fs)
{
    if (in_play || !nav_ring || nav_cur < 0 || nav_n <= 0) return;
    NavTarget *t = &nav_t[nav_cur];
    float pad = 3.0f * fs, th = (fs < 2 ? 2.0f : fs);
    float x = gx + t->x * fs - pad, y = gy + t->y * fs - pad;
    float w = t->w * fs + 2 * pad, h = t->h * fs + 2 * pad;
    SDL_SetRenderDrawColor(g_ren, 255, 215, 64, 255);
    SDL_FRect top = { x, y, w, th }, bot = { x, y + h - th, w, th };
    SDL_FRect lef = { x, y, th, h }, rig = { x + w - th, y, th, h };
    SDL_RenderFillRect(g_ren, &top); SDL_RenderFillRect(g_ren, &bot);
    SDL_RenderFillRect(g_ren, &lef); SDL_RenderFillRect(g_ren, &rig);
}

void plat_keyboard(bool show)
{
#ifdef __ANDROID__
    /* SDL has no call for the keyboard itself: it shows it when text input starts and the hint
     * allows, and hides it when text input stops while the hint still allows. So the hint is
     * switched on around a stop/start to bring it up, and left on across the stop that takes it
     * away, then back off with text input restarted for the hardware keyboard. */
    if (show) {
        SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "1");
        SDL_StopTextInput(g_win);
        SDL_StartTextInput(g_win);
    } else {
        SDL_StopTextInput(g_win);
        SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
        SDL_StartTextInput(g_win);
    }
#else
    (void)show;
#endif
}

void pump(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) plat_shutdown(0);
        if (e.type == SDL_EVENT_GAMEPAD_ADDED || e.type == SDL_EVENT_GAMEPAD_REMOVED) pad_rescan();
        /* The movement is taken from the motion events rather than asked for with
         * SDL_GetRelativeMouseState, which on some X servers answers with the pointer's absolute
         * position instead of a delta (libsdl-org/SDL#14926). xrel/yrel are the same numbers SDL
         * would accumulate for that call, and reading them here means one path on every platform. */
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            if (e.motion.which == SDL_TOUCH_MOUSEID) { in_count.touch_move++; touch_vis = true; }
            else { in_count.mouse_move++; touch_vis = false; nav_mouse_moved(); }
            if (!(in_play && opt_touch && e.motion.which == SDL_TOUCH_MOUSEID)) {
                rel_x += e.motion.xrel;
                rel_y += e.motion.yrel;
            }
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            bool down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            bool touch = e.button.which == SDL_TOUCH_MOUSEID;
            if (down) { if (touch) { in_count.touch_btn++; touch_vis = true; } else { in_count.mouse_btn++; touch_vis = false; } }
            /* In a game on a touch screen the fingers are read as fingers (FINGER_* below); the
             * mouse events SDL makes up from them would fire the gun wherever they land. */
            if (touch && in_play && opt_touch) continue;
            if (e.button.button == SDL_BUTTON_MIDDLE) mid_down = down;
            else if (e.button.button == SDL_BUTTON_RIGHT) {
                if (down && in_play) pause_latch = !pause_latch;
            } else if (down) {
                mouse_down_count++;
                button_event = true; have_click = true;
                click_x = (int)((e.button.x * px_density - last_gx) / last_scale);
                click_y = (int)((e.button.y * px_density - last_gy) / last_scale);
                nav_ring = false;
            } else if (mouse_down_count > 0) mouse_down_count--;
        }
        if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_MOTION ||
            e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED)
            finger_event(&e);
        if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) { has_focus = false; mouse_down_count = 0; fingers_clear(); mid_down = false; }
        if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) has_focus = true;
        if (e.type == SDL_EVENT_TEXT_INPUT && ntyped < (int)sizeof typed - 1)
            for (const char *t = e.text.text; *t; t++)
                if ((unsigned char)*t >= 32 && ntyped < (int)sizeof typed - 1) typed[ntyped++] = *t;
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
            in_count.pad++;
            touch_vis = false;
        }
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_START &&
            in_play && opt_pad)
            pause_latch = !pause_latch;
        if (e.type == SDL_EVENT_KEY_DOWN) {
            in_count.key++;
            if (e.key.scancode != SDL_SCANCODE_UNKNOWN && !phone_mode) touch_vis = false;
            if (e.key.key == SDLK_F11) toggle_fullscreen();
            else button_event = true;
            /* P, a remote's Menu and its Play/Pause key pause the game (#48). */
            if (in_play && !e.key.repeat &&
                (e.key.scancode == SDL_SCANCODE_P || e.key.scancode == SDL_SCANCODE_MENU ||
                 e.key.scancode == SDL_SCANCODE_MEDIA_PLAY_PAUSE))
                pause_latch = !pause_latch;
        }
    }
    nav_step();
}

bool button(void) { pump(); bool b = button_event; button_event = false; return b; }

bool window_focused(void) { pump(); return has_focus; }

/* [CapsLockDown] is GetKeys + BitTst(.., 0x3e). SDL reports Caps Lock as a modifier that is on while
 * the light is on, which is the same thing the original is testing for. */
bool caps_lock_down(void)
{
    pump();
    return (SDL_GetModState() & SDL_KMOD_CAPS) != 0;
}

bool take_key(char *c)          /* one typed character, oldest first */
{
    pump();
    if (!ntyped) return false;
    *c = typed[0];
    memmove(typed, typed + 1, (size_t)(--ntyped));
    return true;
}

bool key_down(SDL_Scancode sc)
{
    pump();
    int n = 0;
    const bool *st = SDL_GetKeyboardState(&n);
    bool v = st && (int)sc < n && st[sc];
    /* Escape is also Back on a remote or a phone, the middle mouse button, and the touch Back
     * button (#48 #49 #51). */
    if (sc == SDL_SCANCODE_ESCAPE) {
        if (st && SDL_SCANCODE_AC_BACK < n && st[SDL_SCANCODE_AC_BACK]) v = true;
        if (mid_down || back_touch) v = true;
    }
    return v;
}

/* Any key or mouse button at all - the title screen's idle timer only needs to know that
 * somebody is there, not what they pressed. */
bool any_input(void)
{
    pump();
    int n = 0;
    const bool *st = SDL_GetKeyboardState(&n);
    for (int i = 0; st && i < n; i++) if (st[i]) return true;
    return SDL_GetMouseState(NULL, NULL) != 0;
}

void mouse_pos(int *x, int *y)
{
    float mx, my;
    SDL_GetMouseState(&mx, &my);
    *x = (int)((mx * px_density - last_gx) / last_scale);
    *y = (int)((my * px_density - last_gy) / last_scale);
}

void flush_clicks(void)         /* FlushEvents(everyEvent, 0) */
{
    pump();
    have_click = false;
    button_event = false;
    ntyped = 0;
}

/* The shooter used to be steered by the absolute pointer, which stopped dead the moment the pointer
 * left the 432-wide playfield - easy to do in a maximised window, and it read as the mouse sticking.
 * During a game the pointer is taken over instead and read as movement, so it has no edges at all. */
/* "Track the pointer directly" (opt_mouse_abs). Relative mouse mode asks SDL to turn whatever the
 * pointing device reports into a stream of deltas. For an ordinary mouse that is what the device
 * already sends and the answer is exact, but a virtual machine's pointer is a graphics tablet: it
 * reports where it *is*, in tablet units, and SDL has to difference and rescale that. Under
 * VirtualBox the result comes out in visible steps rather than smoothly.
 *
 * So this mode does not ask. It reads the pointer's own position once a frame and differences that
 * itself, which is the same arithmetic without the unit conversion in the middle, and recentres the
 * pointer whenever it nears the edge of the window so it never runs out of room - the warp is
 * accounted for rather than measured, so it does not register as movement. */
static bool  abs_on;                /* relative mode is off and we are tracking the pointer */
static bool  abs_have;              /* abs_px/py hold a position to difference against */
static float abs_px, abs_py;

static void abs_recentre(void)
{
    int ww = 0, wh = 0;
    SDL_GetWindowSize(g_win, &ww, &wh);
    if (ww <= 0 || wh <= 0) return;
    abs_px = ww * 0.5f; abs_py = wh * 0.5f;
    SDL_WarpMouseInWindow(g_win, abs_px, abs_py);
    abs_have = true;
}

void mouse_relative(bool on)
{
    abs_on = on && opt_mouse_abs;
    if (abs_on) {
        SDL_SetWindowRelativeMouseMode(g_win, false);
        SDL_HideCursor();                       /* relative mode would have done this for us */
        abs_have = false;
        abs_recentre();
    } else {
        if (!SDL_SetWindowRelativeMouseMode(g_win, on) && on) {
            /* Not every platform can capture the pointer (Android before 8, say). Rather than
             * leave the ship without a mouse, read the pointer's own position instead. */
            diag_note("mouse: relative mode refused (%s) - tracking the pointer instead", SDL_GetError());
            abs_on = true; abs_have = false;
            SDL_HideCursor();
            abs_recentre();
        }
        if (!on) SDL_ShowCursor();
    }
    pump();
    rel_x = rel_y = 0;              /* drop whatever built up on the way in or out */
}

/* The movement since the last call, in 640x480 screen pixels. */
void mouse_delta(float *dx, float *dy)
{
    pump();
    float s = last_scale > 0 ? last_scale : 1.0f;
    s /= px_density > 0 ? px_density : 1.0f;   /* the deltas below are in logical units */
    if (abs_on) {
        float mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        if (!abs_have) { abs_px = mx; abs_py = my; abs_have = true; }
        float ax = mx - abs_px, ay = my - abs_py;
        abs_px = mx; abs_py = my;
        int ww = 0, wh = 0;
        SDL_GetWindowSize(g_win, &ww, &wh);
        if (ww > 0 && wh > 0 &&
            (mx < ww * 0.15f || mx > ww * 0.85f || my < wh * 0.15f || my > wh * 0.85f))
            abs_recentre();
        rel_x = rel_y = 0;          /* the warp's own motion events are not movement */
        *dx = ax / s;
        *dy = ay / s;
        return;
    }
    float gain = opt_mouse_speed / 100.0f;
    *dx = rel_x / s * gain;
    *dy = rel_y / s * gain;
    rel_x = rel_y = 0;
}

/* The Options screen flipping the switch mid-session: whichever mode is wanted has to be set up
 * now, because the game only calls mouse_relative when it enters or leaves the playfield. */
void mouse_abs_changed(void)
{
    if (abs_on || SDL_GetWindowRelativeMouseMode(g_win)) mouse_relative(true);
}

bool take_click_only(void)      /* Button(): the mouse, not the keyboard */
{
    pump();
    bool b = have_click;
    have_click = false;
    return b;
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

/* crsr 128 as tools/extract_assets.py wrote it: the colours are kept as RGB so the hand looks the
 * same over the title screen's colour table and the game's. */
bool cursor_load(const char *dir)
{
    char p[1024], line[64];
    snprintf(p, sizeof p, "%s/cursor_hand.cur", dir);
    FILE *f = fopen(p, "r");
    if (!f) { SDL_Log("cannot open %s", p); return false; }
    int ncol = 0;
    uint32_t col[32];
    bool ok = fscanf(f, "CURSOR %d %d %d %d", &cur_w, &cur_h, &cur_hx, &cur_hy) == 4;
    ok = ok && fscanf(f, " COLORS %d", &ncol) == 1 && ncol > 0 && ncol <= 32;
    ok = ok && cur_w > 0 && cur_w <= 32 && cur_h > 0 && cur_h <= 32;
    for (int i = 0; ok && i < ncol; i++) {
        int r, g, b;
        ok = fscanf(f, "%d %d %d", &r, &g, &b) == 3;
        col[i] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
    /* Skip to the end of the last colour line. Casting the call to void does not silence fgets's
     * warn_unused_result - the result has to land somewhere - so it is taken and then discarded.
     * Hitting the end of the file here only means the rows below are missing, and the loop that
     * reads them catches that. */
    if (ok) {
        char *rest = fgets(line, sizeof line, f);
        (void)rest;
    }
    for (int y = 0; ok && y < cur_h; y++) {
        if (!fgets(line, sizeof line, f)) { ok = false; break; }
        for (int x = 0; x < cur_w; x++) {
            char c = line[x];
            cur_px[y * cur_w + x] = (c >= 'a' && c < 'a' + ncol) ? col[c - 'a'] : 0;
        }
    }
    fclose(f);
    if (!ok) SDL_Log("bad cursor file %s", p);
    return ok;
}

/* The system arrow is off throughout ([HandleInitializations] calls HideCursor); CURSOR_HAND draws
 * the game's own hand into the frame instead, and CURSOR_NONE leaves the pointer invisible. */
void cursor_set(int mode) { cursor_mode = mode; }

void present(void)
{
    uint32_t lut[256];
    for (int i = 0; i < 256; i++)
        lut[i] = 0xFF000000u | ((uint32_t)(shown[i].r >> 8) << 16)
               | ((uint32_t)(shown[i].g >> 8) << 8) | (uint32_t)(shown[i].b >> 8);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) argb[i] = lut[fb[i]];

    /* whole-number scale, centred; outside the 640x480 area is "the rest of the monitor" */
    int ow, oh;
    SDL_GetRenderOutputSize(g_ren, &ow, &oh);
    px_density = SDL_GetWindowPixelDensity(g_win);
    if (px_density <= 0) px_density = 1.0f;
    int s = ow / SCREEN_W < oh / SCREEN_H ? ow / SCREEN_W : oh / SCREEN_H;
    if (s < 1) s = 1;
    int gw = SCREEN_W * s, gh = SCREEN_H * s;
    if (opt_scale == SCALE_FIT) {        /* fill the window, keeping the 4:3 shape */
        gh = oh; gw = oh * SCREEN_W / SCREEN_H;
        if (gw > ow) { gw = ow; gh = ow * SCREEN_H / SCREEN_W; }
    }
    int gx = (ow - gw) / 2, gy = (oh - gh) / 2;
    float fs = (float)gw / (float)SCREEN_W;
    lay.n = 0; lay.portrait = false;

    if (phone_mode) {
        /* No desktop pattern and no frame: the picture takes all the room there is, inside the
         * parts of the screen a notch or a rounded corner leaves alone. */
        float sx = 0, sy = 0, sw = (float)ow, sh = (float)oh;
        SDL_Rect sa;
        if (SDL_GetWindowSafeArea(g_win, &sa) && sa.w > 0 && sa.h > 0) {
            sx = sa.x * px_density; sy = sa.y * px_density; sw = sa.w * px_density; sh = sa.h * px_density;
        }
        if (in_play && sh > sw * 1.05f) {
            /* Upright: the playfield across the top, the panel's boxes re-laid under it (the panel
             * is a tall strip, which is no use to a tall screen), and the controls in the rest. */
            float sc = sw / FIELD_W;
            if (FIELD_H * sc > sh * 0.60f) sc = sh * 0.60f / FIELD_H;
            float fw = FIELD_W * sc, fh = FIELD_H * sc;
            float fx = sx + (sw - fw) / 2, fy = sy;
            lay.b[lay.n++] = (Blit){ { 0, 0, FIELD_W, FIELD_H }, { fx, fy, fw, fh } };
            /* where each box and its label sit on the panel (PICT 401), top and height */
            static const float cell[4][2] = { { 100, 72 }, { 198, 72 }, { 298, 106 }, { 404, 66 } };
            float rest = sy + sh - (fy + fh);
            float hsA = sw / 416.0f;                       /* two across */
            bool two = (72 + 106) * hsA <= rest * 0.5f;
            float hs = two ? hsA : sw / 832.0f;            /* otherwise all four in a row */
            float cw = sw / (two ? 2 : 4);
            float row1 = two ? 72 * hs : 106 * hs, row2 = 106 * hs;
            float hy = fy + fh;
            for (int i = 0; i < 4; i++) {
                int col = two ? i % 2 : i, row = two ? i / 2 : 0;
                float rh = (two && row == 0) ? row1 : row2;
                float ry = hy + ((two && row == 1) ? row1 : 0);
                float dw = 208 * hs, dh = cell[i][1] * hs;
                lay.b[lay.n++] = (Blit){ { (float)PANEL_X, cell[i][0], 208, cell[i][1] },
                                         { sx + col * cw + (cw - dw) / 2, ry + (rh - dh) / 2, dw, dh } };
            }
            float hud_h = two ? row1 + row2 : row2;
            lay.portrait = true;
            lay.scale = sc;
            lay.ctl_x = sx; lay.ctl_w = sw;
            lay.ctl_y = hy + hud_h; lay.ctl_h = sy + sh - lay.ctl_y;
            if (lay.ctl_h < 1) lay.ctl_h = 1;
            gx = (int)fx; gy = (int)fy; fs = sc;
        } else {
            /* Menus, and a game held sideways: the whole 640x480 picture, as big as it will go. */
            fs = sw / SCREEN_W < sh / SCREEN_H ? sw / SCREEN_W : sh / SCREEN_H;
            gw = (int)(SCREEN_W * fs); gh = (int)(SCREEN_H * fs);
            gx = (int)(sx + (sw - gw) / 2); gy = (int)(sy + (sh - gh) / 2);
            fs = (float)gw / SCREEN_W;
            lay.b[lay.n++] = (Blit){ { 0, 0, SCREEN_W, SCREEN_H }, { (float)gx, (float)gy, (float)gw, (float)gh } };
            lay.scale = fs;
        }
    } else {
        lay.b[lay.n++] = (Blit){ { 0, 0, SCREEN_W, SCREEN_H }, { (float)gx, (float)gy, (float)gw, (float)gh } };
        lay.scale = fs;
    }
    lay.pic_x = (float)gx; lay.pic_w = (float)gw;
    /* The scale the picture is actually drawn at, which "Fill the window" makes fractional. The
     * frame and the desktop pattern around it are drawn at this too, not at the whole-number `s`:
     * using `s` there left the lavender rings the wrong thickness and sitting away from the edge
     * of the picture, which is what the odd left border was (#21). */
    last_scale = fs > 0 ? fs : 1.0f;
    last_gx = gx; last_gy = gy;
    t_ow = (float)ow; t_oh = (float)oh;

    /* Asked of the window rather than remembered from an event: a focus change that arrives while
     * something else is draining the queue used to leave the flag stuck off, and the hand with it. */
    bool lit = (SDL_GetWindowFlags(g_win) & (SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS)) != 0;
    if (cursor_mode == CURSOR_HAND && cur_w && lit) {          /* [SetCCursor](crsr 128) */
        int mx, my;
        mouse_pos(&mx, &my);
        for (int y = 0; y < cur_h; y++) {
            int dy = my - cur_hy + y;
            if (dy < 0 || dy >= SCREEN_H) continue;
            for (int x = 0; x < cur_w; x++) {
                int dx = mx - cur_hx + x;
                uint32_t c = cur_px[y * cur_w + x];
                if (c && dx >= 0 && dx < SCREEN_W) argb[dy * SCREEN_W + dx] = c;
            }
        }
    }
    SDL_UpdateTexture(g_tex, NULL, argb, SCREEN_W * 4);

    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);
    if (!phone_mode && (gx > 0 || gy > 0)) {
        if (margin_pat && margin_tex) {
            for (int i = 0; i < margin_pat->w * margin_pat->h; i++) margin_argb[i] = lut[margin_pat->px[i]];
            SDL_UpdateTexture(margin_tex, NULL, margin_argb, margin_pat->w * 4);
            SDL_FRect all = { 0, 0, (float)ow, (float)oh };
            SDL_RenderTextureTiled(g_ren, margin_tex, NULL, fs, &all);
        }
        if (border_active) {
            for (int k = 1; k <= 7; k++) {
                set_draw_color(&shown[border_idx[k - 1]]);
                float x0 = gx - k * fs, y0 = gy - k * fs, t = fs;
                float w = (SCREEN_W + 2 * k) * fs, h = (SCREEN_H + 2 * k) * fs;
                SDL_FRect top = { x0, y0, w, t }, bot = { x0, y0 + h - t, w, t };
                SDL_FRect lef = { x0, y0, t, h }, rig = { x0 + w - t, y0, t, h };
                SDL_RenderFillRect(g_ren, &top); SDL_RenderFillRect(g_ren, &bot);
                SDL_RenderFillRect(g_ren, &lef); SDL_RenderFillRect(g_ren, &rig);
            }
        }
    }
    for (int i = 0; i < lay.n; i++) SDL_RenderTexture(g_ren, g_tex, &lay.b[i].src, &lay.b[i].dst);
    nav_draw((float)gx, (float)gy, fs);
    touch_draw();
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

/* Draw an image through its own colour table rather than the screen's. cicn 128 - the grinning
 * face on the high-score dialog - is the one picture in the game that appears over a palette that
 * is not the one it was cut for, so its colours are matched to whatever is on the monitor at the
 * moment it is drawn instead of being baked in by the extractor. */
/* The options screen's Graphics page, applied once the saved settings have been read.
 *
 * SDL_MaximizeWindow alone is not enough: the request has to reach a window that is already mapped,
 * and even then some window managers drop it. So the window is pumped and synced first, and then
 * sized to the display's usable bounds by hand as well - which looks the same either way, and
 * leaves a window that is really that size even where the maximized state did not take. */
void window_set_maximized(bool on)
{
    if (!g_win) return;
    if (!on) { SDL_RestoreWindow(g_win); SDL_RaiseWindow(g_win); return; }

    /* Give the window manager a chance to act on the state the window was CREATED with before
     * asking again: SDL_SyncWindow blocks until a pending state change has actually landed. */
    SDL_PumpEvents();
    SDL_SyncWindow(g_win);
    if (!(SDL_GetWindowFlags(g_win) & SDL_WINDOW_MAXIMIZED)) {
        SDL_MaximizeWindow(g_win);
        SDL_SyncWindow(g_win);
    }

    /* Only if the state really did not take is the window sized by hand, and then on the display
     * it is actually on rather than the primary one - with two monitors those differ, and the
     * usable bounds of the wrong one would throw the window across to the other screen. The test
     * is the MAXIMIZED flag, not the size: asking for the size before the change has landed
     * reports the old one, and sizing a window that IS maximized is exactly what un-maximizes it
     * on Windows. Acting on that stale size was undoing the thing it was meant to guarantee. */
    if (!(SDL_GetWindowFlags(g_win) & SDL_WINDOW_MAXIMIZED)) {
        SDL_DisplayID d = SDL_GetDisplayForWindow(g_win);
        SDL_Rect use;
        if (d && SDL_GetDisplayUsableBounds(d, &use) && use.w >= SCREEN_W && use.h >= SCREEN_H) {
            SDL_SetWindowPosition(g_win, use.x, use.y);
            SDL_SetWindowSize(g_win, use.w, use.h);
        }
    }
    SDL_RaiseWindow(g_win);
}

void draw_image_remap(const Image *im, int x0, int y0, bool skip0)
{
    uint8_t map[256];
    bool have[256] = { false };
    for (int y = 0; y < im->h; y++) {
        int dy = y0 + y;
        if (dy < 0 || dy >= SCREEN_H) continue;
        for (int x = 0; x < im->w; x++) {
            int dx = x0 + x;
            if (dx < 0 || dx >= SCREEN_W) continue;
            uint8_t i = im->px[y * im->w + x];
            if (skip0 && i == 0) continue;
            if (!have[i]) { map[i] = find_color(im->pal[i].r, im->pal[i].g, im->pal[i].b); have[i] = true; }
            fb[dy * SCREEN_W + dx] = map[i];
        }
    }
}

/* A QuickDraw round rect: FrameRoundRect/PaintRoundRect with ovalWidth and ovalHeight 16, which is
 * what every Mac push button and the default item's ring are drawn with - a radius of eight. */
void fill_round_rect(int x, int y, int w, int h, int r, uint8_t c)
{
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int dy = 0; dy < h; dy++) {
        int ty = dy < r ? r - dy : (dy >= h - r ? dy - (h - r) + 1 : 0);
        int inset = 0;
        if (ty > 0) {                       /* inside the corner's quarter circle */
            while (inset < r) {
                int ax = r - inset, ay = ty;
                if (ax * ax + ay * ay <= r * r) break;
                inset++;
            }
        }
        int yy = y + dy;
        if (yy < 0 || yy >= SCREEN_H) continue;
        for (int dx = inset; dx < w - inset; dx++) {
            int xx = x + dx;
            if (xx < 0 || xx >= SCREEN_W) continue;
            fb[yy * SCREEN_W + xx] = c;
        }
    }
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

/* The shield, masked to a circle with a soft edge (#24). The artwork is a 16x16 square of sparkle
 * and it reads as a square; a disc of radius eight centred on the sprite reads as the shell it is
 * meant to be.
 *
 * There is no alpha channel to do this with - the frame buffer is 8-bit indices into a 256-entry
 * clut, exactly as the Mac's was - so the edge is anti-aliased the only way an indexed display can
 * be: work out how much of each rim pixel the circle covers, mix the sprite's colour with the
 * colour already on the screen in that proportion, and write the nearest index the clut has to the
 * result. The coverage comes from sampling each pixel on a 4x4 grid, which gives seventeen levels -
 * enough that the rim reads as a curve rather than a staircase. It is computed once; the colour
 * mixing has to happen every frame, because what is behind the shooter changes and so does the
 * clut, but it is only the sixty-odd pixels on the rim. */
void draw_sprite_disc(const Sprite *s, int frame, int x, int y, int cx, int cy, int cw, int ch)
{
    if (s->count <= 0) return;
    if (frame < 1) frame = 1;
    if (frame > s->count) frame = ((frame - 1) % s->count) + 1;

#define DISC_SS 8                           /* samples per axis: 64 coverage levels, not 16 */
    static uint8_t cov[64 * 64];
    static int cov_w, cov_h;
    if (s->w <= 64 && s->h <= 64 && (cov_w != s->w || cov_h != s->h)) {
        cov_w = s->w; cov_h = s->h;
        float r = (float)(s->w < s->h ? s->w : s->h) / 2.0f;
        float ox = (float)s->w / 2.0f, oy = (float)s->h / 2.0f;
        for (int row = 0; row < s->h; row++)
            for (int col = 0; col < s->w; col++) {
                int n = 0;
                for (int sy = 0; sy < DISC_SS; sy++)
                    for (int sx = 0; sx < DISC_SS; sx++) {
                        float px = (float)col + ((float)sx + 0.5f) / DISC_SS - ox;
                        float py = (float)row + ((float)sy + 0.5f) / DISC_SS - oy;
                        if (px * px + py * py <= r * r) n++;
                    }
                cov[row * s->w + col] = (uint8_t)n;
            }
    }
    if (cov_w != s->w || cov_h != s->h) {   /* too big to mask: draw it as it is */
        draw_sprite_clipped(s, frame, x, y, cx, cy, cw, ch);
        return;
    }

    const uint8_t *src = s->img.px + (size_t)(frame - 1) * s->h * s->w;
    for (int row = 0; row < s->h; row++) {
        int dy = y + row;
        if (dy < cy || dy >= cy + ch || dy < 0 || dy >= SCREEN_H) continue;
        for (int col = 0; col < s->w; col++) {
            int dx = x + col;
            if (dx < cx || dx >= cx + cw || dx < 0 || dx >= SCREEN_W) continue;
            uint8_t v = src[row * s->w + col];
            if (v == s->trans) continue;
            int n = cov[row * s->w + col];
            if (n == 0) continue;
            if (n == DISC_SS * DISC_SS) { fb[dy * SCREEN_W + dx] = v; continue; }
            const Rgb *a = &s->img.pal[v];               /* the sprite's own colour */
            const Rgb *b = &table[fb[dy * SCREEN_W + dx]];   /* and what it is covering */
            int m = DISC_SS * DISC_SS;
            int r = (a->r * n + b->r * (m - n)) / m;
            int g = (a->g * n + b->g * (m - n)) / m;
            int bl = (a->b * n + b->b * (m - n)) / m;
            fb[dy * SCREEN_W + dx] = find_color(r, g, bl);
        }
    }
}

/* Like draw_sprite, but keying out a colour of the caller's choosing rather than the sheet's own
 * transparent index - the weapon icons are drawn on black for the score panel's boxes, so flying
 * one across the playfield needs the black treated as nothing. */
void draw_sprite_key(const Sprite *s, int frame, int x, int y, uint8_t key)
{
    if (!s->img.px || frame < 1 || frame > s->count) return;
    const uint8_t *src = s->img.px + (size_t)(frame - 1) * s->h * s->w;
    for (int yy = 0; yy < s->h; yy++) {
        int dy = y + yy;
        if (dy < 0 || dy >= SCREEN_H) continue;
        for (int xx = 0; xx < s->w; xx++) {
            int dx = x + xx;
            uint8_t v = src[yy * s->w + xx];
            if (v == key || v == s->trans || dx < 0 || dx >= SCREEN_W) continue;
            fb[dy * SCREEN_W + dx] = v;
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

/* QuickDraw LineTo with the default 1x1 pen - Bresenham, both ends included. */
void draw_line(int x0, int y0, int x1, int y1, uint8_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < SCREEN_W && y0 >= 0 && y0 < SCREEN_H) fb[y0 * SCREEN_W + x0] = c;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
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

/* The audio device is opened once and every voice is a stream bound to it for the life of the
 * program. Opening a device stream per sound effect - which is what this used to do - costs the
 * best part of a frame or more each time the hardware is renegotiated, and with a sound on nearly
 * every mushroom hit that is exactly the stutter it produced. */
Voice sound_play(const Sound *s)
{
    Voice v = { NULL, 0 };
    if (!s || !s->loaded || !s->data) return v;
    int bps = s->spec.freq * s->spec.channels * (int)SDL_AUDIO_BYTESIZE(s->spec.format);
    if (bps <= 0) return v;
    v.end_ms = SDL_GetTicks() + (Uint64)s->len * 1000 / (Uint64)bps;   /* keeps timing right with no device */
    return v;                       /* the caller's own stream carries the sound; see voice_play */
}

/* The Sound Volume setting. [SetPrefs] turns the six menu entries (Off, Very Soft, Soft, Moderate,
 * Loud, Very Loud) into [ST_SetVolume] levels 0, 0x25, 0x50, 0x8c, 200 and 0xff out of 0x100, and
 * the original passed that to the Sound Manager as the whole machine's output level. Here it is a
 * gain on every stream the game opens, which is the same level without touching the device's own
 * volume (#47: the setting was saved and shown but never applied, so the game was always at full
 * level - "Off" included). Music and effects share it, as they did. */
static float volume_gain(void)
{
    static const int level[6] = { 0, 0x25, 0x50, 0x8c, 200, 0xff };
    int v = A.pref_volume < 0 ? 0 : A.pref_volume > 5 ? 5 : A.pref_volume;
    return (float)level[v] / 256.0f;
}

static void stream_gain(SDL_AudioStream *st)
{
    if (st) SDL_SetAudioStreamGain(st, volume_gain());
}

/* Puts `s` into an already-open stream, converting on the way in. */
static void voice_play(SDL_AudioStream **slot, Voice *v, const Sound *s)
{
    if (!audio_ok || !s || !s->loaded || !s->data) return;
    if (!*slot) {
        *slot = SDL_CreateAudioStream(&s->spec, NULL);
        if (!*slot) return;
        if (!SDL_BindAudioStream(audio_dev, *slot)) {
            SDL_DestroyAudioStream(*slot);
            *slot = NULL;
            return;
        }
    }
    stream_gain(*slot);
    SDL_ClearAudioStream(*slot);
    SDL_SetAudioStreamFormat(*slot, &s->spec, NULL);
    SDL_PutAudioStreamData(*slot, s->data, (int)s->len);
    SDL_FlushAudioStream(*slot);
    v->stream = *slot;
}

bool sound_playing(const Voice *v)
{
    if (SDL_GetTicks() < v->end_ms) return true;
    return v->stream && SDL_GetAudioStreamQueued(v->stream) > 0;
}

/* Stops what a voice is playing without throwing its stream away. */
void sound_halt(Voice *v)
{
    if (v->stream) SDL_ClearAudioStream(v->stream);
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
static struct { Voice v; int id; SDL_AudioStream *stream; } voices[VOICES];
static Voice music_voice;
static int  music_state;                  /* 0 off, 1 playing "start", 2 playing the loop */
static Sound music_start_snd, music_loop_snd;
static SDL_AudioStream *music_stream;

/* The title screen's arrows change the setting; every open stream picks the new level up at once. */
void audio_volume_changed(void)
{
    for (int i = 0; i < VOICES; i++) stream_gain(voices[i].stream);
    stream_gain(music_stream);
}

/* Loads whatever sounds `dir` holds over whatever is loaded already, freeing what it replaces.
 * assets_set_pack() calls it twice - the game's own bank, then the pack's - so a pack carrying
 * only a few sounds leaves the rest of the game audible. */
void sfx_load_bank(const char *dir)
{
    char p[1024];
    static bool have[MAX_SFX];
    for (int i = 0; i < MAX_SFX; i++) {
        snprintf(p, sizeof p, "%s/snd/%d.wav", dir, i);
        FILE *f = fopen(p, "rb");
        if (!f) continue;
        fclose(f);
        if (have[i] && sfx_bank[i].data) SDL_free(sfx_bank[i].data);
        have[i] = sound_load(&sfx_bank[i], p);
    }
    struct { Sound *s; const char *stem; } mus[2] = {
        { &music_start_snd, "music_start" }, { &music_loop_snd, "music_loop" }
    };
    for (int i = 0; i < 2; i++) {
        snprintf(p, sizeof p, "%s/snd/%s.wav", dir, mus[i].stem);
        FILE *f = fopen(p, "rb");
        if (!f) continue;
        fclose(f);
        if (mus[i].s->loaded && mus[i].s->data) SDL_free(mus[i].s->data);
        sound_load(mus[i].s, p);
    }
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
    voice_play(&voices[slot].stream, &voices[slot].v, &sfx_bank[id]);
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
        if (voices[i].v.stream && !sound_playing(&voices[i].v)) { voices[i].v.stream = NULL; voices[i].v.end_ms = 0; }
}

void music_start(void)                     /* [StartMusic]: "Music Start" then "Music Loop" */
{
    music_halt();
    if (music_start_snd.loaded) {
        music_voice = sound_play(&music_start_snd);
        voice_play(&music_stream, &music_voice, &music_start_snd);
        music_state = 1;
    }
    else if (music_loop_snd.loaded) { music_voice = sound_play(&music_loop_snd); music_state = 2; }
}

void music_halt(void) { sound_halt(&music_voice); music_state = 0; }

void music_pump(void)
{
    voices_reap();
    if (!music_state) return;
    if (sound_playing(&music_voice)) return;
    sound_halt(&music_voice);
    if (music_loop_snd.loaded) {
        music_voice = sound_play(&music_loop_snd);
        voice_play(&music_stream, &music_voice, &music_loop_snd);
        music_state = 2;
    }
    else music_state = 0;
}

/* ---- asking for the artwork -------------------------------------------------------------------
 * SDL's folder picker is asynchronous: it calls back while events are being pumped, so this shows
 * it and then keeps the window alive until the callback has fired. A .app started from the Finder
 * has no console, so this is the only way it can say what it needs.
 */
typedef struct { char *out; size_t n; volatile bool done, got; } PickState;

static void SDLCALL pick_cb(void *userdata, const char * const *list, int filter)
{
    PickState *st = (PickState *)userdata;
    (void)filter;
    if (list && list[0]) {
        snprintf(st->out, st->n, "%s", list[0]);
        st->got = true;
    }
    st->done = true;
}

/* Is anyone actually there? The dummy and offscreen video drivers are what a CI runner and a
 * headless machine get, and nothing that waits for a person may wait under them: the release
 * workflow runs the game exactly like this to check that a missing assets folder is refused
 * rather than crashed on, and anything that blocks for input hangs that job until it is killed.
 * Which is what the folder browser did, for fifty-five minutes, twice. */
bool plat_headless(void)
{
    const char *drv = SDL_GetCurrentVideoDriver();
    return !drv || !strcmp(drv, "dummy") || !strcmp(drv, "offscreen");
}

bool assets_pick_dir(char *out, size_t n)
{
    PickState st = { out, n, false, false };
    if (plat_headless()) return false;
    plat_message("Where is the artwork?",
                 "This build does not include the game's artwork, fonts or sounds - they are "
                 "Ambrosia Software's. Choose the 'assets' folder you made from your own copy "
                 "of Apeiron with tools/extract_assets.py.");
    SDL_ShowOpenFolderDialog(pick_cb, &st, g_win, NULL, false);
    Uint64 giveup = SDL_GetTicks() + 300000;
    while (!st.done && SDL_GetTicks() < giveup) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_EVENT_QUIT) return false;
        SDL_Delay(16);
    }
    return st.got;
}

/* The same, for the game's zip: what the player has is the download, and the game makes its own
 * artwork from it. */
bool assets_pick_zip(char *out, size_t n)
{
    PickState st = { out, n, false, false };
    if (plat_headless()) return false;
#ifdef __ANDROID__
    /* The system's picker filters by MIME type, and a browser may have saved the download as
     * application/octet-stream or x-zip-compressed - which a "zip" filter would hide. Show
     * everything; the game checks what it was given. */
    SDL_ShowOpenFileDialog(pick_cb, &st, g_win, NULL, 0, NULL, false);
#else
    static const SDL_DialogFileFilter filters[] = { { "Zip files", "zip" } };
    SDL_ShowOpenFileDialog(pick_cb, &st, g_win, filters, 1, NULL, false);
#endif
    Uint64 giveup = SDL_GetTicks() + 300000;
    while (!st.done && SDL_GetTicks() < giveup) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_EVENT_QUIT) return false;
        SDL_Delay(16);
    }
    return st.got;
}

/* A message the player will actually see.
 *
 * SDL has no message-box backend on Android: SDL_ShowSimpleMessageBox returns false there and
 * puts nothing on the screen. So a build that could not find its artwork used to print a line to
 * a log nobody reads and exit - which from the sofa is indistinguishable from the app not starting
 * at all, and is what "it installs but it does not open" turned out to look like.
 *
 * Where there is a system dialog, that is still used. Where there is not, the message is drawn in
 * the game's own frame buffer with the font built into the executable - which needs no artwork -
 * and held until a key, a button or a tap, with a long stop so it can never hang a machine with
 * nobody at it. */
void plat_message(const char *title, const char *body)
{
    SDL_Log("%s: %s", title, body);          /* logcat, the terminal, wherever SDL_Log goes */

    if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, body, g_win)) return;
    if (!g_win || !g_ren || !g_tex) return;   /* no window to draw in: the log is all there is */

    enum { C_BG = 1, C_INK = 2, C_TITLE = 3 };
    Rgb c[256];
    memset(c, 0, sizeof c);
    c[C_BG]    = (Rgb){ 0x1000, 0x1800, 0x2600 };
    c[C_INK]   = (Rgb){ 0xE800, 0xE800, 0xE800 };
    c[C_TITLE] = (Rgb){ 0xFFFF, 0xD800, 0x3000 };
    set_monitor_clut(c, true);

    /* Wrapped by hand at the width the built-in 6-pixel font gives us. */
    const int cols = (SCREEN_W - 80) / 6;
    char lines[12][128];
    int nlines = 0;
    const char *p = body;
    while (*p && nlines < 12) {
        int take = 0, brk = 0;
        while (p[take] && take < cols && take < (int)sizeof lines[0] - 1) {
            if (p[take] == ' ') brk = take;
            take++;
        }
        if (p[take] && brk) take = brk;       /* step back to a space rather than split a word */
        memcpy(lines[nlines], p, (size_t)take);
        lines[nlines][take] = 0;
        nlines++;
        p += take;
        while (*p == ' ') p++;
    }

    Uint64 until = SDL_GetTicks() + 120000;
    for (;;) {
        fill_rect(0, 0, SCREEN_W, SCREEN_H, C_BG);

        mini_text2x(40, 60, title, C_TITLE);
        for (int i = 0; i < nlines; i++) mini_text(40, 100 + i * 12, lines[i], C_INK);
        mini_text(40, SCREEN_H - 42, "left/right or D: what the game can see", C_TITLE);
        mini_text(40, SCREEN_H - 30, "any other key, button or tap carries on", C_TITLE);
        set_entries();
        present();
        if (plat_headless()) break;        /* drawn for the record; nobody is here to dismiss it */

        /* The report is the useful half of this on a TV: it says which folders the game can
         * actually open and why not when it cannot. Left and right reach it from a remote. */
        if (key_down(SDL_SCANCODE_D) || key_down(SDL_SCANCODE_LEFT) ||
            key_down(SDL_SCANCODE_RIGHT)) {
            diag_screen();
            set_monitor_clut(c, true);
            wait_ms(150);
            continue;
        }
        char k;
        if (take_key(&k) || take_click_only() || pad_fire() || pad_start()) break;
        if (key_down(SDL_SCANCODE_RETURN) || key_down(SDL_SCANCODE_ESCAPE) ||
            key_down(SDL_SCANCODE_SPACE)  || key_down(SDL_SCANCODE_Q)) break;
        if (SDL_GetTicks() > until) break;
        pump();
        wait_ms(16);
    }
}

/* Android's picker hands back a content:// URI, not a path: the file stays with the app that owns it
 * and SDL_IOFromFile is the one way to read it. The game's zip is read with fopen like everything
 * else, so the picked file is copied into the app's own folder first (about five megabytes) and
 * the caller deletes the copy once the artwork is made. */
#ifdef __ANDROID__
bool plat_import_zip(const char *uri, char *out, size_t n)
{
    const char *base = SDL_GetAndroidInternalStoragePath();
    if (!base || !*base) return false;
    snprintf(out, n, "%s/picked.zip", base);
    SDL_IOStream *in = SDL_IOFromFile(uri, "rb");
    if (!in) { diag_note("import: cannot open '%s': %s", uri, SDL_GetError()); return false; }
    FILE *f = fopen(out, "wb");
    if (!f) { SDL_CloseIO(in); return false; }
    static unsigned char buf[65536];
    bool ok = true;
    size_t total = 0;
    for (;;) {
        size_t got = SDL_ReadIO(in, buf, sizeof buf);
        if (got == 0) break;
        if (fwrite(buf, 1, got, f) != got) { ok = false; break; }
        total += got;
        if (total > 256u * 1024 * 1024) { ok = false; break; }       /* nothing here is that big */
    }
    SDL_CloseIO(in);
    if (fclose(f) != 0) ok = false;
    if (!ok || total == 0) { remove(out); return false; }
    diag_note("import: copied %zu bytes from '%s'", total, uri);
    return true;
}
#else
bool plat_import_zip(const char *uri, char *out, size_t n) { (void)uri; (void)out; (void)n; return false; }
#endif
