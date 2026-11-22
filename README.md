# Apeiron 1.0.3 — SDL3 port

A playable port of Ambrosia Software's classic Mac game **Apeiron** to SDL3, for Windows and Android
(including Google TV). It is built by reverse engineering the game's own PowerPC program, and it uses
the game's original artwork, sounds and fonts, so nothing is redrawn or re-recorded.

## Step 1: get the game's artwork (you need this to play)

The artwork belongs to Ambrosia, so it isn't included. The game makes it from a zip of your own copy
of Apeiron, and the easiest way to get that zip is a browser emulator:

1. Open the [Apeiron page on macintoshrepository.org](https://www.macintoshrepository.org/5191-apeiron),
   then its [emulator](https://www.macintoshrepository.org/emulate_in_browser.php?macmodel=8&disk1=MacOS753.dsk&disk2=34294_Apeiron-1.0.3.dsk&relativemouse=1).
2. In the emulator, drag the **Apeiron folder** onto **The Outside World → Uploads**.
3. It offers a zip to download. Keep it, ideally in your Downloads folder.

## Step 2: play

Download the latest build from the [Releases page](../../releases) and run it.

- **Windows:** unzip and run `apeiron.exe`.
- **Android / Google TV:** install the `.apk`.

The first time, the game looks for the zip (Downloads, Desktop, Documents, or beside the game),
offers to make the artwork from it, and remembers. If it can't find the zip it asks you to choose it.

## Controls

| | |
|---|---|
| Move | Arrow keys, WASD, mouse, or a controller's stick / d-pad |
| Fire | Space, Ctrl, mouse button, or any controller face button |
| Pause | Caps Lock, or Start on a controller |
| Quit to title | Escape |
| Fullscreen | F11 |

The title screen's **Options** page (key **O**) has graphics, controls, artwork packs and the
original's cheats.

## Building it yourself

Needs SDL3 3.2 or newer:

    cmake -B build && cmake --build build
    ./build/apeiron

Android builds are in `android/`; the GitHub workflows build both targets.

## More

Implementation notes, the full feature list and known differences from the original are in
[docs/NOTES.md](docs/NOTES.md). Tools for unpacking the original files are in `tools/`.
The menu fonts are free stand-ins for Apple's Geneva and Chicago (pixChicago, Geneva 9.1; SIL Open
Font License, see `tools/fonts/ATTRIBUTION.txt`).
