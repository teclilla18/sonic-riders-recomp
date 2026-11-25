package net.srnative.sonicriders.standalone;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.util.Locale;

public final class SetupActivity extends Activity {
    private static final int PICK_ISO = 1;
    private static final String RELEASES = "https://github.com/teclilla18/sonic-riders-recomp/releases";
    private static final int BACKGROUND = 0xff040a17;
    private static final int PANEL = 0xff101e31;
    private static final int BORDER = 0xff25405a;
    private static final int INK = 0xffe9f4ff;
    private static final int MUTED = 0xff9fb5cf;
    private static final int CYAN = 0xff3fe3ff;
    private static final int GOLD = 0xffffd23f;

    private final Handler timer = new Handler(Looper.getMainLooper());
    private final Runnable refresh = new Runnable() {
        @Override
        public void run() {
            boolean wasInstalling = installer.progress() != null;
            render();
            if (wasInstalling) timer.postDelayed(this, 300);
        }
    };
    private Installer installer;
    private TextView state, title, description, elapsed;
    private ProgressBar bar;
    private Button primary;

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        try {
            installer = Installer.get(this);
        } catch (IllegalStateException error) {
            new AlertDialog.Builder(this).setTitle("Could not open Riders Plus")
                .setMessage(error.getMessage()).setPositiveButton("Close", (d, w) -> finish()).show();
            return;
        }
        buildLayout();
    }

    @Override
    protected void onStart() {
        super.onStart();
        if (installer != null) timer.post(refresh);
    }

    @Override
    protected void onStop() {
        timer.removeCallbacks(refresh);
        super.onStop();
    }

    private void buildLayout() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackground(new GradientDrawable(GradientDrawable.Orientation.TL_BR,
            new int[] {BACKGROUND, 0xff0b2035, BACKGROUND}));
        setContentView(scroll);
        scroll.setOnApplyWindowInsetsListener((view, insets) -> {
            var bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            view.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            return insets;
        });
        LinearLayout page = column();
        page.setPadding(dp(22), dp(36), dp(22), dp(26));
        scroll.addView(page);

        LinearLayout brand = new LinearLayout(this);
        brand.setGravity(Gravity.CENTER_VERTICAL);
        ImageView icon = new ImageView(this);
        icon.setImageDrawable(getApplicationInfo().loadIcon(getPackageManager()));
        brand.addView(icon, new LinearLayout.LayoutParams(dp(62), dp(62)));
        LinearLayout identity = column();
        identity.setPadding(dp(12), 0, 0, 0);
        TextView name = text(32, INK, true);
        name.setText("Riders Plus");
        identity.addView(name);
        TextView platform = text(14, MUTED, false);
        platform.setText("Standalone Android installer");
        identity.addView(platform);
        brand.addView(identity);
        page.addView(brand);

        LinearLayout card = column();
        card.setBackground(shape(PANEL, BORDER, 22));
        card.setPadding(dp(20), dp(21), dp(20), dp(20));
        add(page, card, 28);
        state = text(13, CYAN, true);
        card.addView(state);
        title = text(25, INK, true);
        title.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        add(card, title, 8);
        description = text(16, MUTED, false);
        description.setLineSpacing(dp(2), 1);
        add(card, description, 8);
        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setProgressTintList(ColorStateList.valueOf(CYAN));
        bar.setProgressBackgroundTintList(ColorStateList.valueOf(BORDER));
        bar.setMax(100);
        bar.setContentDescription("Installation progress");
        add(card, bar, 14);
        elapsed = text(14, MUTED, false);
        add(card, elapsed, 4);
        primary = button("", true);
        primary.setOnClickListener(view -> primaryAction());
        add(card, primary, 20);

        LinearLayout actions = new LinearLayout(this);
        Button diagnostics = button("Diagnostics", false);
        diagnostics.setOnClickListener(view -> showDiagnostics());
        Button updates = button("Updates", false);
        updates.setOnClickListener(view -> showUpdates());
        actions.addView(diagnostics, new LinearLayout.LayoutParams(0, dp(52), 1));
        var second = new LinearLayout.LayoutParams(0, dp(52), 1);
        second.leftMargin = dp(10);
        actions.addView(updates, second);
        add(page, actions, 14);
        render();
    }

    private void render() {
        Installer.Progress progress = installer.progress();
        boolean active = progress != null;
        if (active) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        bar.setVisibility(active ? View.VISIBLE : View.GONE);
        elapsed.setVisibility(active ? View.VISIBLE : View.GONE);
        primary.setVisibility(active ? View.GONE : View.VISIBLE);
        if (active) {
            display("INSTALLING", progress.title, "Keep Riders Plus open while your phone builds the game.", "");
            bar.setProgress(progress.percent);
            long seconds = (SystemClock.elapsedRealtime() - progress.start) / 1000;
            setText(elapsed, String.format(Locale.ROOT, "%d%%   ·   %d:%02d elapsed",
                progress.percent, seconds / 60, seconds % 60));
            return;
        }
        String compatibility = installer.compatibility();
        if (!compatibility.isEmpty()) {
            display("DEVICE CHECK", "This device is unsupported", compatibility, "");
            primary.setVisibility(View.GONE);
        } else if (installer.prepared() != null) {
            display("INSTALLED", "Ready to play", "Your game is ready. Your disc, saves, settings and mods stay on this phone.", "PLAY");
        } else if (!installer.error().isEmpty()) {
            display("SETUP STOPPED", "Installation needs attention", installer.error(),
                installer.hasIso() ? "Try again" : "Choose an ISO");
        } else if (installer.interrupted()) {
            display("SETUP INTERRUPTED", "Continue installation",
                installer.hasIso() ? "Your verified disc is safe. Continue to rebuild the game."
                    : "Choose your disc image again to restart the interrupted copy.",
                installer.hasIso() ? "Continue" : "Choose an ISO");
        } else if (installer.needsUpdateBuild()) {
            display("UPDATE READY", "Prepare this version", "This update needs a new game build. Your disc, saves, settings and mods will stay in place.", "Build update");
        } else if (installer.hasIso()) {
            display("DISC READY", "Build Riders Plus", "Your verified disc is on this phone. The build takes a few minutes.", "Build game");
        } else {
            display("FIRST-TIME SETUP", "Choose your disc image",
                "Select your own Sonic Riders ISO for GameCube (USA, revision 0). Keep about 2 GB free for setup. The build runs offline on this phone.",
                "Select ISO");
        }
    }

    private void display(String label, String heading, String body, String action) {
        setText(state, label);
        state.setTextColor(label.contains("STOPPED") || label.contains("INTERRUPTED") ? GOLD : CYAN);
        setText(title, heading);
        setText(description, body);
        setText(primary, action);
    }

    private static void setText(TextView view, String text) {
        if (!text.contentEquals(view.getText())) view.setText(text);
    }

    private void primaryAction() {
        if (installer.prepared() != null) {
            startActivity(new Intent(this, GameActivity.class));
            return;
        }
        String space = installer.spaceCheck(!installer.hasIso());
        if (!space.isEmpty()) {
            new AlertDialog.Builder(this).setTitle("More storage needed").setMessage(space)
                .setPositiveButton("OK", null).show();
        } else if (installer.hasIso()) {
            startInstall(null);
        } else {
            Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT)
                .addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
            try {
                startActivityForResult(picker, PICK_ISO);
            } catch (ActivityNotFoundException exception) {
                new AlertDialog.Builder(this).setTitle("File picker is unavailable")
                    .setMessage("Use a device with the Android system Files picker.").setPositiveButton("OK", null).show();
            }
        }
    }

    @Override
    protected void onActivityResult(int code, int result, Intent data) {
        super.onActivityResult(code, result, data);
        if (code == PICK_ISO && result == RESULT_OK && data != null && data.getData() != null)
            startInstall(data.getData());
    }

    private void startInstall(Uri source) {
        installer.start(source);
        timer.removeCallbacks(refresh);
        timer.post(refresh);
    }

    private void showDiagnostics() {
        String report = installer.diagnostics();
        AlertDialog.Builder dialog = new AlertDialog.Builder(this).setTitle("Build diagnostics")
            .setMessage(report).setNeutralButton("Close", null)
            .setPositiveButton("Copy report", (d, w) -> {
                getSystemService(ClipboardManager.class).setPrimaryClip(ClipData.newPlainText("Riders Plus", report));
                Toast.makeText(this, "Report copied", Toast.LENGTH_SHORT).show();
            });
        if (installer.prepared() != null && installer.progress() == null) {
            dialog.setNegativeButton("Repair build", (d, w) -> new AlertDialog.Builder(this)
                .setTitle("Rebuild the game?")
                .setMessage("This prepares the game again from your verified disc. Your saves, settings and mods will stay in place.")
                .setNegativeButton("Cancel", null).setPositiveButton("Rebuild", (confirm, which) -> {
                    installer.repair();
                    primaryAction();
                }).show());
        }
        dialog.show();
    }

    private void showUpdates() {
        new AlertDialog.Builder(this).setTitle("Riders Plus updates")
            .setMessage("Download the standalone Android APK from GitHub Releases and open it over this installation. Android verifies the signature. Keep this app installed to retain your game data.")
            .setNegativeButton("Close", null).setPositiveButton("Open releases", (dialog, which) -> {
                try {
                    startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(RELEASES)));
                } catch (ActivityNotFoundException exception) {
                    Toast.makeText(this, "Open the Riders Plus GitHub Releases page in a browser.", Toast.LENGTH_LONG).show();
                }
            }).show();
    }

    private LinearLayout column() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        return layout;
    }

    private TextView text(int size, int color, boolean bold) {
        TextView text = new TextView(this);
        text.setTextSize(size);
        text.setTextColor(color);
        if (bold) text.setTypeface(Typeface.create("sans-serif", Typeface.BOLD));
        return text;
    }

    private Button button(String value, boolean main) {
        Button button = new Button(this);
        button.setText(value);
        button.setTextColor(main ? BACKGROUND : INK);
        button.setTextSize(main ? 18 : 15);
        button.setAllCaps(false);
        button.setTypeface(Typeface.DEFAULT_BOLD);
        button.setMinHeight(dp(main ? 56 : 48));
        button.setBackgroundTintList(null);
        button.setBackground(new RippleDrawable(ColorStateList.valueOf(0x484b7090),
            shape(main ? CYAN : PANEL, main ? CYAN : BORDER, 14), null));
        return button;
    }

    private GradientDrawable shape(int fill, int stroke, int corner) {
        GradientDrawable shape = new GradientDrawable();
        shape.setCornerRadius(dp(corner));
        shape.setColor(fill);
        shape.setStroke(dp(1), stroke);
        return shape;
    }

    private void add(LinearLayout parent, View child, int gap) {
        var params = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(gap);
        parent.addView(child, params);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
