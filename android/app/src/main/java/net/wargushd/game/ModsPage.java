package net.wargushd.game;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.view.View;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * The Mods page: the packages installed on this device and those the mod list offers, each
 * with its state, and what the selected one says about itself. A mod that is switched on takes
 * part in the games started from then on; with none switched on the game is the base game. A
 * package comes in from the list or as a file the player picks, and the selected one can be
 * removed. The list is asked for when the page opens and when it is pulled down.
 */
final class ModsPage {
    static final int ACTION_NONE = 0, ACTION_SWITCH = 1, ACTION_IMPORT = 2, ACTION_REMOVE = 3,
        ACTION_DOWNLOAD = 4, ACTION_CANCEL = 5, ACTION_REFRESH = 6, ACTION_FILTER = 7;
    /** What the page waits for: nothing, a package file being read, a package being removed. */
    static final int IDLE = 0, IMPORTING = 1, REMOVING = 2;
    static final int FILTER_ALL = 0, FILTER_INSTALLED = 1, FILTER_UPDATES = 2;
    /** A row's state as the list and the device give it; a download and a failure stand above it. */
    static final int NEW = 0, AVAILABLE = 1, UPDATE = 2, INSTALLED = 3, WITHDRAWN = 4, IMPORTED = 5;

    /** One mod: installed, offered by the list, or both. */
    static final class Row {
        String id, name;
        ModStore.Mod mod;
        ModCatalog.Entry entry;
        int state;
        /** ModCatalog.FITS, or why this version cannot take the list's package. */
        int fit = ModCatalog.FITS;
    }

    // Layout, logical units: the list and the detail panel stand where the Load Game page has its own.
    private static final float X0 = 816f, TOP = 264f, BOTTOM_UP = 264f, LIST_W = 1936f;
    private static final float DETAIL_X = 2800f, DETAIL_W = 944f;
    private static final float ROW_H = 132f, ROW_PITCH = 138f, LINE_PITCH = 82.5f;
    private static final float SWITCH_W = 144f, SWITCH_H = 72f;
    private static final float CAP_TITLE = 60f, CAP_BODY = 44f, CAP_MID = 36f;
    // The import stands where the Custom Scenario page has its own; the removal at the detail panel's foot.
    private static final float ACTION_W = 720f, ACTION_H = 120f, ACTION_UP = 204f, REMOVE_UP = 60f;
    // The filter: the Custom Scenario page's source segments, at the head of the list panel.
    private static final float SEG_H = 108f, BOX_RADIUS = 14f;
    // A download in a row: the bar before the percentage.
    private static final float BAR_W = 300f, BAR_H = 18f;
    /** How far the list is pulled before a release asks for it again. */
    private static final float PULL = 180f;

    private static final int PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E;
    private static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, GOLD = 0xFFF0D250;
    private static final int INK_TITLE = 0xFF000000, INK_DISABLED = 0xFF9C9486, INK_ON_GOLD = 0xFF1E1608;
    private static final int ROW_SELECTED = 0x36F0D250, ROW_FAINT = 0x10FFFFFF, SEG_FAINT = 0x0EFFFFFF;
    private static final int WELL = 0x78000000, INK_FAILED = 0xFFE89070;
    private static final int SWITCH_ON = 0xFF78B060, SWITCH_OFF = 0xFF463E34, KNOB = 0xFFECE6D6;
    private static final int DIM = 0x96000000;

    private List<ModStore.Mod> mods = new ArrayList<ModStore.Mod>();
    private Set<String> enabled;
    private ModCatalog.State catalog = new ModCatalog.State();
    private Set<String> newIds = new HashSet<String>(), fromList = new HashSet<String>();
    private int versionCode;
    /** The reason each failed download is told with, by id. */
    private final Map<String, String> failed = new HashMap<String, String>();
    private List<Row> rows = new ArrayList<Row>();
    private boolean reading, syncing;
    private int filter = FILTER_ALL;
    private String selectedId;
    private float scroll;
    private int waiting = IDLE;
    private Bitmap buttonPlain;

