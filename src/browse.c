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

static bool is_zip_name(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, ".zip");
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
        bool isd = dir_is_dir(full);
        if (!isd && !is_zip_name(s.name)) continue;   /* folders, and the game's zip, are worth showing */
        snprintf(entries[n_entries].name, sizeof entries[0].name, "%s", s.name);
        entries[n_entries].is_dir = isd;
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
static char note[160];               /* a line under the list: why the last choice was refused */

static void draw(const char *path, int sel, int top, bool here_is_it)
{
    fill_rect(0, 0, SCREEN_W, SCREEN_H, C_BG);
    mini_text2x(LIST_X, 20, "Where is your copy of Apeiron?", C_TITLE);
    mini_text(LIST_X, 46,
              "Find the Apeiron zip you downloaded (the Mac 1.0.3 one) and open it - the game", C_DIM);
    mini_text(LIST_X, 58, "makes its own artwork from it. A folder of ready-made artwork also works.", C_DIM);

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
                 entries[idx].is_dir && strcmp(entries[idx].name, "..") ? "/" : "");
        mini_text(LIST_X, y, line, on ? C_SELINK : C_INK);
    }
    if (!n_entries) {
        if (read_err) {
            char why[120];
            snprintf(why, sizeof why, "(cannot open this folder: %s)", dir_why(read_err));
            mini_text(LIST_X, LIST_Y, why, C_DIM);
            mini_text(LIST_X, LIST_Y + 14, "Press left or right to see what the game is allowed to read.", C_DIM);
        } else {
            mini_text(LIST_X, LIST_Y, "(no folders or zip files in here)", C_DIM);
        }
    }

    if (here_is_it) {
        fill_rect(LIST_X - 6, SCREEN_H - 54, SCREEN_W - 2 * LIST_X + 12, 14, C_SEL);
        mini_text(LIST_X, SCREEN_H - 51, "This folder has the artwork - press Start, or click here,"
                                         " to use it", C_OK);
    }
    if (note[0]) mini_text(LIST_X, SCREEN_H - 66, note, C_OK);
    mini_text(LIST_X, SCREEN_H - 30,
              "d-pad or arrows to move    A / Return opens a folder or the zip    B / Esc goes up", C_DIM);
    mini_text(LIST_X, SCREEN_H - 18,
              "left/right or D: what the game can see        Q gives up", C_DIM);
    set_entries();
    present();
}

/* Run the browser until the player picks a folder holding the artwork or the game's zip, or gives
 * up. Returns 1 for a folder, 2 for the zip - `out` has the path either way - and 0 for neither. */
int assets_browse(char *out, size_t n)
{
    /* Nobody is at the screen - a CI runner, a headless machine - so there is no point drawing
     * a folder list and waiting for a choice that cannot come. Say so and let the caller give up
     * tidily, which is what the release workflow checks the game does. */
    if (plat_headless()) { diag_note("apeiron: no display - not opening the browser"); return 0; }

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
            return 1;
        }
        if (open_it && sel < n_entries && !entries[sel].is_dir) {
            char full[1500];
            snprintf(full, sizeof full, "%s%s%s", path,
                     path[0] && path[strlen(path) - 1] == '/' ? "" : "/", entries[sel].name);
            if (convert_zip_ok(full)) {
                snprintf(out, n, "%s", full);
                return 2;
            }
            snprintf(note, sizeof note, "That zip is not the Apeiron download - it has no game files in it.");
            wait_ms(200);
            open_it = false;
        }
        if (open_it && sel < n_entries) {
            moved = true;
            note[0] = 0;
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
                return 1;
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
        if (give_up) return 0;
        pump();
        wait_ms(16);
    }
}


/* ---- making the artwork ------------------------------------------------------------------------ */

/* "Found this - shall I make the artwork from it?" Asked rather than just done, so that a download
 * that happens to be in the folder is never unpacked without the player knowing why. */
