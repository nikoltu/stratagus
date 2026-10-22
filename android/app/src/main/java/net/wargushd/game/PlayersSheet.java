package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

import net.wargushd.game.editor.MapModel;
import net.wargushd.game.editor.MapRules;

import java.util.ArrayList;

/**
 * The editor's sheet of the eight player slots of a map: who controls a slot, its race, the AI
 * of a computer and the gold, lumber and oil it starts with. A tap changes one setting; the
 * change is handed back as a step of the map, so that it can be undone like any other.
 *
 * The controller is one wide cell a row (owner decision 93, 1A): a tap opens a list of the six
 * controllers, each with its OWNR number and one line of what it does; a tap on a row sets it,
 * closes the list and is one step.
 */
final class PlayersSheet {
    /** CHANGE_VIEW: nothing of the map changed, but the sheet looks different (the list opened or closed). */
    static final int NONE = 0, CLOSE = 1, CHANGE = 2, CHANGE_VIEW = 3;

    // Layout, logical units; y from the sheet's top.
    private static final float W = 3360f, H = 1248f, PAD = 48f;
    private static final float TITLE_Y = 36f, HEAD_Y = 126f, HEAD_H = 48f, ROW_Y = 186f, ROW_PITCH = 108f, ROW_H = 96f;
    private static final float NAME_X = 288f, NAME_W = 290f, SWATCH = 60f;
    /** The Controller cell: the width of the three cells it replaces (owner board 1A). */
    private static final float OWNER_X = 600f, OWNER_W = 702f;
    private static final float RACE_X = 1338f, RACE_W = 204f, RACE_PITCH = 210f;
    private static final float AI_X = 1788f, AI_W = 264f;
    private static final float GOODS_X = 2088f, GOODS_PITCH = 432f, STEP_W = 96f, VALUE_W = 204f;
    private static final float BUTTON_Y = 1092f, BUTTON_W = 720f, BUTTON_H = 120f, SAME_X = 288f, CLOSE_X = 2832f;
    private static final float CAP_BODY = 44f, CAP_MID = 36f, CAP_SMALL = 30f;

    private static final int SHADE = 0xAA000000, PANEL_FILL = 0xF2140F0A, LINE = 0xFF92743E;
    private static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, GOLD = 0xFFF0D250;
    private static final int CELL_FILL = 0x10FFFFFF, CELL_CHOSEN = 0x36F0D250;

    // The controller list (owner board 1A): ChoiceSheet's width and padding, rows 160 at a pitch of 176.
    private static final float LIST_W = 1800f, LIST_H = 1436f, LIST_PAD = 96f, LIST_TITLE_Y = 60f;
    private static final float LIST_ROW_Y = 150f, LIST_ROW_H = 160f, LIST_ROW_PITCH = 176f, LIST_TEXT_X = 34f;
    private static final float LIST_WORD_Y = 30f, LIST_MEANING_Y = 98f, LIST_NUMBER_IN = 34f;
    private static final float LIST_CLOSE_Y = 1236f;
    private static final float CAP_WORD = 40f, CAP_NUMBER = 26f;
    private static final int LIST_FILL = 0xF5140F0A;

    /** The slot whose controller list is open, or -1. */
    private int listSlot = -1;
    private static final int[] RACES = { MapModel.RACE_HUMAN, MapModel.RACE_ORC };
    private static final String[][] RACE_WORD = { { "customscenario_race_human", "Human" },
        { "customscenario_race_orc", "Orc" } };
    /** The AI numbers a tap goes through, and their words. Any other number is shown and left as it is. */
    private static final int[] AIS = { MapRules.AI_LAND, MapRules.AI_SEA, MapRules.AI_AIR, MapRules.AI_PASSIVE };
    private static final String[] AI_WORD = { "Land attack", "Sea attack", "Air attack", "Passive" };
    private static final int[] GOODS = { MapModel.FIELD_GOLD, MapModel.FIELD_LUMBER, MapModel.FIELD_OIL };
    private static final String[][] GOODS_WORD = { { "victorydefeat_gold", "Gold" }, { "victorydefeat_lumber", "Lumber" },
        { "victorydefeat_oil", "Oil" } };
    /** A tap on - or + changes a starting amount by this much; the file holds 0 to 65535. */
    private static final int GOODS_STEP = 500, GOODS_MOST = 65000;

    private Bitmap buttonPlain, buttonMarked;
    /** The change the last tap asked for; read after {@link #up} returned CHANGE. */
    MapModel.Step step;

