package com.phrecomp.ph;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.MessageDigest;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * The start screen: bring in your own Phantom Hourglass ROM (and, if you like, a save from another emulator), then
 * play. The ROM and the save are copied into the app's own storage (files/rom/azee.nds, files/saves/azee.sav); the
 * originals are only read. A save is never overwritten without a dated backup next to it.
 */
public final class LauncherActivity extends Activity {
    private static final int PICK_ROM = 1, PICK_SAVE = 2, EXPORT_SAVE = 3, PICK_MODULE = 4;
    // The dumps the game can be recompiled for (SHA-1): USA and Europe, clean or patched.
    private static final String[] KNOWN = {
            "4c8f52dd719918bbcd46e73a8bae8628139c1b85",
            "7e0d26d143992f1eee182b24f1abb9e7f038eacf",
            "8915d99da2dd99bd2c05bc7a79fd2660a5e8ce5e",
            "02be55db55cf254bd064d2b3eb368b92a5b4156d",
            "8e3bbda6056c47f06fecf1a06d9f144a19283d88",
    };
    private static final int SAVE_SIZE = 512 * 1024;  // the cartridge's 4 Mbit flash

    private File rom() { return new File(getFilesDir(), "rom/azee.nds"); }

    /** The recompiled game, for a shell APK (BuildConfig.SHELL): frontend main.cpp loads it from here when present. */
    private File module() { return new File(getFilesDir(), "game/libph_game.so"); }

    private static final String MODULE_HELP = "The game is built on this phone from your ROM: nothing from the game is "
            + "downloaded. It takes a few minutes, once.";

    /** Which app install the game module was built by (lastUpdateTime:versionCode); a newer app rebuilds it. */
    private File moduleStamp() { return new File(getFilesDir(), "game/built-for.txt"); }

    private String appStamp() { return GameBuilder.appStamp(this); }

    private void stampModule() {
        try (FileOutputStream out = new FileOutputStream(moduleStamp())) {
            out.write(appStamp().getBytes("UTF-8"));
        } catch (IOException ignored) {
            // (no stamp: the module is taken as current)
        }
    }

    /** True when the app was updated since the game was built (the new engine needs the game built again). A module
     *  without a stamp (copied in by the PC scripts, or built before stamps) is taken as current. */
    private boolean moduleStale() {
        if (!BuildConfig.SHELL || !module().isFile() || !moduleStamp().isFile()) return false;
        try (BufferedReader r = new BufferedReader(new FileReader(moduleStamp()))) {
            final String line = r.readLine();
            return line == null || !line.trim().equals(appStamp());
        } catch (IOException e) {
            return false;
        }
    }

    private boolean building;

    private File save() { return new File(getFilesDir(), "saves/azee.sav"); }

    /** The resume state the game writes when it quits or goes to the background (frontend main.cpp). */
    private File resume(String ext) { return new File(getFilesDir(), "states/resume." + ext); }