bool assets_offer_zip(const char *zip)
{
    if (plat_headless()) return false;
    browse_palette();
    char shown[110];
    snprintf(shown, sizeof shown, "%s", zip);
    if ((int)strlen(shown) > 88) {
        memmove(shown, shown + strlen(shown) - 88, 89);
        shown[0] = shown[1] = shown[2] = '.';
    }
    wait_ms(300);
    char k;
    while (take_key(&k)) { }
    int mx, my;
    while (take_click(&mx, &my)) { }
    for (;;) {
        fill_rect(0, 0, SCREEN_W, SCREEN_H, C_BG);
        mini_text2x(LIST_X, 40, "Found your copy of Apeiron", C_TITLE);
        mini_text(LIST_X, 90, shown, C_INK);
        mini_text(LIST_X, 120, "The game does not come with Ambrosia's artwork, sounds or fonts. It can make its own", C_DIM);
        mini_text(LIST_X, 132, "from this zip - once, in a few seconds - and keeps them in its own folder.", C_DIM);
        fill_rect(LIST_X - 6, 200, SCREEN_W - 2 * LIST_X + 12, 26, C_SEL);
        mini_text2x(LIST_X + 6, 205, "Make the artwork now", C_SELINK);
        mini_text(LIST_X, 236, "Start / A / Return / click", C_DIM);
        fill_rect(LIST_X - 6, 290, SCREEN_W - 2 * LIST_X + 12, 20, C_BG);
        mini_text(LIST_X + 6, 296, "Choose a different file instead", C_INK);
        mini_text(LIST_X, 318, "B / Esc / Back", C_DIM);
        set_entries();
        present();

        bool yes = key_down(SDL_SCANCODE_RETURN) || key_down(SDL_SCANCODE_KP_ENTER) ||
                   key_down(SDL_SCANCODE_SPACE) || pad_fire() || pad_start();
        bool no = key_down(SDL_SCANCODE_ESCAPE) || key_down(SDL_SCANCODE_B) ||
                  key_down(SDL_SCANCODE_AC_BACK) || key_down(SDL_SCANCODE_Q);
        if (take_click(&mx, &my)) {
            if (my >= 195 && my < 232) yes = true;
            else if (my >= 280) no = true;
        }
        if (yes) return true;
        if (no) return false;
        pump();
        wait_ms(16);
    }
}

typedef struct { char lines[14][110]; int n; int ticks; Uint64 last; } Prog;

static void draw_progress(const Prog *p, const char *headline)
{
    fill_rect(0, 0, SCREEN_W, SCREEN_H, C_BG);
    mini_text2x(LIST_X, 40, headline, C_TITLE);
    mini_text(LIST_X, 78, "Reading your copy of Apeiron and writing the game's artwork, sounds and fonts.", C_DIM);
    int pct = p->ticks * 100 / 70;
    if (pct > 100) pct = 100;
    fill_rect(LIST_X, 110, SCREEN_W - 2 * LIST_X, 14, C_DIM);
    fill_rect(LIST_X + 1, 111, (SCREEN_W - 2 * LIST_X - 2) * pct / 100, 12, C_OK);
    for (int i = 0; i < p->n; i++)
        mini_text(LIST_X, 144 + i * ROW_H, p->lines[i], i == p->n - 1 ? C_INK : C_DIM);
    set_entries();
    present();
}

static void prog_say(void *ud, const char *line)
{
    Prog *p = ud;
    if (line) {
        if (p->n == 14) { memmove(p->lines[0], p->lines[1], sizeof p->lines - sizeof p->lines[0]); p->n--; }
        snprintf(p->lines[p->n++], sizeof p->lines[0], "%s", line);
    } else {
        p->ticks++;
    }
    Uint64 now = SDL_GetTicks();
    if (!line && now - p->last < 40) return;
    p->last = now;
    draw_progress(p, "Making the artwork...");
    pump();                       /* so a TV does not decide the app has stopped answering */
}

/* Make the artwork folder from the game's zip. On success `out` is the folder, ready for
 * assets_load. A failure is shown on screen and returns false. */
bool assets_make_from_zip(const char *zip, char *out, size_t n)
{
    char dest[1200];
    if (!convert_out_dir(dest, sizeof dest)) {
        plat_message("Cannot make the artwork",
                     "The game could not find anywhere it is allowed to write files. "
                     "Make the artwork on a computer with tools/extract_assets.py instead - see README.md.");
        return false;
    }
    browse_palette();
    Prog *p = calloc(1, sizeof *p);
    if (!p) return false;
    char err[400] = "";
    draw_progress(p, "Making the artwork...");
    bool ok = convert_zip(zip, dest, prog_say, p, err, sizeof err);
    if (ok) {
        snprintf(p->lines[p->n < 14 ? p->n++ : 13], sizeof p->lines[0], "All done.");
        p->ticks = 70;
        draw_progress(p, "Done");
        wait_ms(700);
        snprintf(out, n, "%s", dest);
    } else {
        diag_note("apeiron: could not make the artwork: %s", err);
        free(p);
        plat_message("Cannot make the artwork", err);
        return false;
    }
    free(p);
    return true;
}
