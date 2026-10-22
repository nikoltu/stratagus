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
    /**
     * The per-unit Artificial Intelligence word of the original editor (ORIG60 item 3, 1 case:
     * status line "Footman(Passive)"; right-click opens "Artificial Intelligence Properties" with
     * Active / Passive, a new unit Passive). Mapped to the .pud record's data word of a unit that
     * holds no resource: Passive = non-zero (written as 1), Active = 0.
     * Why this way round (to be measured): a new unit is Passive in the original and the game's
     * own new-unit samples carry 1; the original's map loader makes a non-zero word a fixed guard
     * with its own tile as home (PASSIVE_AI.md) and the engine reads it as MapFixed
     * (SetUnitMapFixed) - "stays at its place", the hint's Passive.
     * TODO(active/passive, owner decision 93): UNMEASURED - which value the original editor
     * writes for each word (save one Active and one Passive unit, read the data words) and what
     * the flag does in a game. A loaded word is kept exactly as read (any non-zero shows Passive,
     * 2+ stays 2+ until the player switches it); only a switch writes 0 or 1.
     */
    public static final int PASSIVE = 1, ACTIVE = 0;
    public static final int HUMAN_PLATFORM = 0x56, ORC_PLATFORM = 0x57;

    /** Whether the unit's data word is the AI word (not an amount, not a start location). */
    public static boolean hasAiWord(int type) {
        return !isResource(type) && !isStart(type) && type != HUMAN_PLATFORM && type != ORC_PLATFORM;
    }

    /** Whether a unit's data word reads Passive (any non-zero). */
    public static boolean isPassive(int data) { return data != 0; }

    /**
     * Every chosen unit that carries the AI word set Passive or Active, as one step; a unit that
     * already reads that word keeps its value. Null when nothing changes.
     */
    public static MapModel.Step aiWordAll(MapModel m, int[] chosen, boolean passive) {
        final ArrayList<MapModel.Step> parts = new ArrayList<MapModel.Step>();
        for (int i : chosen) {
            final MapModel.Unit u = m.unit(i);
            if (!hasAiWord(u.type) || isPassive(u.data) == passive) {
                continue;
            }
            final MapModel.Step s = m.changeUnit(i, new MapModel.Unit(u.x, u.y, u.type, u.owner,
                                                                      passive ? PASSIVE : ACTIVE));
            if (s != null) {
                parts.add(s);
            }
        }
        return parts.isEmpty() ? null : (parts.size() == 1) ? parts.get(0) : new MapModel.GroupStep(parts);
    }

    private static final int MOVE_WATER = 0, MOVE_COAST = 1, MOVE_LAND = 2, MOVE_OTHER = 3;
    private static final int GROUND_ANY = UnitCatalog.GROUND_ANY, GROUND_LAND = UnitCatalog.GROUND_LAND,
        GROUND_BUILD = UnitCatalog.GROUND_BUILD, GROUND_WATER = UnitCatalog.GROUND_WATER,
        GROUND_SHORE = UnitCatalog.GROUND_SHORE;

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

    // ----- the controller of a slot (owner decision 93, 1A) -----

    /** The controllers the Players sheet's list offers for slots 1-8, in its order. */
    public static final int[] CONTROLLERS = { MapModel.OWNER_NOBODY, MapModel.OWNER_PERSON, MapModel.OWNER_COMPUTER,
        MapModel.OWNER_RESCUE_WAITS, MapModel.OWNER_RESCUE_FIGHTS, MapModel.OWNER_NEUTRAL };
    /** The game's text key of each controller's word, or null for a word of the launcher. */
    public static final String[] CONTROLLER_KEY = { null, null, "mp_computer", null, null, null };
    /** Each controller's word (the launcher's own, or the fallback of the game's key). */
    public static final String[] CONTROLLER_WORD = { "Nobody", "Person", "Computer", "Rescuable - waits",
        "Rescuable - fights", "Neutral" };
    /** One line of what each controller does in the game (owner board 1A). */
    public static final String[] CONTROLLER_MEANING = {
        "The slot is empty. No units, no start.",
        "A player plays it. Needs a start.",
        "The computer plays it against the persons. Needs a start.",
        "Its units stand still. They join the person who reaches them.",
        "Joins the same way. Until then its computer fights the computers.",
        "Nobody's side. Critters, portals and the like." };

    /** The place of a controller in {@link #CONTROLLERS}, or -1 for a number the list has not. */
    public static int controllerIndex(int owner) {
        for (int i = 0; i < CONTROLLERS.length; ++i) {
            if (CONTROLLERS[i] == owner) {
                return i;
            }
        }
        return -1;
    }

    /** A rescue kind (6 or 7): its units carry the take-over mark in the game. */
    public static boolean isRescue(int owner) {
        return owner == MapModel.OWNER_RESCUE_WAITS || owner == MapModel.OWNER_RESCUE_FIGHTS;
    }

    /**
     * The rescue kind of a slot: OWNER_RESCUE_WAITS, OWNER_RESCUE_FIGHTS, or 0 (not a player slot,
     * or not rescuable). What the canvas marks on the slot's units (owner decision 93, 2A).
     */
    public static int rescueOf(MapModel m, int slot) {
        if (slot < 0 || slot >= PLAYERS) {
            return 0;
        }
        final int o = m.slot(MapModel.FIELD_OWNER, slot);
        return isRescue(o) ? o : 0;
    }

    /** The race means something for this controller: Person, Computer and both rescue kinds. */
    public static boolean raceLive(int owner) {
        return owner == MapModel.OWNER_PERSON || owner == MapModel.OWNER_COMPUTER || isRescue(owner);
    }

    /** The engine runs an AI for this controller: Computer and Rescuable - fights. */
    public static boolean aiLive(int owner) {
        return owner == MapModel.OWNER_COMPUTER || owner == MapModel.OWNER_RESCUE_FIGHTS;
    }

    /** The starting goods mean something: Person, Computer and Rescuable - fights (to confirm, board 1A). */
    public static boolean goodsLive(int owner) {
        return owner == MapModel.OWNER_PERSON || owner == MapModel.OWNER_COMPUTER
            || owner == MapModel.OWNER_RESCUE_FIGHTS;
    }

    /**
     * A slot's owner changed, as one step; null when it is already so. A slot that becomes one
     * whose goods count ({@link #goodsLive}) and has no resources at all gets the usual starting
     * resources.
     */
    public static MapModel.Step owner(MapModel m, int slot, int owner) {
        final MapModel.Step first = m.changeSlot(MapModel.FIELD_OWNER, slot, owner);
        final boolean player = goodsLive(owner);
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
        // TileSize of the HD unit scripts (UnitCatalog); 1 for a type the palette has not. The
        // engine's export, while installed, for a type it has square.
        final EngineRuns.Unit u = engineUnit(type);
        return (u != null && u.w == u.h && u.w > 0) ? u.w : UnitCatalog.size(type);
    }

    /** The engine's tables the units are placed by, or null for the built-in rules (Session 2026-10-19 e). */
    private static volatile EngineRuns.Tables engine;

    /** Place units by the tables of an engine export from now on; null goes back to the built-in rules. */
    public static void useEngine(EngineRuns.Tables tables) { engine = tables; }
    /** The installed export, or null. */
    public static EngineRuns.Tables engine() { return engine; }

    /** The export's type of a type number, or null (no export, or the type not in it). */
    private static EngineRuns.Unit engineUnit(int type) {
        final EngineRuns.Tables t = engine;
        return (t == null) ? null : t.unit(UnitCatalog.ident(type));
    }

    public static boolean isStart(int type) { return type == HUMAN_START || type == ORC_START; }
    public static boolean isResource(int type) { return type == GOLD_MINE || type == OIL_PATCH; }

    /** True for the types the palette has and whose footprint is therefore known here (UnitCatalog). */
    private static boolean known(int type) {
        return UnitCatalog.has(type);
    }

    /** True for a unit that flies: it shares no tile with one on the ground (two layers). */
    private static boolean flies(int type) {
        final EngineRuns.Unit u = engineUnit(type);
        return (u != null) ? u.flies() : UnitCatalog.flies(type);
    }

    /** True for a unit that belongs to the neutral slot whoever places it: the neutral types of the palette. */
    public static boolean neutral(int type) {
        return isResource(type) || (UnitCatalog.has(type) && UnitCatalog.race(type) == UnitCatalog.NEUTRAL);
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

    private static int ground(int type) { return UnitCatalog.ground(type); }

    /** True for a mixed tile of water and coast: the "coast" flag of the tilesets, which a shore building needs. */
    public static boolean coast(int tile) { return move(tile) == MOVE_COAST; }

    private static boolean groundFits(MapModel m, int type, int tile) {
        // The engine's rule while its export is installed: no bit of the tile's flags in the
        // type's movement mask (UnitTypeCanBeAt); a tile or type the export has not, as before.
        final EngineRuns.Unit u = engineUnit(type);
        final long flags = (u != null && u.mask != 0L) ? engine.flags(m.tileset(), tile) : -1L;
        if (flags >= 0L) {
            return (flags & u.mask) == 0L;
        }
        switch (ground(type)) {
            case GROUND_BUILD: return buildable(tile);
            case GROUND_WATER: return water(tile);
            case GROUND_LAND: return land(tile);
            // unittype.cpp: a shore building may not stand on a land tile (water and coast only).
            case GROUND_SHORE: return water(tile) || coast(tile);
            default: return true;
        }
    }

    // ----- the view switches Passable / Buildable (E11), by the same rule as placing -----

    /** The type a sea tile is asked of: the human destroyer (PUD 0x1E, Sea Units, water). */
    private static final int SHOW_SEA_TYPE = 0x1E;
    /** What a tile shows under Passable: nothing, land, water. */
    public static final int PASS_NONE = 0, PASS_LAND = 1, PASS_WATER = 2;

    /**
     * The Passable class of a tile of this map: land where a worker may stand, water where a
     * ship may; the same rule as placing, so with an engine export installed its flags decide
     * (Session 2026-10-19 e gap), without one the built-in classes as before.
     */
    public static int passClass(MapModel m, int tile) {
        return groundFits(m, PEASANT, tile) ? PASS_LAND : groundFits(m, SHOW_SEA_TYPE, tile) ? PASS_WATER : PASS_NONE;
    }

    /** The Buildable class of a tile of this map: a land building (the town hall) may stand on it; same rule as placing. */
    public static boolean buildableOn(MapModel m, int tile) {
        return groundFits(m, TOWN_HALL, tile);
    }

    /** build.cpp HasAtLeastOneCoastTile: a shore building needs a coast tile under it; true for the other types. */
    private static boolean coastUnder(MapModel m, int type, int x, int y) {
        final EngineRuns.Unit u = engineUnit(type);
        if ((u != null) ? !u.shore : ground(type) != GROUND_SHORE) {
            return true;
        }
        final int size = footprint(type);
        for (int yy = y; yy < y + size; ++yy) {
            for (int xx = x; xx < x + size; ++xx) {
                if (xx >= 0 && yy >= 0 && xx < m.width() && yy < m.height() && coastAt(m, m.tile(xx, yy))) {
                    return true;
                }
            }
        }
        return false;
    }

    /** A coast tile: by the export's flags (MapFieldCoastAllowed) while installed and it has the tile. */
    private static boolean coastAt(MapModel m, int tile) {
        final EngineRuns.Tables t = engine;
        final long flags = (t == null) ? -1L : t.flags(m.tileset(), tile);
        return (flags >= 0L) ? (flags & EngineRuns.COAST_ALLOWED) != 0L : coast(tile);
    }

    // ----- placing -----

    /** True when one tile could carry a tile of a unit of this type: inside the map, right ground. */
    public static boolean tileFits(MapModel m, int type, int x, int y) {
        return x >= 0 && y >= 0 && x < m.width() && y < m.height() && groundFits(m, type, m.tile(x, y));
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
    public static boolean taken(MapModel m, int x, int y, int leftOut) { return taken(m, x, y, leftOut, -1); }

    /**
     * True when a tile is taken, for a unit of a type (-1: of any layer), by a unit other than the
     * one left out: for a flying unit by flying ones, for the others by the others.
     */
    public static boolean taken(MapModel m, int x, int y, int leftOut, int type) {
        for (int i = 0; i < m.unitCount(); ++i) {
            final MapModel.Unit u = m.unit(i);
            if (i == leftOut || isStart(u.type) || (type >= 0 && flies(type) != flies(u.type))) {
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
                if (!groundFits(m, type, m.tile(xx, yy)) || (!isStart(type) && taken(m, xx, yy, leftOut, type))) {
                    return false;
                }
            }
        }
        return coastUnder(m, type, x, y);
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

    /** The slot has a controller a placed unit keeps: any of the list but Nobody. */
    private static boolean controlled(MapModel m, int slot) {
        final int o = m.slot(MapModel.FIELD_OWNER, slot);
        return o != MapModel.OWNER_NOBODY && controllerIndex(o) >= 0;
    }

    /**
     * A unit of a palette kind placed for a slot, as one step; null where it may not stand.
     * A resource goes to the neutral slot. A slot's start location is moved, not doubled. A slot
     * owned by nobody (or by a number the list has not) becomes a person's, and gets the usual
     * starting resources when it has none; a rescuable or neutral slot stays as it is.
     */
    public static MapModel.Step place(MapModel m, int kind, int slot, int x, int y) {
        final boolean resource = kind == KIND_MINE || kind == KIND_PATCH;
        if (!resource && (slot < 0 || slot >= PLAYERS)) {
            return null;
        }
        return placeType(m, typeOf(kind, resource ? MapModel.RACE_HUMAN : m.slot(MapModel.FIELD_RACE, slot)), slot, x, y);
    }

    /**
     * A unit of a type placed for a slot, as one step; null where it may not stand (stage 1b,
     * owner decision 88, 5B: any type of UnitCatalog, of either race, for any player). A neutral
     * type goes to the neutral slot; the rest as place.
     */
    public static MapModel.Step placeType(MapModel m, int type, int slot, int x, int y) {
        final boolean resource = neutral(type);
        if (!resource && (slot < 0 || slot >= PLAYERS)) {
            return null;
        }
        final int owner = resource ? MapModel.NEUTRAL_SLOT : slot;
        final int old = isStart(type) ? startOf(m, slot) : -1;
        if (!mayPlace(m, type, x, y, old)) {
            return null;
        }
        final int data = (type == GOLD_MINE) ? MINE_AMOUNT : (type == OIL_PATCH) ? PATCH_AMOUNT
            : isStart(type) ? 0 : PASSIVE;
        final ArrayList<MapModel.Step> parts = new ArrayList<MapModel.Step>();
        if (old >= 0) {
            parts.add(m.changeUnit(old, new MapModel.Unit(x, y, type, owner, m.unit(old).data)));
            if (parts.get(0) == null) {
                return null;
            }
        } else {
            parts.add(m.placeUnit(new MapModel.Unit(x, y, type, owner, data)));
        }
        if (!resource && !controlled(m, slot)) {
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

    // ----- mirror editing of units (EDITOR_DESIGN.md 4.2) -----

    /** Steps made one after the other on the map, taken back again, as one step; null for none. */
    private static MapModel.Step together(MapModel m, List<MapModel.Step> parts) {
        for (int i = parts.size() - 1; i >= 0; --i) {
            parts.get(i).back(m);
        }
        return parts.isEmpty() ? null : (parts.size() == 1) ? parts.get(0) : new MapModel.GroupStep(parts);
    }

    /** What a mirrored placing did: the step (null when the unit itself may not stand), and how many of how many were placed. */
    public static final class Mirrored {
        public final MapModel.Step step;
        public final int placed, wanted;

        Mirrored(MapModel.Step step, int placed, int wanted) {
            this.step = step; this.placed = placed; this.wanted = wanted;
        }
    }

    /** How many sectors a mirror style cuts the map into: halves, quarters, eighths (1 for Off, and 8 only on a square map). */
    public static int sectors(int mirror, int w, int h) {
        return (mirror >= Terrain.MIRROR_8 && w == h) ? 8 : (mirror >= Terrain.MIRROR_4) ? 4
            : (mirror >= Terrain.MIRROR_2) ? 2 : 1;
    }

    /**
     * The sector a map point (in tiles) lies in: counted clockwise from the top round the map's
     * middle, n sectors of equal angle - for 2 the right and the left half, for 4 the quarters
     * from the top right, for 8 the eighths bounded by the axes and the diagonals, the parts the
     * mirror styles repeat.
     */
    public static int sectorOf(float px, float py, int w, int h, int n) {
        if (n <= 1) {
            return 0;
        }
        double a = Math.atan2(px - w / 2.0, -(py - h / 2.0));     // 0 up, growing clockwise
        if (a < 0) {
            a += 2 * Math.PI;
        }
        return Math.min(n - 1, (int) Math.floor(a / (2 * Math.PI / n)));
    }

    /**
     * The owner of an image (owner decision 88, 1A): the player whose start location lies in
     * the image's sector. In a sector with no start the image takes the slot of the fixed order
     * (1B: the source's block of n slots, moved on clockwise by as many sectors), or, when that
     * slot has its start in another sector or went to another image already (given), the next
     * slot after it that has neither. Of two starts in one sector the lower slot is taken.
     */
    public static int imageOwner(MapModel m, int source, int sourceSector, int imageSector, int n, boolean[] given) {
        final int w = m.width(), h = m.height();
        final int[] startSector = new int[PLAYERS];
        for (int s = 0; s < PLAYERS; ++s) {
            final int at = startOf(m, s);
            startSector[s] = (at < 0) ? -1 : sectorOf(m.unit(at).x + 0.5f, m.unit(at).y + 0.5f, w, h, n);
        }
        for (int s = 0; s < PLAYERS; ++s) {
            if (startSector[s] == imageSector) {
                return s;
            }
        }
        final int offset = ((imageSector - sourceSector) % n + n) % n;
        final int base = source - source % n;
        final int fixed = base + (source % n + offset) % n;
        if (startSector[fixed] < 0 && !given[fixed]) {
            return fixed;
        }
        for (int k = 1; k < PLAYERS; ++k) {
            final int s = (fixed + k) % PLAYERS;
            if (startSector[s] < 0 && !given[s]) {
                return s;
            }
        }
        return fixed;
    }

    /** As mirrorPlace, the step alone. */
    public static MapModel.Step placeMirrored(MapModel m, int kind, int slot, int x, int y, int mirror) {
        return mirrorPlace(m, kind, slot, x, y, mirror).step;
    }

    /**
     * A unit of a palette kind placed with Mirror on (Terrain.MIRROR_*): at (x, y) and at each
     * image of its footprint, as one step (EDITOR_DESIGN.md 4.2; owner decision 88, 1A). Each
     * image of a player's unit goes to the player of its sector (imageOwner); a gold mine or an
     * oil patch stays neutral. An image that falls where its unit may not stand is not placed
     * (the caller says so); the step is null when the unit itself may not stand at (x, y).
     *
     * A start location is not mirrored: each player has one, placed by hand.
     */
    public static Mirrored mirrorPlace(MapModel m, int kind, int slot, int x, int y, int mirror) {
        return mirrorPlace(m, kind, -1, slot, x, y, mirror);
    }

    /**
     * As mirrorPlace for a type of UnitCatalog (stage 1b, 5B): the images are of the same type
     * whatever the race of their owner (a map may give a unit of either race to any player); a
     * neutral type stays neutral.
     */
    public static Mirrored mirrorPlaceType(MapModel m, int type, int slot, int x, int y, int mirror) {
        return mirrorPlace(m, -1, type, slot, x, y, mirror);
    }

    /** A palette kind (type -1) or a type (kind -1) placed with Mirror. */
    private static Mirrored mirrorPlace(MapModel m, int kind, int type, int slot, int x, int y, int mirror) {
        final boolean byType = kind < 0;
        if (mirror == Terrain.MIRROR_OFF || (byType ? isStart(type) : kind == KIND_START)) {
            final MapModel.Step s = byType ? placeType(m, type, slot, x, y) : place(m, kind, slot, x, y);
            return new Mirrored(s, (s == null) ? 0 : 1, 1);
        }
        final boolean resource = byType ? neutral(type) : (kind == KIND_MINE || kind == KIND_PATCH);
        final int size = byType ? footprint(type) : footprint(typeOf(kind, resource ? MapModel.RACE_HUMAN
            : (slot >= 0 && slot < PLAYERS) ? m.slot(MapModel.FIELD_RACE, slot) : MapModel.RACE_HUMAN));
        final int w = m.width(), h = m.height(), n = sectors(mirror, w, h);
        final int[][] at = Terrain.images(x, y, size, w, h, mirror);
        final int[] owners = new int[at.length];
        final int home = sectorOf(x + size / 2f, y + size / 2f, w, h, n);
        final boolean[] given = new boolean[PLAYERS];
        if (slot >= 0 && slot < PLAYERS) {
            given[slot] = true;
        }
        for (int i = 0; i < at.length; ++i) {
            // The owners are found on the map as it was: a start is not moved by the images.
            owners[i] = (resource || i == 0 || slot < 0 || slot >= PLAYERS) ? slot
                : imageOwner(m, slot, home, sectorOf(at[i][0] + size / 2f, at[i][1] + size / 2f, w, h, n), n, given);
            if (owners[i] >= 0 && owners[i] < PLAYERS) {
                given[owners[i]] = true;
            }
        }
        final ArrayList<MapModel.Step> parts = new ArrayList<MapModel.Step>();
        for (int i = 0; i < at.length; ++i) {
            final MapModel.Step s = byType ? placeType(m, type, owners[i], at[i][0], at[i][1])
                : place(m, kind, owners[i], at[i][0], at[i][1]);
            if (s == null) {
                if (i == 0) {
                    return new Mirrored(null, 0, at.length);
                }
                continue;
            }
            s.run(m);
            parts.add(s);
        }
        return new Mirrored(together(m, parts), parts.size(), at.length);
    }

    /**
     * A unit removed with Mirror on: it and the units of its kind whose footprint lies exactly
     * on an image of its own, as one step. A start location is removed alone (see placeMirrored).
     */
    public static MapModel.Step eraseMirrored(MapModel m, int index, int mirror) {
        return eraseAll(m, withImages(m, index, mirror));
    }

    /**
     * A unit and, with Mirror on, the units of its kind whose footprint lies exactly on an image
     * of its own; a start location alone.
     */
    public static java.util.Set<Integer> withImages(MapModel m, int index, int mirror) {
        final MapModel.Unit u = m.unit(index);
        final java.util.TreeSet<Integer> out = new java.util.TreeSet<Integer>();
        out.add(index);
        if (mirror == Terrain.MIRROR_OFF || isStart(u.type)) {
            return out;
        }
        for (int[] p : Terrain.images(u.x, u.y, footprint(u.type), m.width(), m.height(), mirror)) {
            for (int i = 0; i < m.unitCount(); ++i) {
                final MapModel.Unit v = m.unit(i);
                if (v.x == p[0] && v.y == p[1] && sameKind(v.type, u.type)) {
                    out.add(i);
                }
            }
        }
        return out;
    }

    private static boolean sameKind(int a, int b) {
        return a == b || (kindOf(a) >= 0 && kindOf(a) == kindOf(b));
    }

    // ----- several units chosen (Select: rectangle; EDITOR_DESIGN.md 4.2, 4.4) -----

    /**
     * The units whose footprint touches the rectangle of tiles x0..x1, y0..y1 (either corner
     * first), in the order of the unit list.
     */
    public static int[] unitsIn(MapModel m, int x0, int y0, int x1, int y1) {
        final int ax = Math.min(x0, x1), bx = Math.max(x0, x1), ay = Math.min(y0, y1), by = Math.max(y0, y1);
        final ArrayList<Integer> found = new ArrayList<Integer>();
        for (int i = 0; i < m.unitCount(); ++i) {
            final MapModel.Unit u = m.unit(i);
            final int size = footprint(u.type);
            if (u.x <= bx && u.x + size - 1 >= ax && u.y <= by && u.y + size - 1 >= ay) {
                found.add(i);
            }
        }
        final int[] out = new int[found.size()];
        for (int i = 0; i < out.length; ++i) {
            out[i] = found.get(i);
        }
        return out;
    }

    /**
     * The kinds among the chosen units, for the chips of the strip (owner decision 88, 6B): for
     * each kind in the order it first comes, a unit of it (its index) and how many there are,
     * as { index, count } pairs.
     */
    public static int[][] kindsOf(MapModel m, int[] chosen) {
        final ArrayList<int[]> out = new ArrayList<int[]>();
        for (int i : chosen) {
            boolean found = false;
            for (int[] k : out) {
                if (sameKind(m.unit(k[0]).type, m.unit(i).type)) {
                    ++k[1];
                    found = true;
                    break;
                }
            }
            if (!found) {
                out.add(new int[] { i, 1 });
            }
        }
        return out.toArray(new int[0][]);
    }

    /** The chosen units of the kind of one unit, in their order: a chip tapped. */
    public static int[] narrow(MapModel m, int[] chosen, int like) {
        int n = 0;
        for (int i : chosen) {
            n += sameKind(m.unit(i).type, m.unit(like).type) ? 1 : 0;
        }
        final int[] out = new int[n];
        n = 0;
        for (int i : chosen) {
            if (sameKind(m.unit(i).type, m.unit(like).type)) {
                out[n++] = i;
            }
        }
        return out;
    }

    /** True when every chosen unit is of one palette kind (or of one type the palette has not). */
    public static boolean oneKind(MapModel m, int[] chosen) {
        for (int i = 1; i < chosen.length; ++i) {
            if (!sameKind(m.unit(chosen[i]).type, m.unit(chosen[0]).type)) {
                return false;
            }
        }
        return chosen.length > 0;
    }

    /**
     * True when a unit of this type may stand with its top left tile at (x, y), the units
     * marked in leftOut not counted: those are the ones being moved together.
     */
    public static boolean mayPlace(MapModel m, int type, int x, int y, boolean[] leftOut) {
        final int size = footprint(type);
        if (x < 0 || y < 0 || x + size > m.width() || y + size > m.height()) {
            return false;
        }
        for (int yy = y; yy < y + size; ++yy) {
            for (int xx = x; xx < x + size; ++xx) {
                if (!groundFits(m, type, m.tile(xx, yy)) || (!isStart(type) && takenBut(m, xx, yy, leftOut, type))) {
                    return false;
                }
            }
        }
        return coastUnder(m, type, x, y);
    }

    /** As taken, the units marked in leftOut not counted. */
    public static boolean takenBut(MapModel m, int x, int y, boolean[] leftOut) { return takenBut(m, x, y, leftOut, -1); }

    /** As taken for a unit of a type (-1: any layer), the units marked in leftOut not counted. */
    public static boolean takenBut(MapModel m, int x, int y, boolean[] leftOut, int type) {
        for (int i = 0; i < m.unitCount(); ++i) {
            final MapModel.Unit u = m.unit(i);
            if ((leftOut != null && i < leftOut.length && leftOut[i]) || isStart(u.type)
                || (type >= 0 && flies(type) != flies(u.type))) {
                continue;
            }
            final int size = footprint(u.type);
            if (x >= u.x && y >= u.y && x < u.x + size && y < u.y + size) {
                return true;
            }
        }
        return false;
    }

    /** The marks of a set of units, for mayPlace. */
    public static boolean[] marks(MapModel m, int[] chosen) {
        final boolean[] out = new boolean[m.unitCount()];
        for (int i : chosen) {
            out[i] = true;
        }
        return out;
    }

    /**
     * The chosen units moved together by dx, dy tiles, as one step. Null when any of them may
     * not stand at its new place (the others of the group are not in its way), or for no move.
     */
    public static MapModel.Step moveAll(MapModel m, int[] chosen, int dx, int dy) {
        if ((dx == 0 && dy == 0) || chosen.length == 0) {
            return null;
        }
        final boolean[] out = marks(m, chosen);
        final ArrayList<MapModel.Step> parts = new ArrayList<MapModel.Step>();
        for (int i : chosen) {
            final MapModel.Unit u = m.unit(i);
            if (!mayPlace(m, u.type, u.x + dx, u.y + dy, out)) {
                return null;
            }
            parts.add(m.changeUnit(i, new MapModel.Unit(u.x + dx, u.y + dy, u.type, u.owner, u.data)));
        }
        return (parts.size() == 1) ? parts.get(0) : new MapModel.GroupStep(parts);
    }

    /** The given units removed, as one step; null for none. */
    public static MapModel.Step eraseAll(MapModel m, java.util.Collection<Integer> chosen) {
        final java.util.TreeSet<Integer> order = new java.util.TreeSet<Integer>(java.util.Collections.reverseOrder());
        order.addAll(chosen);
        final ArrayList<MapModel.Step> parts = new ArrayList<MapModel.Step>();
        // From the last in the list, so that the places of the others stay as they are.
        for (int i : order) {
            parts.add(m.removeUnit(i));
        }
        return parts.isEmpty() ? null : (parts.size() == 1) ? parts.get(0) : new MapModel.GroupStep(parts);
    }

    public static MapModel.Step eraseAll(MapModel m, int[] chosen) {
        final ArrayList<Integer> list = new ArrayList<Integer>();
        for (int i : chosen) {
            list.add(i);
        }
        return eraseAll(m, list);
    }

    /** The amount of every chosen gold mine and oil patch changed by so many units of 2500, as one step. */
    public static MapModel.Step amountAll(MapModel m, int[] chosen, int by) {
        final ArrayList<MapModel.Step> parts = new ArrayList<MapModel.Step>();
        for (int i : chosen) {
            final MapModel.Step s = amount(m, i, by);
            if (s != null) {
                parts.add(s);
            }
        }
        return parts.isEmpty() ? null : (parts.size() == 1) ? parts.get(0) : new MapModel.GroupStep(parts);
    }

    // ----- unit limits (EDITOR_DESIGN.md section 3; audit U6) -----

    /** A limit whose number is not known. */
    public static final int UNKNOWN = -1;
    /**
     * The original editor's most units on a map and of one player. UNKNOWN: the numbers are not
     * in the game's resources and were not measured (audit U6, design U-E2). While a number is
     * UNKNOWN its check says nothing.
     */
    public static final int UNITS_PER_MAP = UNKNOWN, UNITS_PER_PLAYER = UNKNOWN;

    /**
     * The units a limit counts: [0..PLAYERS-1] those of each player slot, [PLAYERS] all of the
     * map. A start location is a mark, not a unit, and is not counted (provisional: what the
     * original counts is part of U6).
     */
    public static int[] unitCounts(MapModel m) {
        final int[] out = new int[PLAYERS + 1];
        for (int i = 0; i < m.unitCount(); ++i) {
            final MapModel.Unit u = m.unit(i);
            if (isStart(u.type)) {
                continue;
            }
            if (u.owner < PLAYERS) {
                ++out[u.owner];
            }
            ++out[PLAYERS];
        }
        return out;
    }

    /** The limit faults of a map for the given numbers (UNKNOWN for a limit not checked). They block Save. */
    public static List<Fault> limits(MapModel m, int perMap, int perPlayer) {
        final ArrayList<Fault> out = new ArrayList<Fault>();
        if (perMap == UNKNOWN && perPlayer == UNKNOWN) {
            return out;
        }
        final int[] n = unitCounts(m);
        if (perMap != UNKNOWN && n[PLAYERS] > perMap) {
            out.add(new Fault(true, -1, -1, "The map has {0} units, more than {1}", n[PLAYERS], perMap));
        }
        for (int s = 0; perPlayer != UNKNOWN && s < PLAYERS; ++s) {
            if (n[s] > perPlayer) {
                out.add(new Fault(true, -1, -1, "Player {0} has {1} units, more than {2}", s + 1, n[s], perPlayer));
            }
        }
        return out;
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
        final boolean[] flown = new boolean[w * h];     // the same, by a flying one (a layer of its own)
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
                    ground &= groundFits(m, u.type, m.tile(xx, yy));
                    if (known(u.type) && !isStart(u.type)) {
                        final boolean[] layer = flies(u.type) ? flown : taken;
                        overlap |= layer[yy * w + xx];
                        layer[yy * w + xx] = true;
                    }
                }
            }
            if (!ground || !coastUnder(m, u.type, u.x, u.y)) {
                final int g = ground(u.type);
                out.add(new Fault(true, u.x, u.y,
                    (u.type == GOLD_MINE) ? "A gold mine is not on buildable land"
                    : (u.type == OIL_PATCH) ? "An oil patch is not on water"
                    : (u.type == TOWN_HALL || u.type == GREAT_HALL) ? "A hall is not on buildable land"
                    : (g == GROUND_BUILD) ? "A building is not on buildable land"
                    : (g == GROUND_SHORE) ? "A shore building is not on the shore"
                    : (g == GROUND_WATER) ? "A unit is not on water"
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
        out.addAll(limits(m, UNITS_PER_MAP, UNITS_PER_PLAYER));
        return out;
    }

    /** The balance below which Save says so (EDITOR_DESIGN.md section 5). */
    public static final double BALANCE_WARN = 75.0;

    /**
     * What Save shows: the faults of {@link #check}, then the warnings of the map as it plays -
     * a start from which no gold mine can be walked to, two starts with no way over land
     * between them on a map whose other starts are joined over land, a balance below 75. The
     * warnings never stop Save. They are measured only when nothing blocks, because a map with
     * a blocking fault (a player without a start, two starts of one player) is not yet a map
     * whose play can be measured.
     */
    public static List<Fault> review(MapModel m) {
        final List<Fault> out = check(m);
        if (!blocked(out)) {
            out.addAll(warnings(MapMetrics.measure(m)));
            out.addAll(rescueWarnings(m));
        }
        return out;
    }

    /**
     * The warning of a rescuable slot with nothing to rescue (owner board 1A): "Player {0} is
     * rescuable but has no units". A start location is a mark, not a unit, so it does not count.
     */
    public static List<Fault> rescueWarnings(MapModel m) {
        final ArrayList<Fault> out = new ArrayList<Fault>();
        final boolean[] has = new boolean[PLAYERS];
        for (int i = 0; i < m.unitCount(); ++i) {
            final MapModel.Unit u = m.unit(i);
            if (u.owner < PLAYERS && !isStart(u.type)) {
                has[u.owner] = true;
            }
        }
        for (int s = 0; s < PLAYERS; ++s) {
            if (isRescue(m.slot(MapModel.FIELD_OWNER, s)) && !has[s]) {
                out.add(new Fault(false, -1, -1, "Player {0} is rescuable but has no units", s + 1));
            }
        }
        return out;
    }

    /**
     * What Test shows (Session 2026-10-19 d): the faults of {@link #check} and those that stop the
     * game's own loader on the bytes that are to be written ({@link EngineLoad#check}).
     */
    public static List<Fault> check(MapModel m, byte[] file) {
        return EngineLoad.join(check(m), EngineLoad.check(file));
    }

    /** What Save shows: {@link #check(MapModel, byte[])}, and the warnings when nothing blocks. */
    public static List<Fault> review(MapModel m, byte[] file) {
        final List<Fault> out = check(m, file);
        if (!blocked(out)) {
            out.addAll(warnings(MapMetrics.measure(m)));
            out.addAll(rescueWarnings(m));
        }
        return out;
    }

    /** The warnings of a measured map; none for fewer than two starts but the walk to gold. */
    public static List<Fault> warnings(MapMetrics.Measure measure) {
        final ArrayList<Fault> out = new ArrayList<Fault>();
        final MapMetrics.Player[] per = measure.per;
        for (MapMetrics.Player p : per) {
            if (Double.isInfinite(p.goldWalk)) {
                out.add(new Fault(false, p.x, p.y, "Player {0} cannot walk to a gold mine", p.slot + 1));
            }
        }
        // Islands on purpose (no start joined to any other) say nothing; some joined and some not does.
        if (measure.kind == MapMetrics.KIND_MIXED) {
            for (int i = 0; i < per.length; ++i) {
                for (int k = i + 1; k < per.length; ++k) {
                    if ((per[i].reach & (1 << per[k].slot)) == 0) {
                        out.add(new Fault(false, per[i].x, per[i].y, "No way over land between players {0} and {1}",
                                          per[i].slot + 1, per[k].slot + 1));
                    }
                }
            }
        }
        final double score = MapMetrics.fairness(measure);
        if (!Double.isNaN(score) && score < BALANCE_WARN) {
            out.add(new Fault(false, -1, -1, "The balance is {0}, below {1}", BalanceWatch.shown(score),
                              (int) BALANCE_WARN));
        }
        return out;
    }

    // ----- owner decision 94 (2026-10-21): typed amount (E1), owner cells (E2), Players rows (E3) -----

    /** The most a mine or a patch holds: the file's 0xFFFF units of 2 500 (163 837 500). */
    public static final long AMOUNT_MOST = 0xFFFFL * MapModel.RESOURCE_UNIT;
    /** The most digits the number sheet takes (163 837 500 has nine). */
    public static final int AMOUNT_DIGITS = 9;

    /**
     * The data word of a typed amount (E1): rounded to the nearest 2 500 (a half rounds up),
     * never under 2 500, at most 163 837 500. -1 for no digits or anything not a digit.
     */
    public static int typedAmount(String digits) {
        if (digits == null || digits.isEmpty()) {
            return -1;
        }
        long n = 0;
        for (int i = 0; i < digits.length(); ++i) {
            final char ch = digits.charAt(i);
            if (ch < '0' || ch > '9') {
                return -1;
            }
            // Held under a bound far past the most: a longer number only clamps.
            n = Math.min(AMOUNT_MOST * 10L, n * 10L + (ch - '0'));
        }
        final long units = (n + MapModel.RESOURCE_UNIT / 2) / MapModel.RESOURCE_UNIT;
        return (int) Math.max(1L, Math.min(0xFFFFL, units));
    }

    /** Every chosen gold mine and oil patch set to one data word, as one step; null when nothing changes. */
    public static MapModel.Step amountSetAll(MapModel m, int[] chosen, int data) {
        final int to = Math.max(1, Math.min(0xFFFF, data));
        final ArrayList<MapModel.Step> parts = new ArrayList<MapModel.Step>();
        for (int i : chosen) {
            final MapModel.Unit u = m.unit(i);
            if (!isResource(u.type)) {
                continue;
            }
            final MapModel.Step s = m.changeUnit(i, new MapModel.Unit(u.x, u.y, u.type, u.owner, to));
            if (s != null) {
                parts.add(s);
            }
        }
        return parts.isEmpty() ? null : (parts.size() == 1) ? parts.get(0) : new MapModel.GroupStep(parts);
    }

    /**
     * Whether the one-kind strip shows the eight owner cells for a type (E2): not for the
     * neutral types; not for a start location either, which is its slot's own mark (a slot has
     * one, Players moves it).
     */
    public static boolean ownerCells(int type) {
        return !neutral(type) && !isStart(type);
    }

    /** Every chosen unit that shows owner cells given to a player slot, as one step; null when nothing changes. */
    public static MapModel.Step ownerAll(MapModel m, int[] chosen, int owner) {
        if (owner < 0 || owner >= PLAYERS) {
            return null;
        }
        final ArrayList<MapModel.Step> parts = new ArrayList<MapModel.Step>();
        for (int i : chosen) {
            final MapModel.Unit u = m.unit(i);
            if (!ownerCells(u.type) || u.owner == owner) {
                continue;
            }
            final MapModel.Step s = m.changeUnit(i, new MapModel.Unit(u.x, u.y, u.type, owner, u.data));
            if (s != null) {
                parts.add(s);
            }
        }
        return parts.isEmpty() ? null : (parts.size() == 1) ? parts.get(0) : new MapModel.GroupStep(parts);
    }

    /** The player slots the Players rows of the maps page show (E3): those not Nobody, in order, at most `most`. */
    public static int[] playerRows(MapModel m, int most) {
        final int[] out = new int[PLAYERS];
        int n = 0;
        for (int s = 0; s < PLAYERS && n < Math.max(0, most); ++s) {
            if (m.slot(MapModel.FIELD_OWNER, s) != MapModel.OWNER_NOBODY) {
                out[n++] = s;
            }
        }
        return java.util.Arrays.copyOf(out, n);
    }

    /**
     * The slots a panel of `cells` cells names (owner decision 95, E7 = option A): all of them when
     * they fit, else one fewer, the last cell then counting the rest ("+n").
     */
    public static int playerCellsShown(int players, int cells) {
        if (cells <= 0 || players <= 0) {
            return 0;
        }
        return (players <= cells) ? players : cells - 1;
    }
}
