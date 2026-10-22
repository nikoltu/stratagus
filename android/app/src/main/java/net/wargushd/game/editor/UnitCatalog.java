package net.wargushd.game.editor;

/**
 * The units the editor's stage 1b palette offers (owner decision 88, 5B): every unit type of the
 * map file that the HD unit scripts define, by race and by group, with the tiles it takes and
 * the ground it wants. No Android class.
 *
 * Where it is taken from (EDITOR_STATUS.md, Session 2026-10-19 c):
 * - the type numbers and idents: the engine's PUD table, UnitScriptNames of src/map/pud_convert.cpp;
 * - race: the script the ident is defined in (scripts/human/units.lua, scripts/orc/units.lua,
 *   scripts/units.lua for the neutral ones) of the HD source (fulltree-changes);
 * - tiles: TileSize; the group from the type's flags, in this order: a neutral type with
 *   GivesResource - Resources; Building with ShoreBuilding or Type "naval" - Shore Buildings;
 *   other Building - Land Buildings; a race's unit that no "train-unit" button of its race's
 *   buttons.lua trains and no spell of spells.lua summons - Heroes; AirUnit - Air Units;
 *   SeaUnit - Sea Units; the rest - Land Units. The start location is put first in Land Units.
 * - left out: a type whose Icon an earlier type of the table already has (the -super towers
 *   share numbers anyway; the attack peasant / peon of 16, 17 are such), the walls (painted as
 *   terrain), the table's empty numbers.
 * The JVM test reads the same scripts and checks every row of this table against them.
 *
 * The ground (where it may stand, engine rules of unittype.cpp / build.cpp for the editor, which
 * leaves the building rules out): a land unit on land, a sea unit on water, an air unit anywhere
 * (on a layer of its own), a land building on buildable land, a shore building on water or coast
 * with at least one coast tile, a naval building (oil platform, oil patch) on water.
 */
public final class UnitCatalog {
    public static final int HUMAN = 0, ORC = 1, NEUTRAL = 2, RACES = 3;

    public static final int LAND_UNITS = 0, SEA_UNITS = 1, AIR_UNITS = 2, LAND_BUILDINGS = 3, SHORE_BUILDINGS = 4,
        HEROES = 5, RESOURCES = 6;
    /** The groups' headers, in the launcher's own words, by group number. */
    public static final String[] GROUP_NAME = { "Land Units", "Sea Units", "Air Units", "Land Buildings",
        "Shore Buildings", "Heroes", "Resources" };
    /** The order the groups are shown in. */
    private static final int[] GROUP_ORDER = { RESOURCES, LAND_UNITS, SEA_UNITS, AIR_UNITS, LAND_BUILDINGS,
        SHORE_BUILDINGS, HEROES };

    /** The ground a type wants under each of its tiles. */
    public static final int GROUND_ANY = 0, GROUND_LAND = 1, GROUND_BUILD = 2, GROUND_WATER = 3, GROUND_SHORE = 4;

    private static final int H = HUMAN, O = ORC, N = NEUTRAL;
    private static final int LU = LAND_UNITS, SU = SEA_UNITS, AU = AIR_UNITS, LB = LAND_BUILDINGS,
        SB = SHORE_BUILDINGS, HE = HEROES, RE = RESOURCES;
    private static final int AN = GROUND_ANY, LA = GROUND_LAND, BU = GROUND_BUILD, WA = GROUND_WATER,
        SH = GROUND_SHORE;

