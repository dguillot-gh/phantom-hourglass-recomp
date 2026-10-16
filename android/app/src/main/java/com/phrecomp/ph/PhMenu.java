package com.phrecomp.ph;

import android.app.Activity;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;

/**
 * The game's menu (the pad's ≡ button): a scrolling panel over the right part of the screen, the game still
 * visible beside it. Tapping outside it or Back closes it. (Laid out like the PS1 recomp apps' menu.)
 *
 * Screen: touch screen, top screen or both. Display: sharp or smooth picture, FPS counter, renderer (software or
 * GPU) and internal resolution (1×-6× with the GPU renderer). Save states: 12 slots with a picture of each, save and
 * load. Game: quit. Controls: each on-screen control on or off, opacity, size.
 */
final class PhMenu {
    private static final int ACCENT = 0xFF8AB4FF, PANEL = 0xF0181820, ROW = 0xFF2A2A34, HINT = 0xFF9090A0;

    private final GameActivity activity;
    private final DsPad pad;
    private final SharedPreferences prefs;
    private final float dp;
    private FrameLayout root;
    private ScrollView scroll;
    private LinearLayout list;  // the panel's contents (rebuilt by refresh)
    private int panelW;
    private static final int SLOTS = 12, COLUMNS = 3;

    PhMenu(GameActivity activity, DsPad pad, SharedPreferences prefs) {
        this.activity = activity;
        this.pad = pad;
        this.prefs = prefs;
        this.dp = activity.getResources().getDisplayMetrics().density;
    }

    boolean isOpen() { return root != null; }

    void close() {
        if (root == null) return;
        ((ViewGroup) root.getParent()).removeView(root);
        root = null;
    }

