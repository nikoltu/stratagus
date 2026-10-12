package net.wargushd.game.editor;

/**
 * Where the picture of a unit of the editor's palette is found in the player's game folder, as
 * the game finds it: the unit's picture file of the unit scripts (one a tileset) is looked up
 * in hd_units_map.json of the data directory, which names the atlas and the frame prefix; the
 * still picture is the frame PREFIX_0 of Data/Art/hd/unit/ATLAS_sprites and, where the unit
 * carries its player's colour, PREFIX_0_team of ATLAS_masks. No Android class.
 */
public final class UnitFrames {
    /** Pixels of the game's art a map tile (HdNativeTilePx of the engine). */
    public static final float NATIVE_TILE = 216f;

    /** The kinds with a picture, in the order of the tables below. */
    public static final int[] TYPES = { MapRules.GOLD_MINE, MapRules.OIL_PATCH, MapRules.PEASANT, MapRules.PEON,
        MapRules.TOWN_HALL, MapRules.GREAT_HALL };

    /**
     * The picture file of each kind by the PUD's tileset number (summer, winter, wasteland,
     * swamp), as UnitTypeFiles of scripts/units.lua, scripts/human/units.lua and
     * scripts/orc/units.lua give it; a worker has one file for every tileset.
     */
    private static final String[][] FILES = {
        { "tilesets/summer/neutral/buildings/gold_mine.png", "tilesets/winter/neutral/buildings/gold_mine.png",
          "tilesets/wasteland/neutral/buildings/gold_mine.png", "tilesets/swamp/neutral/buildings/gold_mine.png" },
        { "tilesets/summer/neutral/buildings/oil_patch.png", "tilesets/summer/neutral/buildings/oil_patch.png",
          "tilesets/wasteland/neutral/buildings/oil_patch.png", "tilesets/swamp/neutral/buildings/oil_patch.png" },
        { "human/units/peasant.png", "human/units/peasant.png", "human/units/peasant.png", "human/units/peasant.png" },
        { "orc/units/peon.png", "orc/units/peon.png", "orc/units/peon.png", "orc/units/peon.png" },
        { "tilesets/summer/human/buildings/town_hall.png", "tilesets/winter/human/buildings/town_hall.png",
          "tilesets/summer/human/buildings/town_hall.png", "tilesets/swamp/human/buildings/town_hall.png" },
        { "tilesets/summer/orc/buildings/great_hall.png", "tilesets/winter/orc/buildings/great_hall.png",
          "tilesets/summer/orc/buildings/great_hall.png", "tilesets/swamp/orc/buildings/great_hall.png" },
    };

    private UnitFrames() {}

    /** The place of a unit type in TYPES, or -1 for a type without a picture. */
    public static int indexOf(int type) {
        for (int i = 0; i < TYPES.length; ++i) {
            if (TYPES[i] == type) {
                return i;
            }
        }
        return -1;
    }

    /** The picture file of a unit type on a tileset, or null. */
    public static String file(int type, int tileset) {
        final int i = indexOf(type);
        return (i < 0 || tileset < 0 || tileset > 3) ? null : FILES[i][tileset];
    }

    /** The text of the object that follows a key, braces included, or null. */
    private static String object(String json, String key) {
        int at = json.indexOf("\"" + key + "\"");
        while (at >= 0) {
            int i = at + key.length() + 2;
            while (i < json.length() && Character.isWhitespace(json.charAt(i))) {
                ++i;
            }
            if (i < json.length() && json.charAt(i) == ':') {
                final int open = json.indexOf('{', i);
                int depth = 0;
                for (int j = open; open >= 0 && j < json.length(); ++j) {
                    final char ch = json.charAt(j);
                    if (ch == '{') {
                        ++depth;
                    } else if (ch == '}' && --depth == 0) {
                        return json.substring(open, j + 1);
                    }
                }
                return null;
            }
            at = json.indexOf("\"" + key + "\"", at + 1);
        }
        return null;
    }

