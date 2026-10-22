package net.wargushd.game.editor;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * What the engine's two runs for the editor leave behind, read (EDITOR_STATUS.md, Engine E13/E17):
 * the tables of `mode = "export"` (launch/export.json) and the check of `mode = "validate"`
 * (launch/validate.json), each beside the run's result.json. No Android class.
 *
 * The export's tables are the rules the editor places units by while they are installed
 * ({@link MapRules#useEngine}): a unit fits a tile when the tile's flags and the type's movement
 * mask share no bit (UnitTypeCanBeAt), a shore building needs a coast tile under it.
 * The check's lines are remarks with Show; a run that did not finish is one line that blocks.
 */
public final class EngineRuns {
    /** tileset.h: MapFieldCoastAllowed. */
    static final long COAST_ALLOWED = 0x20L;
    /** The tileset scripts the export loads, in the order of the map file's tileset number. */
    private static final String[] TILESET = { "summer", "winter", "wasteland", "swamp" };

    private EngineRuns() {}

    /** One unit type of the export. */
    public static final class Unit {
        public final String ident, domain;
        public final int w, h;
        public final boolean building, shore;
        public final long mask;

        Unit(String ident, int w, int h, String domain, boolean building, boolean shore, long mask) {
            this.ident = ident; this.w = w; this.h = h; this.domain = domain;
            this.building = building; this.shore = shore; this.mask = mask;
        }

        public boolean flies() { return "air".equals(domain); }
    }

    /** The tables of one export: tile flags by tileset and tile number, unit types by ident. */
    public static final class Tables {
        public final String stamp;
        public final List<String> mods;
        private final List<Map<Integer, Long>> flags;
        private final Map<String, Unit> units;

        Tables(String stamp, List<String> mods, List<Map<Integer, Long>> flags, Map<String, Unit> units) {
            this.stamp = stamp; this.mods = mods; this.flags = flags; this.units = units;
        }

        /** The flags of a tile number of a tileset (the map file's 0..3), or -1 when the export has none. */
        public long flags(int tileset, int tile) {
            if (tileset < 0 || tileset >= flags.size()) {
                return -1L;
            }
            final Long f = flags.get(tileset).get(tile);
            return (f == null) ? -1L : f;
        }

        /** The type of an ident, or null. */
        public Unit unit(String ident) { return (ident == null) ? null : units.get(ident); }
        public int unitCount() { return units.size(); }
        /** Tile numbers the export has for a tileset. */
        public int tileCount(int tileset) { return (tileset < 0 || tileset >= flags.size()) ? 0 : flags.get(tileset).size(); }
    }

    /**
     * How a run ended by its result.json: null when it finished, else the reason - the record's
     * error, "closed" when there is no record, "unreadable", "token" for another run's record.
     */
    public static String failure(String result, String token) {
        if (result == null) {
            return "closed";
        }
        final Map<?, ?> r;
        try {
            r = object(parse(result));
        } catch (RuntimeException e) {
            return "unreadable";
        }
        if (token != null && !token.equals(r.get("token"))) {
            return "token";
        }
        if ("exit".equals(r.get("state")) && "finished".equals(r.get("exit"))) {
            return null;
        }
        final Object error = r.get("error");
        return (error instanceof String) ? (String) error : "unknown";
    }

    /** The tables of an export, or null when the text is not one of this run. */
    public static Tables readExport(String json, String token) {
        if (json == null) {
            return null;
        }
        try {
            final Map<?, ?> e = object(parse(json));
            if (number(e.get("v")) != 1 || (token != null && !token.equals(e.get("token")))) {
                return null;
            }
            final List<String> mods = new ArrayList<String>();
            for (Object m : list(e.get("mods"))) {
                mods.add((String) m);
            }
            final List<Map<Integer, Long>> flags = new ArrayList<Map<Integer, Long>>();
            final List<?> sets = list(e.get("tilesets"));
            for (int era = 0; era < TILESET.length; ++era) {
                // By its script; the export writes them in this order anyway.
                Map<?, ?> found = (era < sets.size()) ? object(sets.get(era)) : null;
                for (Object s : sets) {
                    final Object file = object(s).get("file");
                    if (file instanceof String && ((String) file).endsWith("/" + TILESET[era] + ".lua")) {
                        found = object(s);
                    }
                }
                final Map<Integer, Long> of = new HashMap<Integer, Long>();
                if (found != null) {
                    for (Object t : list(found.get("tiles"))) {
                        final List<?> row = list(t);
                        of.put(number(row.get(0)), hex((String) row.get(2)));
                    }
                }
                flags.add(of);
            }
            final Map<String, Unit> units = new LinkedHashMap<String, Unit>();
            for (Object o : list(e.get("units"))) {
                final Map<?, ?> u = object(o);
                final String ident = (String) u.get("ident");
                units.put(ident, new Unit(ident, number(u.get("w")), number(u.get("h")), (String) u.get("domain"),
                                          Boolean.TRUE.equals(u.get("building")), Boolean.TRUE.equals(u.get("shore")),
                                          hex((String) u.get("mask"))));
            }
            final Object stamp = e.get("stamp");
            return new Tables((stamp instanceof String) ? (String) stamp : "", mods, flags, units);
        } catch (RuntimeException ex) {
            return null;
        }
    }

    /**
     * The lines of a check: a seam (two tiles whose corners do not meet) and a unit the game moved
     * aside or could not place are remarks with Show; counts above the listed get one line more.
     *
     * @return the lines, or null when the text is not a check of this run
     */
    public static List<MapRules.Fault> readValidate(String json, String token) {
        if (json == null) {
            return null;
        }
        try {
            final Map<?, ?> v = object(parse(json));
            if (number(v.get("v")) != 1 || (token != null && !token.equals(v.get("token")))) {
                return null;
            }
            final Map<?, ?> check = object(v.get("check"));
            final List<MapRules.Fault> out = new ArrayList<MapRules.Fault>();
            final Map<?, ?> moved = object(check.get("moved"));
            final List<?> units = list(moved.get("at"));
            for (Object o : units) {
                final Map<?, ?> u = object(o);
                final int x = number(u.get("x")), y = number(u.get("y")), player = number(u.get("player")) + 1;
                final String name = nameOf((String) u.get("type"));
                if (Boolean.TRUE.equals(u.get("lost"))) {
                    out.add(new MapRules.Fault(false, x, y, "The game finds no place for the {0} of player {1}",
                                               name, player));
                } else {
                    out.add(new MapRules.Fault(false, x, y, "The game moves the {0} of player {1} to {2},{3}", name,
                                               player, number(u.get("to_x")), number(u.get("to_y"))));
                }
            }
            if (number(moved.get("count")) > units.size()) {
                out.add(new MapRules.Fault(false, -1, -1, "The game moves {0} more units",
                                           number(moved.get("count")) - units.size()));
            }
            final Map<?, ?> seams = object(check.get("seams"));
            final List<?> at = list(seams.get("at"));
            for (Object o : at) {
                final List<?> s = list(o);
                out.add(new MapRules.Fault(false, number(s.get(0)), number(s.get(1)),
                                           "down".equals(s.get(2)) ? "A tile does not meet the tile below it"
                                                                   : "A tile does not meet the tile to its right"));
            }
            if (number(seams.get("count")) > at.size()) {
                out.add(new MapRules.Fault(false, -1, -1, "{0} more tiles do not meet their neighbours",
                                           number(seams.get("count")) - at.size()));
            }
            return out;
        } catch (RuntimeException e) {
            return null;
        }
    }

    /** The one line of a check that did not come back: it blocks. */
    public static MapRules.Fault runFault(String reason) {
        if ("load-failed".equals(reason)) {
            return new MapRules.Fault(true, -1, -1, "The game could not load the map");
        }
        if ("closed".equals(reason)) {
            return new MapRules.Fault(true, -1, -1, "The game closed while it checked the map");
        }
        return new MapRules.Fault(true, -1, -1, "The game could not check the map ({0})", reason);
    }

    /** The palette's name of an ident, else the ident itself. */
    private static String nameOf(String ident) {
        for (int i = 0; i < UnitCatalog.IDENT.length; ++i) {
            if (UnitCatalog.IDENT[i].equals(ident)) {
                return UnitCatalog.NAME[i];
            }
        }
        return String.valueOf(ident);
    }

    private static long hex(String s) {
        if (s == null || !s.startsWith("0x")) {
            throw new IllegalArgumentException("flags");
        }
        return Long.parseUnsignedLong(s.substring(2), 16);
    }

    private static int number(Object o) {
        if (!(o instanceof Double)) {
            throw new IllegalArgumentException("number");
        }
        return (int) (double) (Double) o;
    }

    private static Map<?, ?> object(Object o) {
        if (!(o instanceof Map)) {
            throw new IllegalArgumentException("object");
        }
        return (Map<?, ?>) o;
    }

    private static List<?> list(Object o) {
        if (!(o instanceof List)) {
            throw new IllegalArgumentException("list");
        }
        return (List<?>) o;
    }

    // ----- a small JSON reader: objects, arrays, strings, numbers, true, false, null -----

    /** The value of a JSON text: Map, List, String, Double, Boolean or null. */
    static Object parse(String text) {
        final int[] at = { 0 };
        final Object v = value(text, at);
        skip(text, at);
        if (at[0] != text.length()) {
            throw new IllegalArgumentException("text after the value");
        }
        return v;
    }

    private static void skip(String s, int[] at) {
        while (at[0] < s.length() && Character.isWhitespace(s.charAt(at[0]))) {
            ++at[0];
        }
    }

    private static char next(String s, int[] at) {
        skip(s, at);
        if (at[0] >= s.length()) {
            throw new IllegalArgumentException("end of text");
        }
        return s.charAt(at[0]);
    }

    private static Object value(String s, int[] at) {
        final char c = next(s, at);
        if (c == '{') {
            ++at[0];
            final Map<String, Object> m = new LinkedHashMap<String, Object>();
            if (next(s, at) == '}') {
                ++at[0];
                return m;
            }
            while (true) {
                if (next(s, at) != '"') {
                    throw new IllegalArgumentException("key");
                }
                final String key = string(s, at);
                if (next(s, at) != ':') {
                    throw new IllegalArgumentException("colon");
                }
                ++at[0];
                m.put(key, value(s, at));
                final char d = next(s, at);
                ++at[0];
                if (d == '}') {
                    return m;
                }
                if (d != ',') {
                    throw new IllegalArgumentException("comma");
                }
            }
        }
        if (c == '[') {
            ++at[0];
            final List<Object> l = new ArrayList<Object>();
            if (next(s, at) == ']') {
                ++at[0];
                return l;
            }
            while (true) {
                l.add(value(s, at));
                final char d = next(s, at);
                ++at[0];
                if (d == ']') {
                    return l;
                }
                if (d != ',') {
                    throw new IllegalArgumentException("comma");
                }
            }
        }
        if (c == '"') {
            return string(s, at);
        }
        for (String word : new String[] { "true", "false", "null" }) {
            if (s.startsWith(word, at[0])) {
                at[0] += word.length();
                return "null".equals(word) ? null : Boolean.valueOf("true".equals(word));
            }
        }
        final int from = at[0];
        while (at[0] < s.length() && "+-0123456789.eE".indexOf(s.charAt(at[0])) >= 0) {
            ++at[0];
        }
        if (from == at[0]) {
            throw new IllegalArgumentException("value");
        }
        return Double.valueOf(s.substring(from, at[0]));
    }

    private static String string(String s, int[] at) {
        ++at[0];   // the opening quote
        final StringBuilder b = new StringBuilder();
        while (true) {
            if (at[0] >= s.length()) {
                throw new IllegalArgumentException("open string");
            }
            final char c = s.charAt(at[0]++);
            if (c == '"') {
                return b.toString();
            }
            if (c != '\\') {
                b.append(c);
                continue;
            }
            final char e = s.charAt(at[0]++);
            switch (e) {
                case 'n': b.append('\n'); break;
                case 't': b.append('\t'); break;
                case 'r': b.append('\r'); break;
                case 'b': b.append('\b'); break;
                case 'f': b.append('\f'); break;
                case 'u':
                    b.append((char) Integer.parseInt(s.substring(at[0], at[0] + 4), 16));
                    at[0] += 4;
                    break;
                default: b.append(e);
            }
        }
    }
}
