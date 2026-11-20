package com.phrecomp.ph;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/** Shared native Android UI for browsing and resetting local Phantom Hourglass achievements (set #14323). */
final class PhAchievementsUi {
    private static final int GOLD = 0xFFF5C542;
    private static final int GOLD_BG = 0xFF2D2514;
    private static final int PANEL = 0xFF181820;
    private static final int ROW = 0xFF23232D;
    private static final int HINT = 0xFF9898A8;
    private static final int ACCENT = 0xFF8AB4FF;

    static final class Entry {
        final int number;
        final String id;
        final String title;
        final String desc;
        final int points;
        final String category;
        boolean unlocked;
        int cur;
        int max;

        Entry(int number, String id, String title, String desc, int points, String category, boolean unlocked, int cur, int max) {
            this.number = number;
            this.id = id;
            this.title = title;
            this.desc = desc;
            this.points = points;
            this.category = category;
            this.unlocked = unlocked;
            this.cur = cur;
            this.max = max;
        }
    }

    static final class Snapshot {
        final List<Entry> items = new ArrayList<>();
        int unlockedCount;
        int totalCount;
        int unlockedPoints;
        int totalPoints;
    }

    // Minimal catalog for LauncherActivity (when libmain.so is not loaded yet).
    private static final Object[][] CATALOG = {
        {1, "permission_shmermission", "Permission Shmermission", "Learn swordplay from Oshus after raiding his storage.", 1, "Story", 0},
        {2, "treasure_hunter", "Treasure Hunter", "Obtain six types of treasure.", 5, "Collectibles", 6},
        {3, "seeker_of_power", "Seeker of Power", "Collect 10 Power Gems.", 5, "Collectibles", 10},
        {4, "omnipotence", "Omnipotence", "Collect all 20 Power Gems.", 10, "Collectibles", 20},
        {5, "seeker_of_wisdom", "Seeker of Wisdom", "Collect 10 Wisdom Gems.", 5, "Collectibles", 10},
        {6, "omniscience", "Omniscience", "Collect all 20 Wisdom Gems.", 10, "Collectibles", 20},
        {7, "seeker_of_courage", "Seeker of Courage", "Collect 10 Courage Gems.", 5, "Collectibles", 10},
        {8, "omnivalor", "Omnivalor", "Collect all 20 Courage Gems.", 10, "Collectibles", 20},
        {9, "africus", "Africus", "Acquire the Southwestern Sea chart.", 3, "Story", 0},
        {10, "corus", "Corus", "Acquire the Northwestern Sea chart.", 3, "Story", 0},
        {11, "vulturnus", "Vulturnus", "Acquire the Southeastern Sea chart.", 3, "Story", 0},
        {12, "caecias", "Caecias", "Acquire the Northeastern Sea chart.", 3, "Story", 0},
        {13, "scorching_earth", "Scorching Earth", "Defeat Blaaz without healing.", 5, "Bosses", 0},
        {14, "hidden_in_flame", "Hidden in Flame", "Free the Spirit of Power.", 5, "Story", 0},
        {15, "a_vision_of_darkness", "A Vision of Darkness", "Rescue Astrid, the fortuneteller on the Isle of Ember.", 1, "Story", 0},
        {16, "back_at_ya", "Back at Ya!", "Acquire the boomerang.", 3, "Items", 0},
        {17, "tempus_fugit", "Tempus Fugit", "Acquire the Phantom Hourglass.", 5, "Story", 0},
        {18, "open_fire", "Open Fire!", "Buy Eddo's cannon.", 3, "Items", 0},
        {19, "dig_to_the_center", "Dig to the Center of the Earth!", "Obtain the shovel.", 3, "Items", 0},
        {20, "where_the_wind_blows", "Where the Wind Blows", "Reach the Isle of Gust for the first time.", 1, "Story", 0},
        {21, "and_kablooey", "And Kablooey!", "Obtain bombs.", 3, "Items", 0},
        {22, "a_short_fuse", "A Short Fuse", "Defeat Cyclok within 45 seconds.", 10, "Bosses", 0},
        {23, "hidden_in_wind", "Hidden in Wind", "Free the Spirit of Wisdom.", 5, "Story", 0},
        {24, "the_sands_of_time", "The Sands of Time", "Fill the Phantom Hourglass with 25 minutes' worth of Sand of Hours.", 10, "Collectibles", 25},
        {25, "mental_reflection", "Mental Reflection", "Solve the crest puzzle in the second section of the Temple of the Ocean King.", 3, "Story", 0},
        {26, "an_arm_and_a_leg", "An Arm and a Leg", "Buy the Salvage Arm.", 1, "Items", 0},
        {27, "a_bowyers_pride", "A Bowyer's Pride", "Obtain the bow.", 3, "Items", 0},
        {28, "minimalist", "Minimalist", "Defeat Crayk using at most one arrow.", 10, "Bosses", 0},
        {29, "hidden_in_oblivion", "Hidden in Oblivion", "Free the Spirit of Courage.", 5, "Story", 0},
        {30, "quadruple_break_up", "Quadruple Break Up", "Win at Dead Man's Volley and earn the Ghost Key using thirty-six sword swings or fewer.", 25, "Bosses", 0},
        {31, "petraphobia", "Petraphobia", "Find Tetra.", 5, "Story", 0},
        {32, "two_in_a_row", "Two in a Row", "Obtain the Northeastern Sea chart while having obtained neither the Bombchu bag nor the grappling hook.", 5, "Endgame", 0},
        {33, "precision_demolition", "Precision Demolition", "Obtain the Bombchu bag.", 3, "Items", 0},
        {34, "seamless_relay", "Seamless Relay", "Complete Dongorongo's first phase without switching between Link and Gongoron more than eight times.", 5, "Bosses", 0},
        {35, "why_dont_ya_just_shoot_him", "Why Don't Ya Just Shoot Him?", "Complete Dongorongo's second phase without using your sword on it.", 10, "Bosses", 0},
        {36, "sanguine_ore", "Sanguine Ore", "Obtain Crimsonine.", 5, "Story", 0},
        {37, "the_hooks", "The Hooks!", "Obtain the grappling hook.", 3, "Items", 0},
        {38, "one_head_is_better_than_two", "One Head Is Better than Two", "Defeat Gleeok and take its Sand of Hours without taking damage.", 5, "Bosses", 0},
        {39, "lacrimal_ore", "Lacrimal Ore", "Obtain Azurine.", 5, "Story", 0},
        {40, "rookie_cartographer", "Rookie Cartographer", "Collect three treasure maps.", 3, "Collectibles", 3},
        {41, "regular_cartographer", "Regular Cartographer", "Collect seven treasure maps.", 5, "Collectibles", 7},
        {42, "veteran_cartographer", "Veteran Cartographer", "Collect fifteen treasure maps.", 10, "Collectibles", 15},
        {43, "legendary_cartographer", "Legendary Cartographer", "Collect all thirty-one treasure maps.", 25, "Collectibles", 31},
        {44, "stop", "Stop!", "Obtain the hammer.", 3, "Items", 0},
        {45, "hammer_time", "Hammer Time!", "Defeat Eox within 2:30.", 5, "Bosses", 0},
        {46, "sudorific_ore", "Sudorific Ore", "Obtain Aquanine.", 5, "Story", 0},
        {47, "be_the_crane", "Be the Crane", "Salvage treasure without damaging the Salvage Arm.", 5, "Minigames", 0},
        {48, "world_explorer", "World Explorer", "Discover all uncharted islands.", 10, "Collectibles", 4},
        {49, "rollin_rollin_rollin", "Rollin', Rollin', Rollin'", "Finish the Goron Game in thirty seconds or less.", 25, "Minigames", 0},
        {50, "armed_to_the_teeth", "Armed to the Teeth", "Get all bomb, Bombchu, and quiver upgrades.", 10, "Collectibles", 6},
        {51, "heartthrob", "Heartthrob", "Collect all thirteen Heart Containers.", 10, "Collectibles", 13},
        {52, "second_intention", "Second Intention", "Score a total of 90 hits in a row during training without getting hit yourself.", 5, "Minigames", 0},
        {53, "action_port", "Action Port", "Get a score of 2640 in the cannon minigame.", 10, "Minigames", 0},
        {54, "renowned_angler", "Renowned Angler", "Catch all five major species of fish.", 10, "Collectibles", 5},
        {55, "gale_treader", "Gale Treader", "Find all six Golden Frogs.", 10, "Collectibles", 6},
        {56, "double_hero", "Double Hero", "Score more than 200 hits during training.", 3, "Minigames", 0},
        {57, "sharpshooter", "Sharpshooter", "Get a combo of 25 or more at the Shooting Range.", 5, "Minigames", 0},
        {58, "thorough_exorcism", "Thorough Exorcism", "Get a score of 2000 or more at the Shooting Range.", 10, "Minigames", 0},
        {59, "ars_bellica", "Ars Bellica", "Obtain the Swordsman's Scroll.", 10, "Items", 0},
        {60, "the_phantombuster", "The Phantombuster", "Forge the Phantom Sword.", 5, "Story", 0},
        {61, "the_virtue_of_balance", "The Virtue of Balance", "Upgrade all spirits' abilities to Level 2.", 25, "Collectibles", 3},
        {62, "pristine_path", "Pristine Path", "Complete the Temple of the Ocean King with 25:00 remaining in the Phantom Hourglass.", 25, "Endgame", 0},
        {63, "eroding_will", "Eroding Will", "Complete the Temple of the Ocean King from beginning to end within 5:30 without replenishing sand.", 25, "Endgame", 0},
        {64, "leech_killer", "Leech Killer", "Defeat Bellum's first phase without your life energy dipping three hearts below its maximum.", 5, "Endgame", 0},
        {65, "the_green_baron", "The Green Baron", "Defeat Bellum's second phase without your ship being damaged more than six times.", 10, "Endgame", 0},
        {66, "defender_of_the_seas", "Defender of the Seas", "Defeat Bellum's final phase while taking less than three hearts of damage and without any spirit abilities equipped.", 25, "Endgame", 0},
        {67, "a_wish_for_freedom", "A Wish for Freedom", "Save the World of the Ocean King from Bellum's influence.", 25, "Endgame", 0},
    };

