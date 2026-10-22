package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

import java.util.List;

/**
 * The list of profiles, opened by Switch on the profile sheet: choose one to play as, make a
 * new one, or delete one that is not in use. Owner's board 2B (2026-10-18): rows 168 high with
 * the profile's portrait, and New on this sheet.
 */
final class SwitchSheet {
    static final int NONE = 0, CANCEL = 1, SWITCH = 2, DELETE = 3, NEW = 4;

    // Layout, logical units: the kit's modal sheet, centred, inner padding 96.
    private static final float W = 1800f, H = 1440f, PAD = 96f;
    private static final float ROW_H = 168f, ROW_PITCH = 180f, BUTTON_H = 120f, GAP = 28f;
    private static final float FACE_X = 30f, FACE_Y = 12f, FACE_W = 173f, FACE_H = 144f, NAME_X = 233f;
    private static final float CAP_BODY = 44f, CAP_MID = 36f;

    private static final int SHADE = 0xAA000000, PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E;
    private static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, GOLD = 0xFFF0D250;
    private static final int INK_DISABLED = 0xFF9C9486, DIM = 0x96000000;
    private static final int ROW_SELECTED = 0x36F0D250, ROW_FAINT = 0x10FFFFFF, FACE_FILL = 0xFF2A2218;

    private final List<Profiles.Profile> profiles;
    private final String activeId;
    private int selected;
    /** The rows' portraits in each profile's colour, made when first drawn. */
    private final java.util.Map<String, Bitmap> faces = new java.util.HashMap<String, Bitmap>();
    private float scroll;
    private Bitmap buttonPlain, buttonMarked;

    // List dragging.
    private float downY, downScroll;
    private boolean inList, dragging;

    SwitchSheet(List<Profiles.Profile> profiles, String activeId) {
        this.profiles = profiles;
        this.activeId = activeId;
        for (int i = 0; i < profiles.size(); ++i) {
            if (profiles.get(i).id.equals(activeId)) {
                selected = i;
            }
        }
    }

    Profiles.Profile chosen() {
        return (selected < profiles.size()) ? profiles.get(selected) : null;
    }

    /** The profile in use cannot be deleted; switch away from it first. */
    private boolean canDelete() {
        final Profiles.Profile p = chosen();
        return p != null && !p.id.equals(activeId);
    }

    private float x() { return (Screen.CANVAS_W - W) / 2f; }
    private float y(SkinKit kit) { return kit.height / 2f - H / 2f; }
    private float px() { return x() + PAD; }
    private float pw() { return W - PAD * 2f; }
    private float listTop(SkinKit kit) { return y(kit) + PAD + 108f; }
    private float buttonY(SkinKit kit) { return y(kit) + H - PAD - BUTTON_H; }
    private float listBottom(SkinKit kit) { return buttonY(kit) - 24f; }
    private float buttonW() { return (pw() - 3f * GAP) / 4f; }
    private float buttonX(int i) { return px() + (buttonW() + GAP) * i; }

    private float maxScroll(SkinKit kit) {
        return Math.max(0f, profiles.size() * ROW_PITCH - 6f - (listBottom(kit) - listTop(kit)));
    }

    private void button(Canvas c, SkinKit kit, int i, String caption, boolean marked, boolean enabled) {
        final float bx = buttonX(i), by = buttonY(kit), bw = buttonW();
        kit.nine(c, marked ? buttonMarked : buttonPlain, bx, by, bw, BUTTON_H, 14);
        if (!enabled) {
            kit.fill(c, bx, by, bw, BUTTON_H, DIM, 0f);
        }
        kit.textCentre(c, caption, bx, by, bw, BUTTON_H, CAP_BODY,
                       !enabled ? INK_DISABLED : marked ? GOLD : INK, enabled);
    }

