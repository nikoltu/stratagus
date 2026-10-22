package net.wargushd.game;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The game's name for a portrait slot (GALLERY-NAME-MISSING, design 22.4): which key of the
 * game's string table names the unit whose icon a slot is.
 *
 * The design wants the mapping built from the engine's unit definitions, not typed by hand, so
 * it is read from the scripts the engine itself runs: the icon table (icon name -> slot), the
 * unit definitions (unit -> icon) and the map of our units to the Remastered's unit ids (unit ->
 * "unit_<id>"), with the card-name exceptions of rm_text.lua (the Flying Machine's stat_txt
 * key). The text itself is the game's, looked up by key at run time. Plain Java, no Android.
 */
final class PortraitNames {
    private PortraitNames() {}

    /** The scripts read, relative to the data directory, in the engine's load order. */
    static final String ICONS = "scripts/icons.lua";
    static final String[] UNITS = { "scripts/units.lua", "scripts/human/units.lua", "scripts/orc/units.lua" };
    static final String UNIT_MAP = "scripts/lib/rm_unit_map.lua";
    static final String TEXT = "scripts/lib/rm_text.lua";

    private static final Pattern ICON = Pattern.compile("\\{\\s*\"(icon-[^\"]+)\"\\s*,\\s*(\\d+)\\s*\\}");
    private static final Pattern DEFINE = Pattern.compile("DefineUnitType\\(\\s*\"([^\"]+)\"");
    private static final Pattern ICON_FIELD = Pattern.compile("\\bIcon\\s*=\\s*\"([^\"]+)\"");
    private static final Pattern ID = Pattern.compile("\\[\"([^\"]+)\"\\]\\s*=\\s*(\\d+)");
    private static final Pattern CARD = Pattern.compile("\\[\"([^\"]+)\"\\]\\s*=\\s*\"([^\"]+)\"");

    /** Slot -> string key from the data directory's scripts; empty when they cannot be read. */
    static Map<Integer, String> read(File dataDir) {
        final String[] units = new String[UNITS.length];
        for (int i = 0; i < UNITS.length; ++i) {
            units[i] = text(new File(dataDir, UNITS[i]));
        }
        return keys(text(new File(dataDir, ICONS)), units, text(new File(dataDir, UNIT_MAP)),
                    text(new File(dataDir, TEXT)));
    }

    /**
     * Slot -> string key. A slot whose icon no unit with a Remastered id uses has no entry. When
     * several units use one icon, the first defined that has an id names it (the Peasant, not the
     * Minuteman who shares his icon and has none).
     */
    static Map<Integer, String> keys(String icons, String[] unitScripts, String unitMap, String rmText) {
        final Map<Integer, String> out = new HashMap<Integer, String>();
        if (icons == null || unitMap == null) {
            return out;
        }
        final Map<String, Integer> slotOf = new HashMap<String, Integer>();
        for (Matcher m = ICON.matcher(icons); m.find();) {
            if (!slotOf.containsKey(m.group(1))) {
                slotOf.put(m.group(1), Integer.valueOf(m.group(2)));
            }
        }
        final Map<String, Integer> idOf = new HashMap<String, Integer>();
        final String table = between(unitMap, "RM_UNIT_ID", "}");
        for (Matcher m = ID.matcher(table); m.find();) {
            idOf.put(m.group(1), Integer.valueOf(m.group(2)));
        }
        final Map<String, String> card = new HashMap<String, String>();
        if (rmText != null) {
            for (Matcher m = CARD.matcher(between(rmText, "RM_CARD_NAME_KEY", "}")); m.find();) {
                card.put(m.group(1), m.group(2));
            }
        }
        // Unit -> icon, in definition order.
        final Map<String, String> iconOf = new LinkedHashMap<String, String>();
        if (unitScripts != null) {
            for (String script : unitScripts) {
                if (script == null) {
                    continue;
                }
                final Matcher d = DEFINE.matcher(script);
                int at = d.find() ? d.start() : -1;
                while (at >= 0) {
                    final String unit = d.group(1);
                    final int next = d.find() ? d.start() : -1;
                    final String body = script.substring(at, next >= 0 ? next : script.length());
                    final Matcher icon = ICON_FIELD.matcher(body);
                    if (icon.find() && !iconOf.containsKey(unit)) {
                        iconOf.put(unit, icon.group(1));
                    }
                    at = next;
                }
            }
        }
        for (Map.Entry<String, String> e : iconOf.entrySet()) {
            final Integer slot = slotOf.get(e.getValue());
            final Integer id = idOf.get(e.getKey());
            if (slot == null || id == null || out.containsKey(slot)) {
                continue;
            }
            final String own = card.get(e.getKey());
            out.put(slot, own != null ? own : "unit_" + id);
        }
        return out;
    }

    /** The text from the first "name" on to the first close after it; "" when absent. */
    private static String between(String text, String name, String close) {
        final int from = text.indexOf(name);
        if (from < 0) {
            return "";
        }
        final int open = text.indexOf('{', from);
        if (open < 0) {
            return "";
        }
        final int end = text.indexOf(close, open);
        return text.substring(open, end >= 0 ? end : text.length());
    }

    private static String text(File f) {
        if (!f.isFile()) {
            return null;
        }
        try (InputStream in = new FileInputStream(f)) {
            final java.io.ByteArrayOutputStream all = new java.io.ByteArrayOutputStream();
            final byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) {
                all.write(buf, 0, n);
            }
            return all.toString("UTF-8");
        } catch (Exception e) {
            return null;
        }
    }
}
