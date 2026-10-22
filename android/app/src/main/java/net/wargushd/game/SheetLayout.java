package net.wargushd.game;

/**
 * Which character stands in each slot of the BLQ55 sheets FontBaker makes (layout 3).
 *
 * Slots 32..255 hold Latin-1, as before. The slots 0x80..0x9D, which Latin-1 leaves to control
 * codes, hold the Lithuanian letters, the typographic marks and (since layout 3) the Polish
 * L-stroke and the euro sign, in the order the engine's
 * font.cpp (HdSheetSlots, code page FontCodePageHdSheet) reads them. The two lists must match.
 * Plain Java: checked on a PC without Android.
 */
final class SheetLayout {
    private SheetLayout() {}

    /** The layout's number; the marker file of a baked set holds it. */
    static final int LAYOUT = 3;
    /** The marker file, beside the sheets: its presence tells the scripts the layout. */
    static final String MARKER = "ui/fonts/blq55.layout";

    /** Slots 0x80.. : A a C c E e E. e. I i S s U u U- u- Z z, then - -- ' ' , " " ,, ..., L/ l/ EUR */
    static final char[] EXTRA = {
        '\u0104', '\u0105', '\u010C', '\u010D', '\u0118', '\u0119', '\u0116', '\u0117',
        '\u012E', '\u012F', '\u0160', '\u0161', '\u0172', '\u0173', '\u016A', '\u016B',
        '\u017D', '\u017E',
        '\u2013', '\u2014', '\u2018', '\u2019', '\u201A', '\u201C', '\u201D', '\u201E', '\u2026',
        '\u0141', '\u0142', '\u20AC',
    };
    static final int EXTRA_FIRST = 0x80;

    /** The character drawn in slot `code` (32..255); 0 where the slot stays empty. */
    static char charAt(int code) {
        if (code >= EXTRA_FIRST && code < EXTRA_FIRST + EXTRA.length) {
            return EXTRA[code - EXTRA_FIRST];
        }
        if (code >= 127 && code < 160) {
            return 0;                                // control codes: no glyph
        }
        // 173 (soft hyphen): the platform draws nothing for it; the face draws its hyphen.
        return (char) (code == 173 ? 45 : code);
    }
}
