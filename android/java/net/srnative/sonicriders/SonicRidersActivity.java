// The app's activity: SDL's SDLActivity (SDL2's Java side, built from the pinned SDL2 source), plus
// launch arguments for test runs, as env.txt does on the Switch:
//   adb shell am start -n <package>/net.srnative.sonicriders.SonicRidersActivity --es args "SR_SCREENSHOT=600 SR_DETCLOCK=1"
// Each space-separated word reaches SDL_main as argv[1..]; the native side turns KEY=VALUE words into
// environment variables.
package net.srnative.sonicriders;

import android.content.Intent;
import android.os.Bundle;

import org.libsdl.app.SDLActivity;

public class SonicRidersActivity extends SDLActivity {
    private static boolean crashChecked;

    @Override
    protected void onCreate(Bundle state) {
        // before the game starts (it rotates sonicriders.log): a report of the last run, if it ended badly
        CrashReport report = null;
        if (!crashChecked) {
            crashChecked = true;
            CrashReport.installJavaHandler(this);
            report = CrashReport.collect(this);
        }
        super.onCreate(state);
        if (report != null) report.show(this);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"c++_shared", "SDL2", "main"};
    }

    @Override
    protected String[] getArguments() {
        Intent i = getIntent();
        String a = i != null ? i.getStringExtra("args") : null;
        if (a == null || a.trim().isEmpty()) return new String[0];
        return a.trim().split("\\s+");
    }
}
