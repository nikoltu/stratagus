package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

/**
 * A modal sheet with one text field: a profile's name. It sits in the upper part of the
 * screen, clear of the keyboard. The typing itself arrives from the view that shows it.
 *
 * In place (owner's board 2B, 2026-10-18) it is no sheet of its own: it holds the name typed
 * into a field of another sheet (the profile sheet's name, the first start's sheet), which
 * draws that field with drawField.
 */
final class NameSheet {
    static final int NONE = 0, CANCEL = 1, OK = 2;

    // Layout, logical units.
    private static final float W = 1800f, H = 600f, TOP = 144f, PAD = 96f;
    private static final float FIELD_H = 120f, BUTTON_W = 720f, BUTTON_H = 120f;
    private static final float CAP_BODY = 44f;

    private static final int SHADE = 0xAA000000, PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E;
    private static final int FIELD = 0xA0000000, INK = 0xFFE8E2D4, GOLD = 0xFFF0D250;

    private final String title;
    private final StringBuilder name = new StringBuilder();
    private Bitmap buttonPlain, buttonMarked;
    /** The name is typed into another sheet's field; this one is not drawn nor touched. */
    final boolean inPlace;
    /** The most characters the field takes. */
    private final int max;

    NameSheet(String title, String initial) {
        this(title, initial, false);
    }

    NameSheet(String title, String initial, boolean inPlace) {
        this(title, initial, inPlace, Profiles.NAME_MAX);
    }

    /** A sheet for a longer text than a name (the mods source's address, owner decision 90 2A). */
    NameSheet(String title, String initial, boolean inPlace, int max) {
        this.title = title;
        this.inPlace = inPlace;
        this.max = max;
        if (initial != null) {
            name.append(initial);
        }
    }

    /** The name as typed; it is cleaned by the profile store when used. */
    String name() {
        return name.toString();
    }

    void type(CharSequence text) {
        for (int i = 0; i < text.length() && name.length() < max; ++i) {
            final char ch = text.charAt(i);
            if (ch >= 0x20 && ch != 0x7F) {
                name.append(ch);
            }
        }
    }

    void erase(int count) {
        name.setLength(Math.max(0, name.length() - Math.max(0, count)));
    }

    private float x() { return (Screen.CANVAS_W - W) / 2f; }
    private float buttonY() { return TOP + H - PAD - BUTTON_H; }
    private float cancelX() { return x() + PAD; }
    private float okX() { return x() + W - PAD - BUTTON_W; }

    void draw(Canvas c, SkinKit kit) {
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        final float x = x(), px = x + PAD, pw = W - PAD * 2f;
        kit.fill(c, x, TOP, W, H, PANEL_FILL, 18f);
        kit.outline(c, x, TOP, W, H, LINE, 18f);
        kit.textTop(c, title, px, TOP + PAD, CAP_BODY, GOLD, true);

        drawField(c, kit, px, TOP + PAD + 108f, pw, FIELD_H, CAP_BODY);

        final float by = buttonY();
        kit.nine(c, buttonPlain, cancelX(), by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.string("common_cancel", "Cancel"), cancelX(), by, BUTTON_W, BUTTON_H,
                       CAP_BODY, INK, true);
        kit.nine(c, buttonMarked, okX(), by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.string("common_ok", "OK"), okX(), by, BUTTON_W, BUTTON_H, CAP_BODY, GOLD,
                       true);
    }

    /** The field with the name as typed and the caret after it, in logical units. */
    void drawField(Canvas c, SkinKit kit, float fx, float fy, float fw, float fh, float cap) {
        kit.fill(c, fx, fy, fw, fh, FIELD, 14f);
        kit.outline(c, fx, fy, fw, fh, GOLD, 14f);
        final String shown = name.toString();
        kit.textMiddle(c, shown, fx + 36f, fy, fh, cap, INK, true);
        // The caret stands after the last letter.
        final float caret = fx + 36f + (shown.isEmpty() ? 0f : kit.width(shown, cap) + 8f);
        kit.fill(c, caret, fy + fh / 4f, 4f, fh / 2f, GOLD, 0f);
    }

    /** A finger was lifted at (x, y), in logical units. */
    int up(float px, float py) {
        final float by = buttonY();
        if (py < by || py >= by + BUTTON_H) {
            return NONE;
        }
        if (px >= cancelX() && px < cancelX() + BUTTON_W) {
            return CANCEL;
        }
        if (px >= okX() && px < okX() + BUTTON_W) {
            return OK;
        }
        return NONE;
    }
}
