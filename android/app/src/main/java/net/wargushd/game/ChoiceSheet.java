package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

/**
 * A small modal sheet that asks one question with three or four answers: a title, one line
 * under it, and the buttons in a row. The last button changes nothing; it is the marked one and
 * is what Back means.
 */
final class ChoiceSheet {
    static final int NONE = -1;

    // Layout, logical units: the kit's small sheet, centred; the buttons in a row. With four
    // answers the sheet is wider, so that a button still holds two words.
    private static final float W3 = 1800f, W4 = 2400f, H = 600f, PAD = 96f;
    private static final float BUTTON_W3 = 500f, GAP3 = 54f, BUTTON_W4 = 525f, GAP4 = 36f, BUTTON_H = 120f;
    private static final float CAP_BODY = 44f, CAP_MID = 36f, LINE_PITCH = 82.5f;

    private static final int SHADE = 0xAA000000, PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E;
    private static final int INK = 0xFFE8E2D4, GOLD = 0xFFF0D250;

    private final String title, line;
    private final String[] captions;
    private Bitmap buttonPlain, buttonMarked;

    /** The texts are shown as given. */
    ChoiceSheet(String title, String line, String first, String second, String nothing) {
        this.title = title;
        this.line = line;
        captions = new String[] { first, second, nothing };
    }

    /** As above, with four answers. */
    ChoiceSheet(String title, String line, String first, String second, String third, String nothing) {
        this.title = title;
        this.line = line;
        captions = new String[] { first, second, third, nothing };
    }

    private boolean four() { return captions.length == 4; }
    private float w() { return four() ? W4 : W3; }
    private float buttonW() { return four() ? BUTTON_W4 : BUTTON_W3; }
    private float x() { return (Screen.CANVAS_W - w()) / 2f; }
    private float y(SkinKit kit) { return kit.height / 2f - H / 2f; }
    private float buttonX(int i) { return x() + PAD + (buttonW() + (four() ? GAP4 : GAP3)) * i; }
    private float buttonY(SkinKit kit) { return y(kit) + H - PAD - BUTTON_H; }

    void draw(Canvas c, SkinKit kit) {
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        final float x = x(), y = y(kit), w = w(), bw = buttonW();
        kit.fill(c, x, y, w, H, PANEL_FILL, 18f);
        kit.outline(c, x, y, w, H, LINE, 18f);

        final float tw = w - PAD * 2f;
        kit.textTop(c, kit.fit(title, CAP_BODY, tw), x + PAD, y + PAD, CAP_BODY, GOLD, true);
        kit.textTop(c, kit.fit(line, CAP_MID, tw), x + PAD, y + PAD + LINE_PITCH * 1.2f, CAP_MID, INK, true);

        final float by = buttonY(kit);
        final int last = captions.length - 1;
        for (int i = 0; i <= last; ++i) {
            kit.nine(c, (i == last) ? buttonMarked : buttonPlain, buttonX(i), by, bw, BUTTON_H, 14);
            kit.textCentre(c, kit.fit(captions[i], CAP_BODY, bw - 48f), buttonX(i), by, bw, BUTTON_H,
                           CAP_BODY, (i == last) ? GOLD : INK, true);
        }
    }

    /**
     * A finger was lifted at (x, y), in logical units.
     *
     * @return the button, counted from 0, or NONE for a touch beside the buttons
     */
    int up(float px, float py, SkinKit kit) {
        final float by = buttonY(kit);
        if (py < by || py >= by + BUTTON_H) {
            return NONE;
        }
        for (int i = 0; i < captions.length; ++i) {
            if (px >= buttonX(i) && px < buttonX(i) + buttonW()) {
                return i;
            }
        }
        return NONE;
    }
}
