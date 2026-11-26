package net.srnative.sonicriders.standalone;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.SystemClock;
import android.system.Os;
import android.system.OsConstants;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.FileInputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.BasicFileAttributes;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.Locale;
import java.util.Properties;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

final class Installer {
    static final long ISO_SIZE = 1459978240L;
    private static final String ISO_SHA1 = "ce0cecf57034bef8bab7d6f588ce92c85a849613";
    private static final long WORK_SPACE = 512L * 1024 * 1024;
    private static final long IMPORT_SPACE = ISO_SIZE + 64L * 1024 * 1024;
    private static Installer instance;

    final Context context;
    final String moduleKey, legacyKey, bundleKey, version;
    final File root, output, tools;
    final SharedPreferences preferences;
    private volatile Progress progress;
    private volatile String qaStorageLimit = "";

    static synchronized Installer get(Context context) {
        if (instance == null) instance = new Installer(context.getApplicationContext());
        return instance;
    }

    static final class Progress {
        final int percent;
        final String title;
        final long start;

        Progress(int percent, String title, long start) {
            this.percent = percent;
            this.title = title;
            this.start = start;
        }
    }

    private Installer(Context context) {
        this.context = context;
        Properties manifest = new Properties();
        try (var input = context.getAssets().open("installer.properties")) {
            manifest.load(input);
        } catch (IOException e) {
            throw new IllegalStateException("The installer metadata could not be read.", e);
        }
        moduleKey = key(manifest, "module");
        legacyKey = key(manifest, "legacy");
        bundleKey = key(manifest, "bundle");
        version = manifest.getProperty("version", "");
        root = new File(context.getFilesDir(), "installer");
        output = new File(root, "prepared/" + moduleKey);
        tools = new File(root, "toolset");
        preferences = context.getSharedPreferences("installer", Context.MODE_PRIVATE);
    }

    private static String key(Properties manifest, String name) {
        String value = manifest.getProperty(name, "");
        if (!value.matches("[a-f0-9]{64}")) throw new IllegalStateException("Incomplete installer metadata.");
        return value;
    }

    File gameData() {
        File storage = context.getExternalFilesDir(null);
        return storage == null ? context.getFilesDir() : storage;
    }

    File iso() {
        return new File(gameData(), "baserom/SonicRiders.iso");
    }

    boolean hasIso() {
        return iso().isFile() && iso().length() == ISO_SIZE;
    }

    private File legacy() {
        return new File(context.getFilesDir(), "standalone-v1");
    }

    File prepared() {
        if (!hasIso() || preferences.getBoolean("repair", false)) return null;
        if (complete(output)) return output;
        if (moduleKey.equals(legacyKey) && complete(legacy())) return legacy();
        return null;
    }

    boolean needsUpdateBuild() {
        if (!hasIso()) return false;
        if (!moduleKey.equals(legacyKey) && complete(legacy())) return true;
        File[] other = new File(root, "prepared").listFiles(
            file -> !file.equals(output) && complete(file));
        return other != null && other.length > 0;
    }

    private static boolean complete(File directory) {
        File image = new File(directory, "libmain.so");
        if (!new File(directory, "report.txt").isFile() || image.canWrite()) return false;
        try (var input = new FileInputStream(image)) {
            byte[] elf = new byte[20];
            return input.read(elf) == elf.length && elf[0] == 0x7f && elf[1] == 'E'
                && elf[2] == 'L' && elf[3] == 'F' && elf[4] == 2 && elf[5] == 1
                && elf[16] == 3 && elf[17] == 0 && (elf[18] & 0xff) == 183 && elf[19] == 0;
        } catch (IOException ignored) {
            return false;
        }
    }

    String compatibility() {
        if (!Arrays.asList(Build.SUPPORTED_64_BIT_ABIS).contains("arm64-v8a"))
            return "Riders Crosswake requires a 64-bit ARM device.";
        if (Os.sysconf(OsConstants._SC_PAGESIZE) != 4096)
            return "This build requires 4 KB memory pages. A 16 KB build is not available yet.";
        if (!context.getPackageManager().hasSystemFeature(PackageManager.FEATURE_VULKAN_HARDWARE_VERSION, 0x401000))
            return "Riders Crosswake requires a device with Vulkan 1.1 support.";
        return "";
    }

