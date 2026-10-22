package net.wargushd.game;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * The list of mods offered by the source, and the download of one of them. The source is a
 * public address that serves index.json and the package files; nothing is asked of it but
 * when the Mods section is open (when it opens, and on a pull of the list), and the index is
 * then asked for with the ETag of the copy at hand, so an unchanged list costs no transfer.
 * A downloaded file is checked against the index's size and SHA-256 before it is unpacked,
 * and from there it takes the same way as a package file the player picks.
 */
final class ModCatalog {
    /** The built-in source. Options, Game Files, Mods source (advanced) can turn it off. */
    static final String BASE = "https://pub-f80f262b299548a6b1683ee0562f6ab0.r2.dev/";
    /** The index format this version reads. */
    static final int FORMAT = 1;
    private static final int MAX_INDEX = 1024 * 1024;
    private static final int CONNECT_MS = 15000, READ_MS = 30000;
    /** Room kept free beside a download and its unpacked copy. */
    private static final long RESERVE = 32L * 1024L * 1024L;

    // What is known of the list (the section's state).
    /** The copy at hand, not yet asked for again. */
    static final int CACHED = 0;
    /** Asked for just now: the copy at hand is the source's. */
    static final int FRESH = 1;
    /** No connection: the copy at hand is shown; nothing can be downloaded. */
    static final int OFFLINE = 2;
    /** The source answered with an error. */
    static final int REFUSED = 3;
    /** The source's index is of a format this version does not read. */
    static final int NEWER_APP = 4;
    /** No source: the built-in one is turned off and no other is given. */
    static final int NO_SOURCE = 5;

    // Whether this version can take an entry.
    static final int FITS = 0, NEEDS_NEWER_APP = 1, NEEDS_OLDER_APP = 2, HOLDS_CODE = 3;

    // How a download ended.
    static final int OK = 0, NO_CONNECTION = 1, NO_SPACE = 2, DAMAGED = 3, SOURCE_REFUSED = 4,
        MISMATCH = 5, CANCELLED = 6;

    /** One mod of the index. */
    static final class Entry {
        String id, name, version, type, description, author, url, asset, sha256;
        long size;
        int engineMin, engineMax;
        boolean executable;
        List<String> categories = new ArrayList<String>(), requires = new ArrayList<String>();
    }

    /** The list as far as it is known. */
    static final class State {
        int section = CACHED;
        /** There is a list (fresh or from before); false when none was ever read. */
        boolean listed;
        /** When the list at hand was last confirmed by the source, in ms; 0 when never. */
        long checkedAt;
        /** The status of an error answer. */
        int status;
        List<Entry> entries = new ArrayList<Entry>();
    }

    /** A download under way. */
    static final class Download {
        final Entry entry;
        final File part;
        volatile long done;
        volatile boolean cancel, installing;
        /** How it ended: one of OK ... CANCELLED. */
        int fault = OK;
        /** The HTTP status of a refusal, the megabytes needed when there is no space. */
        long detail;
        /** The package as it was read; null when the transfer did not end well. */
        ModStore.Import read;
        /** The package was new and is installed already. */
        boolean installed;

        Download(Entry entry, File part) {
            this.entry = entry;
            this.part = part;
        }

        /** 0 .. 100. */
        int percent() {
            return entry.size <= 0 ? 0 : (int) Math.min(100L, done * 100L / entry.size);
        }
    }

    /** Who shows the download: the shell in front, when there is one. */
    interface Listener {
        void downloadMoved();

        void downloadEnded(Download d);
    }

    static volatile Listener listener;
    /** The download under way; one at a time. Read and set on the main thread. */
    static Download current;

    private static final Handler MAIN = new Handler(Looper.getMainLooper());

    private ModCatalog() {}

    private static SharedPreferences store(Context c) {
        return c.getSharedPreferences("mods_catalog", Context.MODE_PRIVATE);
    }

    private static File cacheFile(Context c) {
        return new File(c.getFilesDir(), "mods-index.json");
    }

    /**
     * The source's base address, ending in '/'; null when there is none. The override of
     * Options is honoured as far as it exists: the built-in source turned off and an address
     * stored under "mods_source_address" (typed in Options' top sheet, decision 90 2A). An
     * address with no scheme is taken as https://; another scheme, or no address, means no source.
     */
    static String base(Context c) {
        final SharedPreferences options = c.getSharedPreferences("options", Context.MODE_PRIVATE);
        if (options.getBoolean("mods_source_built_in", true)) {
            return BASE;
        }
        return ownBase(options.getString("mods_source_address", ""));
    }

