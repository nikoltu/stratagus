package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

import net.wargushd.game.editor.MapGen;
import net.wargushd.game.editor.MapMetrics;
import net.wargushd.game.editor.MapModel;
import net.wargushd.game.editor.MapRules;
import net.wargushd.game.editor.Terrain;

import java.io.File;

/**
 * The sheet of a new, random map: the generator's options in rows of cells on the left, the
 * picture of the generated map on the right, and the buttons Cancel, Another and Create.
 *
 * The map is made on a worker thread. While it works the last picture stays, dimmed; a result
 * that was asked for before the latest change of an option is dropped. The generator always
 * answers: a map, or that these options and this number give none - which is then said in the
 * picture's place.
 */
final class RandomMapSheet {
    static final int NONE = 0, CANCEL = 1, CREATE = 2, CHOSE = 3, EMPTY = 4;

    /** What the sheet asks of the page that shows it. */
    interface Host {
        /** Run on the thread that draws. */
        void post(Runnable r);
        void redraw();
        /** The folder of the unpacked scripts, and the game folder or null. */
        File dataDir();
        File gameFolder();
    }

    // Layout, logical units: a wide modal sheet, centred; its height follows the screen's. Seven
    // rows of a word and a segment of cells on the left, the square picture with two lines
    // under it on the right, three buttons along the bottom.
    private static final float W = 3456f, H_MOST = 1868f, EDGE = 48f, PAD = 72f;
    private static final float ROW_Y = 168f, ROW_PITCH = 156f, ROW_H = 108f, LABEL_W = 380f;
    private static final float SEGMENT_X = 472f, SEGMENT_W = 1500f, CELL_H = 96f, CELL_GAP = 6f;
    private static final float COLUMN_X = 2044f, COLUMN_W = 1340f, FACTS_H = 132f;
    private static final float BUTTON_W = 720f, BUTTON_H = 120f, BUTTON_GAP = 48f, BUTTONS_UP = 228f;
    private static final float CAP_BODY = 44f, CAP_MID = 36f, CAP_SMALL = 30f;

    private static final int SHADE = 0xAA000000, PANEL_FILL = 0xF0140F0A, LINE = 0xFF92743E;
    private static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, INK_OFF = 0xFF6E685C;
    private static final int GOLD = 0xFFF0D250, INK_ON_GOLD = 0xFF1E1608;
    private static final int WELL = 0x78000000, CELL = 0x0EFFFFFF, DIM = 0xB0000000, MARK_EDGE = 0xFF000000;
    private static final int OIL = 0xFF16120E;
    /** The picture's colours by movement class when the game's own cannot be read. */
    private static final int[] PLAIN = { 0xFF1C3C6C, 0xFF5A6A4A, 0xFF2E6B2A, 0xFF14401A, 0xFF6A6A6A };

    private static final int ROW_MAP = 0, ROW_SIZE = 1, ROW_TILESET = 2, ROW_PLAYERS = 3, ROW_KIND = 4,
        ROW_RESOURCES = 5, ROW_LAYOUT = 6, ROWS = 7;
    private static final int[] CELLS = { 2, 4, 4, 7, 3, 3, 2 };
    /** The rows' words: the game's own text by its key, or (no key) a word of the launcher. */
    private static final String[][] ROW_WORD = { { "mp_lobby_map", "Map" }, { null, "Size" }, { null, "Tileset" },
        { "mp_lobby_players", "Players" }, { null, "Map kind" }, { null, "Resources" }, { null, "Layout" } };
    private static final String[] KIND_WORD = { "Land", "Coast", "Islands" };
    private static final String[] ERA_SCRIPT = { "summer", "winter", "wasteland", "swamp" };

    /** A request works no longer than this; on a slow device it then says so. */
    private static final long LIMIT_MILLIS = 20000L;

    private final Host host;
    /** The options: an index of NewMapSheet.SIZES, the map file's tileset number, and the generator's. */
    int size, tileset;
    private int players = 4, kind = MapGen.LAND, resources = MapGen.MEDIUM;
    private boolean natural;
    private long number;

