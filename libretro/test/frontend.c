/*
 * frontend.c - a libretro frontend just big enough to drive the core without RetroArch.
 *
 * It loads the shared object, hands it the six callbacks a frontend must supply, calls retro_run
 * in a loop and writes the frames out as PNGs. That is enough to check everything mechanical -
 * does the core load, does it produce the right pixels, does input reach the game, does audio
 * come out at the right rate - on a machine with no display and no Android.
 *
 *   frontend <core.so> <system-dir> [frames] [out-dir]
 *
 * Input is scripted: see script() below. The PNG writer uses stored deflate blocks so that this
 * needs no zlib; the files are large and perfectly valid.
 */
#include "../libretro.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char sysdir[1024];
static unsigned frame_no;
static const uint32_t *last_frame;
static unsigned last_w, last_h;
static long audio_frames;
static double audio_peak;

/* ---- a PNG writer with no dependencies ------------------------------------------------------- */
static uint32_t crc_table[256];
static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
static uint32_t crc(const uint8_t *b, size_t n, uint32_t c)
{
    c = c ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static void be32(uint8_t *p, uint32_t v)
{ p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void chunk(FILE *f, const char *tag, const uint8_t *data, size_t n)
{
    uint8_t hdr[8];
    be32(hdr, (uint32_t)n);
    memcpy(hdr + 4, tag, 4);
    fwrite(hdr, 1, 8, f);
    if (n) fwrite(data, 1, n, f);
    uint32_t c = crc((const uint8_t *)tag, 4, 0);
    if (n) c = crc(data, n, c);
    uint8_t cb[4];
    be32(cb, c);
    fwrite(cb, 1, 4, f);
}

static void write_png(const char *path, const uint32_t *px, unsigned w, unsigned h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    static const uint8_t sig[8] = { 137, 'P', 'N', 'G', '\r', '\n', 26, '\n' };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, w); be32(ihdr + 4, h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;   /* 8-bit truecolour */
    chunk(f, "IHDR", ihdr, sizeof ihdr);

    size_t raw_n = (size_t)h * (1 + (size_t)w * 3);
    uint8_t *raw = malloc(raw_n);
    size_t o = 0;
    for (unsigned y = 0; y < h; y++) {
        raw[o++] = 0;                                   /* filter: none */
        for (unsigned x = 0; x < w; x++) {
            uint32_t c = px[y * w + x];
            raw[o++] = (c >> 16) & 255; raw[o++] = (c >> 8) & 255; raw[o++] = c & 255;
        }
    }
    /* zlib stream, stored blocks only */
    size_t cap = raw_n + raw_n / 65535 * 5 + 64;
    uint8_t *z = malloc(cap);
    size_t zo = 0;
    z[zo++] = 0x78; z[zo++] = 0x01;
    size_t left = raw_n, pos = 0;
    while (left) {
        size_t n = left > 65535 ? 65535 : left;
        z[zo++] = (left == n) ? 1 : 0;
        z[zo++] = n & 255; z[zo++] = (n >> 8) & 255;
        z[zo++] = (~n) & 255; z[zo++] = ((~n) >> 8) & 255;
        memcpy(z + zo, raw + pos, n);
        zo += n; pos += n; left -= n;
    }
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    be32(z + zo, (b << 16) | a);
    zo += 4;
    chunk(f, "IDAT", z, zo);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}

/* ---- the callbacks --------------------------------------------------------------------------- */
static void RETRO_CALLCONV log_printf(enum retro_log_level lvl, const char *fmt, ...)
{
    (void)lvl;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static bool RETRO_CALLCONV environment(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = sysdir;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = sysdir;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *(const enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = log_printf;
        return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        return true;
    default:
        return false;
    }
}

static void RETRO_CALLCONV video_refresh(const void *data, unsigned w, unsigned h, size_t pitch)
{
    (void)pitch;
    last_frame = (const uint32_t *)data;
    last_w = w; last_h = h;
}

static size_t RETRO_CALLCONV audio_batch(const int16_t *data, size_t frames)
{
    audio_frames += (long)frames;
    for (size_t i = 0; i < frames * 2; i++) {
        double v = data[i] / 32768.0;
        if (v < 0) v = -v;
        if (v > audio_peak) audio_peak = v;
    }
    return frames;
}

static void RETRO_CALLCONV input_poll(void) { }

/* A scripted player: click to get past the intro, click Play, then sweep and fire. */
static int16_t RETRO_CALLCONV input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)port; (void)index;
    unsigned f = frame_no;
    if (device == RETRO_DEVICE_POINTER) {
        /* a tap at 1s to skip the intro, and one on "Play Apeiron" at 25s */
        /* One tap at a second to get past the intro, then a tap on "Play Apeiron" every twenty
         * seconds - the title screen drops into its attract mode and back, so a single tap at a
         * fixed moment is not reliable enough to leave the run depending on. */
        bool tap1 = f > 30 && f < 36;
        bool tap2 = f >= 300 && (f % 90) < 4;
        if (id == RETRO_DEVICE_ID_POINTER_PRESSED) return (tap1 || tap2) ? 1 : 0;
        if (id == RETRO_DEVICE_ID_POINTER_X) return tap2 ? (int16_t)((462 * 0x10000 / 640) - 0x8000) : 0;
        if (id == RETRO_DEVICE_ID_POINTER_Y) return tap2 ? (int16_t)(( 22 * 0x10000 / 480) - 0x8000) : 0;
        return 0;
    }
    if (device == RETRO_DEVICE_ANALOG && f > 800) {
        /* sweep the shooter side to side once in play */
        if (id == RETRO_DEVICE_ID_ANALOG_X) return (int16_t)(((f / 45) % 2) ? 26000 : -26000);
        return 0;
    }
    if (device == RETRO_DEVICE_JOYPAD && f > 800) {
        if (id == RETRO_DEVICE_ID_JOYPAD_B) return (f / 5) % 2;        /* fire */
    }
    return 0;
}

#define SYM(n) do { *(void **)(&n) = dlsym(h, #n); \
                    if (!n) { fprintf(stderr, "core has no %s\n", #n); return 1; } } while (0)

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <core.so> <system-dir> [frames] [out-dir]\n", argv[0]);
        return 2;
    }
    crc_init();
    snprintf(sysdir, sizeof sysdir, "%s", argv[2]);
    unsigned want = argc > 3 ? (unsigned)atoi(argv[3]) : 900;
    const char *out = argc > 4 ? argv[4] : NULL;

    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }

    void (*retro_set_environment)(retro_environment_t);
    void (*retro_set_video_refresh)(retro_video_refresh_t);
    void (*retro_set_audio_sample)(retro_audio_sample_t);
    void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*retro_set_input_poll)(retro_input_poll_t);
    void (*retro_set_input_state)(retro_input_state_t);
    void (*retro_init)(void);
    void (*retro_deinit)(void);
    unsigned (*retro_api_version)(void);
    void (*retro_get_system_info)(struct retro_system_info *);
    void (*retro_get_system_av_info)(struct retro_system_av_info *);
    bool (*retro_load_game)(const struct retro_game_info *);
    void (*retro_unload_game)(void);
    void (*retro_run)(void);

    SYM(retro_set_environment); SYM(retro_set_video_refresh); SYM(retro_set_audio_sample);
    SYM(retro_set_audio_sample_batch); SYM(retro_set_input_poll); SYM(retro_set_input_state);
    SYM(retro_init); SYM(retro_deinit); SYM(retro_api_version);
    SYM(retro_get_system_info); SYM(retro_get_system_av_info);
    SYM(retro_load_game); SYM(retro_unload_game); SYM(retro_run);

    printf("api version %u\n", retro_api_version());
    struct retro_system_info si;
    retro_get_system_info(&si);
    printf("core: %s %s  extensions=%s  need_fullpath=%d\n",
           si.library_name, si.library_version, si.valid_extensions, si.need_fullpath);

    retro_set_environment(environment);
    retro_set_video_refresh(video_refresh);
    retro_set_audio_sample(NULL);
    retro_set_audio_sample_batch(audio_batch);
    retro_set_input_poll(input_poll);
    retro_set_input_state(input_state);
    retro_init();

    struct retro_system_av_info av;
    retro_get_system_av_info(&av);
    printf("av: %ux%u  %.2f fps  %.0f Hz  aspect %.3f\n",
           av.geometry.base_width, av.geometry.base_height,
           av.timing.fps, av.timing.sample_rate, av.geometry.aspect_ratio);

    if (!retro_load_game(NULL)) { fprintf(stderr, "retro_load_game failed\n"); return 1; }

    unsigned blank = 0;
    for (frame_no = 0; frame_no < want; frame_no++) {
        last_frame = NULL;
        retro_run();
        if (!last_frame) { fprintf(stderr, "frame %u: no video\n", frame_no); blank++; continue; }
        unsigned long sum = 0;
        for (unsigned i = 0; i < last_w * last_h; i += 97) sum += last_frame[i] & 0xFFFFFF;
        if (!sum) blank++;
        if (out && frame_no % 150 == 0) {
            char p[1200];
            snprintf(p, sizeof p, "%s/f_%05u.png", out, frame_no);
            write_png(p, last_frame, last_w, last_h);
            printf("  frame %5u  %ux%u  checksum %08lx  -> %s\n", frame_no, last_w, last_h, sum, p);
        }
    }
    printf("ran %u frames, %u of them blank; audio %ld frames (%.2f s at %.0f Hz), peak %.3f\n",
           want, blank, audio_frames, audio_frames / av.timing.sample_rate,
           av.timing.sample_rate, audio_peak);
    retro_unload_game();
    retro_deinit();
    return 0;
}
