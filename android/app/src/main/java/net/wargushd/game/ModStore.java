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
        /**
         * The package is one this version can load: every capability it requires is known, and
         * it is presentation only or a data gameplay package (no script entry, no script or
         * program file in it; owner decision 79).
         */
        boolean usable;
        boolean gameplay;
        /** The request modes the package is made for. A gameplay package is never in the campaign. */
        boolean inCampaign, inCustom, inLan;
        /** A gameplay package that requires "lan-gameplay": network games carry it and the room compares it. */
        boolean lanGameplay;
        /** Its kinds name "computer-players": its defs/bots.json offers computer players. */
        boolean computerPlayers;
        /** The language the package is (mod.json "language", e.g. "ltLT") and its own name; null when none. */
        String language, languageName;
    }

    /** What a package may need of this version. */
    private static final List<String> CAPABILITIES = Arrays.asList("mod-path", "voice-override", "language", "briefing-text",
        "unit-voices", "computer-players", "lan-gameplay");

    /** File name endings a data package may not hold: scripts and programs of any kind. */
    private static final String[] PROGRAM_ENDINGS = { ".lua", ".luac", ".so", ".dex", ".jar", ".apk", ".class",
        ".exe", ".dll", ".sh", ".bat", ".cmd", ".py", ".js", ".bin", ".elf" };

    /** The longest "<mod>:<id>": the engine's seat name "mod-bot:<mod>:<id>" stays under 64 bytes. */
    static final int BOT_KEY_MAX = 55;

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

    /** Whether this version has a capability a package may require. */
    static boolean knows(String capability) {
        return CAPABILITIES.contains(capability);
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
            // Owner decision 79: a gameplay mod is never used in the campaign.
            mod.inCampaign = !mod.gameplay && scope != null && scope.optBoolean("campaign", false);
            mod.inCustom = scope == null || scope.optBoolean("custom", true);
            mod.inLan = scope != null && scope.optBoolean("lan", false);
            final JSONArray kinds = json.optJSONArray("kinds");
            for (int i = 0; kinds != null && i < kinds.length(); ++i) {
                mod.computerPlayers |= "computer-players".equals(kinds.optString(i));
            }
            final String tongue = json.optString("language", "");
            if (tongue.matches("[a-z]{2}[A-Z]{2}")) {
                mod.language = tongue;
                mod.languageName = json.optString("language_name", tongue);
            }
            boolean known = true;
            final JSONArray requires = json.optJSONArray("requires");
            for (int i = 0; requires != null && i < requires.length(); ++i) {
                known &= CAPABILITIES.contains(requires.optString(i));
                mod.lanGameplay |= "lan-gameplay".equals(requires.optString(i));
            }
            // A gameplay package is usable only as data: no script entry, no script or program
            // file anywhere in it (owner decision 79; LAW 18.8 - no sandbox, so no mod code).
            mod.usable = known && json.optInt("format", 0) == 1
                && mod.id.equals(json.optString("id"))
                && !json.optBoolean("contains_game_derived", true)
                && mod.version.matches("[0-9A-Za-z.+-]{1,32}")
                && (!mod.gameplay || (noScriptEntry(json) && dataOnly(dir)));
            mod.lanGameplay &= mod.gameplay && mod.inLan;
            mod.computerPlayers &= mod.gameplay;
            return mod;
        } catch (Exception e) {
            return null;
        }
    }

    /** The manifest's entry names no script (absent or null). */
    private static boolean noScriptEntry(JSONObject json) {
        final JSONObject entry = json.optJSONObject("entry");
        return entry == null || !entry.has("script") || entry.isNull("script");
    }

    /**
     * True when no file of the package is a script or a program, by its name's ending and by its
     * first bytes (ELF, PE, dex, Lua byte code, "#!"). A package with more entries than a package
     * may hold is not taken. Only gameplay packages are looked at; they are small.
     */
    static boolean dataOnly(File dir) {
        final List<File> todo = new ArrayList<File>();
        todo.add(dir);
        int seen = 0;
        while (!todo.isEmpty()) {
            final File[] inside = todo.remove(todo.size() - 1).listFiles();
            for (int i = 0; inside != null && i < inside.length; ++i) {
                final File f = inside[i];
                if (++seen > ModPackage.MAX_ENTRIES) {
                    return false;
                }
                if (f.isDirectory()) {
                    todo.add(f);
                    continue;
                }
                final String name = f.getName().toLowerCase(java.util.Locale.ROOT);
                for (String ending : PROGRAM_ENDINGS) {
                    if (name.endsWith(ending)) {
                        return false;
                    }
                }
                final byte[] head = new byte[4];
                int got = 0;
                try (InputStream in = new java.io.FileInputStream(f)) {
                    for (int n; got < 4 && (n = in.read(head, got, 4 - got)) > 0; ) {
                        got += n;
                    }
                } catch (java.io.IOException e) {
                    return false;
                }
                final boolean elf = got >= 4 && head[0] == 0x7f && head[1] == 'E' && head[2] == 'L' && head[3] == 'F';
                final boolean pe = got >= 2 && head[0] == 'M' && head[1] == 'Z';
                final boolean dex = got >= 4 && head[0] == 'd' && head[1] == 'e' && head[2] == 'x' && head[3] == '\n';
                final boolean luac = got >= 4 && head[0] == 0x1b && head[1] == 'L' && head[2] == 'u' && head[3] == 'a';
                final boolean shebang = got >= 2 && head[0] == '#' && head[1] == '!';
                if (elf || pe || dex || luac || shebang) {
                    return false;
                }
            }
        }
        return true;
    }

    /** SHA-256 of a package's manifest.json in lower-case hex; "" when it has none. */
    static String manifestHash(File dir) {
        final File f = new File(dir, "manifest.json");
        return f.isFile() ? sha256(f) : "";
    }

    /** SHA-256 of a file in lower-case hex; "" when it cannot be read. */
    static String sha256(File f) {
        try (InputStream in = new java.io.FileInputStream(f)) {
            final java.security.MessageDigest md = java.security.MessageDigest.getInstance("SHA-256");
            final byte[] buf = new byte[65536];
            for (int n; (n = in.read(buf)) > 0; ) {
                md.update(buf, 0, n);
            }
            final StringBuilder b = new StringBuilder();
            for (byte x : md.digest()) {
                b.append(Character.forDigit((x >> 4) & 15, 16)).append(Character.forDigit(x & 15, 16));
            }
            return b.toString();
        } catch (Exception e) {
            return "";
        }
    }

    // ----- computer players (bots mod, phase 1 part 2) -----

    /** One computer player a mod offers. "<mod>:<id>" is its key in requests and in the LAN room. */
    static final class Bot {
        final String mod, id, nameKey;

        Bot(String mod, String id, String nameKey) {
            this.mod = mod;
            this.id = id;
            this.nameKey = nameKey;
        }

        String key() {
            return mod + ":" + id;
        }
    }

    /**
     * The computer players the switched-on mods offer (only those of status "playable"), with
     * their names in the language in use. Empty when no such mod is on: then no page shows a
     * choice and every computer seat is the original's.
     */
    static final class BotChoices {
        final List<Bot> bots = new ArrayList<Bot>();
        final List<String> names = new ArrayList<String>();
        /** The mod's words for the row label and for the original computer player; null when it has none. */
        String label, original;
        private final java.util.Map<String, String> modNames = new java.util.HashMap<String, String>();

        boolean any() {
            return !bots.isEmpty();
        }

        /** The place of a key in the list, or -1. */
        int indexOf(String key) {
            for (int i = 0; key != null && i < bots.size(); ++i) {
                if (bots.get(i).key().equals(key)) {
                    return i;
                }
            }
            return -1;
        }

        /** The name of a key ("" is the original); a key of a mod this device lacks shows its id. */
        String nameOf(String key, String originalWord) {
            if (key == null || key.isEmpty()) {
                return original != null ? original : originalWord;
            }
            final int at = indexOf(key);
            if (at >= 0) {
                return names.get(at);
            }
            return capital(key.substring(key.indexOf(':') + 1));
        }

        /** The name of an installed mod, or its id. */
        String modName(String id) {
            final String n = modNames.get(id);
            return (n != null && !n.isEmpty()) ? n : id;
        }
    }

    private static String capital(String id) {
        return id.isEmpty() ? id : Character.toUpperCase(id.charAt(0)) + id.substring(1);
    }

    /** A JSON object of a file, a leading byte-order mark allowed; null when it does not read. */
    private static JSONObject json(File f) {
        String text = GameLaunch.readText(f);
        if (text == null) {
            return null;
        }
        if (!text.isEmpty() && text.charAt(0) == '﻿') {
            text = text.substring(1);
        }
        try {
            return new JSONObject(text);
        } catch (Exception e) {
            return null;
        }
    }

    /** A package's own words: strings/enUS.json with strings/<language>.json laid over it. */
    private static JSONObject words(File dir, String language) {
        final JSONObject all = new JSONObject();
        for (String tongue : new String[] { "enUS", language }) {
            final JSONObject one = (tongue == null) ? null : json(new File(dir, "strings/" + tongue + ".json"));
            for (java.util.Iterator<String> k = (one == null) ? null : one.keys(); k != null && k.hasNext(); ) {
                final String key = k.next();
                final String text = one.optString(key, "");
                if (!text.isEmpty()) {
                    try {
                        all.put(key, text);
                    } catch (Exception e) {
                        // not taken
                    }
                }
            }
        }
        return all;
    }

    /** The playable bots of a package's defs/bots.json (format 1); empty when it does not read. */
    private static List<Bot> playable(File dir, String modId) {
        final List<Bot> out = new ArrayList<Bot>();
        final JSONObject defs = json(new File(dir, "defs/bots.json"));
        final JSONArray list = (defs != null && defs.optInt("format", 0) == 1) ? defs.optJSONArray("bots") : null;
        for (int i = 0; list != null && i < list.length(); ++i) {
            final JSONObject b = list.optJSONObject(i);
            final String id = (b == null) ? "" : b.optString("id", "");
            if (b != null && "playable".equals(b.optString("status")) && isId(id)
                && (modId + ":" + id).length() <= BOT_KEY_MAX) {
                out.add(new Bot(modId, id, b.optString("name_key", "")));
            }
        }
        return out;
    }

    /** The computer players offered now. Reads a few small files: once per page, not per frame. */
    static BotChoices botChoices(Context c) {
        final BotChoices r = new BotChoices();
        final Set<String> on = enabled(c);
        if (on.isEmpty()) {
            return r;
        }
        final String language = SkinKit.language(c);
        for (Mod m : installed(c)) {
            r.modNames.put(m.id, m.name);
        }
        for (String id : on) {
            final File dir = new File(directory(c), id);
            final Mod mod = dir.isDirectory() ? describe(dir, id) : null;
            if (mod == null || !mod.usable || !mod.computerPlayers) {
                continue;
            }
            final List<Bot> bots = playable(dir, id);
            if (bots.isEmpty()) {
                continue;
            }
            final JSONObject words = words(dir, language);
            for (Bot b : bots) {
                if (r.indexOf(b.key()) < 0) {
                    r.bots.add(b);
                    final String name = words.optString(b.nameKey, "");
                    r.names.add(name.isEmpty() ? capital(b.id) : name);
                }
            }
            if (r.label == null && !words.optString("bots.computer", "").isEmpty()) {
                r.label = words.optString("bots.computer");
            }
            if (r.original == null && !words.optString("bots.original", "").isEmpty()) {
                r.original = words.optString("bots.original");
            }
        }
        return r;
    }

    /**
     * The switched-on gameplay mods a network game carries, as the room compares them:
     * { id, version, SHA-256 of manifest.json }, in the order they were switched on.
     */
    static List<String[]> lanGameplay(Context c) {
        final List<String[]> out = new ArrayList<String[]>();
        for (String id : enabled(c)) {
            final File dir = new File(directory(c), id);
            final Mod mod = dir.isDirectory() ? describe(dir, id) : null;
            if (mod != null && mod.usable && mod.lanGameplay) {
                out.add(new String[] { mod.id, mod.version, manifestHash(dir) });
            }
        }
        return out;
    }

    /** The line of a saved game that names a computer player of a mod (wc2bot.cpp, Wc2BotSaveText). */
    private static final java.util.regex.Pattern SAVED_BOT = java.util.regex.Pattern.compile(
        "^if SetWc2BotState then SetWc2BotState\\(\\s*\\d+\\s*,\\s*\"mod-bot:([a-z0-9-]{3,40}):([a-z0-9-]{3,40})\""
        + "\\s*,\\s*\"([0-9A-Fa-f]*)\"");

    /**
     * Why a saved game cannot be loaded with the mods switched on now, in the words of the
     * language in use; null when no mod stands in its way. The head of a save names no mods, so
     * the whole file is read for the computer players' lines the engine writes; the engine checks
     * the same again when it reads the file (Wc2BotRefusal). Reads the file: a moment for a large one.
     */
    static String saveRefusal(Context c, File save, SkinKit kit) {
        // With no computer players' mod on this device the file is not read: nothing changes
        // without the mod (LAW 18.2); a save that needs one is refused by the engine.
        boolean any = false;
        for (Mod m : installed(c)) {
            any |= m.computerPlayers;
        }
        if (!any) {
            return null;
        }
        final List<String[]> wanted = new ArrayList<String[]>();
        try (InputStream raw = new java.io.FileInputStream(save);
             InputStream in = save.getName().endsWith(".gz") ? new java.util.zip.GZIPInputStream(raw, 65536) : raw;
             java.io.BufferedReader r = new java.io.BufferedReader(new java.io.InputStreamReader(in, "UTF-8"), 65536)) {
            for (String line; (line = r.readLine()) != null; ) {
                if (!line.startsWith("if SetWc2BotState")) {
                    continue;
                }
                final java.util.regex.Matcher m = SAVED_BOT.matcher(line);
                if (m.find()) {
                    wanted.add(new String[] { m.group(1), m.group(2), m.group(3) });
                }
            }
        } catch (Exception e) {
            // Unreadable: the engine tells what it finds.
            return null;
        }
        final Set<String> on = enabled(c);
        for (String[] w : wanted) {
            final File dir = new File(directory(c), w[0]);
            final Mod mod = (on.contains(w[0]) && dir.isDirectory()) ? describe(dir, w[0]) : null;
            boolean has = false;
            if (mod != null && mod.usable && mod.computerPlayers) {
                for (Bot b : playable(dir, w[0])) {
                    has |= b.id.equals(w[1]);
                }
            }
            if (!has) {
                return botRefusal(c, kit, w[0], false);
            }
            if (!sha256(new File(dir, "defs/bots.json")).equalsIgnoreCase(w[2])) {
                return botRefusal(c, kit, w[0], true);
            }
        }
        return null;
    }

    /**
     * The engine's refusal of a saved game (Wc2BotRefusal, in English) in the words of the
     * language in use: the mod's own sentence where the mod is installed, else the launcher's.
     */
    static String botRefusalText(Context c, SkinKit kit, String engineText) {
        final String text = (engineText == null) ? "" : engineText;
        final int a = text.indexOf('\''), b = (a >= 0) ? text.indexOf('\'', a + 1) : -1;
        final String mod = (b > a + 1) ? text.substring(a + 1, b) : "";
        if (!isId(mod)) {
            return text.isEmpty() ? kit.own("The saved game could not be loaded.") : text;
        }
        return botRefusal(c, kit, mod, text.contains("another version"));
    }

    private static String botRefusal(Context c, SkinKit kit, String modId, boolean changed) {
        final File dir = new File(directory(c), modId);
        final JSONObject words = dir.isDirectory() ? words(dir, SkinKit.language(c)) : new JSONObject();
        final String own = words.optString(changed ? "bots.load_changed" : "bots.load_missing", "");
        if (!own.isEmpty()) {
            return own;
        }
        return changed
            ? kit.own("This saved game was made with another version of the mod {0}.", modId)
            : kit.own("This saved game needs the mod {0}. Install and enable it to load the game.", modId);
    }

    /**
     * The first switched-on, usable package that is a language and holds its game string table
     * (strings/<language>.json); null when none is. Reads a few small files.
     */
    static Mod languageMod(Context c) {
        for (String id : enabled(c)) {
            final File dir = new File(directory(c), id);
            final Mod mod = dir.isDirectory() ? describe(dir, id) : null;
            if (mod != null && mod.usable && mod.language != null
                && new File(dir, "strings/" + mod.language + ".json").isFile()) {
                return mod;
            }
        }
        return null;
    }

    /** A file of an installed package, by its path inside the package. */
    static File file(Context c, String id, String path) {
        return new File(new File(directory(c), id), path);
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
     * package says it belongs: campaign missions (never a gameplay mod), custom scenarios; a
     * saved game of either is loaded with all of them; a network game only with the gameplay
     * mods made for it ("lan-gameplay"), which the room compares before Ready.
     */
    static String requestBlock(Context c, String fields) {
        final Set<String> ids = enabled(c);
        if (ids.isEmpty()) {
            return "";
        }
        final boolean lan = fields.contains("mode = \"lan_");
        final boolean campaign = fields.contains("mode = \"campaign\"");
        final boolean load = fields.contains("mode = \"load\"");
        final StringBuilder r = new StringBuilder();
        for (String id : ids) {
            final File dir = new File(directory(c), id);
            final Mod mod = dir.isDirectory() ? describe(dir, id) : null;
            final boolean takes = mod != null && mod.usable
                && (lan ? mod.lanGameplay : (load || (campaign ? mod.inCampaign : mod.inCustom)));
            if (!takes) {
                continue;
            }
            r.append("    { id = \"").append(mod.id).append("\", version = \"").append(mod.version)
                .append("\", hash = \"").append(manifestHash(dir)).append("\", gameplay = ")
                .append(mod.gameplay).append(" },\n");
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
        /** The package was downloaded from the mod list, not picked as a file. */
        boolean fromList;
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
    static Import read(final Context c, final Uri document) {
        return read(c, new Source() {
            @Override
            public InputStream open() throws java.io.IOException {
                final InputStream in = c.getContentResolver().openInputStream(document);
                if (in == null) {
                    throw new java.io.IOException();
                }
                return in;
            }
        });
    }

    /** As read, for a package file of the app's own (a download). */
    static Import read(Context c, final File file) {
        return read(c, new Source() {
            @Override
            public InputStream open() throws java.io.IOException {
                return new java.io.BufferedInputStream(new java.io.FileInputStream(file), 65536);
            }
        });
    }

    /** Where a package file is read from. */
    private interface Source {
        InputStream open() throws java.io.IOException;
    }

    private static Import read(Context c, Source source) {
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
        try (InputStream in = source.open()) {
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
