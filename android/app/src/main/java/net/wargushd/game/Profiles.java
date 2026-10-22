package net.wargushd.game;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.List;

/**
 * Player profiles.
 *
 * A profile is a name, a portrait and a colour, plus everything the engine keeps for a
 * player: campaign progress, saved games and preferences live in the profile's own
 * directory, which the engine is started with as its user directory. The list of profiles
 * and the active one are kept in the launcher's preferences.
 */
final class Profiles {
    /** The eight player colours, first shade of each ramp, in the game's order. */
    static final int[] COLORS = {
        0xFFA40000, 0xFF0C48CC, 0xFF2CB494, 0xFF9848B0,
        0xFFF88C14, 0xFF28283C, 0xFFE0E0E0, 0xFFFCFC48,
    };

    /** Longest name the engine can carry. */
    static final int NAME_MAX = 15;

    private static final String PREFS = "launcher";
    private static final String KEY_LIST = "profiles";
    private static final String KEY_ACTIVE = "activeProfile";

    /** One profile. */
    static final class Profile {
        String id;
        String name;
        /** Slot in the portrait atlas; for a random portrait, the one drawn in this run. */
        int portrait;
        /** The gallery's Random (design 22.2, adopted Q22 "random each start"): a new portrait
         *  is drawn once per launcher process, as the owner's other launcher does. */
        boolean portraitRandom;
        /** 0..7, index into COLORS. */
        int color;
        long created;
        long lastUsed;
        /** Lifetime numbers; games that reached an outcome. */
        int games;
        int victories;
        int defeats;
        /** Sums over those games of the profile's own numbers, as the game reported them. */
        long units, buildings, kills, razings, score;
        /** The highest rank a game earned, 1..19 (0 = none yet), and whether it was an Orc game. */
        int bestRank;
        boolean bestRankOrc;

        JSONObject toJson() throws Exception {
            return new JSONObject().put("id", id).put("name", name).put("portrait", portrait)
                .put("color", color).put("created", created).put("lastUsed", lastUsed)
                .put("games", games).put("victories", victories).put("defeats", defeats)
                .put("units", units).put("buildings", buildings).put("kills", kills)
                .put("razings", razings).put("score", score)
                .put("bestRank", bestRank).put("bestRankOrc", bestRankOrc)
                .put("portraitMode", portraitRandom ? "random" : "fixed");
        }

        static Profile fromJson(JSONObject o) {
            final Profile p = new Profile();
            p.id = o.optString("id", "");
            p.name = o.optString("name", "");
            p.portrait = o.optInt("portrait", 0);
            p.color = Math.max(0, Math.min(COLORS.length - 1, o.optInt("color", 0)));
            p.created = o.optLong("created", 0L);
            p.lastUsed = o.optLong("lastUsed", 0L);
            p.games = o.optInt("games", 0);
            p.victories = o.optInt("victories", 0);
            p.defeats = o.optInt("defeats", 0);
            p.units = o.optLong("units", 0L);
            p.buildings = o.optLong("buildings", 0L);
            p.kills = o.optLong("kills", 0L);
            p.razings = o.optLong("razings", 0L);
            p.score = o.optLong("score", 0L);
            p.bestRank = o.optInt("bestRank", 0);
            p.bestRankOrc = o.optBoolean("bestRankOrc", false);
            p.portraitRandom = "random".equals(o.optString("portraitMode", "fixed"));
            if (p.portraitRandom) {
                p.portrait = rolled(p.id);
            }
            return p;
        }
    }

    /** This process's draw of each random portrait, by profile id. */
    private static final java.util.HashMap<String, Integer> ROLLED = new java.util.HashMap<String, Integer>();

    /** The portrait drawn for a random profile in this process; drawn at the first ask. */
    private static synchronized int rolled(String id) {
        Integer slot = ROLLED.get(id);
        if (slot == null) {
            slot = GallerySheet.SLOTS[new java.util.Random().nextInt(GallerySheet.SLOTS.length)];
            ROLLED.put(id, slot);
        }
        return slot;
    }

    /**
     * The gallery's answer: a fixed portrait forgets this run's draw; a random one keeps the
     * portrait shown at OK as this run's draw, so nothing changes until the next start.
     */
    static synchronized void chose(Profile p, int slot, boolean random) {
        p.portrait = slot;
        p.portraitRandom = random;
        if (random) {
            ROLLED.put(p.id, slot);
        } else {
            ROLLED.remove(p.id);
        }
    }

    /** The key of a rank's name in the game's strings: 19 per race, Human odd, Orc even. */
    static String rankKey(int index, boolean orc) {
        return "rank_txt_" + (2 * index - 1 + (orc ? 1 : 0));
    }

    private Profiles() {}

    private static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    /** The profile in use, or null when none exists yet. */
    static Profile active(Context c) {
        final String id = prefs(c).getString(KEY_ACTIVE, null);
        if (id == null) {
            return null;
        }
        try {
            final JSONArray list = new JSONArray(prefs(c).getString(KEY_LIST, "[]"));
            for (int i = 0; i < list.length(); ++i) {
                final Profile p = Profile.fromJson(list.getJSONObject(i));
                if (id.equals(p.id)) {
                    return p;
                }
            }
        } catch (Exception e) {
            // An unreadable list is treated as no profile.
        }
        return null;
    }

    /** The directory the engine uses for this profile. */
    static File directory(Context c, Profile p) {
        return GameLaunch.profileDir(c, p.id);
    }

