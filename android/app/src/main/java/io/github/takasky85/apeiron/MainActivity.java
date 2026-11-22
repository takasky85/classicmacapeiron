package io.github.takasky85.apeiron;

import android.Manifest;
import android.content.ContentValues;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.MediaStore;
import android.util.Log;
import android.view.InputDevice;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.Locale;

/**
 * SDLActivity does the real work; this subclass exists to leave a log behind.
 *
 * A TV box has no terminal, and an app that closes the moment it opens says nothing about why. So
 * from the first line of onCreate - before SDL, and before any native code has been loaded - this
 * writes a plain text file into the Download folder, "apeiron-log-<date>-<time>.txt", that can be
 * picked up and read. It holds:
 *
 *   - what the device is, and which CPU types it can run;
 *   - which native libraries came with the install, and whether each one actually loads (the
 *     usual reason an app opens and closes at once is a library that will not load, and the error
 *     says exactly why);
 *   - which input devices Android reports - the answer to "is the mouse being seen at all";
 *   - the app's own lines from the system log, which includes everything the game printed while
 *     starting and, because the log outlives the process, the end of the previous run: so an app
 *     that crashed leaves its reason in the file written by the NEXT launch.
 *
 * The file is rewritten a few seconds in, when the app is paused, on a Java exception and as the
 * process exits, so it is as complete as it can be whichever way the run ends. A native crash
 * gives no warning; that is the case the next launch's file is for.
 *
 * On Android 10 and later the file goes in through MediaStore, which lets an app add its own file
 * to Downloads with no permission. On older Android it is written to Downloads directly, which
 * the READ permission's group covers once it has been granted, and failing that to the app's own
 * folder. A copy is always kept in the app's private folder too.
 */
public class MainActivity extends SDLActivity {

    private static final String TAG = "ApeironLog";

    private final Object lock = new Object();
    private String logName;
    private Uri logUri;              // the Downloads entry, Android 10 and later
    private boolean saidWhere;
    private String saveNote = "";     // why a destination failed, shown in the toast and the file

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        logName = "apeiron-log-" + new SimpleDateFormat("yyMMdd-HHmmss", Locale.US).format(new Date()) + ".txt";