    /** The text after a key up to the next comma or closing brace, without quotes and blanks; null without the key. */
    private static String value(String object, String key) {
        final int at = object.indexOf("\"" + key + "\"");
        final int colon = (at < 0) ? -1 : object.indexOf(':', at + key.length() + 2);
        if (colon < 0) {
            return null;
        }
        int end = colon + 1;
        boolean quoted = false;
        while (end < object.length()) {
            final char ch = object.charAt(end);
            if (ch == '"') {
                quoted = !quoted;
            } else if (!quoted && (ch == ',' || ch == '}')) {
                break;
            }
            ++end;
        }
        return object.substring(colon + 1, end).replace("\"", "").trim();
    }

    private static int number(String object, String key) {
        final String v = (object == null) ? null : value(object, key);
        if (v == null) {
            throw new NumberFormatException(key);
        }
        return Integer.parseInt(v);
    }

    /**
     * The atlas and the frame prefix hd_units_map.json gives a picture file, or null. The key
     * is the file in small letters, as the engine looks it up.
     */
    public static String[] entry(String mapJson, String file) {
        if (mapJson == null || file == null) {
            return null;
        }
        final String o = object(mapJson, file.toLowerCase(java.util.Locale.ROOT));
        final String atlas = (o == null) ? null : value(o, "atlas"), prefix = (o == null) ? null : value(o, "prefix");
        return (atlas == null || prefix == null || atlas.isEmpty() || prefix.isEmpty()) ? null
            : new String[] { atlas, prefix };
    }

    /**
     * A frame of a texture-packer sheet by its name: the rect in the sheet (x, y, w, h), where
     * the rect lies in the frame's own untrimmed canvas (x, y) and the canvas (w, h). Null when
     * the sheet has no such frame, the frame is turned or its numbers do not read.
     */
    public static int[] frame(String atlasJson, String name) {
        if (atlasJson == null) {
            return null;
        }
        final String o = object(atlasJson, name);
        if (o == null) {
            return null;
        }
        try {
            final String in = object(o, "frame"), at = object(o, "spriteSourceSize"), whole = object(o, "sourceSize");
            final String turned = value(o, "rotated");
            if (turned != null && !"false".equals(turned)) {
                return null;
            }
            final int[] f = { number(in, "x"), number(in, "y"), number(in, "w"), number(in, "h"),
                (at == null) ? 0 : number(at, "x"), (at == null) ? 0 : number(at, "y"),
                (whole == null) ? number(in, "w") : number(whole, "w"),
                (whole == null) ? number(in, "h") : number(whole, "h") };
            return (f[2] > 0 && f[3] > 0 && f[6] > 0 && f[7] > 0) ? f : null;
        } catch (NumberFormatException e) {
            return null;
        }
    }

    /**
     * Where a frame is drawn, as the engine's DrawAtlasFrameCentred places it: the frame's own
     * canvas centred on (cx, cy) at s screen pixels a pixel of the art, the four edges rounded.
     * For a unit on the map (cx, cy) is the middle of its footprint and s the size of a tile on
     * the screen / NATIVE_TILE. out takes left, top, right, bottom.
     */
    public static void place(int[] frame, float cx, float cy, float s, float[] out) {
        final double halfW = frame[6] / 2.0, halfH = frame[7] / 2.0;
        out[0] = Math.round(cx + (frame[4] - halfW) * s);
        out[1] = Math.round(cy + (frame[5] - halfH) * s);
        out[2] = Math.round(cx + (frame[4] + frame[2] - halfW) * s);
        out[3] = Math.round(cy + (frame[5] + frame[3] - halfH) * s);
    }

    /**
     * The scale and the canvas centre at which the trimmed frame fits a square cell of the
     * given side centred on (cx, cy), with the share `room` of the side used: out takes the
     * canvas centre x, y and the scale.
     */
    public static void fit(int[] frame, float cx, float cy, float side, float room, float[] out) {
        final float s = side * room / Math.max(frame[2], frame[3]);
        out[0] = cx - (frame[4] + frame[2] / 2f - frame[6] / 2f) * s;
        out[1] = cy - (frame[5] + frame[3] / 2f - frame[7] / 2f) * s;
        out[2] = s;
    }
}
