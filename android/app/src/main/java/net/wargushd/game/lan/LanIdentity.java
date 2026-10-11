package net.wargushd.game.lan;

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
