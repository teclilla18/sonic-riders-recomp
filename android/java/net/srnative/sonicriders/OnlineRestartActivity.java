package net.srnative.sonicriders;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;

/** Foreground helper in a separate process: restart native SDL state at frame 1 without
 * alarms, background activity launches, or additional Android permissions. */
public final class OnlineRestartActivity extends Activity {
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        int oldPid = getIntent().getIntExtra("oldPid", -1);
        Handler handler = new Handler(Looper.getMainLooper());
        handler.postDelayed(() -> {
            if (oldPid > 0 && oldPid != Process.myPid()) Process.killProcess(oldPid);
            handler.postDelayed(() -> {
                Intent game = new Intent(this, SonicRidersActivity.class);
                game.putExtra("args", "SR_ONLINE=1");
                game.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
                startActivity(game);
                finish();
            }, 300);
        }, 300);
    }
}
