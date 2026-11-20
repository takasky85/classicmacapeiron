/*
 * core.h - what the SDL shim and the core say to each other.
 *
 * Two threads, strictly taking turns. The game thread is the port's own main(), running exactly
 * as it does on the desktop: blocking loops, present() at the bottom of each one. The frontend
 * thread is whatever called retro_run(). Only one of the two ever runs at a time, so nothing
 * here needs locking beyond the handoff itself.
 */
#ifndef APEIRON_CORE_H
#define APEIRON_CORE_H

#include <stdint.h>
#include <stdbool.h>

#define LR_W   640
#define LR_H   480
#define LR_FPS 30.0
#define LR_RATE 44100

/* ---- called from the game thread ------------------------------------------------------------ */

/* Hand over the finished frame and block until the frontend asks for the next one. This is what
 * present() becomes, and it is the only place the game thread ever stops. */
void lr_frame(const uint32_t *pixels);

/* The clock the game sees. It advances by one frame's worth every time lr_frame() returns, so
 * SDL_GetTicks() moves in step with the frontend rather than with the wall. */
uint64_t lr_ticks(void);

/* How many frames have gone out. The shim reads the input once per frame rather than once per
 * poll, and this is how it tells one frame from the next. */
uint64_t lr_frame_no(void);

/* SDL_Delay. A delay is a request to let time pass, and the only way time passes here is a frame
 * going out, so this hands over frames until enough of them have. */
void lr_delay(uint32_t ms);

/* The game called exit(). Marks the core finished and parks the thread for good. */
void lr_exit(int code) __attribute__((noreturn));

void lr_log(const char *fmt, ...);
const char *lr_system_dir(void);      /* RetroArch's system folder, or "." */
const char *lr_content_dir(void);     /* the folder the loaded content sits in, or NULL */

/* ---- input, as of the last retro_run() ------------------------------------------------------- */
/* The RetroPad button numbers the shim uses, spelled out so it needs no libretro header. */
enum { RETRO_PAD_B = 0, RETRO_PAD_Y, RETRO_PAD_SELECT, RETRO_PAD_START, RETRO_PAD_UP,
       RETRO_PAD_DOWN, RETRO_PAD_LEFT, RETRO_PAD_RIGHT, RETRO_PAD_A, RETRO_PAD_X,
       RETRO_PAD_L, RETRO_PAD_R };

typedef struct {
    bool     button[16];      /* RETRO_DEVICE_ID_JOYPAD_* */
    int16_t  lx, ly, rx, ry;  /* analog sticks, -0x8000..0x7fff */
    int      mdx, mdy;        /* mouse movement since the last frame, in game pixels */
    bool     mouse_down;
    int      px, py;          /* pointer, in game pixels, when pressed */
    bool     pointer;
} LrInput;

const LrInput *lr_input(void);

/* ---- audio ----------------------------------------------------------------------------------
 * The shim owns the voices, because the game drives them through SDL's audio-stream calls. The
 * core asks for a frame's worth of mixed stereo once per retro_run().
 */
void lr_audio_render(int16_t *out, int frames);

#endif
