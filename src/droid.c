/*
 * droid.c - the one thing Android makes an app ask for: permission to read the Download folder.
 *
 * The artwork lives in a folder the player made, usually copied to the TV's Download folder, and
 * the game opens it with fopen. Android only lets an app do that after it has been granted
 * READ_EXTERNAL_STORAGE, and an app has to ask for that while it is running - listing it in the
 * manifest is not enough from Android 6 onwards.
 *
 * Only that one permission is asked for, and only when the artwork was not already found without
 * it. The app's own folder under Android/data needs none, so a player who would rather not grant
 * anything can put the artwork there instead (the diagnostics screen says where it is).
 *
 * What the grant actually buys depends on the Android version, and that is the part that cannot be
 * promised from here: on Android 10 and later the system decides which files in someone else's
 * folder an app may read, and which kinds count is not something a build can be tested for without
 * the device. So the answer is recorded and shown on the diagnostics screen next to what each
 * folder really does when opened, instead of being assumed.
 */
#include "apeiron.h"

#ifdef __ANDROID__

static SDL_AtomicInt answer;          /* 0 not answered yet, 1 granted, 2 refused */
static const char *outcome = "not asked";

static void SDLCALL perm_cb(void *ud, const char *perm, bool granted)
{
    (void)ud; (void)perm;
    SDL_SetAtomicInt(&answer, granted ? 1 : 2);
}

enum { W_BG = 1, W_INK = 2, W_TITLE = 3, W_DIM = 4 };

static void wait_screen(int secs_left)
{
    Rgb c[256];
    memset(c, 0, sizeof c);
    c[W_BG]    = (Rgb){ 0x0800, 0x0C00, 0x1800 };
    c[W_INK]   = (Rgb){ 0xE000, 0xE000, 0xE000 };
    c[W_TITLE] = (Rgb){ 0x60FF, 0xD000, 0x80FF };
    c[W_DIM]   = (Rgb){ 0x7000, 0x7800, 0x8000 };
    set_monitor_clut(c, true);

    fill_rect(0, 0, SCREEN_W, SCREEN_H, W_BG);
    mini_text2x(16, 16, "Apeiron needs to read your files", W_TITLE);
    mini_text(16, 60,  "The artwork was not found yet. Android is asking whether", W_INK);
    mini_text(16, 72,  "Apeiron may read the Download folder.", W_INK);
    mini_text(16, 100, "Choose Allow in the box that has appeared.", W_INK);
    mini_text(16, 128, "Nothing is changed or sent anywhere; the game only reads", W_DIM);
    mini_text(16, 140, "the folder you put the artwork in.", W_DIM);
    char line[80];
    snprintf(line, sizeof line, "Any key, button or click carries on without it (%d)", secs_left);
    mini_text(16, SCREEN_H - 14, line, W_TITLE);
    set_entries();
    present();
}

bool droid_ask_storage(void)
{
    int sdk = SDL_GetAndroidSDKVersion();
    if (sdk < 23) {                    /* before runtime permissions: the manifest already did it */
        outcome = "granted at install";
        diag_note("storage: Android %d - permission comes with the install", sdk);
        return true;
    }

    SDL_SetAtomicInt(&answer, 0);
    if (!SDL_RequestAndroidPermission("android.permission.READ_EXTERNAL_STORAGE", perm_cb, NULL)) {
        outcome = "could not ask";
        diag_note("storage: the permission request could not be made");
        return false;
    }

    /* Nobody to answer under a dummy display; do not wait for one. */
    bool nobody = plat_headless();
    Uint64 start = SDL_GetTicks();
    Uint64 limit = 90000;
    flush_clicks();
    while (SDL_GetAtomicInt(&answer) == 0) {
        if (nobody) break;
        /* The system's box takes the app out of the foreground, and SDL holds the event loop while
         * it is away, so these ticks mostly pass while nothing here is running. The answer is
         * always looked at first, so a slow reader is not penalised for taking their time. */
        Uint64 gone = SDL_GetTicks() - start;
        if (gone > limit) { outcome = "no answer"; diag_note("storage: no answer to the permission box"); return false; }
        wait_screen((int)((limit - gone) / 1000));
        if (button() || pad_fire() || pad_start()) {
            /* Only a press that arrives while the game still has the focus is "skip"; a press the
             * system's box took never gets here. */
            if (SDL_GetAtomicInt(&answer) == 0) {
                outcome = "skipped";
                diag_note("storage: skipped waiting for the permission box");
                return false;
            }
        }
        pump();
        wait_ms(16);
    }

    int a = SDL_GetAtomicInt(&answer);
    outcome = a == 1 ? "granted" : a == 2 ? "refused" : "no answer";
    diag_note("storage: READ_EXTERNAL_STORAGE %s (Android %d)", outcome, sdk);
    flush_clicks();
    return a == 1;
}

const char *droid_storage_state(void) { return outcome; }

#else

bool droid_ask_storage(void) { return true; }
const char *droid_storage_state(void) { return "not Android"; }

#endif