    /** type, race, group, tiles a side, ground; in the order of the PUD table. */
    private static final int[][] ROWS = {
        { 0x00, H, LU, 1, LA }, { 0x01, O, LU, 1, LA }, { 0x02, H, LU, 1, LA }, { 0x03, O, LU, 1, LA },
        { 0x04, H, LU, 1, LA }, { 0x05, O, LU, 1, LA }, { 0x06, H, LU, 1, LA }, { 0x07, O, LU, 1, LA },
        { 0x08, H, LU, 1, LA }, { 0x09, O, LU, 1, LA }, { 0x0A, H, LU, 1, LA }, { 0x0B, O, LU, 1, LA },
        { 0x0C, H, LU, 1, LA }, { 0x0D, O, LU, 1, LA }, { 0x0E, H, LU, 1, LA }, { 0x0F, O, LU, 1, LA },
        { 0x12, H, LU, 1, LA }, { 0x13, O, LU, 1, LA },
        { 0x14, H, HE, 1, LA }, { 0x15, O, HE, 1, LA }, { 0x16, H, HE, 1, AN }, { 0x17, O, HE, 1, LA },
        { 0x18, H, HE, 1, LA }, { 0x19, O, HE, 1, LA },
        { 0x1A, H, SU, 1, WA }, { 0x1B, O, SU, 1, WA }, { 0x1C, H, SU, 1, WA }, { 0x1D, O, SU, 1, WA },
        { 0x1E, H, SU, 1, WA }, { 0x1F, O, SU, 1, WA }, { 0x20, H, SU, 1, WA }, { 0x21, O, SU, 1, WA },
        { 0x23, O, HE, 1, AN },
        { 0x26, H, SU, 1, WA }, { 0x27, O, SU, 1, WA },
        { 0x28, H, AU, 1, AN }, { 0x29, O, AU, 1, AN }, { 0x2A, H, AU, 1, AN }, { 0x2B, O, AU, 1, AN },
        { 0x2C, H, HE, 1, LA }, { 0x2D, O, AU, 1, AN }, { 0x2E, H, HE, 1, LA }, { 0x2F, O, HE, 1, LA },
        { 0x31, O, HE, 1, LA }, { 0x32, H, HE, 1, LA }, { 0x33, O, HE, 1, LA }, { 0x34, H, HE, 1, LA },
        { 0x35, O, HE, 1, LA },
        { 0x37, O, LU, 1, LA }, { 0x38, N, AU, 1, AN }, { 0x39, N, LU, 1, LA },
        { 0x3A, H, LB, 2, BU }, { 0x3B, O, LB, 2, BU }, { 0x3C, H, LB, 3, BU }, { 0x3D, O, LB, 3, BU },
        { 0x3E, H, LB, 3, BU }, { 0x3F, O, LB, 3, BU }, { 0x40, H, LB, 2, BU }, { 0x41, O, LB, 2, BU },
        { 0x42, H, LB, 3, BU }, { 0x43, O, LB, 3, BU }, { 0x44, H, LB, 3, BU }, { 0x45, O, LB, 3, BU },
        { 0x46, H, LB, 3, BU }, { 0x47, O, LB, 3, BU }, { 0x48, H, SB, 3, SH }, { 0x49, O, SB, 3, SH },
        { 0x4A, H, LB, 4, BU }, { 0x4B, O, LB, 4, BU }, { 0x4C, H, LB, 3, BU }, { 0x4D, O, LB, 3, BU },
        { 0x4E, H, SB, 3, SH }, { 0x4F, O, SB, 3, SH }, { 0x50, H, LB, 3, BU }, { 0x51, O, LB, 3, BU },
        { 0x52, H, LB, 3, BU }, { 0x53, O, LB, 3, BU }, { 0x54, H, SB, 3, SH }, { 0x55, O, SB, 3, SH },
        { 0x56, H, SB, 3, WA }, { 0x57, O, SB, 3, WA }, { 0x58, H, LB, 4, BU }, { 0x59, O, LB, 4, BU },
        { 0x5A, H, LB, 4, BU }, { 0x5B, O, LB, 4, BU },
        { 0x5C, N, RE, 3, BU }, { 0x5D, N, RE, 3, WA },
        { 0x5E, H, LU, 1, LA }, { 0x5F, O, LU, 1, LA },
        { 0x60, H, LB, 2, BU }, { 0x61, O, LB, 2, BU }, { 0x62, H, LB, 2, BU }, { 0x63, O, LB, 2, BU },
        { 0x64, N, LB, 2, BU }, { 0x65, N, LB, 4, BU }, { 0x66, N, LB, 2, BU },
    };