    void open() {
        if (root != null) return;
        final ViewGroup host = (ViewGroup) pad.getParent();
        if (host == null) return;
        pad.releaseAll();
        root = new FrameLayout(activity);
        root.setBackgroundColor(0x55000000);
        root.setOnClickListener(v -> close());  // a tap outside the panel

        scroll = new ScrollView(activity);
        scroll.setBackgroundColor(PANEL);
        scroll.setOnClickListener(v -> { });  // taps inside do not close it
        list = new LinearLayout(activity);
        list.setOrientation(LinearLayout.VERTICAL);
        list.setPadding(px(16), px(10), px(16), px(24));
        scroll.addView(list);
        final int screenW = activity.getResources().getDisplayMetrics().widthPixels;
        panelW = Math.min(screenW, Math.max(px(320), (int) (screenW * 0.40f)));
        build(list);
        root.addView(scroll, new FrameLayout.LayoutParams(panelW, ViewGroup.LayoutParams.MATCH_PARENT, Gravity.END));
        host.addView(root, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
    }

    /** Rebuilds the panel's contents with the new values, in place (the panel stays, so it does not flash). */
    private void refresh() {
        if (root == null) return;
        final int y = scroll.getScrollY();
        list.removeAllViews();
        build(list);
        scroll.post(() -> scroll.scrollTo(0, y));
    }

    // ---- contents ---------------------------------------------------------------------------------------------------
    private void build(LinearLayout list) {
        final LinearLayout head = new LinearLayout(activity);
        head.setGravity(Gravity.CENTER_VERTICAL);
        final TextView title = text("Menu", 22, Color.WHITE);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        head.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        final TextView fps = text(String.format(java.util.Locale.US, "%.1f fps", PhInput.nativeGameFps()), 13, HINT);
        fps.setPadding(0, 0, px(8), 0);
        head.addView(fps);
        final TextView x = text("✕", 24, Color.WHITE);
        x.setPadding(px(12), px(4), px(4), px(4));
        x.setOnClickListener(v -> close());
        head.addView(x);
        list.addView(head);

        section(list, "Screen");
        chips(list, new String[] {"Touch screen", "Top screen", "Both"}, pad.screen(), i -> {
            pad.setScreen(i);
            refresh();
        }, null);
        toggle(list, "Portrait", "Hold the phone upright: both screens stacked, like a DS held open, the controls "
                + "under them (also the ⟳ button).", activity.portrait(), on -> {
                    close();
                    activity.setPortrait(on);
                });

        section(list, "Display");
        list.addView(label("Picture", "Sharp keeps the DS's pixels crisp; smooth softens them when they are scaled up."));
        final boolean smooth = prefs.getBoolean("smooth", false);
        chips(list, new String[] {"Sharp", "Smooth"}, smooth ? 1 : 0, i -> {
            prefs.edit().putBoolean("smooth", i == 1).apply();
            PhInput.nativeSetSmooth(i == 1);
            refresh();
        }, null);
        list.addView(label("Picture size", "Fit keeps the DS's shape; Stretch fills the whole screen (the controls sit "
                + "over the picture)."));
        chips(list, new String[] {"Fit", "Stretch"}, prefs.getBoolean("stretch", false) ? 1 : 0, i -> {
            prefs.edit().putBoolean("stretch", i == 1).apply();
            PhInput.nativeSetStretch(i == 1);
            refresh();
        }, null);
        toggle(list, "FPS counter", "The game's frames per second. Drag it anywhere on the screen.",
                prefs.getBoolean("fps", false), on -> {
                    prefs.edit().putBoolean("fps", on).apply();
                    activity.setFpsShown(on);
                });
        // The renderer: the GPU one (OpenGL ES) draws the DS's 2D and 3D at 2× to 8× its size; the software one is
        // the DS's own picture (1×).
        final boolean failed = PhInput.nativeGpuFailed();
        final boolean gpu = prefs.getBoolean("gpu", false) && !failed;
        final int scale = Math.min(6, prefs.getInt("scale", 2));
        list.addView(label("Renderer", failed ? "The GPU renderer could not start on this phone: software only."
                : "GPU draws the game sharper at a higher resolution (uses more battery); Software is the DS's own picture."));
        chips(list, new String[] {"Software", "GPU"}, gpu ? 1 : 0, i -> {
            prefs.edit().putBoolean("gpu", i == 1).apply();
            PhInput.nativeSetVideo(i == 1, prefs.getInt("scale", 2));
            refresh();
        }, new boolean[] {true, !failed});
        list.addView(label("Internal resolution", gpu ? "How many times the DS's resolution the GPU renderer draws at."
                : "With the GPU renderer."));
        // (Up to 6×: at 8× the renderer's buffers ran a Pixel 8 out of graphics memory.)
        final int[] scales = {1, 2, 4, 6};
        int chosen = 0;
        for (int i = 1; i < scales.length; i++) if (gpu && scales[i] == scale) chosen = i;
        chips(list, new String[] {"1×", "2×", "4×", "6×"}, chosen, i -> {
            if (i == 0) return;
            prefs.edit().putInt("scale", scales[i]).apply();
            PhInput.nativeSetVideo(true, scales[i]);
            refresh();
        }, new boolean[] {!gpu, gpu, gpu, gpu});

        states(list);

        section(list, "Game");
        row(list, "Quit the game", () -> {
            close();
            activity.onBackPressed();
        });

        section(list, "Controls");
        toggle(list, "Widescreen (test)", "16:9 with the GPU renderer: more of the world at the sides. Things may "
                + "appear late at the edges.", prefs.getBoolean("wide", false), on -> {
            prefs.edit().putBoolean("wide", on).apply();
            PhInput.nativeSetWide(on);
            pad.setWide(on && prefs.getBoolean("gpu", false));
        });
        toggle(list, "Analog stick", "In place of the D-pad, like the 3DS's circle pad: Link walks the way you push it, "
                + "running at the rim. A controller's left stick does the same.", pad.stickMode(), pad::setStickMode);
        row(list, "Move and resize controls", () -> {
            close();
            pad.startEditing();
        });
        for (String[] g : DsPad.GROUPS) {
            final String id = g[0];
            toggle(list, g[1], null, pad.groupOn(id), on -> pad.setGroup(id, on));
        }
        row(list, "Opacity: " + pad.opacityPercent() + "%", () -> {
            pad.cycleOpacity();
            refresh();
        });
        row(list, "Size: " + pad.sizeName(), () -> {
            pad.cycleSize();
            refresh();
        });

        final TextView about = text("The game's own code runs natively (recompiled for this phone); melonDS provides "
                + "the DS hardware. Touch the picture: it is the stylus.", 12, HINT);
        about.setPadding(0, px(20), 0, 0);
        list.addView(about);
    }

    // ---- save states ------------------------------------------------------------------------------------------------
    private File slotFile(int slot, String ext) {
        return new File(new File(activity.getFilesDir(), "states"), String.format(java.util.Locale.US, "slot-%02d.%s", slot, ext));
    }

    /** When the slot was saved, or null if it is empty. */
    private String slotTime(int slot) {
        if (!slotFile(slot, "state").isFile()) return null;
        try (BufferedReader r = new BufferedReader(new FileReader(slotFile(slot, "txt")))) {
            final String line = r.readLine();
            return line != null && !line.isEmpty() ? line : "saved";
        } catch (java.io.IOException e) {
            return "saved";
        }
    }

    /** The slot's picture: the screen the player is looking at (the file has both, top then bottom). */
    private Bitmap slotPicture(int slot) {
        final Bitmap both = BitmapFactory.decodeFile(slotFile(slot, "png").getAbsolutePath());
        if (both == null || both.getWidth() != 256 || both.getHeight() != 384) return null;
        return Bitmap.createBitmap(both, 0, pad.screen() == PhInput.SCREEN_TOP ? 0 : 192, 256, 192);
    }

    /** A grid of the 12 slots (tap one to choose it), then Save and Load for the chosen one. */
    private void states(LinearLayout list) {
        section(list, "Save states");
        final int chosen = Math.max(1, Math.min(SLOTS, prefs.getInt("slot", 1)));
        final int cardW = (panelW - px(32) - px(6) * (COLUMNS - 1)) / COLUMNS;
        LinearLayout line = null;
        for (int i = 1; i <= SLOTS; i++) {
            if ((i - 1) % COLUMNS == 0) {
                line = new LinearLayout(activity);
                list.addView(line);
            }
            final int slot = i;
            final String when = slotTime(slot);
            final boolean sel = slot == chosen;
            final LinearLayout card = new LinearLayout(activity);
            card.setOrientation(LinearLayout.VERTICAL);
            card.setPadding(px(3), px(3), px(3), px(3));
            card.setBackground(rounded(sel ? ACCENT : 0xFF202028));
            final ImageView pic = new ImageView(activity);
            pic.setScaleType(ImageView.ScaleType.FIT_XY);
            pic.setBackgroundColor(when == null ? 0xFF121218 : ROW);
            final Bitmap b = when != null ? slotPicture(slot) : null;
            if (b != null) pic.setImageBitmap(b);
            card.addView(pic, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, (cardW - px(6)) * 3 / 4));
            final TextView name = text(slot + (when == null ? "  empty" : "  " + when), 11,
                    sel ? Color.BLACK : when == null ? 0xFF707078 : Color.WHITE);
            name.setSingleLine(true);
            name.setPadding(px(2), px(2), 0, 0);
            card.addView(name);
            card.setOnClickListener(v -> {
                prefs.edit().putInt("slot", slot).apply();
                refresh();
            });
            final LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(cardW, ViewGroup.LayoutParams.WRAP_CONTENT);
            lp.setMargins((i - 1) % COLUMNS == 0 ? 0 : px(6), px(6), 0, 0);
            line.addView(card, lp);
        }
        final boolean used = slotTime(chosen) != null;
        chips(list, new String[] {"Save to " + chosen, "Load " + chosen}, -1, i -> {
            if (i == 0) PhInput.nativeSaveState(chosen);
            else PhInput.nativeLoadState(chosen);
            close();  // (back to the game: "Saved to slot N" shows over it)
        }, new boolean[] {true, used});
        final TextView h = text("A state is the whole game at that moment. Loading one never changes your save in "
                + "the game itself.", 12, HINT);
        h.setPadding(px(4), px(4), 0, 0);
        list.addView(h);
    }