    // List dragging, and the pull that asks for the list again.
    private float downY, downScroll, pull;
    private boolean inList, dragging;

    /** Read the packages and the list again, off the main thread; the page is redrawn when they are known. */
    void open(Context c, View view) {
        open(c, view, null);
    }

    /** As open; the package of this id, when it is there, is then the selected one. */
    void open(Context c, final View view, final String select) {
        newIds.clear();
        read(c, view, select);
    }

    /** The list pulled down: asked for again; what was new in this visit stays marked. */
    void refresh(Context c, View view) {
        read(c, view, selectedId);
    }

    private void read(Context c, final View view, final String select) {
        final Context app = c.getApplicationContext();
        reading = true;
        syncing = ModCatalog.base(app) != null;
        new Thread(new Runnable() {
            @Override
            public void run() {
                final List<ModStore.Mod> found = ModStore.installed(app);
                final Set<String> on = ModStore.enabled(app);
                final Set<String> ids = new HashSet<String>();
                for (ModStore.Mod m : found) {
                    ids.add(m.id);
                }
                final Set<String> listed = ModCatalog.fromList(app);
                final int code = ModCatalog.versionCode(app);
                final ModCatalog.State cached = ModCatalog.cached(app);
                final Set<String> fresh = ModCatalog.takeNew(app, cached, ids);
                view.post(new Runnable() {
                    @Override
                    public void run() {
                        mods = found;
                        enabled = on;
                        fromList = listed;
                        versionCode = code;
                        catalog = cached;
                        newIds.addAll(fresh);
                        reading = false;
                        if (select != null) {
                            selectedId = select;
                        }
                        rebuild();
                        scroll = 0f;
                        view.invalidate();
                    }
                });
                if (cached.section == ModCatalog.NO_SOURCE) {
                    return;
                }
                final ModCatalog.State synced = ModCatalog.sync(app);
                final Set<String> later = ModCatalog.takeNew(app, synced, ids);
                view.post(new Runnable() {
                    @Override
                    public void run() {
                        catalog = synced;
                        newIds.addAll(later);
                        syncing = false;
                        rebuild();
                        view.invalidate();
                    }
                });
            }
        }, "mods-list").start();
    }

    /** Join the installed packages and the list into rows, in name order, as the filter keeps them. */
    private void rebuild() {
        final Map<String, Row> byId = new LinkedHashMap<String, Row>();
        for (ModStore.Mod m : mods) {
            final Row r = new Row();
            r.id = m.id;
            r.name = m.name;
            r.mod = m;
            byId.put(m.id, r);
        }
        for (ModCatalog.Entry e : catalog.entries) {
            Row r = byId.get(e.id);
            if (r == null) {
                r = new Row();
                r.id = e.id;
                r.name = e.name;
                byId.put(e.id, r);
            }
            r.entry = e;
        }
        final List<Row> all = new ArrayList<Row>();
        for (Row r : byId.values()) {
            if (r.entry != null) {
                r.fit = ModCatalog.fit(r.entry, versionCode);
            }
            if (r.mod == null) {
                r.state = newIds.contains(r.id) ? NEW : AVAILABLE;
            } else if (r.entry != null) {
                r.state = ModCatalog.compareVersions(r.entry.version, r.mod.version) > 0 ? UPDATE : INSTALLED;
            } else if (fromList.contains(r.id)) {
                r.state = catalog.listed ? WITHDRAWN : INSTALLED;
            } else {
                r.state = IMPORTED;
            }
            if (filter == FILTER_ALL || (filter == FILTER_INSTALLED && r.mod != null)
                || (filter == FILTER_UPDATES && r.state == UPDATE)) {
                all.add(r);
            }
        }
        Collections.sort(all, new Comparator<Row>() {
            @Override
            public int compare(Row a, Row b) {
                return a.name.compareToIgnoreCase(b.name);
            }
        });
        rows = all;
        if (selectedRow() == null) {
            selectedId = rows.isEmpty() ? null : rows.get(0).id;
        }
    }

