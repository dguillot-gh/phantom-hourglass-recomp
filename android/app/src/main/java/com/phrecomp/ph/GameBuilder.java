package com.phrecomp.ph;

import android.content.Context;
import android.content.res.AssetManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The shell APK's game builder (BuildConfig.SHELL): recompiles the player's ROM to C and compiles it with TinyCC into
 * files/game/libph_game.so, on the phone (libphbuild: frontend/src/android_build.cpp). Nothing is downloaded.
 */
final class GameBuilder {
    private GameBuilder() {}

    private static boolean loaded;

    private static native String build(String rom, String assets, String out, String work);

    /** 0-100 while build() runs. */
    static native int progress();

    /** Builds the game; returns null when done, else a message for the player. Call off the UI thread. */
    static String run(Context c, File rom, File out) {
        try {
            if (!loaded) {
                System.loadLibrary("phbuild");
                loaded = true;
            }
            final File assets = new File(c.getFilesDir(), "phbuild");
            deleteTree(assets);
            unpack(c.getAssets(), "phbuild", assets);
            final String error = build(rom.getPath(), assets.getPath(), out.getPath(),
                    new File(c.getCacheDir(), "phbuild-work").getPath());
            deleteTree(assets);
            return error;
        } catch (IOException | UnsatisfiedLinkError e) {
            return "The game could not be built: " + e.getMessage();
        }
    }

    private static void unpack(AssetManager am, String from, File to) throws IOException {
        final String[] names = am.list(from);
        if (names != null && names.length > 0) {
            if (!to.isDirectory() && !to.mkdirs()) throw new IOException("cannot create " + to);
            for (String n : names) unpack(am, from + "/" + n, new File(to, n));
            return;
        }
        try (InputStream in = am.open(from); OutputStream out = new FileOutputStream(to)) {
            final byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        }
    }

    private static void deleteTree(File f) {
        final File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        //noinspection ResultOfMethodCallIgnored
        f.delete();
    }
}
