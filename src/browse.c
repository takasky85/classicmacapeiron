/*
 * browse.c - finding the artwork when the game cannot.
 *
 * On a desktop this is a last resort behind the system's own folder picker. On Android it is the
 * only way: SDL has no dialog backend there, and the Storage Access Framework hands back content
 * URIs rather than paths, which is no use to a game that opens files with fopen. So the browser
 * is drawn here, in the game's own frame buffer, with the font built into the executable - it has
 * to work before a single asset exists.
 *
 * It is driven by whatever is to hand: a d-pad or stick, a mouse, a touch screen, or the keyboard.
 * A Google TV remote has the first of those and nothing else, which is why the list is big, the
 * highlight is obvious, and nothing depends on hitting a small target.
 */
#include "apeiron.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ENTRIES 512
#define ROWS        18
#define ROW_H       11
#define LIST_Y      96
#define LIST_X      40

typedef struct { char name[260]; bool is_dir; } Entry;   /* matches DirScan.name */

static Entry entries[MAX_ENTRIES];
static int   n_entries;
static int   read_err;               /* why the last read_dir could not open its folder; 0 if it did */

static int cmp_entry(const void *a, const void *b)
{
    const Entry *x = a, *y = b;
    if (x->is_dir != y->is_dir) return x->is_dir ? -1 : 1;   /* folders first */
    return strcasecmp(x->name, y->name);
}