    Row selectedRow() {
        for (Row r : rows) {
            if (r.id.equals(selectedId)) {
                return r;
            }
        }
        return null;
    }

    ModStore.Mod selectedMod() {
        final Row r = selectedRow();
        return r != null ? r.mod : null;
    }

    /** While the page waits, its buttons and switches take no touch. */
    void setWaiting(int what) {
        waiting = what;
    }

    /** Waiting, or a download under way (one package at a time comes in). */
    boolean isWaiting() {
        return waiting != IDLE || ModCatalog.current != null;
    }

    /** A download of this id failed for this reason. */
    void failed(String id, String reason) {
        failed.put(id, reason);
    }

    void clearFailed(String id) {
        failed.remove(id);
    }

    /** The list's package of this row can be fetched now. */
    boolean canDownload(Row r) {
        return r != null && r.entry != null && r.fit == ModCatalog.FITS && !isWaiting() && !syncing
            && catalog.section == ModCatalog.FRESH && (r.mod == null || r.state == UPDATE || failed.containsKey(r.id));
    }

    private boolean isOn(ModStore.Mod m) {
        return m.usable && enabled != null && enabled.contains(m.id);
    }

    private static boolean downloading(Row r) {
        final ModCatalog.Download d = ModCatalog.current;
        return d != null && d.entry.id.equals(r.id);
    }

    private float bottom(SkinKit kit) { return kit.height - BOTTOM_UP; }
    private float segY() { return TOP + 24f; }
    private float listTop() { return segY() + SEG_H + 24f; }
    private float listBottom(SkinKit kit) { return bottom(kit) - 24f; }
    private float rowX() { return X0 + 24f; }
    private float rowW() { return LIST_W - 48f; }
    private float switchX() { return rowX() + rowW() - 36f - SWITCH_W; }
    private float actionY(SkinKit kit) { return kit.height - ACTION_UP; }
    private float removeX() { return DETAIL_X + (DETAIL_W - ACTION_W) / 2f; }
    private float removeY(SkinKit kit) { return bottom(kit) - REMOVE_UP - ACTION_H; }
    private float downloadY(SkinKit kit, Row r) {
        return r.mod != null ? removeY(kit) - ACTION_H - 24f : removeY(kit);
    }

    /** Whether the detail panel shows the Download / Update / Cancel button for this row. */
    private boolean hasDownloadButton(Row r) {
        return r.entry != null && (r.mod == null || r.state == UPDATE || failed.containsKey(r.id) || downloading(r));
    }

