package net.srnative.sonicriders.standalone;

import java.io.File;

import net.srnative.sonicriders.SonicRidersActivity;

public final class GameActivity extends SonicRidersActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"c++_shared", "SDL2"};
    }

    @Override
    public void loadLibraries() {
        super.loadLibraries();
        try {
            System.load(getMainSharedObject());
        } catch (UnsatisfiedLinkError error) {
            Installer.get(this).loadFailed();
            throw error;
        }
    }

    @Override
    protected String getMainSharedObject() {
        Installer installer = Installer.get(this);
        File prepared = installer.prepared();
        if (prepared == null) throw new UnsatisfiedLinkError("Return to Riders Rebuilt setup to repair the installation.");
        return new File(prepared, "libmain.so").getAbsolutePath();
    }

    @Override
    protected String[] getArguments() {
        // the disc, then any launch arguments (Open Online restarts with "SR_ONLINE=1")
        String[] extra = super.getArguments();
        String[] all = new String[extra.length + 1];
        all[0] = Installer.get(this).iso().getAbsolutePath();
        System.arraycopy(extra, 0, all, 1, extra.length);
        return all;
    }
}