    /** The idents of the rows (pud_convert.cpp), for the test against the scripts. */
    public static final String[] IDENT = {
        "unit-footman", "unit-grunt", "unit-peasant", "unit-peon", "unit-ballista", "unit-catapult",
        "unit-knight", "unit-ogre", "unit-archer", "unit-axethrower", "unit-mage", "unit-death-knight",
        "unit-paladin", "unit-ogre-mage", "unit-dwarves", "unit-goblin-sappers", "unit-ranger", "unit-berserker",
        "unit-female-hero", "unit-evil-knight", "unit-flying-angel", "unit-fad-man", "unit-white-mage",
        "unit-beast-cry", "unit-human-oil-tanker", "unit-orc-oil-tanker", "unit-human-transport",
        "unit-orc-transport", "unit-human-destroyer", "unit-orc-destroyer", "unit-battleship",
        "unit-ogre-juggernaught", "unit-fire-breeze", "unit-human-submarine", "unit-orc-submarine",
        "unit-balloon", "unit-zeppelin", "unit-gryphon-rider", "unit-dragon", "unit-knight-rider",
        "unit-eye-of-vision", "unit-arthor-literios", "unit-quick-blade", "unit-double-head", "unit-wise-man",
        "unit-ice-bringer", "unit-man-of-light", "unit-sharp-axe", "unit-skeleton", "unit-daemon", "unit-critter",
        "unit-farm", "unit-pig-farm", "unit-human-barracks", "unit-orc-barracks", "unit-church",
        "unit-altar-of-storms", "unit-human-watch-tower", "unit-orc-watch-tower", "unit-stables",
        "unit-ogre-mound", "unit-inventor", "unit-alchemist", "unit-gryphon-aviary", "unit-dragon-roost",
        "unit-human-shipyard", "unit-orc-shipyard", "unit-town-hall", "unit-great-hall",
        "unit-elven-lumber-mill", "unit-troll-lumber-mill", "unit-human-foundry", "unit-orc-foundry",
        "unit-mage-tower", "unit-temple-of-the-damned", "unit-human-blacksmith", "unit-orc-blacksmith",
        "unit-human-refinery", "unit-orc-refinery", "unit-human-oil-platform", "unit-orc-oil-platform",
        "unit-keep", "unit-stronghold", "unit-castle", "unit-fortress", "unit-gold-mine", "unit-oil-patch",
        "unit-human-start-location", "unit-orc-start-location", "unit-human-guard-tower",
        "unit-orc-guard-tower", "unit-human-cannon-tower", "unit-orc-cannon-tower", "unit-circle-of-power",
        "unit-dark-portal", "unit-runestone",
    };

    /** The English names of the rows (Name of the scripts), shown where the game's string table has none. */
    public static final String[] NAME = {
        "Footman", "Grunt", "Peasant", "Peon", "Ballista", "Catapult", "Knight", "Ogre", "Archer",
        "Troll Axethrower", "Mage", "Death Knight", "Paladin", "Ogre Mage", "Dwarves", "Goblin Sappers", "Ranger",
        "Berserker", "Alleria", "Teron Gorefiend", "Kurdan and Sky'ree", "Dentarg", "Khadgar", "Grom Hellscream",
        "Oil tanker", "Oil tanker", "Transport", "Transport", "Elven Destroyer", "Troll Destroyer", "Battleship",
        "Ogre Juggernaught", "Deathwing", "Gnomish Submarine", "Giant Turtle", "Gnomish Flying Machine",
        "Goblin Zeppelin", "Gryphon Rider", "Dragon", "Turalyon", "Eye of Kilrogg", "Danath", "Korgath Bladefist",
        "Cho'gall", "Lothar", "Gul'dan", "Uther Lightbringer", "Zuljin", "Skeleton", "Daemon", "Critter", "Farm",
        "Pig Farm", "Barracks", "Barracks", "Church", "Altar of Storms", "Scout Tower", "Watch Tower", "Stables",
        "Ogre Mound", "Gnomish Inventor", "Goblin Alchemist", "Gryphon Aviary", "Dragon Roost", "Shipyard",
        "Shipyard", "Town Hall", "Great Hall", "Elven Lumber Mill", "Troll Lumber Mill", "Foundry", "Foundry",
        "Mage Tower", "Temple of the Damned", "Blacksmith", "Blacksmith", "Refinery", "Refinery", "Oil Platform",
        "Oil Platform", "Keep", "Stronghold", "Castle", "Fortress", "Gold Mine", "Oil Patch", "Start Location",
        "Start Location", "Guard Tower", "Guard Tower", "Cannon Tower", "Cannon Tower", "Circle of Power",
        "Dark Portal", "Runestone",
    };

