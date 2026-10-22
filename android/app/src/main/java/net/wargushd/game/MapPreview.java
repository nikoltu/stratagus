package net.wargushd.game;

import android.graphics.Bitmap;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.util.HashMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * A map's picture, one pixel a tile, coloured as the game's own minimap colours a tile:
 *
 * the map's tile number (PUD section MTXM) goes through the tileset's tile table (the "slots"
 * of scripts/tilesets/wargus/<era>.lua: a "solid" slot is one row of sixteen tile numbers, a
 * "mixed" slot is rows of sixteen) to a megatile number; the game folder's
 * <era>_mapColors.bin holds sixteen palette indices a megatile (the first megatile is 16) and
 * <era>.ppl the palette (256 x 3 bytes, 6-bit); the tile's colour is the mean of the sixteen.
 */
final class MapPreview {
    private MapPreview() {}

    /** The tileset script, the colour file's stem and the palette's folder, by the PUD's era. */
    private static final String[][] ERAS = {
        { "summer", "forest", "Forest" }, { "winter", "iceland", "Iceland" },
        { "wasteland", "swamp", "Swamp" }, { "swamp", "xswamp", "XSwamp" },
    };
    private static final int FIRST_MEGATILE = 16;

    /** Pictures made, and the maps whose picture could not be made; read without the lock. */
    private static final java.util.concurrent.ConcurrentHashMap<String, Bitmap> KNOWN =
        new java.util.concurrent.ConcurrentHashMap<String, Bitmap>();
    private static final java.util.concurrent.ConcurrentHashMap<String, Boolean> FAILED =
        new java.util.concurrent.ConcurrentHashMap<String, Boolean>();

    private static String key(File pud) {
        return pud.getPath() + "|" + pud.length() + "|" + pud.lastModified();
    }

    /**
     * Whether the picture of a map has been made (or found impossible) already, so that
     * {@link #known} answers for it. Never reads the map: for the main thread.
     */
    static boolean isKnown(File pud) {
        final String key = key(pud);
        return KNOWN.containsKey(key) || FAILED.containsKey(key);
    }

    /** The picture of a map if it has been made already, else null. Never reads the map. */
    static Bitmap known(File pud) {
        return (pud != null) ? KNOWN.get(key(pud)) : null;
    }
    private static final HashMap<Integer, int[]> TABLES = new HashMap<Integer, int[]>();
    private static final HashMap<String, int[]> COLOURS = new HashMap<String, int[]>();

    /**
     * The picture of a map, or null when the map, the tileset's table or the game's colour
     * files cannot be read. Pictures are kept for the life of the process.
     */
    static synchronized Bitmap of(File gameFolder, File dataDir, File pud) {
        if (gameFolder == null || pud == null) {
            return null;
        }
        final String key = key(pud);
        if (KNOWN.containsKey(key) || FAILED.containsKey(key)) {
            return KNOWN.get(key);
        }
        Bitmap made = null;
        try {
            made = make(gameFolder, dataDir, pud);
        } catch (Exception e) {
            made = null;
        } catch (OutOfMemoryError e) {
            made = null;
        }
        if (made != null) {
            KNOWN.put(key, made);
        } else {
            FAILED.put(key, Boolean.TRUE);
        }
        return made;
    }

    /**
     * The picture of a map that is in no file, coloured the same way; null when the tileset's
     * table or the game's colour files cannot be read. Not kept: the caller holds it.
     */
    static synchronized Bitmap of(File gameFolder, File dataDir, net.wargushd.game.editor.MapModel m) {
        if (gameFolder == null || m == null) {
            return null;
        }
        try {
            final int era = (m.tileset() >= 0 && m.tileset() <= 3) ? m.tileset() : 0;
            final int[] table = table(dataDir, era);
            final int[] colours = colours(gameFolder, era);
            if (table == null || colours == null) {
                return null;
            }
            final int w = m.width(), h = m.height();
            final int[] px = new int[w * h];
            for (int i = 0; i < px.length; ++i) {
                final int tile = m.tileAt(i);
                final int mega = (tile >= 0 && tile < table.length) ? table[tile] - FIRST_MEGATILE : -1;
                px[i] = (mega >= 0 && mega < colours.length) ? colours[mega] : 0xFF000000;
            }
            return Bitmap.createBitmap(px, w, h, Bitmap.Config.ARGB_8888);
        } catch (Exception e) {
            return null;
        } catch (OutOfMemoryError e) {
            return null;
        }
    }