    /** The map on show and its picture; `fresh` when they are of the options as they stand. */
    private MapGen.Result result;
    private Bitmap picture;
    private boolean fresh, working;
    /** Said in the picture's place when the options and the number gave no map, or null. */
    private String trouble;

    // The worker: one thread at a time; it takes the newest request when it is done with one.
    private final Object lock = new Object();
    private MapGen.Options wanted;
    private boolean busy;
    private volatile int serial;
    private final Terrain[] terrains = new Terrain[4];
    private final boolean[] terrainRead = new boolean[4];

    private Bitmap buttonPlain, buttonMarked;

    RandomMapSheet(Host host, int size, int tileset) {
        this.host = host;
        this.size = size;
        this.tileset = tileset;
        number = (System.nanoTime() >>> 8) % 1000000L;
        settle();
        ask();
    }

    // ----- the options -----

    private int tiles() { return NewMapSheet.SIZES[size]; }

    /** After a change of the size or the kind: a kind and a number of players the map takes. */
    private void settle() {
        if (MapGen.mostPlayers(tiles(), kind) < 2) {
            kind = MapGen.LAND;
        }
        players = Math.max(2, Math.min(MapGen.mostPlayers(tiles(), kind, resources), players));
    }

    private boolean chosen(int row, int i) {
        switch (row) {
            case ROW_MAP: return i == 1;
            case ROW_SIZE: return i == size;
            case ROW_TILESET: return i == tileset;
            case ROW_PLAYERS: return i + 2 == players;
            case ROW_KIND: return i == kind;
            case ROW_RESOURCES: return i == resources;
            default: return (i == 1) == natural;
        }
    }

    /** False for a cell the size or the kind rules out; it is drawn faint and takes no tap. */
    private boolean live(int row, int i) {
        if (row == ROW_PLAYERS) {
            return i + 2 <= MapGen.mostPlayers(tiles(), kind, resources);
        }
        return row != ROW_KIND || MapGen.mostPlayers(tiles(), i) >= 2;
    }

    private String word(SkinKit kit, int row, int i) {
        switch (row) {
            case ROW_MAP: return kit.own(i == 0 ? "Empty" : "Random");
            case ROW_SIZE: return String.valueOf(NewMapSheet.SIZES[i]);
            case ROW_TILESET: return LanSettings.tilesetName(kit, i);
            case ROW_PLAYERS: return String.valueOf(i + 2);
            case ROW_KIND: return kit.own(KIND_WORD[i]);
            case ROW_RESOURCES:
                return i == 0 ? kit.string("customscenario_resources_low", "Low")
                     : i == 1 ? kit.string("customscenario_resources_medium", "Medium")
                     : kit.string("customscenario_resources_high", "High");
            default: return kit.own(i == 0 ? "Mirrored" : "Natural");
        }
    }

    private void choose(int row, int i) {
        switch (row) {
            case ROW_SIZE: size = i; break;
            case ROW_TILESET: tileset = i; break;
            case ROW_PLAYERS: players = i + 2; break;
            case ROW_KIND: kind = i; break;
            case ROW_RESOURCES: resources = i; break;
            default: natural = i == 1; break;
        }
        settle();
    }

    /** The line under the rows: why a cell is faint, or nothing. */
    private String hint(SkinKit kit) {
        final int most = MapGen.mostPlayers(tiles(), kind, resources);
        final String small = (tiles() < 64) ? kit.own("Coast and islands need a map of 64 or more.") : "";
        final String few = (most >= 8) ? ""
            : (most < MapGen.mostPlayers(tiles(), kind)) ? kit.own("With high resources this map takes up to {0} players.", most)
            : kit.own("This map takes up to {0} players.", most);
        return small.isEmpty() ? few : few.isEmpty() ? small : small + " " + few;
    }

    // ----- the worker -----

