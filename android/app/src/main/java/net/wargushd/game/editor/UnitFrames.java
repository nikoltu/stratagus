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

    /**
     * The types with a picture: every type of the palette (UnitCatalog) but the two start
     * locations, which are drawn as marks; in the PUD table's order, as FILES.
     */
    public static final int[] TYPES = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
        0x0C, 0x0D, 0x0E, 0x0F, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
        0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x23, 0x26, 0x27, 0x28,
        0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x31, 0x32, 0x33, 0x34, 0x35,
        0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42,
        0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E,
        0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A,
        0x5B, 0x5C, 0x5D, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
    };

    /**
     * The picture file of each type, by the PUD's tileset number (summer, winter, wasteland,
     * swamp) where it differs by tileset, else one for all: UnitTypeFiles of scripts/units.lua,
     * scripts/human/units.lua and scripts/orc/units.lua where the type has one there (the game's
     * scripts.lua puts the tileset's file into Image), else the "file" of its Image (HD scripts,
     * fulltree-changes; the JVM test reads them and checks every row).
     */
    private static final String[][] FILES = {
        { "human/units/footman.png" },  // unit-footman
        { "orc/units/grunt.png" },  // unit-grunt
        { "human/units/peasant.png" },  // unit-peasant
        { "orc/units/peon.png" },  // unit-peon
        { "human/units/ballista.png" },  // unit-ballista
        { "orc/units/catapult.png" },  // unit-catapult
        { "human/units/knight.png" },  // unit-knight
        { "orc/units/ogre.png" },  // unit-ogre
        { "human/units/elven_archer.png" },  // unit-archer
        { "orc/units/troll_axethrower.png" },  // unit-axethrower
        { "human/units/mage.png" },  // unit-mage
        { "orc/units/death_knight.png" },  // unit-death-knight
        { "human/units/knight.png" },  // unit-paladin
        { "orc/units/ogre.png" },  // unit-ogre-mage
        { "human/units/dwarven_demolition_squad.png" },  // unit-dwarves
        { "orc/units/goblin_sappers.png" },  // unit-goblin-sappers
        { "human/units/elven_archer.png" },  // unit-ranger
        { "orc/units/troll_axethrower.png" },  // unit-berserker
        { "human/units/elven_archer.png" },  // unit-female-hero
        { "orc/units/death_knight.png" },  // unit-evil-knight
        { "human/units/gryphon_rider.png" },  // unit-flying-angel
        { "orc/units/ogre.png" },  // unit-fad-man
        { "human/units/mage.png" },  // unit-white-mage
        { "orc/units/grunt.png" },  // unit-beast-cry
        { "human/units/oil_tanker_empty.png" },  // unit-human-oil-tanker
        { "orc/units/oil_tanker_empty.png" },  // unit-orc-oil-tanker
        { "human/units/transport.png" },  // unit-human-transport
        { "orc/units/transport.png" },  // unit-orc-transport
        { "human/units/elven_destroyer.png" },  // unit-human-destroyer
        { "orc/units/troll_destroyer.png" },  // unit-orc-destroyer
        { "human/units/battleship.png" },  // unit-battleship
        { "orc/units/ogre_juggernaught.png" },  // unit-ogre-juggernaught
        { "orc/units/dragon.png" },  // unit-fire-breeze
        // unit-human-submarine
        { "human/units/gnomish_submarine.png", "human/units/gnomish_submarine.png",
          "tilesets/wasteland/human/units/gnomish_submarine.png", "tilesets/swamp/human/units/gnomish_submarine.png" },
        // unit-orc-submarine
        { "orc/units/giant_turtle.png", "orc/units/giant_turtle.png",
          "tilesets/wasteland/orc/units/giant_turtle.png", "tilesets/swamp/orc/units/giant_turtle.png" },
        { "human/units/gnomish_flying_machine.png" },  // unit-balloon
        { "orc/units/goblin_zeppelin.png" },  // unit-zeppelin
        { "human/units/gryphon_rider.png" },  // unit-gryphon-rider
        { "orc/units/dragon.png" },  // unit-dragon
        { "human/units/knight.png" },  // unit-knight-rider
        { "orc/units/eye_of_kilrogg.png" },  // unit-eye-of-vision
        { "human/units/footman.png" },  // unit-arthor-literios
        { "orc/units/grunt.png" },  // unit-quick-blade
        { "orc/units/ogre.png" },  // unit-double-head
        { "human/units/knight.png" },  // unit-wise-man
        { "orc/units/death_knight.png" },  // unit-ice-bringer
        { "human/units/knight.png" },  // unit-man-of-light
        { "orc/units/troll_axethrower.png" },  // unit-sharp-axe
        { "neutral/units/skeleton.png" },  // unit-skeleton
        // unit-daemon
        { "tilesets/summer/neutral/units/daemon.png", "tilesets/winter/neutral/units/daemon.png",
          "tilesets/wasteland/neutral/units/daemon.png", "tilesets/swamp/neutral/units/daemon.png" },
        // unit-critter
        { "tilesets/summer/neutral/units/critter.png", "tilesets/winter/neutral/units/critter.png",
          "tilesets/wasteland/neutral/units/critter.png", "tilesets/swamp/neutral/units/critter.png" },
        // unit-farm
        { "tilesets/summer/human/buildings/farm.png", "tilesets/winter/human/buildings/farm.png",
          "tilesets/wasteland/human/buildings/farm.png", "tilesets/swamp/human/buildings/farm.png" },
        // unit-pig-farm
        { "tilesets/summer/orc/buildings/pig_farm.png", "tilesets/winter/orc/buildings/pig_farm.png",
          "tilesets/wasteland/orc/buildings/pig_farm.png", "tilesets/swamp/orc/buildings/pig_farm.png" },
        // unit-human-barracks
        { "tilesets/summer/human/buildings/barracks.png", "tilesets/winter/human/buildings/barracks.png",
          "tilesets/summer/human/buildings/barracks.png", "tilesets/swamp/human/buildings/barracks.png" },
        // unit-orc-barracks
        { "tilesets/summer/orc/buildings/barracks.png", "tilesets/winter/orc/buildings/barracks.png",
          "tilesets/summer/orc/buildings/barracks.png", "tilesets/swamp/orc/buildings/barracks.png" },
        // unit-church
        { "tilesets/summer/human/buildings/church.png", "tilesets/winter/human/buildings/church.png",
          "tilesets/summer/human/buildings/church.png", "tilesets/swamp/human/buildings/church.png" },
        // unit-altar-of-storms
        { "tilesets/summer/orc/buildings/altar_of_storms.png", "tilesets/winter/orc/buildings/altar_of_storms.png",
          "tilesets/summer/orc/buildings/altar_of_storms.png", "tilesets/swamp/orc/buildings/altar_of_storms.png" },
        // unit-human-watch-tower
        { "tilesets/summer/human/buildings/scout_tower.png", "tilesets/winter/human/buildings/scout_tower.png",
          "tilesets/summer/human/buildings/scout_tower.png", "tilesets/swamp/human/buildings/scout_tower.png" },
        // unit-orc-watch-tower
        { "tilesets/summer/orc/buildings/watch_tower.png", "tilesets/winter/orc/buildings/watch_tower.png",
          "tilesets/summer/orc/buildings/watch_tower.png", "tilesets/swamp/orc/buildings/watch_tower.png" },
        // unit-stables
        { "tilesets/summer/human/buildings/stables.png", "tilesets/winter/human/buildings/stables.png",
          "tilesets/summer/human/buildings/stables.png", "tilesets/swamp/human/buildings/stables.png" },
        // unit-ogre-mound
        { "tilesets/summer/orc/buildings/ogre_mound.png", "tilesets/winter/orc/buildings/ogre_mound.png",
          "tilesets/summer/orc/buildings/ogre_mound.png", "tilesets/swamp/orc/buildings/ogre_mound.png" },
        // unit-inventor
        { "tilesets/summer/human/buildings/gnomish_inventor.png", "tilesets/winter/human/buildings/gnomish_inventor.png",
          "tilesets/summer/human/buildings/gnomish_inventor.png", "tilesets/swamp/human/buildings/gnomish_inventor.png" },
        // unit-alchemist
        { "tilesets/summer/orc/buildings/goblin_alchemist.png", "tilesets/winter/orc/buildings/goblin_alchemist.png",
          "tilesets/summer/orc/buildings/goblin_alchemist.png", "tilesets/swamp/orc/buildings/goblin_alchemist.png" },
        // unit-gryphon-aviary
        { "tilesets/summer/human/buildings/gryphon_aviary.png", "tilesets/winter/human/buildings/gryphon_aviary.png",
          "tilesets/summer/human/buildings/gryphon_aviary.png", "tilesets/swamp/human/buildings/gryphon_aviary.png" },
        // unit-dragon-roost
        { "tilesets/summer/orc/buildings/dragon_roost.png", "tilesets/winter/orc/buildings/dragon_roost.png",
          "tilesets/summer/orc/buildings/dragon_roost.png", "tilesets/swamp/orc/buildings/dragon_roost.png" },
        // unit-human-shipyard
        { "tilesets/summer/human/buildings/shipyard.png", "tilesets/winter/human/buildings/shipyard.png",
          "tilesets/summer/human/buildings/shipyard.png", "tilesets/swamp/human/buildings/shipyard.png" },
        // unit-orc-shipyard
        { "tilesets/summer/orc/buildings/shipyard.png", "tilesets/winter/orc/buildings/shipyard.png",
          "tilesets/summer/orc/buildings/shipyard.png", "tilesets/swamp/orc/buildings/shipyard.png" },
        // unit-town-hall
        { "tilesets/summer/human/buildings/town_hall.png", "tilesets/winter/human/buildings/town_hall.png",
          "tilesets/summer/human/buildings/town_hall.png", "tilesets/swamp/human/buildings/town_hall.png" },
        // unit-great-hall
        { "tilesets/summer/orc/buildings/great_hall.png", "tilesets/winter/orc/buildings/great_hall.png",
          "tilesets/summer/orc/buildings/great_hall.png", "tilesets/swamp/orc/buildings/great_hall.png" },
        // unit-elven-lumber-mill
        { "tilesets/summer/human/buildings/elven_lumber_mill.png", "tilesets/winter/human/buildings/elven_lumber_mill.png",
          "tilesets/wasteland/human/buildings/elven_lumber_mill.png", "tilesets/swamp/human/buildings/elven_lumber_mill.png" },
        // unit-troll-lumber-mill
        { "tilesets/summer/orc/buildings/troll_lumber_mill.png", "tilesets/winter/orc/buildings/troll_lumber_mill.png",
          "tilesets/wasteland/orc/buildings/troll_lumber_mill.png", "tilesets/swamp/orc/buildings/troll_lumber_mill.png" },
        // unit-human-foundry
        { "tilesets/summer/human/buildings/foundry.png", "tilesets/winter/human/buildings/foundry.png",
          "tilesets/summer/human/buildings/foundry.png", "tilesets/swamp/human/buildings/foundry.png" },
        // unit-orc-foundry
        { "tilesets/summer/orc/buildings/foundry.png", "tilesets/winter/orc/buildings/foundry.png",
          "tilesets/summer/orc/buildings/foundry.png", "tilesets/swamp/orc/buildings/foundry.png" },
        // unit-mage-tower
        { "tilesets/summer/human/buildings/mage_tower.png", "tilesets/winter/human/buildings/mage_tower.png",
          "tilesets/summer/human/buildings/mage_tower.png", "tilesets/swamp/human/buildings/mage_tower.png" },
        // unit-temple-of-the-damned
        { "tilesets/summer/orc/buildings/temple_of_the_damned.png", "tilesets/winter/orc/buildings/temple_of_the_damned.png",
          "tilesets/summer/orc/buildings/temple_of_the_damned.png", "tilesets/swamp/orc/buildings/temple_of_the_damned.png" },
        // unit-human-blacksmith
        { "tilesets/summer/human/buildings/blacksmith.png", "tilesets/winter/human/buildings/blacksmith.png",
          "tilesets/summer/human/buildings/blacksmith.png", "tilesets/swamp/human/buildings/blacksmith.png" },
        // unit-orc-blacksmith
        { "tilesets/summer/orc/buildings/blacksmith.png", "tilesets/winter/orc/buildings/blacksmith.png",
          "tilesets/summer/orc/buildings/blacksmith.png", "tilesets/swamp/orc/buildings/blacksmith.png" },
        // unit-human-refinery
        { "tilesets/summer/human/buildings/refinery.png", "tilesets/winter/human/buildings/refinery.png",
          "tilesets/summer/human/buildings/refinery.png", "tilesets/swamp/human/buildings/refinery.png" },
        // unit-orc-refinery
        { "tilesets/summer/orc/buildings/refinery.png", "tilesets/winter/orc/buildings/refinery.png",
          "tilesets/summer/orc/buildings/refinery.png", "tilesets/swamp/orc/buildings/refinery.png" },
        // unit-human-oil-platform
        { "tilesets/summer/human/buildings/oil_platform.png", "tilesets/winter/human/buildings/oil_platform.png",
          "tilesets/wasteland/human/buildings/oil_platform.png", "tilesets/swamp/human/buildings/oil_platform.png" },
        // unit-orc-oil-platform
        { "tilesets/summer/orc/buildings/oil_platform.png", "tilesets/winter/orc/buildings/oil_platform.png",
          "tilesets/wasteland/orc/buildings/oil_platform.png", "tilesets/swamp/orc/buildings/oil_platform.png" },
        // unit-keep
        { "tilesets/summer/human/buildings/keep.png", "tilesets/winter/human/buildings/keep.png",
          "tilesets/summer/human/buildings/keep.png", "tilesets/swamp/human/buildings/keep.png" },
        // unit-stronghold
        { "tilesets/summer/orc/buildings/stronghold.png", "tilesets/winter/orc/buildings/stronghold.png",
          "tilesets/summer/orc/buildings/stronghold.png", "tilesets/swamp/orc/buildings/stronghold.png" },
        // unit-castle
        { "tilesets/summer/human/buildings/castle.png", "tilesets/winter/human/buildings/castle.png",
          "tilesets/summer/human/buildings/castle.png", "tilesets/swamp/human/buildings/castle.png" },
        // unit-fortress
        { "tilesets/summer/orc/buildings/fortress.png", "tilesets/winter/orc/buildings/fortress.png",
          "tilesets/summer/orc/buildings/fortress.png", "tilesets/swamp/orc/buildings/fortress.png" },
        // unit-gold-mine
        { "tilesets/summer/neutral/buildings/gold_mine.png", "tilesets/winter/neutral/buildings/gold_mine.png",
          "tilesets/wasteland/neutral/buildings/gold_mine.png", "tilesets/swamp/neutral/buildings/gold_mine.png" },
        // unit-oil-patch
        { "tilesets/summer/neutral/buildings/oil_patch.png", "tilesets/summer/neutral/buildings/oil_patch.png",
          "tilesets/wasteland/neutral/buildings/oil_patch.png", "tilesets/swamp/neutral/buildings/oil_patch.png" },
        // unit-human-guard-tower
        { "tilesets/summer/human/buildings/guard_tower.png", "tilesets/winter/human/buildings/guard_tower.png",
          "tilesets/summer/human/buildings/guard_tower.png", "tilesets/swamp/human/buildings/guard_tower.png" },
        // unit-orc-guard-tower
        { "tilesets/summer/orc/buildings/guard_tower.png", "tilesets/winter/orc/buildings/guard_tower.png",
          "tilesets/summer/orc/buildings/guard_tower.png", "tilesets/swamp/orc/buildings/guard_tower.png" },
        // unit-human-cannon-tower
        { "tilesets/summer/human/buildings/cannon_tower.png", "tilesets/winter/human/buildings/cannon_tower.png",
          "tilesets/summer/human/buildings/cannon_tower.png", "tilesets/swamp/human/buildings/cannon_tower.png" },
        // unit-orc-cannon-tower
        { "tilesets/summer/orc/buildings/cannon_tower.png", "tilesets/winter/orc/buildings/cannon_tower.png",
          "tilesets/summer/orc/buildings/cannon_tower.png", "tilesets/swamp/orc/buildings/cannon_tower.png" },
        // unit-circle-of-power
        { "tilesets/summer/neutral/buildings/circle_of_power.png", "tilesets/winter/neutral/buildings/circle_of_power.png",
          "tilesets/wasteland/neutral/buildings/circle_of_power.png", "tilesets/swamp/neutral/buildings/circle_of_power.png" },
        // unit-dark-portal
        { "tilesets/summer/neutral/buildings/dark_portal.png", "tilesets/winter/neutral/buildings/dark_portal.png",
          "tilesets/wasteland/neutral/buildings/dark_portal.png", "tilesets/swamp/neutral/buildings/dark_portal.png" },
        // unit-runestone
        { "neutral/buildings/runestone.png", "tilesets/winter/neutral/buildings/runestone.png",
          "neutral/buildings/runestone.png", "tilesets/swamp/neutral/buildings/runestone.png" },
    };

    private UnitFrames() {}

    /** The place of a unit type in TYPES, or -1 for a type without a picture. */
    public static int indexOf(int type) {
        return (type < 0 || type >= INDEX.length) ? -1 : INDEX[type];
    }

    /** The place in TYPES by type number, -1 for none (looked up for every unit drawn). */
    private static final int[] INDEX = new int[256];
    static {
        java.util.Arrays.fill(INDEX, -1);
        for (int i = 0; i < TYPES.length; ++i) {
            INDEX[TYPES[i]] = i;
        }
    }

    /** The picture file of a unit type on a tileset, or null. */
    public static String file(int type, int tileset) {
        final int i = indexOf(type);
        return (i < 0 || tileset < 0 || tileset > 3) ? null : FILES[i][(FILES[i].length == 1) ? 0 : tileset];
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

    /** The biome letter of the engine's CurrentEffectTileset by the PUD's tileset number (forest, winter, swamp, xswamp). */
    private static final String[] BIOME = { "_bf", "_bs", "_bl", "_bx" };

    /**
     * Frame n of a family of a sheet, tried in the spellings of the engine's HDFrameByNumber
     * (src/unit/unittype.cpp), each only when the earlier ones are absent: PREFIX_N, PREFIX_fN
     * (Circle of Power, whose prefix holds the number), PREFIX_N_f0 (a frame that ships only as
     * a cycle: winter Church, winter Goblin Alchemist), PREFIX_N_f0_bBIOME. The suffix ("" or
     * "_team") follows each. Null when none is there.
     */
    public static int[] frameOf(String atlasJson, String prefix, int n, String suffix, int tileset) {
        int[] f = frame(atlasJson, prefix + "_" + n + suffix);
        if (f == null) {
            f = frame(atlasJson, prefix + "_f" + n + suffix);
        }
        if (f == null) {
            f = frame(atlasJson, prefix + "_" + n + "_f0" + suffix);
        }
        if (f == null && tileset >= 0 && tileset < BIOME.length) {
            f = frame(atlasJson, prefix + "_" + n + "_f0" + BIOME[tileset] + suffix);
        }
        return f;
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
