package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

/**
 * A small modal sheet that asks one question: a title, one line under it, and two buttons.
 * The button that changes nothing is the marked one and is what Back means.
 *
 * As a notice it tells one thing and has one button, OK: what the shell has to say after a game
 * that closed unexpectedly or a start that was refused.
 */
final class ConfirmSheet {
    static final int NONE = 0, CANCEL = 1, CONFIRM = 2;

    // Layout, logical units: the kit's modal sheet — centred, inner padding 96.
    private static final float W = 1800f, H = 720f, PAD = 96f;
    private static final float BUTTON_W = 720f, BUTTON_H = 120f, LINE_PITCH = 82.5f;
    private static final float CAP_BODY = 44f, CAP_MID = 36f;
    // A notice: its sheet is 600 high and its one button 600 wide, at the right.
    private static final float NOTICE_H = 600f, NOTICE_BUTTON_W = 600f;

    private static final int SHADE = 0xAA000000, PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E;
    private static final int INK = 0xFFE8E2D4, GOLD = 0xFFF0D250;

    private final String title, body, cancel, confirm;
    private final boolean notice;
    private Bitmap buttonPlain, buttonMarked;

    ConfirmSheet(String title, String body, String cancel, String confirm) {
        this.title = title;
        this.body = body;
        this.cancel = cancel;
        this.confirm = confirm;
        this.notice = false;
    }

    private ConfirmSheet(String title, String body) {
        this.title = title;
        this.body = body;
        this.cancel = "OK";
        this.confirm = null;
        this.notice = true;
    }

    /** A sheet that says one thing; its OK answers as CANCEL (nothing changes). */
    static ConfirmSheet notice(String title, String body) {
        return new ConfirmSheet(title, body);
    }

    private float h() { return notice ? NOTICE_H : H; }
    private float x() { return (Screen.CANVAS_W - W) / 2f; }
    private float y(SkinKit kit) { return kit.height / 2f - h() / 2f; }
    private float buttonY(SkinKit kit) { return y(kit) + h() - PAD - BUTTON_H; }
    private float cancelW() { return notice ? NOTICE_BUTTON_W : BUTTON_W; }
    private float cancelX() { return notice ? x() + W - PAD - NOTICE_BUTTON_W : x() + PAD; }
    private float confirmX() { return x() + W - PAD - BUTTON_W; }

    void draw(Canvas c, SkinKit kit) {
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        final float x = x(), y = y(kit);
        kit.fill(c, x, y, W, h(), PANEL_FILL, 18f);
        kit.outline(c, x, y, W, h(), LINE, 18f);

        final float tw = W - PAD * 2f;
        kit.textTop(c, kit.fit(title, CAP_BODY, tw), x + PAD, y + PAD, CAP_BODY, GOLD, true);
        kit.textTop(c, kit.fit(body, CAP_MID, tw), x + PAD, y + PAD + LINE_PITCH * 1.2f, CAP_MID, INK, true);

        final float by = buttonY(kit);
        kit.nine(c, buttonMarked, cancelX(), by, cancelW(), BUTTON_H, 14);
        kit.textCentre(c, notice ? kit.string("common_ok", cancel) : cancel, cancelX(), by, cancelW(), BUTTON_H, CAP_BODY, GOLD, true);
        if (notice) {
            return;
        }
        kit.nine(c, buttonPlain, confirmX(), by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, confirm, confirmX(), by, BUTTON_W, BUTTON_H, CAP_BODY, INK, true);
    }

    /** A finger was lifted at (x, y), in logical units. A touch beside the buttons does nothing. */
    int up(float px, float py, SkinKit kit) {
        final float by = buttonY(kit);
        if (py < by || py >= by + BUTTON_H) {
            return NONE;
        }
        if (px >= cancelX() && px < cancelX() + cancelW()) {
            return CANCEL;
        }
        if (!notice && px >= confirmX() && px < confirmX() + BUTTON_W) {
            return CONFIRM;
        }
        return NONE;
    }
}
