// Crash reports a player can send without a PC. The game's own folder (Android/data/<package>/files:
// sonicriders.log, crash.txt from the native crash handler in platform_host.c) is out of reach of
// phone file managers since Android 11, so on the next launch, before the game starts and rotates its
// log, a run that ended badly (Android's exit record: crash, native crash, ANR, error exit; or a
// crash.txt) becomes Download/SonicRiders/sonicriders-crash-<time>.txt, and a dialog offers to share it.
package net.srnative.sonicriders;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.app.ApplicationExitInfo;
import android.content.ClipData;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.io.RandomAccessFile;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;

final class CrashReport {
    private static final String TAG = "sonicriders";
    private static final int LOG_HEAD = 256 * 1024;  // a long log: its start (build, setup) and its end
    private static final int LOG_TAIL = 768 * 1024;

    private String summary;  // one line for the dialog
    private String name;     // file name in Download/SonicRiders
    private Uri uri;         // the saved copy (null: saving failed, the report is only in the app folder)

    static File dir(Context c) {
        File d = c.getExternalFilesDir(null);  // what SDL_AndroidGetExternalStoragePath returns
        return d != null ? d : c.getFilesDir();
    }

    // Java exceptions on any thread: into crash.txt, then the default handling (the "app stopped" exit)
    static void installJavaHandler(Context c) {
        final File crash = new File(dir(c), "crash.txt");
        final Thread.UncaughtExceptionHandler prev = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((t, e) -> {
            try (PrintWriter w = new PrintWriter(new FileOutputStream(crash))) {
                w.println("[crash] Java exception on thread " + t.getName() + ":");
                e.printStackTrace(w);
            } catch (Throwable ignored) {
            }
            if (prev != null) prev.uncaughtException(t, e);
        });
    }

    // Called before the game starts: null if the last run ended normally
    static CrashReport collect(Context c) {
        try {
            return collectUnchecked(c);
        } catch (Throwable e) {
            Log.w(TAG, "crash report failed", e);
            return null;
        }
    }

