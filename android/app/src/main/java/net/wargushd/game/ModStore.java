package net.wargushd.game;

import android.content.Context;
import android.net.Uri;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

/**
 * The mods on this device: the packages in the app's own mods directory, and which of them
 * the player has switched on. A package is a directory named by the mod's id with a mod.json
 * in it. The game is told the enabled ones in the request of each start and never reads this
 * record itself; with none enabled a request says nothing about mods.
 */
final class ModStore {
    /** One installed package, as its mod.json describes it. */
    static final class Mod {
        String id, name, author, description, version;
        /** The package is one this version can load: presentation only, known capabilities. */
        boolean usable;
        boolean gameplay;
        /** The request modes the package is made for. */
        boolean inCampaign, inCustom;
    }

    /** What a package may need of this version. */
    private static final List<String> CAPABILITIES = Arrays.asList("mod-path", "voice-override");

    private ModStore() {}

    /** Where the packages are: beside the game's data, never inside it. */
    static File directory(Context c) {
        return new File(c.getFilesDir(), "mods");
    }

    private static File record(Context c) {
        return new File(c.getFilesDir(), "mods.json");
    }

    private static boolean isId(String id) {
        return ModPackage.isId(id);
    }

    /** The installed packages in name order. Reads files: not for the main thread. */
    static List<Mod> installed(Context c) {
        final List<Mod> mods = new ArrayList<Mod>();
        final File[] dirs = directory(c).listFiles();
        if (dirs == null) {
            return mods;
        }
        for (File dir : dirs) {
            final Mod mod = dir.isDirectory() ? describe(dir, dir.getName()) : null;
            if (mod != null) {
                mods.add(mod);
            }
        }
        Collections.sort(mods, new Comparator<Mod>() {
            @Override
            public int compare(Mod a, Mod b) {
                return a.name.compareToIgnoreCase(b.name);
            }
        });
        return mods;
    }

    /** The package in a directory that has, or will get, an id for its name; null when it is not one. */
    private static Mod describe(File dir, String id) {
        final String text = GameLaunch.readText(new File(dir, "mod.json"));
        if (text == null || !isId(id)) {
            return null;
        }
        try {
            final JSONObject json = new JSONObject(text);
            final Mod mod = new Mod();
            mod.id = id;
            mod.name = json.optString("name", mod.id);
            mod.author = json.optString("author", "");
            mod.description = json.optString("description", "");
            mod.version = json.optString("version", "");
            mod.gameplay = json.optBoolean("gameplay", true);
            final JSONObject scope = json.optJSONObject("scope");
            mod.inCampaign = scope != null && scope.optBoolean("campaign", false);
            mod.inCustom = scope == null || scope.optBoolean("custom", true);
            boolean known = true;
            final JSONArray requires = json.optJSONArray("requires");
            for (int i = 0; requires != null && i < requires.length(); ++i) {
                known &= CAPABILITIES.contains(requires.optString(i));
            }
            mod.usable = known && !mod.gameplay && json.optInt("format", 0) == 1
                && mod.id.equals(json.optString("id"))
                && !json.optBoolean("contains_game_derived", true)
                && mod.version.matches("[0-9A-Za-z.+-]{1,32}");
            return mod;
        } catch (Exception e) {
            return null;
        }
    }

    /** The ids switched on, in the order they were switched on. */
    static Set<String> enabled(Context c) {
        final Set<String> ids = new LinkedHashSet<String>();
        final File f = record(c);
        if (!f.isFile()) {
            return ids;
        }
        try {
            final JSONArray list = new JSONObject(GameLaunch.readText(f)).optJSONArray("enabled");
            for (int i = 0; list != null && i < list.length(); ++i) {
                if (isId(list.optString(i))) {
                    ids.add(list.optString(i));
                }
            }
        } catch (Exception e) {
            // An unreadable record: nothing is switched on.
        }
        return ids;
    }

    /** Switch a mod on or off. With the last one off the record itself is gone. */
    static void setEnabled(Context c, String id, boolean on) {
        final Set<String> ids = enabled(c);
        if (on ? !ids.add(id) : !ids.remove(id)) {
            return;
        }
        if (ids.isEmpty()) {
            record(c).delete();
            return;
        }
        try {
            GameLaunch.writeText(record(c), new JSONObject().put("enabled", new JSONArray(ids)).toString());
        } catch (Exception e) {
            // Not written: the switch shows what the record says at the next reading.
        }
    }

