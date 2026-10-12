package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

import net.wargushd.game.editor.BalanceWatch;
import net.wargushd.game.editor.MapMetrics;

/**
 * The editor's sheet of a map's balance: the fairness of the map as it stands, one row a
 * player with what the fairness is made of - the walk to the nearest gold mine, the share of
 * the map's gold, the forest and the room to build near the start, the distance to the other
 * players - and the part in which the players differ most.
 */
final class BalanceSheet {
    static final int NONE = 0, CLOSE = 1;

    // Layout, logical units; y from the sheet's top.
    private static final float W = 2880f, H = 1140f, PAD = 48f;
    private static final float TITLE_Y = 36f, HEAD_Y = 126f, HEAD_H = 48f, ROW_Y = 186f, ROW_PITCH = 84f, ROW_H = 72f;
    private static final float NAME_X = 48f, NAME_W = 420f, SWATCH = 48f, COLUMN_X = 492f, COLUMN_W = 468f;
    private static final float LEAST_Y = 876f, LEAST_H = 60f;
    private static final float BUTTON_Y = 984f, BUTTON_W = 720f, BUTTON_H = 120f;
    private static final float CAP_BODY = 44f, CAP_MID = 36f, CAP_SMALL = 30f;
    private static final int ROWS = 8;

    private static final int SHADE = 0xAA000000, PANEL_FILL = 0xF2140F0A, LINE = 0xFF92743E;
    private static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, GOLD = 0xFFF0D250;

    private static final String[] COLUMN_WORD = { "Walk to the mine", "Gold share", "Wood", "Building room",
        "Distance to opponents" };
    /** The parts of the fairness, in the order of MapMetrics.PART_... */
    private static final String[] PART_WORD = { "the gold share", "the walk to the mine", "the wood",
        "the building room", "the distance to opponents", "the width of the way to the opponent", "the oil share" };

    private final BalanceWatch.Answer answer;
    private Bitmap buttonMarked;

    BalanceSheet(BalanceWatch.Answer answer) {
        this.answer = answer;
    }

    private float x() { return (Screen.CANVAS_W - W) / 2f; }
    private float y(SkinKit kit) { return kit.height / 2f - H / 2f; }
    private float closeX() { return x() + W - PAD - BUTTON_W; }

    /** A distance in tiles with one decimal; "-" where there is no way. */
    private static String tiles(double d) {
        return (Double.isInfinite(d) || Double.isNaN(d)) ? "-" : String.format(java.util.Locale.ROOT, "%.1f", d);
    }

    private static String cell(MapMetrics.Measure m, MapMetrics.Player p, int column) {
        switch (column) {
            case 0: return tiles(p.goldWalk);
            case 1: return Math.round(p.goldShare * 100.0) + " %";
            case 2: return String.valueOf(p.wood);
            case 3: return String.valueOf(p.build);
            default: return tiles((m.kind == MapMetrics.KIND_LAND) ? p.enemyMeanPath : p.enemyMeanLine);
        }
    }

    void draw(Canvas c, SkinKit kit) {
        if (buttonMarked == null) {
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        final float x = x(), y = y(kit);
        kit.fill(c, x, y, W, H, PANEL_FILL, 18f);
        kit.outline(c, x, y, W, H, LINE, 18f);
        final MapMetrics.Measure m = answer.measure;
        final int players = (m == null) ? 0 : m.per.length;
        final String title = (players < 2) ? kit.own("Balance") : kit.own("Balance {0} / 100", BalanceWatch.shown(answer.score));
        kit.textTop(c, kit.fit(title, CAP_BODY, W - PAD * 2f), x + PAD, y + TITLE_Y, CAP_BODY, GOLD, true);
        if (players < 2) {
            kit.textMiddle(c, kit.fit(kit.own("The balance needs the start locations of two players."), CAP_MID,
                                      W - PAD * 2f), x + PAD, y + ROW_Y, ROW_H, CAP_MID, INK_MUTED, true);
        } else {
            kit.textMiddle(c, kit.fit(kit.own("Player"), CAP_SMALL, NAME_W), x + NAME_X, y + HEAD_Y, HEAD_H, CAP_SMALL,
                           INK_MUTED, true);
            for (int k = 0; k < COLUMN_WORD.length; ++k) {
                kit.textCentre(c, kit.fit(kit.own(COLUMN_WORD[k]), CAP_SMALL, COLUMN_W - 24f), x + COLUMN_X + k * COLUMN_W,
                               y + HEAD_Y, COLUMN_W, HEAD_H, CAP_SMALL, INK_MUTED, true);
            }
            for (int i = 0; i < Math.min(ROWS, players); ++i) {
                final MapMetrics.Player p = m.per[i];
                final float ry = y + ROW_Y + i * ROW_PITCH;
                kit.fill(c, x + NAME_X, ry + (ROW_H - SWATCH) / 2f, SWATCH, SWATCH, Profiles.COLORS[p.slot & 7], 6f);
                kit.outline(c, x + NAME_X, ry + (ROW_H - SWATCH) / 2f, SWATCH, SWATCH, LINE, 6f);
                kit.textMiddle(c, kit.fit(kit.own("Player") + " " + (p.slot + 1), CAP_MID, NAME_W - SWATCH - 24f),
                               x + NAME_X + SWATCH + 24f, ry, ROW_H, CAP_MID, INK, true);
                for (int k = 0; k < COLUMN_WORD.length; ++k) {
                    kit.textCentre(c, kit.fit(cell(m, p, k), CAP_MID, COLUMN_W - 24f), x + COLUMN_X + k * COLUMN_W, ry,
                                   COLUMN_W, ROW_H, CAP_MID, INK, true);
                }
            }
            if (answer.weakest >= 0 && answer.weakest < PART_WORD.length) {
                kit.textMiddle(c, kit.fit(kit.own("Least even: {0}", kit.own(PART_WORD[answer.weakest])), CAP_MID,
                                          W - PAD * 2f), x + PAD, y + LEAST_Y, LEAST_H, CAP_MID, INK_MUTED, true);
            }
        }
        final float by = y + BUTTON_Y;
        kit.nine(c, buttonMarked, closeX(), by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.fit(kit.own("Close"), CAP_BODY, BUTTON_W - 48f), closeX(), by, BUTTON_W, BUTTON_H,
                       CAP_BODY, GOLD, true);
    }

    /** A finger was lifted at (px, py), in logical units of the screen. */
    int up(float px, float py, SkinKit kit) {
        final float by = y(kit) + BUTTON_Y;
        return (py >= by && py < by + BUTTON_H && px >= closeX() && px < closeX() + BUTTON_W) ? CLOSE : NONE;
    }
}
