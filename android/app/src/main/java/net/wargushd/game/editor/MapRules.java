package net.wargushd.game.editor;

import java.util.ArrayList;
import java.util.List;

/**
 * Where a unit may stand on a map, the steps that place, move and remove one, and the checks a
 * map is put through before it is saved. Nothing here knows the screen or Android.
 *
 * The ground classes are those of the tileset scripts' flags: a solid tile by its terrain, a
 * mixed tile by its pair. A gold mine wants buildable land under all of its 3 x 3 tiles, an oil
 * patch water under all of its 3 x 3, a start location and a worker land; no two units share a
 * tile; every unit lies inside the map. A hall takes 4 x 4 tiles (TileSize of the unit scripts)
 * and is held to the mine's rule, buildable land.
 *
 * A start location is a mark, not a body: the game's own maps have the hall or a worker standing
 * on it, so it takes no tile and may lie under a unit. Of a unit of a kind the palette does not
 * have, only its top left tile is known to be taken: nothing is placed on that tile, and the
 * checks say nothing of how such a unit lies to others. The game's maps also give start
 * locations to slots that are to be rescued, so only a start location of nobody is a fault.
 */
public final class MapRules {
    public static final int PEASANT = 0x02, PEON = 0x03, TOWN_HALL = 0x4A, GREAT_HALL = 0x4B;
    public static final int GOLD_MINE = 0x5C, OIL_PATCH = 0x5D, HUMAN_START = 0x5E, ORC_START = 0x5F;
    /** The last unit type number the map loader has a name for. */
    public static final int LAST_TYPE = 0x68;
    /** The slots a player can have. */
    public static final int PLAYERS = 8;

    /** What the palette of units offers. */
    public static final int KIND_MINE = 0, KIND_PATCH = 1, KIND_START = 2, KIND_WORKER = 3, KIND_HALL = 4;
    public static final int KINDS = 5;

    /** AI numbers of the map file: land attack, passive, sea attack, air attack. */
    public static final int AI_LAND = 0, AI_PASSIVE = 1, AI_SEA = 25, AI_AIR = 26;
    /** What a slot gets when its first unit is placed and it has no resources at all. */
    public static final int START_GOLD = 2000, START_LUMBER = 1000, START_OIL = 1000;
    /**
     * What every slot of a new map holds: the amounts the game's own maps carry in the slots
     * their makers never set (the slots no player can have).
     */
    public static final int NEW_GOLD = 1000, NEW_LUMBER = 1000, NEW_OIL = 1000;
    /** The amounts a new gold mine and a new oil patch hold, in units of 2500. */
    public static final int MINE_AMOUNT = 18, PATCH_AMOUNT = 8;
    /** A unit that is not a resource: the data word of an active unit. */
    private static final int ACTIVE = 1;

    private static final int MOVE_WATER = 0, MOVE_COAST = 1, MOVE_LAND = 2, MOVE_OTHER = 3;
    private static final int GROUND_ANY = 0, GROUND_LAND = 1, GROUND_BUILD = 2, GROUND_WATER = 3;

    private MapRules() {}

    /** A new map: every tile the given number, no units, no players, every slot with the new map's amounts. */
    public static MapModel blank(int width, int height, int tileset, int tile) {
        final MapModel m = new MapModel(width, height, tileset, tile);
        for (int s = 0; s < MapModel.SLOTS; ++s) {
            m.loadSlot(MapModel.FIELD_GOLD, s, NEW_GOLD);
            m.loadSlot(MapModel.FIELD_LUMBER, s, NEW_LUMBER);
            m.loadSlot(MapModel.FIELD_OIL, s, NEW_OIL);
        }
        return m;
    }