    static Snapshot fromNative() {
        final String raw = PhInput.nativeAchievementsData();
        if (raw == null || raw.isEmpty()) return null;
        final Snapshot snap = new Snapshot();
        final String[] lines = raw.split("\n");
        if (lines.length == 0) return null;
        final String[] hdr = lines[0].split("\t");
        if (hdr.length >= 4) {
            snap.unlockedCount = parseInt(hdr[0]);
            snap.totalCount = parseInt(hdr[1]);
            snap.unlockedPoints = parseInt(hdr[2]);
            snap.totalPoints = parseInt(hdr[3]);
        }
        for (int i = 1; i < lines.length; i++) {
            final String[] p = lines[i].split("\t", -1);
            if (p.length < 9) continue;
            snap.items.add(new Entry(parseInt(p[0]), p[1], p[2], p[3], parseInt(p[4]), p[5],
                    "1".equals(p[6]), parseInt(p[7]), parseInt(p[8])));
        }
        return snap;
    }

    static Snapshot fromFile(File file) {
        final Set<String> unlocked = new HashSet<>();
        if (file != null && file.isFile()) {
            try (BufferedReader r = new BufferedReader(new FileReader(file))) {
                String line;
                while ((line = r.readLine()) != null) {
                    line = line.trim();
                    if (line.isEmpty() || line.startsWith("#")) continue;
                    final int eq = line.indexOf('=');
                    if (eq > 0) unlocked.add(line.substring(0, eq).trim());
                }
            } catch (Exception ignored) {
            }
        }
        final Snapshot snap = new Snapshot();
        snap.totalCount = CATALOG.length;
        for (Object[] row : CATALOG) {
            final String id = (String) row[1];
            final int pts = (Integer) row[4];
            final int max = (Integer) row[6];
            final boolean u = unlocked.contains(id);
            snap.totalPoints += pts;
            if (u) {
                snap.unlockedCount++;
                snap.unlockedPoints += pts;
            }
            snap.items.add(new Entry((Integer) row[0], id, (String) row[2], (String) row[3], pts,
                    (String) row[5], u, u ? max : 0, max));
        }
        return snap;
    }

