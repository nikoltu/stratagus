package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

import net.wargushd.game.editor.MapRules;

import java.util.List;

/**
 * The editor's sheet of what the checks found in a map before it is saved: one line a fault,
 * with Show where the fault has a place on the map. A fault that stops Save is in plain ink, a
 * remark that does not is muted; with remarks only the sheet offers Save.
 */
final class FaultSheet {
    static final int NONE = 0, CLOSE = 1, SAVE = 2, SHOW = 3;

    // Layout, logical units; y from the sheet's top.
    private static final float W = 2400f, H = 1200f, PAD = 48f;
    private static final float TITLE_Y = 36f, ROW_Y = 138f, ROW_PITCH = 108f, ROW_H = 96f;
    private static final float SHOW_W = 396f;
    private static final float BUTTON_Y = 1044f, BUTTON_W = 720f, BUTTON_H = 120f;
    private static final float CAP_BODY = 44f, CAP_MID = 36f;
    private static final int ROWS = 8;

    private static final int SHADE = 0xAA000000, PANEL_FILL = 0xF2140F0A, LINE = 0xFF92743E;
    private static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, GOLD = 0xFFF0D250, CELL_FILL = 0x10FFFFFF;

    private final List<MapRules.Fault> faults;
    private final boolean blocked;
    private Bitmap buttonPlain, buttonMarked;
    /** The tile to show; read after {@link #up} returned SHOW. */
    int showX, showY;

    /** True when the sheet stands before a test game instead of before Save. */
    private final boolean test;

    FaultSheet(List<MapRules.Fault> faults) {
        this(faults, false);
    }

    /** As above; with `test` the title speaks of the test game the faults stop. */
    FaultSheet(List<MapRules.Fault> faults, boolean test) {
        this.faults = faults;
        this.test = test;
        blocked = MapRules.blocked(faults);
    }

    /** True when the sheet stands before a test game: its button (SAVE) goes on with the test. */
    boolean forTest() { return test; }

    private float x() { return (Screen.CANVAS_W - W) / 2f; }
    private float y(SkinKit kit) { return kit.height / 2f - H / 2f; }
    private float showX() { return x() + W - PAD - SHOW_W; }
    private float saveX() { return x() + PAD; }
    private float closeX() { return x() + W - PAD - BUTTON_W; }

    /** The lines shown: all of them, or one fewer than fit and a count of the rest. */
    private int lines() { return (faults.size() <= ROWS) ? faults.size() : ROWS - 1; }

    void draw(Canvas c, SkinKit kit) {
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        final float x = x(), y = y(kit);
        kit.fill(c, x, y, W, H, PANEL_FILL, 18f);
        kit.outline(c, x, y, W, H, LINE, 18f);
        kit.textTop(c, kit.fit(kit.own(test ? (blocked ? "The map cannot be tested yet" : "Before the map is tested")
                                            : blocked ? "The map cannot be saved yet" : "Before the map is saved"),
                               CAP_BODY, W - PAD * 2f), x + PAD, y + TITLE_Y, CAP_BODY, GOLD, true);
        final float tw = W - PAD * 3f - SHOW_W;
        for (int i = 0; i < lines(); ++i) {
            final MapRules.Fault f = faults.get(i);
            final float ry = y + ROW_Y + i * ROW_PITCH;
            kit.textMiddle(c, kit.fit(kit.own(f.text, f.values), CAP_MID, tw), x + PAD, ry, ROW_H, CAP_MID,
                           f.blocks ? INK : INK_MUTED, true);
            if (f.x >= 0) {
                kit.fill(c, showX(), ry, SHOW_W, ROW_H, CELL_FILL, 10f);
                kit.outline(c, showX(), ry, SHOW_W, ROW_H, LINE, 10f);
                kit.textCentre(c, kit.fit(kit.own("Show"), CAP_MID, SHOW_W - 18f), showX(), ry, SHOW_W, ROW_H,
                               CAP_MID, INK, true);
            }
        }
        if (faults.size() > lines()) {
            kit.textMiddle(c, kit.fit(kit.own("and {0} more", faults.size() - lines()), CAP_MID, tw), x + PAD,
                           y + ROW_Y + lines() * ROW_PITCH, ROW_H, CAP_MID, INK_MUTED, true);
        }
        final float by = y + BUTTON_Y;
        if (!blocked) {
            kit.nine(c, buttonPlain, saveX(), by, BUTTON_W, BUTTON_H, 14);
            // Before a test game the button goes on with it (the engine's remarks, Session 2026-10-19 e).
            final String go = test ? kit.own("Test") : kit.string("common_save", "Save");
            kit.textCentre(c, kit.fit(go, CAP_BODY, BUTTON_W - 48f), saveX(), by, BUTTON_W, BUTTON_H,
                           CAP_BODY, INK, true);
        }
        kit.nine(c, buttonMarked, closeX(), by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.fit(kit.own("Close"), CAP_BODY, BUTTON_W - 48f), closeX(), by, BUTTON_W, BUTTON_H,
                       CAP_BODY, GOLD, true);
    }

    /** A finger was lifted at (px, py), in logical units of the screen. */
    int up(float px, float py, SkinKit kit) {
        final float y = y(kit), by = y + BUTTON_Y;
        if (py >= by && py < by + BUTTON_H) {
            if (px >= closeX() && px < closeX() + BUTTON_W) {
                return CLOSE;
            }
            return (!blocked && px >= saveX() && px < saveX() + BUTTON_W) ? SAVE : NONE;
        }
        final float in = py - y - ROW_Y;
        final int row = (int) Math.floor(in / ROW_PITCH);
        if (in >= 0f && row < lines() && in - row * ROW_PITCH < ROW_H && px >= showX() && px < showX() + SHOW_W
            && faults.get(row).x >= 0) {
            showX = faults.get(row).x;
            showY = faults.get(row).y;
            return SHOW;
        }
        return NONE;
    }
}