/* Does this folder hold the artwork? The same test the rest of the game uses. */
static bool is_assets(const char *dir)
{
    char p[1200];
    snprintf(p, sizeof p, "%s/spr/shooter.bmp", dir);
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static void read_dir(const char *path)
{
    n_entries = 0;
    read_err = 0;
    DirScan s;
    if (!dir_open(&s, path)) { read_err = s.err ? s.err : -1; return; }
    while (dir_next(&s) && n_entries < MAX_ENTRIES) {
        if (!strcmp(s.name, ".")) continue;
        if (s.name[0] == '.' && strcmp(s.name, "..")) continue;   /* skip hidden, keep .. */
        char full[1400];
        snprintf(full, sizeof full, "%s/%s", path, s.name);
        if (!dir_is_dir(full)) continue;         /* only folders are worth showing */
        snprintf(entries[n_entries].name, sizeof entries[0].name, "%s", s.name);
        entries[n_entries].is_dir = true;
        n_entries++;
    }
    dir_close(&s);
    qsort(entries, (size_t)n_entries, sizeof entries[0], cmp_entry);
}

/* Where to start looking. Android keeps the user's downloads in a couple of well-known places,
 * and a sideloaded build reaches them with ordinary file calls as long as the manifest asked for
 * storage; the rest are for a desktop running this as a fallback. */
static const char *candidates[] = {
    "/sdcard/Download", "/storage/emulated/0/Download", "/sdcard", "/storage/emulated/0",
    "/storage", NULL /* $HOME, filled in below */, "/"
};
#define N_CANDIDATES (int)(sizeof candidates / sizeof candidates[0])

/* The best place we can open at this moment, and how good it is - lower is better, and
 * N_CANDIDATES means nothing on the list would open.
 *
 * "At this moment" is the point of returning the rank. The storage permission is asked for while
 * the game is starting and nothing waits on it, so the first call here often runs before it has
 * been granted: every candidate under /sdcard fails to open and the browser would otherwise sit
 * at "/" showing the root of an Android system, with the player's downloads unreachable and no
 * hint as to why. Asking again as the browser runs costs an opendir and fixes that by itself the
 * moment the permission lands. */
int assets_candidate_count(void) { return N_CANDIDATES; }
const char *assets_candidate(int i)
{
    if (i < 0 || i >= N_CANDIDATES) return NULL;
    if (i == 5) candidates[5] = getenv("HOME");
    return candidates[i];
}

static const char *best_start(int *rank)
{
    candidates[5] = getenv("HOME");
    for (int i = 0; i < N_CANDIDATES; i++) {
        if (!candidates[i]) continue;
        DirScan sc;
        if (dir_open(&sc, candidates[i])) { dir_close(&sc); if (rank) *rank = i; return candidates[i]; }
    }
    if (rank) *rank = N_CANDIDATES;
    return "/";
}

static void parent_of(char *path)
{
    char *slash = strrchr(path, '/');
    if (!slash) return;
    if (slash == path) { path[1] = 0; return; }    /* "/x" -> "/" */
    *slash = 0;
    if (!path[0]) strcpy(path, "/");
}

/* The browser's own colours, because the game's palette is not loaded yet. */
enum { C_BG = 1, C_INK = 2, C_DIM = 3, C_SEL = 4, C_SELINK = 5, C_TITLE = 6, C_OK = 7 };

static void browse_palette(void)
{
    Rgb c[256];
    memset(c, 0, sizeof c);
    Rgb v[] = {
        { 0, 0, 0 },                                      /* 0 black */
        { 0x1000, 0x1800, 0x2600 },                       /* background */
        { 0xE800, 0xE800, 0xE800 },                       /* text */
        { 0x7800, 0x8000, 0x8800 },                       /* dim text */
        { 0x2800, 0x5000, 0x3800 },                       /* the selected row */
        { 0xFFFF, 0xFFFF, 0xFFFF },                       /* text on it */
        { 0x60FF, 0xD000, 0x80FF },                       /* the heading */
        { 0xFFFF, 0xD800, 0x3000 },                       /* "this is the one" */
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) c[i] = v[i];
    set_monitor_clut(c, true);
}

/* One frame of the browser. */
static void draw(const char *path, int sel, int top, bool here_is_it)
{
    fill_rect(0, 0, SCREEN_W, SCREEN_H, C_BG);
    mini_text2x(LIST_X, 20, "Where is the Apeiron artwork?", C_TITLE);
    mini_text(LIST_X, 46,
              "This build has none of its own - make a folder with tools/extract_assets.py", C_DIM);
    mini_text(LIST_X, 58, "and find it here. The folder you want is the one called 'assets'.", C_DIM);

    char shown[110];
    snprintf(shown, sizeof shown, "%s", path);
    if ((int)strlen(shown) > 88) {              /* keep the tail, which is the useful end */
        memmove(shown, shown + strlen(shown) - 88, 89);
        shown[0] = shown[1] = shown[2] = '.';
    }
    mini_text(LIST_X, 78, shown, C_INK);

    for (int i = 0; i < ROWS; i++) {
        int idx = top + i;
        if (idx >= n_entries) break;
        int y = LIST_Y + i * ROW_H;
        bool on = idx == sel;
        if (on) fill_rect(LIST_X - 6, y - 2, SCREEN_W - 2 * LIST_X + 12, ROW_H, C_SEL);
        char line[300];
        snprintf(line, sizeof line, "%s%s", entries[idx].name,
                 strcmp(entries[idx].name, "..") ? "/" : "");
        mini_text(LIST_X, y, line, on ? C_SELINK : C_INK);
    }
    if (!n_entries) {
        if (read_err) {
            char why[120];
            snprintf(why, sizeof why, "(cannot open this folder: %s)", dir_why(read_err));
            mini_text(LIST_X, LIST_Y, why, C_DIM);
            mini_text(LIST_X, LIST_Y + 14, "Press left or right to see what the game is allowed to read.", C_DIM);
        } else {
            mini_text(LIST_X, LIST_Y, "(no folders in here)", C_DIM);
        }
    }

    if (here_is_it) {
        fill_rect(LIST_X - 6, SCREEN_H - 54, SCREEN_W - 2 * LIST_X + 12, 14, C_SEL);
        mini_text(LIST_X, SCREEN_H - 51, "This folder has the artwork - press Start, or click here,"
                                         " to use it", C_OK);
    }
    mini_text(LIST_X, SCREEN_H - 30,
              "d-pad or arrows to move    A / Return opens a folder    B / Esc goes up", C_DIM);
    mini_text(LIST_X, SCREEN_H - 18,
              "left/right or D: what the game can see        Q gives up", C_DIM);
    set_entries();
    present();
}

/* Run the browser until the player picks a folder holding the artwork, or gives up.
 * Returns true and fills `out` on success. */
bool assets_browse(char *out, size_t n)
{
    /* Nobody is at the screen - a CI runner, a headless machine - so there is no point drawing
     * a folder list and waiting for a choice that cannot come. Say so and let the caller give up
     * tidily, which is what the release workflow checks the game does. */
    if (plat_headless()) { diag_note("apeiron: no display - not opening the browser"); return false; }

    char path[1200];
    int rank;
    snprintf(path, sizeof path, "%s", best_start(&rank));
    read_dir(path);
    browse_palette();

    int sel = 0, top = 0;
    bool held = false;                  /* so one push of the stick moves one row */
    int repeat = 0;
    bool moved = false;                 /* has the player navigated anywhere themselves yet? */
    int recheck = 0;

    for (;;) {
        /* Until the player goes somewhere of their own accord, keep an eye out for a better
         * starting point than the one we could reach when we opened - which is how the storage
         * permission being granted a second or two in gets noticed. Twice a second is plenty.
         * Once they have moved, this stops: being yanked out of the folder you are looking at
         * would be worse than starting in the wrong one. */
        if (!moved && rank > 0 && ++recheck >= 30) {
            recheck = 0;
            int r2;
            const char *p2 = best_start(&r2);
            if (r2 < rank) {
                rank = r2;
                snprintf(path, sizeof path, "%s", p2);
                read_dir(path);
                sel = top = 0;
                diag_note("apeiron: storage opened up - starting at '%s'", path);
            }
        }

        bool here = is_assets(path);
        draw(path, sel, top, here);

        /* ---- what the player did ---- */
        int dv = 0;
        bool open_it = false, up = false, take = false;

        char k;
        while (take_key(&k)) { }
        float px = 0, py = 0;
        pad_move(&px, &py);
        bool kb_up = key_down(SDL_SCANCODE_UP) || key_down(SDL_SCANCODE_W);
        bool kb_dn = key_down(SDL_SCANCODE_DOWN) || key_down(SDL_SCANCODE_S);
        bool dir_up = kb_up || py < -0.4f;
        bool dir_dn = kb_dn || py > 0.4f;

        if (dir_up || dir_dn) {
            if (!held || ++repeat > 6) { dv = dir_up ? -1 : 1; repeat = held ? 0 : repeat; }
            held = true;
        } else {
            held = false;
            repeat = 0;
        }

        if (key_down(SDL_SCANCODE_RETURN) || key_down(SDL_SCANCODE_KP_ENTER) || pad_fire())
            open_it = true;
        /* AC_BACK is where Android sends a TV remote's and a phone's Back button, and it is the
         * only "go up" a remote has: it has no Escape and no B. */
        if (key_down(SDL_SCANCODE_ESCAPE) || key_down(SDL_SCANCODE_B) ||
            key_down(SDL_SCANCODE_AC_BACK)) up = true;
        if (pad_start() || key_down(SDL_SCANCODE_SPACE)) take = true;
        bool give_up = key_down(SDL_SCANCODE_Q);
        /* Left and right do nothing else in here, and on a TV remote they are the only spare
         * keys there are - no Escape, no letters, no mouse. So they open the diagnostics. */
        bool want_diag = key_down(SDL_SCANCODE_D) || key_down(SDL_SCANCODE_LEFT) ||
                         key_down(SDL_SCANCODE_RIGHT) || px < -0.6f || px > 0.6f;

        int mx, my;
        if (take_click(&mx, &my)) {
            if (my >= SCREEN_H - 24) {
                want_diag = true;                      /* the footer line that says so */
            } else if (here && my >= SCREEN_H - 56 && my < SCREEN_H - 38) {
                take = true;
            } else if (my >= LIST_Y - 2) {
                int row = (my - (LIST_Y - 2)) / ROW_H;
                if (row >= 0 && row < ROWS && top + row < n_entries) {
                    sel = top + row;
                    open_it = true;
                }
            }
        }

        if (dv) {
            sel += dv;
            if (sel < 0) sel = 0;
            if (sel >= n_entries) sel = n_entries ? n_entries - 1 : 0;
            if (sel < top) top = sel;
            if (sel >= top + ROWS) top = sel - ROWS + 1;
        }

        if (take && here) {
            snprintf(out, n, "%s", path);
            return true;
        }
        if (open_it && sel < n_entries) {
            moved = true;
            if (!strcmp(entries[sel].name, "..")) {
                parent_of(path);
            } else {
                size_t len = strlen(path);
                snprintf(path + len, sizeof path - len, "%s%s",
                         len && path[len - 1] == '/' ? "" : "/", entries[sel].name);
            }
            read_dir(path);
            sel = top = 0;
            /* Walking into the artwork folder itself is the usual way to arrive, so take it. */
            if (is_assets(path)) {
                snprintf(out, n, "%s", path);
                return true;
            }
            wait_ms(120);                       /* so one press is not read as several */
        }
        if (up) {
            moved = true;
            parent_of(path);
            read_dir(path);
            sel = top = 0;
            wait_ms(120);
        }
        if (want_diag) {
            diag_screen();
            browse_palette();                 /* the report had the screen's colours */
            wait_ms(150);
        }
        if (give_up) return false;
        pump();
        wait_ms(16);
    }
}
