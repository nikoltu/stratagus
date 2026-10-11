package net.wargushd.game.lan;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The wire of the launcher's room: one line per message, the discovery datagrams and the map
 * transfer pieces. No socket in here (see LanSockets) and no library: the lines are a small
 * subset of JSON written and read by this class.
 *
 * A line is one object. Values: string, whole number, true / false / null, object, array.
 * Written lines are plain ASCII: the quote and the backslash are escaped with a backslash,
 * everything outside 0x20..0x7e as \\uXXXX. No fractions, no exponents.
 */
public final class LanWire {
    /** The room protocol. Changed whenever a message changes its meaning. */
    public static final int PROTOCOL = 1;
    /** The launcher's port: UDP discovery and the TCP room. */
    public static final int PORT = 26660;
    /** The engine's own port for the game. */
    public static final int ENGINE_PORT = 6660;
    /** Longest line accepted, in bytes, without the line end. */
    public static final int MAX_LINE = 16384;
    /** Longest discovery datagram. */
    public static final int MAX_DATAGRAM = 1200;
    /** Map bytes per chunk line. */
    public static final int CHUNK = 8192;
    /** Largest map accepted from a host. */
    public static final int MAX_MAP = 4 * 1024 * 1024;
    /** First field of every datagram. */
    public static final String MAGIC = "WHDLAN";

    private static final int MAX_DEPTH = 6;
    private static final int MAX_ITEMS = 64;

    private LanWire() {}

    // ---- lines: writing ---------------------------------------------------------------------

    /** A message of the given type; further fields as key, value, key, value. */
    public static Map<String, Object> msg(String type, Object... fields) {
        final Map<String, Object> m = new LinkedHashMap<String, Object>();
        m.put("t", type);
        for (int i = 0; i + 1 < fields.length; i += 2) {
            m.put((String) fields[i], fields[i + 1]);
        }
        return m;
    }

    /** One line for a value built of maps, lists, strings, numbers and booleans. */
    public static String encode(Object v) {
        final StringBuilder b = new StringBuilder(128);
        put(b, v);
        return b.toString();
    }

    private static void put(StringBuilder b, Object v) {
        if (v == null) {
            b.append("null");
        } else if (v instanceof String) {
            quote(b, (String) v);
        } else if (v instanceof Boolean) {
            b.append(((Boolean) v).booleanValue() ? "true" : "false");
        } else if (v instanceof Number) {
            b.append(((Number) v).longValue());
        } else if (v instanceof Map) {
            b.append('{');
            boolean first = true;
            for (Map.Entry<?, ?> e : ((Map<?, ?>) v).entrySet()) {
                if (!first) {
                    b.append(',');
                }
                first = false;
                quote(b, String.valueOf(e.getKey()));
                b.append(':');
                put(b, e.getValue());
            }
            b.append('}');
        } else if (v instanceof List) {
            b.append('[');
            boolean first = true;
            for (Object o : (List<?>) v) {
                if (!first) {
                    b.append(',');
                }
                first = false;
                put(b, o);
            }
            b.append(']');
        } else {
            quote(b, v.toString());
        }
    }

    private static void quote(StringBuilder b, String s) {
        b.append('"');
        for (int i = 0; i < s.length(); ++i) {
            char c = s.charAt(i);
            if (Character.isHighSurrogate(c)) {
                if (i + 1 < s.length() && Character.isLowSurrogate(s.charAt(i + 1))) {
                    hex4(b, c);
                    hex4(b, s.charAt(++i));
                    continue;
                }
                c = '?';   // half a pair cannot travel
            } else if (Character.isLowSurrogate(c)) {
                c = '?';
            }
            if (c == '"' || c == '\\') {
                b.append('\\').append(c);
            } else if (c >= 0x20 && c <= 0x7e) {
                b.append(c);
            } else {
                hex4(b, c);
            }
        }
        b.append('"');
    }

