/*
 * main.c - [main] and [MainLoop]: the intro, then the title screen, then whatever it asks for.
 */
#include "apeiron.h"
#include <SDL3/SDL_main.h>


int main(int argc, char **argv)
{
    /* Where the artwork is. A path on the command line wins; otherwise the usual places are tried
     * in turn, which on a Mac includes inside the .app, and the one that worked last time is
     * remembered. If none of them has it the game asks, once the window is up. */
    static char found[1200];
    const char *dir = argc > 1 ? argv[1] : NULL;
    if (!dir) dir = assets_find_dir(found, sizeof found);

    /* The saved options are read before the window exists, because whether it opens maximized has
     * to be one of the flags it is created with: asking a window that is already up to maximize
     * is a request the window manager is free to ignore, and some of them do. */
    assets_load_options(dir);

    if (!plat_init("Apeiron (SDL3 port)")) return 1;

    /* Asked again now that SDL is up: SDL_GetBasePath and SDL_GetPrefPath are the two candidates
     * that matter inside a .app, and neither answers before SDL_Init has run. */
    if (argc <= 1) {
        dir = assets_find_dir(found, sizeof found);
        assets_load_options(dir);
    }
    if (!assets_load(dir)) {
        /* Nothing where it was expected. Rather than exit with a line in a log nobody will read -
         * and a .app started from the Finder has no console at all - put up the system's folder
         * picker and take what it is given. The choice is remembered for next time. */
        SDL_Log("no artwork in '%s' - asking", dir);
        /* The system's folder picker where there is one. Android has none - SDL has no dialog
         * backend there, and the Storage Access Framework deals in content URIs rather than
         * paths, which is no use to a game that opens files with fopen - so there it goes
         * straight to the browser the game draws itself. */
        bool got;
#ifdef __ANDROID__
        got = assets_browse(found, sizeof found);
#else
        got = assets_pick_dir(found, sizeof found);
        if (!got) got = assets_browse(found, sizeof found);
#endif
        if (!got || !assets_load(found)) {
            plat_message("Apeiron needs its artwork",
                         "This build does not include the game's artwork, fonts or sounds - "
                         "they are Ambrosia Software's. Make an 'assets' folder from your own "
                         "copy of Apeiron with tools/extract_assets.py, then point the game at "
                         "it. See README.md.");
            return 1;
        }
        dir = found;
        assets_remember_dir(dir);
        assets_load_options(dir);
    }
    assets_load_options(dir);          /* again, for the artwork pack: that one needs the sheets */

    /* The first [FunkyFade] fades out whatever was on the monitor; the game's window is not up yet. */
    for (int i = 0; i < 256; i++) shown[i] = table[i] = (Rgb){ 0xC000, 0xC000, 0xC000 };
    memset(fb, 0, sizeof fb);

    apeiron_intro();

    for (;;) {                               /* [MainLoop] */
        set_monitor_clut(A.title_clut, false);
        int action = title_loop();
        switch (action) {
        case TITLE_PLAY:
            new_game();
            break;
        case TITLE_INSTRUCTIONS:
            do_help();                       /* the game's own three instruction pages */
            break;
        case TITLE_CONTROLS:             /* the menu entry is Options now */
        case TITLE_OPTIONS:
            do_options();                /* graphics, controls, packs, cheats, debug */
            assets_save_options(dir);
            break;
        case TITLE_CLEAR_SCORES:             /* [ClearScores] */
            for (int i = 0; i < 10; i++) {
                snprintf(A.score_row[i].name, sizeof A.score_row[i].name, "%s", "Ambrosia SW");
                A.score_row[i].score = (10 - i) * 1000;
                A.score_row[i].wave = 1;
            }
            assets_save_scores(dir);
            break;
        case TITLE_ABOUT:
            do_about();                      /* [DoAbout]: the game's own dialog */
            break;
        case TITLE_QUIT:
            fade_to_color(20, RGB_BLACK);
            plat_shutdown(0);
        }
    }
}
