package net.wargushd.game;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeSet;

/**
 * What FontBaker draws for a language, worked out from text alone (plain Java: checked on a PC
 * without Android, tools/launcher-jvm-test).
 *
 * - The face: the game's Data/skins/skins.json names, per font and per language, the file the
 *   game draws that language with ("fonts"[].localization[]: enUS BLQ55, the other languages a
 *   Noto face). A language the table does not name (a mod's language) takes the enUS entry.
 * - The glyphs after slot 255 (FONT-CJK): every character of the language's texts that the
 *   sheet's slots 32..255 (SheetLayout) do not hold, in codepoint order. The engine reads them
 *   from `<stem>.map`, one decimal codepoint a line, line i = slot 256 + i (font.cpp
 *   ParseFontGlyphMap).
 * - The grid: 16 glyphs a row as before when nothing follows slot 255; otherwise wider, so the
 *   sheet stays within MAX_SIDE on each side.
 */
final class FontScan {
    private FontScan() {}

    /** The longest side of a sheet: a texture above the GPU's limit falls to the slow CPU blit. */
    static final int MAX_SIDE = 4096;
    /** Slots 32..255 come first (index = character - 32). */
    static final int FIXED = 256 - 32;

    // ---------------------------------------------------------------- a small JSON reader

    /**
     * A JSON text as Map (objects, keys in order) / List / String / Double / Boolean / null.
     * A leading byte-order mark is skipped. Throws IllegalArgumentException on a broken text.
     */
    static Object parse(String json) {
        final int[] at = { (!json.isEmpty() && json.charAt(0) == 0xFEFF) ? 1 : 0 };
        final Object v = value(json, at);
        skip(json, at);
        if (at[0] != json.length()) {
            throw new IllegalArgumentException("text after the value at " + at[0]);
        }
        return v;
    }

    private static void skip(String s, int[] at) {
        while (at[0] < s.length() && Character.isWhitespace(s.charAt(at[0]))) {
            ++at[0];
        }
    }

    private static Object value(String s, int[] at) {
        skip(s, at);
        if (at[0] >= s.length()) {
            throw new IllegalArgumentException("end of text");
        }
        final char c = s.charAt(at[0]);
        if (c == '{') {
            ++at[0];
            final Map<String, Object> m = new LinkedHashMap<String, Object>();
            skip(s, at);
            if (at[0] < s.length() && s.charAt(at[0]) == '}') {
                ++at[0];
                return m;
            }
            for (;;) {
                skip(s, at);
                final String key = string(s, at);
                skip(s, at);
                expect(s, at, ':');
                m.put(key, value(s, at));
                skip(s, at);
                if (at[0] < s.length() && s.charAt(at[0]) == ',') {
                    ++at[0];
                    continue;
                }
                expect(s, at, '}');
                return m;
            }
        }
        if (c == '[') {
            ++at[0];
            final List<Object> l = new ArrayList<Object>();
            skip(s, at);
            if (at[0] < s.length() && s.charAt(at[0]) == ']') {
                ++at[0];
                return l;
            }
            for (;;) {
                l.add(value(s, at));
                skip(s, at);
                if (at[0] < s.length() && s.charAt(at[0]) == ',') {
                    ++at[0];
                    continue;
                }
                expect(s, at, ']');
                return l;
            }
        }
        if (c == '"') {
            return string(s, at);
        }
        if (s.startsWith("true", at[0])) {
            at[0] += 4;
            return Boolean.TRUE;
        }
        if (s.startsWith("false", at[0])) {
            at[0] += 5;
            return Boolean.FALSE;
        }
        if (s.startsWith("null", at[0])) {
            at[0] += 4;
            return null;
        }
        final int from = at[0];
        while (at[0] < s.length() && "+-0123456789.eE".indexOf(s.charAt(at[0])) >= 0) {
            ++at[0];
        }
        if (from == at[0]) {
            throw new IllegalArgumentException("unexpected '" + c + "' at " + from);
        }
        return Double.valueOf(s.substring(from, at[0]));
    }

    private static void expect(String s, int[] at, char c) {
        if (at[0] >= s.length() || s.charAt(at[0]) != c) {
            throw new IllegalArgumentException("'" + c + "' expected at " + at[0]);
        }
        ++at[0];
    }

    private static String string(String s, int[] at) {
        expect(s, at, '"');
        final StringBuilder b = new StringBuilder();
        for (;;) {
            if (at[0] >= s.length()) {
                throw new IllegalArgumentException("string not closed");
            }
            final char c = s.charAt(at[0]++);
            if (c == '"') {
                return b.toString();
            }
            if (c != '\\') {
                b.append(c);
                continue;
            }
            if (at[0] >= s.length()) {
                throw new IllegalArgumentException("string not closed");
            }
            final char e = s.charAt(at[0]++);
            switch (e) {
                case 'n': b.append('\n'); break;
                case 't': b.append('\t'); break;
                case 'r': b.append('\r'); break;
                case 'b': b.append('\b'); break;
                case 'f': b.append('\f'); break;
                case 'u':
                    if (at[0] + 4 > s.length()) {
                        throw new IllegalArgumentException("short \\u escape");
                    }
                    b.append((char) Integer.parseInt(s.substring(at[0], at[0] + 4), 16));
                    at[0] += 4;
                    break;
                default: b.append(e);   // \" \\ \/
            }
        }
    }

