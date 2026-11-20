package com.phrecomp.ph;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
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
    private static final int PICK_ROM = 1, PICK_SAVE = 2, EXPORT_SAVE = 3;
    // The dumps the game was recompiled for (SHA-1): the clean USA one and the D-pad patched one.
    private static final String[] KNOWN = {
            "4c8f52dd719918bbcd46e73a8bae8628139c1b85",
            "7e0d26d143992f1eee182b24f1abb9e7f038eacf",
    };
    private static final int SAVE_SIZE = 512 * 1024;  // the cartridge's 4 Mbit flash

    private File rom() { return new File(getFilesDir(), "rom/azee.nds"); }

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
    private static final String ROM_HELP = "Choose your own dump of Phantom Hourglass (USA, clean or D-pad patch). "
            + "It is copied into the app's storage; nothing is downloaded. A save from melonDS (.sav) can be "
            + "imported afterwards under Save file.";
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
        look.dialog().setTitle("Game ROM").setMessage("Ready: " + rom().getName() + " ("
                        + (rom().length() >> 20) + " MB)\n\nChoose the ROM again to replace it. Saves and save "
                        + "states stay.")
                .setPositiveButton("Choose ROM…", (d, w) -> pick(PICK_ROM))
                .setNegativeButton("Back", null).show();
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
                else if (request == PICK_SAVE) message = importSave(uri);
                else message = exportSave(uri);
            } catch (IOException e) {
                message = "That did not work: " + e.getMessage();
            }
            final String m = message;
            runOnUiThread(() -> {
                refresh();
                new AlertDialog.Builder(this).setMessage(m).setPositiveButton("OK", null).show();
            });
        }).start();
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
        return "That file is not a supported Phantom Hourglass dump (USA, clean or D-pad patch).\nSHA-1: " + sha1;
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
