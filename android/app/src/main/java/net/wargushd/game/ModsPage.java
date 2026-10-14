package net.wargushd.game;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.view.View;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;

/**
 * The Mods page: the packages installed on this device, each with its switch, and what the
 * selected one says about itself. A mod that is switched on takes part in the games started
 * from then on; with none switched on the game is the base game. A package comes in as a file
 * the player picks, and the selected one can be removed.
 */
final class ModsPage {
    static final int ACTION_NONE = 0, ACTION_SWITCH = 1, ACTION_IMPORT = 2, ACTION_REMOVE = 3;
    /** What the page waits for: nothing, a package file being read, a package being removed. */
    static final int IDLE = 0, IMPORTING = 1, REMOVING = 2;

    // Layout, logical units: the list and the detail panel stand where the Load Game page has its own.
    private static final float X0 = 816f, TOP = 264f, BOTTOM_UP = 264f, LIST_W = 1936f;
    private static final float DETAIL_X = 2800f, DETAIL_W = 944f;
    private static final float ROW_H = 132f, ROW_PITCH = 138f, LINE_PITCH = 82.5f;
    private static final float SWITCH_W = 144f, SWITCH_H = 72f;
    private static final float CAP_TITLE = 60f, CAP_BODY = 44f, CAP_MID = 36f;
    // The import stands where the Custom Scenario page has its own; the removal at the detail panel's foot.
    private static final float ACTION_W = 720f, ACTION_H = 120f, ACTION_UP = 204f, REMOVE_UP = 60f;

    private static final int PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E;
    private static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, GOLD = 0xFFF0D250;
    private static final int INK_TITLE = 0xFF000000, INK_DISABLED = 0xFF9C9486;
    private static final int ROW_SELECTED = 0x36F0D250, ROW_FAINT = 0x10FFFFFF;
    private static final int SWITCH_ON = 0xFF78B060, SWITCH_OFF = 0xFF463E34, KNOB = 0xFFECE6D6;
    private static final int DIM = 0x96000000;

    private List<ModStore.Mod> mods = new ArrayList<ModStore.Mod>();
    private Set<String> enabled;
    private boolean reading;
    private int selected;
    private float scroll;
    private int waiting = IDLE;
    private Bitmap buttonPlain;

    // List dragging.
    private float downY, downScroll;
    private boolean inList, dragging;

    /** Read the packages again, off the main thread; the page is redrawn when they are known. */
    void open(Context c, View view) {
        open(c, view, null);
    }

    /** As open; the package of this id, when it is there, is then the selected one. */
    void open(Context c, final View view, final String select) {
        final Context app = c.getApplicationContext();
        reading = true;
        new Thread(new Runnable() {
            @Override
            public void run() {
                final List<ModStore.Mod> found = ModStore.installed(app);
                final Set<String> on = ModStore.enabled(app);
                view.post(new Runnable() {
                    @Override
                    public void run() {
                        mods = found;
                        enabled = on;
                        reading = false;
                        selected = Math.min(selected, Math.max(0, mods.size() - 1));
                        for (int i = 0; i < mods.size(); ++i) {
                            if (mods.get(i).id.equals(select)) {
                                selected = i;
                            }
                        }
                        scroll = 0f;
                        view.invalidate();
                    }
                });
            }
        }, "mods-list").start();
    }

    ModStore.Mod selectedMod() {
        return (selected < mods.size()) ? mods.get(selected) : null;
    }

    /** While the page waits, its buttons and switches take no touch. */
    void setWaiting(int what) {
        waiting = what;
    }

    boolean isWaiting() {
        return waiting != IDLE;
    }

    private boolean isOn(ModStore.Mod m) {
        return m.usable && enabled != null && enabled.contains(m.id);
    }

