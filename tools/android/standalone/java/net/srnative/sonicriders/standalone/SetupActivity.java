package net.srnative.sonicriders.standalone;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public final class SetupActivity extends Activity {
    private static final long ISO_SIZE = 1459978240L;
    private static final String ISO_SHA1 = "ce0cecf57034bef8bab7d6f588ce92c85a849613";
    private static final int PICK_ISO = 1;
    private TextView status;
    private Button setup;
    private Button play;

    private static File work(Context context) {
        return new File(context.getFilesDir(), "standalone-v1");
    }

    static File iso(Context context) {
        File storage = context.getExternalFilesDir(null);
        return new File(storage == null ? context.getFilesDir() : storage, "baserom/SonicRiders.iso");
    }

    static File module(Context context) {
        return new File(work(context), "libmain.so");
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        ScrollView scroll = new ScrollView(this);
        setContentView(scroll);
        scroll.setOnApplyWindowInsetsListener((view, insets) -> {
            var bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            view.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            return insets;
        });
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        int pad = Math.round(20 * getResources().getDisplayMetrics().density);
        panel.setPadding(pad, pad, pad, pad);
        scroll.addView(panel);
        status = new TextView(this);
        status.setTextSize(16);
        status.setTextIsSelectable(true);
        panel.addView(status);
        setup = new Button(this);
        setup.setText(iso(this).isFile() ? "Build using imported ISO" : "Select USA revision 0 ISO");
        setup.setOnClickListener(view -> {
            if (iso(this).isFile()) {
                startBuild(null);
            } else {
                Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                picker.addCategory(Intent.CATEGORY_OPENABLE);
                picker.setType("*/*");
                startActivityForResult(picker, PICK_ISO);
            }
        });
        panel.addView(setup);
        play = new Button(this);
        play.setText("Play");
        play.setOnClickListener(view -> startActivity(new Intent(this, GameActivity.class)));
        panel.addView(play);
        updateButtons();
        status.setText(ready() ? "Reused local module.\n" + previousReport() : "Choose your clean Sonic Riders USA revision 0 ISO.");
    }

    private boolean ready() {
        return module(this).isFile() && new File(work(this), "report.txt").isFile()
            && iso(this).isFile() && iso(this).length() == ISO_SIZE;
    }

    private void updateButtons() {
        setup.setEnabled(!ready());
        play.setEnabled(ready());
        if (iso(this).isFile()) setup.setText("Build using imported ISO");
    }

    private String previousReport() {
        try {
            return Files.readString(new File(work(this), "report.txt").toPath());
        } catch (IOException ignored) {
            return "";
        }
    }

    @Override
    protected void onActivityResult(int code, int result, Intent data) {
        super.onActivityResult(code, result, data);
        if (code == PICK_ISO && result == RESULT_OK && data != null && data.getData() != null)
            startBuild(data.getData());
    }

    private void progress(String message) {
        runOnUiThread(() -> status.setText(message));
    }

    private void startBuild(Uri source) {
        setup.setEnabled(false);
        play.setEnabled(false);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        new Thread(() -> {
            try {
                if (source != null) importDisc(source);
                prepareBuildFiles();
                buildGame();
                progress(previousReport());
            } catch (Exception error) {
                progress("Setup failed\n" + error.getMessage());
            } finally {
                runOnUiThread(() -> {
                    getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                    updateButtons();
                });
            }
        }, "game-build").start();
    }

    private void importDisc(Uri source) throws Exception {
        File disc = iso(this);
        Files.createDirectories(disc.getParentFile().toPath());
        if (disc.getParentFile().getUsableSpace() < ISO_SIZE + 64 * 1024 * 1024)
            throw new IOException("At least 1.5 GB free is required to import the ISO.");
        File pending = new File(disc.getParentFile(), "import.pending");
        MessageDigest hash = MessageDigest.getInstance("SHA-1");
        try {
            long total = 0;
            int percent = -1;
            try (var input = getContentResolver().openInputStream(source);
                 var output = Files.newOutputStream(pending.toPath())) {
                if (input == null) throw new IOException("The selected file could not be opened.");
                byte[] buffer = new byte[1024 * 1024];
                int length;
                while ((length = input.read(buffer)) != -1) {
                    total += length;
                    if (total > ISO_SIZE) throw new IOException("Expected a complete 1,459,978,240-byte ISO.");
                    hash.update(buffer, 0, length);
                    output.write(buffer, 0, length);
                    int next = (int)(total * 100 / ISO_SIZE);
                    if (next != percent) {
                        percent = next;
                        progress("Validating and copying ISO… " + percent + "%");
                    }
                }
            }
            StringBuilder digest = new StringBuilder();
            for (byte part : hash.digest()) digest.append(String.format("%02x", part & 0xff));
            if (total != ISO_SIZE || !ISO_SHA1.contentEquals(digest))
                throw new IOException("The selected ISO is not the clean USA GXEE8P revision 0 dump.");
            Files.move(pending.toPath(), disc.toPath(), StandardCopyOption.ATOMIC_MOVE);
        } finally {
            Files.deleteIfExists(pending.toPath());
        }
    }

    private void prepareBuildFiles() throws IOException {
        File root = work(this);
        File marker = new File(root, "tools-ready");
        if (marker.isFile()) return;
        if (getFilesDir().getUsableSpace() < 512L * 1024 * 1024)
            throw new IOException("At least 512 MiB free is required for the local build workspace.");
        progress("Preparing bundled build tools…");
        Files.createDirectories(root.toPath());
        try (ZipInputStream archive = new ZipInputStream(getAssets().open("standalone-tools.zip"))) {
            ZipEntry entry;
            while ((entry = archive.getNextEntry()) != null) {
                var destination = root.toPath().resolve(entry.getName()).normalize();
                if (!destination.startsWith(root.toPath())) throw new IOException("Invalid tool archive path.");
                Files.createDirectories(destination.getParent());
                Files.copy(archive, destination, StandardCopyOption.REPLACE_EXISTING);
                if ((entry.getName().startsWith("tool_libs/") || entry.getName().startsWith("python/dynload/"))
                    && !destination.toFile().setReadOnly()) throw new IOException("Could not finalize a tool library.");
            }
        }
        Files.createFile(marker.toPath());
    }

    private void buildGame() throws IOException, InterruptedException {
        File root = work(this);
        String libs = getApplicationInfo().nativeLibraryDir;
        ProcessBuilder builder = new ProcessBuilder(libs + "/libsr_python.so",
            new File(root, "build_on_phone.py").getPath(), root.getPath(), libs, iso(this).getPath());
        builder.redirectErrorStream(true);
        var env = builder.environment();
        env.put("LD_LIBRARY_PATH", new File(root, "tool_libs").getPath() + ":" + libs);
        env.put("PYTHONHOME", new File(root, "python").getPath());
        env.put("PYTHONPATH", new File(root, "python/stdlib.zip") + ":" + new File(root, "python/dynload"));
        env.put("PYTHONDONTWRITEBYTECODE", "1");
        env.put("PYTHONNOUSERSITE", "1");
        env.put("TMPDIR", root.getPath());
        Process process = builder.start();
        String last = "Build worker did not emit a diagnostic";
        try (var output = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
            String line;
            while ((line = output.readLine()) != null) {
                last = line;
                progress(line);
            }
        }
        if (process.waitFor() != 0) throw new IOException(last);
    }
}