    private static void hex4(StringBuilder b, char c) {
        b.append("\\u");
        final String h = Integer.toHexString(c);
        for (int i = h.length(); i < 4; ++i) {
            b.append('0');
        }
        b.append(h);
    }

    // ---- lines: reading ---------------------------------------------------------------------

    private static final class Bad extends RuntimeException {
        private static final long serialVersionUID = 1L;

        Bad() {
            super(null, null, false, false);
        }
    }

    /** The object a line holds, or null for anything that is not one well-formed object. */
    public static Map<String, Object> decode(String line) {
        if (line == null || line.isEmpty() || line.length() > MAX_LINE) {
            return null;
        }
        try {
            final Parser p = new Parser(line);
            final Object v = p.value(0);
            p.space();
            if (p.i != line.length() || !(v instanceof Map)) {
                return null;
            }
            @SuppressWarnings("unchecked")
            final Map<String, Object> m = (Map<String, Object>) v;
            return m;
        } catch (RuntimeException e) {
            return null;
        }
    }

    private static final class Parser {
        final String s;
        int i;

        Parser(String s) {
            this.s = s;
        }

        void space() {
            while (i < s.length()) {
                final char c = s.charAt(i);
                if (c != ' ' && c != '\t' && c != '\r') {
                    break;
                }
                ++i;
            }
        }

        char next() {
            if (i >= s.length()) {
                throw new Bad();
            }
            return s.charAt(i++);
        }

        void word(String w) {
            if (!s.startsWith(w, i)) {
                throw new Bad();
            }
            i += w.length();
        }

        Object value(int depth) {
            space();
            if (i >= s.length() || depth > MAX_DEPTH) {
                throw new Bad();
            }
            final char c = s.charAt(i);
            if (c == '{') {
                return object(depth);
            }
            if (c == '[') {
                return array(depth);
            }
            if (c == '"') {
                return string();
            }
            if (c == 't') {
                word("true");
                return Boolean.TRUE;
            }
            if (c == 'f') {
                word("false");
                return Boolean.FALSE;
            }
            if (c == 'n') {
                word("null");
                return null;
            }
            return number();
        }

        Object object(int depth) {
            final Map<String, Object> m = new LinkedHashMap<String, Object>();
            ++i;
            space();
            if (i < s.length() && s.charAt(i) == '}') {
                ++i;
                return m;
            }
            for (;;) {
                space();
                if (i >= s.length() || s.charAt(i) != '"' || m.size() >= MAX_ITEMS) {
                    throw new Bad();
                }
                final String key = string();
                space();
                if (next() != ':') {
                    throw new Bad();
                }
                m.put(key, value(depth + 1));
                space();
                final char c = next();
                if (c == '}') {
                    return m;
                }
                if (c != ',') {
                    throw new Bad();
                }
            }
        }

        Object array(int depth) {
            final List<Object> l = new ArrayList<Object>();
            ++i;
            space();
            if (i < s.length() && s.charAt(i) == ']') {
                ++i;
                return l;
            }
            for (;;) {
                if (l.size() >= MAX_ITEMS) {
                    throw new Bad();
                }
                l.add(value(depth + 1));
                space();
                final char c = next();
                if (c == ']') {
                    return l;
                }
                if (c != ',') {
                    throw new Bad();
                }
            }
        }