    private static Bitmap make(File gameFolder, File dataDir, File pud) throws Exception {
        int w = 0, h = 0, era = 0;
        byte[] tiles = null;
        try (InputStream in = new BufferedInputStream(new FileInputStream(pud))) {
            final byte[] head = new byte[8];
            while (readFully(in, head, 8)) {
                final String tag = new String(head, 0, 4, "US-ASCII");
                final long length = (head[4] & 0xFFL) | ((head[5] & 0xFFL) << 8)
                    | ((head[6] & 0xFFL) << 16) | ((head[7] & 0xFFL) << 24);
                if (length < 0 || length > 64L * 1024 * 1024) {
                    break;
                }
                final byte[] body = new byte[(int) length];
                if (!readFully(in, body, body.length)) {
                    break;
                }
                if (tag.equals("DIM ") && length >= 4) {
                    w = (body[0] & 0xFF) | ((body[1] & 0xFF) << 8);
                    h = (body[2] & 0xFF) | ((body[3] & 0xFF) << 8);
                } else if ((tag.equals("ERA ") || tag.equals("ERAX")) && length >= 2) {
                    final int e = (body[0] & 0xFF) | ((body[1] & 0xFF) << 8);
                    era = (e >= 0 && e <= 3) ? e : 0;
                } else if (tag.equals("MTXM")) {
                    tiles = body;
                }
            }
        }
        if (w <= 0 || h <= 0 || w > 1024 || h > 1024 || tiles == null || tiles.length < w * h * 2) {
            return null;
        }
        final int[] table = table(dataDir, era);
        final int[] colours = colours(gameFolder, era);
        if (table == null || colours == null) {
            return null;
        }
        final int[] px = new int[w * h];
        for (int i = 0; i < px.length; ++i) {
            final int tile = (tiles[i * 2] & 0xFF) | ((tiles[i * 2 + 1] & 0xFF) << 8);
            final int mega = (tile < table.length) ? table[tile] - FIRST_MEGATILE : -1;
            px[i] = (mega >= 0 && mega < colours.length) ? colours[mega] : 0xFF000000;
        }
        return Bitmap.createBitmap(px, w, h, Bitmap.Config.ARGB_8888);
    }

    /** The tileset's tile table: the megatile number of every PUD tile number. */
    private static int[] table(File dataDir, int era) {
        if (TABLES.containsKey(era)) {
            return TABLES.get(era);
        }
        int[] table = null;
        String text = GameLaunch.readText(
            new File(dataDir, "scripts/tilesets/wargus/" + ERAS[era][0] + ".lua"));
        final int slots = (text == null) ? -1 : text.indexOf("\"slots\"");
        if (slots >= 0) {
            text = text.substring(slots).replaceAll("--[^\\n]*", "");
            final int first = text.indexOf("\"solid\"");      // the "special" block has no rows
            if (first >= 0) {
                final Matcher rows = Pattern.compile("\\{([\\s\\d,]*)\\}").matcher(text.substring(first));
                final java.util.ArrayList<Integer> flat = new java.util.ArrayList<Integer>();
                final Pattern number = Pattern.compile("\\d+");
                while (rows.find()) {
                    final Matcher n = number.matcher(rows.group(1));
                    int got = 0;
                    while (got < 16 && n.find()) {
                        flat.add(Integer.parseInt(n.group()));
                        ++got;
                    }
                    for (; got < 16; ++got) {
                        flat.add(0);
                    }
                }
                table = new int[flat.size()];
                for (int i = 0; i < table.length; ++i) {
                    table[i] = flat.get(i);
                }
            }
        }
        TABLES.put(era, table);
        return table;
    }

    /** One colour a megatile, from the game's own minimap colour file and palette. */
    private static int[] colours(File gameFolder, int era) throws Exception {
        final String key = gameFolder.getPath() + "|" + era;
        if (COLOURS.containsKey(key)) {
            return COLOURS.get(key);
        }
        int[] out = null;
        final byte[] ppl = bytes(new File(gameFolder,
            "Data/Art/bgs/" + ERAS[era][2] + "/" + ERAS[era][1] + ".ppl"));
        final byte[] bin = bytes(new File(gameFolder,
            "Data/Art/hd/classic/" + ERAS[era][1] + "_mapColors.bin"));
        if (ppl != null && ppl.length >= 768 && bin != null && bin.length >= 16) {
            final int[][] pal = new int[256][3];
            for (int i = 0; i < 256; ++i) {
                for (int c = 0; c < 3; ++c) {
                    pal[i][c] = ((ppl[i * 3 + c] & 0x3F) * 255 + 31) / 63;
                }
            }
            out = new int[bin.length / 16];
            for (int t = 0; t < out.length; ++t) {
                int r = 0, g = 0, b = 0;
                for (int k = 0; k < 16; ++k) {
                    final int p = bin[t * 16 + k] & 0xFF;
                    r += pal[p][0];
                    g += pal[p][1];
                    b += pal[p][2];
                }
                out[t] = 0xFF000000 | ((r / 16) << 16) | ((g / 16) << 8) | (b / 16);
            }
        }
        COLOURS.put(key, out);
        return out;
    }

    private static byte[] bytes(File f) throws Exception {
        if (!f.isFile() || f.length() > 8L * 1024 * 1024) {
            return null;
        }
        final byte[] all = new byte[(int) f.length()];
        try (InputStream in = new FileInputStream(f)) {
            return readFully(in, all, all.length) ? all : null;
        }
    }

    private static boolean readFully(InputStream in, byte[] into, int count) throws java.io.IOException {
        int got = 0;
        while (got < count) {
            final int n = in.read(into, got, count - got);
            if (n < 0) {
                return false;
            }
            got += n;
        }
        return true;
    }
}
