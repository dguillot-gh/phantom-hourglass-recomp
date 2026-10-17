package com.phrecomp.ph;

import android.app.Activity;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.util.SparseArray;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;
import java.util.List;

/**
 * The on-screen DS controls, drawn over the game.
 *
 * Phantom Hourglass is played on the touch screen itself: any touch that does not land on a control goes through to
 * the game as the stylus. The controls sit in the bars beside the picture: a D-pad (the D-pad patch moves Link with
 * it), small Mic / Lid / Top-screen buttons and a menu button. A B X Y (in the DS's own places: X top, A right, B
 * bottom, Y left), L / R and Start / Select can be switched on in the menu; every control can be switched off.
 *
 * Positions and sizes are in units of the screen's short side, from the left or the right edge, so the controls keep
 * their physical size on any phone. In landscape they fill the bars beside the picture; in portrait (the Rotate button:
 * both screens stacked at the top, like a DS held open) the same layout fills the space under the screens. Every
 * finger is tracked on its own.
 *
 * The menu's "Analog stick" switch turns the D-pad into a stick like the 3DS's circle pad: Link walks the way it is
 * pushed, slowly near the middle and running at the rim (frontend/src/ph_stick.hpp holds the stylus near Link for it).
 *
 * The menu's "Move and resize controls" opens the layout editor: drag a control anywhere (over the picture too), make
 * it smaller or bigger, hide it, or reset the layout. Each orientation keeps a layout of its own (preferences
 * "lay_land_<id>" / "lay_port_<id>": side, distance from that edge and height, in screen heights, and a size factor;
 * "hide_land_<id>" / "hide_port_<id>"). A control never moved keeps its place in the bars, as above.
 */
public final class DsPad extends View {
    private static final int KIND_BUTTON = 0, KIND_DPAD = 1, KIND_SHOULDER = 2, KIND_TOOL = 3;
    private static final int LEFT = -1, RIGHT = 1;
    private static final int TOOL_MENU = 1, TOOL_MIC = 2, TOOL_LID = 3, TOOL_TOP = 4, TOOL_ROTATE = 5,
            TOOL_MIC_SAMPLE = 6;
    private static final float HIT_GROW = 1.25f;  // a thumb lands off-centre: the hit area is a bit larger than the drawing
    static final String PREFS = "ph_pad";

    /** One control. Groups switch on and off together (the menu's list). */
    private static final class Control {
        final int kind, bits, tool, side;
        final String id, label, group;
        final float x, y, r;  // x from the side's edge, y from the top, radius: all in screen heights
        // Moved in the editor (for the orientation shown): the side, the centre's distance from that edge and from
        // the top, in units of the screen's height; and a size factor. `moved` false: the default place.
        boolean moved, hidden;
        int mSide;
        float mx, my, mScale = 1f;
        Control(int kind, int bits, int tool, String id, String label, String group, int side, float x, float y, float r) {
            this.kind = kind;
            this.bits = bits;
            this.tool = tool;
            this.id = id;
            this.label = label;
            this.group = group;
            this.side = side;
            this.x = x;
            this.y = y;
            this.r = r;
        }
    }

    /* The menu's groups: preference key, label, on by default. */
    static final String[][] GROUPS = {
            {"dpad", "D-pad", "1"},
            {"face", "A B X Y buttons", "0"},
            {"shoulders", "L and R", "0"},
            {"startselect", "Start and Select", "0"},
            {"mic", "Mic button", "1"},
            {"mic_sample", "Blow sample button", "1"},
            {"lid", "Lid button", "1"},
            {"top", "Top screen button", "1"},
            {"rotate", "Rotate button (portrait, both screens)", "1"},
    };
    private static final float[] OPACITY = {0.85f, 0.6f, 0.4f, 0.25f};
    private static final float[] SIZE = {0.85f, 1.0f, 1.2f};

