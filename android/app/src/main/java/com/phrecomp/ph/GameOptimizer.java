package com.phrecomp.ph;

import android.app.ActivityManager;
import android.content.Context;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;

/**
 * The game build with clang: the C from the recompiler (GameBuilder.run without TinyCC) is compiled with clang -O2 on
 * the phone, several files at a time, and linked with lld into files/game/libph_game.so (OptimizeService runs it, with
 * a notification; TinyCC builds it instead when this fails). clang and lld come in the APK's native library folder, the only place
 * Android lets an app run programs from:
 * - libllvm.so: one program holding both ("libllvm.so clang ...", "libllvm.so ld.lld ...";
 *   tools/ondevice/build_device_llvm.sh), or
 * - libclang_exe.so + liblld_exe.so plus their shared libraries (a prebuilt clang, e.g. Termux's).
 * Nothing is downloaded. The C stays in files/opt (GameBuilder) until this has run.
 */
final class GameOptimizer {
    private GameOptimizer() {}

    private static final String TAG = "PHOpt";

    /** -O level: -O2 (clang's -O1 and -O2 run the game about the same, -O2 takes ~20% longer to build). */
    static final String LEVEL = "-O2";

    static File dir(Context c) { return new File(c.getFilesDir(), "opt"); }

    private static File lib(Context c, String name) { return new File(c.getApplicationInfo().nativeLibraryDir, name); }

    static boolean available(Context c) {
        return lib(c, "libllvm.so").isFile() || (lib(c, "libclang_exe.so").isFile() && lib(c, "liblld_exe.so").isFile());
    }

    /** True when the quick build left C for this (the optimised build has not run, or was cut short). */
    static boolean pending(Context c) { return available(c) && new File(dir(c), "gen").isDirectory(); }

    /** 0-100 while run() works; -1 when idle. */
    static volatile int progress = -1;

    private static List<String> clang(Context c) {
        final File llvm = lib(c, "libllvm.so");
        return llvm.isFile() ? new ArrayList<>(Arrays.asList(llvm.getPath(), "clang"))
                : new ArrayList<>(Arrays.asList(lib(c, "libclang_exe.so").getPath()));
    }

    private static List<String> lld(Context c) {
        final File llvm = lib(c, "libllvm.so");
        return llvm.isFile() ? new ArrayList<>(Arrays.asList(llvm.getPath(), "ld.lld"))
                : new ArrayList<>(Arrays.asList(lib(c, "liblld_exe.so").getPath(), "-flavor", "gnu"));
    }

    /** How many files at once: every core up to 6 (the player waits for this build), memory permitting. */
    static int jobs(Context c) {
        int n = Math.max(1, Math.min(6, Runtime.getRuntime().availableProcessors()));
        final ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
        if (am != null) {
            final ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
            am.getMemoryInfo(mi);
            // (~450 MB per compiler at most; Android frees cached apps for it, so count all memory, 1.3 GB per job)
            n = (int) Math.max(1, Math.min(n, mi.totalMem / (1300L << 20)));
        }
        return n;
    }