    /** Intent extra from the game (GameActivity.restartWithResume): continue it again at once, in a fresh process. */
    static final String EXTRA_RELAUNCH = "relaunch";

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
    }

    /** Continue, once the old game process has exited (SDL's native side starts once per process), or after 4 s. */
    private void relaunchWhenGameGone(long until) {
        boolean alive = false;
        final android.app.ActivityManager am = (android.app.ActivityManager) getSystemService(ACTIVITY_SERVICE);
        final java.util.List<android.app.ActivityManager.RunningAppProcessInfo> procs = am.getRunningAppProcesses();
        if (procs != null)
            for (android.app.ActivityManager.RunningAppProcessInfo p : procs)
                if (p.processName.equals(getPackageName() + ":game")) alive = true;
        if (alive && System.currentTimeMillis() < until) {
            getWindow().getDecorView().postDelayed(() -> relaunchWhenGameGone(until), 100);
            return;
        }
        startActivity(new Intent(this, GameActivity.class).putExtra(GameActivity.EXTRA_CONTINUE, true));
    }

    // ---- per game ----
    private static final String TITLE = "The Legend of Zelda\nPhantom Hourglass";
    private static final String SUBTITLE = "Nintendo DS · native Android port";
    private static final String ROM_HELP = "Choose your own dump of Phantom Hourglass (USA or Europe, clean or patched). "
            + (BuildConfig.SHELL ? "The game is then built from it on this phone. "
                    : "It must match the region this APK was built for. ")
            + "It is copied into the app's storage; nothing from the game is downloaded. "
            + "A save from melonDS (.sav) can be imported afterwards under Save file.";
    private static final String SAVE_NAME = "PhantomHourglass.sav";
    private static final boolean GPU_DEFAULT = true;   // GameActivity: prefs.getBoolean("gpu", ...)

    private LauncherLook look;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        look = new LauncherLook(this);
        setContentView(look.root);
        refresh();
    }

    @Override
    protected void onResume() {
        super.onResume();
        look.hideSystemBars();
        refresh();
        UpdateCheck.maybeCheck(this, prefs(), this::refresh);
        if (BuildConfig.SHELL && !building && OptimizeService.running()) buildGame(false);  // (the build goes on)
        // Benchmark from adb (the game activity is not exported): am start -n <pkg>/com.phrecomp.ph.LauncherActivity
        //   --ei bench_frames 1320 [--ez bench_continue true]  ->  the game unpaced, quits after N frames, log "bench:".
        if (getIntent() != null && getIntent().getIntExtra("bench_frames", 0) > 0) {
            final int n = getIntent().getIntExtra("bench_frames", 0);
            final boolean cont = getIntent().getBooleanExtra("bench_continue", false);
            getIntent().removeExtra("bench_frames");
            // (--ez bench_paced true: at the DS's 60 fps like normal play, to see whether it holds and at what CPU cost)
            final boolean paced = getIntent().getBooleanExtra("bench_paced", false);
            startActivity(new Intent(this, GameActivity.class).putExtra("unpaced", !paced).putExtra("exit_after", n)
                    .putExtra(GameActivity.EXTRA_CONTINUE, cont));
        }
        if (getIntent() != null && getIntent().getBooleanExtra(EXTRA_RELAUNCH, false)) {
            getIntent().removeExtra(EXTRA_RELAUNCH);
            if (resume("state").isFile()) relaunchWhenGameGone(System.currentTimeMillis() + 4000);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) look.hideSystemBars();
    }

    @Override
    public boolean dispatchKeyEvent(android.view.KeyEvent e) {
        return look.onKey(e) || super.dispatchKeyEvent(e);
    }

    private SharedPreferences prefs() { return getSharedPreferences(DsPad.PREFS, MODE_PRIVATE); }

    /** Builds the start screen (LauncherLook: the KH Birth by Sleep port's style). */
    private void refresh() {
        final LinearLayout column = look.begin(TITLE, SUBTITLE, getDrawable(R.drawable.launcher_background));
        if (!rom().isFile()) {
            column.addView(look.text("First-time setup", 20, Color.WHITE, true));
            final TextView info = look.text(ROM_HELP, 15, 0xFFDDDDDD, false);
            info.setPadding(0, look.dp(8), 0, look.dp(20));
            column.addView(info);
            look.button(column, "Choose game ROM (.nds)…", null, () -> pick(PICK_ROM)).requestFocus();
            return;
        }
        if (BuildConfig.SHELL && !module().isFile()) {
            column.addView(look.text("One more step: build the game", 20, Color.WHITE, true));
            final TextView info = look.text(MODULE_HELP, 15, 0xFFDDDDDD, false);
            info.setPadding(0, look.dp(8), 0, look.dp(20));
            column.addView(info);
            look.button(column, "Build the game", null, () -> buildGame(false)).requestFocus();
            look.button(column, "Use a game module built on a PC…", "libph_game.so", () -> pick(PICK_MODULE));
            return;
        }
        if (moduleStale() && !building) {
            // The app was updated: build the game again for the new version first (the ROM is kept; saves stay).
            column.post(() -> buildGame(true));
        }
        final View first;
        if (resume("state").isFile()) {
            first = look.button(column, "Continue", "Where you last quit · " + resumeTime(), () ->
                    startActivity(new Intent(this, GameActivity.class).putExtra(GameActivity.EXTRA_CONTINUE, true)));
            look.button(column, "Start game", "From the title screen (in-game saves)",
                    () -> startActivity(new Intent(this, GameActivity.class)));
        } else {
            first = look.button(column, "Start game", "From the title screen",
                    () -> startActivity(new Intent(this, GameActivity.class)));
        }
        final String update = UpdateCheck.available(prefs());
        if (update != null)
            look.button(column, "Update to " + update, "A new version is out: download it from GitHub (saves stay)",
                    () -> UpdateCheck.install(this, prefs()));
        final LinearLayout row = look.row(column);
        look.small(row, "Save file", this::saveFile);
        look.small(row, "Trophies", () -> {
            final File achFile = new File(getFilesDir(), "saves/achievements.txt");
            PhAchievementsUi.showDialog(this, achFile, false, this::refresh);
        });
        look.small(row, "Settings", () -> look.settings(prefs(), GPU_DEFAULT, this::refresh));
        look.small(row, "Game ROM", this::gameRom);
        look.note(column, LauncherLook.summary(prefs(), GPU_DEFAULT) + "  ·  Save states: in the game's ≡ menu");
        first.requestFocus();
    }

    private void saveFile() {
        final String when = save().isFile() ? "Last saved: " + new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US)
                .format(new Date(save().lastModified())) : "No save yet (the game makes one when you save in it).";
        look.dialog().setTitle("Save file").setMessage(when + "\n\nImport a save from melonDS (.sav): the save "
                        + "already here is kept as a dated backup. Export copies the save out.")
                .setPositiveButton("Import…", (d, w) -> pick(PICK_SAVE))
                .setNeutralButton("Export…", (d, w) -> {
                    final Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                    i.addCategory(Intent.CATEGORY_OPENABLE);
                    i.setType("application/octet-stream");
                    i.putExtra(Intent.EXTRA_TITLE, SAVE_NAME);
                    startActivityForResult(i, EXPORT_SAVE);
                })
                .setNegativeButton("Back", null).show();
    }

    private void gameRom() {
        final AlertDialog.Builder b = look.dialog().setTitle("Game ROM").setMessage("Ready: " + rom().getName() + " ("
                        + (rom().length() >> 20) + " MB)\n\nChoose the ROM again to replace it. Saves and save "
                        + "states stay." + (BuildConfig.SHELL ? "\n\nGame module: " + (module().isFile()
                        ? (module().length() >> 20) + " MB, " + new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US)
                        .format(new Date(module().lastModified())) : "none") + "." + optimisedText() : ""))
                .setPositiveButton("Choose ROM…", (d, w) -> pick(PICK_ROM))
                .setNegativeButton("Back", null);
        if (BuildConfig.SHELL) b.setNeutralButton("Rebuild game", (d, w) -> buildGame(false));
        b.show();
    }

    /** How the game was built, for the Game ROM dialog (GameOptimizer writes game/optimised.txt). */
    private String optimisedText() {
        if (!GameOptimizer.available(this)) return "";
        if (OptimizeService.running()) return "\n\nBuilding: " + OptimizeService.progress() + "%";
        final File done = new File(getFilesDir(), "game/optimised.txt");
        if (done.isFile()) {
            try (BufferedReader r = new BufferedReader(new FileReader(done))) {
                return "\n\nBuilt with " + r.readLine();
            } catch (IOException ignored) {
                // (fall through)
            }
        }
        return module().isFile() ? "\n\nBuilt with the quick compiler (TinyCC)." : "";
    }

    /** When the resume state was written ("MM-DD HH:MM", from resume.txt). */
    private String resumeTime() {
        try (BufferedReader r = new BufferedReader(new FileReader(resume("txt")))) {
            final String line = r.readLine();
            if (line != null && !line.isEmpty()) return line.trim();
        } catch (IOException ignored) {
            // (no time: the file's date instead)
        }
        return new SimpleDateFormat("MM-dd HH:mm", Locale.US).format(new Date(resume("state").lastModified()));
    }


    private void pick(int request) {
        final Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        startActivityForResult(i, request);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        final Uri uri = data.getData();
        if (request == LauncherLook.REQUEST_BACKGROUND) {
            final String error = look.importBackground(uri);
            refresh();
            if (error != null) look.dialog().setMessage(error).setPositiveButton("OK", null).show();
            return;
        }
        // Large copies off the UI thread.
        new Thread(() -> {
            String message;
            try {
                if (request == PICK_ROM) message = importRom(uri);
                else if (request == PICK_MODULE) message = importModule(uri);
                else if (request == PICK_SAVE) message = importSave(uri);
                else message = exportSave(uri);
            } catch (IOException e) {
                message = "That did not work: " + e.getMessage();
            }
            final String m = message;
            final boolean build = BuildConfig.SHELL && request == PICK_ROM && m.startsWith("Game ROM ready");
            runOnUiThread(() -> {
                refresh();
                if (build) buildGame(false);  // (a new ROM needs its own game)
                else new AlertDialog.Builder(this).setMessage(m).setPositiveButton("OK", null).show();
            });
        }).start();
    }

    /**
     * Shell APK: builds files/game/libph_game.so from the ROM on the phone, with a progress bar. With the phone clang in
     * the APK (GameOptimizer.available), OptimizeService builds it with clang (a notification shows the progress too,
     * and the build carries on outside the app; TinyCC is its fallback); else TinyCC here (GameBuilder).
     */
    private void buildGame(boolean afterUpdate) {
        if (building) return;
        building = true;
        final boolean clang = GameOptimizer.available(this);
        final LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        final int pad = (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 20, getResources().getDisplayMetrics());
        box.setPadding(pad, pad / 2, pad, 0);
        final TextView text = new TextView(this);
        text.setText("Recompiling your ROM…");
        final android.widget.ProgressBar bar = new android.widget.ProgressBar(this, null,
                android.R.attr.progressBarStyleHorizontal);
        bar.setMax(100);
        final TextView note = new TextView(this);
        note.setText(clang ? "Built once on this phone with clang, for the best speed: several minutes on most "
                + "phones. You can leave the app: the notification shows the progress." : "");
        note.setAlpha(0.7f);
        box.addView(text);
        box.addView(bar);
        box.addView(note);
        final AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle(afterUpdate ? "App updated: building the game again" : "Building the game")
                .setView(box).setCancelable(false).show();
        final long start = System.currentTimeMillis();
        final boolean attach = clang && OptimizeService.running();  // (back in the app during a build: show it)
        if (!attach) {
            //noinspection ResultOfMethodCallIgnored
            module().delete();
            //noinspection ResultOfMethodCallIgnored
            new File(getFilesDir(), "game/optimised.txt").delete();
        }
        if (clang && !attach) {
            if (Build.VERSION.SDK_INT >= 33) requestPermissions(new String[] {"android.permission.POST_NOTIFICATIONS"}, 3);
            OptimizeService.start(this);
        }
        final Runnable tick = new Runnable() {
            @Override
            public void run() {
                if (!dialog.isShowing()) return;
                if (clang && !OptimizeService.running()) {
                    finishBuild(dialog, OptimizeService.result == null || OptimizeService.result.isEmpty() ? null
                            : OptimizeService.result, OptimizeService.millis);
                    return;
                }
                final int p = clang ? OptimizeService.progress() : GameBuilder.progress();
                final long secs = (System.currentTimeMillis() - start) / 1000;
                text.setText((clang ? OptimizeService.stage : p < 20 ? "Recompiling your ROM…" : "Compiling the game…")
                        + " " + p + "%   (" + secs / 60 + ":" + String.format(Locale.US, "%02d", secs % 60) + ")");
                bar.setProgress(p);
                getWindow().getDecorView().postDelayed(this, 300);
            }
        };
        tick.run();
        if (clang) return;
        new Thread(() -> {
            final String error = GameBuilder.run(this, rom(), module(), true);
            if (error == null) GameBuilder.stamp(this);
            final long ms = System.currentTimeMillis() - start;
            android.util.Log.i("PHOpt", "TinyCC build " + (error == null ? "done" : "failed") + " in " + ms + " ms");
            runOnUiThread(() -> finishBuild(dialog, error, ms));
        }).start();
    }

    private void finishBuild(AlertDialog dialog, String error, long ms) {
        building = false;
        dialog.dismiss();
        refresh();
        new AlertDialog.Builder(this).setMessage(error == null
                        ? "The game is ready (built in " + (ms / 1000) + " s). Press Start game."
                        : error.startsWith("Built") ? error + "\n\nThe game is ready. Press Start game." : error)
                .setPositiveButton("OK", null).show();
    }

    /** A shell APK's game: an arm64 ELF shared library. Which ROM it was built from is checked when the game starts. */
    private String importModule(Uri uri) throws IOException {
        final File dest = module(), tmp = new File(dest.getPath() + ".tmp");
        //noinspection ResultOfMethodCallIgnored
        dest.getParentFile().mkdirs();
        copy(uri, tmp);
        final byte[] h = new byte[20];
        try (InputStream in = new FileInputStream(tmp)) {
            if (in.read(h) != h.length) h[0] = 0;
        }
        // ELF magic, 64-bit (EI_CLASS 2), little-endian, shared object (e_type 3), AArch64 (e_machine 183).
        if (!(h[0] == 0x7f && h[1] == 'E' && h[2] == 'L' && h[3] == 'F' && h[4] == 2 && h[5] == 1 && h[16] == 3
                && (h[18] & 0xff) == 183 && h[19] == 0)) {
            //noinspection ResultOfMethodCallIgnored
            tmp.delete();
            return "That file is not a game module: choose the libph_game.so the PC build made for Android (arm64).";
        }
        if (dest.exists() && !dest.delete()) throw new IOException("cannot replace the game module");
        if (!tmp.renameTo(dest)) throw new IOException("cannot store the game module");
        stampModule();
        return "Game module ready (" + (dest.length() >> 20) + " MB). Press Start game.";
    }

    private String importRom(Uri uri) throws IOException {
        final File dest = rom(), tmp = new File(dest.getPath() + ".tmp");
        //noinspection ResultOfMethodCallIgnored
        dest.getParentFile().mkdirs();
        final String sha1 = copy(uri, tmp);
        for (String k : KNOWN) {
            if (k.equals(sha1)) {
                if (!tmp.renameTo(dest)) throw new IOException("cannot store the ROM");
                return "Game ROM ready. Press Play.";
            }
        }
        //noinspection ResultOfMethodCallIgnored
        tmp.delete();
        return "That file is not a supported Phantom Hourglass dump (USA or Europe, clean or patched).\nSHA-1: " + sha1;
    }

    private String importSave(Uri uri) throws IOException {
        final File dest = save(), tmp = new File(dest.getPath() + ".tmp");
        //noinspection ResultOfMethodCallIgnored
        dest.getParentFile().mkdirs();
        copy(uri, tmp);
        if (tmp.length() != SAVE_SIZE) {
            final long n = tmp.length();
            //noinspection ResultOfMethodCallIgnored
            tmp.delete();
            return "That save is " + n + " bytes; a Phantom Hourglass save is " + SAVE_SIZE + " bytes (melonDS's .sav).";
        }
        if (dest.isFile()) {
            final String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date());
            if (!dest.renameTo(new File(dest.getPath() + ".bak-" + stamp))) throw new IOException("cannot back up the old save");
        }
        if (!tmp.renameTo(dest)) throw new IOException("cannot store the save");
        return "Save imported (the previous one, if any, is kept as a backup).";
    }

    private String exportSave(Uri uri) throws IOException {
        if (!save().isFile()) return "There is no save yet.";
        try (InputStream in = new FileInputStream(save()); OutputStream out = getContentResolver().openOutputStream(uri)) {
            if (out == null) throw new IOException("cannot write there");
            final byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        }
        return "Save exported.";
    }

    /** Copies a picked file to `to` and returns its SHA-1 (lowercase hex). */
    private String copy(Uri uri, File to) throws IOException {
        try (InputStream in = getContentResolver().openInputStream(uri); OutputStream out = new FileOutputStream(to)) {
            if (in == null) throw new IOException("cannot open the file");
            final MessageDigest sha = MessageDigest.getInstance("SHA-1");
            final byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
                sha.update(buf, 0, n);
            }
            final StringBuilder hex = new StringBuilder();
            for (byte b : sha.digest()) hex.append(String.format(Locale.US, "%02x", b));
            return hex.toString();
        } catch (java.security.NoSuchAlgorithmException e) {
            throw new IOException(e);
        }
    }
}
