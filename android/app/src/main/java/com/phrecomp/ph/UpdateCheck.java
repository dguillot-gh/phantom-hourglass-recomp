package com.phrecomp.ph;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInstaller;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Updates from the project's GitHub Releases (BuildConfig.UPDATE_REPO, set by CI; empty for local builds: no check).
 * At most every few hours the launcher asks GitHub's public API for the newest release (no account, no ID sent). A
 * newer one shows an "Update" button; tapping it downloads PhantomHourglass.apk and hands it to Android's installer, which
 * asks the player. The release key (build.gradle) makes it install over this one, saves kept; the launcher then
 * rebuilds the game for the new version (LauncherActivity.moduleStale). Settings can turn the check off.
 */
final class UpdateCheck {
    private UpdateCheck() {}

    static final String PREF_ON = "update_check";
    private static final String PREF_LAST = "update_last", PREF_CODE = "update_code", PREF_TAG = "update_tag",
            PREF_URL = "update_url";
    private static final long INTERVAL_MS = 6L * 60 * 60 * 1000;
    private static final String APK_NAME = "PhantomHourglass.apk";
    private static final Pattern VERSION_CODE = Pattern.compile("versionCode:\\s*(\\d+)");

    static boolean supported() { return !BuildConfig.UPDATE_REPO.isEmpty(); }

    static boolean enabled(SharedPreferences p) { return supported() && p.getBoolean(PREF_ON, true); }

    /** The newer release's tag ("v0.2.0") when one was found, else null. */
    static String available(SharedPreferences p) {
        if (!enabled(p) || p.getLong(PREF_CODE, 0) <= BuildConfig.VERSION_CODE) return null;
        return p.getString(PREF_TAG, null);
    }

    /** Asks GitHub in the background when the last check is old enough; `found` runs (UI thread) if a newer one is out. */
    static void maybeCheck(Activity a, SharedPreferences p, Runnable found) {
        if (!enabled(p)) return;
        final long now = System.currentTimeMillis();
        if (now - p.getLong(PREF_LAST, 0) < INTERVAL_MS) return;
        p.edit().putLong(PREF_LAST, now).apply();
        new Thread(() -> {
            try {
                check(p);
                if (available(p) != null) a.runOnUiThread(found);
            } catch (Exception ignored) {
                // (offline, rate-limited, ...: try again next time)
            }
        }).start();
    }

    private static void check(SharedPreferences p) throws Exception {
        final String json = new String(get("https://api.github.com/repos/" + BuildConfig.UPDATE_REPO
                + "/releases?per_page=10"), "UTF-8");
        // Test builds (a version with "-", from a v0.2.0-test tag) also take pre-releases; real releases only take
        // real releases.
        final boolean takePre = BuildConfig.VERSION_NAME.contains("-");
        final JSONArray list = new JSONArray(json);
        for (int i = 0; i < list.length(); i++) {
            final JSONObject r = list.getJSONObject(i);
            if (r.optBoolean("draft") || (r.optBoolean("prerelease") && !takePre)) continue;
            final Matcher m = VERSION_CODE.matcher(r.optString("body"));
            String url = null;
            final JSONArray assets = r.optJSONArray("assets");
            if (assets != null)
                for (int j = 0; j < assets.length(); j++)
                    if (APK_NAME.equals(assets.getJSONObject(j).optString("name")))
                        url = assets.getJSONObject(j).optString("browser_download_url");
            if (!m.find() || url == null) continue;  // (not a release this check understands)
            p.edit().putLong(PREF_CODE, Long.parseLong(m.group(1))).putString(PREF_TAG, r.optString("tag_name"))
                    .putString(PREF_URL, url).apply();
            return;  // the newest one
        }
    }

    private static HttpURLConnection open(String url) throws IOException {
        final HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(15000);
        c.setReadTimeout(30000);
        c.setRequestProperty("User-Agent", "PhantomHourglassRecomp-updater");
        c.setRequestProperty("Accept", url.contains("api.github.com") ? "application/vnd.github+json" : "*/*");
        c.setInstanceFollowRedirects(true);  // (release files redirect to GitHub's file host, https to https)
        if (c.getResponseCode() != 200) throw new IOException("HTTP " + c.getResponseCode());
        return c;
    }

