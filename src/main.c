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
    /* Breadcrumbs. On a desktop these go to the terminal and cost nothing; on Android they go to
     * logcat, which is the only way to see how far a build got before it gave up. An app that
     * exits during startup looks exactly like an app that never started, so each step says so. */
    diag_note("apeiron: start, argc=%d", argc);
    const char *dir = argc > 1 ? argv[1] : NULL;
    if (!dir) dir = assets_find_dir(found, sizeof found);
    diag_note("apeiron: artwork guess '%s'", dir ? dir : "(none)");

    /* The saved options are read before the window exists, because whether it opens maximized has
     * to be one of the flags it is created with: asking a window that is already up to maximize
     * is a request the window manager is free to ignore, and some of them do. */
    assets_load_options(dir);

    /* plat_shutdown rather than return, here and everywhere below. Returning from main looks like
     * the harmless way to give up and on Android it is the one thing that must not happen:
     * SDLActivity records that main finished in a *static* field, and a static field outlives the
     * activity for as long as Android keeps the process - which it does, long after the window has
     * gone. Every later launch then takes the branch at the top of its onCreate, calls
     * System.exit(0) and draws nothing at all. The app installs, runs once, and from then on does
     * nothing when you pick it, which is exactly what was reported. plat_shutdown ends the process
     * instead, so the next launch starts from a clean one. */
    if (!plat_init("Apeiron (SDL3 port)")) {
        diag_note("apeiron: plat_init failed - giving up");
        plat_shutdown(1);
    }
    diag_note("apeiron: window and renderer up");

    /* A way in from a desktop, where there is a shell to set it from. On Android the browser's
     * left/right reaches the same screen, which is the case it was written for. */
    if (getenv("APEIRON_DIAG")) diag_screen();

    /* Asked again now that SDL is up: SDL_GetBasePath and SDL_GetPrefPath are the two candidates
     * that matter inside a .app, and neither answers before SDL_Init has run. */
    if (argc <= 1) {
        dir = assets_find_dir(found, sizeof found);
        assets_load_options(dir);
    }
    bool loaded = assets_load(dir);
    /* On Android nothing asks for storage permission up front: a phone should not be sent to a
     * Settings page before it has been shown what the game needs. The start screen below has the
     * system's own file picker, which needs none, and a button that does ask. */
    if (!loaded && !plat_headless()) {
        /* No artwork yet. The usual case is a first run with the Apeiron zip somewhere in
         * Downloads, so look for it and offer to make the artwork from it. */
        char zip[1200];
        if (convert_find_zip(zip, sizeof zip)) {
            diag_note("apeiron: found a game zip at '%s'", zip);
            if (assets_offer_zip(zip) && assets_make_from_zip(zip, found, sizeof found) && assets_load(found)) {
                loaded = true;
                dir = found;
                assets_remember_dir(dir);
            }
        }
    }
    if (!loaded) {
        /* Nothing where it was expected, and no zip to make it from. Rather than exit with a line
         * in a log nobody will read - and a .app started from the Finder has no console at all -
         * ask: the system's picker where there is one, and the browser the game draws itself
         * where there is not. The choice is remembered for next time. */
        diag_note("no artwork in '%s' - asking", dir);
        /* Android has no picker - SDL has no dialog backend there, and the Storage Access
         * Framework deals in content URIs rather than paths, which is no use to a game that opens
         * files with fopen - so there it goes straight to the browser. */
        /* Asked until there is an answer. On a desktop one refusal is taken as a refusal, because
         * there is a console to read and a shell to try again from. On a TV there is neither, so
         * the message is shown and the browser comes back: being returned to the folder list is
         * something a remote control can deal with, and an app that quietly disappeared is not. */
        for (;;) {
            int got = 0;
#ifdef __ANDROID__
            if (plat_phone()) {
                diag_note("apeiron: opening the artwork start screen");
                got = assets_start(found, sizeof found);
            } else {
                /* A TV: no touch, and often no system file picker either, so the start screen's
                 * buttons would lead nowhere. Ask for storage access, look in Downloads once more,
                 * and otherwise go straight to the file browser a remote can drive. */
                diag_note("apeiron: no touch screen - storage access, then the file browser");
                droid_ask_storage();
                char zip[1200];
                if (convert_find_zip(zip, sizeof zip) && assets_offer_zip(zip)) {
                    snprintf(found, sizeof found, "%s", zip);
                    got = 2;
                } else {
                    nav_keys(false);                 /* the browser reads the arrows and buttons itself */
                    got = assets_browse(found, sizeof found);
                    nav_keys(true);
                }
            }
#else
            plat_message("Where is your copy of Apeiron?",
                         "This build does not include the game's artwork, fonts or sounds - they are "
                         "Ambrosia Software's. Choose the Apeiron 1.0.3 zip you downloaded and the "
                         "game will make its own from it. (Cancel to choose a folder of artwork "
                         "made with tools/extract_assets.py instead.)");
            if (assets_pick_zip(found, sizeof found)) got = 2;
            else if (assets_pick_dir(found, sizeof found)) got = 1;
            else got = assets_browse(found, sizeof found);
#endif
            diag_note("apeiron: picker returned %d, '%s'", got, got ? found : "");
            if (got == 2) {
                char zip[1200];
                snprintf(zip, sizeof zip, "%s", found);
                got = assets_make_from_zip(zip, found, sizeof found) ? 1 : 0;
            }
            if (got && assets_load(found)) break;
            plat_message("Apeiron needs its artwork",
                         "This build does not include the game's artwork, fonts or sounds - "
                         "they are Ambrosia Software's. Point the game at your Apeiron 1.0.3 zip "
                         "and it will make its own. See README.md.");
#ifndef __ANDROID__
            plat_shutdown(1);
#else
            /* Asking again is right only while there is someone to ask. Without a display the
             * loop would spin at full speed forever rather than wait on input. */
            if (plat_headless()) plat_shutdown(1);
#endif
        }
        dir = found;
        assets_remember_dir(dir);
        assets_load_options(dir);
    }
    assets_load_options(dir);          /* again, for the artwork pack: that one needs the sheets */

    /* The first [FunkyFade] fades out whatever was on the monitor; the game's window is not up yet. */
    for (int i = 0; i < 256; i++) shown[i] = table[i] = (Rgb){ 0xC000, 0xC000, 0xC000 };
    memset(fb, 0, sizeof fb);

    nav_keys(true);                          /* a remote or a controller can drive the menus from here on */
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