    void draw(Canvas c, SkinKit kit) {
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        kit.fill(c, x(), y(kit), W, H, PANEL_FILL, 18f);
        kit.outline(c, x(), y(kit), W, H, LINE, 18f);
        final float px = px(), pw = pw();
        kit.textTop(c, kit.own("Switch profile"), px, y(kit) + PAD, CAP_BODY, GOLD, true);

        c.save();
        c.clipRect(kit.rect(px, listTop(kit), pw, listBottom(kit) - listTop(kit)));
        for (int i = 0; i < profiles.size(); ++i) {
            final float ry = listTop(kit) + i * ROW_PITCH - scroll;
            if (ry + ROW_H < listTop(kit) || ry > listBottom(kit)) {
                continue;
            }
            final Profiles.Profile p = profiles.get(i);
            final boolean sel = (i == selected);
            kit.fill(c, px, ry, pw, ROW_H, sel ? ROW_SELECTED : ROW_FAINT, 10f);
            if (sel) {
                kit.fill(c, px, ry, 10f, ROW_H, GOLD, 0f);
            }
            Bitmap face = faces.get(p.id);
            if (face == null && !faces.containsKey(p.id)) {
                face = Portraits.tinted(kit, p.portrait, Profiles.COLORS[p.color]);
                faces.put(p.id, face);
            }
            kit.fill(c, px + FACE_X, ry + FACE_Y, FACE_W, FACE_H, FACE_FILL, 10f);
            kit.picture(c, face, px + FACE_X, ry + FACE_Y, FACE_W, FACE_H);
            kit.outline(c, px + FACE_X, ry + FACE_Y, FACE_W, FACE_H, LINE, 10f);
            kit.textMiddle(c, kit.fit(p.name, CAP_BODY, pw - NAME_X - 520f), px + NAME_X, ry, ROW_H, CAP_BODY,
                           sel ? GOLD : INK, true);
            final String right = p.id.equals(activeId) ? kit.own("In use")
                : (p.games == 0) ? kit.own("No games yet")
                : kit.own(p.games == 1 ? "{0} game" : "{0} games", p.games);
            kit.textMiddleRight(c, right, px + pw - 36f, ry, ROW_H, CAP_MID, sel ? INK : INK_MUTED, true);
        }
        c.restore();

        button(c, kit, 0, kit.own("New"), false, true);
        button(c, kit, 1, kit.string("common_delete", "Delete"), false, canDelete());
        button(c, kit, 2, kit.string("common_cancel", "Cancel"), false, true);
        button(c, kit, 3, kit.own("Switch"), true, true);
    }

    private static boolean in(float x, float y, float rx, float ry, float rw, float rh) {
        return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
    }

    void down(float x, float y, SkinKit kit) {
        downY = y;
        downScroll = scroll;
        dragging = false;
        inList = in(x, y, px(), listTop(kit), pw(), listBottom(kit) - listTop(kit));
    }

    /** @return true when the sheet must be redrawn */
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

    /** A finger was lifted at (x, y), in logical units. */
    int up(float x, float y, SkinKit kit) {
        final boolean wasDrag = dragging, wasInList = inList;
        dragging = false;
        inList = false;
        if (wasDrag) {
            return NONE;
        }
        if (wasInList) {
            final int row = (int) ((y - listTop(kit) + scroll) / ROW_PITCH);
            if (row >= 0 && row < profiles.size()) {
                selected = row;
            }
            return NONE;
        }
        if (y >= buttonY(kit) && y < buttonY(kit) + BUTTON_H) {
            if (in(x, y, buttonX(0), buttonY(kit), buttonW(), BUTTON_H)) {
                return NEW;
            }
            if (in(x, y, buttonX(1), buttonY(kit), buttonW(), BUTTON_H) && canDelete()) {
                return DELETE;
            }
            if (in(x, y, buttonX(2), buttonY(kit), buttonW(), BUTTON_H)) {
                return CANCEL;
            }
            if (in(x, y, buttonX(3), buttonY(kit), buttonW(), BUTTON_H)) {
                return SWITCH;
            }
        }
        return NONE;
    }
}