    // ---------------------------------------------------------------- the face per language

    /**
     * The face skins.json names for one of its fonts ("default_font", "detail_font",
     * "small_detail_font") and a language, as written there (e.g. "font/NotoSansJP-Regular.ttf",
     * relative to the game's Data folder); a language the font does not list takes the enUS
     * entry; null when the table or the font has neither.
     */
    @SuppressWarnings("unchecked")
    static String faceFor(Object skins, String fontId, String language) {
        if (!(skins instanceof Map)) {
            return null;
        }
        final Object fonts = ((Map<String, Object>) skins).get("fonts");
        if (!(fonts instanceof List)) {
            return null;
        }
        for (Object f : (List<Object>) fonts) {
            if (!(f instanceof Map) || !fontId.equals(((Map<String, Object>) f).get("id"))) {
                continue;
            }
            final Object loc = ((Map<String, Object>) f).get("localization");
            if (!(loc instanceof List)) {
                return null;
            }
            String english = null;
            for (Object e : (List<Object>) loc) {
                if (!(e instanceof Map)) {
                    continue;
                }
                final Object lang = ((Map<String, Object>) e).get("language");
                final Object file = ((Map<String, Object>) e).get("file");
                if (!(file instanceof String)) {
                    continue;
                }
                if (language.equals(lang)) {
                    return (String) file;
                }
                if ("enUS".equals(lang)) {
                    english = (String) file;
                }
            }
            return english;
        }
        return null;
    }

    // ---------------------------------------------------------------- the characters

    /** True when slot 32..255 of the sheet (SheetLayout) already draws the character. */
    static boolean inFixedSlots(int cp) {
        if (cp >= 32 && cp <= 0xFF) {
            return true;              // ASCII and Latin-1 (control codes have no glyph to add)
        }
        for (char c : SheetLayout.EXTRA) {
            if (c == cp) {
                return true;
            }
        }
        return false;
    }

    /** Characters that are never drawn: controls, the byte-order mark, lone surrogates. */
    private static boolean undrawn(int cp) {
        return cp < 32 || cp == 0xFEFF || (cp >= 0xD800 && cp <= 0xDFFF)
            || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2028 && cp <= 0x202E)
            || cp == 0x2060 || (cp >= 0xFFF0 && cp <= 0xFFFF) || cp > 0x10FFFF;
    }

    /** Add the characters of a text that need a slot after 255. */
    static void addText(String text, TreeSet<Integer> out) {
        for (int i = 0; i < text.length(); ) {
            final int cp = text.codePointAt(i);
            i += Character.charCount(cp);
            if (!undrawn(cp) && !inFixedSlots(cp)) {
                out.add(cp);
            }
        }
    }

    /** Add the characters of every string value of a JSON text (keys are names, not drawn). */
    @SuppressWarnings("unchecked")
    static void addJsonValues(Object json, TreeSet<Integer> out) {
        if (json instanceof String) {
            addText((String) json, out);
        } else if (json instanceof Map) {
            for (Object v : ((Map<String, Object>) json).values()) {
                addJsonValues(v, out);
            }
        } else if (json instanceof List) {
            for (Object v : (List<Object>) json) {
                addJsonValues(v, out);
            }
        }
    }

    // ---------------------------------------------------------------- the grid and the map

    /**
     * Glyphs a row for a sheet of cells cw x ch holding slots 32..255 and `extra` more: 16 when
     * there are none (the sheet as before); else the fewest, in steps of 16, that keep the sheet
     * about square and within MAX_SIDE high, never wider than MAX_SIDE.
     */
    static int columns(int extra, int cw, int ch) {
        if (extra <= 0) {
            return 16;
        }
        final int widest = Math.max(16, MAX_SIDE / cw / 16 * 16);
        final int total = FIXED + extra;
        for (int cols = 16; cols <= widest; cols += 16) {
            final int rows = (total + cols - 1) / cols;
            if (rows * ch <= MAX_SIDE && rows * ch <= cols * cw) {
                return cols;
            }
        }
        return widest;
    }

    /** How many glyphs after slot 255 a sheet of that grid can hold. */
    static int capacity(int cols, int cw, int ch) {
        return Math.max(0, cols * Math.max(1, MAX_SIDE / ch) - FIXED);
    }

    /** The `<stem>.map` text: one decimal codepoint a line, slot 256 first. */
    static String mapText(int[] cps) {
        final StringBuilder b = new StringBuilder(cps.length * 6);
        for (int cp : cps) {
            b.append(cp).append('\n');
        }
        return b.toString();
    }

    /**
     * The marker text (ui/fonts/blq55.layout): the layout first (scripts/fonts.lua only asks
     * that the file exists; the launcher compares the layout), then what the sheets were made
     * for, so a change of language, face or texts makes them again.
     */
    static String markerText(int layout, String key) {
        return layout + " " + key + "\n";
    }

    /** True when a marker text is of the given layout (its first word). */
    static boolean markerLayout(String marker, int layout) {
        if (marker == null) {
            return false;
        }
        final String t = marker.trim();
        final int sp = t.indexOf(' ');
        return (sp < 0 ? t : t.substring(0, sp)).equals(String.valueOf(layout));
    }
}
