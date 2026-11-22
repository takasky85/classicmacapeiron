# Apeiron 1.0.3 — SDL3 port

A playable port of Ambrosia Software's Apeiron (Mac OS, 1995–2000) to SDL3, built by reverse
engineering the game's own PowerPC binary and extracting its original artwork, fonts and sounds from
its resource forks. Nothing here is redrawn or re-recorded: every sprite, pattern, palette, font and
sound effect comes out of the game's own files.

Throughout the source, names in `[brackets]` are the functions in the decompiled PowerPC build
(`Apeiron.pef`, exported from Ghidra), which is the reference for the behaviour implemented.

## Build and run

**`assets/` is not in this repository.** It holds Ambrosia's artwork, fonts and sounds, lifted out
of the game's own resource forks, so a clone starts without it and you make it from your own copy of
Apeiron. Nothing builds or runs until you have:

    python3 tools/extract_assets.py Apeiron_1_0_3.zip

That unpacks the zip, converts what is in it and writes a folder called `apeiron_classic_assets`
beside the zip (give a second argument to put it somewhere else). The game also needs Apple's
Geneva and Chicago fonts, which are not in the game's files or in this repository: put the
`Geneva.zip` and `Chicago.zip` you get from a Mac beside the game's zip (or in Downloads) and it
finds them. Without them it still works, using the game's own New York font in their place and
saying so. With no arguments at all it looks for `Apeiron*.zip` in Downloads or the current folder,
and on Windows you can drop the zip onto the script. The game
looks for `apeiron_classic_assets` (or the older name, `assets`) beside the executable, or in
Download on Android; a path on the command line works anywhere.

The build itself needs SDL3 3.2 or newer:

    cmake -B build && cmake --build build
    ./build/apeiron                 # or: ./build/apeiron path/to/assets