    // ---- widgets ----------------------------------------------------------------------------------------------------
    interface OnToggle { void set(boolean on); }

    interface OnChoice { void pick(int index); }

    private int px(float v) { return (int) (v * dp + 0.5f); }

    private TextView text(String s, float sp, int color) {
        final TextView t = new TextView(activity);
        t.setText(s);
        t.setTextColor(color);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        return t;
    }

    private GradientDrawable rounded(int color) {
        final GradientDrawable bg = new GradientDrawable();
        bg.setColor(color);
        bg.setCornerRadius(px(10));
        return bg;
    }

    private void section(LinearLayout list, String name) {
        final TextView t = text(name.toUpperCase(java.util.Locale.US), 13, ACCENT);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setPadding(0, px(18), 0, px(4));
        list.addView(t);
    }

    private void row(LinearLayout list, String name, Runnable action) {
        final TextView t = text(name, 17, Color.WHITE);
        t.setPadding(px(12), 0, px(12), 0);
        t.setGravity(Gravity.CENTER_VERTICAL);
        t.setBackground(rounded(ROW));
        t.setOnClickListener(v -> action.run());
        final LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, px(48));
        lp.topMargin = px(6);
        list.addView(t, lp);
    }

    private LinearLayout label(String name, String hint) {
        final LinearLayout box = new LinearLayout(activity);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(0, px(10), 0, 0);
        box.addView(text(name, 17, Color.WHITE));
        if (hint != null) box.addView(text(hint, 12, HINT));
        return box;
    }

    private void toggle(LinearLayout list, String name, String hint, boolean on, OnToggle onToggle) {
        final LinearLayout row = new LinearLayout(activity);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.addView(label(name, hint), new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        final Switch sw = new Switch(activity);
        sw.setChecked(on);
        sw.setOnCheckedChangeListener((b, checked) -> onToggle.set(checked));
        row.addView(sw);
        row.setOnClickListener(v -> sw.toggle());
        list.addView(row);
    }

    /** A row of choices; `enabled` (may be null: all) greys out the ones not available. */
    private void chips(LinearLayout list, String[] names, int selected, OnChoice onChoice, boolean[] enabled) {
        final LinearLayout row = new LinearLayout(activity);
        for (int i = 0; i < names.length; i++) {
            final int index = i;
            final boolean on = enabled == null || enabled[i];
            final boolean sel = i == selected;
            final TextView b = text(names[i], 16, sel ? Color.BLACK : on ? Color.WHITE : 0xFF606068);
            b.setGravity(Gravity.CENTER);
            b.setTypeface(Typeface.DEFAULT_BOLD);
            b.setBackground(rounded(sel ? ACCENT : on ? ROW : 0xFF202028));
            if (on) b.setOnClickListener(v -> onChoice.pick(index));
            final LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, px(44), 1);
            lp.setMargins(i == 0 ? 0 : px(6), px(6), 0, px(4));
            row.addView(b, lp);
        }
        list.addView(row);
    }
}