    /** A typed address as the source's base, ending in '/'; null when it gives no https source. */
    static String ownBase(String typed) {
        String own = (typed == null) ? "" : typed.trim();
        if (own.isEmpty()) {
            return null;
        }
        if (!own.contains("://")) {
            own = "https://" + own;
        }
        if (!own.startsWith("https://") || own.length() <= "https://".length()) {
            return null;
        }
        return own.endsWith("/") ? own : own + "/";
    }

    static int versionCode(Context c) {
        try {
            return c.getPackageManager().getPackageInfo(c.getPackageName(), 0).versionCode;
        } catch (Exception e) {
            return 0;
        }
    }

    /** Whether this version can take the entry. */
    static int fit(Entry e, int versionCode) {
        if (e.executable) {
            return HOLDS_CODE;
        }
        if (e.engineMax > 0 && versionCode > e.engineMax) {
            return NEEDS_OLDER_APP;
        }
        if (e.engineMin > versionCode) {
            return NEEDS_NEWER_APP;
        }
        for (String capability : e.requires) {
            if (!ModStore.knows(capability)) {
                return NEEDS_NEWER_APP;
            }
        }
        return FITS;
    }

    // ----- the list -----

    /** The list at hand, as it was last read. Reads a file: not for the main thread. */
    static State cached(Context c) {
        final State s = new State();
        final String base = base(c);
        if (base == null) {
            s.section = NO_SOURCE;
            return s;
        }
        final SharedPreferences p = store(c);
        if (!base.equals(p.getString("base", ""))) {
            return s;   // a list of another source is no list of this one
        }
        final String text = GameLaunch.readText(cacheFile(c));
        if (text != null && parse(text, s)) {
            s.listed = true;
            s.checkedAt = p.getLong("checked", 0L);
        }
        return s;
    }

    /** Ask the source for the list, with the ETag of the copy at hand. Not for the main thread. */
    static State sync(Context c) {
        final State s = cached(c);
        final String base = base(c);
        if (base == null) {
            return s;
        }
        final SharedPreferences p = store(c);
        HttpURLConnection http = null;
        try {
            http = (HttpURLConnection) new URL(base + "index.json").openConnection();
            http.setConnectTimeout(CONNECT_MS);
            http.setReadTimeout(READ_MS);
            http.setUseCaches(false);
            final String etag = p.getString("etag", "");
            if (s.listed && !etag.isEmpty()) {
                http.setRequestProperty("If-None-Match", etag);
            }
            final int code = http.getResponseCode();
            if (code == HttpURLConnection.HTTP_NOT_MODIFIED && s.listed) {
                s.section = FRESH;
                s.checkedAt = System.currentTimeMillis();
                p.edit().putLong("checked", s.checkedAt).apply();
                return s;
            }
            if (code != HttpURLConnection.HTTP_OK) {
                s.section = REFUSED;
                s.status = code;
                return s;
            }
            final String text = readSmall(http.getInputStream());
            if (text == null) {
                s.section = REFUSED;
                return s;
            }
            final JSONObject json = new JSONObject(text);
            if (json.optInt("format", 0) > FORMAT) {
                s.section = NEWER_APP;
                s.entries.clear();
                s.listed = false;
                return s;
            }
            final int serial = json.optInt("serial", 0);
            if (s.listed && serial < p.getInt("serial", 0)) {
                // An older index served again: the copy at hand stays.
                s.section = FRESH;
                return s;
            }
            final State fresh = new State();
            if (!parse(text, fresh)) {
                s.section = REFUSED;
                return s;
            }
            // A copy that could not be kept is asked for in full the next time.
            final boolean kept = GameLaunch.writeText(cacheFile(c), text);
            fresh.section = FRESH;
            fresh.listed = true;
            fresh.checkedAt = System.currentTimeMillis();
            p.edit().putString("base", base).putString("etag", kept ? nonNull(http.getHeaderField("ETag")) : "")
                .putInt("serial", serial).putLong("checked", fresh.checkedAt).apply();
            return fresh;
        } catch (IOException e) {
            s.section = OFFLINE;
            return s;
        } catch (Exception e) {
            // Not JSON, a file that cannot be written.
            s.section = REFUSED;
            return s;
        } finally {
            if (http != null) {
                http.disconnect();
            }
        }
    }

    private static String nonNull(String s) {
        return s == null ? "" : s;
    }

    /** A text of at most MAX_INDEX bytes; null when it is larger. */
    private static String readSmall(InputStream in) throws IOException {
        try (InputStream from = in) {
            final ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            final byte[] buffer = new byte[16384];
            for (int got; (got = from.read(buffer)) > 0; ) {
                bytes.write(buffer, 0, got);
                if (bytes.size() > MAX_INDEX) {
                    return null;
                }
            }
            return bytes.toString("UTF-8");
        }
    }