    String spaceCheck(boolean importing) {
        long discSpace = qaStorageLimit.equals("disc") ? 0 : gameData().getUsableSpace() + pendingIso().length();
        if (importing && discSpace < IMPORT_SPACE)
            return "Free at least 1.5 GB for the imported disc image and try again.";
        long reclaimable = importing ? pendingIso().length() : 0;
        long workSpace = qaStorageLimit.equals("build") ? 0 : context.getFilesDir().getUsableSpace() + reclaimable;
        if (workSpace < WORK_SPACE)
            return "Free at least 512 MiB for the build workspace and try again.";
        return "";
    }

    void setQaStorageLimit(String limit) {
        if (context.getPackageName().equals("net.srnative.sonicriders.standalone.qa"))
            qaStorageLimit = limit == null ? "" : limit;
    }

    Progress progress() { return progress; }

    private void update(int percent, String title) {
        Progress previous = progress;
        progress = new Progress(percent, title, previous.start);
    }

    synchronized void start(Uri uri) {
        if (progress != null) return;
        preferences.edit().putBoolean("interrupted", true).remove("error").commit();
        progress = new Progress(0, "Getting ready", SystemClock.elapsedRealtime());
        new Thread(() -> prepare(uri), "riders-install").start();
    }

    void repair() {
        preferences.edit().putBoolean("repair", true).remove("error").apply();
    }

    void loadFailed() {
        preferences.edit().putBoolean("repair", true)
            .putString("error", "The prepared game could not start. Rebuild using the disc already on this phone.")
            .apply();
    }

    String error() { return preferences.getString("error", ""); }
    boolean interrupted() { return preferences.getBoolean("interrupted", false); }

    private void prepare(Uri source) {
        String failure = "";
        try {
            require(compatibility());
            require(spaceCheck(source != null));
            if (source != null) importDisc(source);
            if (!hasIso()) throw new IOException("Choose a clean Sonic Riders USA revision 0 ISO to continue.");
            require(spaceCheck(false));
            prepareTools();
            buildGame();
            if (!complete(output)) throw new IOException("The build ended without a complete game. Try again.");
            preferences.edit().putBoolean("repair", false).apply();
        } catch (Exception e) {
            String message = e.getMessage();
            failure = message == null ? "Installation stopped. Open Diagnostics and try again." : message;
        } finally {
            long seconds = (SystemClock.elapsedRealtime() - progress.start) / 1000;
            preferences.edit().putBoolean("interrupted", false).putString("error", failure)
                .putLong("elapsed", seconds).commit();
            progress = null;
        }
    }

    private static void require(String error) throws IOException {
        if (!error.isEmpty()) throw new IOException(error);
    }

    private void importDisc(Uri source) throws Exception {
        File disc = iso();
        Files.createDirectories(disc.getParentFile().toPath());
        File pending = pendingIso();
        MessageDigest hash = MessageDigest.getInstance("SHA-1");
        update(1, "Checking your disc image");
        try {
            long total = 0;
            int displayed = 0;
            try (var input = context.getContentResolver().openInputStream(source);
                 var contents = Files.newOutputStream(pending.toPath())) {
                if (input == null) throw new IOException("The selected file could not be opened.");
                byte[] buffer = new byte[1024 * 1024];
                int length;
                while ((length = input.read(buffer)) != -1) {
                    total += length;
                    if (total > ISO_SIZE) throw new IOException("Choose a complete Sonic Riders ISO (1.46 GB).");
                    hash.update(buffer, 0, length);
                    contents.write(buffer, 0, length);
                    int percent = 1 + (int)(total * 11 / ISO_SIZE);
                    if (percent != displayed) {
                        displayed = percent;
                        update(percent, "Checking and copying your disc");
                    }
                }
            }
            StringBuilder digest = new StringBuilder();
            for (byte part : hash.digest()) digest.append(String.format(Locale.ROOT, "%02x", part & 0xff));
            if (total != ISO_SIZE || !ISO_SHA1.contentEquals(digest))
                throw new IOException("This file is not the clean USA revision 0 ISO. Choose a different disc image.");
            Files.move(pending.toPath(), disc.toPath(), StandardCopyOption.ATOMIC_MOVE,
                StandardCopyOption.REPLACE_EXISTING);
        } finally {
            Files.deleteIfExists(pending.toPath());
        }
    }

    private File pendingIso() {
        return new File(iso().getParentFile(), "import.pending");
    }

