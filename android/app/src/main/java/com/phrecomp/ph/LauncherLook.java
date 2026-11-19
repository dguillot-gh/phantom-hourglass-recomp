package com.phrecomp.ph;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.Matrix;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The start screen's look (2026-11-18), shared by the DS ports (copy this file as it is, only the package line
 * changes): the same style as the KH Birth by Sleep port's launcher. The game's picture fills the screen behind a
 * left-to-right dark shade; the menu is a column on the left (title, two wide buttons, one row of small ones, a
 * settings line). The picture: one the player picked (Settings >
 * Choose background image, kept as files/launcher_bg.img), else the game's cover in the app (res/drawable-nodpi/
 * launcher_cover.png, shown as a case on a blurred copy), else the screenshot of the last Continue point
 * (files/states/resume.png, both DS screens). Gamepad: the D-pad moves the focus, A presses.
 */
final class LauncherLook {
    static final int REQUEST_BACKGROUND = 41;
    private static final String BG_FILE = "launcher_bg.img";
    private static final int MENU_WIDTH_DP = 440;

    private final Activity a;
    final FrameLayout root;

    LauncherLook(Activity a) {
        this.a = a;
        root = new FrameLayout(a);
        root.setBackgroundColor(Color.BLACK);
    }

    int dp(float v) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, a.getResources().getDisplayMetrics()));
    }

    /** Full screen: call from onResume and onWindowFocusChanged. */
    @SuppressWarnings("deprecation")
    void hideSystemBars() {
        if (android.os.Build.VERSION.SDK_INT < 30) {
            a.getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
            return;
        }
        WindowInsetsController c = a.getWindow().getInsetsController();
        if (c != null) {
            c.hide(WindowInsets.Type.systemBars());
            c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        }
    }

    /** Gamepad A presses the focused button; returns true when it handled the key (call from dispatchKeyEvent). */
    boolean onKey(KeyEvent e) {
        if (e.getKeyCode() != KeyEvent.KEYCODE_BUTTON_A) return false;
        View f = a.getCurrentFocus();
        if (f != null && e.getAction() == KeyEvent.ACTION_UP) f.performClick();
        return true;
    }

    // ---- the screen ----

    private boolean portrait() {
        android.util.DisplayMetrics m = a.getResources().getDisplayMetrics();
        return m.heightPixels > m.widthPixels;
    }

    /** Clears the screen and draws the background; returns the menu column to fill. */
    LinearLayout begin(String title, String subtitle, Drawable fallbackArt) {
        root.removeAllViews();
        Bitmap pic = background();
        if (pic != null && isCover(pic)) addCover(pic);
        else if (pic != null) addPicture(pic, !hasCustomBackground());
        else if (fallbackArt != null) {
            View art = new View(a);
            art.setBackground(fallbackArt);
            root.addView(art, new FrameLayout.LayoutParams(-1, -1));
        }
        // Sideways: shade from the left, menu column on the left. Upright: shade from the bottom, menu at the bottom.
        final boolean upright = portrait();
        View scrim = new View(a);
        scrim.setBackground(new GradientDrawable(upright ? GradientDrawable.Orientation.BOTTOM_TOP
                : GradientDrawable.Orientation.LEFT_RIGHT, new int[]{0xF0000000, 0xB0000000, 0x30000000}));
        root.addView(scrim, new FrameLayout.LayoutParams(-1, -1));

        LinearLayout column = new LinearLayout(a);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setGravity(upright ? Gravity.BOTTOM : Gravity.CENTER_VERTICAL);
        if (upright) column.setPadding(dp(24), dp(24), dp(24), dp(40));
        else column.setPadding(dp(56), dp(24), dp(24), dp(24));
        ScrollView scroll = new ScrollView(a);
        scroll.setFillViewport(true);
        scroll.setVerticalScrollBarEnabled(false);
        scroll.addView(column);
        int width = upright ? -1 : Math.min(dp(MENU_WIDTH_DP), a.getResources().getDisplayMetrics().widthPixels);
        root.addView(scroll, new FrameLayout.LayoutParams(width, -1, Gravity.START));

        column.addView(text(title, 28, Color.WHITE, true));
        TextView sub = text(subtitle, 14, 0xFFB8C7E0, false);
        sub.setPadding(0, dp(2), 0, dp(16));
        column.addView(sub);
        return column;
    }

    TextView text(String s, float sp, int color, boolean bold) {
        TextView t = new TextView(a);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        return t;
    }

    /** A small grey line (the settings summary, a hint). */
    TextView note(LinearLayout column, String s) {
        TextView t = text(s, 12, 0xFF9FB0CC, false);
        column.addView(t);
        return t;
    }

    /** A full-width rounded button with a bold label and an optional detail line. */
    View button(LinearLayout parent, String label, String detail, Runnable click) {
        LinearLayout b = new LinearLayout(a);
        b.setOrientation(LinearLayout.VERTICAL);
        b.setPadding(dp(20), dp(10), dp(20), dp(10));
        b.addView(text(label, 19, Color.WHITE, true));
        if (detail != null) b.addView(text(detail, 13, 0xFFC8D2E6, false));
        b.setBackground(states());
        b.setFocusable(true);
        b.setClickable(true);
        b.setOnClickListener(v -> click.run());
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.bottomMargin = dp(8);
        parent.addView(b, lp);
        return b;
    }

    /** A row for the small buttons, so the less used actions share one line. */
    LinearLayout row(LinearLayout column) {
        LinearLayout row = new LinearLayout(a);
        row.setOrientation(LinearLayout.HORIZONTAL);
        column.addView(row, new LinearLayout.LayoutParams(-1, ViewGroup.LayoutParams.WRAP_CONTENT));
        return row;
    }

    View small(LinearLayout row, String label, Runnable click) {
        TextView t = text(label, 15, Color.WHITE, true);
        t.setGravity(Gravity.CENTER);
        t.setPadding(dp(6), dp(10), dp(6), dp(10));
        t.setBackground(states());
        t.setFocusable(true);
        t.setClickable(true);
        t.setOnClickListener(v -> click.run());
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        lp.bottomMargin = dp(8);
        if (row.getChildCount() > 0) lp.leftMargin = dp(8);
        row.addView(t, lp);
        return t;
    }

    AlertDialog.Builder dialog() {
        return new AlertDialog.Builder(a, android.R.style.Theme_DeviceDefault_Dialog_Alert);
    }

    private StateListDrawable states() {
        StateListDrawable bg = new StateListDrawable();
        bg.addState(new int[]{android.R.attr.state_pressed}, rounded(0xCC3D6FD6, 0xFFFFFFFF));
        bg.addState(new int[]{android.R.attr.state_focused}, rounded(0xB0284C9C, 0xFFFFFFFF));
        bg.addState(new int[]{}, rounded(0x80101828, 0x60FFFFFF));
        return bg;
    }

    private GradientDrawable rounded(int fill, int stroke) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(fill);
        d.setCornerRadius(dp(14));
        d.setStroke(dp(1), stroke);
        return d;
    }

    // ---- settings (the ones every DS port reads at start; the in-game menu has the rest) ----

    /** "GPU · 2× · smooth" from the game's settings. */
    static String summary(SharedPreferences p, boolean gpuDefault) {
        boolean gpu = p.getBoolean("gpu", gpuDefault);
        StringBuilder b = new StringBuilder(gpu ? "GPU" : "Software");
        if (gpu) b.append(" · ").append(Math.max(1, Math.min(4, p.getInt("scale", 2)))).append("×");
        if (p.getBoolean("smooth", false)) b.append(" · smooth");
        if (p.getBoolean("stretch", false)) b.append(" · stretched");
        b.append(p.getBoolean("portrait", false) ? " · upright" : " · sideways");
        return b.toString();
    }

    /** The start screen's Settings: a few display settings plus the background picture. */
    void settings(SharedPreferences p, boolean gpuDefault, Runnable changed) {
        final boolean gpu = p.getBoolean("gpu", gpuDefault);
        final int scale = Math.max(1, Math.min(4, p.getInt("scale", 2)));
        String[] items = {
                "Renderer: " + (gpu ? "GPU" : "Software"),
                "Resolution: " + scale + "×" + (gpu ? "" : " (GPU only)"),
                (p.getBoolean("smooth", false) ? "✓ " : "   ") + "Smooth picture",
                (p.getBoolean("stretch", false) ? "✓ " : "   ") + "Stretch to fill the screen",
                (p.getBoolean("portrait", false) ? "✓ " : "   ") + "Hold the phone upright (both screens stacked)",
                "Choose background image…",
                hasCustomBackground() ? "Use the game's cover again" : null,
        };
        int n = items[6] == null ? 6 : 7;
        String[] shown = java.util.Arrays.copyOf(items, n);
        dialog().setTitle("Settings").setItems(shown, (d, w) -> {
            switch (w) {
                case 0: p.edit().putBoolean("gpu", !gpu).apply(); break;
                case 1: {
                    String[] choices = {"1× (DS original)", "2×", "3×", "4×"};
                    dialog().setTitle("Resolution").setSingleChoiceItems(choices, scale - 1, (x, which) -> {
                        x.dismiss();
                        p.edit().putInt("scale", which + 1).apply();
                        changed.run();
                        settings(p, gpuDefault, changed);
                    }).setNegativeButton("Back", null).show();
                    return;
                }
                case 2: p.edit().putBoolean("smooth", !p.getBoolean("smooth", false)).apply(); break;
                case 3: p.edit().putBoolean("stretch", !p.getBoolean("stretch", false)).apply(); break;
                case 4: p.edit().putBoolean("portrait", !p.getBoolean("portrait", false)).apply(); break;
                case 5: pickBackground(); return;
                case 6: clearBackground(); changed.run(); return;
                default: return;
            }
            changed.run();
            settings(p, gpuDefault, changed);
        }).setNegativeButton("Done", null).show();
    }

    // ---- background picture ----

    boolean hasCustomBackground() { return new File(a.getFilesDir(), BG_FILE).isFile(); }

    void clearBackground() {
        //noinspection ResultOfMethodCallIgnored
        new File(a.getFilesDir(), BG_FILE).delete();
    }

    void pickBackground() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("image/*");
        try {
            a.startActivityForResult(i, REQUEST_BACKGROUND);
        } catch (Exception e) {
            dialog().setMessage("No file picker available").setPositiveButton("OK", null).show();
        }
    }

    /** Copies the picked picture into app storage if it decodes; returns an error message or null. */
    String importBackground(Uri uri) {
        File tmp = new File(a.getFilesDir(), BG_FILE + ".tmp");
        try (InputStream in = a.getContentResolver().openInputStream(uri); OutputStream out = new FileOutputStream(tmp)) {
            if (in == null) throw new IOException("can't open the file");
            byte[] buf = new byte[1 << 16];
            long total = 0;
            for (int n; (n = in.read(buf)) > 0; ) {
                out.write(buf, 0, n);
                if ((total += n) > 40L << 20) throw new IOException("the picture is too big (over 40 MB)");
            }
        } catch (IOException e) {
            //noinspection ResultOfMethodCallIgnored
            tmp.delete();
            return "Could not use that picture: " + e.getMessage();
        }
        if (decode(tmp) == null) {
            //noinspection ResultOfMethodCallIgnored
            tmp.delete();
            return "That file isn't a picture this phone can read.";
        }
        //noinspection ResultOfMethodCallIgnored
        tmp.renameTo(new File(a.getFilesDir(), BG_FILE));
        return null;
    }

    private Bitmap decode(File f) {
        if (!f.isFile()) return null;
        BitmapFactory.Options o = new BitmapFactory.Options();
        o.inJustDecodeBounds = true;
        BitmapFactory.decodeFile(f.getAbsolutePath(), o);
        if (o.outWidth <= 0 || o.outHeight <= 0) return null;
        int max = Math.max(a.getResources().getDisplayMetrics().widthPixels, a.getResources().getDisplayMetrics().heightPixels);
        int sample = 1;
        while (Math.max(o.outWidth, o.outHeight) / (sample * 2) >= max) sample *= 2;
        o = new BitmapFactory.Options();
        o.inSampleSize = sample;
        return BitmapFactory.decodeFile(f.getAbsolutePath(), o);
    }

    /** Picked picture, else the game's cover in the app (res/drawable-nodpi/launcher_cover), else the Continue screenshot. */
    private Bitmap background() {
        Bitmap b = decode(new File(a.getFilesDir(), BG_FILE));
        if (b != null) return b;
        int id = a.getResources().getIdentifier("launcher_cover", "drawable", a.getPackageName());
        if (id != 0 && (b = BitmapFactory.decodeResource(a.getResources(), id)) != null) return b;
        return decode(new File(a.getFilesDir(), "states/resume.png"));
    }

    private boolean isCover(Bitmap pic) {
        if (hasCustomBackground()) return pic.getWidth() < pic.getHeight() * 1.2f;
        return a.getResources().getIdentifier("launcher_cover", "drawable", a.getPackageName()) != 0;
    }

    /**
     * Box art: a blurred, enlarged copy fills the screen and the cover stands as a case with rounded corners and a
     * shadow in the free area (right of the menu sideways, above it upright).
     */
    private void addCover(final Bitmap pic) {
        ImageView back = new ImageView(a);
        if (android.os.Build.VERSION.SDK_INT >= 31) {
            back.setImageBitmap(Bitmap.createScaledBitmap(pic, 256, Math.max(1, 256 * pic.getHeight() / pic.getWidth()), true));
            back.setRenderEffect(android.graphics.RenderEffect.createBlurEffect(dp(18), dp(18),
                    android.graphics.Shader.TileMode.CLAMP));
        } else {
            Bitmap b = Bitmap.createScaledBitmap(pic, 40, Math.max(1, 40 * pic.getHeight() / pic.getWidth()), true);
            for (int i = 0; i < 4; i++) b = Bitmap.createScaledBitmap(b, b.getWidth() * 2, b.getHeight() * 2, true);
            back.setImageBitmap(b);
        }
        back.setScaleType(ImageView.ScaleType.CENTER_CROP);
        root.addView(back, new FrameLayout.LayoutParams(-1, -1));

        final boolean upright = portrait();
        final int menuWidth = dp(MENU_WIDTH_DP);
        final ImageView card = new ImageView(a);
        card.setImageBitmap(pic);
        card.setScaleType(ImageView.ScaleType.FIT_XY);
        GradientDrawable shape = new GradientDrawable();
        shape.setCornerRadius(dp(10));
        shape.setColor(Color.BLACK);
        card.setBackground(shape);
        card.setClipToOutline(true);
        card.setElevation(dp(24));
        root.addView(card, new FrameLayout.LayoutParams(0, 0));
        root.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> {
            int w = r - l, h = b - t;
            if (w == 0) return;
            // The free area: right of the menu (sideways) or the top 45% of the screen (upright).
            float fx = upright ? 0 : menuWidth, fw = upright ? w : Math.max(1, w - menuWidth);
            float fh = upright ? h * 0.45f : h;
            int ch = Math.round(Math.min(fh * (upright ? 0.8f : 0.74f), fw * 0.8f * pic.getHeight() / pic.getWidth()));
            int cw = ch * pic.getWidth() / pic.getHeight();
            FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) card.getLayoutParams();
            int left = Math.round(fx + (fw - cw) / 2f), top = Math.round((fh - ch) / 2f);
            if (lp.width == cw && lp.height == ch && lp.leftMargin == left && lp.topMargin == top) return;
            lp.width = cw; lp.height = ch; lp.leftMargin = left; lp.topMargin = top;
            card.post(() -> card.setLayoutParams(lp));
        });
    }

    /**
     * Places the picture in the area right of the menu, centred. A picked picture covers that area; the DS
     * screenshot (two screens, taller than wide) is shown whole at full height, with soft dark edges.
     */
    private void addPicture(final Bitmap pic, final boolean screenshot) {
        final int menuWidth = portrait() ? 0 : dp(MENU_WIDTH_DP);
        final View left = new View(a), right = new View(a);
        left.setBackground(new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT, new int[]{0xFF000000, 0x00000000}));
        right.setBackground(new GradientDrawable(GradientDrawable.Orientation.RIGHT_LEFT, new int[]{0xFF000000, 0x00000000}));
        ImageView bg = new ImageView(a) {
            @Override protected void onLayout(boolean changed, int l, int t, int r, int b) {
                super.onLayout(changed, l, t, r, b);
                final int w = r - l, h = b - t;
                float free = Math.max(1, w - menuWidth);
                float scale = screenshot ? h / (float) pic.getHeight()
                        : Math.max(free / pic.getWidth(), h / (float) pic.getHeight());
                float targetX = menuWidth + free / 2f;
                Matrix m = new Matrix();
                m.setScale(scale, scale);
                final float x0 = targetX - pic.getWidth() * scale / 2f;
                m.postTranslate(x0, (h - pic.getHeight() * scale) / 2f);
                setImageMatrix(m);
                final int edgeL = Math.round(x0), edgeR = Math.round(x0 + pic.getWidth() * scale);
                post(() -> {
                    place(left, edgeL > 0 ? edgeL : 0, edgeL > 0 ? dp(64) : 0);
                    place(right, edgeR - dp(64), edgeR < w ? dp(64) : 0);
                });
            }
        };
        bg.setImageBitmap(pic);
        bg.setScaleType(ImageView.ScaleType.MATRIX);
        root.addView(bg, new FrameLayout.LayoutParams(-1, -1));
        root.addView(left, new FrameLayout.LayoutParams(0, -1, Gravity.START));
        root.addView(right, new FrameLayout.LayoutParams(0, -1, Gravity.START));
    }

    private static void place(View v, int left, int width) {
        FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) v.getLayoutParams();
        if (lp.leftMargin == left && lp.width == width) return;
        lp.leftMargin = left;
        lp.width = width;
        v.setLayoutParams(lp);
    }
}
