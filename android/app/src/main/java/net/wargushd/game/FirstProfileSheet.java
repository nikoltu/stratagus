package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

/**
 * The first start in one step (owner's board 2B, 2026-10-18): one sheet above the keyboard
 * with the name, the portrait and the colour together, and Start. The name is typed into
 * `editor` (a NameSheet in place); the portrait is chosen in the gallery.
 */
final class FirstProfileSheet {
    static final int NONE = 0, PORTRAIT = 1, COLOR = 2, START = 3;

    // Layout, logical units (board 2B, first start): the panel at the top, clear of the keyboard.
    private static final float X = 720f, Y = 96f, W = 2400f, H = 1150f, IN_X = 816f;
    private static final float FACE_Y = 286f, FACE_W = 560f, FACE_H = 462f, PORTRAIT_Y = 786f;
    private static final float RIGHT_X = 1480f, FIELD_Y = 346f, FIELD_W = 1544f, FIELD_H = 120f;
    private static final float SWATCH_Y = 576f, SWATCH_W = 174f, SWATCH_H = 96f, SWATCH_DX = 194f;
    private static final float START_X = 2304f, START_Y = 1030f, BUTTON_W = 720f, BUTTON_H = 120f;
    private static final float CAP_BODY = 44f, CAP_MID = 36f;

    private static final int SHADE = 0xAA000000, PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E;
    private static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, GOLD = 0xFFF0D250;
    private static final int FACE_FILL = 0xFF2A2218, SWATCH_LINE = 0xFFECE6D6;

    /** The name as it is typed. */
    NameSheet editor;
    /** The colour index of the last swatch tapped. */
    int pickedColor;

    private Bitmap buttonPlain, buttonMarked, face;
    private int faceSlot = -1, faceColor = -1;

    void draw(Canvas c, SkinKit kit, Profiles.Profile p) {
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        if (faceSlot != p.portrait || faceColor != p.color) {
            faceSlot = p.portrait;
            faceColor = p.color;
            face = Portraits.tinted(kit, p.portrait, Profiles.COLORS[p.color]);
        }
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        kit.fill(c, X, Y, W, H, PANEL_FILL, 18f);
        kit.outline(c, X, Y, W, H, LINE, 18f);
        kit.textTop(c, kit.own("Your profile"), IN_X, Y + 72f, CAP_BODY, GOLD, true);

        // Left: the portrait in the profile's colour, and the way to the gallery.
        kit.fill(c, IN_X, FACE_Y, FACE_W, FACE_H, FACE_FILL, 10f);
        kit.picture(c, face, IN_X, FACE_Y, FACE_W, FACE_H);
        kit.outline(c, IN_X, FACE_Y, FACE_W, FACE_H, LINE, 10f);
        kit.nine(c, buttonPlain, IN_X, PORTRAIT_Y, FACE_W, BUTTON_H, 14);
        kit.textCentre(c, kit.fit(kit.own("Portrait") + "  >", CAP_BODY, FACE_W - 48f), IN_X, PORTRAIT_Y, FACE_W,
                       BUTTON_H, CAP_BODY, INK, true);

        // Right: the name being typed, the colours.
        kit.textTop(c, kit.own("Name"), RIGHT_X, FIELD_Y - 58f, CAP_MID, INK_MUTED, true);
        if (editor != null) {
            editor.drawField(c, kit, RIGHT_X, FIELD_Y, FIELD_W, FIELD_H, CAP_BODY);
        } else {
            kit.textMiddle(c, p.name, RIGHT_X + 36f, FIELD_Y, FIELD_H, CAP_BODY, INK, true);
        }
        kit.textTop(c, kit.own("Colour"), RIGHT_X, SWATCH_Y - 58f, CAP_MID, INK_MUTED, true);
        for (int i = 0; i < Profiles.COLORS.length; ++i) {
            final float rx = RIGHT_X + SWATCH_DX * i;
            if (i == p.color) {
                kit.fill(c, rx - 9f, SWATCH_Y - 9f, SWATCH_W + 18f, SWATCH_H + 18f, GOLD, 18f);
            }
            kit.fill(c, rx, SWATCH_Y, SWATCH_W, SWATCH_H, Profiles.COLORS[i], 12f);
            kit.outline(c, rx, SWATCH_Y, SWATCH_W, SWATCH_H, SWATCH_LINE, 12f);
        }

        kit.nine(c, buttonMarked, START_X, START_Y, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.fit(kit.own("Start"), CAP_BODY, BUTTON_W - 48f), START_X, START_Y, BUTTON_W,
                       BUTTON_H, CAP_BODY, GOLD, true);
    }

    private static boolean in(float x, float y, float rx, float ry, float rw, float rh) {
        return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
    }

    /** A finger was lifted at (x, y), in logical units. */
    int up(float x, float y) {
        for (int i = 0; i < Profiles.COLORS.length; ++i) {
            if (in(x, y, RIGHT_X + SWATCH_DX * i, SWATCH_Y, SWATCH_W, SWATCH_H)) {
                pickedColor = i;
                return COLOR;
            }
        }
        if (in(x, y, IN_X, FACE_Y, FACE_W, PORTRAIT_Y + BUTTON_H - FACE_Y)) {
            return PORTRAIT;
        }
        if (in(x, y, START_X, START_Y, BUTTON_W, BUTTON_H)) {
            return START;
        }
        return NONE;
    }
}
