package net.wargushd.game.editor;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * Whether the game loads a map file: the bytes the editor is about to write, put through the
 * rules of the engine's own map loader, so that a map that is saved or tested also plays. No
 * Android class.
 *
 * The engine of this tree loads a .pud directly (src/stratagus/iolib.cpp, LoadPudMapNative: the
 * only map-load path, no conversion to .smp / .sms): wc2pud::PudData::Parse reads the sections,
 * BuildPresentation and BuildSetup turn them into Lua that is run at once
 * (src/map/pud_convert.cpp). What stops it, or loses part of the map, is taken from there and
 * from the Lua functions that Lua calls:
 *
 * - Parse: the first section must be TYPE with "WAR2 MAP" and its end; a tag Parse does not
 *   know stops the load (a second TYPE among them); a section is read wherever it lies, so one
 *   that runs past the file's end, or bytes after the last section, are read out of the file.
 *   MTXM is sized by the DIM read before it; DESC is copied up to its end byte into 32 bytes;
 *   the slot sections are read as 16 entries.
 * - The slot numbers name a table entry: OWNR 2..7 (DefinePlayerTypes, script_map.cpp, refuses
 *   the empty names of 0 and 1 and the table has no 8 on), SIDE 0..2 for a slot that is not
 *   nobody, ERA / ERAX 0..3 (its first byte).
 * - The description is put between quotes into Lua: a quote, a line end or a backslash that
 *   makes the string not end (Lua 5.1's string reader) breaks the presentation.
 * - UNIT: a type without a script name is left out with a line on the console (the unit is lost),
 *   a number past the table is read out of it; a player number of 16 or more is out of the
 *   tables; CreateUnit (script_unit.cpp) stops at a unit of a slot that is nobody. Start
 *   locations are no units for the loader: they only set the slot's start view.
 *
 * What the loader accepts but plays otherwise than drawn (a unit moved aside, a tile whose
 * neighbours do not match) is not here: that stays the engine's validate pass (LAUNCHER_DESIGN.md
 * 31.7, not built).
 */
public final class EngineLoad {
    /** The tags Parse reads or skips, besides the first section's TYPE. */
    private static final String[] KNOWN = {
        "VER ", "DESC", "OWNR", "ERA ", "ERAX", "DIM ", "UDTA", "ALOW", "UGRD", "SIDE", "SGLD", "SLBR",
        "SOIL", "AIPL", "MTXM", "SQM ", "OILM", "REGM", "SIGN", "UNIT" };
    /** The slot sections and the bytes Parse reads of each. */
    private static final String[] SLOT_TAGS = { "OWNR", "SIDE", "SGLD", "SLBR", "SOIL", "AIPL" };
    private static final int[] SLOT_BYTES = { 16, 16, 32, 32, 32, 16 };

    /** UnitScriptNames of pud_convert.cpp: the numbers that have no name, and the length of the table. */
    private static final int[] NAMELESS = { 0x22, 0x24, 0x25, 0x30, 0x36 };
    public static final int TYPE_NAMES = 0x69;
    private static final int OWNER_NOBODY = 3, HUMAN_START = 0x5E, ORC_START = 0x5F;
    private static final int SLOTS = 16, DESCRIPTION_BYTES = 32;

    private EngineLoad() {}

    /** True for a type number the loader has a script name for. */
    public static boolean named(int type) {
        if (type < 0 || type >= TYPE_NAMES) {
            return false;
        }
        for (int n : NAMELESS) {
            if (n == type) {
                return false;
            }
        }
        return true;
    }

    private static boolean known(String tag) {
        for (String k : KNOWN) {
            if (k.equals(tag)) {
                return true;
            }
        }
        return false;
    }

    private static int u16(byte[] b, int at) {
        return (b[at] & 0xFF) | ((b[at + 1] & 0xFF) << 8);
    }

    private static long u32(byte[] b, int at) {
        return (b[at] & 0xFFL) | ((b[at + 1] & 0xFFL) << 8) | ((b[at + 2] & 0xFFL) << 16) | ((b[at + 3] & 0xFFL) << 24);
    }