    private float bottom(SkinKit kit) { return kit.height - BOTTOM_UP; }
    private float listTop() { return TOP + 24f; }
    private float listBottom(SkinKit kit) { return bottom(kit) - 24f; }
    private float rowX() { return X0 + 24f; }
    private float rowW() { return LIST_W - 48f; }
    private float switchX() { return rowX() + rowW() - 36f - SWITCH_W; }
    private float actionY(SkinKit kit) { return kit.height - ACTION_UP; }
    private float removeX() { return DETAIL_X + (DETAIL_W - ACTION_W) / 2f; }
    private float removeY(SkinKit kit) { return bottom(kit) - REMOVE_UP - ACTION_H; }

    private float maxScroll(SkinKit kit) {
        return Math.max(0f, mods.size() * ROW_PITCH - 6f - (listBottom(kit) - listTop()));
    }

    private void panel(Canvas c, SkinKit kit, float x, float y, float w, float h) {
        kit.fill(c, x, y, w, h, PANEL_FILL, 18f);
        kit.outline(c, x, y, w, h, LINE, 18f);
    }

    private void button(Canvas c, SkinKit kit, float x, float y, String caption, boolean enabled) {
        kit.nine(c, buttonPlain, x, y, ACTION_W, ACTION_H, 14);
        if (!enabled) {
            kit.fill(c, x, y, ACTION_W, ACTION_H, DIM, 0f);
        }
        kit.textCentre(c, caption, x, y, ACTION_W, ACTION_H, CAP_BODY, enabled ? INK : INK_DISABLED, enabled);
    }

    /** Draw a text broken into lines of the given width. @return the y below its last line */
    private float paragraph(Canvas c, SkinKit kit, String text, float x, float y, float w, float cap,
                            float pitch, int color) {
        String line = "";
        for (String word : text.trim().split("\\s+")) {
            final String longer = line.isEmpty() ? word : line + " " + word;
            if (!line.isEmpty() && kit.width(longer, cap) > w) {
                kit.textTop(c, line, x, y, cap, color, true);
                y += pitch;
                line = word;
            } else {
                line = longer;
            }
        }
        if (!line.isEmpty()) {
            kit.textTop(c, kit.fit(line, cap, w), x, y, cap, color, true);
            y += pitch;
        }
        return y;
    }

    void draw(Canvas c, SkinKit kit) {
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
        }
        kit.textTop(c, kit.own("Mods"), X0, 84f, CAP_TITLE, INK_TITLE, false);

        final float b = bottom(kit);
        panel(c, kit, X0, TOP, LIST_W, b - TOP);
        c.save();
        c.clipRect(kit.rect(rowX(), listTop(), rowW(), listBottom(kit) - listTop()));
        if (mods.isEmpty() && !reading) {
            // As the maps' list: one line in the middle (owner's rule).
            kit.textCentre(c, kit.own("No mods installed"), rowX(), listTop(), rowW(),
                           listBottom(kit) - listTop(), CAP_BODY, INK_MUTED, true);
        }
        for (int i = 0; i < mods.size(); ++i) {
            final float y = listTop() + i * ROW_PITCH - scroll;
            if (y + ROW_H < listTop() || y > listBottom(kit)) {
                continue;
            }
            final ModStore.Mod m = mods.get(i);
            final boolean sel = (i == selected), on = isOn(m);
            kit.fill(c, rowX(), y, rowW(), ROW_H, sel ? ROW_SELECTED : ROW_FAINT, 10f);
            if (sel) {
                kit.fill(c, rowX(), y, 10f, ROW_H, GOLD, 0f);
            }
            kit.textMiddle(c, kit.fit(m.name, CAP_BODY, rowW() - 36f - 72f - SWITCH_W - 36f), rowX() + 36f,
                           y, ROW_H, CAP_BODY, m.usable ? INK : INK_DISABLED, true);
            if (m.usable) {
                final float sx = switchX(), sy = y + (ROW_H - SWITCH_H) / 2f;
                kit.fill(c, sx, sy, SWITCH_W, SWITCH_H, on ? SWITCH_ON : SWITCH_OFF, 36f);
                kit.outline(c, sx, sy, SWITCH_W, SWITCH_H, LINE, 36f);
                kit.fill(c, sx + (on ? 78f : 6f), sy + 6f, 60f, 60f, KNOB, 30f);
            }
        }
        c.restore();