    /**
     * A slot's owner changed, as one step; null when it is already so. A slot that becomes a
     * player's and has no resources at all gets the usual starting resources.
     */
    public static MapModel.Step owner(MapModel m, int slot, int owner) {
        final MapModel.Step first = m.changeSlot(MapModel.FIELD_OWNER, slot, owner);
        final boolean player = owner == MapModel.OWNER_PERSON || owner == MapModel.OWNER_COMPUTER;
        if (first == null || !player || m.slot(MapModel.FIELD_GOLD, slot) != 0
            || m.slot(MapModel.FIELD_LUMBER, slot) != 0 || m.slot(MapModel.FIELD_OIL, slot) != 0) {
            return first;
        }
        final ArrayList<MapModel.Step> parts = new ArrayList<MapModel.Step>();
        parts.add(first);
        parts.add(m.changeSlot(MapModel.FIELD_GOLD, slot, START_GOLD));
        parts.add(m.changeSlot(MapModel.FIELD_LUMBER, slot, START_LUMBER));
        parts.add(m.changeSlot(MapModel.FIELD_OIL, slot, START_OIL));
        return new MapModel.GroupStep(parts);
    }

    // ----- unit kinds -----

    /** Tiles a unit of this type takes along each side. */
    public static int footprint(int type) {
        if (type == GOLD_MINE || type == OIL_PATCH) {
            return 3;
        }
        return (type == TOWN_HALL || type == GREAT_HALL) ? 4 : 1;
    }

    public static boolean isStart(int type) { return type == HUMAN_START || type == ORC_START; }
    public static boolean isResource(int type) { return type == GOLD_MINE || type == OIL_PATCH; }

    /** True for the kinds the palette has and whose footprint is therefore known here. */
    private static boolean known(int type) {
        return isResource(type) || type == PEASANT || type == PEON || type == TOWN_HALL || type == GREAT_HALL;
    }

    /** The file's type number of a palette kind for a race. */
    public static int typeOf(int kind, int race) {
        final boolean orc = race == MapModel.RACE_ORC;
        switch (kind) {
            case KIND_MINE: return GOLD_MINE;
            case KIND_PATCH: return OIL_PATCH;
            case KIND_START: return orc ? ORC_START : HUMAN_START;
            case KIND_WORKER: return orc ? PEON : PEASANT;
            default: return orc ? GREAT_HALL : TOWN_HALL;
        }
    }

    /** The same unit of the other race, or the type itself when it has none here. */
    private static int ofRace(int type, int race) {
        final boolean orc = race == MapModel.RACE_ORC;
        switch (type) {
            case PEASANT: case PEON: return orc ? PEON : PEASANT;
            case TOWN_HALL: case GREAT_HALL: return orc ? GREAT_HALL : TOWN_HALL;
            case HUMAN_START: case ORC_START: return orc ? ORC_START : HUMAN_START;
            default: return type;
        }
    }

    /** The palette kind of a unit type, or -1 for a type the palette has not. */
    public static int kindOf(int type) {
        switch (type) {
            case GOLD_MINE: return KIND_MINE;
            case OIL_PATCH: return KIND_PATCH;
            case HUMAN_START: case ORC_START: return KIND_START;
            case PEASANT: case PEON: return KIND_WORKER;
            case TOWN_HALL: case GREAT_HALL: return KIND_HALL;
            default: return -1;
        }
    }

    // ----- ground -----

    private static int move(int tile) {
        final int hi = tile >> 8;
        if (hi == 0) {
            final int slot = (tile >> 4) & 0xF;
            return (slot == 1 || slot == 2) ? MOVE_WATER : (slot >= 3 && slot <= 6) ? MOVE_LAND : MOVE_OTHER;
        }
        return (hi == 1) ? MOVE_WATER : (hi == 2) ? MOVE_COAST : (hi == 3 || hi == 5 || hi == 6) ? MOVE_LAND
            : MOVE_OTHER;
    }

    /** True for grass and dark grass and the tiles mixed of the two. */
    public static boolean buildable(int tile) {
        final int hi = tile >> 8;
        if (hi == 0) {
            final int slot = (tile >> 4) & 0xF;
            return slot == 5 || slot == 6;
        }
        return hi == 6;
    }

    public static boolean land(int tile) { return move(tile) == MOVE_LAND; }
    public static boolean water(int tile) { return move(tile) == MOVE_WATER; }