    private void prepareTools() throws IOException {
        File marker = new File(tools, "tools-ready");
        if (marker.isFile() && new String(Files.readAllBytes(marker.toPath()), StandardCharsets.US_ASCII)
            .equals(bundleKey)) return;
        update(14, "Getting the build tools ready");
        if (tools.exists()) {
            Files.walkFileTree(tools.toPath(), new SimpleFileVisitor<Path>() {
                @Override
                public FileVisitResult visitFile(Path file, BasicFileAttributes attrs) throws IOException {
                    Files.delete(file);
                    return FileVisitResult.CONTINUE;
                }

                @Override
                public FileVisitResult postVisitDirectory(Path directory, IOException error) throws IOException {
                    if (error != null) throw error;
                    Files.delete(directory);
                    return FileVisitResult.CONTINUE;
                }
            });
        }
        Files.createDirectories(tools.toPath());
        try (ZipInputStream archive = new ZipInputStream(context.getAssets().open("standalone-tools.zip"))) {
            ZipEntry entry;
            while ((entry = archive.getNextEntry()) != null) {
                var destination = tools.toPath().resolve(entry.getName()).normalize();
                if (!destination.startsWith(tools.toPath())) throw new IOException("Invalid build tool archive.");
                Files.createDirectories(destination.getParent());
                Files.copy(archive, destination, StandardCopyOption.REPLACE_EXISTING);
                if ((entry.getName().startsWith("tool_libs/") || entry.getName().startsWith("python/dynload/"))
                    && !destination.toFile().setReadOnly()) throw new IOException("Could not prepare a build library.");
            }
        }
        var pending = new File(tools, "tools-ready.pending").toPath();
        Files.write(pending, bundleKey.getBytes(StandardCharsets.US_ASCII));
        Files.move(pending, marker.toPath(), StandardCopyOption.ATOMIC_MOVE);
    }

    private void buildGame() throws IOException, InterruptedException {
        String libs = context.getApplicationInfo().nativeLibraryDir;
        ProcessBuilder command = new ProcessBuilder(libs + "/libsr_python.so",
            new File(tools, "build_on_phone.py").getPath(), tools.getPath(), libs, iso().getPath(), output.getPath());
        command.redirectErrorStream(true);
        var env = command.environment();
        env.put("LD_LIBRARY_PATH", new File(tools, "tool_libs").getPath() + ":" + libs);
        env.put("PYTHONHOME", new File(tools, "python").getPath());
        env.put("PYTHONPATH", new File(tools, "python/stdlib.zip") + ":" + new File(tools, "python/dynload"));
        env.put("PYTHONDONTWRITEBYTECODE", "1");
        env.put("PYTHONNOUSERSITE", "1");
        env.put("TMPDIR", tools.getPath());
        update(17, "Starting the game build");
        Process process = command.start();
        String failure = "";
        try (var stream = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
            String line;
            while ((line = stream.readLine()) != null) {
                String[] event = line.split("\\|", 2);
                if (event.length != 2) continue;
                if (event[0].equals("FAIL")) {
                    failure = event[1];
                } else if (event[0].matches("[0-9]{1,3}")) {
                    update(Integer.parseInt(event[0]), event[1]);
                }
            }
        }
        int exit = process.waitFor();
        if (exit != 0) {
            // Compiler stderr can contain snippets of generated code. The shareable report stays stage-only.
            if (!failure.matches("[\\p{L}\\p{N} .:_()-]{1,140}")) failure = progress.title + " (status " + exit + ")";
            throw new IOException("The game build stopped. Try again.\nDetails: " + failure);
        }
    }

    String diagnostics() {
        File prepared = prepared();
        String report = "";
        if (prepared != null) {
            try {
                report = "\n\n" + new String(Files.readAllBytes(new File(prepared, "report.txt").toPath()),
                    StandardCharsets.UTF_8);
            } catch (IOException ignored) {
                report = "\n\nBuild metrics unavailable.";
            }
        }
        long elapsed = preferences.getLong("elapsed", -1);
        return "Riders Crosswake · " + version
            + "\n" + Build.MODEL + " · Android " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")"
            + "\n" + Os.sysconf(OsConstants._SC_PAGESIZE) + "-byte pages · "
            + (context.getFilesDir().getUsableSpace() / (1024 * 1024)) + " MiB free"
            + "\nPrepared data: " + (prepared == null ? "needs setup" : "compatible; reuse")
            + (elapsed < 0 ? "" : "\nLast installation: " + elapsed + " s including import and tool setup")
            + (error().isEmpty() ? "" : "\n\n" + error()) + report;
    }
}
