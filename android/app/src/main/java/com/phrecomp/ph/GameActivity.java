package com.phrecomp.ph;

import android.app.AlertDialog;
import android.content.SharedPreferences;
import android.content.pm.ActivityInfo;
import android.os.Build;
import android.os.Bundle;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.File;

/**
 * The game: SDL runs the program (libmain.so, frontend/src/main.cpp) and the DS controls (DsPad) sit on top.
 * It runs in its own process (":game" in the manifest): SDL's native side starts once per process, so quitting ends
 * the process and the next Play starts a fresh one.
 */
public class GameActivity extends SDLActivity {
    private DsPad pad;
    private PhMenu menu;
    private FpsBadge fps;

    @Override
    protected String[] getLibraries() {
        // SDL3 is linked into libmain.so (no separate libSDL3.so); the game itself, libph_game.so, is loaded by main.
        return new String[] {"main"};
    }

    @Override
    protected String[] getArguments() {
        final File files = getFilesDir();
        final SharedPreferences prefs = getSharedPreferences(DsPad.PREFS, MODE_PRIVATE);
        // Android offers Software or Compute. Migrate the old classic-GPU selection to Compute, while preserving
        // Software choices from existing installs.
        final boolean hardware = prefs.getBoolean("gpu", true);
        prefs.edit().putBoolean("compute", hardware).apply();
        final java.util.ArrayList<String> args = new java.util.ArrayList<>(java.util.Arrays.asList(
                new File(files, "rom/azee.nds").getAbsolutePath(),
                "--data-dir", files.getAbsolutePath(),
                "--renderer", hardware ? "compute" : "software",
                "--scale", String.valueOf(hardware ? Math.min(4, prefs.getInt("scale", 2)) : 1)));
        if (getIntent() != null && getIntent().getBooleanExtra(EXTRA_CONTINUE, false)) args.add("--continue");
        if (getIntent() != null && getIntent().getBooleanExtra("fps_probe", false)) args.add("--fps-probe");
        if (hardware && prefs.getBoolean("wide", false)) args.add("--wide");
        if (hardware && prefs.getInt("wide_mode", 1) == 2) args.add("--wide21");
        final String[] textures = {"pixels", "smooth", "sharp"};
        args.add("--textures");
        args.add(textures[Math.max(0, Math.min(2, prefs.getInt("textures", 0)))]);
        args.add("--fixes");
        args.add("0");
        return args.toArray(new String[0]);
    }

    /** Intent extra: start from the resume state (the start screen's Continue). */
    static final String EXTRA_CONTINUE = "continue";

    /** The FPS counter on or off (the menu's switch). */
    void setFpsShown(boolean on) { if (fps != null) fps.setShown(on); }

    /** Portrait: the phone held upright with both screens stacked (the Rotate button and the menu's switch). */
    boolean portrait() { return getSharedPreferences(DsPad.PREFS, MODE_PRIVATE).getBoolean("portrait", false); }

    void setPortrait(boolean on) {
        getSharedPreferences(DsPad.PREFS, MODE_PRIVATE).edit().putBoolean("portrait", on).apply();
        applyOrientation();
        if (pad != null) pad.setScreen(on ? PhInput.SCREEN_BOTH : PhInput.SCREEN_TOUCH);
    }

    private void applyOrientation() {
        setRequestedOrientation(portrait() ? ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                                           : ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    /** SDL sets the orientation from the window it makes (landscape): ours is the player's choice instead. */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        runOnUiThread(this::applyOrientation);
    }

    @Override
    protected void onCreate(Bundle state) {
        setRequestedOrientation(portrait() ? ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                                           : ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= 28) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
        super.onCreate(state);
        if (mLayout == null) return;  // SDL could not start (it is finishing)
        if (!new File(getFilesDir(), "rom/azee.nds").isFile()) {  // reached without a ROM: back to the launcher
            finish();
            return;
        }
        pad = new DsPad(this);
        mLayout.addView(pad, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        final SharedPreferences prefs = getSharedPreferences(DsPad.PREFS, MODE_PRIVATE);
        // The FPS counter, over the controls: a finger drags it anywhere.
        fps = new FpsBadge(this, prefs);
        mLayout.addView(fps, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        mLayout.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> fps.place());
        menu = new PhMenu(this, pad, prefs);
        pad.setMenu(menu);
        // The display choices kept from last time.
        PhInput.nativeSetSmooth(prefs.getBoolean("smooth", false));
        PhInput.nativeSetFps(false);  // (the phone's counter is the FpsBadge, not drawn in the picture)
        setFpsShown(prefs.getBoolean("fps", false));
        PhInput.nativeSetStretch(prefs.getBoolean("stretch", false));
        PhInput.nativeSetStickMode(pad.stickMode());
        final int wideMode = prefs.getBoolean("wide", false) && prefs.getBoolean("gpu", true)
                ? Math.max(1, Math.min(2, prefs.getInt("wide_mode", 1))) : 0;
        pad.setWide(wideMode);
        PhInput.nativeSetWide(wideMode);
        if (portrait()) pad.setScreen(PhInput.SCREEN_BOTH);
        hideSystemBars();
    }

    @Override
    public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) hideSystemBars();
        else if (pad != null) pad.releaseAll();
    }

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    /* A gamepad or keyboard drives the game through SDL; the touch controls step aside until the screen is touched. */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (pad != null && event.getKeyCode() != KeyEvent.KEYCODE_BACK && fromController(event)) pad.hideForController();
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (pad != null && event.isFromSource(InputDevice.SOURCE_JOYSTICK)) pad.hideForController();
        return super.dispatchGenericMotionEvent(event);
    }

    private static boolean fromController(KeyEvent event) {
        if (event.isFromSource(InputDevice.SOURCE_GAMEPAD) || event.isFromSource(InputDevice.SOURCE_JOYSTICK)) return true;
        final InputDevice device = event.getDevice();
        return device != null && !device.isVirtual() && device.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC;
    }

    /** Quit: the resume state first (Continue on the start screen), waiting for it up to a few seconds. */
    private void quitWithResume() {
        PhInput.nativeRequestResume();
        new Thread(() -> {
            final long until = System.currentTimeMillis() + 6000;
            while (!PhInput.nativeResumeDone() && System.currentTimeMillis() < until) {
                try {
                    Thread.sleep(30);
                } catch (InterruptedException e) {
                    break;
                }
            }
            runOnUiThread(this::finish);
        }).start();
    }

    /** Back asks first: a stray swipe must not end the game. The save is on disk already (the game writes it). */
    @Override
    public void onBackPressed() {
        if (menu != null && menu.isOpen()) {  // Back closes the menu first
            menu.close();
            return;
        }
        if (pad != null && pad.isEditing()) {  // and the layout editor (keeping what was moved)
            pad.stopEditing();
            return;
        }
        if (pad != null) pad.releaseAll();
        new AlertDialog.Builder(this)
                .setTitle("Quit Phantom Hourglass?")
                .setMessage("Continue on the start screen picks up from here next time. (The game's own save "
                        + "holds only what you saved in the game.)")
                .setPositiveButton("Quit", (d, w) -> quitWithResume())
                .setNegativeButton("Keep playing", null)
                .show();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (isFinishing()) System.exit(0);  // a fresh process for the next game
    }
}