    private static byte[] get(String url) throws IOException {
        final HttpURLConnection c = open(url);
        try (InputStream in = c.getInputStream()) {
            final ByteArrayOutputStream out = new ByteArrayOutputStream();
            final byte[] buf = new byte[1 << 14];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return out.toByteArray();
        } finally {
            c.disconnect();
        }
    }

    /** The Update button: download the APK, then Android's installer asks the player. */
    static void install(Activity a, SharedPreferences p) {
        final String tag = p.getString(PREF_TAG, "");
        final String url = p.getString(PREF_URL, null);
        if (url == null) return;
        if (Build.VERSION.SDK_INT >= 26 && !a.getPackageManager().canRequestPackageInstalls()) {
            new AlertDialog.Builder(a).setTitle("Allow updates")
                    .setMessage("Android asks once: allow this app to install its own updates (Install unknown apps). "
                            + "Turn it on, come back and tap Update again.")
                    .setPositiveButton("Open settings", (d, w) -> a.startActivity(new Intent(
                            Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + a.getPackageName()))))
                    .setNegativeButton("Not now", null).show();
            return;
        }
        final AlertDialog dialog = new AlertDialog.Builder(a).setTitle("Updating to " + tag)
                .setMessage("Downloading…").setCancelable(false).show();
        new Thread(() -> {
            String error = null;
            final File apk = new File(a.getCacheDir(), "update.apk");
            try {
                final HttpURLConnection c = open(url);
                final long total = c.getContentLengthLong();
                try (InputStream in = c.getInputStream(); OutputStream out = new FileOutputStream(apk)) {
                    final byte[] buf = new byte[1 << 16];
                    long done = 0, shown = -1;
                    int n;
                    while ((n = in.read(buf)) > 0) {
                        out.write(buf, 0, n);
                        done += n;
                        final long pct = total > 0 ? done * 100 / total : -1;
                        if (pct != shown) {
                            shown = pct;
                            final String msg = pct >= 0 ? "Downloading… " + pct + "%" : "Downloading… " + (done >> 20) + " MB";
                            a.runOnUiThread(() -> dialog.setMessage(msg));
                        }
                    }
                } finally {
                    c.disconnect();
                }
                commit(a, apk);
            } catch (IOException e) {
                error = "The update did not download: " + e.getMessage();
            } finally {
                //noinspection ResultOfMethodCallIgnored
                apk.delete();
            }
            final String m = error;
            a.runOnUiThread(() -> {
                dialog.dismiss();
                if (m != null) new AlertDialog.Builder(a).setMessage(m).setPositiveButton("OK", null).show();
            });
        }).start();
    }

    /** Hands the APK to Android's installer (it shows its own Update prompt: Receiver below). */
    private static void commit(Context c, File apk) throws IOException {
        final PackageInstaller pi = c.getPackageManager().getPackageInstaller();
        final PackageInstaller.SessionParams params =
                new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(c.getPackageName());
        final int id = pi.createSession(params);
        try (PackageInstaller.Session s = pi.openSession(id)) {
            try (InputStream in = new FileInputStream(apk); OutputStream out = s.openWrite("base.apk", 0, apk.length())) {
                final byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                s.fsync(out);
            }
            final Intent done = new Intent(c, Receiver.class);
            final int flags = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
            s.commit(PendingIntent.getBroadcast(c, id, done, flags).getIntentSender());
        }
    }

    /** The installer's answer: show its confirmation screen, or say why it failed. */
    public static final class Receiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context c, Intent i) {
            final int status = i.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
            if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
                final Intent confirm = i.getParcelableExtra(Intent.EXTRA_INTENT);
                if (confirm != null) c.startActivity(confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            } else if (status != PackageInstaller.STATUS_SUCCESS) {
                android.widget.Toast.makeText(c, "Update not installed: "
                        + i.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE), android.widget.Toast.LENGTH_LONG).show();
            }
        }
    }
}