    private static CrashReport collectUnchecked(Context c) {
        File d = dir(c);
        File crash = new File(d, "crash.txt");
        File log = new File(d, "sonicriders.log");
        SharedPreferences prefs = c.getSharedPreferences("crashreport", Context.MODE_PRIVATE);
        long seen = prefs.getLong("exit_seen", 0);

        ApplicationExitInfo exit = null;
        ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
        List<ApplicationExitInfo> exits = am != null ? am.getHistoricalProcessExitReasons(null, 0, 1) : null;
        if (exits != null && !exits.isEmpty() && exits.get(0).getTimestamp() > seen) {
            exit = exits.get(0);
            prefs.edit().putLong("exit_seen", exit.getTimestamp()).apply();
        }
        boolean bad = crash.exists() || (exit != null && endedBadly(exit));
        if (!bad) return null;

        CrashReport r = new CrashReport();
        String crashText = crash.exists() ? readHead(crash, 64 * 1024) : null;
        String logText = log.exists() ? readLog(log) : null;
        StringBuilder sb = new StringBuilder();
        sb.append("Sonic Riders crash report\n");
        sb.append("App: ").append(appVersion(c)).append('\n');
        sb.append("Device: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
          .append(", Android ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT)
          .append("), ").append(Build.SUPPORTED_ABIS.length > 0 ? Build.SUPPORTED_ABIS[0] : "?").append('\n');
        if (exit != null) {
            sb.append("Last exit: ").append(reasonName(exit.getReason())).append(", status ").append(exit.getStatus())
              .append(", at ").append(stamp(exit.getTimestamp(), "yyyy-MM-dd HH:mm:ss"));
            if (exit.getDescription() != null) sb.append(", ").append(exit.getDescription());
            sb.append('\n');
        }
        sb.append('\n');
        if (crashText != null) sb.append("== crash.txt ==\n").append(crashText).append('\n');
        sb.append("== sonicriders.log ==\n").append(logText != null ? logText : "(no log: the game did not start)\n");

        r.summary = summarize(crashText, logText, exit);
        long when = exit != null ? exit.getTimestamp() : crash.lastModified();
        r.name = "sonicriders-crash-" + stamp(when, "yyyyMMdd-HHmmss") + ".txt";
        byte[] bytes = sb.toString().getBytes(StandardCharsets.UTF_8);
        writeFile(new File(d, "crash-report.txt"), bytes);  // adb pull reaches this one
        r.uri = saveToDownloads(c, r.name, bytes);
        crash.delete();
        Log.i(TAG, "crash report: " + r.summary + (r.uri != null ? " -> Download/SonicRiders/" + r.name : ""));
        return r;
    }

    void show(final Activity a) {
        String where = uri != null
            ? "A report was saved to Download/SonicRiders/" + name + "."
            : "A report was saved in the game's folder (crash-report.txt).";
        AlertDialog.Builder b = new AlertDialog.Builder(a, android.R.style.Theme_DeviceDefault_Dialog_Alert)
            .setTitle("Sonic Riders stopped last time")
            .setMessage(summary + "\n\n" + where + " It holds the game's log and your phone model, nothing "
                + "personal. Send it with your bug report to help fix the problem.")
            .setNegativeButton("Close", null);
        if (uri != null) b.setPositiveButton("Share report", (dlg, w) -> share(a));
        b.show();
    }

    private void share(Activity a) {
        Intent send = new Intent(Intent.ACTION_SEND);
        send.setType("text/plain");
        send.putExtra(Intent.EXTRA_STREAM, uri);
        send.putExtra(Intent.EXTRA_SUBJECT, "Sonic Riders crash report");
        send.setClipData(ClipData.newRawUri(name, uri));
        send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try {
            a.startActivity(Intent.createChooser(send, "Share crash report"));
        } catch (Throwable e) {
            Log.w(TAG, "share failed", e);
        }
    }

    private static boolean endedBadly(ApplicationExitInfo e) {
        switch (e.getReason()) {
            case ApplicationExitInfo.REASON_CRASH:
            case ApplicationExitInfo.REASON_CRASH_NATIVE:
            case ApplicationExitInfo.REASON_ANR:
            case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE:
                return true;
            case ApplicationExitInfo.REASON_EXIT_SELF:
                return e.getStatus() != 0;
            default:
                return false;  // closed by the player, low memory, app update, ...
        }
    }

    private static String reasonName(int r) {
        switch (r) {
            case ApplicationExitInfo.REASON_CRASH: return "Java crash";
            case ApplicationExitInfo.REASON_CRASH_NATIVE: return "native crash";
            case ApplicationExitInfo.REASON_ANR: return "not responding (ANR)";
            case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE: return "failed to start";
            case ApplicationExitInfo.REASON_EXIT_SELF: return "exited";
            case ApplicationExitInfo.REASON_SIGNALED: return "killed by a signal";
            case ApplicationExitInfo.REASON_LOW_MEMORY: return "low memory";
            default: return "reason " + r;
        }
    }

    // the most telling line: a FATAL error from the game, else the crash's first line, else Android's reason
    private static String summarize(String crashText, String logText, ApplicationExitInfo exit) {
        if (logText != null) {
            int i = logText.lastIndexOf("FATAL: ");
            if (i >= 0) {
                int e = logText.indexOf('\n', i);
                return logText.substring(i + 7, e < 0 ? logText.length() : e).trim();
            }
        }
        if (crashText != null) {
            String first = crashText.split("\n", 2)[0].replace("[crash] ", "").trim();
            if (!first.isEmpty()) return "The game crashed: " + first;
        }
        if (exit != null) return "The game closed: " + reasonName(exit.getReason()) + ".";
        return "The game closed unexpectedly.";
    }

    private static String appVersion(Context c) {
        try {
            PackageInfo p = c.getPackageManager().getPackageInfo(c.getPackageName(), 0);
            return p.versionName + " (" + p.getLongVersionCode() + ")";
        } catch (Throwable e) {
            return "?";
        }
    }

    private static String stamp(long ms, String fmt) {
        return new SimpleDateFormat(fmt, Locale.US).format(new Date(ms));
    }

    private static String readHead(File f, int max) {
        try (FileInputStream in = new FileInputStream(f)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            int n;
            while (out.size() < max && (n = in.read(buf, 0, Math.min(buf.length, max - out.size()))) > 0)
                out.write(buf, 0, n);
            return new String(out.toByteArray(), StandardCharsets.UTF_8);
        } catch (Throwable e) {
            return null;
        }
    }

    private static String readLog(File f) {
        long len = f.length();
        if (len <= LOG_HEAD + LOG_TAIL) return readHead(f, LOG_HEAD + LOG_TAIL);
        try (RandomAccessFile raf = new RandomAccessFile(f, "r")) {
            byte[] head = new byte[LOG_HEAD], tail = new byte[LOG_TAIL];
            raf.readFully(head);
            raf.seek(len - LOG_TAIL);
            raf.readFully(tail);
            return new String(head, StandardCharsets.UTF_8) + "\n[... " + (len - LOG_HEAD - LOG_TAIL)
                + " bytes left out ...]\n" + new String(tail, StandardCharsets.UTF_8);
        } catch (Throwable e) {
            return null;
        }
    }

    private static void writeFile(File f, byte[] b) {
        try (FileOutputStream o = new FileOutputStream(f)) {
            o.write(b);
        } catch (Throwable e) {
            Log.w(TAG, "cannot write " + f, e);
        }
    }

    // Download/SonicRiders/<name> through MediaStore: no storage permission needed (Android 10+), and the
    // file stays after the app is uninstalled
    private static Uri saveToDownloads(Context c, String name, byte[] bytes) {
        ContentValues v = new ContentValues();
        v.put(MediaStore.MediaColumns.DISPLAY_NAME, name);
        v.put(MediaStore.MediaColumns.MIME_TYPE, "text/plain");
        v.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/SonicRiders");
        v.put(MediaStore.MediaColumns.IS_PENDING, 1);
        Uri u = null;
        try {
            u = c.getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
            if (u == null) return null;
            try (OutputStream o = c.getContentResolver().openOutputStream(u)) {
                o.write(bytes);
            }
            v.clear();
            v.put(MediaStore.MediaColumns.IS_PENDING, 0);
            c.getContentResolver().update(u, v, null, null);
            return u;
        } catch (Throwable e) {
            Log.w(TAG, "cannot save the report to Download", e);
            if (u != null) {
                try {
                    c.getContentResolver().delete(u, null, null);
                } catch (Throwable ignored) {
                }
            }
            return null;
        }
    }
}
