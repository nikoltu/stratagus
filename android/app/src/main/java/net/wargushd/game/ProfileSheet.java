package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

/**
 * The profile sheet, opened from the profile chip: the portrait in the profile's colour,
 * the name and the eight colours at the left; statistics, campaign progress and the
 * profile's actions at the right. The name is edited in place: a tap on it gives RENAME and the
 * shell types into `editor` (owner's board 2B, 2026-10-18; the Rename sheet is gone).
 */
final class ProfileSheet {
    static final int NONE = 0, CLOSE = 1, COLOR = 2, PORTRAIT = 3, NEW = 4, RENAME = 5, SWITCH = 6;
    /** While the name is typed: its text, the sheet raised to TOP_EDITING clear of the keyboard. */
    NameSheet editor;
    private static final float TOP_EDITING = 60f, NAME_FIELD_DY = 648f, NAME_FIELD_H = 110f, CAP_NAME = 52f;

    // Layout, logical units: the kit's modal sheet, centred, inner padding 96.
    private static final float W = 2400f, H = 1560f, PAD = 96f;
    private static final float FACE_W = 756f, FACE_H = 624.5f, BUTTON_H = 120f;
    private static final float SWATCH_W = 174f, SWATCH_H = 96f, SWATCH_DX = 194f, SWATCH_DY = 116f;
    private static final float LINE_PITCH = 82.5f, GAP = 48f;
    private static final float CAP_TITLE = 60f, CAP_BODY = 44f, CAP_MID = 36f, CAP_SMALL = 30f;

    private static final int SHADE = 0xAA000000, PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E;
    private static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, GOLD = 0xFFF0D250;
    private static final int TRACK = 0xFF50463A, SWATCH_LINE = 0xFFECE6D6;

    /** The rows of the statistics block, in the design's order. */
    private static final String[] STATS = {
        "Games played", "Victories", "Defeats", "Units", "Buildings built", "Kills",
        "Razings", "Total score", "Best rank",
    };
    /** The game's own text for a row, where it has one of that meaning. */
    private static final String[] STATS_KEYS = {
        null, null, null, "victorydefeat_units", null, "victorydefeat_kills", "victorydefeat_razings", null, null,
    };
    private static final String[] PROGRESS = {
        "Tides - Orc", "Tides - Human", "Dark Portal - Orc", "Dark Portal - Human",
    };
    private static final String[] ACTIONS = { "Switch", "New", "Close" };

    private Bitmap frame, buttonPlain, buttonMarked, face;
    private int faceSlot = -1, faceColor = -1;
    /** The colour index of the last swatch tapped. */
    int pickedColor;

    private float x() { return (Screen.CANVAS_W - W) / 2f; }
    private float y(SkinKit kit) { return (editor != null) ? TOP_EDITING : kit.height / 2f - H / 2f; }
    private float px() { return x() + PAD; }
    private float py(SkinKit kit) { return y(kit) + PAD; }
    private float sx() { return px() + FACE_W + 120f; }
    private float sw() { return x() + W - PAD - sx(); }
    private float actionY(SkinKit kit) { return y(kit) + H - PAD - BUTTON_H; }
    private float actionW() { return (sw() - 2f * GAP) / 3f; }
    private float swatchX(int i) { return px() + SWATCH_DX * (i % 4); }
    private float swatchY(SkinKit kit, int i) { return py(kit) + 960f + SWATCH_DY * (i / 4); }

    /**
     * What the profile's store holds for a statistics row, or null: the game does not report
     * a rank, so there is none to keep.
     */
    private static String stat(Profiles.Profile p, int row) {
        switch (row) {
            case 0: return String.valueOf(p.games);
            case 1: return String.valueOf(p.victories);
            case 2: return String.valueOf(p.defeats);
            case 3: return grouped(p.units);
            case 4: return grouped(p.buildings);
            case 5: return grouped(p.kills);
            case 6: return grouped(p.razings);
            case 7: return grouped(p.score);
            default: return null;   // the rank is a name from the game's strings, see draw
        }
    }

    /** A number with its thousands set apart by a space. */
    private static String grouped(long n) {
        final String digits = String.valueOf(Math.abs(n));
        final StringBuilder s = new StringBuilder();
        for (int i = 0; i < digits.length(); ++i) {
            if (i > 0 && (digits.length() - i) % 3 == 0) {
                s.append(' ');
            }
            s.append(digits.charAt(i));
        }
        return (n < 0 ? "-" : "") + s;
    }