    static String summaryText(File fallbackFile, boolean useNative) {
        final Snapshot s = useNative ? fromNative() : fromFile(fallbackFile);
        final Snapshot snap = s != null ? s : fromFile(fallbackFile);
        return String.format(Locale.US, "%d / %d  ·  %d / %d pts",
                snap.unlockedCount, snap.totalCount, snap.unlockedPoints, snap.totalPoints);
    }

    static void showDialog(Activity activity, File fallbackFile, boolean useNative, Runnable onChanged) {
        final float dp = activity.getResources().getDisplayMetrics().density;
        final int[] filter = {0};
        final Snapshot[] snapRef = {useNative ? fromNative() : fromFile(fallbackFile)};
        if (snapRef[0] == null) snapRef[0] = fromFile(fallbackFile);

        final LinearLayout container = new LinearLayout(activity);
        container.setOrientation(LinearLayout.VERTICAL);
        container.setBackgroundColor(PANEL);
        final int pad = px(dp, 14);
        container.setPadding(pad, pad, pad, pad);

        final TextView summary = makeText(activity, "", 15, GOLD, true);
        container.addView(summary);

        final LinearLayout filterBar = new LinearLayout(activity);
        filterBar.setPadding(0, px(dp, 8), 0, px(dp, 8));
        container.addView(filterBar);

        final ScrollView scroll = new ScrollView(activity);
        final LinearLayout list = new LinearLayout(activity);
        list.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(list);
        container.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, px(dp, 340)));

        final Runnable[] rebuild = new Runnable[1];
        rebuild[0] = () -> {
            final Snapshot s = snapRef[0];
            final int pct = s.totalCount > 0 ? (s.unlockedCount * 100 / s.totalCount) : 0;
            summary.setText(String.format(Locale.US, "%d / %d unlocked  ·  %d / %d pts (%d%%)",
                    s.unlockedCount, s.totalCount, s.unlockedPoints, s.totalPoints, pct));

            filterBar.removeAllViews();
            final String[] labels = {
                "All (" + s.totalCount + ")",
                "Unlocked (" + s.unlockedCount + ")",
                "Locked (" + Math.max(0, s.totalCount - s.unlockedCount) + ")",
            };
            for (int i = 0; i < 3; i++) {
                final int idx = i;
                final boolean sel = filter[0] == i;
                final TextView chip = makeText(activity, labels[i], 13, sel ? Color.BLACK : Color.WHITE, true);
                chip.setGravity(Gravity.CENTER);
                chip.setPadding(px(dp, 8), px(dp, 6), px(dp, 8), px(dp, 6));
                chip.setBackground(rounded(dp, sel ? GOLD : ROW));
                chip.setOnClickListener(v -> {
                    filter[0] = idx;
                    rebuild[0].run();
                });
                final LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1);
                if (i > 0) lp.leftMargin = px(dp, 6);
                filterBar.addView(chip, lp);
            }

            list.removeAllViews();
            int shown = 0;
            for (Entry e : s.items) {
                if (filter[0] == 1 && !e.unlocked) continue;
                if (filter[0] == 2 && e.unlocked) continue;
                shown++;
                final LinearLayout card = new LinearLayout(activity);
                card.setOrientation(LinearLayout.VERTICAL);
                card.setPadding(px(dp, 10), px(dp, 8), px(dp, 10), px(dp, 8));
                card.setBackground(rounded(dp, e.unlocked ? GOLD_BG : ROW));

                final LinearLayout top = new LinearLayout(activity);
                top.setGravity(Gravity.CENTER_VERTICAL);
                final TextView title = makeText(activity, (e.unlocked ? "★ " : "☆ ") + e.title, 14,
                        e.unlocked ? GOLD : Color.WHITE, true);
                top.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
                final TextView pts = makeText(activity, "+" + e.points + " pts", 12,
                        e.unlocked ? GOLD : HINT, true);
                top.addView(pts);
                card.addView(top);

                String descText = e.desc;
                if (e.max > 0) descText += "  (" + e.cur + "/" + e.max + ")";
                final TextView desc = makeText(activity, descText, 12, e.unlocked ? 0xFFE6E6F0 : HINT, false);
                desc.setPadding(0, px(dp, 2), 0, 0);
                card.addView(desc);

                final LinearLayout.LayoutParams clp = new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
                clp.topMargin = px(dp, 6);
                list.addView(card, clp);
            }
            if (shown == 0) {
                final TextView empty = makeText(activity, filter[0] == 1
                        ? "No achievements unlocked yet." : "All achievements unlocked!", 14, HINT, false);
                empty.setPadding(px(dp, 6), px(dp, 12), 0, 0);
                list.addView(empty);
            }
        };
        rebuild[0].run();

        final AlertDialog.Builder b = new AlertDialog.Builder(activity)
                .setTitle("Achievements")
                .setView(container)
                .setPositiveButton("Close", null);
        if (snapRef[0].unlockedCount > 0) {
            b.setNeutralButton("Reset all", (d, w) -> new AlertDialog.Builder(activity)
                    .setTitle("Reset achievements?")
                    .setMessage("This clears all local unlocked achievements. Your game save is untouched.")
                    .setPositiveButton("Reset", (d2, w2) -> {
                        if (useNative) PhInput.nativeResetAchievements();
                        else if (fallbackFile != null && fallbackFile.isFile()) {
                            //noinspection ResultOfMethodCallIgnored
                            fallbackFile.delete();
                        }
                        if (onChanged != null) onChanged.run();
                    })
                    .setNegativeButton("Cancel", null)
                    .show());
        }
        b.show();
    }

    private static int parseInt(String s) {
        try { return Integer.parseInt(s.trim()); } catch (Exception e) { return 0; }
    }

    private static int px(float dp, float v) { return (int) (v * dp + 0.5f); }

    private static TextView makeText(Activity a, String s, float sp, int color, boolean bold) {
        final TextView t = new TextView(a);
        t.setText(s);
        t.setTextColor(color);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private static GradientDrawable rounded(float dp, int color) {
        final GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(px(dp, 8));
        return d;
    }
}
