/*
 * main.c - [main] and [MainLoop]: the intro, then the title screen, then whatever it asks for.
 */
#include "apeiron.h"
#include <SDL3/SDL_main.h>


int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "assets";

    /* The saved options are read before the window exists, because whether it opens maximized has
     * to be one of the flags it is created with: asking a window that is already up to maximize
     * is a request the window manager is free to ignore, and some of them do. */
    assets_load_options(dir);

    if (!plat_init("Apeiron (SDL3 port)")) return 1;
    if (!assets_load(dir)) {
        SDL_Log("missing assets - run tools/extract_assets.py first (looked in '%s')", dir);
        return 1;
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