    private float maxScroll(SkinKit kit) {
        return Math.max(0f, rows.size() * ROW_PITCH - 6f - (listBottom(kit) - listTop()));
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

    private void bar(Canvas c, SkinKit kit, float x, float y, float w, int percent) {
        kit.fill(c, x, y, w, BAR_H, WELL, BAR_H / 2f);
        if (percent > 0) {
            kit.fill(c, x, y, Math.max(BAR_H, w * percent / 100f), BAR_H, GOLD, BAR_H / 2f);
        }
        kit.outline(c, x, y, w, BAR_H, LINE, BAR_H / 2f);
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

    static String megabytes(long bytes) {
        final double mb = bytes / (1024.0 * 1024.0);
        return mb < 10.0 ? String.format(Locale.ROOT, "%.1f", mb) : String.valueOf(Math.round(mb));
    }

    private static String time(long ms) {
        return new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.ROOT).format(new java.util.Date(ms));
    }

    /** The section's state, shown under the title. */
    private String subtitle(SkinKit kit) {
        if (syncing) {
            return kit.own("Checking the list...");
        }
        final String from = catalog.checkedAt > 0 ? time(catalog.checkedAt) : "";
        switch (catalog.section) {
            case ModCatalog.FRESH:
                return kit.own("Checked just now - {0} in the list", catalog.entries.size());
            case ModCatalog.OFFLINE:
                return catalog.listed ? kit.own("Offline - list from {0}", from) : kit.own("Offline - no list yet");
            case ModCatalog.REFUSED:
                return catalog.listed ? kit.own("The mod list could not be opened - list from {0}", from)
                                      : kit.own("The mod list could not be opened");
            case ModCatalog.NEWER_APP:
                return kit.own("A newer app version is needed for the mod list.");
            case ModCatalog.NO_SOURCE:
                return kit.own("No mod source - installed mods and Import a Mod only");
            default:
                return catalog.listed ? kit.own("List from {0}", from) : "";
        }
    }

    /** A row's state as a short word, and its ink; null text when there is none to show. */
    private String label(SkinKit kit, Row r, int[] ink) {
        ink[0] = INK_MUTED;
        if (failed.containsKey(r.id)) {
            ink[0] = INK_FAILED;
            return kit.own("Failed - tap to retry");
        }
        if (r.mod == null && r.fit != ModCatalog.FITS) {
            ink[0] = INK_DISABLED;
            return r.fit == ModCatalog.NEEDS_NEWER_APP ? kit.own("Needs a newer app") : kit.own("Not for this app");
        }
        switch (r.state) {
            case NEW:
                ink[0] = GOLD;
                return kit.own("New - {0} MB", megabytes(r.entry.size));
            case AVAILABLE:
                return kit.own("{0} MB", megabytes(r.entry.size));
            case UPDATE:
                ink[0] = GOLD;
                return kit.own("Update");
            case INSTALLED:
                return kit.own("Installed");
            case WITHDRAWN:
                return kit.own("No longer in the list");
            default:
                return kit.own("Imported");
        }
    }

    void draw(Canvas c, SkinKit kit) {
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
        }
        kit.textTop(c, kit.own("Mods"), X0, 84f, CAP_TITLE, INK_TITLE, false);
        final String sub = subtitle(kit);
        final float titleW = kit.width(kit.own("Mods"), CAP_TITLE);
        if (!sub.isEmpty()) {
            // The section's state on the title's line, at the list's right edge.
            kit.textTopRight(c, kit.fit(sub, CAP_MID, LIST_W - titleW - 96f), X0 + LIST_W, 84f + CAP_TITLE - CAP_MID,
                             CAP_MID, INK_TITLE, false);
        }

        final float b = bottom(kit);
        panel(c, kit, X0, TOP, LIST_W, b - TOP);
        // The filter.
        final float sy = segY();
        kit.fill(c, rowX(), sy, rowW(), SEG_H, WELL, BOX_RADIUS);
        kit.outline(c, rowX(), sy, rowW(), SEG_H, LINE, BOX_RADIUS);
        final String[] filters = { kit.own("All"), kit.own("Installed"), kit.own("Updates") };
        final float segW = rowW() / filters.length;
        for (int i = 0; i < filters.length; ++i) {
            final float x = rowX() + segW * i + 6f;
            final boolean on = (i == filter);
            kit.fill(c, x, sy + 6f, segW - 12f, SEG_H - 12f, on ? GOLD : SEG_FAINT, 10f);
            kit.textCentre(c, filters[i], x, sy + 6f, segW - 12f, SEG_H - 12f, CAP_MID, on ? INK_ON_GOLD : INK, !on);
        }

        c.save();
        c.clipRect(kit.rect(rowX(), listTop(), rowW(), listBottom(kit) - listTop()));
        final float shift = Math.min(pull, PULL) / 2f;
        if (pull > 0f) {
            kit.textCentre(c, pull >= PULL ? kit.own("Release to check the list") : kit.own("Pull to check the list"),
                           rowX(), listTop(), rowW(), shift, CAP_MID, INK_MUTED, true);
        }
        if (rows.isEmpty() && !reading) {
            // As the maps' list: one line in the middle (owner's rule).
            kit.textCentre(c, filter == FILTER_UPDATES ? kit.own("No updates") : kit.own("No mods installed"),
                           rowX(), listTop(), rowW(), listBottom(kit) - listTop(), CAP_BODY, INK_MUTED, true);
        }
        final int[] ink = new int[1];
        for (int i = 0; i < rows.size(); ++i) {
            final float y = listTop() + shift + i * ROW_PITCH - scroll;
            if (y + ROW_H < listTop() || y > listBottom(kit)) {
                continue;
            }
            final Row r = rows.get(i);
            final boolean sel = r.id.equals(selectedId);
            kit.fill(c, rowX(), y, rowW(), ROW_H, sel ? ROW_SELECTED : ROW_FAINT, 10f);
            if (sel) {
                kit.fill(c, rowX(), y, 10f, ROW_H, GOLD, 0f);
            }
            final boolean hasSwitch = r.mod != null && r.mod.usable;
            float right = hasSwitch ? switchX() - 36f : rowX() + rowW() - 36f;
            if (hasSwitch) {
                final boolean on = isOn(r.mod);
                final float sx = switchX(), swy = y + (ROW_H - SWITCH_H) / 2f;
                kit.fill(c, sx, swy, SWITCH_W, SWITCH_H, on ? SWITCH_ON : SWITCH_OFF, 36f);
                kit.outline(c, sx, swy, SWITCH_W, SWITCH_H, LINE, 36f);
                kit.fill(c, sx + (on ? 78f : 6f), swy + 6f, 60f, 60f, KNOB, 30f);
            }
            if (downloading(r)) {
                final ModCatalog.Download d = ModCatalog.current;
                final String what = d.installing ? kit.own("Installing...") : d.percent() + "%";
                final float tw = kit.width(what, CAP_MID);
                kit.textMiddleRight(c, what, right, y, ROW_H, CAP_MID, GOLD, true);
                right -= tw + 24f;
                if (!d.installing) {
                    bar(c, kit, right - BAR_W, y + (ROW_H - BAR_H) / 2f, BAR_W, d.percent());
                    right -= BAR_W + 24f;
                }
            } else {
                final String what = label(kit, r, ink);
                kit.textMiddleRight(c, what, right, y, ROW_H, CAP_MID, ink[0], true);
                right -= kit.width(what, CAP_MID) + 36f;
            }
            final boolean usable = r.mod != null ? r.mod.usable : r.fit == ModCatalog.FITS;
            kit.textMiddle(c, kit.fit(r.name, CAP_BODY, right - rowX() - 36f), rowX() + 36f, y, ROW_H, CAP_BODY,
                           usable ? INK : INK_DISABLED, true);
        }
        c.restore();

        panel(c, kit, DETAIL_X, TOP, DETAIL_W, b - TOP);
        final Row r = selectedRow();
        if (r != null) {
            drawDetail(c, kit, r);
        }

        // As the import of a map: at the left of the row under the panels.
        button(c, kit, X0, actionY(kit),
               waiting == IMPORTING ? kit.own("Importing...") : kit.own("Import a Mod"), !isWaiting());
    }

