package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

/**
 * The sheet of a new, empty map: the choice Empty / Random, the map's size and its tileset,
 * each one of four cells, and the buttons Cancel and Create. The map's name is asked after it,
 * in the name sheet. Random hands over to the sheet of the random map.
 */
final class NewMapSheet {
    static final int NONE = 0, CANCEL = 1, CREATE = 2, CHOSE = 3, RANDOM = 4;

    static final int[] SIZES = { 32, 64, 96, 128 };

    // Layout, logical units: the kit's modal sheet, centred, inner padding 96; three rows of a
    // word and a segment of cells (two in the first row, four in the others), two buttons. A cell is wide enough for the longest
    // tileset name of the game ("Wasteland", "Orc Swamp") at the cells' letter size.
    private static final float W = 2400f, H = 936f, PAD = 96f;
    private static final float ROW_Y = 228f, ROW_PITCH = 156f, ROW_H = 108f;
    private static final float SEGMENT_X = 476f, SEGMENT_W = 1828f, CELL_W = 449.5f, CELL_H = 96f, CELL_GAP = 6f;
    private static final float KIND_W = 905f;
    private static final int ROWS = 3;
    private static final float BUTTON_W = 720f, BUTTON_H = 120f;
    private static final float CAP_BODY = 44f, CAP_MID = 36f, CAP_SMALL = 30f;

    private static final int SHADE = 0xAA000000, PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E;
    private static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, GOLD = 0xFFF0D250, INK_ON_GOLD = 0xFF1E1608;
    private static final int WELL = 0x78000000, CELL = 0x0EFFFFFF;

    /** The chosen size (an index of SIZES) and tileset (the map file's number, 0 to 3). */
    int size = 1, tileset;
    private Bitmap buttonPlain, buttonMarked;

    private float x() { return (Screen.CANVAS_W - W) / 2f; }
    private float y(SkinKit kit) { return kit.height / 2f - H / 2f; }
    private float buttonY(SkinKit kit) { return y(kit) + H - PAD - BUTTON_H; }
    private float cancelX() { return x() + PAD; }
    private float createX() { return x() + W - PAD - BUTTON_W; }
    private float cellX(int i) { return x() + SEGMENT_X + CELL_GAP + i * (CELL_W + CELL_GAP); }
    private float kindX(int i) { return x() + SEGMENT_X + CELL_GAP + i * (KIND_W + CELL_GAP); }
    private float cellY(SkinKit kit, int row) { return y(kit) + ROW_Y + row * ROW_PITCH + CELL_GAP; }

    void draw(Canvas c, SkinKit kit) {
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        final float x = x(), y = y(kit);
        kit.fill(c, x, y, W, H, PANEL_FILL, 18f);
        kit.outline(c, x, y, W, H, LINE, 18f);
        kit.textTop(c, kit.own("New Map"), x + PAD, y + PAD, CAP_BODY, GOLD, true);

        kit.textMiddle(c, kit.string("mp_lobby_map", "Map"), x + PAD, y + ROW_Y, ROW_H, CAP_BODY, INK_MUTED, true);
        kit.fill(c, x + SEGMENT_X, y + ROW_Y, SEGMENT_W, ROW_H, WELL, 14f);
        for (int i = 0; i < 2; ++i) {
            kit.fill(c, kindX(i), cellY(kit, 0), KIND_W, CELL_H, (i == 0) ? GOLD : CELL, 10f);
            kit.textCentre(c, kit.own(i == 0 ? "Empty" : "Random"), kindX(i), cellY(kit, 0), KIND_W, CELL_H, CAP_MID,
                           (i == 0) ? INK_ON_GOLD : INK, i != 0);
        }
        for (int row = 1; row < ROWS; ++row) {
            final float ry = y + ROW_Y + row * ROW_PITCH;
            kit.textMiddle(c, kit.own(row == 1 ? "Size" : "Tileset"), x + PAD, ry, ROW_H, CAP_BODY, INK_MUTED, true);
            kit.fill(c, x + SEGMENT_X, ry, SEGMENT_W, ROW_H, WELL, 14f);
            for (int i = 0; i < 4; ++i) {
                final boolean chosen = (row == 1) ? i == size : i == tileset;
                final String word = (row == 1) ? String.valueOf(SIZES[i]) : LanSettings.tilesetName(kit, i);
                final float cap = (row == 1) ? CAP_MID : CAP_SMALL;
                kit.fill(c, cellX(i), cellY(kit, row), CELL_W, CELL_H, chosen ? GOLD : CELL, 10f);
                kit.textCentre(c, kit.fit(word, cap, CELL_W - 12f), cellX(i), cellY(kit, row), CELL_W, CELL_H, cap,
                               chosen ? INK_ON_GOLD : INK, !chosen);
            }
        }

        final float by = buttonY(kit);
        kit.nine(c, buttonMarked, cancelX(), by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.string("common_cancel", "Cancel"), cancelX(), by, BUTTON_W, BUTTON_H, CAP_BODY,
                       GOLD, true);
        kit.nine(c, buttonPlain, createX(), by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.own("Create"), createX(), by, BUTTON_W, BUTTON_H, CAP_BODY, INK, true);
    }

    /**
     * A finger was lifted at (x, y), in logical units. CHOSE: a cell was chosen that was not.
     * RANDOM: the player wants a random map.
     */
    int up(float px, float py, SkinKit kit) {
        if (px >= kindX(1) && px < kindX(1) + KIND_W && py >= cellY(kit, 0) && py < cellY(kit, 0) + CELL_H) {
            return RANDOM;
        }
        for (int row = 1; row < ROWS; ++row) {
            final float cy = cellY(kit, row);
            for (int i = 0; i < 4; ++i) {
                if (px >= cellX(i) && px < cellX(i) + CELL_W && py >= cy && py < cy + CELL_H) {
                    final boolean was = (row == 1) ? i == size : i == tileset;
                    if (row == 1) {
                        size = i;
                    } else {
                        tileset = i;
                    }
                    return was ? NONE : CHOSE;
                }
            }
        }
        final float by = buttonY(kit);
        if (py < by || py >= by + BUTTON_H) {
            return NONE;
        }
        if (px >= cancelX() && px < cancelX() + BUTTON_W) {
            return CANCEL;
        }
        if (px >= createX() && px < createX() + BUTTON_W) {
            return CREATE;
        }
        return NONE;
    }
}
