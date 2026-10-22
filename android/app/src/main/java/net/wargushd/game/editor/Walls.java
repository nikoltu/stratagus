package net.wargushd.game.editor;

/**
 * Human and orc walls as the map's tiles hold them. A wall is not part of the corner model: a
 * wall tile is chosen by which of its four neighbours are walls of its own race, as the engine
 * does it (src/map/map_wall.cpp GetDirectionFromSurrounding, getWallTile; the tables of
 * src/map/script_tileset.cpp buildWallReplacementTable). Nothing here knows the screen or Android.
 *
 * Direction bits: 1 up, 2 right, 4 down, 8 left; a side off the map counts as a wall. A wall
 * placed in the editor is a whole wall: the table's tile itself (its first variant), as the
 * engine's SetWall gives a wall of full hit points.
 */
public final class Walls {
    public static final int HUMAN = 0, ORC = 1;

    /** The tile of a whole wall by its direction bits (engine tables, 1:1). */
    private static final int[] HUMAN_TABLE = { 0x090, 0x830, 0x810, 0x850, 0x800, 0x840, 0x820, 0x860,
        0x870, 0x8B0, 0x890, 0x8D0, 0x880, 0x8C0, 0x8A0, 0x0B0 };
    private static final int[] ORC_TABLE = { 0x0A0, 0x930, 0x910, 0x950, 0x900, 0x940, 0x920, 0x960,
        0x970, 0x9B0, 0x990, 0x9D0, 0x980, 0x9C0, 0x9A0, 0x0C0 };

    private Walls() {}

    /** HUMAN or ORC for a wall tile (whole, broken or destroyed variant), -1 for any other tile. */
    public static int raceOf(int tile) {
        final int hi = tile >> 8, sub = (tile >> 4) & 0xF;
        if (hi == 8 && sub <= 13) {
            return HUMAN;
        }
        if (hi == 9 && sub <= 13) {
            return ORC;
        }
        if (hi == 0) {
            if (sub == 9 || sub == 11) {
                return HUMAN;
            }
            if (sub == 10 || sub == 12) {
                return ORC;
            }
        }
        return -1;
    }

    public static boolean isWall(int tile) { return raceOf(tile) >= 0; }

    /** The direction bits of a tile of a race among the tiles of a map (row by row, w x h). */
    public static int direction(int[] tiles, int w, int h, int x, int y, int race) {
        int dir = 0;
        final int[] dx = { 0, 1, 0, -1 }, dy = { -1, 0, 1, 0 };
        for (int i = 0; i < 4; ++i) {
            final int nx = x + dx[i], ny = y + dy[i];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || raceOf(tiles[ny * w + nx]) == race) {
                dir |= 1 << i;
            }
        }
        return dir;
    }

    /** The whole wall's tile of a race for direction bits. */
    public static int base(int race, int dir) {
        return ((race == ORC) ? ORC_TABLE : HUMAN_TABLE)[dir & 15];
    }

    /** The direction a wall tile stands for (the engine's getWallDirection: the base looked up). */
    public static int directionOf(int tile, int race) {
        final int b = tile & 0xFF0;
        final int[] table = (race == ORC) ? ORC_TABLE : HUMAN_TABLE;
        for (int i = 0; i < 16; ++i) {
            if (table[i] == b) {
                return i;
            }
        }
        return 0;
    }

    /**
     * The tile a wall of a race gets for its direction bits. Where the tileset has no picture
     * for it, the wall keeps the direction its old tile had, as the engine does (getWallTile
     * with oldTile); a new wall's old tile is the plain one of direction 0 (SetWall).
     *
     * @param terrain the tileset, or null to take every tile as having a picture
     */
    public static int tileFor(int race, int dir, int old, Terrain terrain) {
        final int want = base(race, dir);
        if (terrain == null || terrain.hasPicture(want)) {
            return want;
        }
        return base(race, (raceOf(old) == race) ? directionOf(old, race) : 0);
    }
}