    /** Read an index into a state's entries; entries that cannot be used are left out. */
    private static boolean parse(String text, State s) {
        try {
            final JSONObject json = new JSONObject(text);
            if (json.optInt("format", 0) != FORMAT) {
                return false;
            }
            final JSONArray mods = json.optJSONArray("mods");
            s.entries.clear();
            for (int i = 0; mods != null && i < mods.length(); ++i) {
                final JSONObject m = mods.optJSONObject(i);
                if (m == null || m.optBoolean("withdrawn", false)) {
                    continue;
                }
                final Entry e = new Entry();
                e.id = m.optString("id");
                e.name = m.optString("name", e.id);
                e.version = m.optString("version");
                e.type = m.optString("type");
                e.description = m.optString("description");
                e.author = m.optString("author");
                e.url = m.optString("url");
                e.asset = m.optString("asset", e.id + "-" + e.version + ".zip");
                e.sha256 = m.optString("sha256").toLowerCase(java.util.Locale.ROOT);
                e.size = m.optLong("size", 0L);
                e.engineMin = m.optInt("engine_min", 0);
                e.engineMax = m.optInt("engine_max", 0);
                e.executable = m.optBoolean("executable", false);
                strings(m.optJSONArray("categories"), e.categories);
                strings(m.optJSONArray("requires"), e.requires);
                if (ModPackage.isId(e.id) && e.version.matches("[0-9A-Za-z.+-]{1,32}") && !e.url.isEmpty()
                    && e.sha256.matches("[0-9a-f]{64}") && e.size > 0 && e.size <= ModPackage.MAX_BYTES) {
                    s.entries.add(e);
                }
            }
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    private static void strings(JSONArray from, List<String> into) {
        for (int i = 0; from != null && i < from.length(); ++i) {
            into.add(from.optString(i));
        }
    }

    /**
     * The ids of the list that are not installed and were not in a list the player saw
     * before; all of the list counts as seen from now on.
     */
    static Set<String> takeNew(Context c, State s, Set<String> installed) {
        final Set<String> fresh = new HashSet<String>();
        if (!s.listed) {
            return fresh;
        }
        final SharedPreferences p = store(c);
        final Set<String> seen = new HashSet<String>(p.getStringSet("seen", Collections.<String>emptySet()));
        for (Entry e : s.entries) {
            if (!installed.contains(e.id) && seen.add(e.id)) {
                fresh.add(e.id);
            }
        }
        p.edit().putStringSet("seen", seen).apply();
        return fresh;
    }

    /** The installed ids that came from the list (and not from a file). */
    static Set<String> fromList(Context c) {
        return new HashSet<String>(store(c).getStringSet("from_list", Collections.<String>emptySet()));
    }

    /** Record whether an installed mod came from the list. */
    static void setFromList(Context c, String id, boolean listed) {
        final Set<String> ids = fromList(c);
        if (listed ? ids.add(id) : ids.remove(id)) {
            store(c).edit().putStringSet("from_list", ids).apply();
        }
    }

    /** -1, 0, 1 as version a is older, the same as, newer than b; parts compared as numbers where they are. */
    static int compareVersions(String a, String b) {
        final String[] x = a.split("[.+-]"), y = b.split("[.+-]");
        for (int i = 0; i < Math.max(x.length, y.length); ++i) {
            final String p = i < x.length ? x[i] : "0", q = i < y.length ? y[i] : "0";
            int d;
            if (p.matches("[0-9]{1,9}") && q.matches("[0-9]{1,9}")) {
                d = Integer.compare(Integer.parseInt(p), Integer.parseInt(q));
            } else {
                d = p.compareTo(q);
            }
            if (d != 0) {
                return d < 0 ? -1 : 1;
            }
        }
        return 0;
    }

    // ----- the download -----

    /**
     * Download an entry and read it the way an imported file is read, on a thread of its own;
     * the listener hears of each step on the main thread. Only on the main thread.
     */
    static Download start(final Context app, final String base, final Entry e) {
        final Download d = new Download(e, new File(new File(app.getCacheDir(), "mod-download"), e.id + ".part"));
        current = d;
        new Thread(new Runnable() {
            @Override
            public void run() {
                d.fault = fetch(app, base, d);
                if (d.fault == OK) {
                    d.installing = true;
                    moved();
                    d.read = ModStore.read(app, d.part);
                    d.read.fromList = true;
                    if (d.read.fault == ModPackage.OK
                        && (!e.id.equals(d.read.mod.id) || !e.version.equals(d.read.mod.version))) {
                        ModStore.discard(app);
                        d.read = null;
                        d.fault = MISMATCH;
                    } else if (d.read.fault == ModPackage.OK && d.read.installed == null) {
                        d.installed = ModStore.install(app, d.read);
                        if (d.installed) {
                            setFromList(app, e.id, true);   // also when no shell hears of the end
                        }
                    }
                }
                d.part.delete();
                MAIN.post(new Runnable() {
                    @Override
                    public void run() {
                        if (current == d) {
                            current = null;
                        }
                        final Listener l = listener;
                        if (l != null) {
                            l.downloadEnded(d);
                        } else if (d.read != null && d.read.fault == ModPackage.OK && d.read.installed != null) {
                            // Nobody to ask whether it may replace the installed one: given up.
                            new Thread(new Runnable() {
                                @Override
                                public void run() {
                                    ModStore.discard(app);
                                }
                            }, "mod-download").start();
                        }
                    }
                });
            }
        }, "mod-download").start();
        return d;
    }

    /** Stop the download under way at its next block; what was fetched is deleted. */
    static void cancel() {
        if (current != null && !current.installing) {
            current.cancel = true;
        }
    }

    private static void moved() {
        MAIN.post(new Runnable() {
            @Override
            public void run() {
                final Listener l = listener;
                if (l != null) {
                    l.downloadMoved();
                }
            }
        });
    }

    /** Fetch the entry's file into d.part and check its size and SHA-256; the file is deleted unless OK. */
    private static int fetch(Context c, String base, Download d) {
        final Entry e = d.entry;
        int result = NO_CONNECTION;
        HttpURLConnection http = null;
        try {
            final File dir = d.part.getParentFile();
            if (!dir.isDirectory() && !dir.mkdirs()) {
                return result = NO_SPACE;
            }
            d.part.delete();
            // The download and its unpacked copy, side by side.
            final long need = e.size * 2L + RESERVE;
            if (c.getFilesDir().getUsableSpace() < need || dir.getUsableSpace() < e.size + RESERVE) {
                d.detail = (need + 1024L * 1024L - 1L) / (1024L * 1024L);
                return result = NO_SPACE;
            }
            final URL url = new URL(new URL(base), e.url);
            if (!"https".equals(url.getProtocol())) {
                return result = SOURCE_REFUSED;
            }
            http = (HttpURLConnection) url.openConnection();
            http.setConnectTimeout(CONNECT_MS);
            http.setReadTimeout(READ_MS);
            http.setUseCaches(false);
            // The bytes as they are stored: the hash is of the file, not of a compressed transfer.
            http.setRequestProperty("Accept-Encoding", "identity");
            final int code = http.getResponseCode();
            if (code != HttpURLConnection.HTTP_OK) {
                d.detail = code;
                return result = SOURCE_REFUSED;
            }
            final String length = http.getHeaderField("Content-Length");
            if (length != null && !length.trim().equals(String.valueOf(e.size))) {
                return result = DAMAGED;
            }
            final MessageDigest sha = MessageDigest.getInstance("SHA-256");
            final byte[] buffer = new byte[65536];
            int shown = -1;
            try (InputStream in = http.getInputStream(); OutputStream out = new FileOutputStream(d.part)) {
                for (int got; (got = in.read(buffer)) > 0; ) {
                    if (d.cancel) {
                        return result = CANCELLED;
                    }
                    if (d.done + got > e.size) {
                        return result = DAMAGED;
                    }
                    try {
                        out.write(buffer, 0, got);
                    } catch (IOException full) {
                        d.detail = (e.size * 2L + RESERVE) / (1024L * 1024L);
                        return result = NO_SPACE;
                    }
                    sha.update(buffer, 0, got);
                    d.done += got;
                    if (d.percent() != shown) {
                        shown = d.percent();
                        moved();
                    }
                }
            }
            if (d.cancel) {
                return result = CANCELLED;
            }
            if (d.done != e.size || !hex(sha.digest()).equals(e.sha256)) {
                return result = DAMAGED;
            }
            return result = OK;
        } catch (IOException | RuntimeException | java.security.NoSuchAlgorithmException x) {
            return result = d.cancel ? CANCELLED : NO_CONNECTION;
        } finally {
            if (http != null) {
                http.disconnect();
            }
            if (result != OK) {
                d.part.delete();
            }
        }
    }

    private static String hex(byte[] bytes) {
        final StringBuilder s = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) {
            s.append(Character.forDigit((b >> 4) & 15, 16)).append(Character.forDigit(b & 15, 16));
        }
        return s.toString();
    }
}
