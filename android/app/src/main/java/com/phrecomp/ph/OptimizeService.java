package com.phrecomp.ph;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

import java.io.File;

/**
 * The game build with clang (shell APK with the phone clang inside, GameOptimizer.available), as a foreground service
 * with a progress notification, so it carries on when the player leaves the app: recompile the ROM to C, compile it
 * with clang -O2, link. When clang fails, TinyCC builds the game instead (a working, slower game). The launcher shows
 * the same progress in its dialog.
 */
public class OptimizeService extends Service {
    private static final String CHANNEL = "optimise";
    private static final int ID = 7;
    private static volatile boolean running;
    /** The last result: null while none, "" when done with clang, else what happened (TinyCC fallback or error). */
    static volatile String result;
    /** How long the last build took (ms), for the launcher's message. */
    static volatile long millis;
    /** What it is doing, for the dialog and the notification. */
    static volatile String stage = "";

    static void start(Context c) {
        if (running) return;
        running = true;  // (set here so the launcher's dialog sees it at once)
        result = null;
        stage = "Recompiling your ROM…";
        final Intent i = new Intent(c, OptimizeService.class);
        if (Build.VERSION.SDK_INT >= 26) c.startForegroundService(i);
        else c.startService(i);
    }

    static boolean running() { return running; }

    /** 0-100 over the whole build. */
    static int progress() {
        final int p = GameOptimizer.progress;
        return stage.startsWith("Compiling") ? 5 + Math.max(0, p) * 95 / 100 : 2;
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }

    private Notification note(String text, boolean ongoing, int progress) {
        final Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(this, CHANNEL)
                : new Notification.Builder(this);
        b.setSmallIcon(android.R.drawable.stat_sys_download).setContentTitle("Phantom Hourglass").setContentText(text)
                .setOngoing(ongoing).setOnlyAlertOnce(true);
        if (progress >= 0) b.setProgress(100, progress, false);
        return b.build();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        final NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (Build.VERSION.SDK_INT >= 26)
            nm.createNotificationChannel(new NotificationChannel(CHANNEL, "Game build", NotificationManager.IMPORTANCE_LOW));
        final Notification first = note("Building the game…", true, 0);
        if (Build.VERSION.SDK_INT >= 29) startForeground(ID, first, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
        else startForeground(ID, first);
        if (intent == null) {  // (restarted by Android after a kill: nothing in flight)
            running = false;
            stopSelf();
            return START_NOT_STICKY;
        }
        running = true;
        final Handler ui = new Handler(Looper.getMainLooper());
        final Runnable tick = new Runnable() {
            @Override
            public void run() {
                if (!running) return;
                nm.notify(ID, note(stage + " " + progress() + "%", true, progress()));
                ui.postDelayed(this, 2000);
            }
        };
        ui.postDelayed(tick, 2000);
        new Thread(() -> {
            final long t0 = System.currentTimeMillis();
            final File rom = new File(getFilesDir(), "rom/azee.nds"), module = new File(getFilesDir(), "game/libph_game.so");
            //noinspection ResultOfMethodCallIgnored
            module.getParentFile().mkdirs();
            String outcome;
            String error = GameBuilder.run(this, rom, module, false);
            final long recompiled = System.currentTimeMillis();
            Log.i("PHOpt", "recompile " + (error == null ? "done" : "failed") + " in " + (recompiled - t0) + " ms");
            if (error == null) {
                stage = "Compiling the game with clang…";
                error = GameOptimizer.run(this, module);
            }
            if (error == null) {
                outcome = "";
            } else {
                Log.w("PHOpt", "clang build failed, TinyCC instead: " + error);
                stage = "Compiling the game (quick compiler)…";
                final String tcc = GameBuilder.run(this, rom, module, true);
                outcome = tcc == null ? "Built with the quick compiler (clang failed: " + error + ")"
                        : "The game could not be built: " + tcc;
            }
            GameBuilder.deleteTree(GameOptimizer.dir(this));
            if (module.isFile() && (outcome.isEmpty() || outcome.startsWith("Built"))) GameBuilder.stamp(this);
            millis = System.currentTimeMillis() - t0;
            Log.i("PHOpt", "game build finished in " + millis + " ms: " + (outcome.isEmpty() ? "clang" : outcome));
            final String done = outcome;
            ui.post(() -> {
                result = done;
                running = false;
                stopForeground(true);
                nm.notify(ID, note(done.isEmpty() ? "The game is ready (built in " + (millis / 1000) + " s)."
                        : done, false, -1));
                stopSelf();
            });
        }).start();
        return START_NOT_STICKY;
    }
}
