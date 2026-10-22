package net.wargushd.game;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.zip.GZIPInputStream;

/**
 * The Load Game page: the active profile's saved games, newest first, and what the
 * selected one is. What a save is comes from the head of the file itself: the game writes
 * there which map it was saved on.
 */
final class LoadPage {
    static final int ACTION_NONE = 0, ACTION_LOAD = 1, ACTION_DELETE = 2;

    /** One saved game. */
    static final class Save {
        /** Name inside the save directory, as the game is asked to load it. */
        String file;
        /** The file itself: what the page selects and what Load resolves (LOAD-WRONG-SAVE-ONCE). */
        File path;
        /** The name shown: the file name without its extensions. */
        String name;
        long modified;
        /** Map the game was saved on, as the game names it; null if the head could not be read. */
        String map;
    }

    private static final String[] ROMAN = {
        "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII", "XIII", "XIV",
    };

    // Layout, logical units.
    private static final float X0 = 816f, TOP = 264f, BOTTOM_UP = 264f, LIST_W = 1936f;
    private static final float DETAIL_X = 2800f, DETAIL_W = 944f;
    private static final float ROW_H = 132f, ROW_PITCH = 138f, LINE_PITCH = 82.5f;
    private static final float ACTION_W = 720f, ACTION_H = 120f, ACTION_UP = 204f;
    private static final float CAP_TITLE = 60f, CAP_BODY = 44f, CAP_MID = 36f;

    private static final int PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E;
    private static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, GOLD = 0xFFF0D250;
    private static final int INK_TITLE = 0xFF000000, INK_DISABLED = 0xFF9C9486, DIM = 0x96000000;
    private static final int ROW_SELECTED = 0x36F0D250, ROW_FAINT = 0x10FFFFFF;

