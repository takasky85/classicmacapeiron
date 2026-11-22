package io.github.takasky85.apeiron;

import org.libsdl.app.SDLActivity;

/**
 * Nothing to add. SDLActivity loads "SDL3" and then "main" - the game - by default, and the one
 * permission the game needs (to read the Download folder) is asked for from the game's own code,
 * where it can wait for the answer and say what is happening on the screen. A subclass exists
 * only because the manifest wants an Activity of this app's own to name.
 */
public class MainActivity extends SDLActivity {
}