    /**
     * True when Lua 5.1 reads the text, put between two quotes, as one string that ends at the
     * second quote (llex.c read_string).
     */
    public static boolean luaQuotable(String text) {
        for (int i = 0; i < text.length(); ++i) {
            final char c = text.charAt(i);
            if (c == '"' || c == '\n' || c == '\r') {
                return false;
            }
            if (c == '\\') {
                if (++i >= text.length()) {
                    return false;     // the closing quote would be taken as a character
                }
                if (text.charAt(i) >= '0' && text.charAt(i) <= '9') {
                    int v = 0, n = 0;
                    while (n < 3 && i < text.length() && text.charAt(i) >= '0' && text.charAt(i) <= '9') {
                        v = v * 10 + (text.charAt(i) - '0');
                        ++i;
                        ++n;
                    }
                    --i;
                    if (v > 255) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    /**
     * What stops the game from loading these map bytes, or loses a part of the map; each line
     * stops Save and Test. Units are shown at their tile; the rest has no place.
     */
    public static List<MapRules.Fault> check(byte[] file) {
        final ArrayList<MapRules.Fault> out = new ArrayList<MapRules.Fault>();
        if (file.length < 8 || !"TYPE".equals(new String(file, 0, 4, StandardCharsets.ISO_8859_1))
            || u32(file, 4) < 9 || 8 + u32(file, 4) > file.length
            || !"WAR2 MAP".equals(new String(file, 8, 8, StandardCharsets.ISO_8859_1)) || file[16] != 0) {
            out.add(new MapRules.Fault(true, -1, -1, "The file does not begin as a map of the game"));
            return out;
        }
        int at = 8 + (int) u32(file, 4);
        int w = 0, h = 0, units = -1, unitsAt = 0;
        boolean tiles = false;
        byte[] owners = null, races = null;
        final List<String> wrong = new ArrayList<String>();
        while (at < file.length) {
            if (at + 8 > file.length || at + 8 + u32(file, at + 4) > file.length) {
                out.add(new MapRules.Fault(true, -1, -1, "The file ends inside a section"));
                break;
            }
            final String tag = new String(file, at, 4, StandardCharsets.ISO_8859_1);
            final int len = (int) u32(file, at + 4), body = at + 8;
            if (!known(tag)) {
                out.add(new MapRules.Fault(true, -1, -1, "The game does not read the section {0}", tag.trim()));
                break;     // Parse stops here
            }
            for (int i = 0; i < SLOT_TAGS.length; ++i) {
                if (SLOT_TAGS[i].equals(tag) && len < SLOT_BYTES[i] && !wrong.contains(tag)) {
                    wrong.add(tag);
                    out.add(new MapRules.Fault(true, -1, -1, "The section {0} is shorter than the game reads it", tag));
                }
            }
            if (tag.equals("DIM ") && len >= 4) {
                w = u16(file, body);
                h = u16(file, body + 2);
            } else if (tag.equals("ERA ") || tag.equals("ERAX")) {
                if (len < 1 || (file[body] & 0xFF) > 3) {
                    out.add(new MapRules.Fault(true, -1, -1, "The map's tileset is not one the game has"));
                }
            } else if (tag.equals("DESC")) {
                int end = 0;
                while (end < len && file[body + end] != 0) {
                    ++end;
                }
                if (end >= DESCRIPTION_BYTES || end >= len) {
                    out.add(new MapRules.Fault(true, -1, -1, "The description is longer than the game reads"));
                } else if (!luaQuotable(new String(file, body, end, StandardCharsets.ISO_8859_1))) {
                    out.add(new MapRules.Fault(true, -1, -1, "The description holds a sign the game cannot read"));
                }
            } else if (tag.equals("OWNR")) {
                owners = java.util.Arrays.copyOfRange(file, body, body + len);
            } else if (tag.equals("SIDE")) {
                races = java.util.Arrays.copyOfRange(file, body, body + len);
            } else if (tag.equals("MTXM")) {
                tiles = w > 0 && h > 0 && (long) len >= 2L * w * h;
            } else if (tag.equals("UNIT")) {
                units = len / 8;
                unitsAt = body;
            }
            at = body + len;
        }
        if (!tiles) {
            out.add(new MapRules.Fault(true, -1, -1, "The map's tiles are not where the game reads them"));
        }
        final int[] owner = new int[SLOTS];
        for (int s = 0; s < SLOTS; ++s) {
            owner[s] = (owners != null && s < owners.length) ? owners[s] & 0xFF : 0;
            final int race = (races != null && s < races.length) ? races[s] & 0xFF : 0;
            if (owner[s] < 2 || owner[s] > 7) {
                out.add(new MapRules.Fault(true, -1, -1, "Slot {0} has a controller the game does not know", s + 1));
            } else if (s < SLOTS - 1 && owner[s] != OWNER_NOBODY && race > 2) {
                out.add(new MapRules.Fault(true, -1, -1, "Slot {0} has a race the game does not know", s + 1));
            }
        }
        for (int i = 0; i < Math.max(0, units); ++i) {
            final int r = unitsAt + i * 8;
            final int x = u16(file, r), y = u16(file, r + 2), type = file[r + 4] & 0xFF, p = file[r + 5] & 0xFF;
            final int sx = Math.max(0, Math.min(w - 1, x)), sy = Math.max(0, Math.min(h - 1, y));
            if (p >= SLOTS) {
                out.add(new MapRules.Fault(true, sx, sy, "A unit of a slot the game does not have"));
            } else if (type == HUMAN_START || type == ORC_START) {
                continue;
            } else if (!named(type)) {
                out.add(new MapRules.Fault(true, sx, sy, "A unit of a kind the game does not know"));
            } else if (owner[p] == OWNER_NOBODY) {
                out.add(new MapRules.Fault(true, sx, sy, "A unit of a slot that has no player"));
            }
        }
        return out;
    }

    /** The faults of the model and those of its file, a line that both give said once. */
    public static List<MapRules.Fault> join(List<MapRules.Fault> model, List<MapRules.Fault> file) {
        final ArrayList<MapRules.Fault> out = new ArrayList<MapRules.Fault>(model);
        for (MapRules.Fault f : file) {
            boolean said = false;
            for (MapRules.Fault g : model) {
                said |= g.text.equals(f.text) && g.x == f.x && g.y == f.y;
            }
            if (!said) {
                out.add(f);
            }
        }
        return out;
    }
}
