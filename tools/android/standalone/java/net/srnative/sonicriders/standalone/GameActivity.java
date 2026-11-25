package net.srnative.sonicriders.standalone;

import net.srnative.sonicriders.SonicRidersActivity;

public final class GameActivity extends SonicRidersActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"c++_shared", "SDL2"};
    }

    @Override
    public void loadLibraries() {
        super.loadLibraries();
        System.load(getMainSharedObject());
    }

    @Override
    protected String getMainSharedObject() {
        return SetupActivity.module(this).getAbsolutePath();
    }

    @Override
    protected String[] getArguments() {
        return new String[] {SetupActivity.iso(this).getAbsolutePath()};
    }
}