    private final List<Save> saves = new ArrayList<>();
    private int selected;
    private float scroll;
    /** The selected row is to be scrolled into view at the next draw. */
    private boolean reveal;
    /** Launcher preference: the path of the save last started from this page. */
    private static final String PREFS = "launcher", PREF_LOADED = "loadedSave";
    private Bitmap buttonPlain, buttonPrimary;
    private final SimpleDateFormat stamp = new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US);

    // List dragging.
    private float downY, downScroll;
    private boolean inList, dragging;

    /**
     * Read the profile's save directory again. The save selected before stays selected while it
     * is there (by its file, not by its place in the list); when it is gone nothing is selected
     * and its name is returned, so the shell can say so. With nothing selected before, the save
     * last started from this page is selected when it is still there, else the newest.
     *
     * @return the name of the selected save that has gone, else null
     */
    String refresh(Context c, Profiles.Profile profile) {
        final Save before = selectedSave();
        final String kept = (before != null && before.path != null) ? before.path.getPath() : null;
        saves.clear();
        selected = SaveChoice.NONE;
        scroll = 0f;
        reveal = false;
        if (profile == null) {
            return null;
        }
        final File dir = new File(Profiles.directory(c, profile), "wc2/save");
        File[] files = dir.listFiles();
        if (files == null) {
            files = new File[0];
        }
        for (File f : files) {
            final String n = f.getName();
            if (!f.isFile() || !(n.endsWith(".sav") || n.endsWith(".sav.gz"))) {
                continue;
            }
            final Save s = new Save();
            s.file = n;
            s.path = f;
            s.name = n.substring(0, n.lastIndexOf(".sav"));
            s.modified = f.lastModified();
            s.map = savedMap(f);
            saves.add(s);
        }
        Collections.sort(saves, new Comparator<Save>() {
            @Override
            public int compare(Save a, Save b) {
                return Long.compare(b.modified, a.modified);
            }
        });
        final String last = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(PREF_LOADED, null);
        final List<String> names = new ArrayList<>();
        for (Save s : saves) {
            names.add(s.file);
        }
        final SaveChoice.Pick pick = SaveChoice.select(dir, names, kept, last);
        selected = pick.index;
        reveal = selected > 0;
        if (pick.vanished != null) {
            android.util.Log.i("WargusHD", "LAUNCHER load: selected save gone " + pick.vanished);
        }
        return pick.vanished;
    }

    /**
     * The selected save's file as Load is to start it: only while it is still a file of the
     * profile's save directory; else null.
     */
    File resolveSelected(Context c, Profiles.Profile profile) {
        final Save s = selectedSave();
        if (s == null || s.path == null || profile == null) {
            return null;
        }
        return SaveChoice.resolve(new File(Profiles.directory(c, profile), "wc2/save"), s.path.getPath());
    }

    /** The selected save is being started: it stays the selected one when the page is read again. */
    void started(Context c, Profiles.Profile profile) {
        final Save s = selectedSave();
        if (s != null && profile != null) {
            c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(PREF_LOADED,
                new File(new File(Profiles.directory(c, profile), "wc2/save"), s.file).getPath()).apply();
        }
    }

    /** The map a save was made on: the SaveFile entry in the head the game writes. */
    private static String savedMap(File f) {
        try (InputStream in = f.getName().endsWith(".gz")
                 ? new GZIPInputStream(new FileInputStream(f)) : new FileInputStream(f);
             BufferedReader r = new BufferedReader(new InputStreamReader(in, "UTF-8"))) {
            String line;
            for (int i = 0; i < 80 && (line = r.readLine()) != null; ++i) {
                final int at = line.indexOf("SaveFile = \"");
                if (at >= 0) {
                    final int from = at + 12, to = line.indexOf('"', from);
                    return (to > from) ? line.substring(from, to) : null;
                }
            }
        } catch (Exception e) {
            // Unreadable head: the save is listed without its details.
        }
        return null;
    }

    /** Delete the selected save and read the directory again. @return false if it is still there */
    boolean deleteSelected(Context c, Profiles.Profile profile) {
        final Save s = selectedSave();
        if (s == null || profile == null) {
            return false;
        }
        final File f = new File(new File(Profiles.directory(c, profile), "wc2/save"), s.file);
        final boolean gone = f.delete() || !f.exists();
        // The deleted save is not a vanished selection: the page starts over on its default.
        selected = SaveChoice.NONE;
        refresh(c, profile);
        return gone;
    }

    /**
     * What a save's row shows: its name without the file suffix. The autosave is named by the
     * game's own string (key autosave, as the original's save list names it, NG92); only that
     * one name is looked up, so a save the player called anything else keeps its name.
     */
    static String caption(SkinKit kit, Save s) {
        return "autosave".equals(s.name) ? kit.string("autosave", "Autosave") : s.name;
    }

    Save selectedSave() {
        return (selected >= 0 && selected < saves.size()) ? saves.get(selected) : null;
    }

    /** The two detail lines under the name: what kind of game it is, and which. */
    private static String[] describe(SkinKit kit, Save s) {
        if (s.map == null) {
            return new String[] { kit.own("Saved game"), "" };
        }
        final String base = s.map.substring(s.map.lastIndexOf('/') + 1);
        if (s.map.startsWith("campaigns/")) {
            // campaigns/<human|orc|human-exp|orc-exp>/level<nn><h|o>.smp
            final String[] part = s.map.split("/");
            final String digits = base.replaceAll("[^0-9]", "");
            if (part.length >= 3 && !digits.isEmpty()) {
                final int n = Integer.parseInt(digits.length() > 2 ? digits.substring(0, 2) : digits);
                final boolean expansion = part[1].contains("exp");
                final String game = kit.own(expansion ? "Dark Portal" : "Tides");
                final String side = part[1].startsWith("orc")
                    ? kit.string("customscenario_race_orc", "Orc")
                    : kit.string("customscenario_race_human", "Human");
                final String number = (n >= 1 && n <= ROMAN.length) ? ROMAN[n - 1] : String.valueOf(n);
                return new String[] { kit.own("Campaign mission"), game + " - " + side + " - " + number };
            }
            return new String[] { kit.own("Campaign mission"), base };
        }
        final int dot = base.lastIndexOf('.');
        return new String[] { kit.own("Custom scenario"), dot > 0 ? base.substring(0, dot) : base };
    }

    private float bottom(SkinKit kit) { return kit.height - BOTTOM_UP; }
    private float listTop() { return TOP + 24f; }
    private float listBottom(SkinKit kit) { return bottom(kit) - 24f; }
    private float rowX() { return X0 + 24f; }
    private float rowW() { return LIST_W - 48f; }
    private float actionY(SkinKit kit) { return kit.height - ACTION_UP; }
    private float loadX() { return Screen.CANVAS_W - 96f - ACTION_W; }
    private float deleteX() { return Screen.CANVAS_W - 96f - 1488f; }

    private float maxScroll(SkinKit kit) {
        return Math.max(0f, saves.size() * ROW_PITCH - 6f - (listBottom(kit) - listTop()));
    }

    private void panel(Canvas c, SkinKit kit, float x, float y, float w, float h) {
        kit.fill(c, x, y, w, h, PANEL_FILL, 18f);
        kit.outline(c, x, y, w, h, LINE, 18f);
    }

    private void button(Canvas c, SkinKit kit, float x, String caption, boolean primary, boolean enabled) {
        final float y = actionY(kit);
        kit.nine(c, (primary && enabled) ? buttonPrimary : buttonPlain, x, y, ACTION_W, ACTION_H, 14);
        if (!enabled) {
            kit.fill(c, x, y, ACTION_W, ACTION_H, DIM, 0f);
        }
        kit.textCentre(c, caption, x, y, ACTION_W, ACTION_H, CAP_BODY,
                       !enabled ? INK_DISABLED : primary ? GOLD : INK, enabled);
    }

    void draw(Canvas c, SkinKit kit) {
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonPrimary = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        kit.textTop(c, kit.string("load_game", "Load Game"), X0, 84f, CAP_TITLE, INK_TITLE, false);

        if (reveal) {
            reveal = false;
            // Only as far as needed: putting the selected row at the top hid every newer save
            // above it (LOAD_LIST_3476).
            scroll = ListScroll.reveal(selected * ROW_PITCH, selected * ROW_PITCH + ROW_H, scroll,
                                       listBottom(kit) - listTop(), maxScroll(kit));
        }
        final float b = bottom(kit);
        panel(c, kit, X0, TOP, LIST_W, b - TOP);
        c.save();
        c.clipRect(kit.rect(rowX(), listTop(), rowW(), listBottom(kit) - listTop()));
        if (saves.isEmpty()) {
            // As the maps' list: one line in the middle (owner's rule).
            kit.textCentre(c, kit.own("No saved games"), rowX(), listTop(), rowW(), listBottom(kit) - listTop(),
                           CAP_BODY, INK_MUTED, true);
        }
        for (int i = 0; i < saves.size(); ++i) {
            final float y = listTop() + i * ROW_PITCH - scroll;
            if (y + ROW_H < listTop() || y > listBottom(kit)) {
                continue;
            }
            final Save s = saves.get(i);
            final boolean sel = (i == selected);
            kit.fill(c, rowX(), y, rowW(), ROW_H, sel ? ROW_SELECTED : ROW_FAINT, 10f);
            if (sel) {
                kit.fill(c, rowX(), y, 10f, ROW_H, GOLD, 0f);
            }
            kit.textMiddle(c, kit.fit(caption(kit, s), CAP_BODY, rowW() - 36f - 520f), rowX() + 36f, y, ROW_H,
                           CAP_BODY, INK, true);
            kit.textMiddleRight(c, stamp.format(new Date(s.modified)), rowX() + rowW() - 36f, y, ROW_H,
                                CAP_MID, sel ? INK : INK_MUTED, true);
        }
        c.restore();

        panel(c, kit, DETAIL_X, TOP, DETAIL_W, b - TOP);
        final Save s = selectedSave();
        if (s != null) {
            final float tx = DETAIL_X + 60f, tw = DETAIL_W - 120f, ty = TOP + 66f;
            kit.textTop(c, kit.fit(caption(kit, s), CAP_BODY, tw), tx, ty, CAP_BODY, GOLD, true);
            final String[] what = describe(kit, s);
            kit.textTop(c, kit.fit(what[0], CAP_MID, tw), tx, ty + LINE_PITCH, CAP_MID, INK, true);
            kit.textTop(c, kit.fit(what[1], CAP_MID, tw), tx, ty + LINE_PITCH * 2f, CAP_MID, INK_MUTED, true);
            kit.textTop(c, kit.fit(kit.own("Saved") + " " + stamp.format(new Date(s.modified)), CAP_MID, tw), tx,
                        ty + LINE_PITCH * 3f, CAP_MID, INK_MUTED, true);
        }

        button(c, kit, deleteX(), kit.string("common_delete", "Delete"), false, s != null);
        button(c, kit, loadX(), kit.string("common_load", "Load"), true, s != null);
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

    /** A finger was lifted at (x, y). @return what the shell has to do */
    int up(float x, float y, SkinKit kit) {
        final boolean wasDrag = dragging, wasInList = inList;
        dragging = false;
        inList = false;
        if (wasDrag) {
            return ACTION_NONE;
        }
        if (wasInList) {
            final int row = (int) ((y - listTop() + scroll) / ROW_PITCH);
            if (row >= 0 && row < saves.size()) {
                selected = row;
            }
        } else if (in(x, y, loadX(), actionY(kit), ACTION_W, ACTION_H) && selectedSave() != null) {
            return ACTION_LOAD;
        } else if (in(x, y, deleteX(), actionY(kit), ACTION_W, ACTION_H) && selectedSave() != null) {
            return ACTION_DELETE;
        }
        return ACTION_NONE;
    }
}