    private void drawDetail(Canvas c, SkinKit kit, Row r) {
        final ModStore.Mod m = r.mod;
        final ModCatalog.Entry e = r.entry;
        final boolean button = hasDownloadButton(r);
        final float tx = DETAIL_X + 60f, tw = DETAIL_W - 120f;
        // The text ends above the panel's buttons.
        final float textEnd = (button ? downloadY(kit, r) : removeY(kit)) - 24f;
        c.save();
        c.clipRect(kit.rect(DETAIL_X, TOP, DETAIL_W, textEnd - TOP));
        float y = paragraph(c, kit, r.name, tx, TOP + 66f, tw, CAP_BODY, LINE_PITCH, GOLD);
        final String author = m != null && !m.author.isEmpty() ? m.author : e != null ? e.author : "";
        if (!author.isEmpty()) {
            kit.textTop(c, kit.fit(author, CAP_MID, tw), tx, y, CAP_MID, INK, true);
            y += LINE_PITCH;
        }
        String version = m != null ? m.version : e.version;
        if (r.state == UPDATE) {
            version = kit.own("{0} - {1} in the list", m.version, e.version);
        }
        if (!version.isEmpty()) {
            kit.textTop(c, kit.fit(kit.own("Version {0}", version), CAP_MID, tw), tx, y, CAP_MID, INK_MUTED, true);
            y += LINE_PITCH;
        }
        if (e != null) {
            final StringBuilder kinds = new StringBuilder();
            for (String k : e.categories) {
                kinds.append(kinds.length() == 0 ? "" : ", ").append(k);
            }
            final String facts = kinds.length() == 0 ? kit.own("{0} MB", megabytes(e.size))
                                                     : kit.own("{0} MB", megabytes(e.size)) + " - " + kinds;
            kit.textTop(c, kit.fit(facts, CAP_MID, tw), tx, y, CAP_MID, INK_MUTED, true);
            y += LINE_PITCH;
        }
        final String about = m != null && !m.description.isEmpty() ? m.description : e != null ? e.description : "";
        y = paragraph(c, kit, about, tx, y + 24f, tw, CAP_MID, 60f, INK);
        y += 24f;
        if (downloading(r)) {
            final ModCatalog.Download d = ModCatalog.current;
            if (d.installing) {
                paragraph(c, kit, kit.own("Checked. Installing..."), tx, y, tw, CAP_MID, 60f, GOLD);
            } else {
                y = paragraph(c, kit, kit.own("Downloading... {0}%", d.percent()), tx, y, tw, CAP_MID, 60f, GOLD);
                bar(c, kit, tx, y + 6f, tw, d.percent());
            }
        } else if (failed.containsKey(r.id)) {
            paragraph(c, kit, kit.own("Failed: {0}", failed.get(r.id)), tx, y, tw, CAP_MID, 60f, INK_FAILED);
        } else if (m == null) {
            final String why;
            if (r.fit == ModCatalog.NEEDS_NEWER_APP) {
                why = kit.own("This mod needs a newer version of the app.");
            } else if (r.fit == ModCatalog.NEEDS_OLDER_APP) {
                why = kit.own("This mod is for an older version of the app.");
            } else if (r.fit == ModCatalog.HOLDS_CODE) {
                why = kit.own("This mod holds program code. The app does not install it.");
            } else if (catalog.section != ModCatalog.FRESH) {
                why = kit.own("Downloads need the list to be checked. Pull the list down to try again.");
            } else {
                why = kit.own("Not installed.");
            }
            paragraph(c, kit, why, tx, y, tw, CAP_MID, 60f, r.fit == ModCatalog.FITS ? INK_MUTED : INK_DISABLED);
        } else if (!m.usable) {
            paragraph(c, kit, kit.own("This mod cannot be used with this version of the app."), tx, y, tw, CAP_MID,
                      60f, INK_DISABLED);
        } else {
            if (r.state == WITHDRAWN) {
                y = paragraph(c, kit, kit.own("No longer in the list. It stays installed and can be used."), tx, y,
                              tw, CAP_MID, 60f, INK_MUTED);
            }
            paragraph(c, kit, isOn(m) ? kit.own("On. The game uses it from the next start.")
                                      : kit.own("Off. The game is as it is without it."),
                      tx, y, tw, CAP_MID, 60f, INK_MUTED);
        }
        c.restore();
        if (button) {
            final String caption;
            final boolean enabled;
            if (downloading(r)) {
                caption = kit.string("common_cancel", "Cancel");
                enabled = !ModCatalog.current.installing;
            } else {
                caption = failed.containsKey(r.id) ? kit.own("Try Again")
                    : r.state == UPDATE ? kit.own("Update") : kit.own("Download");
                enabled = canDownload(r);
            }
            button(c, kit, removeX(), downloadY(kit, r), caption, enabled);
        }
        if (m != null) {
            button(c, kit, removeX(), removeY(kit),
                   waiting == REMOVING ? kit.own("Removing...") : kit.own("Remove"), !isWaiting());
        }
    }

