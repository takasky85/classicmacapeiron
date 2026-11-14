# Ambrosia Apeiron 1.0.3 for Classic Mac OS ported to SDL3
Attempting to decompile PowerPC binaries for Ambrosia's Apeiron 1.0.3 for Classic Mac OS 7/8/9.

![type](https://img.shields.io/badge/type-fan%20preservation-orange)

Tested on Windows 11 and Ubuntu 26.04

No original assets included. I tested by installing Apeiron in Mac OS 7.5.3 on Basilisk and copying the install folder to "My Computer", that exposes all of the resource files as plain .rsrc folders in Windows.


Video of progress so far:

[![Watch the video](https://img.youtube.com/vi/oyI8netpoao/hqdefault.jpg)](https://www.youtube.com/watch?v=oyI8netpoao)




<img width="606" height="223" alt="PICT_889_Apeiron Intro Logo pictRes" src="https://github.com/user-attachments/assets/c0e5f941-e5cc-4346-995b-c1ee417c1435" />



# Apeiron 1.0.3 — SDL3 port

A playable port of Ambrosia Software's Apeiron (Mac OS, 1995–2000) to SDL3, built by reverse
engineering the game's own PowerPC binary and extracting its original artwork, fonts and sounds from
its resource forks. Nothing here is redrawn or re-recorded: every sprite, pattern, palette, font and
sound effect comes out of the game's own files.

Throughout the source, names in `[brackets]` are the functions in the decompiled PowerPC build
(`Apeiron.pef`, exported from Ghidra), which is the reference for the behaviour implemented.

## Build and run

Needs SDL3 3.2 or newer.

    cmake -B build && cmake --build build
    ./build/apeiron                 # or: ./build/apeiron path/to/assets

Without CMake:

    gcc src/*.c -o apeiron $(pkg-config --cflags --libs sdl3) -lm    # skip src/boot.c

On Windows, MSYS2 (`pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,sdl3}`) is the easiest route;
with Visual Studio, point `SDL3_DIR` at the `cmake` folder of the `SDL3-devel-*-VC` package.

## Playing

| | |
|---|---|
| Move | Arrow keys, WASD, or the mouse inside the field |
| Fire | Space, Ctrl, or the mouse button |
| Pause / quit a game | Escape (returns to the title screen) |
| Title screen | Click a menu entry; the arrows either side of Sound Volume and Music change them |
| Fullscreen | F11 |

The window starts maximised and F11 toggles fullscreen; it never goes below 640×480. The screen is
scaled by the largest whole number that fits and centred, and the surround is black in the intro
and, on the title screen, tiled with pixel pattern 400 inside seven rings, as the original did on a
monitor-sized window.

## What is implemented

* **Intro** — `[ApeironIntro]`: the transition fades, the Ambrosia logo with its jingle, the Apeiron
  logo with the intro theme and the 180-step palette-cycling animation, all driven by rewriting the
  colour table exactly as the original does.
* **Title screen** — `[UpdateTitleScreen]` / `[TitleClick]`: the logo, the real high-score table read
  from the game's own `Apeiron Scores` file, the version and licence lines, the six menu entries and
  the two settings, laid out at the original's coordinates in the original's fonts, with the game's
  own colour-icon buttons (cicn 1000-1003) and their pressed states.
* **Instructions** — `[DoHelp]` / `[AboutWindowUpdate]`: the game's own three pages (Bestiary,
  Yummiary, Hintiary) replayed from PICT 4000-4002, which are QuickDraw drawing commands rather than
  pictures, with the live animated creatures `[CreateSprites]` places beside the text.
* **In-game screen** — `[GameWindowUpdate]`: the 432-wide playfield flush left, the ScoreMatt panel
  (PICT 401) beside it with the score, bonus, lives and wave in its own boxes, pattern 401 behind and
  the seven rings framing the field.
* **The game** — `[GameLoop]` and friends on a 27×30 grid of 16-pixel cells, one frame every two
  ticks (30 a second), which is what `[CompositeFrame]`'s closing spin-wait enforces and what the
  movement speeds are scaled for:
  * the centipede as a chain that follows its head, splitting into new heads when a segment is shot,
    at the wave's speed (3 or 5 px/tick, `[StartLevel]`), plus the loose heads of `[HandleSideFeed]`
  * mushrooms with four damage states, destroyed on the fourth hit, replanted where a segment dies
  * fleas (two hits, trailing mushrooms), geckos, scorpions that poison what they cross, yummies,
    bonus fodder, the multiplier coin, the psychedelic power-up and uprooted mushrooms
  * the scoring from `[ShotHitCheck]` — 100× for a head, 10× for a body, 1000× for a gecko, the
    scorpion's distance-based value, the doubling flea chain, 3000/9000× for an uproot
  * dying flashes the whole screen red for 20 ticks — the palette put through `[HueScale](1)`, the
    same routine the greyscale transition uses — before the ship respawns
  * mushrooms block the player and stop shots (a shot is swept a pixel at a time so it cannot pass
    through one directly above), and the score, bonus and wave are drawn with the game's own digits
    (PICT 1100, `[CompileNumbers]`)
  * one shot on screen at a time and four ships to start, as the game's own instruction pages state;
    the player speed bands, flea threshold and respawn timing from `[GameHouseKeeping]`, death, the
    wave rebuild on respawn, extra ships every 20000
  * Gordon the Gecko poisons the mushrooms he crosses, and a centipede that meets a poisoned
    mushroom plummets straight to the bottom
* **Game over** — `[GameOver]`: the caterpillar picture under colour table 888 with "Hooka Music",
  the eight frames of PICT 900-907 played over it at (225, 99) eight ticks apart (a click cuts the
  sound short), then `[EnterHighScore]`'s name dialog pre-filled with the last name and its own
  sound, then a transition fade back to the title; a different picture past wave 15
* **Power-ups** — `[UpdateWeapons]`: six slots in the red box on the score panel, the multiplier and
  then the five power-ups the Yummiary page names, each shown while it lasts (the Yummielock keeps
  them when you die)
  * per-wave mushroom colours and background patterns, the end-of-wave bonus count-up, game over and
    high-score entry (saved back to `assets/scores.txt`)

## Assets

`tools/extract_assets.py` regenerates everything from the unzipped game folder (needs Pillow):

    python tools/extract_assets.py <unzipped Apeiron folder> assets

`<unzipped Apeiron folder>` is the one holding `Apeiron Titles`, `Apeiron Sounds` and the hidden
`.rsrc/` folder of resource forks.

| | |
|---|---|
| `spr/*.bmp` + `.spr` | the 21 sprite sheets, frames unpacked from each PICT strip, white keyed out |
| `snd/*.wav` | all 42 sound effects plus the two music parts, 8-bit mono as they are in the game |
| `pattern_128..138` | the level backgrounds; `pattern_400` the title surround, `401/402` the rest |
| `ambrosia_logo`, `apeiron_intro`, `title_logo`, `title_morrison` | the intro and title pictures |
| `font_geneva9`, `font_chicago12`, `font_newyork14/18/24` | real Mac bitmap fonts (`.bmp` strip + `.fnt` metrics) |
| `strings.txt`, `scores.txt` | STR# 133 and the shipped high-score table and preferences |

Every image is an 8-bit indexed BMP carrying the game's own palette, with its colours mapped to that
palette the way QuickDraw maps them (exact match where there is one, nearest otherwise).

New York 14/18/24 are the copies embedded in the game itself. Geneva and Chicago come from
`source_fonts/` and are Apple's fonts — keep them for personal and preservation use rather than
redistributing them.

## Known gaps and approximations

* The centipede's path logic is written from the behaviour the decompile describes, not transcribed
  from its waypoint queue, so individual turns may differ from the original in detail.
* `[HandleSideFeed]` is gated in the original by a flag this port could not identify; here at most
  three loose heads are out at once, without which they outpace the player and a wave can never end.
* The power-ups use the game's own five icons and come from coins, but what each one does, and how
  long it lasts, is guessed: only the machine gun (more shots at once), free move and Yummielock are
  implemented, and their durations are invented.
* **Sprite behaviour in the original lives behind per-sprite function pointers.** Each sprite keeps
  two routine pointers at +0x08 and +0x0c, and `[AnimateFrame]` calls them every frame; that is where
  the real movement, bouncing and animation live. The decompile shows them only as data addresses
  (`PTR_PTR_1001c96c` for `[MakeCoin]`, `…caac` for `[CreateUproot]`, `…caa4` for
  `[CreatePsychadelic]`, `…ca9c` for `[CreateMultiplier]`, `…ca84` for `[CreateExplosion]`,
  `…ca70` for `[CreateRicochet]`), so every movement rule here outside the create/spawn functions is
  inferred from behaviour rather than read from the binary. Following those pointers in Ghidra and
  decompiling what they point at is the single biggest remaining accuracy win.
  Because of that, so the bouncing coins on the Hintiary page use `[MakeCoin]`'s real launch
  values with invented gravity, and stay in the strip they spawn in.
* The frame length (`DAT_1001ddac`, the number of ticks `[CompositeFrame]` waits out) is one of the
  unresolved globals; two ticks is inferred from the movement speeds, not read from the binary.
* Which sound each `ST_SimplePlay` call plays is inferred — the sound handles are unresolved globals,
  so the death sound, the instruction-page sounds and a few others are best guesses.
* Shot speed, several animation rates and the yummie power-up effects are chosen to match the
  behaviour described rather than read from the original's tables.
* Not ported: the loading screen's status text, the shareware notice, the About dialog, the cheat
  codes, the difficulty setting, InputSprocket configuration, and the original's dirty-rectangle
  redraw (this redraws every frame).
* Backgrounds 128, 129 and 134 were authored against a different palette from the game's, so they
  dither when mapped to it; the other eight map exactly.
* `src/boot.c` is the first pass — the intro and title screen only — kept as a reference build.

## Testing

Built and run only against a headless stand-in for SDL3 that implements the calls this port uses
(software renderer to PPM frames, a fake clock and scripted input). The game was played through to
wave 18 that way to check wave progression, scoring, death and respawn. It has not been built against
real SDL3, so API mismatches are possible; the gameplay has never been seen or heard at full speed.
