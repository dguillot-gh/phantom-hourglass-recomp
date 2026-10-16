package com.phrecomp.ph;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
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

    private TextView status;
    private Button play, cont;

    private File rom() { return new File(getFilesDir(), "rom/azee.nds"); }

    private File save() { return new File(getFilesDir(), "saves/azee.sav"); }

    /** The resume state the game writes when it quits or goes to the background (frontend main.cpp). */
    private File resume(String ext) { return new File(getFilesDir(), "states/resume." + ext); }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        final LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setGravity(Gravity.CENTER);
        final int pad = dp(24);
        box.setPadding(pad, pad, pad, pad);
        box.setBackgroundColor(0xFF101014);

        final TextView title = new TextView(this);
        title.setText("Phantom Hourglass");
        title.setTextColor(Color.WHITE);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 34);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setGravity(Gravity.CENTER);
        box.addView(title);

        final TextView sub = new TextView(this);
        sub.setText("The Legend of Zelda · native recomp");
        sub.setTextColor(0xFF8AB4FF);
        sub.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        sub.setGravity(Gravity.CENTER);
        box.addView(sub);

        status = new TextView(this);
        status.setTextColor(0xFFB0B0B8);
        status.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, dp(12), 0, dp(20));
        box.addView(status);

        // Continue: the game from where it was last quit (its resume state); Play: from the title screen.
        cont = button(box, "Continue", () -> startActivity(new Intent(this, GameActivity.class)
                .putExtra(GameActivity.EXTRA_CONTINUE, true)));
        play = button(box, "Play", () -> startActivity(new Intent(this, GameActivity.class)));
        button(box, "Choose your game ROM (.nds)", () -> pick(PICK_ROM));
        button(box, "Import a save (.sav)", () -> pick(PICK_SAVE));
        button(box, "Export the save", () -> {
            final Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("application/octet-stream");
            i.putExtra(Intent.EXTRA_TITLE, "PhantomHourglass.sav");
            startActivityForResult(i, EXPORT_SAVE);
        });

        final TextView hint = new TextView(this);
        hint.setText("Use your own dump of the game (USA, clean or D-pad patch). A save from melonDS (.sav) can be "
                + "imported; the save already here is kept as a dated backup. In the game, touch the picture: it is "
                + "the stylus. The ≡ button opens the menu. Continue picks up where you last quit; a save state never "
                + "changes the game's own save.");
        hint.setTextColor(0xFF707078);
        hint.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        hint.setGravity(Gravity.CENTER);
        hint.setPadding(0, dp(16), 0, 0);
        box.addView(hint, new LinearLayout.LayoutParams(dp(420), ViewGroup.LayoutParams.WRAP_CONTENT));

        final ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(0xFF101014);
        scroll.setFillViewport(true);
        scroll.addView(box);
        setContentView(scroll);
        refresh();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
    }

    private void refresh() {
        final boolean haveRom = rom().isFile();
        final StringBuilder s = new StringBuilder();
        s.append(haveRom ? "Game ROM: ready" : "Choose your own Phantom Hourglass (USA) ROM to start.");
        s.append('\n');
        s.append(save().isFile() ? "Save: " + new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US)
                .format(new Date(save().lastModified())) : "Save: none yet (the game makes one when you save)");
        status.setText(s);
        play.setEnabled(haveRom);
        play.setAlpha(haveRom ? 1f : 0.5f);
        final boolean haveResume = haveRom && resume("state").isFile();
        cont.setVisibility(haveResume ? View.VISIBLE : View.GONE);
        if (haveResume) cont.setText("Continue  (" + resumeTime() + ")");
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

    private Button button(LinearLayout box, String text, Runnable action) {
        final Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setOnClickListener(v -> action.run());
        final LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(320), ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(8);
        box.addView(b, lp);
        return b;
    }

    private int dp(int v) { return Math.round(v * getResources().getDisplayMetrics().density); }

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
