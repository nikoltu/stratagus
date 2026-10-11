package net.wargushd.game;

import net.wargushd.game.lan.LanRoom;

/**
 * The five settings of a network game as the Create segment and the room's map sheet show them:
 * Resources, Units, Map Tileset, Fog of War, Terrain. Values are the room's: -1 is the map's own.
 */
final class LanSettings {
    static final int ROWS = 5;

    /** -1 map default, 0 low, 1 medium, 2 high. */
    int resources = -1;
    /** -1 map default, 0 one peasant only. */
    int units = -1;
    /** -1 map default, 0 forest, 1 winter, 2 wasteland, 3 orc swamp. */
    int tileset = -1;
    boolean fog = true;
    /** 0 hidden, 1 explored, 2 revealed. */
    int reveal;

    private static final String[] TILESET_KEYS = {
        "customscenario_tileset_forest", "customscenario_tileset_winter",
        "customscenario_tileset_wasteland", "customscenario_tileset_orcswamp" };
    private static final String[] TILESET_PLAIN = { "Forest", "Winter", "Wasteland", "Orc Swamp" };

    void from(LanRoom.Snapshot s) {
        resources = s.resources;
        units = s.units;
        tileset = s.tileset;
        fog = s.fog;
        reveal = s.reveal;
    }

    static String label(SkinKit kit, int row) {
        switch (row) {
            case 0: return kit.own("Resources");
            case 1: return kit.own("Units");
            case 2: return kit.own("Map Tileset");
            case 3: return kit.own("Fog of War");
            default: return kit.own("Terrain");
        }
    }

    static String tilesetName(SkinKit kit, int tileset) {
        return (tileset >= 0 && tileset <= 3) ? kit.string(TILESET_KEYS[tileset], TILESET_PLAIN[tileset])
                                              : kit.string("customscenario_mapdefault", "Map Default");
    }

    String value(SkinKit kit, int row) {
        final String own = kit.string("customscenario_mapdefault", "Map Default");
        switch (row) {
            case 0:
                return resources == 0 ? kit.string("customscenario_resources_low", "Low")
                     : resources == 1 ? kit.string("customscenario_resources_medium", "Medium")
                     : resources == 2 ? kit.string("customscenario_resources_high", "High") : own;
            case 1:
                return units == 0 ? kit.string("customscenario_units_peasant", "One Peasant Only") : own;
            case 2:
                return tilesetName(kit, tileset);
            case 3:
                return fog ? kit.own("On") : kit.own("Off");
            default:
                return reveal == 1 ? kit.own("Explored") : reveal == 2 ? kit.own("Revealed") : kit.own("Hidden");
        }
    }

    /** The next value of a row, as a tap on its box asks. */
    void next(int row) {
        switch (row) {
            case 0: resources = (resources + 2) % 4 - 1; break;
            case 1: units = (units + 2) % 2 - 1; break;
            case 2: tileset = (tileset + 2) % 5 - 1; break;
            case 3: fog = !fog; break;
            default: reveal = (reveal + 1) % 3; break;
        }
    }
}