        String string() {
            ++i;
            final StringBuilder b = new StringBuilder();
            for (;;) {
                char c = next();
                if (c == '"') {
                    break;
                }
                if (c < 0x20) {
                    throw new Bad();
                }
                if (c == '\\') {
                    c = next();
                    switch (c) {
                        case '"': case '\\': case '/': break;
                        case 'n': c = '\n'; break;
                        case 'r': c = '\r'; break;
                        case 't': c = '\t'; break;
                        case 'b': c = '\b'; break;
                        case 'f': c = '\f'; break;
                        case 'u': {
                            int v = 0;
                            for (int k = 0; k < 4; ++k) {
                                final int d = Character.digit(next(), 16);
                                if (d < 0) {
                                    throw new Bad();
                                }
                                v = v * 16 + d;
                            }
                            c = (char) v;
                            break;
                        }
                        default: throw new Bad();
                    }
                }
                b.append(c);
            }
            // Half a surrogate pair is not text.
            for (int k = 0; k < b.length(); ++k) {
                final char c = b.charAt(k);
                if (Character.isHighSurrogate(c)) {
                    if (k + 1 >= b.length() || !Character.isLowSurrogate(b.charAt(k + 1))) {
                        throw new Bad();
                    }
                    ++k;
                } else if (Character.isLowSurrogate(c)) {
                    throw new Bad();
                }
            }
            return b.toString();
        }

        Object number() {
            final int from = i;
            if (i < s.length() && s.charAt(i) == '-') {
                ++i;
            }
            final int digits = i;
            while (i < s.length() && s.charAt(i) >= '0' && s.charAt(i) <= '9') {
                ++i;
            }
            if (i == digits || i - digits > 18) {
                throw new Bad();
            }
            if (i < s.length()) {
                final char c = s.charAt(i);
                if (c == '.' || c == 'e' || c == 'E') {
                    throw new Bad();
                }
            }
            return Long.valueOf(Long.parseLong(s.substring(from, i)));
        }
    }

    // ---- reading fields ---------------------------------------------------------------------

    /** A string field, or null when absent or of another kind. */
    public static String str(Map<String, Object> m, String key) {
        final Object v = (m == null) ? null : m.get(key);
        return (v instanceof String) ? (String) v : null;
    }

    /** A number field, or the given value when absent or of another kind. */
    public static long num(Map<String, Object> m, String key, long otherwise) {
        final Object v = (m == null) ? null : m.get(key);
        return (v instanceof Long) ? ((Long) v).longValue() : otherwise;
    }

    /** A number field as an int inside [min, max], or min - 1 when it is not. */
    public static int range(Map<String, Object> m, String key, int min, int max) {
        final long v = num(m, key, (long) min - 1);
        return (v < min || v > max) ? min - 1 : (int) v;
    }

    /** A true / false field; null when absent or of another kind. */
    public static Boolean flag(Map<String, Object> m, String key) {
        final Object v = (m == null) ? null : m.get(key);
        return (v instanceof Boolean) ? (Boolean) v : null;
    }

    @SuppressWarnings("unchecked")
    public static Map<String, Object> obj(Map<String, Object> m, String key) {
        final Object v = (m == null) ? null : m.get(key);
        return (v instanceof Map) ? (Map<String, Object>) v : null;
    }

    @SuppressWarnings("unchecked")
    public static List<Object> arr(Map<String, Object> m, String key) {
        final Object v = (m == null) ? null : m.get(key);
        return (v instanceof List) ? (List<Object>) v : null;
    }

    // ---- framing ----------------------------------------------------------------------------

    /**
     * Cuts a byte stream into lines. A line longer than MAX_LINE is dropped whole, up to its
     * line end; the stream goes on after it.
     */
    public static final class Framer {
        private final byte[] buf = new byte[MAX_LINE];
        private int n;
        private boolean skipping;
        /** Lines dropped for their length. */
        public int dropped;

        public void feed(byte[] data, int off, int len, List<String> out) {
            for (int k = off; k < off + len; ++k) {
                final byte b = data[k];
                if (b == '\n') {
                    if (skipping) {
                        skipping = false;
                        ++dropped;
                    } else if (n > 0) {
                        out.add(new String(buf, 0, n, StandardCharsets.UTF_8));
                    }
                    n = 0;
                } else if (!skipping) {
                    if (n == MAX_LINE) {
                        skipping = true;
                        n = 0;
                    } else {
                        buf[n++] = b;
                    }
                }
            }
        }
    }

