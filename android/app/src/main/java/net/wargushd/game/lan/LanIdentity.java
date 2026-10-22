package net.wargushd.game.lan;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;

/**
 * What must be equal on two devices before they may play together: the app version, the
 * content stamp, the game-data stamp and the mod-set hash. The protocol version is the wire's
 * own and is compared beside these.
 */
public final class LanIdentity {
    /** Reason codes, in the order the values are compared. */
    public static final String PROTOCOL = "protocol";
    public static final String VERSION = "version";
    public static final String CONTENT = "content";
    public static final String GAME_FILES = "game_files";
    public static final String MODS = "mods";

    public final long appVersion;
    public final String contentStamp;
    public final String gameDataStamp;
    public final String modSetHash;

    public LanIdentity(long appVersion, String contentStamp, String gameDataStamp,
                       String modSetHash) {
        this.appVersion = appVersion;
        this.contentStamp = (contentStamp == null) ? "" : contentStamp;
        this.gameDataStamp = (gameDataStamp == null) ? "" : gameDataStamp;
        this.modSetHash = (modSetHash == null) ? "" : modSetHash;
    }

    /**
     * The game-folder files the simulation reads (LAN_ARCHITECTURE.md, "game-data stamp"), from
     * the engine's readers: src/stratagus/wc2data.cpp (unit stats) and src/ai/wc2ai.cpp (the
     * computer player's tables). A map is not here: it has its own hash in the room.
     */
    public static final String[] GAME_DATA_FILES = {
        "Data/Rez/unitdata.dat",
        "Data/Rez/ai.bin",
        "Data/Rez/script.bin",
    };
    /** A stamp's length in hex digits. */
    static final int STAMP_DIGITS = 16;
    /** Larger files are not read whole: the stamp then says so (no simulation file is near it). */
    private static final long STAMP_FILE_MAX = 64L << 20;

    /**
     * The content stamp: the staged scripts the engine runs (files/data/scripts), every file in
     * the order of its path relative to the folder; its path, its length and its bytes go in.
     * "none" when the folder holds no file.
     */
    public static String contentStamp(File scripts) {
        final List<String> paths = new ArrayList<String>();
        collect(scripts, "", paths);
        if (paths.isEmpty()) {
            return "none";
        }
        Collections.sort(paths);
        final MessageDigest d = digest();
        for (String rel : paths) {
            add(d, rel, new File(scripts, rel));
        }
        return hex(d.digest());
    }

    /**
     * The game-data stamp: the files of GAME_DATA_FILES in the game folder, in that order; a
     * missing file goes in as missing. "no-folder" without a folder.
     */
    public static String gameDataStamp(File gameFolder) {
        if (gameFolder == null || !gameFolder.isDirectory()) {
            return "no-folder";
        }
        final MessageDigest d = digest();
        for (String rel : GAME_DATA_FILES) {
            add(d, rel, new File(gameFolder, rel));
        }
        return hex(d.digest());
    }

    private static void collect(File dir, String prefix, List<String> out) {
        final File[] list = (dir == null) ? null : dir.listFiles();
        if (list == null) {
            return;
        }
        for (File f : list) {
            final String rel = prefix.isEmpty() ? f.getName() : prefix + "/" + f.getName();
            if (f.isDirectory()) {
                collect(f, rel, out);
            } else if (f.isFile()) {
                out.add(rel);
            }
        }
    }

    private static void add(MessageDigest d, String rel, File f) {
        d.update(rel.getBytes(StandardCharsets.UTF_8));
        d.update((byte) 0);
        if (!f.isFile()) {
            d.update("missing".getBytes(StandardCharsets.UTF_8));
            d.update((byte) 0);
            return;
        }
        final long length = f.length();
        d.update(Long.toString(length).getBytes(StandardCharsets.UTF_8));
        d.update((byte) 0);
        if (length > STAMP_FILE_MAX) {
            d.update("large".getBytes(StandardCharsets.UTF_8));
            return;
        }
        final byte[] buf = new byte[65536];
        try (InputStream in = new FileInputStream(f)) {
            for (int n; (n = in.read(buf)) > 0; ) {
                d.update(buf, 0, n);
            }
        } catch (IOException e) {
            d.update("unreadable".getBytes(StandardCharsets.UTF_8));
        }
        d.update((byte) 0);
    }

    private static MessageDigest digest() {
        try {
            return MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
    }

    private static String hex(byte[] bytes) {
        final StringBuilder s = new StringBuilder(STAMP_DIGITS);
        for (int i = 0; i < STAMP_DIGITS / 2; ++i) {
            s.append(Character.forDigit((bytes[i] >> 4) & 15, 16)).append(Character.forDigit(bytes[i] & 15, 16));
        }
        return s.toString();
    }

    /** Null when equal, otherwise the code of the first value that differs. */
    public String differsFrom(LanIdentity other) {
        if (other == null || appVersion != other.appVersion) {
            return VERSION;
        }
        if (!contentStamp.equals(other.contentStamp)) {
            return CONTENT;
        }
        if (!gameDataStamp.equals(other.gameDataStamp)) {
            return GAME_FILES;
        }
        if (!modSetHash.equals(other.modSetHash)) {
            return MODS;
        }
        return null;
    }

    /** The same with the protocol version in front. */
    public static String differ(int protocolA, LanIdentity a, int protocolB, LanIdentity b) {
        if (protocolA != protocolB) {
            return PROTOCOL;
        }
        return a.differsFrom(b);
    }

    /** Write the four values into a message. */
    public void into(Map<String, Object> m) {
        m.put("app", appVersion);
        m.put("content", contentStamp);
        m.put("data", gameDataStamp);
        m.put("mods", modSetHash);
    }

    /** Read the four values of a message; null when one is missing or too long. */
    public static LanIdentity from(Map<String, Object> m) {
        final long app = LanWire.num(m, "app", -1);
        final String content = LanWire.str(m, "content");
        final String data = LanWire.str(m, "data");
        final String mods = LanWire.str(m, "mods");
        if (app < 0 || content == null || data == null || mods == null
            || content.length() > 80 || data.length() > 80 || mods.length() > 80) {
            return null;
        }
        return new LanIdentity(app, content, data, mods);
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof LanIdentity && differsFrom((LanIdentity) o) == null;
    }

    @Override
    public int hashCode() {
        return (int) appVersion ^ contentStamp.hashCode() ^ gameDataStamp.hashCode()
            ^ modSetHash.hashCode();
    }
}
