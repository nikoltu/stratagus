package net.wargushd.game;

import org.json.JSONTokener;

import java.io.File;
import java.io.FileInputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * What a Warcraft II: Remastered folder must contain for this app, and how to check it.
 *
 * Nothing here touches the user interface, so both the first-run gate and any later
 * status indicator can use it. The checks read; they never write and never copy.
 */
final class GameFiles {
    /** Files the app cannot work without, relative to the game folder. */
    static final String[] REQUIRED = {
        "Data/skins/skins.json",
        "Data/skins/Modern_Graphics.json",
        "Data/skins/Modern_Graphics.png",
        "Data/skins/Modern_Widgets_Orc.json",
        "Data/skins/Modern_Widgets_Orc.png",
        "Data/skins/Modern_Widgets_Human.json",
        "Data/skins/Modern_Widgets_Human.png",
        "Data/skins/Modern_HUD_Widgets.json",
        "Data/skins/Modern_HUD_Widgets.png",
        "Data/skins/Maps.json",
        "Data/skins/Maps.png",
        "Data/Backgrounds/main_background.png",
        "Data/Backgrounds/main_background_characters.png",
        "Data/Strings/enUS.json",
        "Data/Font/BLQ55.ttf",
    };

    /** Content the game runs without: directory, and the word used for it on screen. */
    static final String[][] OPTIONAL = {
        { "Data/Movies", "movies" },
        { "Data/Speech", "speech" },
        { "Data/Music", "music" },
    };

    private GameFiles() {}

    /** Outcome of checking the required files. */
    static final class Required {
        /** True when every required file is present and well-formed. */
        boolean ok;
        /** The first path that failed, relative to the folder; null when ok. */
        String path;
        /** True when that file exists but could not be read as what it should be. */
        boolean unreadable;
    }

    /**
     * Check the required files in order and stop at the first that fails. Existence alone
     * would accept a truncated copy, so JSON files are parsed and PNG files must begin
     * with a PNG header.
     */
    static Required checkRequired(File folder) {
        final Required r = new Required();
        for (String rel : REQUIRED) {
            final File f = new File(folder, rel);
            if (!f.isFile() || f.length() == 0) {
                r.path = rel;
                return r;
            }
            final boolean good;
            if (rel.endsWith(".json")) {
                good = isJson(f);
            } else if (rel.endsWith(".png")) {
                good = isPng(f);
            } else {
                good = f.canRead();
            }
            if (!good) {
                r.path = rel;
                r.unreadable = true;
                return r;
            }
        }
        r.ok = true;
        return r;
    }

    /** The on-screen words of the optional content that is present (found) or not (absent). */
    static void checkOptional(File folder, List<String> found, List<String> absent) {
        for (String[] entry : OPTIONAL) {
            final File dir = new File(folder, entry[0]);
            final String[] names = dir.isDirectory() ? dir.list() : null;
            if (names != null && names.length > 0) {
                found.add(entry[1]);
            } else {
                absent.add(entry[1]);
            }
        }
    }

    /** Join words with commas. */
    static String join(List<String> words) {
        final StringBuilder s = new StringBuilder();
        for (String w : words) {
            if (s.length() > 0) {
                s.append(", ");
            }
            s.append(w);
        }
        return s.toString();
    }

    private static boolean isJson(File f) {
        final byte[] buf = readAll(f);
        if (buf == null) {
            return false;
        }
        try {
            int off = 0;
            // A UTF-8 byte-order mark is not part of the text.
            if (buf.length >= 3 && (buf[0] & 0xFF) == 0xEF && (buf[1] & 0xFF) == 0xBB
                && (buf[2] & 0xFF) == 0xBF) {
                off = 3;
            }
            final Object value =
                new JSONTokener(new String(buf, off, buf.length - off, "UTF-8")).nextValue();
            return value instanceof org.json.JSONObject || value instanceof org.json.JSONArray;
        } catch (Exception e) {
            return false;
        } catch (OutOfMemoryError e) {
            return false;
        }
    }

    private static boolean isPng(File f) {
        final int[] signature = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        final byte[] head = new byte[16];
        try (FileInputStream in = new FileInputStream(f)) {
            int off = 0;
            while (off < head.length) {
                final int n = in.read(head, off, head.length - off);
                if (n < 0) {
                    return false;
                }
                off += n;
            }
        } catch (Exception e) {
            return false;
        }
        for (int i = 0; i < signature.length; ++i) {
            if ((head[i] & 0xFF) != signature[i]) {
                return false;
            }
        }
        // The first chunk of a PNG is its header chunk.
        return head[12] == 'I' && head[13] == 'H' && head[14] == 'D' && head[15] == 'R';
    }

    private static byte[] readAll(File f) {
        final long size = f.length();
        if (size > 64L * 1024 * 1024) {
            return null;
        }
        final byte[] buf = new byte[(int) size];
        try (FileInputStream in = new FileInputStream(f)) {
            int off = 0;
            while (off < buf.length) {
                final int n = in.read(buf, off, buf.length - off);
                if (n < 0) {
                    return null;
                }
                off += n;
            }
            return buf;
        } catch (Exception e) {
            return null;
        } catch (OutOfMemoryError e) {
            return null;
        }
    }

    /** Words an empty list should not print: helper for callers building a sentence. */
    static List<String> newList() {
        return new ArrayList<String>();
    }
}
