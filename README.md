# Ambrosia Apeiron 1.0.3 for Classic Mac OS ported to SDL3
Attempting to decompile PowerPC binaries for Ambrosia's Apeiron 1.0.3 for Classic Mac OS 7/8/9.

Tested on Windows 11 and Ubuntu 26.04

No original assets included. I tested by installing Apeiron in Mac OS 7.5.3 on Basilisk and copying the install folder to "My Computer", that exposes all of the resource files as plain .rsrc folders in Windows.


Video of progress so far:

[![Watch the video](https://img.youtube.com/vi/a0l0yr2OHTA/hqdefault.jpg)](https://www.youtube.com/watch?v=a0l0yr2OHTA)

![type](https://img.shields.io/badge/type-fan%20preservation-orange)


<img width="606" height="223" alt="PICT_889_Apeiron Intro Logo pictRes" src="https://github.com/user-attachments/assets/c0e5f941-e5cc-4346-995b-c1ee417c1435" />



# Apeiron startup -> title screen, SDL3

Reproduces Apeiron 1.0.3's startup (FunkyFade, Ambrosia logo + jingle, Apeiron logo + intro theme with
palette cycling, title screen fade-in) with an 8-bit framebuffer and a 256-colour palette, like the original.

## Build and run (needs SDL3 3.2+)
    cmake -B build && cmake --build build
    ./build/apeiron_boot            # or: ./build/apeiron_boot path/to/assets
or without CMake: `gcc src/boot.c -o apeiron_boot $(pkg-config --cflags --libs sdl3)`
Esc or closing the window quits; a click or key press skips the palette-cycling part of the intro.
The window starts at 640x480 (its minimum size). When you maximise it, the game screen is scaled by the largest
whole number that fits and centred; the area around it is black during the intro and, on the title screen, is
tiled with pixel pattern 400 and framed by seven rings, as in the original (which ran in a monitor-sized window).

## Regenerating the assets (already in assets/)
    pip install pillow
    python tools/extract_assets.py <unzipped Apeiron folder> assets
(`<unzipped Apeiron folder>` is the one containing `Apeiron Titles` and its hidden `.rsrc/` folder.)

## Assets
- `ambrosia_logo.bmp`  PICT 999, indices = clut 999            - `apeiron_intro.bmp` PICT 889, indices = clut 889
- `title_logo.bmp`     PICT 400, indices = clut 129 (Title Clut)
- `transition.wav` snd 128, `level_clear.wav` snd 144, `intro_theme.wav` snd 143 (8-bit mono, 22255 Hz)
- `title_morrison.bmp` PICT 402, drawn bottom-right on the title screen (indices = clut 129)
- `pattern_128..138.bmp` (level backgrounds, Game Clut 128), `pattern_400.bmp` (title margins, Title Clut 129),
  `pattern_401/402.bmp` (Game Clut). Only 400 is used so far; the rest are ready for the gameplay port.
- `font_geneva9`, `font_chicago12`, `font_newyork14/18/24` (.bmp glyph strip + .fnt metrics): the real Mac bitmap fonts.
  New York 14/18/24 are the game's own embedded copies; Geneva and Chicago come from `source_fonts/`.
  Geneva and Chicago are Apple's fonts: keep them for personal/preservation use rather than redistributing.
- `strings.txt` (STR# 133 + version/licence strings, Mac Roman bytes) and `scores.txt` (the shipped "Apeiron Scores" table and prefs)
Every BMP is 8-bit indexed with the game's own palette in the file; images and patterns are pre-mapped to that palette
(QuickDraw does the same when it draws into a window: exact colour match, else nearest).

## Not ported
Loading of sprites/sounds and the ReportStr status text during the logo, the cicn icons on the title screen (menu bullets,
volume/music arrows), the shareware notice and the menu/main loop.
