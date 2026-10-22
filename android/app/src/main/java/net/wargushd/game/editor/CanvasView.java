package net.wargushd.game.editor;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;

/**
 * The editor's map view: the map drawn from the terrain atlas of the player's game folder, and
 * its minimap. Two fingers move and zoom the view; a finger on the minimap moves it; one finger
 * on the map paints the terrain kind chosen in the palette column with the chosen brush, on as
 * many sides of the map as the Mirror cells of the tool column say. The other tools of the tool
 * column place, choose and remove units, fill an area with the chosen kind and pick a kind or a
 * unit from the map; three switches under them lay a grid and the ground classes over the map.
 *
 * A tile's picture is found as the game finds it: the map's tile number goes through the
 * tileset's tile table (the "slots" of scripts/tilesets/wargus/ERA.lua) to a megatile number M,
 * and the picture is the frame PREFIX_(M - 16) of Data/Art/hd/bgs/bgs_NAME.png, read here at
 * run time and reduced into one small sheet. Nothing of the game folder is kept on the disk.
 *
 * The view lies under the editor's top bar; its rects are those of the editor frame less the
 * bar: tool column, canvas, palette column with the minimap (logical units of a 3840 canvas).
 */
public final class CanvasView extends View {
    /** Tileset script, atlas name and frame prefix, by the PUD's tileset number. */
    private static final String[][] ERAS = {
        { "summer", "Forest", "forest" }, { "winter", "Ice", "iceland" },
        { "wasteland", "Swamp", "swamp" }, { "swamp", "XSwamp", "xswamp" },
    };
    private static final int FIRST_MEGATILE = 16;
    /** The atlas is decoded at this reduction; a frame becomes a cell of CELL pixels in the sheet. */
    private static final int REDUCE = 2, CELL = 64;
    /** Below this many pixels a tile the map is drawn from one picture of the whole map. */
    private static final int OVERVIEW_TILE = 8;

    // Layout, logical units of the 3840-wide canvas; y from the view's top (the bar's foot).
    private static final float CANVAS_W = 3840f, TOOLS_W = 264f, PALETTE_X = 3120f, PALETTE_W = 720f;
    private static final float MINI = 672f, MINI_PAD = 24f;
    /** The largest a tile is drawn, logical units (twice the game's own 216 on a 3840 canvas). */
    private static final float MAX_TILE = 432f;

    private static final int GROUND = 0xFF0D1114, COLUMN = 0xF00E0B08, LINE = 0xFF92743E;
    private static final int VIEW_BOX = 0xFFF0D250, INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6;

    /**
     * What the view needs of the page that shows it: the launcher's words and letters, its
     * click, a redraw of the bar above the view, and whatever the page lays over the view.
     */
    public interface Host {
        /** One of the launcher's own words in the player's language. */
        String word(String english);
        /** A text of the game's own string table by its key; the English text where the table has none. */
        String term(String key, String english);
        /**
         * Draw text on the view's canvas; the rect is in logical units from the view's top
         * left. Centred in the rect, or from its left edge with the capitals centred in h.
         */
        void text(Canvas c, String s, float x, float y, float w, float h, float cap, int color, boolean centred);
        /** The map, the undo list or the saved state changed: the bar is to be drawn again. */
        void changed();
        /** A tap chose something. */
        void click();
        /** Draw what lies over the view (a sheet), after everything else. */
        void over(Canvas c);
        /** True while what lies over the view takes every touch. */
        boolean modal();
        /** A touch while modal, in the view's pixels. */
        void touchOver(int action, float x, float y);
        /** The colour of a player slot, 0 to 7. */
        int colour(int slot);
        /** The Balance tile was tapped; the last measurement of the map, which may be an older one. */
        void balance(BalanceWatch.Answer measured);
        /** Say a line in the bar for a while: one of the launcher's own words with {0}.. places. */
        void notice(long millis, String english, Object... values);
        /**
         * The amount of the chosen mines or patches was tapped (owner decision 94, E1): open the
         * number sheet over the view, titled with the kind's name; `current` is the amount they
         * share, or -1 when they differ. OK hands the rounded data word to {@link #typeAmount}.
         */
        void amount(String title, long current);
    }
    private Host host;

    private final WorkingCopy work;
    private final MapModel map;
    private final File gameFolder, dataDir;

    private volatile Bitmap sheet;         // the tileset's frames, CELL pixels each, in rows of sheetColumns
    private volatile int sheetColumns;
    private volatile int[] cellOfTile;     // sheet cell by tile number, -1 without a picture
    private volatile Bitmap overview;      // the whole map, OVERVIEW_TILE pixels a tile
    private volatile boolean failed;
    private volatile Terrain terrain;      // null until the tile table is read

    /**
     * The still pictures of the palette's units, in the order of UnitFrames.TYPES; null where none
     * is held. Read lazily (Session 2026-10-19 d): a type's picture is asked for when it is first
     * drawn (a palette cell, a unit on the map) and read off this thread, one after the other;
     * above PICTURE_BUDGET bytes the pictures drawn longest ago are let go and asked for again
     * when drawn. Only the UI thread sets and frees them.
     */
    private final java.util.concurrent.atomic.AtomicReferenceArray<UnitArt.Picture> pictures =
        new java.util.concurrent.atomic.AtomicReferenceArray<UnitArt.Picture>(UnitFrames.TYPES.length);
    /** By the same order: 0 not asked, 1 asked, 2 read and none found. */
    private final java.util.concurrent.atomic.AtomicIntegerArray asked =
        new java.util.concurrent.atomic.AtomicIntegerArray(UnitFrames.TYPES.length);
    private final java.util.concurrent.LinkedBlockingQueue<Integer> wanted = new java.util.concurrent.LinkedBlockingQueue<Integer>();
    /** The frame a picture was last drawn in; the frame being drawn. UI thread only. */
    private final long[] lastDrawn = new long[UnitFrames.TYPES.length];
    private long frameNumber;
    private long picturesHeld;
    /** Bytes of pictures held at most (provisional: the whole palette of a tileset holds about 26 MB at half size). */
    private static final long PICTURE_BUDGET = 16L * 1024 * 1024;
    private Thread unitLoader;
    private final UnitArt art = new UnitArt();
    private final float[] spot = new float[3];
    private volatile boolean released;

    // The balance of the map as it stands: measured away from this thread once the changes have settled.
    private static final long SETTLE_MILLIS = 500L;
    private BalanceWatch.Answer balance;
    private boolean balanceStale = true;
    private final BalanceWatch watch = new BalanceWatch(new BalanceWatch.Listener() {
        @Override
        public void measured(final BalanceWatch.Answer a) {
            post(new Runnable() {
                @Override
                public void run() {
                    taken(a);
                }
            });
        }
    });
    private final Runnable settled = new Runnable() {
        @Override
        public void run() {
            if (released) {
                return;
            }
            try {
                watch.ask(work.pud().bytes());
            } catch (RuntimeException e) {
                // The tile keeps what it shows.
            }
        }
    };

    /** A measurement came back; one overtaken by a later change is dropped. */
    private void taken(BalanceWatch.Answer a) {
        if (watch.current(a)) {
            balance = a;
            balanceStale = false;
            invalidate();
        }
    }

    // The terrain tool: the kind and the brush chosen, and the stroke under the finger.
    private int kind = Terrain.GRASS, brush, mirror = Terrain.MIRROR_OFF;
    private Terrain.Stroke painting;
    private float paintX, paintY;
    private long paintBegan;
    private boolean inPalette, inTools, trouble;

    // The tools of the tool column, and what the tools for units hold.
    private static final int TOOL_TERRAIN = 0, TOOL_UNITS = 1, TOOL_SELECT = 2, TOOL_ERASE = 3;
    private static final int TOOL_FILL = 4, TOOL_PICK = 5;
    /** What is laid over the map: the tile grid, where land units pass, where a hall may stand. */
    private static final int SHOW_GRID = 0, SHOW_PASSABLE = 1, SHOW_BUILDABLE = 2;
    private final boolean[] shows = new boolean[3];
    /** Fill and Pick: a finger is down on the map and has not left its place. */
    private boolean tapPending;
    private float tapX, tapY;
    private int tool = TOOL_TERRAIN, owner;
    /** The unit type the Units tool places (UnitCatalog), and the race the palette shows (owner decision 88, 5B). */
    private int unitType = MapRules.GOLD_MINE, unitRace = UnitCatalog.HUMAN;
    /** How far the palette's list of groups is scrolled, logical units. */
    private float listScroll;
    /** The units chosen (Select, a rectangle, a long press), in the order of the unit list; none when empty. */
    private int[] chosen = new int[0];
    /** The tools for units: the type under the finger, or -1, and its top left tile. */
    private int ghostType = -1, ghostX, ghostY;
    private float grabX, grabY;
    /** Select: the finger came down on a chosen unit at a tile; the chosen units are moved by so many tiles. */
    private boolean grabbing, groupMoving;
    private int grabTX, grabTY, moveDX, moveDY;
    /** Select: a rectangle drawn from a tile of empty ground; whether the finger has left that tile. */
    private boolean boxing, boxMoved;
    private int boxX0, boxY0, boxX1, boxY1;

    // The long press (EDITOR_DESIGN.md 4.1): 400 ms with no movement.
    private static final long HOLD_MILLIS = 400L;
    private float downX, downY;
    /** A long press has acted; the finger does nothing more until it is lifted. */
    private boolean held;
    private final Runnable hold = new Runnable() {
        @Override
        public void run() {
            longPress();
        }
    };

    // Scrolling at the canvas edge while painting or moving a unit (EDITOR_DESIGN.md 4.1). The band
    // and the speed are not designed: provisional numbers (EDITOR_STATUS.md, Session 2026-10-19 b).
    private static final float EDGE_BAND = 120f, EDGE_SPEED = 2400f;     // logical units; logical units a second
    private float lastX, lastY;
    private long edgeLast;
    private boolean edging;
    private final Runnable edge = new Runnable() {
        @Override
        public void run() {
            edgeStep();
        }
    };

    /** How long a line of the view stays in the bar (owner decision 88, 1A: 4 s). */
    private static final long NOTICE_MILLIS = 4000L;

    /** The palette's cells for the walls: kinds of their own beside the corner model's. */
    private static final int KIND_HUMAN_WALL = 100, KIND_ORC_WALL = 101;

    /** Walls.HUMAN or Walls.ORC for a wall kind of the palette, else -1. */
    private static int wallRace(int k) {
        return (k == KIND_HUMAN_WALL) ? Walls.HUMAN : (k == KIND_ORC_WALL) ? Walls.ORC : -1;
    }
    private boolean inStrip;
    /** Erase: the tile the finger came down on, not yet cleared (a second finger may still make it a view gesture). */
    private boolean erasePending;
    private int eraseX, eraseY;
    /** A tile the page asked to be shown, or -1. */
    private int markX = -1, markY = -1;

    private float unit = 1f;
    /** Size of a tile on the screen and the map point at the canvas' top left, in pixels. */
    private float tilePx, originX, originY;
    private boolean placed;

    private final Paint image = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint fill = new Paint();
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    /** The rescue mark (owner decision 93, 2A): its dashed outline and its badge. */
    private final Paint rescueLine = new Paint(Paint.ANTI_ALIAS_FLAG), rescueBadge = new Paint(Paint.ANTI_ALIAS_FLAG);
    private float rescueDashFor = -1f;
    private final RectF rescueBox = new RectF();
    private final Rect src = new Rect();
    private final RectF dst = new RectF();

    // Gesture state.
    private boolean twoFingers, onMini;
    private float lastMidX, lastMidY, lastSpan;

    /** Told after every draw how long it took, in milliseconds; for measuring. May be null. */
    public interface FrameListener { void frame(float millis, float tilePx); }
    private FrameListener listener;

    public CanvasView(Context c, WorkingCopy work, File gameFolder, File dataDir) {
        super(c);
        this.work = work;
        map = work.model();
        this.gameFolder = gameFolder;
        this.dataDir = dataDir;
        stroke.setStyle(Paint.Style.STROKE);
        // The palette opens on the race of player 1, at its start location (owner decision 88, 5B).
        unitRace = UnitCatalog.ofSlotRace(map.slot(MapModel.FIELD_RACE, 0));
        unitType = UnitCatalog.types(unitRace, UnitCatalog.LAND_UNITS)[0];
        new Thread(new Runnable() {
            @Override
            public void run() {
                load();
                postInvalidate();
            }
        }, "editor-terrain").start();
        unitLoader = new Thread(new Runnable() {
            @Override
            public void run() {
                loadUnits();
            }
        }, "editor-units");
        unitLoader.start();
        post(settled);
    }

    public void setFrameListener(FrameListener l) { listener = l; }

    public void setHost(Host h) { host = h; }

    private void changed() {
        balanceStale = true;
        removeCallbacks(settled);
        postDelayed(settled, SETTLE_MILLIS);
        if (host != null) {
            host.changed();
        }
    }

    /** True when the terrain pictures could not be read; the caller may say so. */
    public boolean failed() { return failed; }

    /** Free the pictures; the view is not drawn again. */
    public void release() {
        released = true;
        removeCallbacks(settled);
        removeCallbacks(hold);
        removeCallbacks(fling);
        stopEdge();
        watch.close();
        if (unitLoader != null) {
            unitLoader.interrupt();
        }
        for (int i = 0; i < pictures.length(); ++i) {
            final UnitArt.Picture p = pictures.getAndSet(i, null);
            if (p != null) {
                p.recycle();
            }
        }
        final Bitmap a = sheet, b = overview;
        sheet = null;
        overview = null;
        if (a != null) {
            a.recycle();
        }
        if (b != null) {
            b.recycle();
        }
    }

    // ----- pictures -----

