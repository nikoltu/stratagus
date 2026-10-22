package net.wargushd.game;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The name and the portrait of a computer seat in a network room and in its result: a hero of
 * the seat's race with "BOT" after his name (GromBOT, AlleriaBOT).
 *
 * The heroes are read from the unit definitions the engine itself runs, not typed here: a hero
 * is a unit type of a race's unit script that is not a building, that no button trains or
 * upgrades to, that takes food (Demand 1 or more) and whose icon no other unit type uses (the
 * Minuteman and the stronger Ballista share their icons and are left out; the Eye of Kilrogg and
 * the Skeleton take no food). The portrait is the icon's slot of the icon table, the frame the
 * launcher already cuts from the player's own portrait atlas.
 *
 * Each computer seat of a room gets a different hero, worked out from the seats' order, kinds
 * and races alone, so that every device of the room names them alike without a word on the
 * wire. Plain Java, no Android.
 */
final class BotNames {
    private BotNames() {}

    /** The race unit scripts and the race each defines, in the engine's load order. */
    static final String[] UNITS = { "scripts/human/units.lua", "scripts/orc/units.lua" };
    static final String[] RACES = { "human", "orc" };
    static final String[] BUTTONS = { "scripts/human/buttons.lua", "scripts/orc/buttons.lua" };
    static final String ICONS = "scripts/icons.lua";
    /** What follows the hero's name. */
    static final String SUFFIX = "BOT";

    private static final Pattern DEFINE = Pattern.compile("DefineUnitType\\(\\s*\"([^\"]+)\"");
    private static final Pattern NAME = Pattern.compile("\\bName\\s*=\\s*_\\(\\s*\"([^\"]+)\"");
    private static final Pattern ICON_FIELD = Pattern.compile("\\bIcon\\s*=\\s*\"([^\"]+)\"");
    private static final Pattern DEMAND = Pattern.compile("\\bDemand\\s*=\\s*(\\d+)");
    private static final Pattern BUILDING = Pattern.compile("\\bBuilding\\s*=\\s*true");
    private static final Pattern VALUE = Pattern.compile("\\bValue\\s*=\\s*\"(unit-[^\"]+)\"");
    private static final Pattern ICON = Pattern.compile("\\{\\s*\"(icon-[^\"]+)\"\\s*,\\s*(\\d+)\\s*\\}");

    /** One hero: his unit type, the scripts' name, his race and his portrait slot. */
    static final class Hero {
        final String unit, name, race;
        final int slot;

        Hero(String unit, String name, String race, int slot) {
            this.unit = unit;
            this.name = name;
            this.race = race;
            this.slot = slot;
        }

        /** The seat's name: the first word of the hero's name without its apostrophes, and BOT. */
        String botName() {
            return BotNames.botName(name);
        }
    }

    /** "Cho'gall" -> "ChogallBOT", "Grom Hellscream" -> "GromBOT"; "" for no name. */
    static String botName(String heroName) {
        if (heroName == null) {
            return "";
        }
        final String first = heroName.trim().split("\\s+")[0];
        final StringBuilder b = new StringBuilder();
        for (int i = 0; i < first.length(); ++i) {
            final char ch = first.charAt(i);
            if (Character.isLetterOrDigit(ch)) {
                b.append(ch);
            }
        }
        return b.length() > 0 ? b.append(SUFFIX).toString() : "";
    }

    /** The heroes from the data directory's scripts; empty when they cannot be read. */
    static List<Hero> read(File dataDir) {
        final String[] units = new String[UNITS.length];
        final String[] buttons = new String[BUTTONS.length];
        for (int i = 0; i < UNITS.length; ++i) {
            units[i] = text(new File(dataDir, UNITS[i]));
            buttons[i] = text(new File(dataDir, BUTTONS[i]));
        }
        return heroes(text(new File(dataDir, ICONS)), units, buttons);
    }

    /**
     * The heroes of the race unit scripts (one per entry of RACES, null where unread), in
     * definition order, human first. A hero whose icon has no slot is left out.
     */
    static List<Hero> heroes(String icons, String[] unitScripts, String[] buttonScripts) {
        final List<Hero> out = new ArrayList<Hero>();
        if (icons == null || unitScripts == null) {
            return out;
        }
        final Map<String, Integer> slotOf = new HashMap<String, Integer>();
        for (Matcher m = ICON.matcher(icons); m.find();) {
            if (!slotOf.containsKey(m.group(1))) {
                slotOf.put(m.group(1), Integer.valueOf(m.group(2)));
            }
        }
        final Set<String> trained = new HashSet<String>();
        if (buttonScripts != null) {
            for (String b : buttonScripts) {
                if (b != null) {
                    for (Matcher m = VALUE.matcher(b); m.find();) {
                        trained.add(m.group(1));
                    }
                }
            }
        }
        // Every unit type of the race scripts, with its body; the icons counted over all of them.
        final List<String[]> types = new ArrayList<String[]>();
        final Map<String, Integer> iconUses = new HashMap<String, Integer>();
        for (int r = 0; r < unitScripts.length && r < RACES.length; ++r) {
            final String script = unitScripts[r];
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
                final String iconName = icon.find() ? icon.group(1) : "";
                types.add(new String[] { unit, body, iconName, RACES[r] });
                final Integer n = iconUses.get(iconName);
                iconUses.put(iconName, Integer.valueOf(n == null ? 1 : n.intValue() + 1));
                at = next;
            }
        }
        final Set<String> seen = new HashSet<String>();
        for (String[] t : types) {
            final String unit = t[0], body = t[1], iconName = t[2], race = t[3];
            final Matcher name = NAME.matcher(body);
            final Matcher demand = DEMAND.matcher(body);
            final Integer slot = slotOf.get(iconName);
            if (seen.contains(unit) || trained.contains(unit) || BUILDING.matcher(body).find()
                || !demand.find() || Integer.parseInt(demand.group(1)) < 1
                || iconUses.get(iconName).intValue() != 1 || slot == null || !name.find()
                || botName(name.group(1)).isEmpty()) {
                continue;
            }
            seen.add(unit);
            out.add(new Hero(unit, name.group(1), race, slot.intValue()));
        }
        return out;
    }

    /**
     * The hero of each seat: null for a seat that is not a computer's. Computer seats are taken
     * in seat order; each takes, from the heroes of its race ("human", "orc"; any other race -
     * random, the map's own - takes from all of them), the first not yet given, beginning at
     * its seat index. Only when every hero of its list is given does a seat repeat one.
     */
    static Hero[] assign(List<Hero> heroes, boolean[] computer, String[] race) {
        final Hero[] out = new Hero[computer.length];
        if (heroes == null || heroes.isEmpty()) {
            return out;
        }
        final Set<String> given = new HashSet<String>();
        for (int i = 0; i < computer.length; ++i) {
            if (!computer[i]) {
                continue;
            }
            final String r = (race != null && i < race.length) ? race[i] : null;
            final List<Hero> list = new ArrayList<Hero>();
            for (Hero h : heroes) {
                if (!"human".equals(r) && !"orc".equals(r) || h.race.equals(r)) {
                    list.add(h);
                }
            }
            if (list.isEmpty()) {
                list.addAll(heroes);
            }
            Hero pick = null;
            for (int k = 0; k < list.size() && pick == null; ++k) {
                final Hero h = list.get((i + k) % list.size());
                if (!given.contains(h.unit)) {
                    pick = h;
                }
            }
            if (pick == null) {
                pick = list.get(i % list.size());
            }
            given.add(pick.unit);
            out[i] = pick;
        }
        return out;
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