    /** Have the map of the options as they stand made; what is on show stays until it is there. */
    private void ask() {
        final MapGen.Options o = new MapGen.Options(players, tiles(), kind, tileset, resources,
                                                    natural ? MapGen.NONE : MapGen.AUTO, number);
        fresh = false;
        working = true;
        trouble = null;
        synchronized (lock) {
            ++serial;
            wanted = o;
            if (!busy) {
                busy = true;
                final Thread t = new Thread(new Runnable() {
                    @Override
                    public void run() {
                        work();
                    }
                }, "random-map");
                t.setPriority(Thread.NORM_PRIORITY - 1);
                t.start();
            }
        }
    }

    /** The sheet is gone: the worker stops after the try it is in and its result is dropped. */
    void close() {
        synchronized (lock) {
            ++serial;
            wanted = null;
        }
    }

    private Terrain terrain(int era) {
        if (!terrainRead[era]) {
            terrainRead[era] = true;
            final int[] table = Terrain.tileTable(new File(host.dataDir(),
                "scripts/tilesets/wargus/" + ERA_SCRIPT[era] + ".lua"));
            terrains[era] = (table == null) ? null : new Terrain(table);
        }
        return terrains[era];
    }

    /** On the worker thread. */
    private void work() {
        while (true) {
            final MapGen.Options o;
            final int mine;
            synchronized (lock) {
                o = wanted;
                wanted = null;
                mine = serial;
                if (o == null) {
                    busy = false;
                    return;
                }
            }
            MapGen.Outcome made = null;
            Bitmap drawn = null;
            boolean noTiles = false;
            try {
                final Terrain t = terrain(o.tileset);
                noTiles = t == null;
                if (t != null) {
                    made = MapGen.make(o, t, MapGen.TRIES, LIMIT_MILLIS, new MapGen.Stop() {
                        @Override
                        public boolean now() {
                            return serial != mine;
                        }
                    });
                    if (made.kind == MapGen.MADE) {
                        drawn = picture(made.result.map);
                    }
                }
            } catch (RuntimeException | OutOfMemoryError e) {
                made = null;
            }
            final MapGen.Outcome outcome = made;
            final Bitmap bitmap = drawn;
            final boolean unread = noTiles;
            host.post(new Runnable() {
                @Override
                public void run() {
                    done(mine, outcome, bitmap, unread);
                }
            });
        }
    }

    /** On the drawing thread: take the answer when it is the answer to the latest request. */
    private void done(int mine, MapGen.Outcome outcome, Bitmap bitmap, boolean unread) {
        if (mine != serial) {
            return;
        }
        working = false;
        if (outcome != null && outcome.kind == MapGen.MADE && bitmap != null) {
            result = outcome.result;
            picture = bitmap;
            fresh = true;
            android.util.Log.i("WargusHD", "LAUNCHER random map " + tiles() + " " + MapGen.KIND_NAMES[kind] + " "
                + players + "p number " + number + " tries " + outcome.tries + " ms " + outcome.millis
                + " fairness " + Math.round(result.fairness));
        } else {
            trouble = unread ? "The tileset could not be read."
                : (outcome != null && outcome.kind == MapGen.OUT_OF_TIME) ? "This map took too long to make."
                : "No map for these options and this number.";
            android.util.Log.i("WargusHD", "LAUNCHER random map none " + tiles() + " " + MapGen.KIND_NAMES[kind] + " "
                + players + "p number " + number + " kind " + ((outcome == null) ? -1 : outcome.kind));
        }
        host.redraw();
    }

    /** One pixel a tile: the game's own minimap colours, or plain ones when they cannot be read. */
    private Bitmap picture(MapModel m) {
        final Bitmap own = MapPreview.of(host.gameFolder(), host.dataDir(), m);
        if (own != null) {
            return own;
        }
        final int[] px = new int[m.width() * m.height()];
        for (int i = 0; i < px.length; ++i) {
            final int move = MapMetrics.moveClass(m.tileAt(i));
            px[i] = (move >= 0 && move < PLAIN.length) ? PLAIN[move] : 0xFF000000;
        }
        return Bitmap.createBitmap(px, m.width(), m.height(), Bitmap.Config.ARGB_8888);
    }

    /** The map to make the draft of, or null while there is none of the options as they stand. */
    MapGen.Result made() { return (fresh && !working) ? result : null; }

    // ----- layout -----