    private void load() {
        try {
            final int era = Math.max(0, Math.min(3, map.tileset()));
            final int[] table = Terrain.tileTable(text(new File(dataDir, "scripts/tilesets/wargus/" + ERAS[era][0] + ".lua")));
            final File dir = new File(gameFolder, "Data/Art/hd/bgs");
            final String json = text(new File(dir, "bgs_" + ERAS[era][1] + ".json"));
            if (table == null || json == null) {
                failed = true;
                return;
            }
            final JSONObject frames = new JSONObject(json.substring(json.indexOf('{'))).getJSONObject("frames");
            int count = 0;
            while (frames.has(ERAS[era][2] + "_" + count)) {
                ++count;
            }
            final BitmapFactory.Options o = new BitmapFactory.Options();
            o.inSampleSize = REDUCE;
            o.inPreferredConfig = Bitmap.Config.ARGB_8888;
            final Bitmap atlas = BitmapFactory.decodeFile(new File(dir, "bgs_" + ERAS[era][1] + ".png").getPath(), o);
            if (atlas == null || count == 0) {
                failed = true;
                return;
            }
            final int columns = 16, rows = (count + columns - 1) / columns;
            final Bitmap made = Bitmap.createBitmap(columns * CELL, rows * CELL, Bitmap.Config.ARGB_8888);
            final Canvas c = new Canvas(made);
            final Paint p = new Paint(Paint.FILTER_BITMAP_FLAG);
            final Rect from = new Rect();
            final Rect to = new Rect();
            for (int i = 0; i < count; ++i) {
                final JSONObject f = frames.getJSONObject(ERAS[era][2] + "_" + i).getJSONObject("frame");
                final int x = f.getInt("x"), y = f.getInt("y"), w = f.getInt("w"), h = f.getInt("h");
                // Only the reduced pixels that lie wholly inside the frame, so that a neighbour's edge is not drawn.
                from.set((x + REDUCE - 1) / REDUCE, (y + REDUCE - 1) / REDUCE, (x + w) / REDUCE, (y + h) / REDUCE);
                to.set((i % columns) * CELL, (i / columns) * CELL, (i % columns + 1) * CELL, (i / columns + 1) * CELL);
                c.drawBitmap(atlas, from, to, p);
            }
            atlas.recycle();
            final int[] cells = new int[table.length];
            for (int t = 0; t < table.length; ++t) {
                final int frame = table[t] - FIRST_MEGATILE;
                cells[t] = (frame >= 0 && frame < count) ? frame : -1;
            }
            sheetColumns = columns;
            cellOfTile = cells;
            sheet = made;
            terrain = new Terrain(table);

            final Bitmap whole = Bitmap.createBitmap(map.width() * OVERVIEW_TILE, map.height() * OVERVIEW_TILE,
                                                     Bitmap.Config.RGB_565);
            final Canvas wc = new Canvas(whole);
            wc.drawColor(0xFF000000);
            overview = whole;
            redraw(0, 0, map.width() - 1, map.height() - 1);
        } catch (Exception e) {
            failed = true;
        } catch (OutOfMemoryError e) {
            failed = true;
        }
    }

    /**
     * The loader of the pictures: reads the types asked for, one after the other, for the map's
     * tileset; with nothing asked it lets the open sheet go and waits. A type whose picture is
     * not found keeps its box.
     */
    private void loadUnits() {
        final String names = UnitArt.text(new File(dataDir, "hd_units_map.json"));
        final int era = Math.max(0, Math.min(3, map.tileset()));
        final UnitArt.Sheets sheets = new UnitArt.Sheets(gameFolder);
        int found = 0, missing = 0;
        long nanos = 0L;
        try {
            while (!released) {
                Integer next = wanted.poll();
                if (next == null) {
                    sheets.close();
                    if (found + missing > 0) {
                        android.util.Log.i("WargusHD", "LAUNCHER editor unit pictures read " + found + ", not found "
                            + missing + ", " + nanos / 1000000L + " ms");
                    }
                    next = wanted.take();
                }
                final int i = next;
                final long began = System.nanoTime();
                final UnitArt.Picture p = UnitArt.load(sheets, names, UnitFrames.TYPES[i], era);
                nanos += System.nanoTime() - began;
                if (p == null) {
                    ++missing;
                    asked.set(i, 2);
                    continue;
                }
                ++found;
                post(new Runnable() {
                    @Override
                    public void run() {
                        pictureRead(i, p);
                    }
                });
            }
        } catch (InterruptedException e) {
            // Released.
        } finally {
            sheets.close();
        }
    }

    /** A picture came from the loader (UI thread): held, and the oldest let go above the budget. */
    private void pictureRead(int i, UnitArt.Picture p) {
        if (released || pictures.get(i) != null) {
            p.recycle();
            return;
        }
        pictures.set(i, p);
        picturesHeld += p.bytes();
        lastDrawn[i] = frameNumber;
        while (picturesHeld > PICTURE_BUDGET) {
            int oldest = -1;
            for (int k = 0; k < lastDrawn.length; ++k) {
                if (pictures.get(k) != null && lastDrawn[k] < frameNumber
                    && (oldest < 0 || lastDrawn[k] < lastDrawn[oldest])) {
                    oldest = k;
                }
            }
            if (oldest < 0) {
                break;     // every picture held is on the screen
            }
            final UnitArt.Picture gone = pictures.getAndSet(oldest, null);
            picturesHeld -= gone.bytes();
            gone.recycle();
            asked.set(oldest, 0);
        }
        invalidate();
    }

    /** The still picture of a unit type, or null; one not yet held is asked for (UI thread). */
    private UnitArt.Picture picture(int type) {
        final int i = UnitFrames.indexOf(type);
        if (i < 0) {
            return null;
        }
        final UnitArt.Picture p = pictures.get(i);
        if (p != null) {
            lastDrawn[i] = frameNumber;
        } else if (!released && asked.compareAndSet(i, 0, 1)) {
            wanted.offer(i);
        }
        return p;
    }

    /** The tile as it is shown: as the stroke under the finger has left it, else the map's. */
    private int shown(int tx, int ty) {
        final Terrain.Stroke s = painting;
        return (s != null) ? s.tile(tx, ty) : map.tile(tx, ty);
    }

    /** Draw tiles of the whole-map picture again. */
    private void redraw(int x0, int y0, int x1, int y1) {
        final Bitmap whole = overview, tiles = sheet;
        if (whole == null || tiles == null) {
            return;
        }
        final Canvas wc = new Canvas(whole);
        final Paint p = new Paint(Paint.FILTER_BITMAP_FLAG);
        final Rect from = new Rect(), to = new Rect();
        final int columns = sheetColumns;
        for (int ty = Math.max(0, y0); ty <= Math.min(map.height() - 1, y1); ++ty) {
            for (int tx = Math.max(0, x0); tx <= Math.min(map.width() - 1, x1); ++tx) {
                final int cell = cell(shown(tx, ty));
                to.set(tx * OVERVIEW_TILE, ty * OVERVIEW_TILE, (tx + 1) * OVERVIEW_TILE, (ty + 1) * OVERVIEW_TILE);
                if (cell < 0) {
                    p.setColor(0xFF000000);
                    wc.drawRect(to, p);
                    continue;
                }
                from.set((cell % columns) * CELL, (cell / columns) * CELL,
                         (cell % columns + 1) * CELL, (cell / columns + 1) * CELL);
                wc.drawBitmap(tiles, from, to, p);
            }
        }
    }

    private int cell(int tile) {
        final int[] cells = cellOfTile;
        return (cells != null && tile < cells.length) ? cells[tile] : -1;
    }

    private static String text(File f) {
        if (!f.isFile() || f.length() > 8L * 1024 * 1024) {
            return null;
        }
        final byte[] all = new byte[(int) f.length()];
        try (InputStream in = new FileInputStream(f)) {
            int got = 0;
            while (got < all.length) {
                final int n = in.read(all, got, all.length - got);
                if (n < 0) {
                    return null;
                }
                got += n;
            }
            return new String(all, "ISO-8859-1");
        } catch (java.io.IOException e) {
            return null;
        }
    }

    // ----- the view on the map -----

    private float canvasLeft() { return TOOLS_W * unit; }
    private float canvasWidth() { return (PALETTE_X - TOOLS_W) * unit; }
    private float canvasHeight() { return getHeight(); }

    /** The size of a tile at which the whole map is in view. */
    private float wholeTile() {
        return Math.min(canvasWidth() / map.width(), canvasHeight() / map.height());
    }

    /** Keep the zoom within "whole map" and the largest tile, and the map within the canvas. */
    private void settle() {
        final float least = wholeTile(), most = Math.max(least, MAX_TILE * unit);
        tilePx = Math.max(least, Math.min(most, tilePx));
        final float mw = map.width() * tilePx, mh = map.height() * tilePx;
        // A map smaller than the canvas is centred; a larger one cannot leave an empty edge.
        originX = (mw <= canvasWidth()) ? -(canvasWidth() - mw) / 2f : Math.max(0f, Math.min(mw - canvasWidth(), originX));
        originY = (mh <= canvasHeight()) ? -(canvasHeight() - mh) / 2f : Math.max(0f, Math.min(mh - canvasHeight(), originY));
    }

    private void miniRect(RectF r) {
        final float side = mini * unit;
        final float w = side * Math.min(1f, (float) map.width() / map.height());
        final float h = side * Math.min(1f, (float) map.height() / map.width());
        final float x = (PALETTE_X + (PALETTE_W - mini) / 2f) * unit + (side - w) / 2f;
        final float y = getHeight() - (MINI_PAD + mini) * unit + (side - h) / 2f;
        r.set(x, y, x + w, y + h);
    }

    // The two columns as they are laid out for the view's height, logical units from the view's top.
    private float palY = PAL_Y, palSide = PAL_SIDE, palPitch = PAL_PITCH_Y, brushKeyY = BRUSH_KEY_Y, brushY = BRUSH_Y;
    private float mini = MINI, mirrorKeyY = MIRROR_KEY_Y, balanceY = BALANCE_Y;
    /** True where the Mirror cells stand two beside two, for want of height. */
    private boolean mirrorPaired;

    /**
     * Lay the columns out for the height there is. Where the palette, the brush cells and the
     * minimap of the design fit one above the other, they stand at the design's rects. On a
     * lower view the palette starts at the column's top, the rows close up to the cell and
     * its name, and the minimap takes what is left, down to MINI_LEAST; below that the
     * palette cells shrink too, down to CELL_LEAST. The Mirror cells move up as far as needed.
     */
    private void layout() {
        unit = getWidth() / CANVAS_W;
        final float high = (unit > 0f) ? getHeight() / unit : 0f;
        final float designEnd = BRUSH_Y + BRUSH_H + MINI_PAD;
        if (high <= 0f || high - MINI_PAD - MINI >= designEnd) {
            palY = PAL_Y;
            palSide = PAL_SIDE;
            palPitch = PAL_PITCH_Y;
            brushKeyY = BRUSH_KEY_Y;
            brushY = BRUSH_Y;
            mini = MINI;
        } else {
            // Top pad, three rows (cell, its name, a gap), the Brush word, the brush cells, a pad, the minimap, a pad.
            final float fixed = MINI_PAD + 3f * ROW_EXTRA + (BRUSH_Y - BRUSH_KEY_Y) + BRUSH_H + MINI_PAD + MINI_PAD;
            palSide = PAL_SIDE;
            mini = Math.min(MINI, high - fixed - 3f * palSide);
            if (mini < MINI_LEAST) {
                mini = MINI_LEAST;
                palSide = Math.max(CELL_LEAST, Math.min(PAL_SIDE, (high - fixed - mini) / 3f));
            }
            palY = MINI_PAD;
            palPitch = palSide + ROW_EXTRA;
            brushKeyY = palY + 3f * palPitch;
            brushY = brushKeyY + (BRUSH_Y - BRUSH_KEY_Y);
        }
        // The Balance tile at the column's foot; the Mirror cells above it, under the switches.
        final float mirrorHigh = (MIRROR_Y - MIRROR_KEY_Y) + 3f * MIRROR_PITCH + MIRROR_H;
        final float pairedHigh = (MIRROR_Y - MIRROR_KEY_Y) + MIRROR_PITCH + MIRROR_H;
        final float least = SHOW_Y + 2f * MIRROR_PITCH + MIRROR_H + MINI_PAD;
        if (high <= 0f) {
            balanceY = BALANCE_Y;
            mirrorKeyY = MIRROR_KEY_Y;
            mirrorPaired = false;
            return;
        }
        balanceY = high - MINI_PAD - BALANCE_SIDE;
        final float foot = balanceY - BALANCE_GAP;
        mirrorPaired = foot - mirrorHigh < least;
        mirrorKeyY = Math.max(least, Math.min(MIRROR_KEY_Y, foot - (mirrorPaired ? pairedHigh : mirrorHigh)));
    }

    @Override
    protected void onDraw(Canvas c) {
        final long began = System.nanoTime();
        ++frameNumber;
        layout();
        if (!placed && getWidth() > 0) {
            placed = true;
            tilePx = wholeTile();
        }
        settle();
        c.drawColor(GROUND);

        final Bitmap tiles = sheet, whole = overview;
        final float left = canvasLeft();
        c.save();
        c.clipRect(left, 0f, left + canvasWidth(), canvasHeight());
        if (whole != null && tilePx <= OVERVIEW_TILE * 1.5f) {
            dst.set(left - originX, -originY, left - originX + map.width() * tilePx, -originY + map.height() * tilePx);
            c.drawBitmap(whole, null, dst, image);
        } else if (tiles != null) {
            final int x0 = Math.max(0, (int) Math.floor(originX / tilePx));
            final int y0 = Math.max(0, (int) Math.floor(originY / tilePx));
            final int x1 = Math.min(map.width() - 1, (int) Math.floor((originX + canvasWidth()) / tilePx));
            final int y1 = Math.min(map.height() - 1, (int) Math.floor((originY + canvasHeight()) / tilePx));
            final int columns = sheetColumns;
            for (int ty = y0; ty <= y1; ++ty) {
                // Whole pixels, each tile ending where the next begins, so that no seam shows.
                final float top = Math.round(ty * tilePx - originY), bottom = Math.round((ty + 1) * tilePx - originY);
                for (int tx = x0; tx <= x1; ++tx) {
                    final int cell = cell(shown(tx, ty));
                    if (cell < 0) {
                        continue;
                    }
                    src.set((cell % columns) * CELL, (cell / columns) * CELL,
                            (cell % columns + 1) * CELL, (cell / columns + 1) * CELL);
                    dst.set(left + Math.round(tx * tilePx - originX), top,
                            left + Math.round((tx + 1) * tilePx - originX), bottom);
                    c.drawBitmap(tiles, src, dst, image);
                }
            }
        }
        if (painting != null) {
            // The brush under the finger: the corners it sets, each with half a tile round it.
            // A wall brush covers whole tiles.
            final int size = Terrain.BRUSHES[brush];
            final boolean wall = wallRace(kind) >= 0;
            final float bx = wall ? Terrain.wallStart(paintX, size) : Terrain.brushStart(paintX, size) - 0.5f;
            final float by = wall ? Terrain.wallStart(paintY, size) : Terrain.brushStart(paintY, size) - 0.5f;
            stroke.setColor(VIEW_BOX);
            stroke.setStrokeWidth(Math.max(2f, 4f * unit));
            c.drawRect(left + bx * tilePx - originX, by * tilePx - originY,
                       left + (bx + size) * tilePx - originX, (by + size) * tilePx - originY, stroke);
        }
        drawShows(c, left);
        drawPending(c, left);
        drawUnits(c, left);
        c.restore();
        drawStrip(c);

        // The columns at the sides: the tools and the Mirror cells, the palette and the minimap.
        fill.setColor(COLUMN);
        c.drawRect(0f, 0f, left, getHeight(), fill);
        c.drawRect(PALETTE_X * unit, 0f, getWidth(), getHeight(), fill);
        stroke.setColor(LINE);
        stroke.setStrokeWidth(Math.max(1f, 2f * unit));
        c.drawLine(left, 0f, left, getHeight(), stroke);
        c.drawLine(PALETTE_X * unit, 0f, PALETTE_X * unit, getHeight(), stroke);

        if (terrainPalette()) {
            drawPalette(c, tiles);
        } else {
            drawUnitPalette(c);
        }
        drawTools(c);
        drawShowCells(c);
        drawMirror(c);
        drawBalance(c);

        // The minimap and the part of the map in view.
        miniRect(dst);
        if (whole != null) {
            c.drawBitmap(whole, null, dst, image);
        }
        c.drawRect(dst, stroke);
        final float mx = dst.left, my = dst.top, mt = dst.width() / map.width();
        for (int i = 0; i < map.unitCount(); ++i) {
            final MapModel.Unit u = map.unit(i);
            final int size = MapRules.footprint(u.type);
            // A start location is one tile; it is shown as large as a mine so that it can be seen.
            final float grow = MapRules.isStart(u.type) ? mt : 0f;
            fill.setColor(unitColour(u));
            c.drawRect(mx + u.x * mt - grow, my + u.y * mt - grow, mx + (u.x + size) * mt + grow,
                       my + (u.y + size) * mt + grow, fill);
        }
        miniRect(dst);
        final float sx = dst.width() / (map.width() * tilePx), sy = dst.height() / (map.height() * tilePx);
        final float vx = dst.left + Math.max(0f, originX) * sx, vy = dst.top + Math.max(0f, originY) * sy;
        final float vw = Math.min(canvasWidth(), map.width() * tilePx) * sx;
        final float vh = Math.min(canvasHeight(), map.height() * tilePx) * sy;
        stroke.setColor(VIEW_BOX);
        c.drawRect(vx, vy, vx + vw, vy + vh, stroke);

        if (host != null) {
            host.over(c);
        }
        if (listener != null) {
            listener.frame((System.nanoTime() - began) / 1e6f, tilePx);
        }
    }

