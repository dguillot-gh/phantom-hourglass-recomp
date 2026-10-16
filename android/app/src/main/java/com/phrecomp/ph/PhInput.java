package com.phrecomp.ph;

/** Calls from the on-screen pad into the game (frontend/src/android_bridge.cpp). Safe from the UI thread. */
public final class PhInput {
    private PhInput() {}

    /* DS buttons (the KEYINPUT bit order the runtime uses, X and Y after L). */
    public static final int A = 1, B = 1 << 1, SELECT = 1 << 2, START = 1 << 3, RIGHT = 1 << 4, LEFT = 1 << 5,
            UP = 1 << 6, DOWN = 1 << 7, R = 1 << 8, L = 1 << 9, X = 1 << 10, Y = 1 << 11;

    /* Screens the game can show (frontend Layout). */
    public static final int SCREEN_TOUCH = 0, SCREEN_TOP = 1, SCREEN_BOTH = 2;

    /** Every DS button held on the pad now. */
    public static native void nativeSetButtons(int held);

    /** The Mic button: while on, the microphone hears blowing. */
    public static native void nativeSetMic(boolean on);

    /** The Lid button: the DS closed (on) or open. */
    public static native void nativeSetLid(boolean closed);

    /** Which screen(s) to show: SCREEN_TOUCH, SCREEN_TOP or SCREEN_BOTH. */
    public static native void nativeShowScreen(int screen);

    /** The picture filter: smooth (on) or sharp pixels (off). */
    public static native void nativeSetSmooth(boolean on);

    /** The FPS counter in the corner of the picture. */
    public static native void nativeSetFps(boolean on);

    /** The game's frames per second over the last second. */
    public static native float nativeGameFps();

    /** Where the FPS counter sits on the picture: 0 top left, 1 top right, 2 bottom left, 3 bottom right. */
    public static native void nativeSetFpsCorner(int corner);

    /** The picture stretched over the whole screen (on), or at the DS's shape with bars beside it (off). */
    public static native void nativeSetStretch(boolean on);

    /** Save states: save to slot 1-12 (at the next moment the game allows, within a frame or two), or load one. The
     *  files are files/states/slot-NN.state, .png (both screens) and .txt (when). */
    public static native void nativeSaveState(int slot);
    public static native void nativeLoadState(int slot);

    /** The renderer: the GPU one (OpenGL ES) at `scale` (2, 4, 6 or 8) times the DS's resolution, or the software one. */
    public static native void nativeSetVideo(boolean gpu, int scale);

    /** The GPU renderer could not start on this phone (the game is on the software one). */
    public static native boolean nativeGpuFailed();

    /** 3D fixes, bits: 1 fewer seams between polygons (GPU renderer), 2 stable textures (no smearing as the view moves). */
    public static native void nativeSetFixes(int bits);

    /** The 3D's textures with the GPU renderer: 0 the DS's pixels, 1 smooth, 2 sharp (crisp, soft only between texels). */
    public static native void nativeSetTextures(int mode);

    /** Widescreen 16:9 (a test; the GPU renderer only): the 3D drawn wider, the HUD in the middle 4:3. */
    public static native void nativeSetWide(boolean on);

    /** The on-screen analog stick: x, y in -1..1 (y down), 0, 0 at rest. */
    public static native void nativeSetStick(float x, float y);

    /** The analog stick moves Link (on: the on-screen stick and a controller's left stick), or the D-pad does. */
    public static native void nativeSetStickMode(boolean on);

    /** Continue: write the resume state (files/states/resume.state, .png, .txt) now, before quitting. */
    public static native void nativeRequestResume();

    /** The resume state asked for is written (or the game gave no moment for it). */
    public static native boolean nativeResumeDone();
}
