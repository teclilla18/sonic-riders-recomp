// The app's activity: SDL's SDLActivity (SDL2's Java side, built from the pinned SDL2 source), plus
// launch arguments for test runs, as env.txt does on the Switch:
//   adb shell am start -n <package>/net.srnative.sonicriders.SonicRidersActivity --es args "SR_SCREENSHOT=600 SR_DETCLOCK=1"
// Each space-separated word reaches SDL_main as argv[1..]; the native side turns KEY=VALUE words into
// environment variables.
package net.srnative.sonicriders;

import android.content.Intent;

import org.libsdl.app.SDLActivity;

public class SonicRidersActivity extends SDLActivity {
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
