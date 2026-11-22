package io.github.takasky85.apeiron;

import android.annotation.TargetApi;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.Bundle;
import android.provider.Settings;
import android.view.MotionEvent;
import android.view.View;

import org.libsdl.app.SDLActivity;

/**
 * Two small things for the Android build.
 *
 * 1. A workaround for SDL 3.4.20.
 *
 * While the pointer is captured - which is how the game steers with a mouse - Android reports the
 * buttons as ACTION_BUTTON_PRESS and ACTION_BUTTON_RELEASE. SDL's Java hands those on unchanged,
 * and SDL's native Android mouse code only understands ACTION_DOWN and ACTION_UP, so every click
 * made while captured is silently dropped: the mouse moves the shooter and can never fire.
 *
 * This puts a listener in front of SDL's own, turns the two actions into the ones SDL understands
 * and passes them on. Everything else goes through to SDL untouched.
 */
public class MainActivity extends SDLActivity {

    /**
     * "All files access" (Android 11 and later): the switch that lets the game read the artwork
     * out of the Download folder, the same permission ScummVM asks for. Called from the game's
     * native code through JNI, which is why these are plain public methods.
     */
    public boolean hasAllFilesAccess() {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
        return true;       // before Android 11 the ordinary storage permission does the job
    }

    /** Opens this app's "All files access" page in Settings. False if the device has none. */
    public boolean requestAllFilesAccess() {
        if (Build.VERSION.SDK_INT < 30) return false;
        try {
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                     Uri.parse("package:" + getPackageName())));
            return true;
        } catch (Throwable t) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                return true;
            } catch (Throwable t2) {
                return false;
            }
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= 26) fixCapturedClicks();
    }

    @TargetApi(26)
    private void fixCapturedClicks() {
        View.OnCapturedPointerListener fix = new View.OnCapturedPointerListener() {
            @Override
            public boolean onCapturedPointer(View v, MotionEvent e) {
                int action = e.getActionMasked();
                if (action == MotionEvent.ACTION_BUTTON_PRESS) {
                    SDLActivity.onNativeMouse(e.getButtonState(), MotionEvent.ACTION_DOWN, 0f, 0f, true);
                    return true;
                }
                if (action == MotionEvent.ACTION_BUTTON_RELEASE) {
                    SDLActivity.onNativeMouse(e.getButtonState(), MotionEvent.ACTION_UP, 0f, 0f, true);
                    return true;
                }
                return false;      // movement and the rest: SDL's own handling
            }
        };
        // Whichever of the two views Android hands captured events to.
        if (mSurface != null) mSurface.setOnCapturedPointerListener(fix);
        View content = SDLActivity.getContentView();
        if (content != null) content.setOnCapturedPointerListener(fix);
    }
}