    /**
     * The request's lines for the mods that take part in a game started with these fields;
     * empty when none does, and then the game is the base game. A mod takes part where its
     * package says it belongs: campaign missions, custom scenarios; a saved game of either is
     * loaded with all of them; a network game with none.
     */
    static String requestBlock(Context c, String fields) {
        final Set<String> ids = enabled(c);
        if (ids.isEmpty() || fields.contains("mode = \"lan_")) {
            return "";
        }
        final boolean campaign = fields.contains("mode = \"campaign\"");
        final boolean load = fields.contains("mode = \"load\"");
        final StringBuilder r = new StringBuilder();
        for (String id : ids) {
            final File dir = new File(directory(c), id);
            final Mod mod = dir.isDirectory() ? describe(dir, id) : null;
            if (mod == null || !mod.usable || !(load || (campaign ? mod.inCampaign : mod.inCustom))) {
                continue;
            }
            r.append("    { id = \"").append(mod.id).append("\", version = \"").append(mod.version)
                .append("\", hash = \"\", gameplay = false },\n");
        }
        return r.length() == 0 ? "" : "  mods = {\n" + r + "  },\n";
    }

    // ----- import and removal -----

    /** A package file that was read, before it is one of the installed. */
    static final class Import {
        /** One of ModPackage's faults; OK when the package waits in the working directory. */
        int fault;
        String detail = "";
        /** The package as its mod.json describes it. */
        Mod mod;
        /** The installed package of the same id, which this one would replace; null when there is none. */
        Mod installed;
        private File root;
    }

    // Working directories of an import. Neither name can be an id, so no list shows them and
    // the game cannot be told to read them.
    private static File incoming(Context c) { return new File(directory(c), ".import"); }
    private static File outgoing(Context c) { return new File(directory(c), ".import-old"); }

    /**
     * Read a picked package file into the working directory and say what it is. Nothing of the
     * installed packages is touched. May take long: not for the main thread.
     */
    static Import read(Context c, Uri document) {
        final Import result = new Import();
        final File work = incoming(c);
        // What an import that never ended has left.
        ModPackage.delete(work);
        ModPackage.delete(outgoing(c));
        if (!work.mkdirs()) {
            result.fault = ModPackage.UNREADABLE;
            return result;
        }
        ModPackage.Result unpacked;
        try (InputStream in = c.getContentResolver().openInputStream(document)) {
            if (in == null) {
                throw new java.io.IOException();
            }
            unpacked = ModPackage.unpack(in, work);
        } catch (java.io.IOException | RuntimeException e) {
            unpacked = new ModPackage.Result();
            unpacked.fault = ModPackage.UNREADABLE;
        }
        result.fault = unpacked.fault;
        result.detail = unpacked.detail;
        if (result.fault == ModPackage.OK) {
            String id = null;
            try {
                id = new JSONObject(GameLaunch.readText(new File(unpacked.root, "mod.json"))).optString("id");
            } catch (Exception e) {
                // Not a manifest.
            }
            result.mod = describe(unpacked.root, id);
            if (result.mod == null) {
                result.fault = ModPackage.BAD_MANIFEST;
            }
        }
        if (result.fault != ModPackage.OK) {
            ModPackage.delete(work);
            return result;
        }
        result.root = unpacked.root;
        final File there = new File(directory(c), result.mod.id);
        result.installed = there.isDirectory() ? describe(there, result.mod.id) : null;
        if (result.installed == null && there.exists()) {
            // A directory of that name that is no package: it is replaced all the same.
            result.installed = new Mod();
            result.installed.id = result.installed.name = result.mod.id;
            result.installed.version = "";
        }
        return result;
    }

    /**
     * Put a read package among the installed, in place of the one of the same id when there is
     * one; that one is removed only when the new one stands in its place. Whether the mod is
     * switched on is not changed. Not for the main thread.
     */
    static boolean install(Context c, Import read) {
        final File there = new File(directory(c), read.mod.id), old = outgoing(c);
        boolean done = false;
        if (read.root != null && read.root.isDirectory() && (!there.exists() || there.renameTo(old))) {
            done = read.root.renameTo(there);
            if (!done && old.exists()) {
                old.renameTo(there);
            }
        }
        ModPackage.delete(incoming(c));
        ModPackage.delete(old);
        return done;
    }

    /** Give up a read package. Not for the main thread. */
    static void discard(Context c) {
        ModPackage.delete(incoming(c));
    }

    /** Remove an installed package and switch it off. Not for the main thread. */
    static boolean remove(Context c, String id) {
        if (!isId(id)) {
            return false;
        }
        setEnabled(c, id, false);
        return ModPackage.delete(new File(directory(c), id));
    }
}