    /** Builds the optimised module and puts it in place of `module`. Returns null when done, else why not. */
    static String run(Context c, File module) {
        final File opt = dir(c), gen = new File(opt, "gen"), obj = new File(opt, "obj"), tccObj = new File(opt, "tcc-obj");
        // With a hot list (config/<code>/hot.txt -> hot_*.c), only the hot files and the function table are compiled again;
        // TinyCC's objects of the rest are linked in as they are. Else every file.
        final File[] hot = gen.listFiles((d, n) -> n.startsWith("hot_") && n.endsWith(".c"));
        final boolean mixed = hot != null && hot.length > 0 && tccObj.isDirectory();
        final File[] sources = gen.listFiles((d, n) -> n.endsWith(".c")
                && (!mixed || n.startsWith("hot_") || n.equals("func_table.c")));
        if (sources == null || sources.length == 0) return "No C to optimise (build the game first).";
        GameBuilder.deleteTree(obj);
        //noinspection ResultOfMethodCallIgnored
        obj.mkdirs();
        // Biggest first, so the last files to finish are small ones.
        Arrays.sort(sources, (a, b) -> Long.compare(b.length(), a.length()));
        long total = 0;
        for (File f : sources) total += f.length();
        final long all = total;
        final AtomicLong done = new AtomicLong();
        final AtomicReference<String> error = new AtomicReference<>();
        final int jobs = jobs(c);
        final long start = System.nanoTime();
        progress = 0;
        Log.i(TAG, "optimised build: " + sources.length + " files, " + (all >> 20) + " MB of C, " + jobs + " at once, "
                + LEVEL + (mixed ? " (hot functions only, the rest from TinyCC)" : ""));
        final ExecutorService pool = Executors.newFixedThreadPool(jobs);
        for (File src : sources) {
            pool.execute(() -> {
                if (error.get() != null) return;
                final String name = src.getName().replace(".c", "");
                final File o = new File(obj, name + ".o"), tmp = new File(obj, name + ".o.tmp");
                final List<String> cmd = clang(c);
                cmd.addAll(Arrays.asList("--target=aarch64-linux-android26", LEVEL, "-fPIC", "-w", "-c",
                        "-ffreestanding", "-nostdinc", "-isystem", new File(opt, "inc").getPath(),
                        "-I" + gen.getPath(), "-I" + new File(opt, "rt").getPath(), "-DNTR_GAME_MODULE=1",
                        src.getPath(), "-o", tmp.getPath()));
                final long t0 = System.nanoTime();
                final String err = exec(c, cmd);
                if (err != null || !tmp.renameTo(o)) {
                    error.compareAndSet(null, "Compiling " + src.getName() + " failed: " + err);
                    return;
                }
                Log.i(TAG, src.getName() + " " + ((System.nanoTime() - t0) / 1000000) + " ms");
                progress = (int) (95 * done.addAndGet(src.length()) / all);
            });
        }
        pool.shutdown();
        try {
            while (!pool.awaitTermination(1, TimeUnit.SECONDS)) { /* wait */ }
        } catch (InterruptedException e) {
            pool.shutdownNow();
            progress = -1;
            return "Stopped.";
        }
        if (error.get() != null) {
            progress = -1;
            return error.get();
        }
        final long compiled = System.nanoTime();
        // --threads=1: lld's parallel link can crash on Android (FF7-recomp-android saw it now and then).
        final File tmp = new File(module.getPath() + ".opt");
        final List<String> link = lld(c);
        // -Bsymbolic: calls and pointers between the game's functions bind inside the module (TinyCC's objects need it).
        link.addAll(Arrays.asList("--threads=1", "-shared", "-Bsymbolic", "-soname", "libph_game.so", "-o", tmp.getPath()));
        final File[] objects = obj.listFiles((d, n) -> n.endsWith(".o"));
        if (objects == null) return "No objects.";
        Arrays.sort(objects);
        for (File o : objects) link.add(o.getPath());
        if (mixed) {
            final File[] rest = tccObj.listFiles((d, n) -> n.endsWith(".o") && !new File(obj, n).isFile());
            if (rest == null) return "No TinyCC objects.";
            Arrays.sort(rest);
            for (File o : rest) link.add(o.getPath());
        }
        link.add("/system/lib64/libc.so");  // memcpy and friends
        final String err = exec(c, link);
        if (err != null) {
            progress = -1;
            return "Linking the optimised game failed: " + err;
        }
        if (!tmp.renameTo(module)) {
            progress = -1;
            return "Cannot store the optimised game.";
        }
        final long end = System.nanoTime();
        final String line = "clang " + LEVEL + (mixed ? " hot functions" : "") + ", " + jobs + " at once: compile " + ((compiled - start) / 1000000000)
                + " s, link " + ((end - compiled) / 1000000) + " ms, " + (module.length() >> 20) + " MB";
        Log.i(TAG, "optimised build done: " + line);
        try (FileOutputStream out = new FileOutputStream(new File(module.getParentFile(), "optimised.txt"))) {
            out.write(line.getBytes("UTF-8"));
        } catch (IOException ignored) {
            // (only shown in the Game ROM dialog)
        }
        GameBuilder.deleteTree(opt);
        progress = -1;
        return null;
    }

    /** Runs a tool from the native library folder; returns null when it exits 0, else its last output. */
    private static String exec(Context c, List<String> cmd) {
        try {
            final ProcessBuilder pb = new ProcessBuilder(cmd).redirectErrorStream(true);
            // (a prebuilt clang's shared libraries sit beside it)
            pb.environment().put("LD_LIBRARY_PATH", c.getApplicationInfo().nativeLibraryDir);
            final Process p = pb.start();
            final StringBuilder out = new StringBuilder();
            try (BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()))) {
                String l;
                while ((l = r.readLine()) != null) if (out.length() < 4000) out.append(l).append('\n');
            }
            final int rc = p.waitFor();
            return rc == 0 ? null : "exit " + rc + "\n" + out;
        } catch (IOException | InterruptedException e) {
            return e.toString();
        }
    }
}
