package io.github.takasky85.apeiron;

import android.annotation.TargetApi;
import android.os.Build;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;

import org.libsdl.app.SDLActivity;

/**
 * The game's one workaround for SDL 3.4.20 on Android.
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