    private static boolean in(float x, float y, float rx, float ry, float rw, float rh) {
        return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
    }

    void down(float x, float y, SkinKit kit) {
        downY = y;
        downScroll = scroll;
        dragging = false;
        pull = 0f;
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
            final float want = downScroll - (y - downY);
            scroll = Math.max(0f, Math.min(maxScroll(kit), want));
            // Pulled beyond the top: the list is asked for again on release.
            pull = (want < 0f && !syncing) ? -want : 0f;
        }
        return dragging;
    }

    /**
     * A finger was lifted at (x, y). @return ACTION_SWITCH when a switch was turned, ACTION_IMPORT,
     * ACTION_REMOVE, ACTION_DOWNLOAD (also a tap on a failed row) or ACTION_CANCEL when that was
     * asked for, ACTION_REFRESH when the list was pulled, ACTION_FILTER when the filter changed,
     * else ACTION_NONE
     */
    int up(Context c, float x, float y, SkinKit kit) {
        final boolean wasDrag = dragging, wasInList = inList;
        final boolean pulled = pull >= PULL;
        dragging = false;
        inList = false;
        pull = 0f;
        if (pulled) {
            return ACTION_REFRESH;
        }
        if (wasDrag) {
            return ACTION_NONE;
        }
        if (in(x, y, rowX(), segY(), rowW(), SEG_H)) {
            final int seg = Math.min(2, (int) ((x - rowX()) / (rowW() / 3f)));
            if (seg == filter) {
                return ACTION_NONE;
            }
            filter = seg;
            scroll = 0f;
            rebuild();
            return ACTION_FILTER;
        }
        final Row sel = selectedRow();
        // A download's Cancel takes touch while the rest waits.
        if (sel != null && downloading(sel) && !ModCatalog.current.installing
            && in(x, y, removeX(), downloadY(kit, sel), ACTION_W, ACTION_H)) {
            return ACTION_CANCEL;
        }
        if (waiting != IDLE) {
            return ACTION_NONE;
        }
        if (!isWaiting() && in(x, y, X0, actionY(kit), ACTION_W, ACTION_H)) {
            return ACTION_IMPORT;
        }
        if (sel != null && hasDownloadButton(sel) && canDownload(sel)
            && in(x, y, removeX(), downloadY(kit, sel), ACTION_W, ACTION_H)) {
            return ACTION_DOWNLOAD;
        }
        if (sel != null && sel.mod != null && !isWaiting() && in(x, y, removeX(), removeY(kit), ACTION_W, ACTION_H)) {
            return ACTION_REMOVE;
        }
        if (!wasInList) {
            return ACTION_NONE;
        }
        final int row = (int) ((y - listTop() + scroll) / ROW_PITCH);
        if (row < 0 || row >= rows.size()) {
            return ACTION_NONE;
        }
        final Row r = rows.get(row);
        selectedId = r.id;
        // The switch takes the row's right end, a finger's width around the drawn one; not while
        // a download is under way (setWaiting: buttons and switches take no touch).
        if (r.mod != null && r.mod.usable && enabled != null && !isWaiting() && x >= switchX() - 48f) {
            final boolean on = !enabled.contains(r.id);
            ModStore.setEnabled(c, r.id, on);
            if (on) {
                enabled.add(r.id);
            } else {
                enabled.remove(r.id);
            }
            return ACTION_SWITCH;
        }
        if (failed.containsKey(r.id) && canDownload(r)) {
            return ACTION_DOWNLOAD;
        }
        return ACTION_NONE;
    }
}