    // ----- the palette column: terrain kinds and brush sizes -----

    /**
     * The kinds in the order of the palette's cells, light and dark side by side (owner decision
     * 88, 2A): Water, Deep water, Coast, Dark coast / Grass, Dark grass, Forest, Rocks / the walls.
     */
    private static final int[] CELL_KIND = { Terrain.WATER, Terrain.DEEP_WATER, Terrain.COAST, Terrain.DARK_COAST,
        Terrain.GRASS, Terrain.DARK_GRASS, Terrain.FOREST, Terrain.ROCKS, KIND_HUMAN_WALL, KIND_ORC_WALL };
    /** A solid tile of each cell, for its picture. */
    private static final int[] CELL_TILE = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0 };
    // Rects of the palette (owner board 2A: cells 156 in four columns at a pitch of 172, rows of 232),
    // logical units; y from the view's top (the screen's y less the bar's 168).
    private static final int PAL_COLUMNS = 4;
    private static final float PAL_X = 3144f, PAL_Y = 348f - 168f, PAL_SIDE = 156f, PAL_PITCH_X = 172f, PAL_PITCH_Y = 232f;
    private static final float BRUSH_X = 3150f, BRUSH_Y = 1206f - 168f, BRUSH_W = 160.5f, BRUSH_H = 96f, BRUSH_PITCH = 166.5f;
    private static final int DIM = 0xB00E0B08, CELL_FILL = 0x10FFFFFF, CELL_CHOSEN = 0x36F0D250;
    /** The cells' names, in the launcher's own words. */
    private static final String[] CELL_NAME = { "Water", "Deep water", "Coast", "Dark coast", "Grass", "Dark grass",
        "Forest", "Rocks", "Human wall", "Orc wall" };
    private static final float NAME_GAP = 6f, NAME_H = 48f, CAP_NAME = 24f;
    /** A terrain cell's name: as wide as the pitch less 8, cap 21 (2A). */
    private static final float TER_NAME_IN = 4f, TER_NAME_W = PAL_PITCH_X - 8f, CAP_TER_NAME = 21f;
    private static final float BRUSH_KEY_X = 3144f, BRUSH_KEY_Y = 1152f - 168f, BRUSH_KEY_H = 40f;
    private static final float CAP_KEY = 30f, CAP_CELL = 36f;
    /** What a palette row takes beside its cell: the name's line and the gaps. */
    private static final float ROW_EXTRA = PAL_PITCH_Y - PAL_SIDE;
    /** The least sides of the minimap and of a palette cell on a low view. */
    private static final float MINI_LEAST = 384f, CELL_LEAST = 96f;
    // The Mirror cells of the tool column: Off, 2, 4, 8.
    private static final String[] MIRROR_NAME = { "Off", "2", "4", "8" };
    private static final float MIRROR_X = 24f, MIRROR_KEY_Y = 1260f - 168f, MIRROR_KEY_H = 60f;
    private static final float MIRROR_Y = 1332f - 168f, MIRROR_W = 216f, MIRROR_H = 96f, MIRROR_PITCH = 108f;
    /** Two Mirror cells side by side: the width of one and the step to the next. */
    private static final float MIRROR_PAIR_W = 102f, MIRROR_PAIR_PITCH = 114f;
    // The Balance tile at the tool column's foot.
    private static final float BALANCE_SIDE = 216f, BALANCE_Y = 2160f - 168f, BALANCE_GAP = 12f;
    private static final float BALANCE_KEY_Y = 12f, BALANCE_KEY_H = 60f, BALANCE_VALUE_Y = 84f, BALANCE_VALUE_H = 108f;
    private static final float CAP_BALANCE = 60f;

    private void paletteCell(int i, RectF r) {
        final float x = (PAL_X + (i % PAL_COLUMNS) * PAL_PITCH_X + (PAL_SIDE - palSide) / 2f) * unit;
        final float y = (palY + (i / PAL_COLUMNS) * palPitch) * unit;
        r.set(x, y, x + palSide * unit, y + palSide * unit);
    }

    private void brushCell(int i, RectF r) {
        final float x = (BRUSH_X + i * BRUSH_PITCH) * unit, y = brushY * unit;
        r.set(x, y, x + BRUSH_W * unit, y + BRUSH_H * unit);
    }

    private void drawPalette(Canvas c, Bitmap tiles) {
        final int columns = sheetColumns;
        for (int i = 0; i < CELL_KIND.length; ++i) {
            paletteCell(i, dst);
            final int cell = cell(CELL_TILE[i]);
            if (tiles != null && cell >= 0) {
                src.set((cell % columns) * CELL, (cell / columns) * CELL,
                        (cell % columns + 1) * CELL, (cell / columns + 1) * CELL);
                c.drawBitmap(tiles, src, dst, image);
            }
            if (CELL_KIND[i] < 0) {
                fill.setColor(DIM);
                c.drawRect(dst, fill);
            }
            final boolean chosen = CELL_KIND[i] == kind;
            stroke.setColor(chosen ? VIEW_BOX : LINE);
            stroke.setStrokeWidth(Math.max(1f, (chosen ? 6f : 2f) * unit));
            c.drawRect(dst, stroke);
            if (host != null) {
                host.text(c, host.word(CELL_NAME[i]), PAL_X - TER_NAME_IN + (i % PAL_COLUMNS) * PAL_PITCH_X,
                          palY + (i / PAL_COLUMNS) * palPitch + palSide + NAME_GAP,
                          TER_NAME_W, NAME_H, CAP_TER_NAME, chosen ? VIEW_BOX : (CELL_KIND[i] < 0) ? INK_MUTED : INK,
                          true);
            }
        }
        if (host != null) {
            host.text(c, host.word("Brush"), BRUSH_KEY_X, brushKeyY, PALETTE_W, BRUSH_KEY_H, CAP_KEY, INK_MUTED, false);
        }
        for (int i = 0; i < Terrain.BRUSHES.length; ++i) {
            brushCell(i, dst);
            fill.setColor((i == brush) ? CELL_CHOSEN : CELL_FILL);
            c.drawRect(dst, fill);
            stroke.setColor((i == brush) ? VIEW_BOX : LINE);
            stroke.setStrokeWidth(Math.max(1f, ((i == brush) ? 6f : 2f) * unit));
            c.drawRect(dst, stroke);
            if (host != null) {
                // The brush's size in corners across.
                host.text(c, String.valueOf(Terrain.BRUSHES[i]), BRUSH_X + i * BRUSH_PITCH, brushY, BRUSH_W, BRUSH_H,
                          CAP_CELL, (i == brush) ? VIEW_BOX : INK, true);
            } else {
                final float half = Terrain.BRUSHES[i] * 7f * unit;
                fill.setColor((i == brush) ? VIEW_BOX : LINE);
                c.drawRect(dst.centerX() - half, dst.centerY() - half, dst.centerX() + half, dst.centerY() + half, fill);
            }
        }
        stroke.setColor(LINE);
        stroke.setStrokeWidth(Math.max(1f, 2f * unit));
    }

    private float mirrorX(int i) { return MIRROR_X + (mirrorPaired ? (i % 2) * MIRROR_PAIR_PITCH : 0f); }
    private float mirrorY(int i) {
        return mirrorKeyY + (MIRROR_Y - MIRROR_KEY_Y) + (mirrorPaired ? i / 2 : i) * MIRROR_PITCH;
    }
    private float mirrorW() { return mirrorPaired ? MIRROR_PAIR_W : MIRROR_W; }

    private void mirrorCell(int i, RectF r) {
        final float x = mirrorX(i) * unit, y = mirrorY(i) * unit;
        r.set(x, y, x + mirrorW() * unit, y + MIRROR_H * unit);
    }

    private void balanceCell(RectF r) {
        final float x = MIRROR_X * unit, y = balanceY * unit;
        r.set(x, y, x + BALANCE_SIDE * unit, y + BALANCE_SIDE * unit);
    }

    /** The Balance tile: the fairness of the map as it stands, muted while a change is not yet measured. */
    private void drawBalance(Canvas c) {
        balanceCell(dst);
        cell(c, dst, false);
        if (host != null) {
            host.text(c, host.word("Balance"), MIRROR_X + 6f, balanceY + BALANCE_KEY_Y, BALANCE_SIDE - 12f, BALANCE_KEY_H,
                      CAP_KEY, INK_MUTED, true);
            host.text(c, BalanceWatch.shown((balance == null) ? Double.NaN : balance.score), MIRROR_X + 6f,
                      balanceY + BALANCE_VALUE_Y, BALANCE_SIDE - 12f, BALANCE_VALUE_H, CAP_BALANCE,
                      balanceStale ? INK_MUTED : INK, true);
        }
        stroke.setColor(LINE);
        stroke.setStrokeWidth(Math.max(1f, 2f * unit));
    }

    private boolean balanceTap(float x, float y) {
        balanceCell(dst);
        if (!dst.contains(x, y)) {
            return false;
        }
        if (host != null && balance != null && balance.measure != null) {
            host.click();
            host.balance(balance);
        }
        return true;
    }

    private void drawMirror(Canvas c) {
        if (host != null) {
            host.text(c, host.word("Mirror"), MIRROR_X, mirrorKeyY, MIRROR_W, MIRROR_KEY_H, CAP_KEY, INK_MUTED, true);
        }
        for (int i = 0; i < MIRROR_NAME.length; ++i) {
            mirrorCell(i, dst);
            fill.setColor((i == mirror) ? CELL_CHOSEN : CELL_FILL);
            c.drawRect(dst, fill);
            stroke.setColor((i == mirror) ? VIEW_BOX : LINE);
            stroke.setStrokeWidth(Math.max(1f, ((i == mirror) ? 6f : 2f) * unit));
            c.drawRect(dst, stroke);
            if (host != null) {
                host.text(c, (i == 0) ? host.term("option_common_off", host.word(MIRROR_NAME[i])) : MIRROR_NAME[i], mirrorX(i), mirrorY(i), mirrorW(),
                          MIRROR_H, mirrorPaired ? CAP_KEY : CAP_CELL, (i == mirror) ? VIEW_BOX : INK, true);
            }
        }
        stroke.setColor(LINE);
        stroke.setStrokeWidth(Math.max(1f, 2f * unit));
    }

    private void mirrorTap(float x, float y) {
        for (int i = 0; i < MIRROR_NAME.length; ++i) {
            mirrorCell(i, dst);
            if (dst.contains(x, y) && mirror != i) {
                mirror = i;
                if (host != null) {
                    host.click();
                }
                break;
            }
        }
        invalidate();
    }

    private void paletteTap(float x, float y) {
        boolean chose = false;
        for (int i = 0; i < CELL_KIND.length; ++i) {
            paletteCell(i, dst);
            if (CELL_KIND[i] >= 0 && dst.contains(x, y) && kind != CELL_KIND[i]) {
                kind = CELL_KIND[i];
                chose = true;
            }
        }
        for (int i = 0; i < Terrain.BRUSHES.length; ++i) {
            brushCell(i, dst);
            if (dst.contains(x, y) && brush != i) {
                brush = i;
                chose = true;
            }
        }
        if (chose && host != null) {
            host.click();
        }
        invalidate();
    }

    // ----- the terrain tool, undo, redo, save -----

    private void dab(float x, float y) {
        final float mx = (x - canvasLeft() + originX) / tilePx, my = (y + originY) / tilePx;
        final int size = Terrain.BRUSHES[brush];
        // Along the finger's way, so that a fast drag leaves no gaps.
        final int steps = Math.max(1, (int) Math.ceil(Math.hypot(mx - paintX, my - paintY) * 2f));
        for (int i = 1; i <= steps; ++i) {
            final float px = paintX + (mx - paintX) * i / steps, py = paintY + (my - paintY) * i / steps;
            final int race = wallRace(kind);
            if ((race >= 0) ? painting.wall(px, py, race, size, mirror) : painting.dab(px, py, kind, size, mirror)) {
                // The place under the finger and its images on the other sides of the map.
                final float ix = map.width() - px, iy = map.height() - py;
                redrawAround(px, py, size);
                if (mirror >= Terrain.MIRROR_2) {
                    redrawAround(ix, py, size);
                }
                if (mirror >= Terrain.MIRROR_4) {
                    redrawAround(px, iy, size);
                    redrawAround(ix, iy, size);
                }
                if (mirror >= Terrain.MIRROR_8 && map.width() == map.height()) {
                    redrawAround(py, px, size);
                    redrawAround(iy, px, size);
                    redrawAround(py, ix, size);
                    redrawAround(iy, ix, size);
                }
            }
        }
        paintX = mx;
        paintY = my;
        invalidate();
    }

    private void redrawAround(float px, float py, int size) {
        redraw((int) px - size - 4, (int) py - size - 4, (int) px + size + 4, (int) py + size + 4);
    }

    /** End the stroke under the finger: as one step of the map, or taken back. */
    private void endStroke(boolean keep) {
        final Terrain.Stroke s = painting;
        if (s == null) {
            return;
        }
        painting = null;
        if (keep) {
            try {
                work.apply(s.step());
            } catch (java.io.IOException e) {
                trouble = true;
            }
        }
        if (s.maxX() >= 0) {
            redraw(s.minX(), s.minY(), s.maxX(), s.maxY());
        }
        invalidate();
        changed();
    }

    // ----- units: the tools, the palette, the ghost, the strip of the chosen unit -----

    private static final String[] TOOL_NAME = { "Terrain", "Units", "Select", "Erase", "Fill", "Pick" };
    private static final float TOOL_Y = 24f;
    // The switches of what is laid over the map, under the tool cells.
    private static final String[] SHOW_NAME = { "Grid", "Passable", "Buildable" };
    private static final float SHOW_Y = 864f - 168f, SHOW_RULE_UP = 18f;
    private static final int PASS_LAND = 0x5030C050, PASS_WATER = 0x503070E0, PASS_NONE = 0x60D03030;
    private static final int GRID_LINE = 0x70000000;
    /** Below this many pixels a tile the grid is not drawn. */
    private static final float GRID_LEAST = 10f;
    /** A finger that moved further than this, in logical units, did not tap. */
    private static final float TAP_SLOP = 36f;

    /** True while the palette column shows the terrain kinds: with Terrain, Fill and Pick. */
    private boolean terrainPalette() { return tool == TOOL_TERRAIN || tool == TOOL_FILL || tool == TOOL_PICK; }

    private void showCell(int i, RectF r) {
        final float x = MIRROR_X * unit, y = (SHOW_Y + i * MIRROR_PITCH) * unit;
        r.set(x, y, x + MIRROR_W * unit, y + MIRROR_H * unit);
    }

    private void drawShowCells(Canvas c) {
        stroke.setColor(LINE);
        stroke.setStrokeWidth(Math.max(1f, 2f * unit));
        c.drawLine(MIRROR_X * unit, (SHOW_Y - SHOW_RULE_UP) * unit, (MIRROR_X + MIRROR_W) * unit,
                   (SHOW_Y - SHOW_RULE_UP) * unit, stroke);
        for (int i = 0; i < SHOW_NAME.length; ++i) {
            showCell(i, dst);
            cell(c, dst, shows[i]);
            if (host != null) {
                host.text(c, host.word(SHOW_NAME[i]), MIRROR_X + 6f, SHOW_Y + i * MIRROR_PITCH, MIRROR_W - 12f, MIRROR_H,
                          CAP_NAME, shows[i] ? VIEW_BOX : INK, true);
            }
        }
        stroke.setColor(LINE);
        stroke.setStrokeWidth(Math.max(1f, 2f * unit));
    }

    private boolean showTap(float x, float y) {
        for (int i = 0; i < SHOW_NAME.length; ++i) {
            showCell(i, dst);
            if (dst.contains(x, y)) {
                shows[i] = !shows[i];
                if (host != null) {
                    host.click();
                }
                invalidate();
                return true;
            }
        }
        return false;
    }

    /** The colour a tile gets from the ground classes that are switched on; 0 for none. */
    private int showColour(int tile) {
        // The placing rule decides (MapRules.passClass / buildableOn): the engine's flags while its export is installed.
        if (shows[SHOW_BUILDABLE]) {
            return MapRules.buildableOn(map, tile) ? PASS_LAND : PASS_NONE;
        }
        if (shows[SHOW_PASSABLE]) {
            final int pass = MapRules.passClass(map, tile);
            return (pass == MapRules.PASS_LAND) ? PASS_LAND : (pass == MapRules.PASS_WATER) ? PASS_WATER : PASS_NONE;
        }
        return 0;
    }

    /** The ground classes and the grid over the part of the map in view. */
    private void drawShows(Canvas c, float left) {
        if (tilePx <= 0f || !(shows[SHOW_GRID] || shows[SHOW_PASSABLE] || shows[SHOW_BUILDABLE])) {
            return;
        }
        final int x0 = Math.max(0, (int) Math.floor(originX / tilePx));
        final int y0 = Math.max(0, (int) Math.floor(originY / tilePx));
        final int x1 = Math.min(map.width() - 1, (int) Math.floor((originX + canvasWidth()) / tilePx));
        final int y1 = Math.min(map.height() - 1, (int) Math.floor((originY + canvasHeight()) / tilePx));
        if (shows[SHOW_PASSABLE] || shows[SHOW_BUILDABLE]) {
            for (int ty = y0; ty <= y1; ++ty) {
                // Tiles of one class side by side are one rect.
                int from = x0, colour = showColour(shown(x0, ty));
                for (int tx = x0 + 1; tx <= x1 + 1; ++tx) {
                    final int next = (tx <= x1) ? showColour(shown(tx, ty)) : ~colour;
                    if (next != colour) {
                        fill.setColor(colour);
                        c.drawRect(left + from * tilePx - originX, ty * tilePx - originY,
                                   left + tx * tilePx - originX, (ty + 1) * tilePx - originY, fill);
                        from = tx;
                        colour = next;
                    }
                }
            }
        }
        if (shows[SHOW_GRID] && tilePx >= GRID_LEAST) {
            fill.setColor(GRID_LINE);
            final float thick = Math.max(1f, 2f * unit);
            final float top = y0 * tilePx - originY, bottom = (y1 + 1) * tilePx - originY;
            final float first = left + x0 * tilePx - originX, last = left + (x1 + 1) * tilePx - originX;
            for (int tx = x0; tx <= x1 + 1; ++tx) {
                final float x = Math.round(left + tx * tilePx - originX);
                c.drawRect(x, top, x + thick, bottom, fill);
            }
            for (int ty = y0; ty <= y1 + 1; ++ty) {
                final float y = Math.round(ty * tilePx - originY);
                c.drawRect(first, y, last, y + thick, fill);
            }
        }
    }

    /** Fill: the area of the kind under the finger takes the chosen kind, as one step. */
    private void fillAt(float x, float y) {
        final Terrain set = terrain;
        // A fill with a wall is not designed (EDITOR_STATUS.md, Session 2026-10-19 b): nothing is done.
        if (set == null || tilePx <= 0f || wallRace(kind) >= 0) {
            return;
        }
        endStroke(true);
        final Terrain.Stroke s = set.begin(map, System.nanoTime());
        if (s.fill((x - canvasLeft() + originX) / tilePx, (y + originY) / tilePx, kind)) {
            final MapModel.TileStep step = s.step();
            if (step != null && step.count() > FILL_ASK) {
                // Above 2 000 tiles the area is shown and the strip asks (owner decision 88, 3B).
                pendingFill = s;
                pendingStep = step;
                pendingMark = new boolean[map.width() * map.height()];
                for (int ty = Math.max(0, s.minY()); ty <= s.maxY(); ++ty) {
                    for (int tx = Math.max(0, s.minX()); tx <= s.maxX(); ++tx) {
                        pendingMark[ty * map.width() + tx] = s.tile(tx, ty) != map.tile(tx, ty);
                    }
                }
                chosen = new int[0];
                invalidate();
            } else {
                painting = s;
                endStroke(true);
            }
            if (host != null) {
                host.click();
            }
        }
    }

    // ----- a fill above 2 000 tiles waits for its answer (owner decision 88, 3B) -----

    /** EDITOR_DESIGN.md 4.2: Fill asks above this many tiles. */
    private static final int FILL_ASK = 2000;
    private static final int PENDING_TINT = 0x61FF7A3D, PENDING_LINE = 0xFFFF7A3D;
    private static final float FILL_CANCEL_X = 2088f, FILL_OK_X = 2748f, FILL_BUTTON_W = 324f, FILL_TEXT_X = 330f;
    private static final float CAP_FILL = 42f;
    private Terrain.Stroke pendingFill;
    private MapModel.TileStep pendingStep;
    private boolean[] pendingMark;
    /** A finger came down on the canvas while a fill waits: lifted without a second finger, it drops the fill. */
    private boolean dropTap;

    /** Drop a fill that waits for its answer; false when none waits. For Android Back. */
    public boolean dropPending() {
        if (pendingFill == null) {
            return false;
        }
        pendingFill = null;
        pendingStep = null;
        pendingMark = null;
        invalidate();
        return true;
    }

    private void drawPending(Canvas c, float left) {
        final boolean[] mark = pendingMark;
        if (mark == null || tilePx <= 0f) {
            return;
        }
        final int w = map.width(), h = map.height();
        final int x0 = Math.max(0, (int) Math.floor(originX / tilePx));
        final int y0 = Math.max(0, (int) Math.floor(originY / tilePx));
        final int x1 = Math.min(w - 1, (int) Math.floor((originX + canvasWidth()) / tilePx));
        final int y1 = Math.min(h - 1, (int) Math.floor((originY + canvasHeight()) / tilePx));
        fill.setColor(PENDING_TINT);
        for (int ty = y0; ty <= y1; ++ty) {
            for (int tx = x0; tx <= x1; ++tx) {
                if (mark[ty * w + tx]) {
                    c.drawRect(left + tx * tilePx - originX, ty * tilePx - originY,
                               left + (tx + 1) * tilePx - originX, (ty + 1) * tilePx - originY, fill);
                }
            }
        }
        // The outline: the edges of the area, where a marked tile meets one that is not.
        fill.setColor(PENDING_LINE);
        final float t = Math.max(2f, 4f * unit);
        for (int ty = y0; ty <= y1; ++ty) {
            for (int tx = x0; tx <= x1; ++tx) {
                if (!mark[ty * w + tx]) {
                    continue;
                }
                final float ax = left + tx * tilePx - originX, ay = ty * tilePx - originY;
                if (ty == 0 || !mark[(ty - 1) * w + tx]) {
                    c.drawRect(ax, ay, ax + tilePx, ay + t, fill);
                }
                if (ty == h - 1 || !mark[(ty + 1) * w + tx]) {
                    c.drawRect(ax, ay + tilePx - t, ax + tilePx, ay + tilePx, fill);
                }
                if (tx == 0 || !mark[ty * w + tx - 1]) {
                    c.drawRect(ax, ay, ax + t, ay + tilePx, fill);
                }
                if (tx == w - 1 || !mark[ty * w + tx + 1]) {
                    c.drawRect(ax + tilePx - t, ay, ax + tilePx, ay + tilePx, fill);
                }
            }
        }
    }

    private String kindName(int k) {
        for (int i = 0; i < CELL_KIND.length; ++i) {
            if (CELL_KIND[i] == k) {
                return host.word(CELL_NAME[i]);
            }
        }
        return "";
    }

    private void drawFillStrip(Canvas c, float y) {
        stripBack(c, y);
        host.text(c, host.word("Fill {0} tiles with {1}?").replace("{0}", String.valueOf(pendingStep.count()))
                      .replace("{1}", kindName(kind)), FILL_TEXT_X, y, FILL_CANCEL_X - FILL_TEXT_X - 24f, STRIP_H, CAP_FILL,
                  VIEW_BOX, false);
        final float cy = y + STRIP_CELL_Y;
        stripCell(c, FILL_CANCEL_X, cy, FILL_BUTTON_W, host.term("common_cancel", "Cancel"));
        dst.set(FILL_OK_X * unit, cy * unit, (FILL_OK_X + FILL_BUTTON_W) * unit, (cy + STRIP_CELL_H) * unit);
        cell(c, dst, true);
        host.text(c, host.word("Fill"), FILL_OK_X, cy, FILL_BUTTON_W, STRIP_CELL_H, CAP_CELL, VIEW_BOX, true);
    }

    private void fillStripTap(float x) {
        if (x >= FILL_OK_X && x < FILL_OK_X + FILL_BUTTON_W) {
            final MapModel.TileStep step = pendingStep;
            final Terrain.Stroke s = pendingFill;
            dropPending();
            if (step != null && step.count() > 0) {
                apply(step);
                redraw(s.minX(), s.minY(), s.maxX(), s.maxY());
            }
            if (host != null) {
                host.click();
            }
        } else if (x >= FILL_CANCEL_X && x < FILL_CANCEL_X + FILL_BUTTON_W) {
            dropPending();
            if (host != null) {
                host.click();
            }
        }
    }

    /** The palette's kind a tile shows at a point of it (a wall by its race), or -1 for a kind the palette has not. */
    private static int paletteKindAt(MapModel map, int tx, int ty, float fx, float fy) {
        final int tile = map.tile(tx, ty);
        final int race = Walls.raceOf(tile);
        final int found = (race == Walls.HUMAN) ? KIND_HUMAN_WALL : (race == Walls.ORC) ? KIND_ORC_WALL
            : Terrain.kindAt(tile, fx, fy);
        for (int k : CELL_KIND) {
            if (k >= 0 && k == found) {
                return found;
            }
        }
        return -1;
    }

    private int paletteKindAt(int tx, int ty, float fx, float fy) { return paletteKindAt(map, tx, ty, fx, fy); }

    /** Pick: the unit under the finger, or else the terrain kind there, goes into the palette with its tool. */
    private void pickAt(float x, float y) {
        if (tilePx <= 0f) {
            return;
        }
        final float mx = (x - canvasLeft() + originX) / tilePx, my = (y + originY) / tilePx;
        final int tx = (int) Math.floor(mx), ty = (int) Math.floor(my);
        if (tx < 0 || ty < 0 || tx >= map.width() || ty >= map.height()) {
            return;
        }
        final int at = MapRules.unitAt(map, tx, ty);
        final int unitIs = (at >= 0) ? map.unit(at).type : -1;
        if (unitIs >= 0 && UnitCatalog.has(unitIs)) {
            unitType = unitIs;
            unitRace = UnitCatalog.race(unitIs);
            reveal(unitIs);
            if (map.unit(at).owner < MapRules.PLAYERS) {
                owner = map.unit(at).owner;
            }
            tool = TOOL_UNITS;
        } else {
            final int found = paletteKindAt(tx, ty, mx - tx, my - ty);
            if (found < 0) {
                return;     // a kind the palette has not: nothing is taken
            }
            kind = found;
            tool = TOOL_TERRAIN;
        }
        chosen = new int[0];
        if (host != null) {
            host.click();
        }
        invalidate();
    }
    /** The tool's own colours of a gold mine and an oil patch, and of a unit of a kind not known here. */
    private static final int MINE = 0xFFFAD63C, PATCH = 0xFF101010, OTHER = 0xFF8C8C8C;
    /** The share of a palette cell's side a unit's picture takes. */
    private static final float PICTURE_ROOM = 0.86f;
    private static final int GHOST_OK = 0x8030C050, GHOST_NO = 0x80D03030, DARK_INK = 0xFF101010;
    private static final float OWNER_KEY_H = 40f, OWNER_PITCH = 108f;
    // The strip of the chosen unit at the canvas' foot; x in logical units, y from the strip's top.
    private static final float STRIP_X = 288f, STRIP_W = 2808f, STRIP_H = 120f, STRIP_UP = 144f;
    private static final float MINUS_X = 1740f, AMOUNT_X = 1872f, AMOUNT_W = 360f, PLUS_X = 2244f, STEP_W = 120f;
    private static final float REMOVE_X = 2748f, REMOVE_W = 324f, STRIP_CELL_Y = 12f, STRIP_CELL_H = 96f;
    // The AI word cell (Passive / Active, ORIG60 item 3): the design is silent, so it takes the span of
    // the resource's - amount + cells in the same strip cell style; a tap switches the word.
    private static final float AI_X = MINUS_X, AI_W = PLUS_X + STEP_W - MINUS_X;

    private void toolCell(int i, RectF r) {
        final float x = MIRROR_X * unit, y = (TOOL_Y + i * MIRROR_PITCH) * unit;
        r.set(x, y, x + MIRROR_W * unit, y + MIRROR_H * unit);
    }

    private void cell(Canvas c, RectF r, boolean chosen) {
        fill.setColor(chosen ? CELL_CHOSEN : CELL_FILL);
        c.drawRect(r, fill);
        stroke.setColor(chosen ? VIEW_BOX : LINE);
        stroke.setStrokeWidth(Math.max(1f, (chosen ? 6f : 2f) * unit));
        c.drawRect(r, stroke);
    }

    private void drawTools(Canvas c) {
        for (int i = 0; i < TOOL_NAME.length; ++i) {
            toolCell(i, dst);
            cell(c, dst, i == tool);
            if (host != null) {
                host.text(c, host.word(TOOL_NAME[i]), MIRROR_X, TOOL_Y + i * MIRROR_PITCH, MIRROR_W, MIRROR_H,
                          CAP_KEY, (i == tool) ? VIEW_BOX : INK, true);
            }
        }
        stroke.setColor(LINE);
        stroke.setStrokeWidth(Math.max(1f, 2f * unit));
    }

    private boolean toolTap(float x, float y) {
        for (int i = 0; i < TOOL_NAME.length; ++i) {
            toolCell(i, dst);
            if (dst.contains(x, y)) {
                if (tool != i) {
                    tool = i;
                    chosen = new int[0];
                    if (host != null) {
                        host.click();
                    }
                }
                invalidate();
                return true;
            }
        }
        return false;
    }

    /**
     * The name of a unit kind: the game's own (its string table names a kind by the kind's
     * number, unit_N), the launcher's word for a start location and for a kind without a name.
     */
    private static String nameOf(Host host, int type) {
        if (MapRules.isStart(type)) {
            return host.word("Start Location");
        }
        final String name = host.term("unit_" + type, UnitCatalog.name(type));
        return name.isEmpty() ? host.word("Unit") : name;
    }

    /**
     * A race segment's word: the game's own (customscenario_race_*), the launcher's own word where
     * the game has no such string (Neutral) or the player's table lacks the key.
     */
    private static String raceName(Host host, int race) {
        if (RACE_KEY[race] == null) {
            // A tab of units (Neutral): its own key "<word> (tab)" for languages whose plural differs
            // from the owner's word; untranslated, the owner's word.
            final String tab = host.word(RACE_NAME[race] + " (tab)");
            return tab.equals(RACE_NAME[race] + " (tab)") ? host.word(RACE_NAME[race]) : tab;
        }
        return host.term(RACE_KEY[race], host.word(RACE_NAME[race]));
    }

    private int unitColour(MapModel.Unit u) {
        if (u.type == MapRules.GOLD_MINE) {
            return MINE;
        } else if (u.type == MapRules.OIL_PATCH) {
            return PATCH;
        }
        return (host != null && u.owner < MapRules.PLAYERS) ? host.colour(u.owner) : OTHER;
    }

    /** Dark ink on a light colour, light ink on the others. */
    private static int inkOn(int colour) {
        final int light = ((colour >> 16) & 0xFF) * 3 + ((colour >> 8) & 0xFF) * 6 + (colour & 0xFF);
        return (light > 1400) ? DARK_INK : INK;
    }

    // ----- the unit palette of stage 1b (owner decision 88, 5B) -----
    // Rects of owner board 5B, logical units, y from the view's top (the screen's less 168): the
    // race segment Human / Orc / Neutral, cells 220 x 96 at a pitch of 226; under it the list of the
    // race's groups [3144, 268, 672, to 30 above the word Player], scrolled by one finger, each group
    // a header of 64 and rows of three cells of 192 at a pitch of 216 x 252, the header sticking at
    // the list's top while its cells pass; a scroll bar of 10 at x 3826; the word Player 414 above
    // the minimap and the eight player cells 54 under it, two rows at a pitch of 108.
    private static final String[] RACE_NAME = { "Human", "Orc", "Neutral" };
    /** The game's own string of each race (read from the player's game folder); the game has none for Neutral. */
    private static final String[] RACE_KEY = { "customscenario_race_human", "customscenario_race_orc", null };
    private static final float SEG_X = 3144f, SEG_Y = 324f - 168f, SEG_W = 220f, SEG_PITCH = 226f, SEG_H = 96f;
    private static final float LIST_X = 3144f, LIST_Y = 436f - 168f, LIST_W = 672f, LIST_FOOT_GAP = 30f;
    private static final float HEAD_H = 64f, HEAD_TEXT_IN = 20f, HEAD_TO_CELLS = 80f, GROUP_GAP = 10f, CAP_HEAD = 30f;
    private static final float U_CELL_X = 3168f, U_CELL = 192f, U_PITCH_X = 216f, U_ROW = 252f, CAP_UNIT_NAME = 22f;
    private static final int U_COLUMNS = 3;
    private static final float BAR_X = 3826f, BAR_W = 10f, BAR_LEAST = 96f;
    private static final float OWNER_ABOVE_MINI = 414f, OWNER_CELLS_DOWN = 54f;
    private static final int HEAD_FILL = 0x5992743E, BAR_TRACK = 0x1AFFFFFF, BAR_THUMB = 0xFFCEC6B6;
    /** A fling slows by this share a second, and stops below this speed (logical units a second). */
    private static final float FLING_KEEP = 0.04f, FLING_LEAST = 60f;

    /** The word Player above the player cells: 414 above the minimap (1290 on the design's height). */
    private float ownerKeyY() {
        final float high = (unit > 0f) ? getHeight() / unit : 0f;
        return high - MINI_PAD - mini - OWNER_ABOVE_MINI;
    }

    /** The player cells: two rows of four, one under the other, under the word "Player". */
    private float ownerY(int i) { return ownerKeyY() + OWNER_CELLS_DOWN + (i / 4) * OWNER_PITCH; }

    private void ownerCell(int i, RectF r) {
        final float x = (BRUSH_X + (i % 4) * BRUSH_PITCH) * unit, y = ownerY(i) * unit;
        r.set(x, y, x + BRUSH_W * unit, y + BRUSH_H * unit);
    }

    /**
     * Owner decision 95 (E5 = option C): on a view too low for the word Player and one list row
     * above it, the word is left out and the list ends above the numbered cells; the cells keep
     * their place. Taller views keep the word.
     */
    private boolean ownerWordShown() { return ownerKeyY() - LIST_FOOT_GAP >= LIST_Y + U_ROW; }

    private float listBottom() {
        return Math.max(LIST_Y + U_ROW, (ownerWordShown() ? ownerKeyY() : ownerY(0)) - LIST_FOOT_GAP);
    }

    /** The height of the race's list: its groups one under the other. */
    private float listHigh() {
        float y = 0f;
        for (int g : UnitCatalog.groups(unitRace)) {
            y += groupHigh(UnitCatalog.types(unitRace, g).length);
        }
        return Math.max(0f, y - GROUP_GAP);
    }

    private static float groupHigh(int n) { return HEAD_TO_CELLS + ((n + U_COLUMNS - 1) / U_COLUMNS) * U_ROW + GROUP_GAP; }

    private float listRoom() { return listBottom() - LIST_Y; }

    private void clampList() { listScroll = Math.max(0f, Math.min(Math.max(0f, listHigh() - listRoom()), listScroll)); }

    private void segCell(int i, RectF r) {
        final float x = (SEG_X + i * SEG_PITCH) * unit, y = SEG_Y * unit;
        r.set(x, y, x + SEG_W * unit, y + SEG_H * unit);
    }

    /** The cell of the k-th type of a group whose header lies at gy of the list (before the scroll). */
    private void unitCell(float gy, int k, RectF r) {
        final float x = (U_CELL_X + (k % U_COLUMNS) * U_PITCH_X) * unit;
        final float y = (LIST_Y + gy - listScroll + HEAD_TO_CELLS + (k / U_COLUMNS) * U_ROW) * unit;
        r.set(x, y, x + U_CELL * unit, y + U_CELL * unit);
    }

    /** Scroll the list so that a type's row is in view (Pick took it from the map). */
    private void reveal(int type) {
        layout();
        float gy = 0f;
        for (int g : UnitCatalog.groups(unitRace)) {
            final int[] types = UnitCatalog.types(unitRace, g);
            for (int k = 0; k < types.length; ++k) {
                if (types[k] == type) {
                    final float top = gy + HEAD_TO_CELLS + (k / U_COLUMNS) * U_ROW;
                    if (top - HEAD_H < listScroll || top + U_ROW > listScroll + listRoom()) {
                        listScroll = top - HEAD_TO_CELLS;
                    }
                    clampList();
                    return;
                }
            }
            gy += groupHigh(types.length);
        }
    }

    private void drawUnitPalette(Canvas c) {
        clampList();
        for (int i = 0; i < RACE_NAME.length; ++i) {
            segCell(i, dst);
            cell(c, dst, i == unitRace);
            if (host != null) {
                host.text(c, raceName(host, i), SEG_X + i * SEG_PITCH, SEG_Y, SEG_W, SEG_H, CAP_KEY,
                          (i == unitRace) ? VIEW_BOX : INK, true);
            }
        }
        final float bottom = listBottom();
        c.save();
        c.clipRect(LIST_X * unit, LIST_Y * unit, (LIST_X + LIST_W) * unit, bottom * unit);
        final int[] groups = UnitCatalog.groups(unitRace);
        float gy = 0f;
        for (int g : groups) {
            final int[] types = UnitCatalog.types(unitRace, g);
            for (int k = 0; k < types.length; ++k) {
                unitCell(gy, k, dst);
                if (dst.bottom + NAME_H * unit < LIST_Y * unit || dst.top > bottom * unit) {
                    continue;
                }
                drawUnitCell(c, types[k], dst);
            }
            gy += groupHigh(types.length);
        }
        // The headers over the cells: each at its place, the one whose cells pass the top held there
        // until the next one pushes it up.
        gy = 0f;
        for (int i = 0; i < groups.length; ++i) {
            final float high = groupHigh(UnitCatalog.types(unitRace, groups[i]).length);
            float y = gy - listScroll;
            if (y < 0f) {
                y = Math.min(0f, gy + high - GROUP_GAP - HEAD_H - listScroll);
            }
            final float top = LIST_Y + y;
            fill.setColor(COLUMN);
            c.drawRect(LIST_X * unit, top * unit, (LIST_X + LIST_W) * unit, (top + HEAD_H) * unit, fill);
            fill.setColor(HEAD_FILL);
            c.drawRect(LIST_X * unit, top * unit, (LIST_X + LIST_W) * unit, (top + HEAD_H) * unit, fill);
            if (host != null) {
                host.text(c, host.word(UnitCatalog.GROUP_NAME[groups[i]]), LIST_X + HEAD_TEXT_IN, top,
                          LIST_W - 2f * HEAD_TEXT_IN, HEAD_H, CAP_HEAD, VIEW_BOX, false);
            }
            gy += high;
        }
        c.restore();
        // The scroll bar, where the list is longer than its room.
        final float room = bottom - LIST_Y, all = listHigh();
        if (all > room && room > 0f) {
            fill.setColor(BAR_TRACK);
            c.drawRect(BAR_X * unit, LIST_Y * unit, (BAR_X + BAR_W) * unit, bottom * unit, fill);
            final float thumb = Math.max(BAR_LEAST, room * room / all);
            final float ty = LIST_Y + (room - thumb) * listScroll / (all - room);
            fill.setColor(BAR_THUMB);
            c.drawRect(BAR_X * unit, ty * unit, (BAR_X + BAR_W) * unit, (ty + thumb) * unit, fill);
        }
        if (host != null && ownerWordShown()) {
            host.text(c, host.word("Player"), BRUSH_KEY_X, ownerKeyY(), PALETTE_W, OWNER_KEY_H, CAP_KEY, INK_MUTED, false);
        }
        for (int i = 0; i < MapRules.PLAYERS; ++i) {
            ownerCell(i, dst);
            final int colour = (host != null) ? host.colour(i) : OTHER;
            fill.setColor(colour);
            c.drawRect(dst, fill);
            stroke.setColor((i == owner) ? VIEW_BOX : LINE);
            stroke.setStrokeWidth(Math.max(1f, ((i == owner) ? 10f : 2f) * unit));
            c.drawRect(dst, stroke);
            if (host != null) {
                host.text(c, String.valueOf(i + 1), BRUSH_X + (i % 4) * BRUSH_PITCH, ownerY(i), BRUSH_W, BRUSH_H,
                          CAP_CELL, inkOn(colour), true);
            }
            // The rescue badge repeated on the player's cell, so the kind is known before placing (2A).
            final int kind = MapRules.rescueOf(map, i);
            if (kind != 0) {
                drawRescueBadge(c, dst.right - RESCUE_CELL_IN * unit, dst.top + RESCUE_CELL_IN * unit,
                                Math.max(4f, RESCUE_BADGE_CELL * unit), kind);
            }
        }
        stroke.setColor(LINE);
        stroke.setStrokeWidth(Math.max(1f, 2f * unit));
    }

    /** One type of the list: its cell, its still picture or its footprint, its name under it. */
    private void drawUnitCell(Canvas c, int type, RectF r) {
        final boolean chosen = type == unitType;
        cell(c, r, chosen);
        final float cx = r.centerX(), cy = r.centerY();
        final boolean neutral = MapRules.neutral(type);
        final int colour = (type == MapRules.GOLD_MINE) ? MINE : (type == MapRules.OIL_PATCH) ? PATCH
            : (neutral || host == null) ? OTHER : host.colour(owner);
        final UnitArt.Picture p = picture(type);
        if (p != null) {
            // The type's still picture, as large as the cell takes it, in the chosen player's colour.
            UnitFrames.fit(p.bodyAt, cx, cy, r.width(), PICTURE_ROOM, spot);
            art.draw(c, p, spot[0], spot[1], spot[2], colour);
        } else {
            // Without a picture: the type as its footprint (a quarter of the cell a tile, at most) in its colour.
            final float side = r.width() * Math.min(4, MapRules.footprint(type) + 1) / 12f;
            fill.setColor(colour);
            stroke.setColor(INK);
            stroke.setStrokeWidth(Math.max(1f, 2f * unit));
            if (MapRules.isStart(type)) {
                c.drawCircle(cx, cy, side, fill);
                c.drawCircle(cx, cy, side, stroke);
            } else {
                c.drawRect(cx - side, cy - side, cx + side, cy + side, fill);
                c.drawRect(cx - side, cy - side, cx + side, cy + side, stroke);
            }
        }
        if (host != null) {
            host.text(c, nameOf(host, type), r.left / unit - (U_PITCH_X - U_CELL) / 2f + 4f, r.bottom / unit + NAME_GAP,
                      U_PITCH_X - 8f, NAME_H, CAP_UNIT_NAME, chosen ? VIEW_BOX : INK, true);
        }
    }

    private void unitPaletteTap(float x, float y) {
        boolean chose = false;
        for (int i = 0; i < RACE_NAME.length; ++i) {
            segCell(i, dst);
            if (dst.contains(x, y) && unitRace != i) {
                unitRace = i;
                listScroll = 0f;
                chose = true;
            }
        }
        if (x >= LIST_X * unit && x < (LIST_X + LIST_W) * unit && y >= LIST_Y * unit && y < listBottom() * unit) {
            float gy = 0f;
            for (int g : UnitCatalog.groups(unitRace)) {
                final int[] types = UnitCatalog.types(unitRace, g);
                for (int k = 0; k < types.length; ++k) {
                    unitCell(gy, k, dst);
                    // The header that sticks over a cell takes the tap, not the cell under it.
                    if (dst.contains(x, y) && y >= (LIST_Y + HEAD_H) * unit) {
                        chose = unitType != types[k] || tool != TOOL_UNITS;
                        unitType = types[k];
                        tool = TOOL_UNITS;
                        chosen = new int[0];
                    }
                }
                gy += groupHigh(types.length);
            }
        }
        for (int i = 0; i < MapRules.PLAYERS; ++i) {
            ownerCell(i, dst);
            if (dst.contains(x, y) && owner != i) {
                owner = i;
                chose = true;
            }
        }
        if (chose && host != null) {
            host.click();
        }
        invalidate();
    }

    // One finger on the list scrolls it (it never paints: the palette is outside the canvas); a fling goes on.
    private boolean listDragging, listScrolled;
    private float listDownY, listFrom, listLastY, listSpeed;
    private long listLastTime, flingLast;
    private final Runnable fling = new Runnable() {
        @Override
        public void run() {
            flingStep();
        }
    };

    private void listDown(float x, float y, long time) {
        removeCallbacks(fling);
        listScrolled = false;
        listDragging = !terrainPalette() && x >= LIST_X * unit && y >= LIST_Y * unit && y < listBottom() * unit;
        listDownY = listLastY = y;
        listFrom = listScroll;
        listLastTime = time;
        listSpeed = 0f;
    }

    private void listMove(float y, long time) {
        if (!listDragging) {
            return;
        }
        listScrolled |= Math.abs(y - listDownY) > TAP_SLOP * unit;
        if (!listScrolled) {
            return;
        }
        listScroll = listFrom - (y - listDownY) / unit;
        clampList();
        if (time > listLastTime) {
            listSpeed = -(y - listLastY) / unit * 1000f / (time - listLastTime);
        }
        listLastY = y;
        listLastTime = time;
        invalidate();
    }

    private void listUp(boolean lifted) {
        if (listDragging && listScrolled && lifted && Math.abs(listSpeed) > FLING_LEAST) {
            flingLast = android.os.SystemClock.uptimeMillis();
            postDelayed(fling, 16L);
        }
        listDragging = false;
    }

    private void flingStep() {
        final long now = android.os.SystemClock.uptimeMillis();
        final float dt = Math.min(0.05f, (now - flingLast) / 1000f);
        flingLast = now;
        final float before = listScroll;
        listScroll += listSpeed * dt;
        clampList();
        listSpeed *= (float) Math.pow(FLING_KEEP, dt);
        invalidate();
        if (Math.abs(listSpeed) > FLING_LEAST && listScroll != before && !released) {
            postDelayed(fling, 16L);
        }
    }

    /** True when a unit is among the chosen. */
    private boolean isChosen(int index) {
        for (int i : chosen) {
            if (i == index) {
                return true;
            }
        }
        return false;
    }

    /** The chosen units, less any that is no longer in the list. */
    private int[] liveChosen() {
        int n = 0;
        for (int i : chosen) {
            n += (i < map.unitCount()) ? 1 : 0;
        }
        if (n == chosen.length) {
            return chosen;
        }
        final int[] out = new int[n];
        n = 0;
        for (int i : chosen) {
            if (i < map.unitCount()) {
                out[n++] = i;
            }
        }
        chosen = out;
        return out;
    }

    /** One footprint under the finger, tile by tile: green where the unit may stand, red where not; its frame. */
    private void drawGhost(Canvas c, float left, int type, int gx, int gy, boolean[] leftOut) {
        final int size = MapRules.footprint(type);
        for (int yy = gy; yy < gy + size; ++yy) {
            for (int xx = gx; xx < gx + size; ++xx) {
                fill.setColor((MapRules.tileFits(map, type, xx, yy)
                               && (MapRules.isStart(type) || !MapRules.takenBut(map, xx, yy, leftOut, type)))
                    ? GHOST_OK : GHOST_NO);
                c.drawRect(left + xx * tilePx - originX, yy * tilePx - originY,
                           left + (xx + 1) * tilePx - originX, (yy + 1) * tilePx - originY, fill);
            }
        }
        stroke.setColor(MapRules.mayPlace(map, type, gx, gy, leftOut) ? 0xFF30C050 : 0xFFD03030);
        stroke.setStrokeWidth(Math.max(2f, 4f * unit));
        c.drawRect(left + gx * tilePx - originX, gy * tilePx - originY,
                   left + (gx + size) * tilePx - originX, (gy + size) * tilePx - originY, stroke);
    }

    private void drawUnits(Canvas c, float left) {
        final int[] picked = liveChosen();
        // The start locations last: one may lie under a hall or a worker.
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < map.unitCount(); ++i) {
                final boolean mine = isChosen(i);
                if (!(groupMoving && mine) && MapRules.isStart(map.unit(i).type) == (pass == 1)) {
                    drawUnit(c, left, map.unit(i), mine);
                }
            }
        }
        // The rescue marks over all units: a unit's neighbour does not hide them (owner decision 93, 2A).
        for (int i = 0; i < map.unitCount(); ++i) {
            final MapModel.Unit u = map.unit(i);
            if (!(groupMoving && isChosen(i)) && !MapRules.isStart(u.type)) {
                final int kind = MapRules.rescueOf(map, u.owner);
                if (kind != 0) {
                    drawRescueMark(c, left, u, kind);
                }
            }
        }
        if (ghostType >= 0) {
            drawGhost(c, left, ghostType, ghostX, ghostY, null);
        }
        if (groupMoving) {
            // The chosen units at the place they are being moved to; the others of the group are not in their way.
            final boolean[] out = MapRules.marks(map, picked);
            for (int i : picked) {
                final MapModel.Unit u = map.unit(i);
                drawGhost(c, left, u.type, u.x + moveDX, u.y + moveDY, out);
            }
        }
        if (boxing && boxMoved) {
            final int ax = Math.min(boxX0, boxX1), bx = Math.max(boxX0, boxX1) + 1;
            final int ay = Math.min(boxY0, boxY1), by = Math.max(boxY0, boxY1) + 1;
            stroke.setColor(VIEW_BOX);
            stroke.setStrokeWidth(Math.max(2f, 4f * unit));
            c.drawRect(left + ax * tilePx - originX, ay * tilePx - originY, left + bx * tilePx - originX,
                       by * tilePx - originY, stroke);
        }
        if (markX >= 0) {
            stroke.setColor(VIEW_BOX);
            stroke.setStrokeWidth(Math.max(3f, 8f * unit));
            c.drawCircle(left + (markX + 0.5f) * tilePx - originX, (markY + 0.5f) * tilePx - originY,
                         Math.max(tilePx * 2.5f, 40f * unit), stroke);
        }
        stroke.setColor(LINE);
        stroke.setStrokeWidth(Math.max(1f, 2f * unit));
    }

    // The rescue mark of owner board 2A, logical units: outline 6 wide, 12 outside the footprint,
    // dashes 22 / 14, corners 8; badge radius 34 on the footprint's top right corner (22 on a palette
    // cell, 6 in from its corner), dark fill, white ring 6; a dot of 0.45 the radius for "fights".
    // A footprint below 60 on the screen gets the badge only.
    private static final float RESCUE_LINE = 6f, RESCUE_OUT = 12f, RESCUE_DASH = 22f, RESCUE_GAP = 14f, RESCUE_CORNER = 8f;
    private static final float RESCUE_BADGE = 34f, RESCUE_BADGE_CELL = 22f, RESCUE_CELL_IN = 6f, RESCUE_RING = 6f;
    private static final float RESCUE_DOT = 0.45f, RESCUE_LEAST = 60f;
    private static final int RESCUE_WHITE = 0xFFFFFFFF, RESCUE_DARK = 0xFF111111;

    /** The dashed white outline round a rescuable unit and its badge (owner decision 93, 2A). */
    private void drawRescueMark(Canvas c, float left, MapModel.Unit u, int kind) {
        final int size = MapRules.footprint(u.type);
        final float x0 = left + u.x * tilePx - originX, y0 = u.y * tilePx - originY;
        final float x1 = x0 + size * tilePx, y1 = y0 + size * tilePx;
        if (x1 - x0 >= RESCUE_LEAST * unit) {
            if (rescueDashFor != unit) {
                rescueDashFor = unit;
                rescueLine.setStyle(Paint.Style.STROKE);
                rescueLine.setColor(RESCUE_WHITE);
                rescueLine.setPathEffect(new DashPathEffect(new float[] { Math.max(2f, RESCUE_DASH * unit),
                    Math.max(2f, RESCUE_GAP * unit) }, 0f));
            }
            rescueLine.setStrokeWidth(Math.max(1.5f, RESCUE_LINE * unit));
            final float out = RESCUE_OUT * unit, r = RESCUE_CORNER * unit;
            rescueBox.set(x0 - out, y0 - out, x1 + out, y1 + out);
            c.drawRoundRect(rescueBox, r, r, rescueLine);
        }
        drawRescueBadge(c, x1, y0, Math.max(6f, RESCUE_BADGE * unit), kind);
    }

    /** The badge: a ring for "waits", a ring with a dot for "fights". */
    private void drawRescueBadge(Canvas c, float cx, float cy, float radius, int kind) {
        rescueBadge.setStyle(Paint.Style.FILL);
        rescueBadge.setColor(RESCUE_DARK);
        c.drawCircle(cx, cy, radius, rescueBadge);
        rescueBadge.setStyle(Paint.Style.STROKE);
        rescueBadge.setColor(RESCUE_WHITE);
        rescueBadge.setStrokeWidth(Math.max(1.5f, RESCUE_RING * unit));
        c.drawCircle(cx, cy, radius, rescueBadge);
        if (kind == MapModel.OWNER_RESCUE_FIGHTS) {
            rescueBadge.setStyle(Paint.Style.FILL);
            c.drawCircle(cx, cy, radius * RESCUE_DOT, rescueBadge);
        }
    }

    /** A unit as its footprint in its colour, with its name where there is room for it. */
    private void drawUnit(Canvas c, float left, MapModel.Unit u, boolean chosen) {
        final int size = MapRules.footprint(u.type);
        final float x0 = left + u.x * tilePx - originX, y0 = u.y * tilePx - originY;
        final float x1 = x0 + size * tilePx, y1 = y0 + size * tilePx;
        final int colour = unitColour(u);
        final UnitArt.Picture p = picture(u.type);
        if (p != null && tilePx > 0f) {
            // As the game draws it: the frame's own canvas centred on the footprint, a tile of the art a tile here.
            art.draw(c, p, (x0 + x1) / 2f, (y0 + y1) / 2f, tilePx / UnitFrames.NATIVE_TILE, colour);
            if (chosen) {
                stroke.setColor(VIEW_BOX);
                stroke.setStrokeWidth(Math.max(2f, 8f * unit));
                c.drawRect(x0, y0, x1, y1, stroke);
            }
            return;
        }
        fill.setColor((colour & 0x00FFFFFF) | 0xC0000000);
        stroke.setColor(chosen ? VIEW_BOX : INK);
        stroke.setStrokeWidth(Math.max(2f, (chosen ? 8f : 3f) * unit));
        if (MapRules.isStart(u.type)) {
            c.drawCircle((x0 + x1) / 2f, (y0 + y1) / 2f, tilePx * 0.5f, fill);
            c.drawCircle((x0 + x1) / 2f, (y0 + y1) / 2f, tilePx * 0.5f, stroke);
            c.drawLine(x0, y0, x1, y1, stroke);
            c.drawLine(x0, y1, x1, y0, stroke);
        } else {
            c.drawRect(x0, y0, x1, y1, fill);
            c.drawRect(x0, y0, x1, y1, stroke);
        }
        final float wide = (x1 - x0) / unit;
        if (host != null && wide >= 150f && !MapRules.isStart(u.type)) {
            host.text(c, nameOf(host, u.type), x0 / unit + 6f, y0 / unit, wide - 12f, (y1 - y0) / unit,
                      Math.min(CAP_KEY, wide / 7f), inkOn(colour), true);
        }
    }

    /**
     * The strip shows while units are chosen, with whatever tool (a long press chooses with any
     * tool): for units of one kind the designed strip; for several kinds one chip a kind and
     * Remove all (owner decision 88, 6B), a chip narrowing the choice to its kind.
     */
    private boolean stripShown() {
        return liveChosen().length > 0 || pendingFill != null;
    }

    // The strip of several kinds (owner board 6B): the count, chips of 440 at a pitch of 470 (StripChips), Remove all.
    private static final float KINDS_COUNT_X = 330f, CHIP_X = StripChips.X, CHIP_W = StripChips.W;
    private static final float CHIP_SWATCH_IN = 20f, CHIP_SWATCH = 48f, CAP_CHIP = 32f;
    private static final float ALL_X = 2700f, ALL_W = 372f;

    // More kinds than the room holds scroll sideways under one finger (board 6B); the scroll
    // starts at 0 for every new choice.
    private float chipScroll, chipScrollDown, stripDownX;
    private boolean chipDragging;
    private int[] chipFor;

    /** The chip scroll of the choice now shown, 0 when the choice is a new one; held in its range. */
    private float chipScroll(int chips) {
        if (chipFor != chosen) {
            chipFor = chosen;
            chipScroll = 0f;
        }
        chipScroll = StripChips.clamp(chipScroll, chips, ALL_X);
        return chipScroll;
    }

    /** A finger down on the strip: where it began and the chip scroll then. */
    private void stripDown(float px) {
        stripDownX = px;
        chipScrollDown = chipScroll;
        chipDragging = false;
    }

    /**
     * A finger moving on the strip: on the chips of several kinds with more than the room holds,
     * past the tap slop it scrolls them sideways (and is no longer a tap).
     */
    private void stripMove(float px) {
        if (pendingFill != null || !stripShown() || MapRules.oneKind(map, chosen)) {
            return;
        }
        final int chips = MapRules.kindsOf(map, chosen).length;
        if (StripChips.maxScroll(chips, ALL_X) <= 0f) {
            return;
        }
        if (!chipDragging && Math.abs(px - stripDownX) > TAP_SLOP * unit) {
            chipDragging = true;
            chipScrollDown = chipScroll(chips);
        }
        if (chipDragging) {
            chipScroll = StripChips.clamp(chipScrollDown - (px - stripDownX) / unit, chips, ALL_X);
            invalidate();
        }
    }

    private void drawKindsStrip(Canvas c, float y) {
        final int[][] kinds = MapRules.kindsOf(map, chosen);
        host.text(c, host.word("{0} units").replace("{0}", String.valueOf(chosen.length)), KINDS_COUNT_X, y,
                  CHIP_X - KINDS_COUNT_X - 12f, STRIP_H, CAP_CELL, VIEW_BOX, false);
        final float cy = y + STRIP_CELL_Y;
        final float scroll = chipScroll(kinds.length), right = StripChips.windowRight(ALL_X);
        c.save();
        // A chip cut at the window's edge shows that the row goes on.
        c.clipRect(CHIP_X * unit, y * unit, right * unit, (y + STRIP_H) * unit);
        for (int i = 0; i < kinds.length; ++i) {
            final float x = StripChips.left(i, scroll);
            if (x + CHIP_W <= CHIP_X || x >= right) {
                continue;
            }
            dst.set(x * unit, cy * unit, (x + CHIP_W) * unit, (cy + STRIP_CELL_H) * unit);
            cell(c, dst, false);
            final MapModel.Unit u = map.unit(kinds[i][0]);
            fill.setColor(unitColour(u));
            final float sx = (x + CHIP_SWATCH_IN) * unit, sy = (cy + (STRIP_CELL_H - CHIP_SWATCH) / 2f) * unit;
            c.drawRect(sx, sy, sx + CHIP_SWATCH * unit, sy + CHIP_SWATCH * unit, fill);
            final float tx = x + CHIP_SWATCH_IN + CHIP_SWATCH;
            host.text(c, nameOf(host, u.type) + " " + kinds[i][1], tx, cy, x + CHIP_W - tx, STRIP_CELL_H, CAP_CHIP, INK, true);
        }
        c.restore();
        stripCell(c, ALL_X, cy, ALL_W, host.word("Remove all"));
    }

    private float stripY() { return getHeight() / unit - STRIP_UP; }

    private void drawStrip(Canvas c) {
        if (!stripShown() || host == null) {
            return;
        }
        final float y0 = stripY();
        if (pendingFill != null) {
            drawFillStrip(c, y0);
            return;
        }
        final int[] picked = chosen;
        if (!MapRules.oneKind(map, picked)) {
            stripBack(c, y0);
            drawKindsStrip(c, y0);
            return;
        }
        final MapModel.Unit u = map.unit(picked[0]);
        boolean oneOwner = true, oneAmount = true, oneWord = true;
        for (int i : picked) {
            oneOwner &= map.unit(i).owner == u.owner;
            oneAmount &= map.unit(i).data == u.data;
            oneWord &= MapRules.isPassive(map.unit(i).data) == MapRules.isPassive(u.data);
        }
        final float y = y0;
        stripBack(c, y);
        // Several units of one kind: their number after the name; the owner only when they share one
        // and the strip has no owner cells (owner decision 94, E2: the cells show the owner).
        final boolean cells = MapRules.ownerCells(u.type);
        String line = nameOf(host, u.type) + ((picked.length > 1) ? " (" + picked.length + ")" : "");
        if (oneOwner && !cells) {
            line += " - " + ((u.owner < MapRules.PLAYERS) ? host.word("Player") + " " + (u.owner + 1)
                                                          : host.word("Neutral"));
        }
        host.text(c, line, STRIP_X + 36f, y, (cells ? StripOwners.X - StripOwners.NAME_GAP : MINUS_X - 36f) - STRIP_X - 36f,
                  STRIP_H, CAP_CELL, INK, false);
        final float cy = y + STRIP_CELL_Y;
        if (cells) {
            drawOwnerCells(c, cy, oneOwner ? u.owner : -1);
        }
        if (MapRules.isResource(u.type)) {
            stripCell(c, MINUS_X, cy, STEP_W, "-");
            // Amounts that differ are not shown (not designed); - and + still step each one.
            if (oneAmount) {
                host.text(c, String.valueOf(u.data * MapModel.RESOURCE_UNIT), AMOUNT_X, cy, AMOUNT_W, STRIP_CELL_H,
                          CAP_CELL, VIEW_BOX, true);
            }
            stripCell(c, PLUS_X, cy, STEP_W, "+");
        } else if (MapRules.hasAiWord(u.type)) {
            // Words that differ show both; a tap then makes all Passive.
            stripCell(c, AI_X, cy, AI_W, !oneWord ? host.word("Passive") + " / " + host.word("Active")
                                              : host.word(MapRules.isPassive(u.data) ? "Passive" : "Active"));
        }
        stripCell(c, REMOVE_X, cy, REMOVE_W, host.word("Remove"));
    }

    /** The eight owner cells (E2): the slots' colours; the owner the chosen units share ringed gold. */
    private void drawOwnerCells(Canvas c, float cy, int owner) {
        final float top = cy + (STRIP_CELL_H - StripOwners.CELL) / 2f;
        for (int i = 0; i < StripOwners.COUNT; ++i) {
            final float x = StripOwners.left(i);
            dst.set(x * unit, top * unit, (x + StripOwners.CELL) * unit, (top + StripOwners.CELL) * unit);
            fill.setColor(host.colour(i));
            c.drawRect(dst, fill);
            stroke.setColor(i == owner ? VIEW_BOX : LINE);
            stroke.setStrokeWidth(Math.max(1f, (i == owner ? 6f : 2f) * unit));
            c.drawRect(dst, stroke);
        }
    }

    /**
     * The number sheet's OK (owner decision 94, E1): every chosen mine or patch takes the data
     * word, as one undo step. Nothing when the choice is no longer mines or patches of one kind.
     */
    public void typeAmount(int data) {
        final int[] picked = liveChosen();
        if (data <= 0 || !MapRules.oneKind(map, picked) || !MapRules.isResource(map.unit(picked[0]).type)) {
            return;
        }
        apply(MapRules.amountSetAll(map, picked, data));
    }

    private void stripBack(Canvas c, float y) {
        fill.setColor(COLUMN);
        c.drawRect(STRIP_X * unit, y * unit, (STRIP_X + STRIP_W) * unit, (y + STRIP_H) * unit, fill);
        stroke.setColor(LINE);
        stroke.setStrokeWidth(Math.max(1f, 2f * unit));
        c.drawRect(STRIP_X * unit, y * unit, (STRIP_X + STRIP_W) * unit, (y + STRIP_H) * unit, stroke);
    }

    private void stripCell(Canvas c, float x, float y, float w, String word) {
        dst.set(x * unit, y * unit, (x + w) * unit, (y + STRIP_CELL_H) * unit);
        cell(c, dst, false);
        host.text(c, word, x, y, w, STRIP_CELL_H, CAP_CELL, INK, true);
    }

    private boolean inStrip(float x, float y) {
        return stripShown() && x >= STRIP_X * unit && x < (STRIP_X + STRIP_W) * unit
            && y >= stripY() * unit && y < (stripY() + STRIP_H) * unit;
    }

    private void stripTap(float px, float py) {
        if (!stripShown()) {
            return;
        }
        final float x = px / unit, y = py / unit - stripY();
        if (y < STRIP_CELL_Y || y >= STRIP_CELL_Y + STRIP_CELL_H) {
            return;
        }
        if (pendingFill != null) {
            fillStripTap(x);
            return;
        }
        final int[] picked = chosen;
        final MapModel.Unit u = map.unit(picked[0]);
        final boolean several = !MapRules.oneKind(map, picked);
        if (several) {
            final int[][] kinds = MapRules.kindsOf(map, picked);
            final int i = StripChips.at(x, chipScroll(kinds.length), kinds.length, ALL_X);
            if (i >= 0) {
                chosen = MapRules.narrow(map, picked, kinds[i][0]);
                if (host != null) {
                    host.click();
                }
                invalidate();
                return;
            }
        }
        MapModel.Step step = null;
        if (several ? (x >= ALL_X && x < ALL_X + ALL_W) : (x >= REMOVE_X && x < REMOVE_X + REMOVE_W)) {
            // Removed units are repeated on the images when Mirror is on (EDITOR_DESIGN.md 4.2).
            final java.util.TreeSet<Integer> gone = new java.util.TreeSet<Integer>();
            for (int i : picked) {
                gone.addAll(MapRules.withImages(map, i, mirror));
            }
            step = MapRules.eraseAll(map, gone);
            chosen = new int[0];
        } else if (!several && MapRules.isResource(u.type) && x >= MINUS_X && x < MINUS_X + STEP_W) {
            step = MapRules.amountAll(map, picked, -1);
        } else if (!several && MapRules.isResource(u.type) && x >= PLUS_X && x < PLUS_X + STEP_W) {
            step = MapRules.amountAll(map, picked, 1);
        } else if (!several && MapRules.isResource(u.type) && x >= AMOUNT_X && x < AMOUNT_X + AMOUNT_W) {
            // Tap the amount: the number sheet over the view (E1); its OK comes back as typeAmount.
            boolean oneAmount = true;
            for (int i : picked) {
                oneAmount &= map.unit(i).data == u.data;
            }
            if (host != null) {
                host.click();
                host.amount(nameOf(host, u.type), oneAmount ? (long) u.data * MapModel.RESOURCE_UNIT : -1L);
            }
            return;
        } else if (!several && MapRules.ownerCells(u.type) && StripOwners.at(x) >= 0) {
            step = MapRules.ownerAll(map, picked, StripOwners.at(x));
        } else if (!several && MapRules.hasAiWord(u.type) && x >= AI_X && x < AI_X + AI_W) {
            boolean allPassive = true;
            for (int i : picked) {
                allPassive &= MapRules.isPassive(map.unit(i).data);
            }
            step = MapRules.aiWordAll(map, picked, !allPassive);
        }
        if (step != null) {
            apply(step);
            if (host != null) {
                host.click();
            }
        }
    }

    /** Make a change of the map that is one undo step. A null step is nothing. */
    public void apply(MapModel.Step step) {
        if (step == null) {
            return;
        }
        try {
            work.apply(step);
        } catch (java.io.IOException e) {
            trouble = true;
        }
        invalidate();
        changed();
    }

    public MapModel model() { return map; }

    /** End whatever a finger has begun, keeping a begun stroke: before the map is read as a whole. */
    public void rest() {
        dropPending();
        removeCallbacks(hold);
        stopEdge();
        endStroke(true);
        unitUp(false);
        tapPending = false;
    }

    /** Bring a tile into the middle of the view, near enough to be seen, and ring it. */
    public void show(int x, int y) {
        layout();
        tilePx = Math.max(tilePx, 48f * unit);
        originX = (x + 0.5f) * tilePx - canvasWidth() / 2f;
        originY = (y + 0.5f) * tilePx - canvasHeight() / 2f;
        markX = x;
        markY = y;
        invalidate();
    }

    private int tileX(float x) { return (int) Math.floor((x - canvasLeft() + originX) / tilePx); }
    private int tileY(float y) { return (int) Math.floor((y + originY) / tilePx); }

    /** A finger came down on the map with a tool for units. */
    private void unitDown(float x, float y) {
        final int tx = tileX(x), ty = tileY(y);
        if (tool == TOOL_UNITS) {
            ghostType = unitType;
            grabX = grabY = (MapRules.footprint(ghostType) - 1) / 2f;
            unitMove(x, y);
        } else if (tool == TOOL_SELECT) {
            ghostType = -1;
            groupMoving = false;
            final int at = MapRules.unitAt(map, tx, ty);
            if (at < 0) {
                // Empty ground: a rectangle, or with no movement a tap that chooses nothing.
                boxing = true;
                boxMoved = false;
                boxX0 = boxX1 = tx;
                boxY0 = boxY1 = ty;
            } else {
                if (!isChosen(at)) {
                    chosen = new int[] { at };
                    if (host != null) {
                        host.click();
                    }
                }
                // Moved only once the finger leaves the tile it came down on; a chosen group moves together.
                grabbing = true;
                grabTX = tx;
                grabTY = ty;
                moveDX = moveDY = 0;
            }
        } else if (tool == TOOL_ERASE) {
            erasePending = true;
            eraseX = tx;
            eraseY = ty;
        }
        invalidate();
    }

    private void unitMove(float x, float y) {
        final int tx = tileX(x), ty = tileY(y);
        if (tool == TOOL_UNITS && ghostType >= 0) {
            ghostX = Math.round((x - canvasLeft() + originX) / tilePx - 0.5f - grabX);
            ghostY = Math.round((y + originY) / tilePx - 0.5f - grabY);
        } else if (tool == TOOL_SELECT && boxing) {
            boxMoved |= tx != boxX0 || ty != boxY0;
            boxX1 = tx;
            boxY1 = ty;
        } else if (tool == TOOL_SELECT && grabbing && liveChosen().length > 0) {
            final int dx = tx - grabTX, dy = ty - grabTY;
            groupMoving |= dx != 0 || dy != 0;
            if (groupMoving) {
                moveDX = dx;
                moveDY = dy;
            }
        } else if (tool == TOOL_ERASE && (!erasePending || tx != eraseX || ty != eraseY)) {
            if (erasePending) {
                erasePending = false;
                eraseAt(eraseX, eraseY);
            }
            eraseAt(tx, ty);
        }
        invalidate();
    }

    /** The finger was lifted, or the touch was taken away (keep false). */
    private void unitUp(boolean keep) {
        final int type = ghostType;
        ghostType = -1;
        if (erasePending) {
            erasePending = false;
            if (keep) {
                eraseAt(eraseX, eraseY);
            }
        }
        if (boxing) {
            boxing = false;
            if (keep) {
                final int[] now = boxMoved ? MapRules.unitsIn(map, boxX0, boxY0, boxX1, boxY1) : new int[0];
                if (!java.util.Arrays.equals(now, chosen) && host != null) {
                    host.click();
                }
                chosen = now;
            }
        }
        final boolean moved = groupMoving;
        groupMoving = false;
        grabbing = false;
        if (keep && moved) {
            final MapModel.Step step = MapRules.moveAll(map, liveChosen(), moveDX, moveDY);
            if (step != null) {
                apply(step);
                if (host != null) {
                    host.click();
                }
            }
        }
        if (keep && type >= 0) {
            // Placed units are repeated on the images when Mirror is on (EDITOR_DESIGN.md 4.2; owner decision 88, 1A).
            final MapRules.Mirrored made = MapRules.mirrorPlaceType(map, unitType, owner, ghostX, ghostY, mirror);
            if (made.step != null) {
                apply(made.step);
                if (host != null) {
                    host.click();
                    if (made.placed < made.wanted) {
                        host.notice(NOTICE_MILLIS, "{0} of {1} not placed: the image falls where it cannot stand",
                                    made.wanted - made.placed, made.wanted);
                    }
                }
            }
        }
        invalidate();
    }

    /** Erase on a tile: the unit there and its images; else the wall there and its images. */
    private void eraseAt(int tx, int ty) {
        final int at = MapRules.unitAt(map, tx, ty);
        if (at >= 0) {
            chosen = new int[0];
            apply(MapRules.eraseAll(map, MapRules.withImages(map, at, mirror)));
            if (host != null) {
                host.click();
            }
            return;
        }
        final Terrain set = terrain;
        if (set == null || tx < 0 || ty < 0 || tx >= map.width() || ty >= map.height()
            || !Walls.isWall(map.tile(tx, ty))) {
            return;
        }
        final Terrain.Stroke s = set.begin(map, System.nanoTime());
        if (s.unwall(tx, ty, mirror)) {
            apply(s.step());
            redraw(s.minX(), s.minY(), s.maxX(), s.maxY());
            if (host != null) {
                host.click();
            }
        }
    }

    // ----- the long press and the canvas edge -----

    /** Whatever the finger began is let go without a change. */
    private void dropFinger() {
        endStroke(false);
        ghostType = -1;
        grabbing = false;
        groupMoving = false;
        boxing = false;
        erasePending = false;
        tapPending = false;
        stopEdge();
    }

    /**
     * A finger held 400 ms without moving: on a unit, with any tool, the unit is chosen and its
     * strip shown; on terrain with the tool Terrain, that terrain is taken into the palette.
     * Otherwise the tool goes on as if nothing had happened.
     */
    private void longPress() {
        if (twoFingers || tilePx <= 0f || released) {
            return;
        }
        final float mx = (downX - canvasLeft() + originX) / tilePx, my = (downY + originY) / tilePx;
        final int tx = (int) Math.floor(mx), ty = (int) Math.floor(my);
        if (tx < 0 || ty < 0 || tx >= map.width() || ty >= map.height()) {
            return;
        }
        final int at = MapRules.unitAt(map, tx, ty);
        if (at >= 0) {
            dropFinger();
            chosen = new int[] { at };
        } else if (tool == TOOL_TERRAIN) {
            final int found = paletteKindAt(tx, ty, mx - tx, my - ty);
            if (found < 0) {
                return;
            }
            dropFinger();
            kind = found;
        } else {
            return;
        }
        held = true;
        if (host != null) {
            host.click();
        }
        invalidate();
    }

    /** True while one finger paints or moves a unit: the drags the canvas edge scrolls under. */
    private boolean edgeDrag() {
        return !twoFingers && !held && (painting != null || (tool == TOOL_UNITS && ghostType >= 0) || grabbing);
    }

    /** How fast the view moves for a finger at a place along one axis, pixels a second; 0 away from the edge. */
    private float edgeSpeed(float at, float low, float high) {
        final float band = EDGE_BAND * unit;
        if (band <= 0f) {
            return 0f;
        }
        if (at < low + band) {
            return -EDGE_SPEED * unit * Math.min(1f, (low + band - at) / band);
        }
        if (at > high - band) {
            return EDGE_SPEED * unit * Math.min(1f, (at - (high - band)) / band);
        }
        return 0f;
    }

    /** The finger moved to (x, y): begin scrolling when it reached the canvas edge. */
    private void edgeCheck(float x, float y) {
        lastX = x;
        lastY = y;
        if (edging || !edgeDrag()) {
            return;
        }
        if (edgeSpeed(x, canvasLeft(), canvasLeft() + canvasWidth()) != 0f || edgeSpeed(y, 0f, canvasHeight()) != 0f) {
            edging = true;
            edgeLast = android.os.SystemClock.uptimeMillis();
            postDelayed(edge, 16L);
        }
    }

    private void edgeStep() {
        if (!edging) {
            return;
        }
        final float vx = edgeSpeed(lastX, canvasLeft(), canvasLeft() + canvasWidth());
        final float vy = edgeSpeed(lastY, 0f, canvasHeight());
        if (!edgeDrag() || (vx == 0f && vy == 0f)) {
            edging = false;
            return;
        }
        final long now = android.os.SystemClock.uptimeMillis();
        final float dt = Math.min(0.05f, (now - edgeLast) / 1000f);
        edgeLast = now;
        originX += vx * dt;
        originY += vy * dt;
        settle();
        // The map moved under the finger that stands still: the stroke or the unit follows it.
        if (painting != null) {
            dab(lastX, lastY);
        } else {
            unitMove(lastX, lastY);
        }
        invalidate();
        postDelayed(edge, 16L);
    }

    private void stopEdge() {
        edging = false;
        removeCallbacks(edge);
    }

    public boolean canUndo() { return map.canUndo(); }
    public boolean canRedo() { return map.canRedo(); }
    public boolean unsaved() { return work.unsaved(); }
    /** True when the working copy or the map file could not be written. */
    public boolean trouble() { return trouble; }

    public void undo() { history(true); }
    public void redo() { history(false); }

    private void history(boolean back) {
        endStroke(true);
        unitUp(false);
        dropPending();
        chosen = new int[0];
        try {
            if (back ? work.undo() : work.redo()) {
                redraw(0, 0, map.width() - 1, map.height() - 1);
            }
        } catch (java.io.IOException e) {
            trouble = true;
        }
        invalidate();
        changed();
    }

    /** Write the map file. */
    public void save() {
        endStroke(true);
        try {
            work.save();
            trouble = false;
        } catch (java.io.IOException e) {
            trouble = true;
        }
        invalidate();
        changed();
    }

    /** Forget the working copy: the map is again as it was last saved. The view is not used after it. */
    public void discard() {
        painting = null;
        try {
            work.discard();
        } catch (java.io.IOException e) {
            trouble = true;
        }
    }

    // ----- gestures -----

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        layout();
        if (host != null && host.modal()) {
            endStroke(false);
            host.touchOver(e.getActionMasked(), e.getX(), e.getY());
            return true;
        }
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                twoFingers = false;
                held = false;
                removeCallbacks(hold);
                stopEdge();
                miniRect(dst);
                onMini = dst.contains(e.getX(), e.getY());
                inPalette = !onMini && e.getX() >= PALETTE_X * unit;
                inTools = e.getX() < canvasLeft();
                if (inPalette) {
                    listDown(e.getX(), e.getY(), e.getEventTime());
                }
                if (onMini) {
                    lookAt(e.getX(), e.getY());
                }
                markX = -1;
                inStrip = !onMini && !inPalette && !inTools && inStrip(e.getX(), e.getY());
                if (inStrip) {
                    stripDown(e.getX());
                }
                if (onMini || inPalette || inTools || inStrip || tilePx <= 0f) {
                    break;
                }
                downX = lastX = e.getX();
                downY = lastY = e.getY();
                if (pendingFill != null) {
                    // A fill waits: the canvas takes no tool; a tap on it drops the fill, two fingers still move the view.
                    dropTap = true;
                    break;
                }
                postDelayed(hold, HOLD_MILLIS);
                if (tool == TOOL_FILL || tool == TOOL_PICK) {
                    tapPending = true;
                    tapX = e.getX();
                    tapY = e.getY();
                } else if (tool != TOOL_TERRAIN) {
                    paintBegan = e.getEventTime();
                    unitDown(e.getX(), e.getY());
                } else if (terrain != null) {
                    painting = terrain.begin(map, System.nanoTime());
                    paintBegan = e.getEventTime();
                    paintX = (e.getX() - canvasLeft() + originX) / tilePx;
                    paintY = (e.getY() + originY) / tilePx;
                    dab(e.getX(), e.getY());
                }
                break;
            case MotionEvent.ACTION_POINTER_DOWN:
                if (e.getPointerCount() == 2) {
                    removeCallbacks(hold);
                    stopEdge();
                    held = false;
                    // A second finger soon after the first takes the begun stroke back.
                    endStroke(e.getEventTime() - paintBegan > 150);
                    unitUp(false);
                    tapPending = false;
                    inStrip = false;
                    inPalette = false;
                    listDragging = false;
                    inTools = false;
                    // Two fingers are always the view, whatever the first finger had begun.
                    twoFingers = true;
                    onMini = false;
                    lastMidX = (e.getX(0) + e.getX(1)) / 2f;
                    lastMidY = (e.getY(0) + e.getY(1)) / 2f;
                    lastSpan = span(e);
                }
                break;
            case MotionEvent.ACTION_MOVE:
                if (!held && Math.hypot(e.getX() - downX, e.getY() - downY) > TAP_SLOP * unit) {
                    removeCallbacks(hold);
                }
                if (twoFingers && e.getPointerCount() >= 2) {
                    final float midX = (e.getX(0) + e.getX(1)) / 2f, midY = (e.getY(0) + e.getY(1)) / 2f;
                    final float now = span(e);
                    // The map point under the middle of the fingers stays under it.
                    final float mapX = (lastMidX - canvasLeft() + originX) / tilePx;
                    final float mapY = (lastMidY + originY) / tilePx;
                    if (lastSpan > 0f && now > 0f) {
                        final float least = wholeTile(), most = Math.max(least, MAX_TILE * unit);
                        tilePx = Math.max(least, Math.min(most, tilePx * now / lastSpan));
                    }
                    originX = mapX * tilePx - (midX - canvasLeft());
                    originY = mapY * tilePx - midY;
                    lastMidX = midX;
                    lastMidY = midY;
                    lastSpan = now;
                    invalidate();
                } else if (onMini) {
                    lookAt(e.getX(), e.getY());
                } else if (inStrip) {
                    stripMove(e.getX());
                } else if (inPalette) {
                    listMove(e.getY(), e.getEventTime());
                } else if (held) {
                    // A long press has acted: the finger does nothing more until it is lifted.
                } else if (painting != null && !twoFingers) {
                    dab(e.getX(), e.getY());
                    edgeCheck(e.getX(), e.getY());
                } else if (tapPending) {
                    tapPending = Math.hypot(e.getX() - tapX, e.getY() - tapY) <= TAP_SLOP * unit;
                } else if (!twoFingers && !inPalette && !inTools && !inStrip && !terrainPalette()) {
                    unitMove(e.getX(), e.getY());
                    edgeCheck(e.getX(), e.getY());
                }
                break;
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                // The finger left of a two-finger gesture does nothing until all are lifted.
                if (e.getActionMasked() != MotionEvent.ACTION_POINTER_UP) {
                    removeCallbacks(hold);
                    stopEdge();
                    held = false;
                    if (dropTap && !twoFingers && e.getActionMasked() == MotionEvent.ACTION_UP) {
                        dropPending();
                    }
                    dropTap = false;
                    endStroke(e.getActionMasked() == MotionEvent.ACTION_UP);
                    unitUp(e.getActionMasked() == MotionEvent.ACTION_UP && !twoFingers);
                    if (tapPending && !twoFingers && e.getActionMasked() == MotionEvent.ACTION_UP) {
                        tapPending = false;
                        if (tool == TOOL_FILL) {
                            fillAt(tapX, tapY);
                        } else {
                            pickAt(tapX, tapY);
                        }
                    }
                    tapPending = false;
                    if (inPalette && !twoFingers && e.getActionMasked() == MotionEvent.ACTION_UP) {
                        if (terrainPalette()) {
                            paletteTap(e.getX(), e.getY());
                        } else if (!listScrolled) {
                            unitPaletteTap(e.getX(), e.getY());
                        }
                    }
                    if (inTools && !twoFingers && e.getActionMasked() == MotionEvent.ACTION_UP
                        && !toolTap(e.getX(), e.getY()) && !showTap(e.getX(), e.getY())
                        && !balanceTap(e.getX(), e.getY())) {
                        mirrorTap(e.getX(), e.getY());
                    }
                    if (inStrip && !twoFingers && !chipDragging && e.getActionMasked() == MotionEvent.ACTION_UP) {
                        stripTap(e.getX(), e.getY());
                    }
                    if (inPalette) {
                        listUp(e.getActionMasked() == MotionEvent.ACTION_UP && !twoFingers);
                    }
                    inStrip = false;
                    inPalette = false;
                    inTools = false;
                    twoFingers = false;
                    onMini = false;
                }
                break;
            default:
                break;
        }
        return true;
    }

    private static float span(MotionEvent e) {
        return (float) Math.hypot(e.getX(0) - e.getX(1), e.getY(0) - e.getY(1));
    }

    /** Centre the view on the map point shown at a point of the minimap. */
    private void lookAt(float x, float y) {
        miniRect(dst);
        final float fx = (x - dst.left) / dst.width(), fy = (y - dst.top) / dst.height();
        originX = fx * map.width() * tilePx - canvasWidth() / 2f;
        originY = fy * map.height() * tilePx - canvasHeight() / 2f;
        invalidate();
    }
}
