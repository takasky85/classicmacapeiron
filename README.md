# Apeiron 1.0.3 — SDL3 port

Ambrosia Software's classic Mac game **Apeiron**, ported to SDL3 for Windows and Android / Google TV,
using the game's own artwork, sounds and fonts.

## Get the artwork first

The artwork is Ambrosia's, so it isn't included. The game makes it from a zip of your own copy:

1. Open the [Apeiron page on macintoshrepository.org](https://www.macintoshrepository.org/5191-apeiron),
   then its [emulator](https://www.macintoshrepository.org/emulate_in_browser.php?macmodel=8&disk1=MacOS753.dsk&disk2=34294_Apeiron-1.0.3.dsk&relativemouse=1).
2. In the emulator, drag the **apeiron folder** onto **The Outside World → Uploads**.
3. It offers a zip to download. Save it in your Downloads folder.

## Play

Get the latest build from [Releases](../../releases): the Windows zip (run `apeiron.exe`) or the
Android `.apk`. On first start the game finds the zip, offers to make the artwork from it, and
remembers. If it can't find the zip it asks you to pick it.

Move: arrows, WASD, mouse or controller. Fire: Space, Ctrl, mouse button or any face button.
Pause: Caps Lock or Start. Quit to title: Escape. Fullscreen: F11. Options: **O** on the title screen.

## Build

    cmake -B build && cmake --build build      # needs SDL3 3.2+

Android is in `android/`. Notes on how it works and where it differs from the original are in
[docs/NOTES.md](docs/NOTES.md).
