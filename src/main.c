/*
 * main.c - [main] and [MainLoop]: the intro, then the title screen, then whatever it asks for.
 */
#include "apeiron.h"
#include <SDL3/SDL_main.h>

static void simple_page(const char *title, const char *const *lines, int n)
{
    uint8_t white = find_color(0xFFFF, 0xFFFF, 0xFFFF);
    uint8_t black = find_color(0, 0, 0);
    memset(fb, black, sizeof fb);
    set_margin_pattern(NULL);
    set_border_rings(NULL, false);
    int w = text_width(&A.newyork24, title, 0);
    draw_text(&A.newyork24, (SCREEN_W - w) / 2, 60, title, white, 0);
    for (int i = 0; i < n; i++)
        draw_text(&A.newyork18, 60, 120 + i * 26, lines[i], white, 0);
    const char *fin = "Click to go back";
    w = text_width(&A.geneva9, fin, STYLE_BOLD);
    draw_text(&A.geneva9, (SCREEN_W - w) / 2, SCREEN_H - 30, fin, white, STYLE_BOLD);
    set_entries();
    button();
    while (!button()) { pump(); present(); SDL_Delay(10); }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "assets";

    if (!plat_init("Apeiron (SDL3 port)")) return 1;
    if (!assets_load(dir)) {
        SDL_Log("missing assets - run tools/extract_assets.py first (looked in '%s')", dir);
        return 1;
    }

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
        case TITLE_CONTROLS: {
            static const char *l[] = {
                "Left / Right / Up / Down ... move",
                "Space, Ctrl, or mouse button ... fire",
                "Mouse inside the field ......... move",
                "Escape ......................... quit to the title screen"
            };
            simple_page(A.str[10], l, (int)(sizeof l / sizeof l[0]));
            break;
        }
        case TITLE_CLEAR_SCORES:             /* [ClearScores] */
            for (int i = 0; i < 10; i++) {
                snprintf(A.score_row[i].name, sizeof A.score_row[i].name, "%s", "Ambrosia SW");
                A.score_row[i].score = (10 - i) * 1000;
                A.score_row[i].wave = 1;
            }
            assets_save_scores(dir);
            break;
        case TITLE_ABOUT: {
            const char *l[3];
            l[0] = A.str[32];
            l[1] = A.str[1];
            l[2] = "Ported to SDL3 from the original PowerPC build.";
            simple_page(A.str[12], l, 3);
            break;
        }
        case TITLE_QUIT:
            fade_to_color(20, RGB_BLACK);
            plat_shutdown(0);
        }
    }
}