    /**
     * Create a profile, make it the active one, and return it. The very first profile takes
     * over what the game has stored so far, so nobody's progress is lost by the change to
     * profiles; the old files are copied, not moved.
     */
    static Profile create(Context c, String name, int portrait, int color) {
        final Profile p = new Profile();
        p.id = newId();
        p.name = clean(name);
        p.portrait = portrait;
        p.color = Math.max(0, Math.min(COLORS.length - 1, color));
        p.created = System.currentTimeMillis();
        p.lastUsed = p.created;
        try {
            final JSONArray list = new JSONArray(prefs(c).getString(KEY_LIST, "[]"));
            final boolean first = list.length() == 0;
            list.put(p.toJson());
            if (first) {
                adoptExistingData(c, p);
            }
            prefs(c).edit().putString(KEY_LIST, list.toString()).putString(KEY_ACTIVE, p.id).apply();
        } catch (Exception e) {
            return null;
        }
        return p;
    }

    /** Every profile, in the order they were made. */
    static List<Profile> list(Context c) {
        final List<Profile> out = new ArrayList<>();
        try {
            final JSONArray list = new JSONArray(prefs(c).getString(KEY_LIST, "[]"));
            for (int i = 0; i < list.length(); ++i) {
                out.add(Profile.fromJson(list.getJSONObject(i)));
            }
        } catch (Exception e) {
            // An unreadable list is treated as empty.
        }
        return out;
    }

    /** Make a profile the one in use. */
    static void setActive(Context c, Profile p) {
        p.lastUsed = System.currentTimeMillis();
        update(c, p);
        prefs(c).edit().putString(KEY_ACTIVE, p.id).apply();
    }

    /**
     * Delete a profile that is not in use: its entry and its directory with everything the
     * game stored there (progress, saved games, preferences).
     */
    static boolean delete(Context c, Profile p) {
        if (p.id.equals(prefs(c).getString(KEY_ACTIVE, null))) {
            return false;
        }
        try {
            final JSONArray list = new JSONArray(prefs(c).getString(KEY_LIST, "[]"));
            final JSONArray kept = new JSONArray();
            for (int i = 0; i < list.length(); ++i) {
                if (!p.id.equals(list.getJSONObject(i).optString("id"))) {
                    kept.put(list.getJSONObject(i));
                }
            }
            prefs(c).edit().putString(KEY_LIST, kept.toString()).apply();
        } catch (Exception e) {
            return false;
        }
        remove(directory(c, p));
        return true;
    }

    private static void remove(File f) {
        final File[] inside = f.listFiles();
        if (inside != null) {
            for (File child : inside) {
                remove(child);
            }
        }
        f.delete();
    }

    /** How many profiles exist. */
    static int count(Context c) {
        try {
            return new JSONArray(prefs(c).getString(KEY_LIST, "[]")).length();
        } catch (Exception e) {
            return 0;
        }
    }

    /** Store a profile whose fields changed. */
    static void update(Context c, Profile p) {
        try {
            final JSONArray list = new JSONArray(prefs(c).getString(KEY_LIST, "[]"));
            for (int i = 0; i < list.length(); ++i) {
                if (p.id.equals(list.getJSONObject(i).optString("id"))) {
                    list.put(i, p.toJson());
                }
            }
            prefs(c).edit().putString(KEY_LIST, list.toString()).apply();
        } catch (Exception e) {
            // Keeps the previous list.
        }
    }

    /** A name the engine can carry: trimmed, single-spaced, no control characters, 1..15 long. */
    static String clean(String name) {
        final StringBuilder s = new StringBuilder();
        if (name != null) {
            for (int i = 0; i < name.length() && s.length() < NAME_MAX; ++i) {
                final char ch = name.charAt(i);
                if (ch < 0x20 || ch == 0x7F) {
                    continue;
                }
                if (ch == ' ' && (s.length() == 0 || s.charAt(s.length() - 1) == ' ')) {
                    continue;
                }
                s.append(ch);
            }
        }
        final String out = s.toString().trim();
        return out.isEmpty() ? "Player" : out;
    }

    private static String newId() {
        final byte[] raw = new byte[4];
        new SecureRandom().nextBytes(raw);
        final StringBuilder s = new StringBuilder("p");
        for (byte b : raw) {
            s.append(String.format("%02x", b));
        }
        return s.toString();
    }

    /** Copy progress, preferences and saved games of the pre-profile store into a profile. */
    private static void adoptExistingData(Context c, Profile p) {
        final File from = new File(c.getFilesDir(), "stratagus/wc2");
        final File to = new File(directory(c, p), "wc2");
        if (!from.isDirectory() || to.exists()) {
            return;
        }
        copy(new File(from, "GlobalSave.json"), new File(to, "GlobalSave.json"));
        copy(new File(from, "preferences.lua"), new File(to, "preferences.lua"));
        final File saves = new File(from, "save");
        final String[] names = saves.list();
        if (names != null) {
            for (String name : names) {
                copy(new File(saves, name), new File(new File(to, "save"), name));
            }
        }
    }

    private static void copy(File from, File to) {
        if (!from.isFile()) {
            return;
        }
        final File dir = to.getParentFile();
        if (dir != null && !dir.isDirectory() && !dir.mkdirs()) {
            return;
        }
        final byte[] buf = new byte[65536];
        try (FileInputStream in = new FileInputStream(from);
             FileOutputStream out = new FileOutputStream(to)) {
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
        } catch (Exception e) {
            to.delete();
        }
    }
}