    private float x() { return (Screen.CANVAS_W - W) / 2f; }
    private float y(SkinKit kit) { return kit.height / 2f - H / 2f; }

    /** A column's heading from the game's table; in some languages the text is a label there and ends in a colon. */
    private static String heading(SkinKit kit, String[] word) {
        String s = kit.string(word[0], word[1]);
        if (s.endsWith(":") || s.endsWith("\uFF1A")) {
            // The space before the colon goes with it (French writes a no-break space there).
            s = s.substring(0, s.length() - 1).replace('\u00A0', ' ').trim();
        }
        return s;
    }

    /** The word of a controller, or its number in brackets when the list has none. */
    static String ownerWord(SkinKit kit, int owner) {
        final int i = MapRules.controllerIndex(owner);
        return (i < 0) ? "(" + owner + ")" : (MapRules.CONTROLLER_KEY[i] != null)
            ? kit.string(MapRules.CONTROLLER_KEY[i], MapRules.CONTROLLER_WORD[i]) : kit.own(MapRules.CONTROLLER_WORD[i]);
    }

    private float listX() { return (Screen.CANVAS_W - LIST_W) / 2f; }
    private float listY(SkinKit kit) { return kit.height / 2f - LIST_H / 2f; }

    /** The list is open; Back closes it first. */
    boolean listOpen() { return listSlot >= 0; }

    /** Close the list if it is open; true when it was. */
    boolean closeList() {
        final boolean was = listSlot >= 0;
        listSlot = -1;
        return was;
    }

    private void cell(Canvas c, SkinKit kit, float x, float y, float w, String word, boolean chosen, boolean live) {
        kit.fill(c, x, y, w, ROW_H, chosen ? CELL_CHOSEN : CELL_FILL, 10f);
        kit.outline(c, x, y, w, ROW_H, chosen ? GOLD : LINE, 10f);
        kit.textCentre(c, kit.fit(word, CAP_MID, w - 18f), x, y, w, ROW_H, CAP_MID,
                       chosen ? GOLD : live ? INK : INK_MUTED, true);
    }

    private static int aiIndex(int number) {
        for (int i = 0; i < AIS.length; ++i) {
            if (AIS[i] == number) {
                return i;
            }
        }
        return -1;
    }