    void draw(Canvas c, SkinKit kit, Profiles.Profile p, CampaignPage.Campaign[] campaigns) {
        if (frame == null) {
            frame = kit.atlasFrame("Modern_HUD_Widgets", "portrait_button", 80, 80);
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        if (faceSlot != p.portrait || faceColor != p.color) {
            faceSlot = p.portrait;
            faceColor = p.color;
            face = Portraits.tinted(kit, p.portrait, Profiles.COLORS[p.color]);
        }
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        kit.fill(c, x(), y(kit), W, H, PANEL_FILL, 18f);
        kit.outline(c, x(), y(kit), W, H, LINE, 18f);

        // Left: who.
        final float px = px(), py = py(kit);
        kit.nine(c, frame, px - 12f, py - 12f, FACE_W + 24f, FACE_H + 23.5f, 25);
        kit.picture(c, face, px, py, FACE_W, FACE_H);
        if (editor != null) {
            editor.drawField(c, kit, px, py + NAME_FIELD_DY, FACE_W, NAME_FIELD_H, CAP_NAME);
        } else {
            kit.textTop(c, kit.fit(p.name, CAP_TITLE, FACE_W), px, py + 672f, CAP_TITLE, INK, true);
        }
        kit.nine(c, buttonPlain, px, py + 780f, FACE_W, BUTTON_H, 14);
        kit.textCentre(c, kit.own("Change Portrait"), px, py + 780f, FACE_W, BUTTON_H, CAP_BODY, INK, true);
        for (int i = 0; i < Profiles.COLORS.length; ++i) {
            final float rx = swatchX(i), ry = swatchY(kit, i);
            if (i == p.color) {
                kit.fill(c, rx - 9f, ry - 9f, SWATCH_W + 18f, SWATCH_H + 18f, GOLD, 18f);
            }
            kit.fill(c, rx, ry, SWATCH_W, SWATCH_H, Profiles.COLORS[i], 12f);
            kit.outline(c, rx, ry, SWATCH_W, SWATCH_H, SWATCH_LINE, 12f);
        }

        // Right: what was done.
        final float sx = sx(), sw = sw();
        kit.textTop(c, kit.own("Statistics"), sx, py, CAP_BODY, GOLD, true);
        for (int i = 0; i < STATS.length; ++i) {
            final float ry = py + LINE_PITCH * (i + 1);
            kit.textTop(c, STATS_KEYS[i] != null ? kit.string(STATS_KEYS[i], STATS[i]) : kit.own(STATS[i]), sx, ry, CAP_MID,
                        INK_MUTED, true);
            final String value = (i == STATS.length - 1 && p.bestRank > 0)
                ? kit.string(Profiles.rankKey(p.bestRank, p.bestRankOrc), null) : stat(p, i);
            kit.textTopRight(c, value != null ? value : "-", sx + sw, ry, CAP_MID,
                             value != null ? INK : INK_MUTED, true);
        }
        final float gy = py + LINE_PITCH * 10.6f;
        kit.textTop(c, kit.string("campaign", "Campaign"), sx, gy, CAP_BODY, GOLD, true);
        for (int i = 0; i < PROGRESS.length && i < campaigns.length; ++i) {
            final CampaignPage.Campaign k = campaigns[i];
            final float ry = gy + 72f + 60f * i;
            kit.textTop(c, kit.own(PROGRESS[i]), sx, ry, CAP_SMALL, INK_MUTED, true);
            final float bw = sw - 660f - 190f;
            kit.fill(c, sx + 660f, ry + 6f, bw, 18f, TRACK, 9f);
            if (k.completed > 0) {
                kit.fill(c, sx + 660f, ry + 6f, bw * k.completed / k.missions, 18f, GOLD, 9f);
            }
            kit.textTopRight(c, k.completed + " / " + k.missions, sx + sw, ry, CAP_SMALL, INK, true);
        }

        final float ay = actionY(kit), aw = actionW();
        for (int i = 0; i < ACTIONS.length; ++i) {
            final float ax = sx + (aw + GAP) * i;
            final boolean marked = (i == ACTIONS.length - 1);
            kit.nine(c, marked ? buttonMarked : buttonPlain, ax, ay, aw, BUTTON_H, 14);
            kit.textCentre(c, kit.fit(kit.own(ACTIONS[i]), CAP_BODY, aw - 48f), ax, ay, aw, BUTTON_H, CAP_BODY, marked ? GOLD : INK, true);
        }
    }

    private static boolean in(float x, float y, float rx, float ry, float rw, float rh) {
        return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
    }

    /** A finger was lifted at (x, y), in logical units. */
    int up(float x, float y, SkinKit kit) {
        for (int i = 0; i < Profiles.COLORS.length; ++i) {
            if (in(x, y, swatchX(i), swatchY(kit, i), SWATCH_W, SWATCH_H)) {
                pickedColor = i;
                return COLOR;
            }
        }
        if (in(x, y, px(), py(kit) + 780f, FACE_W, BUTTON_H)) {
            return PORTRAIT;
        }
        if (in(x, y, px(), py(kit) + NAME_FIELD_DY, FACE_W, NAME_FIELD_H)) {
            return RENAME;
        }
        final float aw = actionW();
        if (in(x, y, sx() + (aw + GAP) * 2f, actionY(kit), aw, BUTTON_H)) {
            return CLOSE;
        }
        if (in(x, y, sx(), actionY(kit), aw, BUTTON_H)) {
            return SWITCH;
        }
        if (in(x, y, sx() + (aw + GAP) * 1f, actionY(kit), aw, BUTTON_H)) {
            return NEW;
        }
        return NONE;
    }
}
