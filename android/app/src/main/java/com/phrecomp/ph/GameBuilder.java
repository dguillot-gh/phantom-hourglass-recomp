package com.phrecomp.ph;

import android.content.Context;
import android.content.res.AssetManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The shell APK's game builder (BuildConfig.SHELL): recompiles the player's ROM to C (libphbuild:
 * frontend/src/android_build.cpp), compiled into files/game/libph_game.so by clang (GameOptimizer) or, without it or
 * when it fails, TinyCC. All on the phone; nothing is downloaded.
 */
final class GameBuilder {
    private GameBuilder() {}

    private static boolean loaded;

    private static native String build(String rom, String assets, String out, String work, String keepC,
                                       boolean compile);

    /** 0-100 while build() runs. */
    static native int progress();

    /**
     * Recompiles the ROM to C; with `tcc`, also compiles it with TinyCC into `out` (the quick build, and the fallback
     * when clang fails). Without `tcc`, the C and its headers are left in GameOptimizer.dir for clang. Returns null when
     * done, else a message for the player. Call off the UI thread.
     */
    static String run(Context c, File rom, File out, boolean tcc) {
        try {
            if (!loaded) {
                System.loadLibrary("phbuild");
                loaded = true;
            }
            final File assets = new File(c.getFilesDir(), "phbuild");
            deleteTree(assets);
            unpack(c.getAssets(), "phbuild", assets);
            final File opt = GameOptimizer.dir(c);
            deleteTree(opt);
            final String error = build(rom.getPath(), assets.getPath(), out.getPath(),
                    new File(c.getCacheDir(), "phbuild-work").getPath(), tcc ? null : new File(opt, "gen").getPath(), tcc);
            if (error == null && !tcc) {
                if (!new File(assets, "tcc/include").renameTo(new File(opt, "inc"))
                        || !new File(assets, "rt").renameTo(new File(opt, "rt"))) {
                    deleteTree(opt);
                    deleteTree(assets);
                    return "Cannot keep the compiler's headers.";
                }
            }
            deleteTree(assets);
            return error;
        } catch (IOException | UnsatisfiedLinkError e) {
            return "The game could not be built: " + e.getMessage();
        }
    }

    /** Which app install built the game (lastUpdateTime:versionCode); a newer app rebuilds it (LauncherActivity). */
    static String appStamp(Context c) {
        try {
            return c.getPackageManager().getPackageInfo(c.getPackageName(), 0).lastUpdateTime + ":" + BuildConfig.VERSION_CODE;
        } catch (android.content.pm.PackageManager.NameNotFoundException e) {
            return "";
        }
    }

    /** Writes files/game/built-for.txt for the game just built. */
    static void stamp(Context c) {
        try (FileOutputStream out = new FileOutputStream(new File(c.getFilesDir(), "game/built-for.txt"))) {
            out.write(appStamp(c).getBytes("UTF-8"));
        } catch (IOException ignored) {
            // (no stamp: the module is taken as current)
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

    static void deleteTree(File f) {
        final File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        //noinspection ResultOfMethodCallIgnored
        f.delete();
    }
}