    private static int ground(int type) {
        switch (type) {
            case GOLD_MINE: case TOWN_HALL: case GREAT_HALL: return GROUND_BUILD;
            case OIL_PATCH: return GROUND_WATER;
            case HUMAN_START: case ORC_START: case PEASANT: case PEON: return GROUND_LAND;
            default: return GROUND_ANY;
        }
    }

    private static boolean groundFits(int type, int tile) {
        switch (ground(type)) {
            case GROUND_BUILD: return buildable(tile);
            case GROUND_WATER: return water(tile);
            case GROUND_LAND: return land(tile);
            default: return true;
        }
    }

    // ----- placing -----

    /** True when one tile could carry a tile of a unit of this type: inside the map, right ground. */
    public static boolean tileFits(MapModel m, int type, int x, int y) {
        return x >= 0 && y >= 0 && x < m.width() && y < m.height() && groundFits(type, m.tile(x, y));
    }

    /** The unit lying on a tile, or -1: the smallest there (a start location under a hall), the last placed first. */
    public static int unitAt(MapModel m, int x, int y) {
        int found = -1, least = Integer.MAX_VALUE;
        for (int i = m.unitCount() - 1; i >= 0; --i) {
            final MapModel.Unit u = m.unit(i);
            final int size = footprint(u.type);
            if (x >= u.x && y >= u.y && x < u.x + size && y < u.y + size && size < least) {
                found = i;
                least = size;
            }
        }
        return found;
    }

    /** True when a tile is taken by a unit other than the one left out (-1 for none). */
    public static boolean taken(MapModel m, int x, int y, int leftOut) {
        for (int i = 0; i < m.unitCount(); ++i) {
            final MapModel.Unit u = m.unit(i);
            if (i == leftOut || isStart(u.type)) {
                continue;
            }
            final int size = footprint(u.type);
            if (x >= u.x && y >= u.y && x < u.x + size && y < u.y + size) {
                return true;
            }
        }
        return false;
    }

    /**
     * True when a unit of this type may have its top left tile at (x, y): the whole footprint
     * inside the map, on the ground the type wants, on no tile of another unit (a start location
     * takes none and may lie under one). The unit at index leftOut is not counted (the unit moved).
     */
    public static boolean mayPlace(MapModel m, int type, int x, int y, int leftOut) {
        final int size = footprint(type);
        if (x < 0 || y < 0 || x + size > m.width() || y + size > m.height()) {
            return false;
        }
        for (int yy = y; yy < y + size; ++yy) {
            for (int xx = x; xx < x + size; ++xx) {
                if (!groundFits(type, m.tile(xx, yy)) || (!isStart(type) && taken(m, xx, yy, leftOut))) {
                    return false;
                }
            }
        }
        return true;
    }

    /** The start location of a slot, or -1. */
    public static int startOf(MapModel m, int slot) {
        for (int i = 0; i < m.unitCount(); ++i) {
            if (isStart(m.unit(i).type) && m.unit(i).owner == slot) {
                return i;
            }
        }
        return -1;
    }

    private static boolean used(MapModel m, int slot) {
        final int o = m.slot(MapModel.FIELD_OWNER, slot);
        return o == MapModel.OWNER_PERSON || o == MapModel.OWNER_COMPUTER;
    }

