package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

/**
 * The Missions page of one campaign: the mission list at the left, the act's map and the
 * selected mission's objectives at the right, and the two actions.
 *
 * Missions 1 … completed + 1 can be played; the others carry the game's lock and can be
 * selected to read them, but not started.
 */
final class MissionsPage {
    static final int ACTION_NONE = 0, ACTION_PREVIOUS = 1, ACTION_START = 2;

    private static final String[] ROMAN = {
        "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII", "XIII", "XIV",
    };

    // Layout, logical units.
    private static final float X0 = 816f, TOP = 264f, BOTTOM_UP = 264f, RIGHT_X = 2304f, PANEL_W = 1440f;
    private static final float ROW_X = 840f, ROW_W = 1392f, ROW_H = 132f, ROW_PITCH = 138f;
    private static final float MAP_H = 880f, MAP_W = MAP_H * 1500f / 1100f;
    private static final float LINE_PITCH = 82.5f;
    private static final float ACTION_W = 720f, ACTION_H = 120f, ACTION_UP = 204f;
    private static final float CAP_TITLE = 60f, CAP_BODY = 44f, CAP_MID = 36f, CAP_SMALL = 30f;

    private static final int PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E, WELL = 0x78000000;
    private static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, GOLD = 0xFFF0D250;
    private static final int INK_ON_GOLD = 0xFF1E1608, INK_TITLE = 0xFF000000, INK_SUB = 0xFF100A02;
    private static final int INK_DISABLED = 0xFF9C9486, DIM = 0x96000000;
    private static final int ROW_SELECTED = 0x36F0D250, ROW_FAINT = 0x10FFFFFF, SEG_FAINT = 0x0EFFFFFF;

    private CampaignPage.Campaign campaign;
    /** 0-based index of the selected mission. */
    private int selected;
    private float scroll;

    private volatile Bitmap lock, map, buttonPlain, buttonPrimary;
    private volatile int mapAct = -1;
    private volatile String mapCode;
    private boolean framesAsked;
    /** Asks for the page to be drawn again; may be called from any thread. */
    Runnable redraw;

    // List dragging.
    private float downY, downScroll;
    private boolean inList, dragging;

    /** The list has to be moved so that the selected row is seen (it may not fit the panel). */
    private boolean reveal;
    /** A line shown in place of the campaign's caption after a session, or null. */
    String notice;

    /** The name a mission is listed by: "<roman>. <name>". */
    static String listed(SkinKit kit, CampaignPage.Campaign c, int mission) {
        return ROMAN[mission - 1] + ". " + kit.string(c.code + "_" + mission + "_name", "");
    }

    /** Show a campaign with its first unplayed mission selected. */
    void open(CampaignPage.Campaign c) {
        notice = null;
        reveal = true;
        campaign = c;
        selected = Math.min(c.completed, c.missions - 1);
        scroll = 0f;
    }

    CampaignPage.Campaign campaign() {
        return campaign;
    }

    /** 1-based number of the selected mission. */
    int mission() {
        return selected + 1;
    }

    private boolean playable(int index) {
        return index <= campaign.completed;
    }

    /** Which of the four acts a mission belongs to (1-based), by the game's act ranges. */
    private int actOf(int mission) {
        final int[] ends = (campaign.missions >= 13) ? new int[] { 4, 7, 11, 14 }
                                                     : new int[] { 3, 6, 9, 12 };
        for (int a = 0; a < ends.length; ++a) {
            if (mission <= ends[a]) {
                return a + 1;
            }
        }
        return ends.length;
    }

    private float bottom(SkinKit kit) { return kit.height - BOTTOM_UP; }
    private float listTop() { return TOP + 24f; }
    private float listBottom(SkinKit kit) { return bottom(kit) - 24f; }
    private float actionY(SkinKit kit) { return kit.height - ACTION_UP; }
    private float startX() { return Screen.CANVAS_W - 96f - ACTION_W; }

    private float maxScroll(SkinKit kit) {
        return Math.max(0f, campaign.missions * ROW_PITCH - 6f - (listBottom(kit) - listTop()));
    }

    private void panel(Canvas c, SkinKit kit, float x, float y, float w, float h) {
        kit.fill(c, x, y, w, h, PANEL_FILL, 18f);
        kit.outline(c, x, y, w, h, LINE, 18f);
    }

