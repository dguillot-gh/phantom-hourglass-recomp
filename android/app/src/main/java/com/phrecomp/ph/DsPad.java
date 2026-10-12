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
 */
public final class DsPad extends View {
    private static final int KIND_BUTTON = 0, KIND_DPAD = 1, KIND_SHOULDER = 2, KIND_TOOL = 3;
    private static final int LEFT = -1, RIGHT = 1;
    private static final int TOOL_MENU = 1, TOOL_MIC = 2, TOOL_LID = 3, TOOL_TOP = 4, TOOL_ROTATE = 5;
    private static final float HIT_GROW = 1.25f;  // a thumb lands off-centre: the hit area is a bit larger than the drawing
    static final String PREFS = "ph_pad";

    /** One control. Groups switch on and off together (the menu's list). */
    private static final class Control {
        final int kind, bits, tool, side;
        final String label, group;
        final float x, y, r;  // x from the side's edge, y from the top, radius: all in screen heights
        Control(int kind, int bits, int tool, String label, String group, int side, float x, float y, float r) {
            this.kind = kind;
            this.bits = bits;
            this.tool = tool;
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
    private boolean micOn, lidClosed, topShown, bothShown, hiddenForController;
    private PhMenu menu;  // the ≡ button's panel (GameActivity sets it)

    public DsPad(Activity activity) {
        super(activity);
        this.activity = activity;
        prefs = activity.getSharedPreferences(PREFS, Activity.MODE_PRIVATE);
        // Left bar: the D-pad, L, Select, and the menu and Top-screen buttons above.
        controls.add(new Control(KIND_DPAD, 0, 0, "", "dpad", LEFT, 0.24f, 0.62f, 0.17f));
        controls.add(new Control(KIND_SHOULDER, PhInput.L, 0, "L", "shoulders", LEFT, 0.16f, 0.30f, 0.06f));
        controls.add(new Control(KIND_BUTTON, PhInput.SELECT, 0, "SELECT", "startselect", LEFT, 0.24f, 0.92f, 0.045f));
        controls.add(new Control(KIND_TOOL, 0, TOOL_MENU, "≡", null, LEFT, 0.07f, 0.08f, 0.05f));
        controls.add(new Control(KIND_TOOL, 0, TOOL_TOP, "TOP", "top", LEFT, 0.19f, 0.08f, 0.05f));
        controls.add(new Control(KIND_TOOL, 0, TOOL_ROTATE, "⟳", "rotate", LEFT, 0.31f, 0.08f, 0.05f));
        // Right bar: A B X Y in the DS's own places, R, Start, and the Mic and Lid buttons above.
        controls.add(new Control(KIND_BUTTON, PhInput.A, 0, "A", "face", RIGHT, 0.10f, 0.62f, 0.065f));
        controls.add(new Control(KIND_BUTTON, PhInput.B, 0, "B", "face", RIGHT, 0.24f, 0.76f, 0.065f));
        controls.add(new Control(KIND_BUTTON, PhInput.X, 0, "X", "face", RIGHT, 0.24f, 0.48f, 0.065f));
        controls.add(new Control(KIND_BUTTON, PhInput.Y, 0, "Y", "face", RIGHT, 0.38f, 0.62f, 0.065f));
        controls.add(new Control(KIND_SHOULDER, PhInput.R, 0, "R", "shoulders", RIGHT, 0.16f, 0.30f, 0.06f));
        controls.add(new Control(KIND_BUTTON, PhInput.START, 0, "START", "startselect", RIGHT, 0.24f, 0.92f, 0.045f));
        controls.add(new Control(KIND_TOOL, 0, TOOL_MIC, "MIC", "mic", RIGHT, 0.07f, 0.08f, 0.05f));
        controls.add(new Control(KIND_TOOL, 0, TOOL_LID, "LID", "lid", RIGHT, 0.19f, 0.08f, 0.05f));
        text.setTextAlign(Paint.Align.CENTER);
        text.setFakeBoldText(true);
        edge.setStyle(Paint.Style.STROKE);
        setHapticFeedbackEnabled(true);
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
        invalidate();  // (rotated: the controls move)
    }

    // ---- geometry ---------------------------------------------------------------------------------------------------
    // The controls' area on each side: in landscape the bar beside the picture (its whole height), in portrait half the
    // width under the screens. Positions are fractions of that height; sizes and distances from the edge are in a unit
    // as large as the area allows: no control may reach over the picture or another control.
    private boolean portrait() { return getHeight() > getWidth() * 1.25f; }

    // The picture, as frontend/src/layout.hpp places it: its width (landscape) or bottom edge (portrait: at the top, a
    // small margin, one or two DS screens the width of the phone).
    private float pictureWidth() {
        final float content = bothShown ? 384f : 192f;
        return 256f * Math.min(getWidth() / 256f, getHeight() / content);
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

    private float areaWidth() { return portrait() ? getWidth() / 2f : Math.max(1f, (getWidth() - pictureWidth()) / 2f); }

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

    private float radius(Control c) { return c.r * unit() * scaleOf(c); }

    private float cx(Control c) {
        final float off = c.x * unit() * scaleOf(c);
        return c.side == LEFT ? off : getWidth() - off;
    }

    private float cy(Control c) { return areaTop() + c.y * areaHeight(); }

    private boolean visible(Control c) { return groupOn(c.group) && (!hiddenForController || c.kind == KIND_TOOL); }

    private Control controlAt(float x, float y) {
        Control best = null;
        float bestD = Float.MAX_VALUE;
        for (Control c : controls) {
            if (!visible(c)) continue;
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
        final int alpha = Math.round(255 * opacity());
        for (Control c : controls) {
            if (!visible(c)) continue;
            final boolean pressed = isHeld(c);
            switch (c.kind) {
                case KIND_DPAD: drawDpad(canvas, c, alpha); break;
                case KIND_SHOULDER: drawShoulder(canvas, c, pressed, alpha); break;
                default: drawRound(canvas, c, pressed, alpha); break;
            }
        }
    }

    private boolean isHeld(Control c) {
        if (c.tool == TOOL_MIC) return micOn;
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
                case TOOL_LID: lidClosed = !lidClosed; PhInput.nativeSetLid(lidClosed); return;
                case TOOL_TOP: setScreen(topShown ? PhInput.SCREEN_TOUCH : PhInput.SCREEN_TOP); return;
                case TOOL_ROTATE:
                    if (activity instanceof GameActivity) ((GameActivity) activity).setPortrait(!portrait());
                    return;
                default: return;
            }
        }
        fingers.put(id, c);
        if (c.kind == KIND_DPAD) dpadBits.put(id, dpadDirection(c, x, y));
    }

    private void move(int id, float x, float y) {
        final Control c = fingers.get(id);
        if (c == null) return;
        if (c.kind == KIND_DPAD) {
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
        if (c != null && c.tool == TOOL_MIC) {
            micOn = false;
            PhInput.nativeSetMic(false);
        }
    }

    private void send() {
        int bits = 0;
        for (int i = 0; i < fingers.size(); i++) bits |= fingers.valueAt(i).bits;
        for (int i = 0; i < dpadBits.size(); i++) bits |= dpadBits.valueAt(i);
        if (bits != sentButtons) {
            sentButtons = bits;
            PhInput.nativeSetButtons(bits);
        }
    }

    /** Lets go of everything (the app lost focus). */
    public void releaseAll() {
        fingers.clear();
        dpadBits.clear();
        micOn = false;
        PhInput.nativeSetMic(false);
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