    /**
     * A unit of a palette kind placed for a slot, as one step; null where it may not stand.
     * A resource goes to the neutral slot. A slot's start location is moved, not doubled. A slot
     * owned by nobody becomes a person's, and gets the usual starting resources when it has none.
     */
    public static MapModel.Step place(MapModel m, int kind, int slot, int x, int y) {
        final boolean resource = kind == KIND_MINE || kind == KIND_PATCH;
        if (!resource && (slot < 0 || slot >= PLAYERS)) {
            return null;
        }
        final int owner = resource ? MapModel.NEUTRAL_SLOT : slot;
        final int type = typeOf(kind, resource ? MapModel.RACE_HUMAN : m.slot(MapModel.FIELD_RACE, slot));
        final int old = (kind == KIND_START) ? startOf(m, slot) : -1;
        if (!mayPlace(m, type, x, y, old)) {
            return null;
        }
        final int data = (kind == KIND_MINE) ? MINE_AMOUNT : (kind == KIND_PATCH) ? PATCH_AMOUNT
            : (kind == KIND_START) ? 0 : ACTIVE;
        final ArrayList<MapModel.Step> parts = new ArrayList<MapModel.Step>();
        if (old >= 0) {
            parts.add(m.changeUnit(old, new MapModel.Unit(x, y, type, owner, m.unit(old).data)));
            if (parts.get(0) == null) {
                return null;
            }
        } else {
            parts.add(m.placeUnit(new MapModel.Unit(x, y, type, owner, data)));
        }
        if (!resource && !used(m, slot)) {
            parts.add(m.changeSlot(MapModel.FIELD_OWNER, slot, MapModel.OWNER_PERSON));
            if (m.slot(MapModel.FIELD_GOLD, slot) == 0 && m.slot(MapModel.FIELD_LUMBER, slot) == 0
                && m.slot(MapModel.FIELD_OIL, slot) == 0) {
                parts.add(m.changeSlot(MapModel.FIELD_GOLD, slot, START_GOLD));
                parts.add(m.changeSlot(MapModel.FIELD_LUMBER, slot, START_LUMBER));
                parts.add(m.changeSlot(MapModel.FIELD_OIL, slot, START_OIL));
            }
        }
        return (parts.size() == 1) ? parts.get(0) : new MapModel.GroupStep(parts);
    }

    /** A unit moved; null where it may not stand or when it is already there. */
    public static MapModel.Step move(MapModel m, int index, int x, int y) {
        final MapModel.Unit u = m.unit(index);
        if (!mayPlace(m, u.type, x, y, index)) {
            return null;
        }
        return m.changeUnit(index, new MapModel.Unit(x, y, u.type, u.owner, u.data));
    }

    public static MapModel.Step erase(MapModel m, int index) {
        return m.removeUnit(index);
    }

    /** The amount of a gold mine or an oil patch changed by so many units of 2500; never below one. */
    public static MapModel.Step amount(MapModel m, int index, int by) {
        final MapModel.Unit u = m.unit(index);
        if (!isResource(u.type)) {
            return null;
        }
        final int data = Math.max(1, Math.min(0xFFFF, u.data + by));
        return m.changeUnit(index, new MapModel.Unit(u.x, u.y, u.type, u.owner, data));
    }

    /**
     * A slot's race changed; its start location, workers and halls become those of the race.
     * Null when the slot is already of that race.
     */
    public static MapModel.Step race(MapModel m, int slot, int race) {
        final ArrayList<MapModel.Step> parts = new ArrayList<MapModel.Step>();
        final MapModel.Step first = m.changeSlot(MapModel.FIELD_RACE, slot, race);
        if (first == null) {
            return null;
        }
        parts.add(first);
        for (int i = 0; i < m.unitCount(); ++i) {
            final MapModel.Unit u = m.unit(i);
            if (u.owner == slot && ofRace(u.type, race) != u.type) {
                parts.add(m.changeUnit(i, new MapModel.Unit(u.x, u.y, ofRace(u.type, race), u.owner, u.data)));
            }
        }
        return (parts.size() == 1) ? first : new MapModel.GroupStep(parts);
    }

    // ----- checks before a map is saved -----

    /** One thing wrong with a map: an English line with {0}.. places, where it is, whether it stops Save. */
    public static final class Fault {
        public final String text;
        public final Object[] values;
        /** A tile to show, or -1, -1 when the fault has no place. */
        public final int x, y;
        public final boolean blocks;

        Fault(boolean blocks, int x, int y, String text, Object... values) {
            this.blocks = blocks; this.x = x; this.y = y; this.text = text; this.values = values;
        }

        @Override public String toString() {
            String s = text;
            for (int i = 0; i < values.length; ++i) {
                s = s.replace("{" + i + "}", String.valueOf(values[i]));
            }
            return s;
        }
    }