        panel(c, kit, DETAIL_X, TOP, DETAIL_W, b - TOP);
        final ModStore.Mod m = selectedMod();
        if (m != null) {
            final float tx = DETAIL_X + 60f, tw = DETAIL_W - 120f;
            // The text ends above the panel's button.
            c.save();
            c.clipRect(kit.rect(DETAIL_X, TOP, DETAIL_W, removeY(kit) - 24f - TOP));
            float y = paragraph(c, kit, m.name, tx, TOP + 66f, tw, CAP_BODY, LINE_PITCH, GOLD);
            if (!m.author.isEmpty()) {
                kit.textTop(c, kit.fit(m.author, CAP_MID, tw), tx, y, CAP_MID, INK, true);
                y += LINE_PITCH;
            }
            if (!m.version.isEmpty()) {
                kit.textTop(c, kit.fit(kit.own("Version {0}", m.version), CAP_MID, tw), tx, y, CAP_MID,
                            INK_MUTED, true);
                y += LINE_PITCH;
            }
            y = paragraph(c, kit, m.description, tx, y + 24f, tw, CAP_MID, 60f, INK);
            if (!m.usable) {
                paragraph(c, kit, kit.own("This mod cannot be used with this version of the app."), tx,
                          y + 24f, tw, CAP_MID, 60f, INK_DISABLED);
            } else {
                paragraph(c, kit, isOn(m) ? kit.own("On. The game uses it from the next start.")
                                          : kit.own("Off. The game is as it is without it."),
                          tx, y + 24f, tw, CAP_MID, 60f, INK_MUTED);
            }
            c.restore();
            button(c, kit, removeX(), removeY(kit),
                   waiting == REMOVING ? kit.own("Removing...") : kit.own("Remove"), waiting == IDLE);
        }

        // As the import of a map: at the left of the row under the panels.
        button(c, kit, X0, actionY(kit),
               waiting == IMPORTING ? kit.own("Importing...") : kit.own("Import a Mod"), waiting == IDLE);
    }

    private static boolean in(float x, float y, float rx, float ry, float rw, float rh) {
        return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
    }

    void down(float x, float y, SkinKit kit) {
        downY = y;
        downScroll = scroll;
        dragging = false;
        inList = in(x, y, rowX(), listTop(), rowW(), listBottom(kit) - listTop());
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

    /**
     * A finger was lifted at (x, y). @return ACTION_SWITCH when a switch was turned, ACTION_IMPORT
     * or ACTION_REMOVE when that button was touched, else ACTION_NONE
     */
    int up(Context c, float x, float y, SkinKit kit) {
        final boolean wasDrag = dragging, wasInList = inList;
        dragging = false;
        inList = false;
        if (wasDrag || waiting != IDLE) {
            return ACTION_NONE;
        }
        if (in(x, y, X0, actionY(kit), ACTION_W, ACTION_H)) {
            return ACTION_IMPORT;
        }
        if (selectedMod() != null && in(x, y, removeX(), removeY(kit), ACTION_W, ACTION_H)) {
            return ACTION_REMOVE;
        }
        if (!wasInList) {
            return ACTION_NONE;
        }
        final int row = (int) ((y - listTop() + scroll) / ROW_PITCH);
        if (row < 0 || row >= mods.size()) {
            return ACTION_NONE;
        }
        selected = row;
        final ModStore.Mod m = mods.get(row);
        // The switch takes the row's right end, a finger's width around the drawn one.
        if (m.usable && enabled != null && x >= switchX() - 48f) {
            final boolean on = !enabled.contains(m.id);
            ModStore.setEnabled(c, m.id, on);
            if (on) {
                enabled.add(m.id);
            } else {
                enabled.remove(m.id);
            }
            return ACTION_SWITCH;
        }
        return ACTION_NONE;
    }
}