    /** The bytes of a line as they go on a stream. */
    public static byte[] frame(String line) {
        return (line + "\n").getBytes(StandardCharsets.UTF_8);
    }

    // ---- discovery --------------------------------------------------------------------------

    /** What a room says about itself to a device that is looking for rooms. */
    public static final class RoomInfo {
        public int protocol = PROTOCOL;
        public String roomId = "";
        public int epoch;
        public String hostName = "";
        public int portrait;
        public int colour;
        public String mapName = "";
        /** The first 16 hex digits of the map hash. */
        public String mapHash = "";
        public int seatsUsed;
        public int seatsTotal;
        /** How many players the room's map has places for. */
        public int seatsMax = 8;
        public LanIdentity identity = new LanIdentity(0, "", "", "");
        public int port = PORT;
        /** Where the reply came from; set by the scanner, not part of the datagram. */
        public String address = "";

        /** Why this room cannot be joined from a device with the given identity, or null. */
        public String blockedFor(LanIdentity mine) {
            final String d = LanIdentity.differ(PROTOCOL, mine, protocol, identity);
            if (d != null) {
                return d;
            }
            return (seatsUsed >= seatsTotal) ? "full" : null;
        }
    }

    /** True for 8 to 32 lower-case hex digits. */
    static boolean isNonce(String s) {
        return s != null && s.length() >= 8 && s.length() <= 32 && isHex(s);
    }