    /** The row of each type number, -1 for a type the palette has not. */
    private static final int[] ROW_OF = new int[256];
    static {
        java.util.Arrays.fill(ROW_OF, -1);
        for (int i = 0; i < ROWS.length; ++i) {
            ROW_OF[ROWS[i][0]] = i;
        }
    }

    private UnitCatalog() {}

    /** How many types the palette has. */
    public static int count() { return ROWS.length; }
    /** The type number of a row. */
    public static int typeAt(int row) { return ROWS[row][0]; }

    private static int row(int type) { return (type < 0 || type >= ROW_OF.length) ? -1 : ROW_OF[type]; }

    /** True for a type of the palette. */
    public static boolean has(int type) { return row(type) >= 0; }
    /** The race a type is shown under; NEUTRAL for a type the palette has not. */
    public static int race(int type) { return has(type) ? ROWS[row(type)][1] : NEUTRAL; }
    /** The group of a type, or -1. */
    public static int group(int type) { return has(type) ? ROWS[row(type)][2] : -1; }
    /** Tiles a unit of the type takes along each side; 1 for a type the palette has not. */
    public static int size(int type) { return has(type) ? ROWS[row(type)][3] : 1; }
    /** The ground a type wants; GROUND_ANY for a type the palette has not. */
    public static int ground(int type) { return has(type) ? ROWS[row(type)][4] : GROUND_ANY; }
    /** True for a type that flies: it shares no tile with a unit on the ground, nor they with it. */
    public static boolean flies(int type) { return has(type) && ROWS[row(type)][4] == GROUND_ANY; }
    public static String ident(int type) { return has(type) ? IDENT[row(type)] : null; }
    /** The English name of a type, or "" for a type the palette has not. */
    public static String name(int type) { return has(type) ? NAME[row(type)] : ""; }

    /** The start location of a race, shown first in its Land Units. */
    private static int startOf(int race) {
        return (race == HUMAN) ? MapRules.HUMAN_START : (race == ORC) ? MapRules.ORC_START : -1;
    }

    /** The types of one group of a race, in the order they are shown (the PUD table's, the start first). */
    public static int[] types(int race, int group) {
        int n = 0;
        for (int[] r : ROWS) {
            n += (r[1] == race && r[2] == group) ? 1 : 0;
        }
        final int[] out = new int[n];
        final int start = (group == LAND_UNITS) ? startOf(race) : -1;
        int k = 0;
        if (start >= 0) {
            out[k++] = start;
        }
        for (int[] r : ROWS) {
            if (r[1] == race && r[2] == group && r[0] != start) {
                out[k++] = r[0];
            }
        }
        return out;
    }

    /** The groups a race has, in the order they are shown; empty groups are left out. */
    public static int[] groups(int race) {
        int n = 0;
        for (int g : GROUP_ORDER) {
            n += (types(race, g).length > 0) ? 1 : 0;
        }
        final int[] out = new int[n];
        n = 0;
        for (int g : GROUP_ORDER) {
            if (types(race, g).length > 0) {
                out[n++] = g;
            }
        }
        return out;
    }

    /** The palette's race of a slot's race field (MapModel.RACE_*). */
    public static int ofSlotRace(int slotRace) { return (slotRace == MapModel.RACE_ORC) ? ORC : HUMAN; }
}
