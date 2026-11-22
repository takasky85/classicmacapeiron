/*
 * diag.c - the diagnostics screen.
 *
 * On a desktop, a build that will not start can be run from a terminal and asked what went wrong.
 * On a Google TV box there is no terminal, no console and no adb unless you go and set it up, and
 * the only thing the machine will tell you is that the app is not on the screen. So the game says
 * it itself, on the screen, in the font built into the executable - which needs no artwork and so
 * works in exactly the situation where nothing else does.
 *
 * Two parts:
 *
 *   diag_note()   is SDL_Log plus a copy kept in a ring buffer, so the startup breadcrumbs are
 *                 still there to be read later rather than only in a log nobody can reach.
 *   diag_screen() draws those notes under a report worked out on the spot: where the game looked
 *                 for its artwork, which of those places it can actually open and why not when it
 *                 cannot, and what SDL thinks the machine is.
 *
 * The "why not" is the part that earns its keep. A folder that will not open because the storage
 * permission has not been granted (EACCES) and one that is simply not there (ENOENT) look the
 * same from the sofa and want completely different things done about them.
 */
#include "apeiron.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define NOTES     24
#define NOTE_LEN  110

static char notes[NOTES][NOTE_LEN];
static int  n_notes;                 /* total ever written; the ring holds the last NOTES of them */

void diag_note(const char *fmt, ...)
{
    char line[NOTE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);

    snprintf(notes[n_notes % NOTES], NOTE_LEN, "%s", line);
    n_notes++;
    SDL_Log("%s", line);             /* logcat, or the terminal, as before */
}

static const char *arch_name(void)
{
#if defined(__aarch64__)
    return "arm64 (aarch64)";
#elif defined(__arm__)
    return "arm 32-bit (armeabi-v7a)";
#elif defined(__x86_64__)
    return "x86_64";
#elif defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

static const char *os_name(void)
{
#if defined(__ANDROID__)
    return "Android";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(_WIN32)
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#else
    return "unknown";
#endif
}

/* ---- the report ------------------------------------------------------------------------------ */

#define MAX_ROWS 40
static char rows[MAX_ROWS][NOTE_LEN];
static int  n_rows;

static void row(const char *fmt, ...)
{
    if (n_rows >= MAX_ROWS) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(rows[n_rows], NOTE_LEN, fmt, ap);
    va_end(ap);
    n_rows++;
}

/* Can this folder be opened, and if not, what did the system say? */
static void probe(const char *label, const char *path)
{
    if (!path || !path[0]) { row("  %-30s (not set)", label); return; }

    DIR *d = opendir(path);
    if (!d) {
        const char *why = strerror(errno);
        if (errno == EACCES) why = "permission denied - storage not granted yet";
        else if (errno == ENOENT) why = "no such folder on this machine";
        row("  %-30s %s", label, why);
        return;
    }
    int files = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        files++;
    }
    closedir(d);

    char probe_path[1200];
    snprintf(probe_path, sizeof probe_path, "%s/spr/shooter.bmp", path);
    FILE *f = fopen(probe_path, "rb");
    bool art = f != NULL;
    if (f) fclose(f);

    row("  %-30s readable, %d item%s%s", label, files, files == 1 ? "" : "s",
        art ? "  <- THE ARTWORK IS HERE" : "");
}

static void build_report(void)
{
    n_rows = 0;

#if defined(APEIRON_BUILD_STAMP)
    if (APEIRON_BUILD_NAME[0])
        row("build %s, stamped %s", APEIRON_BUILD_NAME, APEIRON_BUILD_STAMP);
    else
        row("build stamped %s", APEIRON_BUILD_STAMP);
#else
    row("build from a working copy, unstamped");
#endif
    row("%s, %s", os_name(), arch_name());

    const char *base = SDL_GetBasePath();
    row("SDL_GetBasePath  %s", base ? base : "(null)");
    char *pref = SDL_GetPrefPath("apeiron-sdl3", "apeiron");
    row("SDL_GetPrefPath  %s", pref ? pref : "(null)");
    if (pref) SDL_free(pref);

    row("");
    row("Where the artwork was looked for:");
    for (int i = 0; i < assets_candidate_count(); i++) {
        const char *p = assets_candidate(i);
        if (p) probe(p, p);
    }
    probe("beside the game", "assets");

    row("");
    row("Input: %s", pad_present() ? pad_name() : "no gamepad found");
}

/* ---- the screen ------------------------------------------------------------------------------ */

enum { D_BG = 1, D_INK = 2, D_DIM = 3, D_TITLE = 4, D_WARN = 5 };

static void diag_palette(void)
{
    Rgb c[256];
    memset(c, 0, sizeof c);
    c[D_BG]    = (Rgb){ 0x0800, 0x0C00, 0x1800 };
    c[D_INK]   = (Rgb){ 0xE000, 0xE000, 0xE000 };
    c[D_DIM]   = (Rgb){ 0x7000, 0x7800, 0x8000 };
    c[D_TITLE] = (Rgb){ 0x60FF, 0xD000, 0x80FF };
    c[D_WARN]  = (Rgb){ 0xFFFF, 0xD800, 0x3000 };
    set_monitor_clut(c, true);
}

void diag_screen(void)
{
    build_report();
    diag_palette();

    Uint64 until = SDL_GetTicks() + 300000;     /* so an unattended machine cannot sit here */
    for (;;) {
        fill_rect(0, 0, SCREEN_W, SCREEN_H, D_BG);
        mini_text2x(16, 12, "Apeiron diagnostics", D_TITLE);

        int y = 38;
        for (int i = 0; i < n_rows && y < SCREEN_H - 90; i++, y += 10) {
            const char *s = rows[i];
            size_t len = strlen(s);
            uint8_t ink = D_INK;
            if (strstr(s, "ARTWORK IS HERE") || strstr(s, "permission denied")) ink = D_WARN;
            else if (len && s[len - 1] == ':') ink = D_TITLE;   /* a heading, not a fact */
            mini_text(16, y, s, ink);
        }

        y += 6;
        mini_text(16, y, "What happened on the way in:", D_TITLE);
        y += 12;
        int first = n_notes > NOTES ? n_notes - NOTES : 0;
        for (int i = first; i < n_notes && y < SCREEN_H - 24; i++, y += 10)
            mini_text(16, y, notes[i % NOTES], D_DIM);

        mini_text(16, SCREEN_H - 14, "any key, any button or a tap goes back", D_TITLE);
        set_entries();
        present();

        char k;
        if (take_key(&k) || take_click_only() || pad_fire() || pad_start()) break;
        if (key_down(SDL_SCANCODE_RETURN) || key_down(SDL_SCANCODE_ESCAPE) ||
            key_down(SDL_SCANCODE_SPACE)  || key_down(SDL_SCANCODE_AC_BACK)) break;
        if (SDL_GetTicks() > until) break;
        pump();
        wait_ms(16);
    }
    /* Whoever called this drew the screen before it and will draw it again. */
}
