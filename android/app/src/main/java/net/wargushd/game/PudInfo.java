package net.wargushd.game;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;

/**
 * The few facts about a map that can be read from a PUD file without the engine: its size,
 * how many players it is made for, and its tileset.
 *
 * A PUD file is a sequence of sections: a four-character tag, a 32-bit little-endian
 * length, and that many bytes of data.
 */
final class PudInfo {
    /** Map size in tiles; 0 when the file did not say. */
    int width, height;
    /** Slots owned by a person or a computer; 0 when the file did not say. */
    int players;
    /** 0 forest, 1 winter, 2 wasteland, 3 swamp; -1 when the file did not say. */
    int tileset = -1;

    private static final int OWNER_COMPUTER = 4, OWNER_PERSON = 5;
    /** Only the first eight slots can be players; the rest are neutral and rescue slots. */
    private static final int PLAYER_SLOTS = 8;

    private PudInfo() {}

    /** Facts that were not read from a file: those of a map that is not written yet. */
    static PudInfo of(int width, int height, int tileset, int players) {
        final PudInfo info = new PudInfo();
        info.width = width;
        info.height = height;
        info.tileset = tileset;
        info.players = players;
        return info;
    }

    /** Facts already read in this process, by path, size and time of change. */
    private static final java.util.Map<String, PudInfo> KNOWN = new java.util.HashMap<String, PudInfo>();

    /**
     * As read, but a file that has not changed since it was last read is not opened again.
     * Safe for two threads; the file is read outside the lock, so one thread never waits for
     * the other's reading.
     */
    static PudInfo of(java.io.File f) {
        final String key = f.getPath() + "|" + f.length() + "|" + f.lastModified();
        PudInfo facts;
        synchronized (KNOWN) {
            facts = KNOWN.get(key);
        }
        if (facts == null) {
            facts = read(f);
            synchronized (KNOWN) {
                KNOWN.put(key, facts);
            }
        }
        return facts;
    }

    /** Read the facts of a file; whatever a damaged or foreign file lacks stays unset. */
    static PudInfo read(File f) {
        final PudInfo info = new PudInfo();
        try (InputStream in = new BufferedInputStream(new FileInputStream(f))) {
            final byte[] head = new byte[8];
            int seen = 0;
            while (seen < 3 && readFully(in, head, 8)) {
                final String tag = new String(head, 0, 4, "US-ASCII");
                final long length = (head[4] & 0xFFL) | ((head[5] & 0xFFL) << 8)
                    | ((head[6] & 0xFFL) << 16) | ((head[7] & 0xFFL) << 24);
                if (length < 0 || length > 64L * 1024 * 1024) {
                    break;
                }
                if (tag.equals("DIM ") && length >= 4) {
                    final byte[] d = new byte[4];
                    if (!readFully(in, d, 4)) {
                        break;
                    }
                    info.width = (d[0] & 0xFF) | ((d[1] & 0xFF) << 8);
                    info.height = (d[2] & 0xFF) | ((d[3] & 0xFF) << 8);
                    skip(in, length - 4);
                    ++seen;
                } else if (tag.equals("OWNR") && length >= PLAYER_SLOTS) {
                    final byte[] d = new byte[PLAYER_SLOTS];
                    if (!readFully(in, d, PLAYER_SLOTS)) {
                        break;
                    }
                    int n = 0;
                    for (byte b : d) {
                        if (b == OWNER_COMPUTER || b == OWNER_PERSON) {
                            ++n;
                        }
                    }
                    info.players = n;
                    skip(in, length - PLAYER_SLOTS);
                    ++seen;
                } else if ((tag.equals("ERA ") || tag.equals("ERAX")) && length >= 2) {
                    final byte[] d = new byte[2];
                    if (!readFully(in, d, 2)) {
                        break;
                    }
                    // The expansion's tag, when present, overrides the base one.
                    final int era = (d[0] & 0xFF) | ((d[1] & 0xFF) << 8);
                    if (tag.equals("ERAX") || info.tileset < 0) {
                        info.tileset = (era >= 0 && era <= 3) ? era : 0;
                    }
                    skip(in, length - 2);
                    if (tag.equals("ERA ")) {
                        ++seen;
                    }
                } else {
                    skip(in, length);
                }
            }
        } catch (Exception e) {
            // Unreadable: the caller shows the file's name only.
        }
        return info;
    }

    private static boolean readFully(InputStream in, byte[] buf, int n) throws Exception {
        int off = 0;
        while (off < n) {
            final int r = in.read(buf, off, n - off);
            if (r < 0) {
                return false;
            }
            off += r;
        }
        return true;
    }

    private static void skip(InputStream in, long n) throws Exception {
        while (n > 0) {
            final long r = in.skip(n);
            if (r <= 0) {
                if (in.read() < 0) {
                    return;
                }
                --n;
            } else {
                n -= r;
            }
        }
    }
}
