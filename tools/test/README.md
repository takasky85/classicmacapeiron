# Headless test harness

A stand-in for the SDL3 calls this port uses: a software renderer that writes each presented frame to
`frames/*.ppm`, a fake clock, and scripted input. It is how the port was exercised without a display.

    mkdir -p frames
    gcc -std=c11 -w -Itools/test -c tools/test/sdl3_stub.c -o stub.o
    for f in platform assets intro game main; do \
      gcc -std=gnu11 -Wall -Wextra -Werror=implicit-function-declaration \
          -Itools/test -Isrc -c src/$f.c -o $f.o; done
    gcc -o apeiron_headless platform.o assets.o intro.o game.o main.o stub.o -lm
    SCRIPT=1 SNAP_EVERY=3000 LIMIT_MS=60000 ./apeiron_headless assets

Environment: `SCRIPT=1` clicks through the intro and the Play entry then drives the ship,
`SNAP_EVERY` is the gap in milliseconds between saved frames, `LIMIT_MS` the run length,
`OUT_W`/`OUT_H` the window size (to check the whole-number scaling), `NOAUDIO=1` to silence audio.
