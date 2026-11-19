# Headless test harness

A stand-in for the SDL3 calls this port uses: a software renderer that writes each presented frame to
`frames/*.ppm`, a fake clock that runs as fast as the machine allows, and scripted input. It is how
the port is exercised without a display, and what CI compiles against.

    tools/test/run.sh                       # 60 seconds of play, a frame every 3 seconds
    LIMIT_MS=20000 SNAP_EVERY=500 tools/test/run.sh assets

By hand, if you would rather:

    mkdir -p frames
    gcc -std=gnu11 -w -Itools/test -Isrc -o apeiron_headless src/*.c tools/test/sdl3_stub.c -lm
    SCRIPT=1 NOAUDIO=1 SNAP_EVERY=3000 LIMIT_MS=60000 ./apeiron_headless assets

`-lm` is not optional: the stub's tiled blit uses `floorf`.

## Environment

| Variable | Effect |
| - | - |
| `SCRIPT=1` | Click through the intro, click the title menu, then drive the shooter and hold fire |
| `SCRIPT=2` | Page through the instruction screens instead of playing |
| `LIMIT_MS` | How long to run, in fake milliseconds |
| `SNAP_EVERY` | Gap between saved frames, in fake milliseconds. Unset saves none |
| `NOAUDIO=1` | Report no audio device and carry on silently |
| `CX`, `CY` | Where the title-screen click lands, at 25 s (default 460,42 — Play is at y 20 since the menu moved up) |
| `CX2`, `CY2` | A second click, at 28 s. Used to reach a tab on the options screen |
| `NOMENU=1` | Suppress the title click and the key script, to leave the title screen alone — which is how attract mode gets tested |
| `KEYSCRIPT` | Text to type at 26 s, as one text-input event |
| `SPAWN` | A scancode to hold down repeatedly from 26 s. With `debug 1` in `options.txt`, `SPAWN=62` is F5 and spawns coins |
| `MOUSEX`, `MOUSEY` | Starting mouse position |
| `CAPSAT` | Fake-millisecond time at which Caps Lock goes down, to test the pause screen |
| `OUT_W`, `OUT_H` | Window size, to check the whole-number scaling |
| `OUT_DIR` | Where `run.sh` puts the binary (default `build-headless/`) |

## Looking at the output

The frames are binary PPM, which most image tools read directly:

    python3 -c "from PIL import Image; Image.open('frames/f_012000.ppm').save('f.png')"

## What it does not do

It is a stand-in, not an emulator of SDL. Audio is accepted and discarded, the window is a fixed
framebuffer, and timing is a counter rather than a clock — so it tells you what was drawn and what
the game logic did, never how it feels to play. Anything about feel still has to be played.