        final Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override
            public void uncaughtException(Thread t, Throwable e) {
                writeReport("uncaught Java exception on thread " + t.getName(), e);
                if (previous != null) previous.uncaughtException(t, e);
            }
        });
        Runtime.getRuntime().addShutdownHook(new Thread() {
            @Override
            public void run() {
                writeReport("the process is exiting (System.exit, or a normal end)", null);
            }
        });

        Log.i(TAG, "launch " + logName);
        writeReport("before SDL starts", null);

        super.onCreate(savedInstanceState);

        Thread later = new Thread() {
            @Override
            public void run() {
                try {
                    Thread.sleep(5000);
                    writeReport("5 seconds after start", null);
                    Thread.sleep(15000);
                    writeReport("20 seconds after start", null);
                } catch (InterruptedException ignored) {
                }
            }
        };
        later.setDaemon(true);
        later.start();
    }

    @Override
    protected void onPause() {
        super.onPause();
        writeReport("the app was paused (another window took the screen, or it is closing)", null);
    }

    @Override
    protected void onDestroy() {
        writeReport("the activity is being destroyed", null);
        super.onDestroy();
    }

    // ---------------------------------------------------------------------------------- the report

    private void writeReport(String why, Throwable err) {
        synchronized (lock) {
            try {
                StringBuilder sb = new StringBuilder();
                sb.append("Apeiron log, written ").append(new Date()).append('\n');
                sb.append("because: ").append(why).append("\n\n");
                describeDevice(sb);
                describeLibraries(sb);
                describeInput(sb);
                if (err != null) {
                    StringWriter sw = new StringWriter();
                    err.printStackTrace(new PrintWriter(sw));
                    sb.append("\nJava exception:\n").append(sw).append('\n');
                }
                sb.append("\n---- this app's system log, oldest first (earlier runs included) ----\n");
                appendLogcat(sb);
                save(sb.toString());
            } catch (Throwable t) {
                Log.e(TAG, "could not write the log", t);
            }
        }
    }

    private void describeDevice(StringBuilder sb) {
        sb.append("device:   ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
          .append(" (").append(Build.DEVICE).append(")\n");
        sb.append("android:  ").append(Build.VERSION.RELEASE).append(", API ").append(Build.VERSION.SDK_INT).append('\n');
        sb.append("cpu:      ").append(Arrays.toString(Build.SUPPORTED_ABIS)).append('\n');
        try {
            sb.append("app:      ").append(getPackageName()).append(' ')
              .append(getPackageManager().getPackageInfo(getPackageName(), 0).versionName).append('\n');
        } catch (Throwable t) {
            sb.append("app:      ").append(getPackageName()).append('\n');
        }
        sb.append("read-storage permission: ")
          .append(checkCallingOrSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == 0 ? "granted" : "not granted")
          .append('\n');
        sb.append("log file: /storage/emulated/0/Download/").append(logName).append('\n');
        if (!saveNote.isEmpty()) sb.append("save problems: ").append(saveNote).append('\n');
    }

    private void describeLibraries(StringBuilder sb) {
        String dir = getApplicationInfo().nativeLibraryDir;
        sb.append("\nnative libraries installed in ").append(dir).append(":\n");
        File[] files = new File(dir).listFiles();
        if (files == null || files.length == 0) {
            sb.append("  (none - the install carries no native code this device can use)\n");
        } else {
            for (File f : files) sb.append("  ").append(f.getName()).append("  ").append(f.length()).append(" bytes\n");
        }
        // The same two loads SDLActivity is about to do. If one fails, the message says why.
        String[] names = { "SDL3", "main" };
        for (String n : names) {
            try {
                System.loadLibrary(n);
                sb.append("load lib").append(n).append(".so: ok\n");
            } catch (Throwable t) {
                sb.append("load lib").append(n).append(".so: FAILED - ").append(t).append('\n');
            }
        }
    }

    private void describeInput(StringBuilder sb) {
        sb.append("\ninput devices Android reports:\n");
        try {
            for (int id : InputDevice.getDeviceIds()) {
                InputDevice d = InputDevice.getDevice(id);
                if (d == null) continue;
                int s = d.getSources();
                sb.append("  ").append(d.getName()).append("  [");
                if ((s & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE) sb.append(" mouse");
                if ((s & InputDevice.SOURCE_TOUCHSCREEN) == InputDevice.SOURCE_TOUCHSCREEN) sb.append(" touchscreen");
                if ((s & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD) sb.append(" gamepad");
                if ((s & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) sb.append(" joystick");
                if ((s & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD) sb.append(" dpad");
                if ((s & InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD) sb.append(" keyboard");
                sb.append(" ]  sources=0x").append(Integer.toHexString(s)).append('\n');
            }
        } catch (Throwable t) {
            sb.append("  could not list them: ").append(t).append('\n');
        }
    }

    private void appendLogcat(StringBuilder sb) {
        Process p = null;
        try {
            p = Runtime.getRuntime().exec(new String[] { "logcat", "-d", "-v", "threadtime", "-t", "3000" });
            BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()));
            String line;
            while ((line = r.readLine()) != null) sb.append(line).append('\n');
            r.close();
        } catch (Throwable t) {
            sb.append("(could not read the system log: ").append(t).append(")\n");
        } finally {
            if (p != null) p.destroy();
        }
    }

    // ------------------------------------------------------------------------------------ saving

    private void save(String text) throws Exception {
        byte[] data = text.getBytes("UTF-8");

        // A private copy, which needs nothing from anyone.
        OutputStream mine = null;
        try {
            mine = new FileOutputStream(new File(getFilesDir(), "apeiron-log.txt"));
            mine.write(data);
        } finally {
            if (mine != null) mine.close();
        }

        boolean done = false;
        String problems = "";

        // First choice: the Download folder by its plain path, /storage/emulated/0/Download on
        // nearly every device. That works wherever the app is allowed to write there.
        try {
            File dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
            dir.mkdirs();
            OutputStream o = new FileOutputStream(new File(dir, logName));
            try { o.write(data); } finally { o.close(); }
            done = true;
        } catch (Throwable t) {
            problems += "direct: " + t + "  ";
        }

        // Android 10 and later refuse that without a permission this app does not hold, but let
        // it add its own file to Download through MediaStore - same folder, same place on disk.
        if (!done && Build.VERSION.SDK_INT >= 29) {
            try {
                if (logUri == null) {
                    ContentValues v = new ContentValues();
                    v.put(MediaStore.MediaColumns.DISPLAY_NAME, logName);
                    v.put(MediaStore.MediaColumns.MIME_TYPE, "text/plain");
                    v.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS);
                    logUri = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
                }
                if (logUri != null) {
                    OutputStream o = getContentResolver().openOutputStream(logUri, "wt");
                    if (o != null) {
                        try { o.write(data); } finally { o.close(); }
                        done = true;
                    }
                }
                if (!done) problems += "MediaStore gave no file  ";
            } catch (Throwable t) {
                Log.e(TAG, "MediaStore write failed", t);
                problems += "MediaStore: " + t + "  ";
            }
        }
        if (!done) saveNote = problems;
        boolean inDownload = done;

        if (!done) {
            // Last resort: the app's own folder on shared storage, reachable with adb.
            File ext = getExternalFilesDir(null);
            if (ext != null) {
                OutputStream o = new FileOutputStream(new File(ext, logName));
                try { o.write(data); } finally { o.close(); }
                done = true;
            }
        }
        if (!saidWhere) {
            saidWhere = true;
            // On screen as well as in the log, because a person with a remote and a TV has no log
            // viewer: seeing this at all proves this code ran, and says where the file went.
            final String msg = inDownload
                ? "Apeiron log saved: /storage/emulated/0/Download/" + logName
                : "Apeiron log NOT in Download. " + saveNote + (done ? "(kept in the app's own folder)" : "(kept privately only)");
            Log.i(TAG, msg);
            try {
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        Toast.makeText(MainActivity.this, msg, Toast.LENGTH_LONG).show();
                    }
                });
            } catch (Throwable ignored) {
            }
        }
    }
}
