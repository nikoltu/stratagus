package net.wargushd.game;

import android.content.Context;
import android.graphics.Canvas;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * A list of maps under its source filter (owner's rule: All / Remastered / My maps), as the Multiplayer page's
 * Create segment and the room's map sheet show it. The folders, their order and the rows are
 * those of the Custom Scenario page's list.
 */
final class MapList {
    // Layout, logical units: the filter, then the rows under it.
    static final float W = 1392f, SRC_H = 108f, ROWS_DY = 132f, ROW_H = 132f, PITCH = 138f;
    private static final float ROW_INSET = 36f;

    final List<File> maps = new ArrayList<File>();
    final List<PudInfo> info = new ArrayList<PudInfo>();
    /** 0 all, 1 the game folder's maps, 2 the player's imported maps. */
    int source = 1;
    int selected;
    private float scroll;

    // List dragging.
    private float downY, downScroll;
    private boolean inList, dragging;

    void load(Context c) {
        maps.clear();
        info.clear();
        final String folder = GateActivity.rememberedFolder(c);
        if (source != 2 && folder != null) {
            collect(new File(folder, "Data/Maps"), 0);
        }
        if (source != 1) {
            collect(MapStore.directory(c), 0);
        }
        selected = 0;
        scroll = 0f;
    }

    /** A folder's own maps first, then those of its sub-folders, each in name order. */
    private void collect(File dir, int depth) {
        final File[] files = dir.listFiles();
        if (files == null || depth > 3) {
            return;
        }
        Arrays.sort(files);
        for (File f : files) {
            if (f.isFile() && f.getName().toLowerCase().endsWith(".pud")) {
                maps.add(f);
                info.add(PudInfo.of(f));
            }
        }
        for (File f : files) {
            if (f.isDirectory()) {
                collect(f, depth + 1);
            }
        }
    }

    /** The selected map, or null when the list is empty. */
    File chosen() {
        return (selected < maps.size()) ? maps.get(selected) : null;
    }

    static String name(File f) {
        final String n = f.getName();
        return n.toLowerCase().endsWith(".pud") ? n.substring(0, n.length() - 4) : n;
    }

    /** "128 x 128 - Winter - up to 8 players", as far as the file said. */
    static String facts(SkinKit kit, PudInfo i, int tileset) {
        final StringBuilder s = new StringBuilder();
        if (i != null && i.width > 0 && i.height > 0) {
            s.append(i.width).append(" x ").append(i.height);
        }
        final int era = (tileset >= 0) ? tileset : (i != null) ? i.tileset : -1;
        if (era >= 0) {
            s.append(s.length() > 0 ? " - " : "").append(LanSettings.tilesetName(kit, era));
        }
        if (i != null && i.players > 0) {
            s.append(s.length() > 0 ? " - " : "").append(kit.own("up to {0} players", i.players));
        }
        return s.toString();
    }

    /** The name the engine finds a map by: its path inside the game folder or the data directory. */
    static String path(Context c, File map) {
        final String folder = GateActivity.rememberedFolder(c);
        if (folder != null && map.getPath().startsWith(folder + "/")) {
            return map.getPath().substring(folder.length() + 1);
        }
        final String data = Provision.dataDir(c).getPath() + "/";
        return map.getPath().startsWith(data) ? map.getPath().substring(data.length())
                                              : "maps/" + map.getName();
    }

    private float maxScroll(float y, float bottom) {
        return Math.max(0f, maps.size() * PITCH - 6f - (bottom - y - ROWS_DY));
    }

    /** The filter at (x, y) and the rows from under it down to bottom. */
    void draw(Canvas c, SkinKit kit, float x, float y, float bottom) {
        final String[] sources = { kit.own("All"), kit.own("Remastered"), kit.own("My maps") };
        LanKit.segments(c, kit, sources, source, x, y, W, SRC_H, LanKit.CAP_MID);
        final float top = y + ROWS_DY;
        scroll = Math.min(scroll, maxScroll(y, bottom));
        c.save();
        c.clipRect(kit.rect(x, top, W, bottom - top));
        if (maps.isEmpty()) {
            kit.textCentre(c, kit.own("No maps here"), x, top, W, bottom - top, LanKit.CAP_BODY,
                           LanKit.INK_MUTED, true);
        }
        for (int i = 0; i < maps.size(); ++i) {
            final float ry = top + i * PITCH - scroll;
            if (ry + ROW_H < top || ry > bottom) {
                continue;
            }
            final boolean sel = (i == selected);
            kit.fill(c, x, ry, W, ROW_H, sel ? LanKit.ROW_SELECTED : LanKit.ROW_FAINT, LanKit.ROW_RADIUS);
            if (sel) {
                kit.fill(c, x, ry, 10f, ROW_H, LanKit.GOLD, 0f);
            }
            final int players = info.get(i).players;
            final String right = (players > 0)
                ? kit.own(players == 1 ? "{0} player" : "{0} players", players) : "";
            kit.textMiddle(c, kit.fit(name(maps.get(i)), LanKit.CAP_BODY, W - ROW_INSET - 520f),
                           x + ROW_INSET, ry, ROW_H, LanKit.CAP_BODY, LanKit.INK, true);
            kit.textMiddleRight(c, right, x + W - ROW_INSET, ry, ROW_H, LanKit.CAP_MID,
                                sel ? LanKit.INK : LanKit.INK_MUTED, true);
        }
        c.restore();
    }

    void down(float px, float py, float x, float y, float bottom) {
        downY = py;
        downScroll = scroll;
        dragging = false;
        inList = LanKit.in(px, py, x, y + ROWS_DY, W, bottom - y - ROWS_DY);
    }

    /** @return true when the list moved */
    boolean move(float py, float y, float bottom) {
        if (!inList) {
            return false;
        }
        if (Math.abs(py - downY) > 16f) {
            dragging = true;
        }
        if (dragging) {
            scroll = Math.max(0f, Math.min(maxScroll(y, bottom), downScroll - (py - downY)));
        }
        return dragging;
    }

    /** A finger was lifted. @return true when the filter or the selection changed */
    boolean up(Context c, float px, float py, float x, float y, float bottom) {
        final boolean dragged = dragging;
        final boolean wasInList = inList;
        dragging = false;
        inList = false;
        final int seg = LanKit.segmentAt(px, py, 3, x, y, W, SRC_H);
        if (seg >= 0 && seg != source) {
            source = seg;
            load(c);
            return true;
        }
        if (wasInList && !dragged && LanKit.in(px, py, x, y + ROWS_DY, W, bottom - y - ROWS_DY)) {
            final int row = (int) ((py - y - ROWS_DY + scroll) / PITCH);
            if (row >= 0 && row < maps.size() && row != selected) {
                selected = row;
                return true;
            }
        }
        return false;
    }
}