    void draw(Canvas c, SkinKit kit, MapModel m) {
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        final float x = x(), y = y(kit);
        kit.fill(c, x, y, W, H, PANEL_FILL, 18f);
        kit.outline(c, x, y, W, H, LINE, 18f);
        kit.textTop(c, kit.string("mp_lobby_players", "Players"), x + PAD, y + TITLE_Y, CAP_BODY, GOLD, true);

        final float hy = y + HEAD_Y;
        kit.textCentre(c, kit.own("Controller"), OWNER_X, hy, OWNER_W, HEAD_H, CAP_SMALL, INK_MUTED, true);
        kit.textCentre(c, kit.string("mp_lobby_race", "Race"), RACE_X, hy, RACE_PITCH * 2f, HEAD_H, CAP_SMALL, INK_MUTED, true);
        kit.textCentre(c, kit.own("Computer plays"), AI_X, hy, AI_W, HEAD_H, CAP_SMALL, INK_MUTED, true);
        for (int k = 0; k < GOODS.length; ++k) {
            kit.textCentre(c, heading(kit, GOODS_WORD[k]), GOODS_X + k * GOODS_PITCH, hy, STEP_W * 2f + VALUE_W, HEAD_H,
                           CAP_SMALL, INK_MUTED, true);
        }
        for (int s = 0; s < MapRules.PLAYERS; ++s) {
            final float ry = y + ROW_Y + s * ROW_PITCH;
            final int owner = m.slot(MapModel.FIELD_OWNER, s);
            final boolean used = owner == MapModel.OWNER_PERSON || owner == MapModel.OWNER_COMPUTER;
            final boolean raceLive = MapRules.raceLive(owner), goodsLive = MapRules.goodsLive(owner);
            kit.fill(c, NAME_X, ry + (ROW_H - SWATCH) / 2f, SWATCH, SWATCH, Profiles.COLORS[s], 8f);
            kit.outline(c, NAME_X, ry + (ROW_H - SWATCH) / 2f, SWATCH, SWATCH, LINE, 8f);
            kit.textMiddle(c, kit.fit(kit.own("Player") + " " + (s + 1), CAP_MID, NAME_W - SWATCH - 24f),
                           NAME_X + SWATCH + 24f, ry, ROW_H, CAP_MID, (used || raceLive) ? INK : INK_MUTED, true);
            // One wide cell: the controller's word and the list mark (a number the list has not shows in brackets).
            cell(c, kit, OWNER_X, ry, OWNER_W, ownerWord(kit, owner) + "   v", false, true);
            for (int j = 0; j < RACES.length; ++j) {
                cell(c, kit, RACE_X + j * RACE_PITCH, ry, RACE_W, kit.string(RACE_WORD[j][0], RACE_WORD[j][1]),
                     m.slot(MapModel.FIELD_RACE, s) == RACES[j], raceLive);
            }
            final int ai = m.slot(MapModel.FIELD_AI, s), index = aiIndex(ai);
            cell(c, kit, AI_X, ry, AI_W, (index >= 0) ? kit.own(AI_WORD[index]) : String.valueOf(ai), false,
                 MapRules.aiLive(owner) && index >= 0);
            for (int k = 0; k < GOODS.length; ++k) {
                final float gx = GOODS_X + k * GOODS_PITCH;
                cell(c, kit, gx, ry, STEP_W, "-", false, goodsLive);
                kit.textCentre(c, String.valueOf(m.slot(GOODS[k], s)), gx + STEP_W, ry, VALUE_W, ROW_H, CAP_MID,
                               goodsLive ? INK : INK_MUTED, true);
                cell(c, kit, gx + STEP_W + VALUE_W, ry, STEP_W, "+", false, goodsLive);
            }
        }
        final float by = y + BUTTON_Y;
        kit.nine(c, buttonPlain, SAME_X, by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.fit(kit.own("Same for all"), CAP_BODY, BUTTON_W - 48f), SAME_X, by, BUTTON_W, BUTTON_H,
                       CAP_BODY, INK, true);
        kit.nine(c, buttonMarked, CLOSE_X, by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.fit(kit.own("Close"), CAP_BODY, BUTTON_W - 48f), CLOSE_X, by, BUTTON_W, BUTTON_H,
                       CAP_BODY, GOLD, true);
        if (listSlot >= 0) {
            drawList(c, kit, m);
        }
    }

    /** The controller list of one slot over the sheet (owner board 1A). */
    private void drawList(Canvas c, SkinKit kit, MapModel m) {
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        final float x = listX(), y = listY(kit), inner = LIST_W - LIST_PAD * 2f;
        kit.fill(c, x, y, LIST_W, LIST_H, LIST_FILL, 18f);
        kit.outline(c, x, y, LIST_W, LIST_H, LINE, 18f);
        kit.textTop(c, kit.fit(kit.own("Controller of Player {0}", listSlot + 1), CAP_BODY, inner), x + LIST_PAD,
                    y + LIST_TITLE_Y, CAP_BODY, GOLD, true);
        final int now = m.slot(MapModel.FIELD_OWNER, listSlot);
        for (int i = 0; i < MapRules.CONTROLLERS.length; ++i) {
            final float rx = x + LIST_PAD, ry = y + LIST_ROW_Y + i * LIST_ROW_PITCH;
            final boolean chosen = MapRules.CONTROLLERS[i] == now;
            kit.fill(c, rx, ry, inner, LIST_ROW_H, chosen ? CELL_CHOSEN : CELL_FILL, 10f);
            kit.outline(c, rx, ry, inner, LIST_ROW_H, chosen ? GOLD : LINE, 10f);
            final String number = "OWNR " + MapRules.CONTROLLERS[i];
            final float numberW = kit.width(number, CAP_NUMBER);
            kit.textTop(c, kit.fit(ownerWord(kit, MapRules.CONTROLLERS[i]), CAP_WORD, inner - LIST_TEXT_X * 2f - numberW - 24f),
                        rx + LIST_TEXT_X, ry + LIST_WORD_Y, CAP_WORD, chosen ? GOLD : INK, true);
            kit.textTopRight(c, number, rx + inner - LIST_NUMBER_IN, ry + LIST_WORD_Y + (CAP_WORD - CAP_NUMBER) / 2f,
                             CAP_NUMBER, INK_MUTED, true);
            kit.textTop(c, kit.fit(kit.own(MapRules.CONTROLLER_MEANING[i]), CAP_SMALL, inner - LIST_TEXT_X * 2f),
                        rx + LIST_TEXT_X, ry + LIST_MEANING_Y, CAP_SMALL, INK_MUTED, true);
        }
        final float bx = x + (LIST_W - BUTTON_W) / 2f, by = y + LIST_CLOSE_Y;
        kit.nine(c, buttonMarked, bx, by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.fit(kit.own("Close"), CAP_BODY, BUTTON_W - 48f), bx, by, BUTTON_W, BUTTON_H,
                       CAP_BODY, GOLD, true);
    }

