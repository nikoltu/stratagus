package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

/**
 * The dialog shown when the game has returned after a campaign's last mission was won: the
 * game's own victory panel with the campaign's victory title, the last mission's name and
 * two buttons.
 */
final class CampaignCompleteSheet {
    static final int NONE = 0, MISSION_LIST = 1, OK = 2;

    // Layout, logical units.
    private static final float PANEL_X = 1096f, PANEL_W = 1648f, PANEL_H = 616f, PANEL_UP = 420f;
    private static final float BUTTON_W = 720f, BUTTON_H = 120f, BUTTON_DOWN = 680f;
    private static final float LEFT_X = 1168f, RIGHT_X = 1952f;
    private static final float CAP_BODY = 44f;

    private static final int SHADE = 0xAA000000, INK = 0xFFE8E2D4, GOLD = 0xFFF0D250;
    private static final int INK_TITLE = 0xFF000000;

    private final boolean orc;
    private final String mission;
    private Bitmap panel, buttonPlain, buttonMarked;

    /** @param mission the last mission as it is listed, "XIV. <name>" */
    CampaignCompleteSheet(boolean orc, String mission) {
        this.orc = orc;
        this.mission = mission;
    }

    private float y(SkinKit kit) { return kit.height / 2f - PANEL_UP; }

    void draw(Canvas c, SkinKit kit) {
        if (panel == null) {
            panel = kit.atlasFrame("Modern_Graphics", orc ? "popup_victory_orc" : "popup_victory_human",
                                   Math.round(PANEL_W * kit.unit), Math.round(PANEL_H * kit.unit));
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        final float y = y(kit);
        kit.picture(c, panel, PANEL_X, y, PANEL_W, PANEL_H);
        kit.textCentre(c, kit.string(orc ? "orc_victory_title" : "human_victory_title",
                                     "Campaign complete"),
                       1396f, y + 165f, 1048f, 150f, CAP_BODY, INK_TITLE, false);
        kit.textCentre(c, kit.fit(mission, CAP_BODY, 1448f), 1196f, y + 350f, 1448f, 150f, CAP_BODY, INK,
                       true);

        final float by = y + BUTTON_DOWN;
        kit.nine(c, buttonPlain, LEFT_X, by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.own("Mission List"), LEFT_X, by, BUTTON_W, BUTTON_H, CAP_BODY, INK, true);
        kit.nine(c, buttonMarked, RIGHT_X, by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.string("common_ok", "OK"), RIGHT_X, by, BUTTON_W, BUTTON_H, CAP_BODY, GOLD,
                       true);
    }

    /** A finger was lifted at (x, y), in logical units. */
    int up(float px, float py, SkinKit kit) {
        final float by = y(kit) + BUTTON_DOWN;
        if (py < by || py >= by + BUTTON_H) {
            return NONE;
        }
        if (px >= LEFT_X && px < LEFT_X + BUTTON_W) {
            return MISSION_LIST;
        }
        if (px >= RIGHT_X && px < RIGHT_X + BUTTON_W) {
            return OK;
        }
        return NONE;
    }
}
