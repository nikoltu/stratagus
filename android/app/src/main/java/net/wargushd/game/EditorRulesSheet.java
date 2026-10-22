package net.wargushd.game;

import android.graphics.Canvas;

/**
 * The small list of the rules presets that the Rules row of editor_maps opens (owner decision
 * 89, 7A; LAUNCHER_DESIGN 31.11): Standard melee, Objective, None. Objective and None need
 * triggers (stage 2) and are dimmed; a tap on them does nothing. Where a preset is kept in a
 * .pud is not designed, so the list changes nothing yet: Standard melee is the one shown.
 */
final class EditorRulesSheet {
    static final int NONE = -1, MELEE = 0, CLOSED = 3;

    private static final String[] PRESET = { "Standard melee", "Objective", "None" };
    // Layout, logical units: the launcher's sheet 1800 wide with padding 96, centred; rows of 120 at a pitch of 138.
    private static final float W = 1800f, PAD = 96f, ROW_H = 120f, ROW_PITCH = 138f, TITLE_H = 96f;
    private static final float CAP_BODY = 44f, CAP_ROW = 40f;
    private static final int SHADE = 0xAA000000, PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E, ROW_FILL = 0x78000000;
    private static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, GOLD = 0xFFF0D250, DIM = 0x66000000;

    private float h() { return PAD + TITLE_H + PRESET.length * ROW_PITCH - (ROW_PITCH - ROW_H) + PAD; }
    private float x() { return (Screen.CANVAS_W - W) / 2f; }
    private float y(SkinKit kit) { return kit.height / 2f - h() / 2f; }
    private float rowY(SkinKit kit, int i) { return y(kit) + PAD + TITLE_H + i * ROW_PITCH; }

    void draw(Canvas c, SkinKit kit) {
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        final float x = x(), y = y(kit), h = h();
        kit.fill(c, x, y, W, h, PANEL_FILL, 18f);
        kit.outline(c, x, y, W, h, LINE, 18f);
        kit.textTop(c, kit.fit(kit.own("Rules"), CAP_BODY, W - 2f * PAD), x + PAD, y + PAD, CAP_BODY, GOLD, true);
        for (int i = 0; i < PRESET.length; ++i) {
            final float ry = rowY(kit, i), rw = W - 2f * PAD;
            kit.fill(c, x + PAD, ry, rw, ROW_H, ROW_FILL, 10f);
            kit.outline(c, x + PAD, ry, rw, ROW_H, (i == MELEE) ? GOLD : LINE, 10f);
            kit.textCentre(c, kit.fit(kit.own(PRESET[i]), CAP_ROW, rw - 48f), x + PAD, ry, rw, ROW_H, CAP_ROW,
                           (i == MELEE) ? GOLD : INK_MUTED, true);
            if (i != MELEE) {
                kit.fill(c, x + PAD, ry, rw, ROW_H, DIM, 10f);
            }
        }
    }

    /**
     * A finger was lifted at (x, y), logical units.
     *
     * @return MELEE for the live preset, CLOSED for a touch beside the sheet, NONE otherwise (a dimmed row)
     */
    int up(float px, float py, SkinKit kit) {
        final float x = x(), y = y(kit);
        if (px < x || px >= x + W || py < y || py >= y + h()) {
            return CLOSED;
        }
        final float ry = rowY(kit, MELEE);
        return (px >= x + PAD && px < x + W - PAD && py >= ry && py < ry + ROW_H) ? MELEE : NONE;
    }
}
