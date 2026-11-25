package net.srnative.sonicriders.probe;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.os.StatFs;
import android.provider.Settings;
import android.system.Os;
import android.system.OsConstants;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;

public final class NativeProbeActivity extends Activity {
    private static native String runProbe(String directory);

    private String usbDebugging() {
        try {
            int value = Settings.Global.getInt(getContentResolver(), Settings.Global.ADB_ENABLED, -1);
            return value < 0 ? "unavailable" : value == 0 ? "off" : "on";
        } catch (SecurityException ignored) {
            return "unavailable";
        }
    }

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        ScrollView scroll = new ScrollView(this);
        setContentView(scroll);
        scroll.setOnApplyWindowInsetsListener((view, insets) -> {
            var bars = insets.getInsets(WindowInsets.Type.systemBars()
                | WindowInsets.Type.displayCutout());
            view.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            return insets;
        });

        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        int padding = Math.round(20 * getResources().getDisplayMetrics().density);
        panel.setPadding(padding, padding, padding, padding);
        scroll.addView(panel);
        TextView report = new TextView(this);
        report.setTextSize(16);
        report.setText("Running native module probe…");
        report.setTextIsSelectable(true);
        panel.addView(report);
        Button copy = new Button(this);
        copy.setText("Copy diagnostics");
        copy.setOnClickListener(view -> getSystemService(ClipboardManager.class)
            .setPrimaryClip(ClipData.newPlainText("Native probe", report.getText())));
        panel.addView(copy);
        Button close = new Button(this);
        close.setText("Close probe");
        close.setOnClickListener(view -> finishAndRemoveTask());
        panel.addView(close);

        new Thread(() -> {
            String result;
            try {
                System.loadLibrary("srnativeprobe");
                result = runProbe(getFilesDir().getAbsolutePath());
            } catch (RuntimeException | LinkageError error) {
                result = "FAIL — probe startup\n" + error + "\n";
            }
            File apk = new File(getApplicationInfo().sourceDir);
            String diagnostics = result
                + "\nDevice: " + Build.MODEL + " · Android " + Build.VERSION.RELEASE
                + " (API " + Build.VERSION.SDK_INT + ")\n"
                + "Target SDK: " + getApplicationInfo().targetSdkVersion
                + " · Page size: " + Os.sysconf(OsConstants._SC_PAGESIZE) + " bytes\n"
                + "PID: " + Process.myPid() + " · USB debugging: " + usbDebugging() + "\n"
                + "APK: " + apk.length() + " bytes (includes TinyCC source and license)\n"
                + "Available storage: " + new StatFs(getFilesDir().getPath()).getAvailableBytes() + " bytes";
            runOnUiThread(() -> report.setText(diagnostics));
        }, "native-probe").start();
    }
}
