package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

import net.wargushd.game.editor.MapRules;

/**
 * The editor's number sheet (owner decision 94, E1 = option B of EDITOR_NEXT_IMPL.md 4.1): a
 * small sheet of its own over the map view, 1800 wide as the other sheets, with the keys 0-9,
 * back and OK - no Android keyboard, so it works the same on every device and needs no change
 * of the shell. Tapping the amount of chosen mines or patches opens it; OK rounds to the
 * nearest 2 500 (MapRules.typedAmount) and every chosen unit of the kind takes it as one undo
 * step. A tap beside the sheet, or Android Back, closes it with nothing changed.
 *
 * The field starts with the amount the units share (none when they differ); the first digit
 * typed replaces it, back takes the last digit away.
 */
final class NumberSheet {
    static final int NONE = 0, KEY = 1, OK = 2, CANCEL = 3;

    // Layout, logical units; y from the sheet's top. NameSheet's width and padding.
    private static final float W = 1800f, PAD = 96f, TITLE_Y = 96f, FIELD_Y = 204f, FIELD_H = 120f;
    private static final float KEY_Y = 372f, KEY_W = 520f, KEY_H = 120f, KEY_PITCH_X = 544f, KEY_PITCH_Y = 144f;
    private static final float H = KEY_Y + 3f * KEY_PITCH_Y + KEY_H + PAD;
    private static final float CAP_BODY = 44f;

    private static final int SHADE = 0xAA000000, PANEL_FILL = 0xF2140F0A, LINE = 0xFF92743E;
    private static final int FIELD = 0xA0000000, INK = 0xFFE8E2D4, GOLD = 0xFFF0D250;

    /** The keys by row: 1 2 3 / 4 5 6 / 7 8 9 / back 0 OK. */
    private static final String[] KEYS = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "<", "0", "OK" };
    private static final int BACK = 9, OK_KEY = 11;

    private final String title;
    private final StringBuilder digits = new StringBuilder();
    /** The field still shows the amount it was opened with: the first digit replaces it. */
    private boolean fresh;
    private Bitmap buttonPlain, buttonMarked;

    /** `current`: the amount the units share, or a negative number when they differ. */
    NumberSheet(String title, long current) {
        this.title = title;
        if (current >= 0) {
            digits.append(current);
            fresh = true;
        }
    }

    /** The digits as typed; MapRules.typedAmount makes the data word of them. */
    String digits() { return digits.toString(); }

    private float x() { return (Screen.CANVAS_W - W) / 2f; }
    private float y(SkinKit kit) { return kit.height / 2f - H / 2f; }
    private float keyX(int i) { return x() + PAD + (i % 3) * KEY_PITCH_X; }
    private float keyY(SkinKit kit, int i) { return y(kit) + KEY_Y + (i / 3) * KEY_PITCH_Y; }

    void draw(Canvas c, SkinKit kit) {
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        final float x = x(), y = y(kit), px = x + PAD, pw = W - PAD * 2f;
        kit.fill(c, x, y, W, H, PANEL_FILL, 18f);
        kit.outline(c, x, y, W, H, LINE, 18f);
        kit.textTop(c, kit.fit(title, CAP_BODY, pw), px, y + TITLE_Y, CAP_BODY, GOLD, true);
        kit.fill(c, px, y + FIELD_Y, pw, FIELD_H, FIELD, 14f);
        kit.outline(c, px, y + FIELD_Y, pw, FIELD_H, GOLD, 14f);
        final String shown = digits.toString();
        kit.textMiddle(c, shown, px + 36f, y + FIELD_Y, FIELD_H, CAP_BODY, fresh ? GOLD : INK, true);
        final float caret = px + 36f + (shown.isEmpty() ? 0f : kit.width(shown, CAP_BODY) + 8f);
        kit.fill(c, caret, y + FIELD_Y + FIELD_H / 4f, 4f, FIELD_H / 2f, GOLD, 0f);
        for (int i = 0; i < KEYS.length; ++i) {
            final float kx = keyX(i), ky = keyY(kit, i);
            kit.nine(c, (i == OK_KEY) ? buttonMarked : buttonPlain, kx, ky, KEY_W, KEY_H, 14);
            final String word = (i == OK_KEY) ? kit.string("common_ok", "OK") : KEYS[i];
            kit.textCentre(c, kit.fit(word, CAP_BODY, KEY_W - 48f), kx, ky, KEY_W, KEY_H, CAP_BODY,
                           (i == OK_KEY) ? GOLD : INK, true);
        }
    }

    /** A finger was lifted at (px, py), in logical units of the screen. */
    int up(float px, float py, SkinKit kit) {
        final float x = x(), y = y(kit);
        if (px < x || px >= x + W || py < y || py >= y + H) {
            return CANCEL;
        }
        for (int i = 0; i < KEYS.length; ++i) {
            final float kx = keyX(i), ky = keyY(kit, i);
            if (px < kx || px >= kx + KEY_W || py < ky || py >= ky + KEY_H) {
                continue;
            }
            if (i == OK_KEY) {
                return OK;
            }
            if (i == BACK) {
                if (digits.length() > 0) {
                    digits.setLength(digits.length() - 1);
                }
            } else {
                if (fresh) {
                    digits.setLength(0);
                }
                // No leading zeros; never more digits than the most amount has.
                if (digits.length() == 1 && digits.charAt(0) == '0') {
                    digits.setLength(0);
                }
                if (digits.length() < MapRules.AMOUNT_DIGITS) {
                    digits.append(KEYS[i]);
                }
            }
            fresh = false;
            return KEY;
        }
        return NONE;
    }
}
