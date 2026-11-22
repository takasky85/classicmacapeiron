#!/usr/bin/env python3
"""Build downloads/apeiron-asset-tools.zip: the asset extractor and what it needs, on its own.

The zip is flat (no folder inside it) so unpacking it never makes a folder in a folder, and it is
built the same way every time so CI can check the committed copy is current:

    python3 tools/make_tools_zip.py           write downloads/apeiron-asset-tools.zip
    python3 tools/make_tools_zip.py --check   exit 1 if the committed zip is out of date
"""
import os, sys, zipfile, io

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, 'downloads', 'apeiron-asset-tools.zip')
STAMP = (2026, 1, 1, 0, 0, 0)          # fixed, so the same input always gives the same bytes

PY_FILES = ['extract_assets.py', 'rsrc.py', 'pict.py', 'unpack_macbinary.py']

README = """Apeiron asset tools
===================

Turns your own copy of Apeiron 1.0.3 into the folder of artwork, sounds and fonts that the SDL3
port needs. The port does not ship Ambrosia's assets; you make them from the game you have.

What you need
  * Apeiron_1.0.3.zip (the Mac game, as you downloaded it)
  * Python 3 and Pillow. On Windows, install Python from python.org (tick "Add to PATH"); the
    .bat file below installs Pillow for you.
  * Optional: Geneva.zip and Chicago.zip (Apple's fonts, from a Mac). Without them the extractor
    still works and uses the game's own New York font in their place, and tells you so.

How to use it
  Windows:  unzip this, then drag Apeiron_1.0.3.zip onto "Extract Apeiron Assets.bat".
            (Or put the game zip in Downloads, or in this folder, and double-click the .bat.)
  Anywhere: python3 extract_assets.py Apeiron_1.0.3.zip

The result is a folder called apeiron_classic_assets, written next to the game zip.
  Windows: put it beside apeiron.exe.
  Google TV / Android: put it in the Download folder.

To get the real Geneva and Chicago fonts, keep Geneva.zip and Chicago.zip in the same folder as
the game zip (or in Downloads) and run it again. It finds them by itself.

If something goes wrong, copy the whole message in the window and send it on.
"""

BAT = r"""@echo off
rem Drag Apeiron_1.0.3.zip onto this file, or double-click it with the zip in Downloads.
cd /d "%~dp0"
set PY=
where py >nul 2>nul && set PY=py
if not defined PY where python >nul 2>nul && set PY=python
if not defined PY (
  echo Python 3 is not installed. Get it from https://www.python.org/downloads/
  echo and tick "Add python.exe to PATH" in the installer, then try again.
  pause
  exit /b 1
)
%PY% -c "import PIL" >nul 2>nul || (
  echo Installing Pillow, the image library this needs...
  %PY% -m pip install --user pillow
)
%PY% extract_assets.py %*
"""


def members():
    out = [(n, open(os.path.join(HERE, n), 'rb').read()) for n in PY_FILES]
    out.append(('README.txt', README.replace('\n', '\r\n').encode('utf-8')))
    out.append(('Extract Apeiron Assets.bat', BAT.replace('\n', '\r\n').encode('utf-8')))
    return out


def build():
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in members():
            zi = zipfile.ZipInfo(name, STAMP)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = 0o644 << 16
            z.writestr(zi, data)
    return buf.getvalue()


if __name__ == '__main__':
    data = build()
    if '--check' in sys.argv:
        # Compare what is inside, not the compressed bytes: those can differ between zlib versions.
        try:
            with zipfile.ZipFile(OUT) as z:
                have = [(i.filename, z.read(i)) for i in z.infolist()]
        except (OSError, zipfile.BadZipFile):
            have = None
        if have != members():
            sys.exit('downloads/apeiron-asset-tools.zip is out of date: run tools/make_tools_zip.py and commit it')
        print('asset tools zip is current')
    else:
        os.makedirs(os.path.dirname(OUT), exist_ok=True)
        open(OUT, 'wb').write(data)
        print('wrote %s (%d bytes)' % (OUT, len(data)))