    private float h(SkinKit kit) { return Math.min(H_MOST, kit.height - 2f * EDGE); }
    private float x() { return (Screen.CANVAS_W - W) / 2f; }
    private float y(SkinKit kit) { return (kit.height - h(kit)) / 2f; }
    private float cellW(int row) { return (SEGMENT_W - CELL_GAP * (CELLS[row] + 1)) / CELLS[row]; }
    private float cellX(int row, int i) { return x() + SEGMENT_X + CELL_GAP + i * (cellW(row) + CELL_GAP); }
    private float cellY(SkinKit kit, int row) { return y(kit) + ROW_Y + row * ROW_PITCH + CELL_GAP; }
    private float side(SkinKit kit) { return Math.min(COLUMN_W, h(kit) - ROW_Y - BUTTONS_UP - FACTS_H); }
    private float pictureX(SkinKit kit) { return x() + COLUMN_X + (COLUMN_W - side(kit)) / 2f; }
    private float pictureY(SkinKit kit) { return y(kit) + ROW_Y; }
    private float buttonY(SkinKit kit) { return y(kit) + h(kit) - PAD - BUTTON_H; }
    private float cancelX() { return x() + PAD; }
    private float createX() { return x() + W - PAD - BUTTON_W; }
    private float anotherX() { return createX() - BUTTON_GAP - BUTTON_W; }

    // ----- drawing -----

    void draw(Canvas c, SkinKit kit) {
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        final float x = x(), y = y(kit), h = h(kit);
        kit.fill(c, x, y, W, h, PANEL_FILL, 18f);
        kit.outline(c, x, y, W, h, LINE, 18f);
        kit.textTop(c, kit.own("New Map"), x + PAD, y + PAD, CAP_BODY, GOLD, true);

        for (int row = 0; row < ROWS; ++row) {
            final float ry = y + ROW_Y + row * ROW_PITCH;
            kit.textMiddle(c, kit.fit(kit.string(ROW_WORD[row][0], ROW_WORD[row][1]), CAP_BODY, LABEL_W), x + PAD, ry, ROW_H, CAP_BODY,
                           INK_MUTED, true);
            kit.fill(c, x + SEGMENT_X, ry, SEGMENT_W, ROW_H, WELL, 14f);
            final float cw = cellW(row), cap = (row == ROW_TILESET) ? CAP_SMALL : CAP_MID;
            for (int i = 0; i < CELLS[row]; ++i) {
                final boolean on = chosen(row, i), can = live(row, i);
                kit.fill(c, cellX(row, i), cellY(kit, row), cw, CELL_H, on ? GOLD : CELL, 10f);
                kit.textCentre(c, kit.fit(word(kit, row, i), cap, cw - 12f), cellX(row, i), cellY(kit, row), cw, CELL_H,
                               cap, on ? INK_ON_GOLD : can ? INK : INK_OFF, !on);
            }
        }
        final String hint = hint(kit);
        if (!hint.isEmpty()) {
            kit.textTop(c, kit.fit(hint, CAP_SMALL, SEGMENT_X + SEGMENT_W - PAD), x + PAD, y + ROW_Y + ROWS * ROW_PITCH,
                        CAP_SMALL, INK_MUTED, true);
        }

        drawPicture(c, kit);

        final float by = buttonY(kit);
        final boolean can = made() != null;
        kit.nine(c, buttonMarked, cancelX(), by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.string("common_cancel", "Cancel"), cancelX(), by, BUTTON_W, BUTTON_H, CAP_BODY, GOLD, true);
        kit.nine(c, buttonPlain, anotherX(), by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.own("Another"), anotherX(), by, BUTTON_W, BUTTON_H, CAP_BODY, INK, true);
        kit.nine(c, buttonPlain, createX(), by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.own("Create"), createX(), by, BUTTON_W, BUTTON_H, CAP_BODY, can ? INK : INK_OFF, true);
    }