    void draw(Canvas c, SkinKit kit) {
        // Pictures are cut out of their atlases off the main thread: the page is drawn without
        // them first and again when they are there.
        final SkinKit k = kit;
        if (!framesAsked) {
            framesAsked = true;
            new Thread(new Runnable() {
                @Override
                public void run() {
                    lock = k.atlasFrame("Modern_Graphics", "icon_lock", 80, 80);
                    buttonPlain = k.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
                    buttonPrimary = k.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
                    if (redraw != null) {
                        redraw.run();
                    }
                }
            }, "missions-frames").start();
        }
        if (reveal) {
            reveal = false;
            final float shown = listBottom(kit) - listTop();
            final float rowEnd = (selected + 1) * ROW_PITCH - 6f;
            scroll = Math.max(0f, Math.min(maxScroll(kit), rowEnd - shown));
        }
        final int act = actOf(mission());
        if (act != mapAct || !campaign.code.equals(mapCode)) {
            mapAct = act;
            mapCode = campaign.code;
            map = null;
            final String code = campaign.code;
            final int mw = Math.round(MAP_W * kit.unit), mh = Math.round(MAP_H * kit.unit);
            new Thread(new Runnable() {
                @Override
                public void run() {
                    final Bitmap picture = k.atlasFrame("Maps", code + "_act" + act, mw, mh);
                    // Only the picture still asked for is kept.
                    if (act == mapAct && code.equals(mapCode)) {
                        map = picture;
                        if (redraw != null) {
                            redraw.run();
                        }
                    }
                }
            }, "missions-map").start();
        }

        final boolean expansion = campaign.code.startsWith("x");
        kit.textTop(c, kit.string(expansion ? "campaign_expansion" : "campaign_base",
                                  expansion ? "Expansion" : "Campaign"),
                    X0, 84f, CAP_TITLE, INK_TITLE, false);
        kit.textTop(c, notice != null ? notice : kit.string(campaign.captionKey, ""), X0, 180f, CAP_SMALL,
                    INK_SUB, false);

        // Left: the missions.
        final float b = bottom(kit);
        panel(c, kit, X0, TOP, PANEL_W, b - TOP);
        c.save();
        c.clipRect(kit.rect(ROW_X, listTop(), ROW_W, listBottom(kit) - listTop()));
        for (int i = 0; i < campaign.missions; ++i) {
            final float y = listTop() + i * ROW_PITCH - scroll;
            if (y + ROW_H < listTop() || y > listBottom(kit)) {
                continue;
            }
            final boolean sel = (i == selected);
            kit.fill(c, ROW_X, y, ROW_W, ROW_H, sel ? ROW_SELECTED : ROW_FAINT, 10f);
            if (sel) {
                kit.fill(c, ROW_X, y, 10f, ROW_H, GOLD, 0f);
            }
            final String name = ROMAN[i] + ". " + kit.string(campaign.code + "_" + (i + 1) + "_name", "");
            kit.textMiddle(c, kit.fit(name, CAP_BODY, ROW_W - 36f - 150f), ROW_X + 36f, y, ROW_H,
                           CAP_BODY, playable(i) ? INK : INK_MUTED, true);
            if (!playable(i)) {
                kit.picture(c, lock, ROW_X + ROW_W - 110f, y + 26f, 80f, 80f);
            }
        }
        c.restore();

        // Right: the act's map, then the selected mission.
        panel(c, kit, RIGHT_X, TOP, PANEL_W, MAP_H + 48f);
        kit.picture(c, map, RIGHT_X + (PANEL_W - MAP_W) / 2f, TOP + 24f, MAP_W, MAP_H);

        final float oy = TOP + MAP_H + 48f + 48f;
        panel(c, kit, RIGHT_X, oy, PANEL_W, b - oy);
        final String title = ROMAN[selected] + ". "
            + kit.string(campaign.code + "_" + mission() + "_name", "");
        kit.textTop(c, kit.fit(title, CAP_BODY, PANEL_W - 120f), RIGHT_X + 60f, oy + 54f, CAP_BODY,
                    GOLD, true);
        final String[] lines =
            kit.string(campaign.code + "_" + mission() + "_objectives", "").trim().split("\n");
        final float lastLineY = bottom(kit) - 60f - CAP_MID;
        for (int j = 0; j < lines.length; ++j) {
            final float y = oy + 54f + LINE_PITCH * (j + 1.2f);
            if (y > lastLineY) {
                break;
            }
            kit.textTop(c, kit.fit(lines[j].trim(), CAP_MID, PANEL_W - 120f), RIGHT_X + 60f, y,
                        CAP_MID, INK, true);
        }

        // Actions: back at the left, start at the right.
        final float ay = actionY(kit);
        kit.nine(c, buttonPlain, X0, ay, ACTION_W, ACTION_H, 14);
        kit.textCentre(c, kit.string("common_previous", "Previous"), X0, ay, ACTION_W, ACTION_H,
                       CAP_BODY, INK, true);
        final String start = kit.string("customscenario_start", "Start Game");
        if (playable(selected)) {
            kit.nine(c, buttonPrimary, startX(), ay, ACTION_W, ACTION_H, 14);
            kit.textCentre(c, start, startX(), ay, ACTION_W, ACTION_H, CAP_BODY, GOLD, true);
        } else {
            kit.nine(c, buttonPlain, startX(), ay, ACTION_W, ACTION_H, 14);
            kit.fill(c, startX(), ay, ACTION_W, ACTION_H, DIM, 0f);
            kit.textCentre(c, start, startX(), ay, ACTION_W, ACTION_H, CAP_BODY, INK_DISABLED, false);
        }
    }

    private static boolean in(float x, float y, float rx, float ry, float rw, float rh) {
        return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
    }

    void down(float x, float y, SkinKit kit) {
        downY = y;
        downScroll = scroll;
        dragging = false;
        inList = in(x, y, ROW_X, listTop(), ROW_W, listBottom(kit) - listTop());
    }

    /** @return true when the page must be redrawn */
    boolean move(float x, float y, SkinKit kit) {
        if (!inList) {
            return false;
        }
        if (Math.abs(y - downY) > 16f) {
            dragging = true;
        }
        if (dragging) {
            scroll = Math.max(0f, Math.min(maxScroll(kit), downScroll - (y - downY)));
        }
        return dragging;
    }

    /** A finger was lifted at (x, y). @return what the shell has to do */
    int up(float x, float y, SkinKit kit) {
        final boolean wasDrag = dragging;
        dragging = false;
        final boolean wasInList = inList;
        inList = false;
        if (wasDrag) {
            return ACTION_NONE;
        }
        if (wasInList) {
            final int row = (int) ((y - listTop() + scroll) / ROW_PITCH);
            if (row >= 0 && row < campaign.missions) {
                selected = row;
            }
        } else if (in(x, y, X0, actionY(kit), ACTION_W, ACTION_H)) {
            return ACTION_PREVIOUS;
        } else if (in(x, y, startX(), actionY(kit), ACTION_W, ACTION_H) && playable(selected)) {
            return ACTION_START;
        }
        return ACTION_NONE;
    }
}