    /** True when a list holds a fault that stops Save. */
    public static boolean blocked(List<Fault> faults) {
        for (Fault f : faults) {
            if (f.blocks) {
                return true;
            }
        }
        return false;
    }

    /**
     * The faults of the map's units and slots; empty for a map that may be saved without a word.
     * Slots are named as the player sees them, 1 to 8.
     */
    public static List<Fault> check(MapModel m) {
        final ArrayList<Fault> out = new ArrayList<Fault>();
        final int w = m.width(), h = m.height();
        final boolean[] taken = new boolean[w * h];     // by a unit of a kind whose footprint is known
        final boolean[] start = new boolean[MapModel.SLOTS];
        for (int i = 0; i < m.unitCount(); ++i) {
            final MapModel.Unit u = m.unit(i);
            final int size = footprint(u.type);
            if (u.x < 0 || u.y < 0 || u.x + size > w || u.y + size > h) {
                out.add(new Fault(true, Math.max(0, Math.min(w - 1, u.x)), Math.max(0, Math.min(h - 1, u.y)),
                                  "A unit lies outside the map"));
                continue;
            }
            if (u.type > LAST_TYPE) {
                out.add(new Fault(true, u.x, u.y, "A unit of a kind the game does not know"));
            }
            boolean ground = true, overlap = false;
            for (int yy = u.y; yy < u.y + size; ++yy) {
                for (int xx = u.x; xx < u.x + size; ++xx) {
                    ground &= groundFits(u.type, m.tile(xx, yy));
                    if (known(u.type)) {
                        overlap |= taken[yy * w + xx];
                        taken[yy * w + xx] = true;
                    }
                }
            }
            if (!ground) {
                out.add(new Fault(true, u.x, u.y,
                    (u.type == GOLD_MINE) ? "A gold mine is not on buildable land"
                    : (u.type == OIL_PATCH) ? "An oil patch is not on water"
                    : (ground(u.type) == GROUND_BUILD) ? "A hall is not on buildable land"
                    : "A unit is not on land"));
            }
            if (overlap) {
                out.add(new Fault(true, u.x, u.y, "Units overlap"));
            }
            if (isStart(u.type)) {
                if (u.owner >= PLAYERS || m.slot(MapModel.FIELD_OWNER, u.owner) == MapModel.OWNER_NOBODY
                    || m.slot(MapModel.FIELD_OWNER, u.owner) == MapModel.OWNER_NEUTRAL) {
                    out.add(new Fault(true, u.x, u.y, "A start location of a slot that has no player"));
                } else if (start[u.owner]) {
                    out.add(new Fault(true, u.x, u.y, "Player {0} has two start locations", u.owner + 1));
                }
                if (u.owner < MapModel.SLOTS) {
                    start[u.owner] = true;
                }
            }
            if (isResource(u.type)) {
                if (u.owner != MapModel.NEUTRAL_SLOT) {
                    out.add(new Fault(true, u.x, u.y, "A resource is not owned by the neutral slot"));
                }
                if (u.data == 0) {
                    out.add(new Fault(true, u.x, u.y, "A resource holds nothing"));
                }
            }
        }
        int players = 0, persons = 0;
        for (int s = 0; s < PLAYERS; ++s) {
            if (used(m, s)) {
                ++players;
                persons += (m.slot(MapModel.FIELD_OWNER, s) == MapModel.OWNER_PERSON) ? 1 : 0;
                if (!start[s]) {
                    out.add(new Fault(true, -1, -1, "Player {0} has no start location", s + 1));
                }
            }
        }
        if (m.slot(MapModel.FIELD_OWNER, MapModel.NEUTRAL_SLOT) != MapModel.OWNER_NEUTRAL) {
            out.add(new Fault(true, -1, -1, "The neutral slot is not neutral"));
        }
        if (persons == 0) {
            out.add(new Fault(true, -1, -1, "No slot is a person's"));
        }
        if (players < 2) {
            out.add(new Fault(false, -1, -1, "Fewer than two players"));
        }
        return out;
    }
}