    private void drawPicture(Canvas c, SkinKit kit) {
        final float side = side(kit), px = pictureX(kit), py = pictureY(kit);
        kit.fill(c, px, py, side, side, WELL, 0f);
        if (picture != null && result != null) {
            kit.pictureSharp(c, picture, px, py, side, side);
            // The start positions in the players' colours, the mines and the oil patches.
            final MapModel m = result.map;
            final float tile = side / m.width();
            for (int i = 0; i < m.unitCount(); ++i) {
                final MapModel.Unit u = m.unit(i);
                final boolean start = MapRules.isStart(u.type);
                if (!start && !MapRules.isResource(u.type)) {
                    continue;
                }
                final float foot = start ? 1f : 3f, mark = Math.max(foot * tile, start ? 30f : 20f);
                final float mx = px + (u.x + foot / 2f) * tile - mark / 2f, my = py + (u.y + foot / 2f) * tile - mark / 2f;
                final float cx = Math.max(px, Math.min(px + side - mark, mx)), cy = Math.max(py, Math.min(py + side - mark, my));
                kit.fill(c, cx, cy, mark, mark, MARK_EDGE, start ? mark / 2f : 0f);
                kit.fill(c, cx + 3f, cy + 3f, mark - 6f, mark - 6f,
                         start ? Profiles.COLORS[u.owner & 7] : (u.type == MapRules.GOLD_MINE) ? GOLD : OIL,
                         start ? (mark - 6f) / 2f : 0f);
            }
        }
        if (working || trouble != null) {
            kit.fill(c, px, py, side, side, DIM, 0f);
            final String line = working ? kit.own("Making the map...") : kit.own(trouble);
            kit.textCentre(c, kit.fit(line, CAP_MID, side - 48f), px, py, side, side - (working ? 0f : 96f), CAP_MID,
                           INK, true);
            if (!working) {
                kit.textCentre(c, kit.fit(kit.own("Tap Another for the next number."), CAP_SMALL, side - 48f), px,
                               py + 48f, side, side, CAP_SMALL, INK_MUTED, true);
            }
        }
        kit.outline(c, px, py, side, side, LINE, 0f);

        final float fy = py + side + 24f, fw = side;
        if (fresh && !working && result != null) {
            final MapMetrics.Measure s = result.measure;
            final String oil = (s.patches > 0) ? kit.own("{0} oil patches", s.patches) : kit.own("no oil");
            final String facts = kit.own("Balance {0} / 100", Math.round(result.fairness)) + "  -  "
                + kit.own("{0} gold mines", s.mines) + "  -  " + oil;
            kit.textTop(c, kit.fit(facts, CAP_MID, fw), px, fy, CAP_MID, INK, true);
        }
        kit.textTop(c, kit.fit(kit.own("Map number {0}", number), CAP_SMALL, fw), px, fy + 60f, CAP_SMALL, INK_MUTED, true);
    }

    // ----- touches -----

    /**
     * A finger was lifted at (x, y), in logical units. CHOSE: an option changed and a new map
     * is being made. EMPTY: the player wants an empty map after all. CREATE comes only while
     * there is a map of the options as they stand.
     */
    int up(float px, float py, SkinKit kit) {
        for (int row = 0; row < ROWS; ++row) {
            final float cy = cellY(kit, row), cw = cellW(row);
            if (py < cy || py >= cy + CELL_H) {
                continue;
            }
            for (int i = 0; i < CELLS[row]; ++i) {
                if (px >= cellX(row, i) && px < cellX(row, i) + cw) {
                    if (row == ROW_MAP) {
                        return (i == 0) ? EMPTY : NONE;
                    }
                    if (chosen(row, i) || !live(row, i)) {
                        return NONE;
                    }
                    choose(row, i);
                    ask();
                    return CHOSE;
                }
            }
            return NONE;
        }
        final float by = buttonY(kit);
        if (py < by || py >= by + BUTTON_H) {
            return NONE;
        }
        if (px >= cancelX() && px < cancelX() + BUTTON_W) {
            return CANCEL;
        }
        if (px >= anotherX() && px < anotherX() + BUTTON_W) {
            ++number;
            ask();
            return CHOSE;
        }
        if (px >= createX() && px < createX() + BUTTON_W && made() != null) {
            return CREATE;
        }
        return NONE;
    }
}