Without CMake:

    gcc src/*.c -o apeiron $(pkg-config --cflags --libs sdl3)

The build succeeds whether or not `assets/` exists - CMake copies it beside the executable when it
is there and tells you what to run when it is not - but the game itself stops at startup with
`missing assets` until it can find them.

To exercise the port without a display at all, there is a headless harness in `tools/test` that
needs neither SDL3 nor a window; see [tools/test/README.md](tools/test/README.md).

`tools/extract_assets.py` takes whichever of these you have and does the rest itself:

    python3 tools/extract_assets.py Apeiron_1_0_3.zip
    python3 tools/extract_assets.py <a MacBinary .bin of one file>
    python3 tools/extract_assets.py <a folder of MacBinary files>

The output is a folder called `apeiron_classic_assets`, written beside the input; add a second
argument to put it somewhere else.

A folder already holding a `.rsrc/` directory is used as it stands; anything else is split into its
data and resource forks first, into a working folder of its own. The one thing it cannot open is
the Installer VISE 3 installer itself - see the note on that below - so run the installer once
under SheepShaver or Basilisk II and point the script at what it installs.

**On Windows**, nothing in this port uses libiconv: `libiconv-2.dll` comes in with SDL3 when SDL
itself was built against the MSYS2 or MinGW one, and then `SDL3.dll` will not load without it
beside the executable. The CMake build copies the whole chain for you on CMake 3.21 and newer. If
you are building by hand, either copy `libiconv-2.dll` from `C:/msys64/ucrt64/bin` next to
`apeiron.exe`, or use the official SDL3 release build from libsdl.org, which has no such
dependency.

On Windows, MSYS2 (`pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,sdl3}`) is the easiest route;
with Visual Studio, point `SDL3_DIR` at the `cmake` folder of the `SDL3-devel-*-VC` package.

## Playing

| | |
|---|---|
| Move | Arrow keys, WASD, the mouse, or a controller's stick or d-pad |
| Fire | Space, Ctrl, the mouse button, or any face button |
| Pause a game | Caps Lock, or Start on a controller |
| Quit a game | Escape (returns to the title screen) |
| Title screen | Click a menu entry; the arrows either side of Sound Volume and Music change them |
| Fullscreen | F11 |
| Title screen keys | a about, c or o options, i instructions, p play, q quit, z clear scores, x the poem |

Caps Lock pauses, which is what the original used: `[GameHouseKeeping]` tests `[CapsLockDown]` every
frame and `[PauseGame]` blacks the window out, puts PICT 3130 - "PAUSED (Hit the Caps Lock key to
continue)" - up a third of the way down it, stops the music, plays snd 139 and waits until Caps Lock
is up again. Nothing else pauses: the window going into the background no longer does.

The pointer is the game's own pointing hand (crsr 128), drawn into the frame so it scales with
everything else. During a game it is taken over and read as movement rather than position, so there
is no field edge for it to catch on; it comes back on pause and at the end of the game.

The window starts maximised and F11 toggles fullscreen; it never goes below 640×480. The screen is
scaled by the largest whole number that fits and centred, and the surround is black in the intro
and, on the title screen, tiled with pixel pattern 400 inside seven rings, as the original did on a
monitor-sized window.

## Options

Not part of the original. **Options** takes the title screen's Controls place (the controls
themselves are one of its pages now); **c** and **o** both open it. Five pages:

**Graphics.** Whole-number scaling, which is the default and what the artwork expects, or filling
the window at 4:3 with uneven pixels, and whether the window opens maximized. F11 still toggles
fullscreen.

**Controls.** Mouse, keyboard and controller each on or off, the stick dead zone, and what is
plugged in. A controller's left stick or d-pad steers at the shooter's own top speed, any face
button fires and Start pauses.

**Assets and cheats.** Three packs ship alongside the game in folders of their own and
`tools/extract_assets.py` unpacks each into `assets/packs/<name>/` holding only what it replaces,
so switching one on reloads a few files rather than re-running anything. *The Simpsons* is a
complete replacement sprite set (28 sheets), *The Muppets* replaces four (the centipede, the flea,
the scorpion and the fodder) and leaves the rest as it shipped, and *Star Trek* carries its own
music. Star Trek's `Apeiron Sounds` is zero-padded from offset 759296 in this copy - its resource
map is gone - so its sound effects cannot be recovered from it; with an intact copy the extractor
would pick them up.

**Enhancements.** Things the original does not do. The power-up's icon arcs from the coin you
picked it up on over to its box on the score panel, drawn over the panel with the icon sheet's
black keyed out, and the box leaves that slot empty until the icon lands - unless one of the same
kind was already counting down there, which is not blanked. An extra shooter does the same thing
to the Lives box. In the box, the shield borrows its six playfield frames (the other four icons
are a single cell each in the original, so there is nothing of theirs to animate) and the spare
shooters turn slowly on the spot; a slot flashes as its time runs out. A multiplier tile flies to
its own slot the same way, and when a shooter is lost the replacement leaves the Lives box and
flies down to its starting place. **Starting wave** begins a game past the first, which forfeits
the high score the way a cheat does. **No screen flashing** leaves the psychedelic mushroom's
scoring and music alone but holds the colour table still. The title screen carries the build's own
`yymmddhhmm` stamp at the foot of the score panel, matching the archive it came in. A shooter
earned off the scoreboard at every twenty thousand points leaves the Score box and flies down to
the Lives box, the same way one picked up off a coin flies out of the playfield. **Attract mode** gives the
game back to itself after the title screen has been left alone for twenty seconds: it plays two
shooters' worth or seventy-five seconds, whichever comes first, with the cheats off, and any key
or click hands control back. Nothing in the original plays itself - there is no Demo or Attract
routine anywhere in the binary - so the demo player is an invention: it lines up under whatever is
worth shooting, backs out from under anything about to land on it, walks over to a coin and holds
the trigger down. A Mac OS boot screen on the way in is still to come.

Everything on these pages is written to `assets/options.txt` when you leave the screen and read
back at startup, so a run begins where the last one left off - including which artwork pack is on.

**Debug.** Keys that spawn a creature straight into a game: F1 a flea, F2 Larry, F3 Gordon, F4 the
fodder, F5 a coin, F6 a multiplier tile, F7 a psychedelic shroom, F8 an uprooted one. They arrive
where they normally would and behave normally once there.

**Cheats.** STR# 132, which the original has you type into the paused game (ROT13'd in the
resource and matched backwards by `[DoKey]`); here they are checkboxes that last for the session.
What each does is what the game's own shipped `Cheats` file says, including the rule that using one
forfeits the high score unless it is PERNTS or SCUMDOG. MORRISON and NUKE EM are momentary rather
than standing, so switching them on binds them to **M** and **N** during play.

## What is implemented

* **Intro** — `[ApeironIntro]`: the transition fades, the Ambrosia logo with its jingle, the Apeiron
  logo with the intro theme and the 180-step palette-cycling animation, all driven by rewriting the
  colour table exactly as the original does.
* **Title screen** — `[UpdateTitleScreen]` / `[TitleClick]`: the logo, the real high-score table read
  from the game's own `Apeiron Scores` file, the version and licence lines, the six menu entries and
  the two settings, laid out at the original's coordinates in the original's fonts, with the game's
  own colour-icon buttons (cicn 1000-1003) and their pressed states.
* **About and the poem** — `[DoAbout]` puts up DLOG 150 with the game's own credits artwork, its
  items placed from DITL 150 and the line after "thanks:" (item 6, a userItem with a draw proc)
  working through STR# 150's seventy names the way the original's does, and
  `[ZappaTribute]` — what pressing **x** on the title screen runs — puts up DLOG 162 with PICT 5555,
  which is drawn text rather than a picture: the Morrison poem. The title screen also takes the
  letter keys `[DoKey]` handles: a, c, i, p, q, x and z.
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
  sound, then a transition fade back to the title, which waits the transition sound out the way
  `[FunkyFade]` does rather than cutting it off
* **The high-score dialog** — `[EnterHighScore]` puts up DLOG 161 with DITL 161's own items: cicn
  128 at (15,15), the two-line prompt beside it, the name field at (61,58) pre-filled with the last
  name and selected so the first keystroke replaces it, and OK at (276,91) as the default item.
  Accepting plays snd 144 and waits it out.
* **The shooter** — `[ShooterFrameProc]` reads the frame straight out of a global that
  `[ShooterMoveProc]` turns by half the sideways movement each frame, wrapping round the 33-frame
  strip. So the crystal spins while you move and holds still when you stop; it is not a loop.
* **The rising score** — `[CreatePoints]` makes **one sprite per digit**, all starting at the centre
  of whatever was hit and each given its own sideways speed, two pixels a frame apart with the
  leftmost going left; `[PointsMoveProc]` subtracts those each frame, so the number rises a pixel a
  frame and spreads out as it goes, for twenty frames.
* **The sprinkly shield** — `[GameLoop]` runs `[ShotHitCheck]` over the 'shel' sprite every frame
  the same way it does over a shot, so the shield really is "quite lethal as well": with it up, the
  centipede, the creatures and the mushrooms all die on contact. `[ShieldMoveProc]` snaps it to the
  shooter's own position every frame - the five-pixel lift in `[MakeInvincible]` is only where it
  starts - so it sits exactly over the crystal and covers it.
* **Power-ups** — `[UpdateWeapons]`: six slots in the red box on the score panel, the multiplier and
  then the five power-ups the Yummiary page names, each shown while it lasts (the Yummielock keeps
  them when you die)
  * per-wave mushroom colours and background patterns, the end-of-wave bonus count-up, game over and
    high-score entry (saved back to `assets/scores.txt`)

## Assets

`tools/extract_assets.py` regenerates everything from the unzipped game folder (needs Pillow):

    python tools/extract_assets.py Apeiron_1_0_3.zip

Or point it at an already unzipped folder instead of the zip. That folder is the one holding `Apeiron Titles`, `Apeiron Sounds` and the hidden
`.rsrc/` folder of resource forks.

| | |
|---|---|
| `spr/*.bmp` + `.spr` | the 21 sprite sheets, frames unpacked from each PICT strip, white keyed out |
| `snd/*.wav` | all 42 sound effects plus the two music parts, 8-bit mono as they are in the game |
| `pattern_128..138` | the level backgrounds; `pattern_400` the title surround, `401/402` the rest |
| `ambrosia_logo`, `apeiron_intro`, `title_logo`, `title_morrison` | the intro and title pictures |
| `font_geneva9`, `font_chicago12`, `font_newyork14/18/24` | real Mac bitmap fonts (`.bmp` strip + `.fnt` metrics) |
| `paused.bmp` | PICT 3130, the PAUSED screen |
| `cursor_hand.cur` | crsr 128, the pointing hand, kept as RGB so it suits either colour table |
| `thanks.txt` | STR# 150, the About dialog's rotating credit line |
| `strings.txt`, `scores.txt` | STR# 133 and the shipped high-score table and preferences |

Every image is an 8-bit indexed BMP carrying the game's own palette, with its colours mapped to that
palette the way QuickDraw maps them (exact match where there is one, nearest otherwise).

### Reading the binary

The Apeiron application's **data fork** is the PEF container itself, so the PowerPC code needs no
export step:

| | |
|---|---|
| `tools/pef.py` | unpacks the container the way Ghidra's loader does and follows a routine pointer through its transition vector: `APEIRON_PEF=Apeiron python3 tools/pef.py 1001caf8` |
| `tools/pef_symbols.py` | the 250 function names out of the compiler's PowerPC traceback tables |
| `tools/apeiron_labels.py` | a Ghidra script that applies those names, so nothing is left as `FUN_`/`undefined` |
| `tools/unpack_macbinary.py` | splits a MacBinary `.bin` - the installer, say - into its data and resource forks, or lists what the resource fork holds |

New York 14/18/24 are the copies embedded in the game itself. Geneva and Chicago come from
`source_fonts/` and are Apple's fonts — keep them for personal and preservation use rather than
redistributing them.

## Known gaps and approximations

* The centipede's path logic is written from the behaviour the decompile describes, not transcribed
  from its waypoint queue, so individual turns may differ from the original in detail.
* `[HandleSideFeed]` is gated in the original by a flag this port could not identify; here at most
  three loose heads are out at once, without which they outpace the player and a wave can never end.
* A coin has **seven** outcomes, not five. `[GameHouseKeeping]` runs `FastRandom(7)` and jumps
  through a table at `TOC+0xc48` (`0x1001D1C8`) whose entries are raw addresses inside the function
  itself from `0x10003d54` on: 0 the tracking gun, 1 passing through mushrooms, 2 **an extra
  shooter** (`lives++` and snd 135 "Extra Ship" below eight, snd 148 "Missed It" at eight),
  3 the machine gun, 4 `[MakeInvincible]`, 5 `[WasteShrooms]`, 6 the Yummielock. Snd 136 "Got
  Yummie" plays for every roll but the extra shooter, which has its own sound, and `DAT_1001db20`
  is set afterwards to redraw the weapons box. `[WasteShrooms]` blows up every mushroom below
  row 18 - the rows the shooter itself can reach - with the fodder's big explosion and snd 162,
  and scores nothing.
* What each power-up does exactly, and how long it lasts, is read off the Yummiary page's
  description rather than from code, and the durations are invented. The first ("your plasma bursts will track
  along with your shooter, so you can curve 'em into your enemies") keeps the shot under the ship at
  the ship's own top speed, which is what the text describes rather than a figure from the binary —
  the shot's movement lives behind one of the sprite routine pointers below.
* The cheat codes are in STR# 132, ROT13'd and matched backwards by `[DoKey]` while paused
  (`URPGBE` is HECTOR, and so on). They are not implemented here.
* **Sprite behaviour in the original lives behind per-sprite function pointers.** Each sprite keeps
  a frame routine at +0x08 and a movement routine at +0x0c, which `[AnimateFrame]` calls once each
  per frame, and which the decompile shows only as data addresses. **These have now been read and
  ported**, so the creatures move and animate off the original's own code rather than off a
  description of it. What each one turned out to be:

  **These are no longer a mystery.** The application's data fork *is* the PEF container, and
  `tools/pef.py` unpacks it the way Ghidra's loader does - code at `0x10000000`, the pattern-packed
  data section straight after it at `0x1001c580`, which is the TOC base - so a routine pointer can
  be followed to its transition vector and on to the code without leaving this repository. On top of
  that, the compiler left a PowerPC traceback table after every function, which is where Ghidra's
  `.debug::_Name` labels come from: `tools/pef_symbols.py` reads all 250 of them out, and
  `tools/apeiron_labels.py` is a Ghidra script that applies the lot, so the functions Ghidra left as
  `FUN_`/`undefined` get their real names. Every creature keeps a frame routine at +0x08 of its
  sprite and a movement routine at +0x0c:

  | sprite | +0x08 pointer -> frame proc | +0x0c pointer -> move proc |
  |---|---|---|
  | gecko (Gordon) | `0x1001caf8` -> `GeckoFrameProc` `0x10007a10` | `0x1001cafc` -> `GeckoMoveProc` `0x10007904` |
  | scorpion (Larry) | `0x1001caf0` -> `ScorpianFrameProc` `0x10008bd0` | `0x1001caf4` -> `ScorpianMoveProc` `0x10007a58` |
  | fodder (the UFO) | `0x1001ca90` -> `FodderFrameProc` `0x100086b8` | `0x1001ca94` -> `FodderMoveProc` `0x10008704` |
  | flea | `0x1001ca88` -> `FleaFrameProc` `0x100078bc` | `0x1001ca8c` -> `FleaMoveProc` `0x100075e4` |
  | the shot | `0x1001cb54` -> `ShotFrameProc` `0x10009bb4` | `0x1001cb58` -> `ShotMoveProc` `0x10009acc` |
  | yummie coin | `0x1001ca78` -> `YummieFrameProc` `0x100080f8` | `0x1001ca7c` -> `YummieMoveProc` `0x10007f00` |
  | uprooted shroom | `0x1001caa8` -> `RootFrameProc` `0x100089f4` | `0x1001caac` -> `RootMoveProc` `0x10008a18` |
  | multiplier tile | `0x1001ca98` -> `MultFrameProc` `0x1000848c` | `0x1001ca9c` -> `MultMoveProc` `0x100084b0` |
  | psychedelic shroom | `0x1001caa0` -> `PsychFrameProc` `0x100083bc` | `0x1001caa4` -> `PsychMoveProc` `0x10008408` |
  | shield | `0x1001cb7c` -> `ShieldFrameProc` `0x10008534` | `0x1001cb80` -> `ShieldMoveProc` `0x10008670` |
  | pede | `0x1001cb68` -> `PedeMoveProc` `0x10008c18` | `0x1001cb60`/`64` -> `PedeFrameProc` `0x100095d8` |

  `ShooterMoveProc` `0x100096c8`, `ShroomMoveProc` `0x10007594`, `ExploMoveProc` `0x10008bac`,
  `RicochetMoveProc` `0x10008958` and `FlakMoveProc` `0x10008190` are the rest of the set.

  **Gordon the gecko** (`GeckoMoveProc`). He walks one way across the field and poisons the first
  mushroom under him that is still at full health - frame > 4 becomes frame 1, which is exactly what
  `[PedeMoveProc]` tests for - with snd 160 "Splut", and is gone when he reaches the far side.
  `GeckoFrameProc` runs frames 1..8 and no further, so the ten frames of him with his head down are
  never used, in play or on the Bestiary page.

  **Larry the scorpion** (`ScorpianMoveProc`). He drifts up and down and only moves sideways while a
  flag of his own is set; once in 18 frames - once in 30 past 5000 points - he flips his vertical
  direction and re-rolls that flag. He *eats* what he crosses rather than poisoning it, with one of
  the two munching sounds (141/142), and bounces off the top and bottom of the field.

  **The fodder** (`FodderMoveProc`). She blows up any mushroom she passes over - an explosion and
  snd 162 - and once in ten frames picks a new vertical drift from {-3, 0, +3}, which is what makes
  her wander instead of flying straight. Leaving either side is a "Missed It".

  **The flea** (`FleaMoveProc`). One with sideways speed is a bouncing flea: half a pixel of gravity
  a frame up to a terminal sixteen, a floor bounce two pixels a frame slower each time, and it dies
  once the next bounce would be under ten. One with no sideways speed falls at a steady rate and is
  simply gone at the floor. Either way it plants a mushroom in one row out of four that it enters,
  with snd 160, and may then drop suddenly at eight pixels a frame.

  **The shot** (`ShotMoveProc`). Exactly sixteen pixels a frame, and with the Z-145 transflogulator
  up its x is *assigned* the shooter's x plus eight every frame rather than easing towards it - the
  shot is locked under the ship, so moving while one is in the air bends its whole path.

  **The coin** (`YummieMoveProc`), **the uprooted mushroom** (`RootMoveProc`), **the multiplier tile
  and the psychedelic shroom** (`MultMoveProc` and `PsychMoveProc`, which are the same routine -
  three hundred frames and then a "Missed It"), **the rising score** (`PointsMoveProc`, twenty
  frames) and **the splash** (`ScumMoveProc`) are ported the same way. `[ShieldFrameProc]` is the
  one place a power-up's life is written down: it warns at 1620 ticks with snd 168 "Warning Will
  Robinson!" and gives up at 1800, which at two ticks a frame is the 900 frames used here for all
  five.

  **The instruction pages** come from the same place. `[CreateSprites]` hands each page sprite one
  of six tiny frame routines - `GenericFrameProc` (the whole strip), `NoFrameProc` (the mushroom and
  the multiplier tile, which do not animate at all), `myGeckoFrameProc` (1..8), `HeadFrameProc`
  (1..14), `LinkFrameProc` (32..45) and `MushFrameProc` (+4 at a time from 5, which is the
  psychedelic shroom's colour flash) - and those are now what the pages run.

  The two that were followed by hand earlier:

  **The centipede** (`0x10008c18`). Direction is a byte — 1 left, 2 right, 3 up, 4 down — held with
  the direction to take after the current vertical step and the row edge that step stops at. A head
  walks sideways until the wall, a mushroom or an oncoming segment stops it, then takes one vertical
  step and resumes the other way. At the floor it bounces back up and, the first time that happens
  in a wave, arms the flag `[HandleSideFeed]` waits on — which is why lone heads only start arriving
  after the centipede has got all the way down. A mushroom in frames 1..4 is a poisoned one (hence
  `[PlantShroom]` starting at frame 5), and a head that touches one heads straight for the floor
  instead of turning. A body runs none of this: it reads one frame of its leader's ten-entry trail
  per frame, and `[StartLevel]` pre-fills 16/speed entries so each segment trails by exactly a cell.

  **The coin** (`0x10001828`). Gravity of one pixel per frame per frame, a floor bounce that loses a
  pixel a frame until it is too slow and the coin is thrown afresh, and a hard clamp to the 48-pixel
  strip it spawns in. The uprooted mushroom and the in-game coin launch with identical values and
  now fall the same way, though their own routines have not been read.
* The frame length (`DAT_1001ddac`, the number of ticks `[CompositeFrame]` waits out) is one of the
  unresolved globals; two ticks is inferred from the movement speeds, not read from the binary.
* Shot speed, several animation rates and the yummie power-up effects are chosen to match the
  behaviour described rather than read from the original's tables.
* `[GameOver]` swaps in PICT 667 past wave 15 - the shareware notice asking to be paid for. It is
  extracted but never drawn: the caterpillar sequence runs however the game ended.
* Each `[CreateExplosion]` call names its own artwork through the sprite definition it is passed:
  the shooter, the fodder and whatever the fodder flies over all use PICT 1009, a centipede segment
  PICT 1011, a flea PICT 1012, the scorpion PICT 1014 and the gecko PICT 1020. `[ExploFrameProc]`
  plays one frame a frame and stops on the last.
* Sound effects are played into streams opened once and bound to one audio device for the life of
  the program. Opening a device stream per effect - which this used to do - renegotiates the
  hardware each time and costs the best part of a frame or more, which with a sound on nearly every
  mushroom hit is audible as a stutter.
* `[CountOffShrooms]` leaves a poisoned mushroom poisoned at the end of a wave (frame under 5 goes
  back to 1) and waits seven ticks on each one. Here they are restored like the rest, and the wait
  is two ticks, because the original's run is slow enough to sit through.
* Most sprite sheets are drawn with a one-pixel rule between frames and a one-pixel margin, so the
  first frame starts at (1,1); a few - the weapon icons on the score panel among them - are a bare
  frame with no margin, and reading those from (1,1) shaved a row and a column off each. The
  extractor now works out which layout a sheet uses from its own dimensions.
* `[ShieldFrameProc]`'s warning at 1620 of its 1800 ticks is the original's only power-up timer, and
  it only covers the shield. Here every power-up warns, since none of them show a countdown. Snd
  168 is genuinely 0.146s long (3256 samples at 22254.5 Hz) - the extraction is not truncating it.
  `[ShieldFrameProc]` tests `elapsed > 1620` on every frame and plays it whenever it is not already
  playing, so over the last 180 ticks it is a rapid stutter rather than a single beep.
* **What can take a shooter.** `[GameHouseKeeping]` tests the shooter against two sprite layers.
  `DAT_1001db48` holds `'phed'` and `'pbod'` and kills outright. `DAT_1001db50` holds everything
  else that moves - yumy, fodr, flea, root, psyc, mult, scor, geck - and its handler reads the tag
  twice: `'yumy'` is the pickup, and then at `0x10003e0c` anything that is **not** `'psyc'` falls
  into a second copy of the death path. So the coin and the psychedelic mushroom are the only two
  things safe to touch; the flea, the fodder, Larry, Gordon, an uprooted mushroom and a multiplier
  tile all take a shooter. An earlier round of this port read only the first tag test and made
  everything but the centipede harmless, which was wrong.
* **The psychedelic mushroom.** Shooting a `'psyc'` sets `DAT_1001da52`, halts the music and puts
  the points multiplier to ten for 900 ticks - fifteen seconds. `[GameLoop]` then swaps the whole
  colour table every six ticks through three that `[ApeironIntro]` built with `[TransmogrifyClut]`,
  which rotates each entry's components one place (red takes green's value, green blue's, blue
  red's). Three rotations land back on the original, so the screen cycles GBR, BRG, normal, and the
  music comes back with the normal table at the end. This is the only palette work the game does
  while a wave is running, and it goes through `SetEntries` rather than `SetMonitorClut`.
* **The centipede's direction lives on the centipede, not the segment.** `[PedeMoveProc]` turns a
  head at an oncoming segment by running a `[CollisionCheck]` against `DAT_1001db48` and then
  testing the direction held in the *other* sprite's record at `+0x58`. That record is shared by
  every segment of one centipede, so a piece of this same centipede always reads back this
  segment's own direction - never the opposing one - and is skipped without the routine needing a
  test for it. This port keeps a direction per segment, and a follower's lags its head's by a
  frame: the instant a head reversed, the segment right behind it still carried the old direction
  and looked exactly like something coming the other way, so the head turned again, and again,
  stepping a row each time and bouncing up and down on the spot with its one follower - no
  mushroom and no wall anywhere near it. Comparing chains is what the shared record was doing for
  free. The upward bounce is clamped to the same `DAT_1001db2c.top` the shooter is held to, 368,
  so the oscillation stayed inside the six-row band at the bottom.
* **A psychedelic mushroom does not survive the wave.** `[StartLevel]` clears `DAT_1001da52` and
  `DAT_1001da50` right after `InitOnTheFlyFade`. That matters more here than there, because this
  port restarts `ticks` at zero for each wave: a `psyc_until` left standing from the old wave can
  never be reached again, and the rotated colour table would stay on the monitor until the next
  death put it back.
* **The shooter's six rows.** `[InitializeAnimation]` builds its rect as `bottom = field bottom -
  shooter height` and `top = bottom - 96`, so the shooter has 96 pixels to move in, not twelve
  rows. `[PedeMoveProc]` bounces off the same `top` on the way back up.
* **No firing during the count-up.** `[CountOffShrooms]` and `[TallyBonus]` call `[HandleInput]` and
  `[AnimateFrame]` but not `[GameHouseKeeping]`, and `[ShooterMoveProc]` makes no calls of its own.
  So the shooter still follows the mouse while the bonus counts down, and no shot can be made.
* **The Lives box is eight fixed slots.** `[DrawLives]` always lays out eight - the same cap the
  extra-shooter roll uses - twenty pixels apart from `DAT_1001daf8`'s `{top 340, left 597}`,
  stepping left. The first `lives` get PICT 1101, a shooter in hand; the rest get PICT 1102, the
  same empty socket the weapons box uses. Nothing shifts along as the count changes. `[NewGame]`
  sets `DAT_1001da70` to **3**, which with the one on the field is the four the Hints file promises.
* **The whole centipede animates as one.** `[PedeFrameProc]` keeps its counter in the shared record
  at `+0x58`, not on the segment, so every piece shows the same frame and the eyes blink in unison
  - one frame per game frame, cycling 14 of them sideways and 3 vertically, with bodies offset 31
  frames into the strip from heads.
* **The shield's edge is the artwork.** PICT 1019 is a 16x16 rounded square of random confetti with
  only the four corner pixels on the white backdrop that `CompileSprite` keys out. Near-white
  confetti lands on the perimeter, so on a dark background it reads as a dotted outline with
  rounded corners. That is the sprite, not a transparency fault.
* **Nothing can take you while the shield is up.** Both death paths in `[GameHouseKeeping]` are
  gated on `DAT_1001db24`, the flag `[MakeInvincible]` sets.
* **A click on nothing.** `[TitleClick]` ends with an else that plays snd 138, "Groucho Ouch", at
  priority 2 for a click that lands on none of its items.
* **Where the installer's files are.** The `.bin` the game shipped as is an Installer VISE 3
  application (`APPL`/`VIS3`). Its data fork is an archive in VISE's own format: the payload
  measures 7.94 bits per byte, so nothing is stored plainly, and the decompressor the installer
  carries - `Dcmp 1005`, "Version 27 (PPC, decomp)" - is itself packed rather than plain PowerPC
  code. Unpacking it means reverse engineering VISE's container first, which is a separate job.
  `tools/extract_assets.py` recognises the installer and says so rather than half-unpacking it.
* **The shot can bounce, but only late.** `[ShotHitCheck]` branches on the mushroom's damage
  frame: a multiple of four finishes it, `frame % 4 == 1` can uproot it, and anything else can
  bounce the shot. Both of those odds count off `DAT_1001da2c`, which `[NewGame]` sets to 1 and
  `[EnterHighScore]` files beside the score - it is the **wave**, not a mushroom count. The bounce
  needs `wave > 22` and a hit in row 18 or above, then a one-in-n roll where `n = 70 - wave` with a
  **floor** of 20 (`cmpwi 20; bf 0` skips the assignment when n is already 20 or more). So it is 1
  in 47 the wave it first appears and does not shorten to 1 in 20 until wave 50. It plays snd 155
  "Ricochet" and hands the shot to `[CreateRicochet]`, which drops it at 8 to 13 pixels a frame
  until it is off the bottom - and puts it in `DAT_1001db50`, not the shot layer, so your own
  bounced shot can take your shooter. The uproot roll has the same shape: `n = 50 - wave`, floor
  10, gated on `wave > 11`.
* **The machine gun has its own report.** `[ShroomShotCheck]` ends on the machine-gun flag: with it
  up every shot is snd 167 "Bam!", and otherwise snd 133 "Ship Laser".
* **The side feed's sound** is `DAT_1001dc44`, which `[HandleInitializations]` loads from snd 152.
  The resource is filed under "Killed Root", but `[HandleSideFeed]` is the only thing that plays
  it: it is the hiccup a new head makes coming in from the side.
* **All 42 sounds are accounted for.** Six are loaded on demand into locals rather than into the
  globals from `DAT_1001dbf4` up - 128 Transition, 139 Pause, 143 Intro Theme, 144 Level Clear,
  145 Oh no and 150 Hooka Music (the game-over screen, under clut 888) - which is why they do not
  show up in a scan of the sound globals' call sites.
* **cicn 128**, the face on the high-score dialog, goes up over a palette that is not the game's
  (`[ScoreContest]` installs one of its own before `[EnterHighScore]`), so it is extracted with its
  own colour table and matched to whatever is on the monitor when it is drawn.
* Not in the original: the title screen's right-hand column is shifted up sixteen pixels to make
  room for the Options entry on the end of it. Everything else in it keeps the original's spacing.
* The expanding outlines in front of both dialogs are `[ZoomShowWindow]`'s, drawn over the title
  screen rather than through the window manager's port. Neither has one on the way out: both simply
  `HideWindow`, in silence.
* The sound handles turned out not to be unresolved. `[HandleInitializations]` loads them all in one
  run into a block of globals from `DAT_1001dbf4` up, so `DAT_1001dcXX` names a sound and so does an
  offset from the TOC base (`0x1001c580`): `*(TOC+0x1698)` is `DAT_1001dc18`, which is
  `ST_LoadSound(0x87)` - snd 135 "Extra Ship", the one `[DoAbout]` plays. **Every `ST_SimplePlay` in
  the binary has been resolved this way and the port's sounds corrected against it**, so nothing
  here is a guess any more. `tools/dis.py` prints them inline when it disassembles.

  The ones that were wrong: a mushroom taking a hit is snd 129 "Hit", not "Shroom Pop"; a flea takes
  138 "Groucho Ouch" and dies on 137 "Groucho Death" then 130 "Squish"; Larry dies on 146 and 130;
  the fodder goes on 162 "Explosion" then 156 "myHooHa"; an uprooted mushroom takes 131 then 164
  "Hey!"; shooting a multiplier tile is 151 "Moan"; uprooting one is 149 "Diehl Belch"; a flea
  arrives on 140 "Groucho Laugh" and a psychedelic shroom on 153 "Riff"; the bonus count-up ticks on
  159 "Bonk" and finishes on 156, 151 and - only if a multiplier survived - 154; dying is 132 alone,
  since 145 "Oh no" belongs to the Clear Scores dialog; and on the title screen every letter key is
  130 "Squish" with 138 "Groucho Ouch" for one the game does not know.
* Gordon's 18 frames are two pieces of animation, not one loop - 9-18 walk with his head down (12-13
  are a blink) and 1-8 rear up and pulse his dewlap - so running all 18 in order made him snap his
  head up and down twice a second. He is the one creature `[CreateSprites]` gives a routine of its
  own (`PTR_PTR_1001c988` where everything else gets `PTR_PTR_1001c9b0`) and that routine could not
  be followed, so here he walks three times round 9-18 and then does the display once.
* Not ported: the loading screen's status text, the shareware notice, the cheat codes, the
  difficulty setting, InputSprocket configuration, and the original's dirty-rectangle redraw (this
  redraws every frame).
* Backgrounds 128, 129 and 134 were authored against a different palette from the game's, so they
  dither when mapped to it; the other eight map exactly.

## Testing

Built and run only against a headless stand-in for SDL3 that implements the calls this port uses
(software renderer to PPM frames, a fake clock and scripted input). The game was played through to
wave 18 that way to check wave progression, scoring, death and respawn. It has not been built against
real SDL3, so API mismatches are possible; the gameplay has never been seen or heard at full speed.
