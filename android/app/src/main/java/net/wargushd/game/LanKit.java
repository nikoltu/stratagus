package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

/**
 * The kit's components as the Multiplayer page, the room and their sheets draw them: panel,
 * value box, segmented control, action button, colour swatch, modal sheet.
 */
final class LanKit {
    static final int PLAIN = 0, PRIMARY = 1, DISABLED = 2;

    static final float CAP_BODY = 44f, CAP_MID = 36f, CAP_SMALL = 30f, LINE_PITCH = 82.5f;
    static final float ROW_RADIUS = 10f;

    static final int SHADE = 0xAA000000, PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E, WELL = 0x78000000;
    static final int POP_FILL = 0xFA100C08;
    static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, GOLD = 0xFFF0D250;
    static final int INK_ON_GOLD = 0xFF1E1608, INK_DISABLED = 0xFF9C9486, DIM = 0x96000000;
    static final int ROW_SELECTED = 0x36F0D250, ROW_FAINT = 0x10FFFFFF, SEG_FAINT = 0x0EFFFFFF;
    static final int FIELD = 0xA0000000, BAR = 0xFF78C878;

    private static Bitmap buttonPlain, buttonMarked;

    private LanKit() {}

    static boolean in(float x, float y, float rx, float ry, float rw, float rh) {
        return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
    }

    static void panel(Canvas c, SkinKit kit, float x, float y, float w, float h) {
        kit.fill(c, x, y, w, h, PANEL_FILL, 18f);
        kit.outline(c, x, y, w, h, LINE, 18f);
    }

    static void well(Canvas c, SkinKit kit, float x, float y, float w, float h) {
        kit.fill(c, x, y, w, h, WELL, 14f);
        kit.outline(c, x, y, w, h, LINE, 14f);
    }

    /** The shade over everything and a sheet on it. */
    static void sheet(Canvas c, SkinKit kit, float x, float y, float w, float h) {
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        panel(c, kit, x, y, w, h);
    }

    /** A list that opens over the rows. */
    static void pop(Canvas c, SkinKit kit, float x, float y, float w, float h) {
        kit.fill(c, x, y, w, h, POP_FILL, 14f);
        kit.outline(c, x, y, w, h, LINE, 14f);
    }

    static void button(Canvas c, SkinKit kit, String text, float x, float y, float w, float h, float cap,
                       int look) {
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        kit.nine(c, look == PRIMARY ? buttonMarked : buttonPlain, x, y, w, h, 14);
        if (look == DISABLED) {
            kit.fill(c, x, y, w, h, DIM, 0f);
        }
        kit.textCentre(c, text, x, y, w, h, cap, look == PRIMARY ? GOLD : look == DISABLED ? INK_DISABLED : INK,
                       look != DISABLED);
    }

    /** A segmented control; `on` is the lit segment, or -1. */
    static void segments(Canvas c, SkinKit kit, String[] texts, int on, float x, float y, float w, float h,
                         float cap) {
        well(c, kit, x, y, w, h);
        final float segW = w / texts.length;
        for (int i = 0; i < texts.length; ++i) {
            final float sx = x + segW * i + 6f;
            kit.fill(c, sx, y + 6f, segW - 12f, h - 12f, i == on ? GOLD : SEG_FAINT, 10f);
            kit.textCentre(c, texts[i], sx, y + 6f, segW - 12f, h - 12f, cap, i == on ? INK_ON_GOLD : INK,
                           i != on);
        }
    }

    /** The segment of a control of n under a point, or -1. */
    static int segmentAt(float px, float py, int n, float x, float y, float w, float h) {
        return in(px, py, x, y, w, h) ? Math.min(n - 1, (int) ((px - x) / (w / n))) : -1;
    }

    static void value(Canvas c, SkinKit kit, String text, float x, float y, float w, float h, float cap) {
        well(c, kit, x, y, w, h);
        kit.textMiddle(c, kit.fit(text, cap, w - 72f), x + 36f, y, h, cap, INK, true);
    }

    static void swatch(Canvas c, SkinKit kit, int colour, float x, float y, float size) {
        kit.fill(c, x, y, size, size, Profiles.COLORS[colour & 7], size / 8f);
        kit.outline(c, x, y, size, size, LINE, size / 8f);
    }

    static void bar(Canvas c, SkinKit kit, float x, float y, float w, float h, float part) {
        kit.fill(c, x, y, w, h, WELL, h / 2f);
        if (part > 0f) {
            kit.fill(c, x, y, w * Math.min(1f, part), h, BAR, h / 2f);
        }
    }
}
