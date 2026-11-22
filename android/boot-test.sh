#!/usr/bin/env bash
#
# Install the APK on whatever device adb is talking to, start it, and say whether it came up.
#
# This exists because the APK was the one build nothing could check. Compiling it proves the
# manifest parses and the sources compile; it says nothing about whether the thing runs, and a
# build that exits during startup is indistinguishable, from the sofa, from one that never
# started - which is how "it installs but it does not open" came to be a release.
#
# Run from the top of the checkout, with an emulator or a device attached:
#
#     ./android/boot-test.sh
#
# It fails if the process is gone, or if the game never got as far as a window.
set -uo pipefail

PKG=io.github.takasky85.apeiron
WANT="apeiron: window and renderer up"

apk=$(find android/app/build/outputs/apk -name '*.apk' -type f | head -1)
if [ -z "$apk" ]; then
    echo "no APK under android/app/build/outputs/apk - build one first"
    exit 1
fi
echo "installing $apk"

adb wait-for-device
# -g grants the runtime permissions up front. The game asks for storage itself, but nobody is
# here to tap "allow", and what is being tested is the startup path rather than the dialog.
adb install -r -g "$apk" || { echo "install failed"; exit 1; }

adb logcat -c
adb shell am start -W -n "$PKG/.MainActivity" || { echo "am start failed"; exit 1; }

# Long enough for the game to open its window and draw the folder browser, or to die trying.
sleep 20

pid=$(adb shell pidof "$PKG" 2>/dev/null | tr -d '\r\n')

echo "=================================================================== logcat (ours and SDL)"
adb logcat -d -v brief -s SDL:V SDLActivity:V apeiron:V AndroidRuntime:E DEBUG:V libc:F \
    ActivityManager:W 2>/dev/null | tail -150

echo "=================================================================== logcat (everything, tail)"
adb logcat -d 2>/dev/null | tail -80

# A picture of the screen, smuggled through the log: artifact downloads are not reachable from
# every environment that needs to read this, and a log is. Lines are tagged so they can be
# pulled out and turned back into a PNG with:
#     grep '^SHOT:' log | cut -c7- | base64 -d > shot.png
echo "=================================================================== screenshot"
if adb exec-out screencap -p > /tmp/shot.png 2>/dev/null && [ -s /tmp/shot.png ]; then
    if command -v convert >/dev/null 2>&1; then
        convert /tmp/shot.png -resize 640x -colors 64 /tmp/shot-small.png 2>/dev/null \
            && mv /tmp/shot-small.png /tmp/shot.png
    fi
    size=$(stat -c%s /tmp/shot.png)
    echo "screenshot is $size bytes"
    if [ "$size" -lt 500000 ]; then
        base64 -w 120 /tmp/shot.png | sed 's/^/SHOT:/'
    else
        echo "too big to put in the log"
    fi
else
    echo "could not take one"
fi

echo "=================================================================== verdict, first launch"
first_ok=1
if [ -z "$pid" ]; then
    echo "FAIL: $PKG is not running 20s after being started"
    first_ok=0
else
    echo "process alive as pid $pid"
    if adb logcat -d 2>/dev/null | grep -qF "$WANT"; then
        echo "PASS: reached '$WANT'"
    else
        echo "FAIL: the process is alive but never reported '$WANT'"
        first_ok=0
    fi
fi

# And again, without killing the process in between. This is the case that was actually
# reported: SDLActivity keeps "main has finished" and "the activity has been created" in static
# fields, which outlive the activity for as long as Android keeps the process around - and it
# keeps it around long after the window is gone. Its default answer to either is System.exit(0),
# so the second launch can draw nothing at all while the first was fine. Going to the home
# screen and coming back is the shortest way to ask that question.
echo "=================================================================== second launch"
adb shell input keyevent KEYCODE_HOME
sleep 3
adb logcat -c
adb shell am start -W -n "$PKG/.MainActivity" || { echo "am start failed"; exit 1; }
sleep 12

pid2=$(adb shell pidof "$PKG" 2>/dev/null | tr -d '\r\n')
adb logcat -d -v brief -s SDL:V SDLActivity:V apeiron:V AndroidRuntime:E 2>/dev/null | tail -60

second_ok=1
if [ -z "$pid2" ]; then
    echo "FAIL: nothing running after the second launch"
    second_ok=0
elif adb logcat -d 2>/dev/null | grep -q "activity finished"; then
    echo "FAIL: SDLActivity took the System.exit(0) branch on relaunch"
    second_ok=0
else
    echo "PASS: still running as pid $pid2 after a relaunch"
fi

[ "$first_ok" = 1 ] && [ "$second_ok" = 1 ] && exit 0
exit 1