    private final Activity activity;
    private final SharedPreferences prefs;
    private final List<Control> controls = new ArrayList<>();
    private final SparseArray<Control> fingers = new SparseArray<>();  // pointer id -> control it holds
    private final SparseArray<Integer> dpadBits = new SparseArray<>();  // pointer id -> direction on the D-pad
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), edge = new Paint(Paint.ANTI_ALIAS_FLAG),
            text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private int sentButtons = -1;
    private boolean micOn, micSampleOn, lidClosed, topShown, bothShown, hiddenForController;
    private PhMenu menu;  // the ≡ button's panel (GameActivity sets it)
    private boolean stickMode;   // the D-pad is an analog stick
    private int wideMode;        // 0: normal 4:3; 1: 16:9; 2: 21:9
    private int stickPointer = -1;
    private float knobX, knobY;  // the stick's position, -1..1 (y down)

    // The layout editor.
    private static final String[] TOOLS = {"Smaller", "Bigger", "Hide / show", "Reset all", "Done"};
    private final RectF[] toolRects = new RectF[TOOLS.length];
    private boolean editing;
    private Control selected;
    private int dragPointer = -1;
    private float dragDx, dragDy;
    private String loadedFor;  // the orientation whose layout the controls hold ("land" / "port")

    public DsPad(Activity activity) {
        super(activity);
        this.activity = activity;
        prefs = activity.getSharedPreferences(PREFS, Activity.MODE_PRIVATE);
        // Left bar: the D-pad, L, Select, and the menu and Top-screen buttons above.
        controls.add(new Control(KIND_DPAD, 0, 0, "dpad", "", "dpad", LEFT, 0.24f, 0.62f, 0.17f));
        controls.add(new Control(KIND_SHOULDER, PhInput.L, 0, "l", "L", "shoulders", LEFT, 0.16f, 0.30f, 0.06f));
        controls.add(new Control(KIND_BUTTON, PhInput.SELECT, 0, "select", "SELECT", "startselect", LEFT, 0.24f, 0.92f, 0.045f));
        controls.add(new Control(KIND_TOOL, 0, TOOL_MENU, "menu", "≡", null, LEFT, 0.07f, 0.08f, 0.05f));
        controls.add(new Control(KIND_TOOL, 0, TOOL_TOP, "top", "TOP", "top", LEFT, 0.19f, 0.08f, 0.05f));
        controls.add(new Control(KIND_TOOL, 0, TOOL_ROTATE, "rotate", "⟳", "rotate", LEFT, 0.31f, 0.08f, 0.05f));
        // Right bar: A B X Y in the DS's own places, R, Start, and the Mic and Lid buttons above.
        controls.add(new Control(KIND_BUTTON, PhInput.A, 0, "a", "A", "face", RIGHT, 0.10f, 0.62f, 0.065f));
        controls.add(new Control(KIND_BUTTON, PhInput.B, 0, "b", "B", "face", RIGHT, 0.24f, 0.76f, 0.065f));
        controls.add(new Control(KIND_BUTTON, PhInput.X, 0, "x", "X", "face", RIGHT, 0.24f, 0.48f, 0.065f));
        controls.add(new Control(KIND_BUTTON, PhInput.Y, 0, "y", "Y", "face", RIGHT, 0.38f, 0.62f, 0.065f));
        controls.add(new Control(KIND_SHOULDER, PhInput.R, 0, "r", "R", "shoulders", RIGHT, 0.16f, 0.30f, 0.06f));
        controls.add(new Control(KIND_BUTTON, PhInput.START, 0, "start", "START", "startselect", RIGHT, 0.24f, 0.92f, 0.045f));
        controls.add(new Control(KIND_TOOL, 0, TOOL_MIC, "mic", "MIC", "mic", RIGHT, 0.07f, 0.08f, 0.05f));
        controls.add(new Control(KIND_TOOL, 0, TOOL_LID, "lid", "LID", "lid", RIGHT, 0.19f, 0.08f, 0.05f));
        controls.add(new Control(KIND_TOOL, 0, TOOL_MIC_SAMPLE, "mic_sample", "BLOW", "mic_sample", RIGHT, 0.31f, 0.08f, 0.05f));
        text.setTextAlign(Paint.Align.CENTER);
        text.setFakeBoldText(true);
        edge.setStyle(Paint.Style.STROKE);
        setHapticFeedbackEnabled(true);
        for (int i = 0; i < toolRects.length; i++) toolRects[i] = new RectF();
        stickMode = prefs.getBoolean("stick", false);
    }

    boolean stickMode() { return stickMode; }

    /** Widescreen: widen the game view and adjust the touch mapping. */
    void setWide(int mode) {
        wideMode = Math.max(0, Math.min(2, mode));
        invalidate();
    }

    /** The analog stick in place of the D-pad (on), or the D-pad (off). The game is told as well. */
    void setStickMode(boolean on) {
        releaseAll();
        stickMode = on;
        prefs.edit().putBoolean("stick", on).apply();
        PhInput.nativeSetStickMode(on);
        invalidate();
    }

    void setMenu(PhMenu menu) { this.menu = menu; }

    // ---- preferences (the menu changes them) --------------------------------------------------------------------
    boolean groupOn(String group) {
        if (group == null) return true;  // the menu button is always there
        for (String[] g : GROUPS)
            if (g[0].equals(group)) return prefs.getBoolean("show_" + group, g[2].equals("1"));
        return true;
    }

    private float opacity() { return OPACITY[Math.max(0, Math.min(OPACITY.length - 1, prefs.getInt("opacity", 1)))]; }

    private float sizeScale() { return SIZE[Math.max(0, Math.min(SIZE.length - 1, prefs.getInt("size", 1)))]; }

    void setGroup(String group, boolean on) {
        prefs.edit().putBoolean("show_" + group, on).apply();
        // A switched-off Lid or Top button must not leave the DS closed or the top screen up.
        if (!on && group.equals("lid") && lidClosed) {
            lidClosed = false;
            PhInput.nativeSetLid(false);
        }
        if (!on && group.equals("top") && topShown) setScreen(PhInput.SCREEN_TOUCH);
        invalidate();
    }

    int opacityPercent() { return Math.round(opacity() * 100); }

    void cycleOpacity() {
        prefs.edit().putInt("opacity", (prefs.getInt("opacity", 1) + 1) % OPACITY.length).apply();
        invalidate();
    }

    String sizeName() {
        final int s = prefs.getInt("size", 1);
        return s == 0 ? "small" : s == 1 ? "normal" : "large";
    }

    void cycleSize() {
        prefs.edit().putInt("size", (prefs.getInt("size", 1) + 1) % SIZE.length).apply();
        invalidate();
    }

    /** The screen(s) shown: PhInput.SCREEN_TOUCH, SCREEN_TOP or SCREEN_BOTH. */
    int screen() { return bothShown ? PhInput.SCREEN_BOTH : topShown ? PhInput.SCREEN_TOP : PhInput.SCREEN_TOUCH; }

    void setScreen(int screen) {
        topShown = screen == PhInput.SCREEN_TOP;
        bothShown = screen == PhInput.SCREEN_BOTH;
        PhInput.nativeShowScreen(screen);
        invalidate();
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        loadLayout();  // (rotated: the other orientation's layout)
        invalidate();
    }

    // ---- moved controls (the layout editor's) -----------------------------------------------------------------------
    private String orientation() { return portrait() ? "port" : "land"; }

    private void loadLayout() {
        final String o = orientation();
        loadedFor = o;
        for (Control c : controls) {
            c.moved = false;
            c.mScale = 1f;
            c.hidden = c.tool != TOOL_MENU && prefs.getBoolean("hide_" + o + "_" + c.id, false);
            final String v = prefs.getString("lay_" + o + "_" + c.id, null);
            if (v == null) continue;
            final String[] f = v.split(",");
            if (f.length != 4) continue;
            try {
                c.mSide = Integer.parseInt(f[0]) < 0 ? LEFT : RIGHT;
                c.mx = Float.parseFloat(f[1]);
                c.my = Float.parseFloat(f[2]);
                c.mScale = Math.max(0.5f, Math.min(2.5f, Float.parseFloat(f[3])));
                c.moved = true;
            } catch (NumberFormatException e) {
                c.moved = false;
            }
        }
    }

    private void saveLayout() {
        final String o = orientation();
        final SharedPreferences.Editor ed = prefs.edit();
        for (Control c : controls) {
            if (c.moved) ed.putString("lay_" + o + "_" + c.id, (c.mSide == LEFT ? -1 : 1) + "," + c.mx + "," + c.my + "," + c.mScale);
            else ed.remove("lay_" + o + "_" + c.id);
            if (c.hidden) ed.putBoolean("hide_" + o + "_" + c.id, true);
            else ed.remove("hide_" + o + "_" + c.id);
        }
        ed.apply();
    }

    private void ensureLayout() { if (!orientation().equals(loadedFor)) loadLayout(); }

    // ---- geometry ---------------------------------------------------------------------------------------------------
    // The controls' area on each side: in landscape the bar beside the picture (its whole height), in portrait half the
    // width under the screens. Positions are fractions of that height; sizes and distances from the edge are in a unit
    // as large as the area allows: no control may reach over the picture or another control.
    private boolean portrait() { return getHeight() > getWidth() * 1.25f; }

    // The picture, as frontend/src/layout.hpp places it: its width (landscape) or bottom edge (portrait: at the top, a
    // small margin, one or two DS screens the width of the phone).
    private float pictureWidth() {
        final float content = bothShown ? 384f : 192f, across = wideMode == 2 ? 448f : wideMode == 1 ? 342f : 256f;
        return across * Math.min(getWidth() / across, getHeight() / content);
    }

    private float portraitTop() {
        final float h = getHeight(), w = getWidth();
        final float screens = w * (bothShown ? 1.5f : 0.75f);
        final float margin = Math.max(0f, Math.min((h - screens) / 2f, h * 0.035f));
        return margin + screens;
    }

    // (A short phone held upright has little room under two screens: the controls then keep a usable size and sit over
    // the bottom of the touch screen.)
    private float areaHeight() {
        return portrait() ? Math.max(getHeight() - portraitTop(), getWidth() * 0.42f) : getHeight();
    }

    private float areaTop() { return getHeight() - areaHeight(); }

    private float areaWidth() {
        if (portrait()) return getWidth() / 2f;
        // A 21:9 image is wider than the Pixel's 20:9 display, so it fills the width and leaves no side bars.
        // Keep a usable control rail over each edge of the image instead of shrinking the controls to zero.
        final float minimumRail = getHeight() * 0.22f;
        return Math.max(minimumRail, (getWidth() - pictureWidth()) / 2f);
    }

    private float scaleOf(Control c) { return c.kind == KIND_TOOL ? 1f : sizeScale(); }

    // How far from the edge the controls reach, in units (the widest one decides).
    private float reach() {
        float m = 0.3f;
        for (Control c : controls) {
            if (!groupOn(c.group)) continue;
            m = Math.max(m, (c.x + c.r * (c.kind == KIND_SHOULDER ? 1.8f : 1f)) * scaleOf(c));
        }
        return m;
    }

    private float unit() { return Math.min(areaHeight(), areaWidth() / reach()); }

    private float radius(Control c) { return c.r * unit() * scaleOf(c) * (c.moved ? c.mScale : 1f); }

    private float cx(Control c) {
        if (c.moved) {
            final float off = c.mx * getHeight();
            return c.mSide == LEFT ? off : getWidth() - off;
        }
        final float off = c.x * unit() * scaleOf(c);
        return c.side == LEFT ? off : getWidth() - off;
    }

    private float cy(Control c) { return c.moved ? c.my * getHeight() : areaTop() + c.y * areaHeight(); }

    private boolean visible(Control c) {
        return groupOn(c.group) && !c.hidden && (!hiddenForController || c.kind == KIND_TOOL);
    }

    // In the editor every control switched on in the menu shows, hidden ones faintly (to show them again).
    private boolean shownInEditor(Control c) { return groupOn(c.group); }

    private Control controlAt(float x, float y) {
        Control best = null;
        float bestD = Float.MAX_VALUE;
        for (Control c : controls) {
            if (editing ? !shownInEditor(c) : !visible(c)) continue;
            final float dx = x - cx(c), dy = y - cy(c), d = (float) Math.hypot(dx, dy);
            final float reach = radius(c) * (c.kind == KIND_SHOULDER ? 1.6f : 1f) * HIT_GROW;
            if (d <= reach && d < bestD) {
                best = c;
                bestD = d;
            }
        }
        return best;
    }

    /* The D-pad direction (one of eight) under a finger, as DS bits; 0 in the dead centre. */
    private int dpadDirection(Control c, float x, float y) {
        final float dx = x - cx(c), dy = y - cy(c), r = radius(c);
        if (Math.hypot(dx, dy) < r * 0.22f) return 0;
        final double a = Math.toDegrees(Math.atan2(dy, dx));  // 0 = right, 90 = down
        int bits = 0;
        if (a > -67.5 && a < 67.5) bits |= PhInput.RIGHT;
        if (a > 112.5 || a < -112.5) bits |= PhInput.LEFT;
        if (a > 22.5 && a < 157.5) bits |= PhInput.DOWN;
        if (a < -22.5 && a > -157.5) bits |= PhInput.UP;
        return bits;
    }

    // ---- drawing ----------------------------------------------------------------------------------------------------
    @Override
    protected void onDraw(Canvas canvas) {
        ensureLayout();
        if (editing) canvas.drawColor(0x66000000);
        final int full = Math.round(255 * opacity());
        for (Control c : controls) {
            if (editing ? !shownInEditor(c) : !visible(c)) continue;
            final int alpha = editing ? (c.hidden ? 70 : 230) : full;
            final boolean pressed = !editing && isHeld(c);
            switch (c.kind) {
                case KIND_DPAD:
                    if (stickMode) drawStick(canvas, c, alpha);
                    else drawDpad(canvas, c, alpha);
                    break;
                case KIND_SHOULDER: drawShoulder(canvas, c, pressed, alpha); break;
                default: drawRound(canvas, c, pressed, alpha); break;
            }
            if (editing && c == selected) {  // the selected one: a ring around it
                edge.setColor(0xFF8AB4FF);
                edge.setAlpha(255);
                edge.setStrokeWidth(unit() * 0.012f);
                final float r = radius(c) * (c.kind == KIND_SHOULDER ? 1.9f : 1.15f);
                canvas.drawCircle(cx(c), cy(c), r, edge);
            }
        }
        if (editing) drawToolbar(canvas);
    }

    private void drawToolbar(Canvas canvas) {
        final float h = getHeight(), bh = 0.085f * Math.min(h, getWidth()), gap = bh * 0.18f;
        text.setTextSize(bh * 0.4f);
        float total = 0;
        final float[] widths = new float[TOOLS.length];
        for (int i = 0; i < TOOLS.length; i++) {
            widths[i] = text.measureText(TOOLS[i]) + bh * 0.8f;
            total += widths[i] + (i > 0 ? gap : 0);
        }
        // In the middle of the screen, a little above centre (clear of the bars' controls).
        float x = (getWidth() - total) / 2f;
        final float y = h * 0.40f;
        for (int i = 0; i < TOOLS.length; i++) {
            final boolean needsSelection = i < 3;
            final boolean on = !needsSelection || (selected != null && (i != 2 || selected.tool != TOOL_MENU));
            toolRects[i].set(x, y, x + widths[i], y + bh);
            fill.setColor(on ? 0xE0303038 : 0x80303038);
            canvas.drawRoundRect(toolRects[i], bh * 0.25f, bh * 0.25f, fill);
            text.setColor(on ? 0xFFFFFFFF : 0x80FFFFFF);
            canvas.drawText(TOOLS[i], toolRects[i].centerX(), y + bh * 0.64f, text);
            x += widths[i] + gap;
        }
        text.setTextSize(bh * 0.34f);
        text.setColor(0xFFFFFFFF);
        canvas.drawText(selected == null ? "Tap a control to choose it, drag it to move it ("
                + (portrait() ? "portrait" : "landscape") + " layout)" : "Chosen: " + describe(selected)
                + (selected.hidden ? " (hidden)" : ""), getWidth() / 2f, y - gap * 1.5f, text);
    }

    private static String describe(Control c) {
        switch (c.id) {
            case "dpad": return "D-pad / stick";
            case "menu": return "menu button (always shown)";
            case "rotate": return "Rotate button";
            default: return c.label + " button";
        }
    }

    private boolean isHeld(Control c) {
        if (c.tool == TOOL_MIC) return micOn;
        if (c.tool == TOOL_MIC_SAMPLE) return micSampleOn;
        if (c.tool == TOOL_LID) return lidClosed;
        if (c.tool == TOOL_TOP) return topShown;
        if (c.tool == TOOL_ROTATE) return portrait();
        for (int i = 0; i < fingers.size(); i++)
            if (fingers.valueAt(i) == c) return true;
        return false;
    }

    private void body(boolean pressed, int alpha) {
        fill.setColor(pressed ? 0xFFFFFFFF : 0xFF1A1A1A);
        fill.setAlpha(pressed ? alpha * 3 / 4 : alpha * 2 / 3);
        edge.setColor(0xFFFFFFFF);
        edge.setAlpha(alpha);
        edge.setStrokeWidth(unit() * 0.006f);
        text.setColor(pressed ? 0xFF000000 : 0xFFFFFFFF);
        text.setAlpha(alpha);
    }

    private void drawRound(Canvas canvas, Control c, boolean pressed, int alpha) {
        final float x = cx(c), y = cy(c), r = radius(c);
        body(pressed, alpha);
        canvas.drawCircle(x, y, r, fill);
        canvas.drawCircle(x, y, r, edge);
        text.setTextSize(r * (c.label.length() > 2 ? 0.55f : 1.0f));
        canvas.drawText(c.label, x, y - (text.descent() + text.ascent()) / 2, text);
    }

    private void drawShoulder(Canvas canvas, Control c, boolean pressed, int alpha) {
        final float x = cx(c), y = cy(c), r = radius(c);
        body(pressed, alpha);
        final RectF box = new RectF(x - r * 1.8f, y - r, x + r * 1.8f, y + r);
        canvas.drawRoundRect(box, r, r, fill);
        canvas.drawRoundRect(box, r, r, edge);
        text.setTextSize(r * 1.0f);
        canvas.drawText(c.label, x, y - (text.descent() + text.ascent()) / 2, text);
    }

    // The stick: its round base, and the knob where the thumb holds it.
    private void drawStick(Canvas canvas, Control c, int alpha) {
        final float x = cx(c), y = cy(c), r = radius(c);
        body(false, alpha);
        canvas.drawCircle(x, y, r, fill);
        canvas.drawCircle(x, y, r, edge);
        final boolean held = stickPointer >= 0;
        body(held, alpha);
        final float kr = r * 0.42f, kx = x + knobX * (r - kr), ky = y + knobY * (r - kr);
        canvas.drawCircle(kx, ky, kr, fill);
        canvas.drawCircle(kx, ky, kr, edge);
    }

    private void stickTo(Control c, float x, float y) {
        final float r = Math.max(1f, radius(c) * 0.58f);  // (the knob's travel: base radius less the knob)
        float dx = (x - cx(c)) / r, dy = (y - cy(c)) / r;
        final float m = (float) Math.hypot(dx, dy);
        if (m > 1f) {
            dx /= m;
            dy /= m;
        }
        knobX = dx;
        knobY = dy;
        PhInput.nativeSetStick(dx, dy);
    }

    private void drawDpad(Canvas canvas, Control c, int alpha) {
        final float x = cx(c), y = cy(c), r = radius(c), w = r * 0.36f;
        int held = 0;
        for (int i = 0; i < dpadBits.size(); i++) held |= dpadBits.valueAt(i);
        // The cross.
        body(false, alpha);
        path.reset();
        path.moveTo(x - w, y - r);
        path.lineTo(x + w, y - r);
        path.lineTo(x + w, y - w);
        path.lineTo(x + r, y - w);
        path.lineTo(x + r, y + w);
        path.lineTo(x + w, y + w);
        path.lineTo(x + w, y + r);
        path.lineTo(x - w, y + r);
        path.lineTo(x - w, y + w);
        path.lineTo(x - r, y + w);
        path.lineTo(x - r, y - w);
        path.lineTo(x - w, y - w);
        path.close();
        canvas.drawPath(path, fill);
        canvas.drawPath(path, edge);
        // The arms that are held.
        fill.setColor(0xFFFFFFFF);
        fill.setAlpha(alpha * 3 / 4);
        if ((held & PhInput.UP) != 0) canvas.drawRect(x - w, y - r, x + w, y - w, fill);
        if ((held & PhInput.DOWN) != 0) canvas.drawRect(x - w, y + w, x + w, y + r, fill);
        if ((held & PhInput.LEFT) != 0) canvas.drawRect(x - r, y - w, x - w, y + w, fill);
        if ((held & PhInput.RIGHT) != 0) canvas.drawRect(x + w, y - w, x + r, y + w, fill);
    }

    // ---- touch ------------------------------------------------------------------------------------------------------
    @Override
    public boolean onTouchEvent(MotionEvent e) {
        final int action = e.getActionMasked();
        if (editing) return editTouch(e, action);
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                final int i = e.getActionIndex();
                final Control c = controlAt(e.getX(i), e.getY(i));
                // Not on a control: the touch is the game's (the stylus), so this view does not take it.
                if (c == null) return action != MotionEvent.ACTION_DOWN;
                if (hiddenForController && c.kind != KIND_TOOL) hiddenForController = false;
                press(e.getPointerId(i), c, e.getX(i), e.getY(i));
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); i++) move(e.getPointerId(i), e.getX(i), e.getY(i));
                break;
            case MotionEvent.ACTION_POINTER_UP:
                release(e.getPointerId(e.getActionIndex()));
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                for (int i = 0; i < e.getPointerCount(); i++) release(e.getPointerId(i));
                break;
            default: break;
        }
        send();
        invalidate();
        return true;
    }

    private void press(int id, Control c, float x, float y) {
        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        if (c.kind == KIND_TOOL) {
            switch (c.tool) {
                case TOOL_MENU: if (menu != null) menu.open(); return;
                case TOOL_MIC: micOn = true; fingers.put(id, c); PhInput.nativeSetMic(true); return;
                case TOOL_MIC_SAMPLE: micSampleOn = true; fingers.put(id, c); PhInput.nativeSetMicSample(true); return;
                case TOOL_LID: lidClosed = !lidClosed; PhInput.nativeSetLid(lidClosed); return;
                case TOOL_TOP: setScreen(topShown ? PhInput.SCREEN_TOUCH : PhInput.SCREEN_TOP); return;
                case TOOL_ROTATE:
                    if (activity instanceof GameActivity) ((GameActivity) activity).setPortrait(!portrait());
                    return;
                default: return;
            }
        }
        fingers.put(id, c);
        if (c.kind == KIND_DPAD && stickMode) {
            stickPointer = id;
            stickTo(c, x, y);
        } else if (c.kind == KIND_DPAD) {
            dpadBits.put(id, dpadDirection(c, x, y));
        }
    }

    private void move(int id, float x, float y) {
        final Control c = fingers.get(id);
        if (c == null) return;
        if (c.kind == KIND_DPAD && stickMode) {
            if (id == stickPointer) stickTo(c, x, y);
        } else if (c.kind == KIND_DPAD) {
            dpadBits.put(id, dpadDirection(c, x, y));
        } else if (c.kind == KIND_BUTTON) {
            // A finger sliding from one face button onto another presses the new one.
            final Control under = controlAt(x, y);
            if (under != null && under != c && under.kind == KIND_BUTTON) {
                fingers.put(id, under);
                performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            }
        }
    }

    private void release(int id) {
        final Control c = fingers.get(id);
        fingers.remove(id);
        dpadBits.remove(id);
        if (id == stickPointer) releaseStick();
        if (c != null && c.tool == TOOL_MIC) {
            micOn = false;
            PhInput.nativeSetMic(false);
        }
        if (c != null && c.tool == TOOL_MIC_SAMPLE) {
            micSampleOn = false;
            PhInput.nativeSetMicSample(false);
        }
    }

    private void releaseStick() {
        stickPointer = -1;
        knobX = knobY = 0;
        PhInput.nativeSetStick(0, 0);
    }

    private void send() {
        int bits = 0;
        for (int i = 0; i < fingers.size(); i++) bits |= fingers.valueAt(i).bits;  // (the D-pad's and stick's are 0)
        for (int i = 0; i < dpadBits.size(); i++) bits |= dpadBits.valueAt(i);
        if (bits != sentButtons) {
            sentButtons = bits;
            PhInput.nativeSetButtons(bits);
        }
    }

    // ---- the layout editor -------------------------------------------------------------------------------------------
    /** The menu's "Move and resize controls". */
    void startEditing() { setEditing(true); }

    boolean isEditing() { return editing; }

    void stopEditing() { setEditing(false); }

    private void setEditing(boolean on) {
        if (on) releaseAll();
        ensureLayout();
        editing = on;
        selected = null;
        dragPointer = -1;
        if (!on) saveLayout();
        invalidate();
    }

    // While editing, every touch is the editor's (none reaches the game).
    private boolean editTouch(MotionEvent e, int action) {
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                final int i = e.getActionIndex();
                editDown(e.getPointerId(i), e.getX(i), e.getY(i));
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); i++) editMove(e.getPointerId(i), e.getX(i), e.getY(i));
                break;
            case MotionEvent.ACTION_POINTER_UP:
                if (e.getPointerId(e.getActionIndex()) == dragPointer) dragPointer = -1;
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                dragPointer = -1;
                break;
            default: break;
        }
        invalidate();
        return true;
    }

    private void editDown(int id, float x, float y) {
        for (int i = 0; i < toolRects.length; i++)
            if (toolRects[i].contains(x, y)) {
                tool(i);
                return;
            }
        final Control c = controlAt(x, y);
        selected = c;
        if (c == null) return;
        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        dragPointer = id;
        dragDx = cx(c) - x;
        dragDy = cy(c) - y;
    }

    private void editMove(int id, float x, float y) {
        if (id != dragPointer || selected == null) return;
        final float w = getWidth(), h = getHeight();
        final float px = Math.max(0, Math.min(w, x + dragDx)), py = Math.max(0, Math.min(h, y + dragDy));
        moveTo(selected, px, py);
    }

    // Places a control at (px, py), anchored to the nearer side so the layout keeps its place from the edges on
    // phones of another shape.
    private void moveTo(Control c, float px, float py) {
        final float w = getWidth(), h = Math.max(1f, getHeight());
        if (!c.moved) c.mScale = 1f;
        c.moved = true;
        c.mSide = px < w / 2f ? LEFT : RIGHT;
        c.mx = (c.mSide == LEFT ? px : w - px) / h;
        c.my = py / h;
    }

    private void tool(int index) {
        switch (index) {
            case 0:  // Smaller
            case 1:  // Bigger
                if (selected == null) break;
                if (!selected.moved) moveTo(selected, cx(selected), cy(selected));
                selected.mScale = Math.max(0.5f, Math.min(2.5f, index == 0 ? selected.mScale / 1.12f : selected.mScale * 1.12f));
                break;
            case 2:  // Hide / show
                if (selected != null && selected.tool != TOOL_MENU) selected.hidden = !selected.hidden;
                break;
            case 3:  // Reset all (this orientation)
                for (Control c : controls) {
                    c.moved = false;
                    c.hidden = false;
                    c.mScale = 1f;
                }
                selected = null;
                break;
            case 4:  // Done
                setEditing(false);
                return;
            default: break;
        }
        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
    }

    /** Lets go of everything (the app lost focus). */
    public void releaseAll() {
        fingers.clear();
        dpadBits.clear();
        releaseStick();
        micOn = false;
        micSampleOn = false;
        PhInput.nativeSetMic(false);
        PhInput.nativeSetMicSample(false);
        send();
        invalidate();
    }

    /** A controller is in use: the touch controls step aside (the small buttons stay) until the screen is touched. */
    public void hideForController() {
        if (hiddenForController) return;
        hiddenForController = true;
        releaseAll();
    }
}