    static boolean isHex(String s) {
        for (int i = 0; i < s.length(); ++i) {
            final char c = s.charAt(i);
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                return false;
            }
        }
        return true;
    }

    /** The probe datagram: magic, protocol version, nonce. */
    public static byte[] probe(String nonce) {
        return encode(msg("probe", "m", MAGIC, "v", PROTOCOL, "nonce", nonce))
            .getBytes(StandardCharsets.UTF_8);
    }

    /**
     * The nonce of a probe, or null when the datagram is not one. A probe of another protocol
     * version is answered as well: the asker shows the room with its reason.
     */
    public static String parseProbe(byte[] data, int len) {
        final Map<String, Object> m = datagram(data, len);
        if (m == null || !"probe".equals(str(m, "t"))) {
            return null;
        }
        final String nonce = str(m, "nonce");
        return isNonce(nonce) ? nonce : null;
    }

    /** The reply datagram, at most MAX_DATAGRAM bytes: long names are cut to fit. */
    public static byte[] reply(String nonce, RoomInfo r) {
        int nameMax = 32;
        int mapMax = 48;
        for (;;) {
            final Map<String, Object> m = msg("room", "m", MAGIC, "v", r.protocol, "nonce", nonce,
                "room", r.roomId, "epoch", r.epoch, "host", cut(r.hostName, nameMax),
                "portrait", r.portrait, "color", r.colour, "map", cut(r.mapName, mapMax),
                "map_hash", cut(r.mapHash, 16), "used", r.seatsUsed, "total", r.seatsTotal, "max", r.seatsMax);
            r.identity.into(m);
            m.put("port", r.port);
            final byte[] out = encode(m).getBytes(StandardCharsets.UTF_8);
            if (out.length <= MAX_DATAGRAM || (nameMax == 0 && mapMax == 0)) {
                return out;
            }
            nameMax /= 2;
            mapMax /= 2;
        }
    }

    /** The room of a reply to the probe with this nonce, or null. */
    public static RoomInfo parseReply(byte[] data, int len, String nonce) {
        final Map<String, Object> m = datagram(data, len);
        if (m == null || !"room".equals(str(m, "t")) || nonce == null
            || !nonce.equals(str(m, "nonce"))) {
            return null;
        }
        final RoomInfo r = new RoomInfo();
        r.protocol = range(m, "v", 0, 1 << 20);
        r.roomId = str(m, "room");
        r.epoch = range(m, "epoch", 0, Integer.MAX_VALUE);
        r.hostName = str(m, "host");
        r.portrait = range(m, "portrait", 0, 255);
        r.colour = range(m, "color", 0, 7);
        r.mapName = str(m, "map");
        r.mapHash = str(m, "map_hash");
        r.seatsUsed = range(m, "used", 0, 8);
        r.seatsTotal = range(m, "total", 0, 8);
        // A room that does not say how many places its map has: eight.
        r.seatsMax = m.containsKey("max") ? range(m, "max", 2, 8) : 8;
        r.identity = LanIdentity.from(m);
        r.port = range(m, "port", 1, 65535);
        if (r.protocol < 0 || r.roomId == null || r.roomId.isEmpty() || r.roomId.length() > 64
            || r.epoch < 0 || r.hostName == null || r.portrait < 0 || r.colour < 0
            || r.mapName == null || r.mapHash == null || r.seatsUsed < 0 || r.seatsTotal < 0
            || r.seatsMax < 2
            || r.identity == null || r.port < 1) {
            return null;
        }
        return r;
    }

    private static Map<String, Object> datagram(byte[] data, int len) {
        if (data == null || len <= 0 || len > MAX_DATAGRAM || len > data.length) {
            return null;
        }
        final Map<String, Object> m = decode(new String(data, 0, len, StandardCharsets.UTF_8));
        return (m != null && MAGIC.equals(str(m, "m"))) ? m : null;
    }

    /** The first max characters, never half a surrogate pair. */
    static String cut(String s, int max) {
        if (s == null) {
            return "";
        }
        if (s.length() <= max) {
            return s;
        }
        int end = max;
        if (end > 0 && Character.isHighSurrogate(s.charAt(end - 1))) {
            --end;
        }
        return s.substring(0, end);
    }

    // ---- map transfer -----------------------------------------------------------------------

    public static String sha256(byte[] data) {
        try {
            final byte[] d = MessageDigest.getInstance("SHA-256").digest(data);
            final StringBuilder b = new StringBuilder(64);
            for (byte x : d) {
                b.append(Character.forDigit((x >> 4) & 15, 16)).append(Character.forDigit(x & 15, 16));
            }
            return b.toString();
        } catch (java.security.NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
    }

    /** A file name a map may arrive under: *.pud, no folder part. */
    static boolean isMapName(String s) {
        if (s == null || s.length() < 5 || s.length() > 96
            || !s.toLowerCase(java.util.Locale.ROOT).endsWith(".pud") || s.startsWith(".")) {
            return false;
        }
        for (int i = 0; i < s.length(); ++i) {
            final char c = s.charAt(i);
            if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '"' || c == 0x7f) {
                return false;
            }
        }
        return true;
    }

    /** The sending side of one transfer: offer, chunks in order, done. */
    public static final class MapOut {
        public final int xid;
        public final String name;
        public final String hash;
        private final byte[] data;
        /** Bytes handed out as chunks. */
        public int sent;
        /** Bytes the receiver has confirmed. */
        public int acked;
        public boolean doneSent;

        public MapOut(int xid, String name, String hash, byte[] data) {
            this.xid = xid;
            this.name = name;
            this.hash = hash;
            this.data = data;
        }

        public int size() {
            return data.length;
        }

        public String offer() {
            return encode(msg("map_offer", "x", xid, "name", name, "size", data.length,
                              "hash", hash));
        }

        /** The next chunk line, or null when every byte has been handed out. */
        public String nextChunk() {
            if (sent >= data.length) {
                return null;
            }
            final int n = Math.min(CHUNK, data.length - sent);
            final String line = encode(msg("map_chunk", "x", xid, "off", sent,
                                           "data", base64(data, sent, n)));
            sent += n;
            return line;
        }

        public String done() {
            doneSent = true;
            return encode(msg("map_done", "x", xid, "hash", hash));
        }
    }

    /** The receiving side of one transfer. */
    public static final class MapIn {
        public final int xid;
        public final String name;
        public final String hash;
        public final byte[] data;
        public int got;

        private MapIn(int xid, String name, String hash, int size) {
            this.xid = xid;
            this.name = name;
            this.hash = hash;
            this.data = new byte[size];
        }

        /** A receiver for an offer, or null when the offer is not acceptable. */
        public static MapIn fromOffer(Map<String, Object> m) {
            final int x = range(m, "x", 0, Integer.MAX_VALUE);
            final String name = str(m, "name");
            final int size = range(m, "size", 1, MAX_MAP);
            final String hash = str(m, "hash");
            if (x < 0 || size < 1 || !isMapName(name) || hash == null || hash.length() != 64
                || !isHex(hash)) {
                return null;
            }
            return new MapIn(x, name, hash, size);
        }

        /** Take one chunk. False when it is not the next piece of this transfer. */
        public boolean chunk(Map<String, Object> m) {
            if (range(m, "x", 0, Integer.MAX_VALUE) != xid || num(m, "off", -1) != got) {
                return false;
            }
            final byte[] piece = unbase64(str(m, "data"));
            if (piece == null || piece.length == 0 || piece.length > CHUNK
                || got + piece.length > data.length) {
                return false;
            }
            System.arraycopy(piece, 0, data, got, piece.length);
            got += piece.length;
            return true;
        }

        /** True when every byte is here and the hash is the offered one. */
        public boolean complete() {
            return got == data.length && hash.equals(sha256(data));
        }

        public int percent() {
            return (int) ((long) got * 100 / data.length);
        }
    }

    // ---- base64 (own, so that nothing depends on the platform's) ----------------------------

    private static final char[] B64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/".toCharArray();

    static String base64(byte[] d, int off, int len) {
        final StringBuilder b = new StringBuilder((len + 2) / 3 * 4);
        for (int i = off; i < off + len; i += 3) {
            final int left = off + len - i;
            final int v = ((d[i] & 255) << 16) | ((left > 1 ? d[i + 1] & 255 : 0) << 8)
                | (left > 2 ? d[i + 2] & 255 : 0);
            b.append(B64[v >> 18]).append(B64[(v >> 12) & 63]);
            b.append(left > 1 ? B64[(v >> 6) & 63] : '=');
            b.append(left > 2 ? B64[v & 63] : '=');
        }
        return b.toString();
    }

    private static int b64(char c) {
        if (c >= 'A' && c <= 'Z') {
            return c - 'A';
        }
        if (c >= 'a' && c <= 'z') {
            return c - 'a' + 26;
        }
        if (c >= '0' && c <= '9') {
            return c - '0' + 52;
        }
        return (c == '+') ? 62 : (c == '/') ? 63 : -1;
    }

    /** The bytes of a padded base64 string, or null when it is not one. */
    static byte[] unbase64(String s) {
        if (s == null || s.length() % 4 != 0) {
            return null;
        }
        int pad = 0;
        if (s.endsWith("==")) {
            pad = 2;
        } else if (s.endsWith("=")) {
            pad = 1;
        }
        final byte[] out = new byte[s.length() / 4 * 3 - pad];
        int o = 0;
        for (int i = 0; i < s.length(); i += 4) {
            final boolean last = i + 4 == s.length();
            int v = 0;
            for (int k = 0; k < 4; ++k) {
                final char c = s.charAt(i + k);
                int d = b64(c);
                if (d < 0) {
                    if (c != '=' || !last || k < 4 - pad) {
                        return null;
                    }
                    d = 0;
                }
                v = (v << 6) | d;
            }
            if (o < out.length) {
                out[o++] = (byte) (v >> 16);
            }
            if (o < out.length) {
                out[o++] = (byte) (v >> 8);
            }
            if (o < out.length) {
                out[o++] = (byte) v;
            }
        }
        return out;
    }
}
