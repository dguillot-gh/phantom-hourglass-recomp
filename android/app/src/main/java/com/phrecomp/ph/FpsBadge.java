package com.phrecomp.ph;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.widget.TextView;

import java.util.Locale;

/**
 * The FPS counter on the phone: a small label over the game that a finger drags anywhere. Where it was left is kept
 * (as a fraction of the screen, so it stays put across rotation). The native one (main.cpp) is the PC's.
 */
class FpsBadge extends TextView {
    private final SharedPreferences prefs;
    private float grabX, grabY;
    private boolean shown;

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            setText(String.format(Locale.US, "%.1f", PhInput.nativeGameFps()));
            if (shown) postDelayed(this, 500);
        }
    };

    FpsBadge(Context context, SharedPreferences prefs) {
        super(context);
        this.prefs = prefs;
        setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        setTextColor(Color.WHITE);
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        setGravity(Gravity.CENTER);
        final float dp = getResources().getDisplayMetrics().density;
        setPadding(Math.round(8 * dp), Math.round(3 * dp), Math.round(8 * dp), Math.round(3 * dp));
        final GradientDrawable back = new GradientDrawable();
        back.setColor(0xA0000000);
        back.setCornerRadius(6 * dp);
        setBackground(back);
        setText("00.0");
        setVisibility(GONE);
        // Its first layout (when it is shown) knows its size: to the saved place then.
        addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> {
            if (r - l != or - ol || b - t != ob - ot) place();
        });
    }

    /** Shows or hides it (the menu's FPS counter switch). */
    void setShown(boolean on) {
        shown = on;
        setVisibility(on ? VISIBLE : GONE);
        removeCallbacks(tick);
        if (on) {
            post(tick);
            post(this::place);
        }
    }

    private View parentView() { return (View) getParent(); }

    /** Back to the saved place: its centre as a fraction of the screen (the default is near the top right). The game
     *  activity calls it when the screen's size changes (rotation). */
    void place() {
        final View p = parentView();
        if (p == null || p.getWidth() == 0 || getWidth() == 0) return;
        moveTo(prefs.getFloat("fps_x", 0.9f) * p.getWidth() - getWidth() / 2f,
               prefs.getFloat("fps_y", 0.06f) * p.getHeight() - getHeight() / 2f);
    }

    // Kept whole on the screen.
    private void moveTo(float x, float y) {
        final View p = parentView();
        setX(Math.max(0, Math.min(x, p.getWidth() - getWidth())));
        setY(Math.max(0, Math.min(y, p.getHeight() - getHeight())));
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        final View p = parentView();
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                grabX = e.getRawX() - getX();
                grabY = e.getRawY() - getY();
                return true;
            case MotionEvent.ACTION_MOVE:
                moveTo(e.getRawX() - grabX, e.getRawY() - grabY);
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                if (p != null && p.getWidth() > 0) {
                    prefs.edit().putFloat("fps_x", (getX() + getWidth() / 2f) / p.getWidth())
                            .putFloat("fps_y", (getY() + getHeight() / 2f) / p.getHeight()).apply();
                }
                return true;
        }
        return true;
    }
}