    /** A tap while the list is open: a row sets the controller and closes the list; Close or outside only closes it. */
    private int listUp(float px, float py, SkinKit kit, MapModel m) {
        final float x = listX(), y = listY(kit);
        final int slot = listSlot;
        final float bx = x + (LIST_W - BUTTON_W) / 2f, by = y + LIST_CLOSE_Y;
        if (px >= bx && px < bx + BUTTON_W && py >= by && py < by + BUTTON_H) {
            listSlot = -1;
            return CHANGE_VIEW;
        }
        if (px < x || px >= x + LIST_W || py < y || py >= y + LIST_H) {
            listSlot = -1;
            return CHANGE_VIEW;
        }
        final float in = py - y - LIST_ROW_Y;
        final int i = (int) Math.floor(in / LIST_ROW_PITCH);
        if (in < 0f || i >= MapRules.CONTROLLERS.length || in - i * LIST_ROW_PITCH >= LIST_ROW_H
            || px < x + LIST_PAD || px >= x + LIST_W - LIST_PAD) {
            return NONE;
        }
        listSlot = -1;
        step = MapRules.owner(m, slot, MapRules.CONTROLLERS[i]);
        return (step != null) ? CHANGE : CHANGE_VIEW;
    }

    /** A finger was lifted at (px, py), in logical units of the screen. */
    int up(float px, float py, SkinKit kit, MapModel m) {
        step = null;
        if (listSlot >= 0) {
            return listUp(px, py, kit, m);
        }
        final float y = y(kit), by = y + BUTTON_Y;
        if (py >= by && py < by + BUTTON_H) {
            if (px >= CLOSE_X && px < CLOSE_X + BUTTON_W) {
                return CLOSE;
            }
            if (px >= SAME_X && px < SAME_X + BUTTON_W) {
                // The starting amounts of the first player for all eight.
                final ArrayList<MapModel.Step> parts = new ArrayList<MapModel.Step>();
                for (int s = 1; s < MapRules.PLAYERS; ++s) {
                    for (int field : GOODS) {
                        final MapModel.Step one = m.changeSlot(field, s, m.slot(field, 0));
                        if (one != null) {
                            parts.add(one);
                        }
                    }
                }
                step = parts.isEmpty() ? null : new MapModel.GroupStep(parts);
                return (step != null) ? CHANGE : NONE;
            }
            return NONE;
        }
        final float in = py - y - ROW_Y;
        final int s = (int) Math.floor(in / ROW_PITCH);
        if (in < 0f || s >= MapRules.PLAYERS || in - s * ROW_PITCH >= ROW_H) {
            return NONE;
        }
        if (px >= OWNER_X && px < OWNER_X + OWNER_W) {
            listSlot = s;
            return CHANGE_VIEW;
        }
        for (int j = 0; j < RACES.length; ++j) {
            if (px >= RACE_X + j * RACE_PITCH && px < RACE_X + j * RACE_PITCH + RACE_W) {
                step = MapRules.race(m, s, RACES[j]);
            }
        }
        if (px >= AI_X && px < AI_X + AI_W) {
            final int index = aiIndex(m.slot(MapModel.FIELD_AI, s));
            if (index >= 0) {
                step = m.changeSlot(MapModel.FIELD_AI, s, AIS[(index + 1) % AIS.length]);
            }
        }
        for (int k = 0; k < GOODS.length; ++k) {
            final float gx = GOODS_X + k * GOODS_PITCH;
            final int now = m.slot(GOODS[k], s);
            if (px >= gx && px < gx + STEP_W) {
                step = m.changeSlot(GOODS[k], s, Math.max(0, now - GOODS_STEP));
            } else if (px >= gx + STEP_W + VALUE_W && px < gx + STEP_W * 2f + VALUE_W) {
                step = m.changeSlot(GOODS[k], s, Math.min(GOODS_MOST, now + GOODS_STEP));
            }
        }
        return (step != null) ? CHANGE : NONE;
    }
}
