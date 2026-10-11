package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

/**
 * What can be done with one of the player's own maps: rename it, remove it, or nothing.
 * Opened by a long press on the map's row (the owner's choice of 2026-10-06, option C).
 */
final class MapSheet {
    static final int NONE = 0, RENAME = 1, REMOVE = 2, CANCEL = 3;

    // Layout, logical units: the kit's small sheet, centred; three buttons in a row.
    private static final float W = 1800f, H = 600f, PAD = 96f;
    private static final float BUTTON_W = 500f, BUTTON_H = 120f, GAP = 54f;
    private static final float CAP_BODY = 44f, CAP_MID = 36f, LINE_PITCH = 82.5f;

    private static final int SHADE = 0xAA000000, PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E;
    private static final int INK = 0xFFE8E2D4, GOLD = 0xFFF0D250;

    private static final String[] CAPTIONS = { "Rename", "Remove" };

    private final String name;
    private Bitmap buttonPlain, buttonMarked;

    MapSheet(String name) {
        this.name = name;
    }

    private float x() { return (Screen.CANVAS_W - W) / 2f; }
    private float y(SkinKit kit) { return kit.height / 2f - H / 2f; }
    private float buttonX(int i) { return x() + PAD + (BUTTON_W + GAP) * i; }
    private float buttonY(SkinKit kit) { return y(kit) + H - PAD - BUTTON_H; }

    void draw(Canvas c, SkinKit kit) {
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        final float x = x(), y = y(kit);
        kit.fill(c, x, y, W, H, PANEL_FILL, 18f);
        kit.outline(c, x, y, W, H, LINE, 18f);

        final float tw = W - PAD * 2f;
        kit.textTop(c, kit.fit(name, CAP_BODY, tw), x + PAD, y + PAD, CAP_BODY, GOLD, true);
        kit.textTop(c, kit.fit(kit.own("An imported map"), CAP_MID, tw), x + PAD, y + PAD + LINE_PITCH * 1.2f,
                    CAP_MID, INK, true);

        final float by = buttonY(kit);
        for (int i = 0; i < 2; ++i) {
            kit.nine(c, buttonPlain, buttonX(i), by, BUTTON_W, BUTTON_H, 14);
            kit.textCentre(c, kit.own(CAPTIONS[i]), buttonX(i), by, BUTTON_W, BUTTON_H, CAP_BODY, INK, true);
        }
        kit.nine(c, buttonMarked, buttonX(2), by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.string("common_cancel", "Cancel"), buttonX(2), by, BUTTON_W, BUTTON_H, CAP_BODY,
                       GOLD, true);
    }

    /** A finger was lifted at (x, y), in logical units. A touch beside the buttons does nothing. */
    int up(float px, float py, SkinKit kit) {
        final float by = buttonY(kit);
        if (py < by || py >= by + BUTTON_H) {
            return NONE;
        }
        for (int i = 0; i < 3; ++i) {
            if (px >= buttonX(i) && px < buttonX(i) + BUTTON_W) {
                return (i == 0) ? RENAME : (i == 1) ? REMOVE : CANCEL;
            }
        }
        return NONE;
    }
}
