/*
 * core.c - the libretro entry points, and the handoff that lets a blocking game live inside a
 * frontend that expects one frame per call.
 *
 * retro_run() must paint a frame and return. This port is the opposite shape: title_loop(),
 * new_game() and do_options() each sit in their own for(;;) until the player does something. So
 * the game runs on its own thread and present() parks it until the frontend asks for the next
 * frame. The two threads strictly alternate - exactly one is runnable at any moment - which is
 * why none of the state they share needs a lock of its own.
 *
 * The point of doing it this way rather than unrolling the game into a state machine is that
 * game.c, intro.c, assets.c and platform.c are not touched at all. The gameplay code that was
 * matched against the original's PowerPC is the same code here.
 */
#include "libretro.h"
#include "core.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <pthread.h>
#endif

/* the game's main(), renamed by the build so it does not collide with the frontend's */
extern int apeiron_main(int argc, char **argv);

/* ---- the frontend's callbacks ---------------------------------------------------------------- */
static retro_environment_t        env_cb;
static retro_video_refresh_t      video_cb;
static retro_audio_sample_batch_t audio_cb;
static retro_input_poll_t         poll_cb;
static retro_input_state_t        input_cb;
static retro_log_printf_t         log_cb;

static char sys_dir[1024]  = ".";
static char game_dir[1024];
static bool have_game_dir;

/* ---- the handoff ----------------------------------------------------------------------------- */
/* turn 0 = the game thread may run, 1 = the frontend may run. */
static int      turn;
static bool     game_started, game_over;
static uint64_t frame_no;
static uint32_t framebuf[LR_W * LR_H];
static bool     have_frame;
static LrInput  input_now;

#if defined(_WIN32)
static CRITICAL_SECTION    mtx;
static CONDITION_VARIABLE  cnd;
static HANDLE              game_thread;
#  define LOCK()    EnterCriticalSection(&mtx)
#  define UNLOCK()  LeaveCriticalSection(&mtx)
#  define WAIT()    SleepConditionVariableCS(&cnd, &mtx, INFINITE)
#  define WAKE()    WakeAllConditionVariable(&cnd)
static DWORD WINAPI thread_entry(LPVOID p)
#else
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cnd = PTHREAD_COND_INITIALIZER;
static pthread_t       game_thread;
#  define LOCK()    pthread_mutex_lock(&mtx)
#  define UNLOCK()  pthread_mutex_unlock(&mtx)
#  define WAIT()    pthread_cond_wait(&cnd, &mtx)
#  define WAKE()    pthread_cond_broadcast(&cnd)
static void *thread_entry(void *p)
#endif
{
    (void)p;
    static char arg0[] = "apeiron";
    char *argv[1] = { arg0 };
    apeiron_main(1, argv);
    lr_exit(0);                      /* if main ever returns, it is over */
}

/* Called from the game thread: publish the frame and wait to be asked for another. */
void lr_frame(const uint32_t *pixels)
{
    LOCK();
    if (pixels) memcpy(framebuf, pixels, sizeof framebuf);
    have_frame = true;
    turn = 1;                        /* the frontend's go */
    WAKE();
    while (turn != 0) WAIT();        /* ... and park until it hands back */
    UNLOCK();
    frame_no++;
}

uint64_t lr_frame_no(void) { return frame_no; }

uint64_t lr_ticks(void)
{
    return (uint64_t)(frame_no * 1000.0 / LR_FPS);
}

void lr_delay(uint32_t ms)
{
    /* Time only passes by frames going out. A delay shorter than a frame still costs one, which
     * is what a 30fps frontend would do with it anyway. */
    uint64_t want = lr_ticks() + (ms ? ms : 1);
    int guard = 0;
    while (lr_ticks() < want && guard++ < 600) lr_frame(NULL);
}

void lr_exit(int code)
{
    (void)code;
    LOCK();
    game_over = true;
    turn = 1;
    WAKE();
    UNLOCK();
#if defined(_WIN32)
    ExitThread(0);
#else
    pthread_exit(NULL);
#endif
    for (;;) { }                     /* not reached; keeps the compiler happy about noreturn */
}

void lr_log(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (log_cb) log_cb(RETRO_LOG_INFO, "[apeiron] %s\n", buf);
    else        fprintf(stderr, "[apeiron] %s\n", buf);
}

const char *lr_system_dir(void)  { return sys_dir; }
const char *lr_content_dir(void) { return have_game_dir ? game_dir : NULL; }
const LrInput *lr_input(void)    { return &input_now; }

/* ---- the entry points ------------------------------------------------------------------------ */
RETRO_API void retro_set_environment(retro_environment_t cb)
{
    env_cb = cb;
    bool no_game = true;             /* the artwork comes from the system folder, so content is optional */
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)      { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)        { (void)cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb)            { poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb)          { input_cb = cb; }

RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof *info);
    info->library_name     = "Apeiron";
    info->library_version  = APEIRON_BUILD_NAME[0] ? APEIRON_BUILD_NAME : APEIRON_BUILD_STAMP;
    info->valid_extensions = "apeiron|zip";
    info->need_fullpath    = true;   /* we read a folder, not a buffer the frontend hands us */
    info->block_extract    = true;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
    memset(info, 0, sizeof *info);
    info->geometry.base_width   = LR_W;
    info->geometry.base_height  = LR_H;
    info->geometry.max_width    = LR_W;
    info->geometry.max_height   = LR_H;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps            = LR_FPS;
    info->timing.sample_rate    = LR_RATE;
}

RETRO_API void retro_init(void)
{
    const char *d = NULL;
    if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &d) && d)
        snprintf(sys_dir, sizeof sys_dir, "%s", d);
    struct retro_log_callback lg;
    if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &lg)) log_cb = lg.log;
#if defined(_WIN32)
    InitializeCriticalSection(&mtx);
    InitializeConditionVariable(&cnd);
#endif
}

RETRO_API void retro_deinit(void) { }

static const struct retro_input_descriptor descriptors[] = {
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Move left" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Move right" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Move up" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Move down" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Fire" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Fire" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Pause / choose" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,"Back to the title" },
    { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
         RETRO_DEVICE_ID_ANALOG_X, "Move" },
    { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
         RETRO_DEVICE_ID_ANALOG_Y, "Move" },
    { 0, 0, 0, 0, NULL }
};

RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (env_cb && !env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
        lr_log("the frontend will not give us XRGB8888");
        return false;
    }
    if (env_cb) env_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void *)descriptors);

    /* Content is optional. When there is some, its folder is one of the places the artwork is
     * looked for, so "load content -> pick anything inside your assets folder" works. */
    have_game_dir = false;
    if (game && game->path && game->path[0]) {
        snprintf(game_dir, sizeof game_dir, "%s", game->path);
        char *slash = strrchr(game_dir, '/');
#if defined(_WIN32)
        char *bslash = strrchr(game_dir, '\\');
        if (bslash > slash) slash = bslash;
#endif
        if (slash) { *slash = 0; have_game_dir = true; }
    }

    turn = 0;
    game_over = false;
    frame_no = 0;
#if defined(_WIN32)
    game_thread = CreateThread(NULL, 0, thread_entry, NULL, 0, NULL);
    if (!game_thread) return false;
#else
    if (pthread_create(&game_thread, NULL, thread_entry, NULL) != 0) return false;
#endif
    game_started = true;
    return true;
}

RETRO_API void retro_unload_game(void)
{
    /* The game thread only ever parks inside lr_frame(), so letting it run once more with the
     * core marked finished is enough to get it out; it exits the next time it tries to draw. */
    game_started = false;
}

RETRO_API void retro_run(void)
{
    if (poll_cb) poll_cb();

    /* One snapshot of the input per frame, read on this thread while the game thread is parked. */
    LrInput in;
    memset(&in, 0, sizeof in);
    if (input_cb) {
        for (unsigned i = 0; i < 16; i++)
            in.button[i] = input_cb(0, RETRO_DEVICE_JOYPAD, 0, i) != 0;
        in.lx = input_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X);
        in.ly = input_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y);
        in.rx = input_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
        in.ry = input_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);

        /* A mouse if there is one: RetroArch reports it as movement since the last poll, which is
         * exactly what the game wants. */
        in.mdx = input_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
        in.mdy = input_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
        in.mouse_down = input_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) != 0;

        /* And a touch screen, which arrives as -0x7fff..0x7fff across the viewport. */
        if (input_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED)) {
            int16_t x = input_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
            int16_t y = input_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
            in.px = (int)(((long)x + 0x8000) * LR_W / 0x10000);
            in.py = (int)(((long)y + 0x8000) * LR_H / 0x10000);
            in.pointer = true;
        }
    }

    LOCK();
    input_now = in;
    if (game_over) {
        UNLOCK();
        if (video_cb) video_cb(framebuf, LR_W, LR_H, LR_W * sizeof(uint32_t));
        return;
    }
    have_frame = false;
    turn = 0;                        /* the game's go */
    WAKE();
    while (turn != 1) WAIT();        /* ... until it has a frame for us */
    UNLOCK();

    if (video_cb) video_cb(framebuf, LR_W, LR_H, LR_W * sizeof(uint32_t));

    static int16_t mix[(int)(LR_RATE / 30) * 2 + 64];
    int frames = (int)(LR_RATE / LR_FPS);
    lr_audio_render(mix, frames);
    if (audio_cb) audio_cb(mix, (size_t)frames);
}

RETRO_API void retro_reset(void) { }
RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }
RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(void *d, size_t n) { (void)d; (void)n; return false; }
RETRO_API bool retro_unserialize(const void *d, size_t n) { (void)d; (void)n; return false; }
RETRO_API void retro_cheat_reset(void) { }
RETRO_API void retro_cheat_set(unsigned i, bool on, const char *code) { (void)i; (void)on; (void)code; }
RETRO_API bool retro_load_game_special(unsigned t, const struct retro_game_info *i, size_t n)
{ (void)t; (void)i; (void)n; return false; }
RETRO_API unsigned retro_get_region(void) { return 0; }   /* RETRO_REGION_NTSC */
RETRO_API void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
RETRO_API size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
