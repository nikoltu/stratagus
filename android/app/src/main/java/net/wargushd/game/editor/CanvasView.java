package net.wargushd.game.editor;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
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

    /** The still pictures of the palette's units, in the order of UnitFrames.TYPES; null where there is none. */
    private final java.util.concurrent.atomic.AtomicReferenceArray<UnitArt.Picture> pictures =
        new java.util.concurrent.atomic.AtomicReferenceArray<UnitArt.Picture>(UnitFrames.TYPES.length);
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
    private int tool = TOOL_TERRAIN, unitKind = MapRules.KIND_MINE, owner;
    /** The unit chosen with Select, or -1. */
    private int selected = -1;
    /** The unit under the finger: its type, or -1; its top left tile; the unit being moved, or -1. */
    private int ghostType = -1, ghostX, ghostY, moving = -1;
    private float grabX, grabY;
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
        new Thread(new Runnable() {
            @Override
            public void run() {
                load();
                postInvalidate();
            }
        }, "editor-terrain").start();
        new Thread(new Runnable() {
            @Override
            public void run() {
                loadUnits();
            }
        }, "editor-units").start();
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
        watch.close();
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
     * Read the still pictures of the palette's units for the map's tileset, one after the
     * other; a unit whose picture is not found keeps its box.
     */
    private void loadUnits() {
        final long began = System.nanoTime();
        final String names = UnitArt.text(new File(dataDir, "hd_units_map.json"));
        final int era = Math.max(0, Math.min(3, map.tileset()));
        int found = 0;
        long held = 0L;
        for (int i = 0; i < UnitFrames.TYPES.length && !released; ++i) {
            final UnitArt.Picture p = UnitArt.load(gameFolder, names, UnitFrames.TYPES[i], era);
            if (p == null) {
                continue;
            }
            pictures.set(i, p);
            if (released) {
                final UnitArt.Picture mine = pictures.getAndSet(i, null);
                if (mine != null) {
                    mine.recycle();
                }
                return;
            }
            ++found;
            held += p.bytes();
            postInvalidate();
        }
        android.util.Log.i("WargusHD", "LAUNCHER editor unit pictures " + found + " of " + UnitFrames.TYPES.length
            + ", " + held + " bytes, " + (System.nanoTime() - began) / 1000000L + " ms");
    }

    /** The still picture of a unit type, or null. */
    private UnitArt.Picture picture(int type) {
        final int i = UnitFrames.indexOf(type);
        return (i < 0) ? null : pictures.get(i);
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
            final int size = Terrain.BRUSHES[brush];
            final float bx = Terrain.brushStart(paintX, size) - 0.5f, by = Terrain.brushStart(paintY, size) - 0.5f;
            stroke.setColor(VIEW_BOX);
            stroke.setStrokeWidth(Math.max(2f, 4f * unit));
            c.drawRect(left + bx * tilePx - originX, by * tilePx - originY,
                       left + (bx + size) * tilePx - originX, (by + size) * tilePx - originY, stroke);
        }
        drawShows(c, left);
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

    /** The kinds in the order of the palette's cells; the last two cells are the walls, not yet to be had. */
    private static final int[] CELL_KIND = { Terrain.WATER, Terrain.DEEP_WATER, Terrain.COAST, Terrain.GRASS,
        Terrain.DARK_GRASS, Terrain.FOREST, Terrain.ROCKS, -1, -1 };
    /** A solid tile of each cell, for its picture. */
    private static final int[] CELL_TILE = { 0x10, 0x20, 0x30, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0 };
    // Rects of the palette, logical units; y from the view's top (the screen's y less the bar's 168).
    private static final float PAL_X = 3168f, PAL_Y = 348f - 168f, PAL_SIDE = 192f, PAL_PITCH_X = 216f, PAL_PITCH_Y = 264f;
    private static final float BRUSH_X = 3150f, BRUSH_Y = 1206f - 168f, BRUSH_W = 160.5f, BRUSH_H = 96f, BRUSH_PITCH = 166.5f;
    private static final int DIM = 0xB00E0B08, CELL_FILL = 0x10FFFFFF, CELL_CHOSEN = 0x36F0D250;
    /** The cells' names, in the launcher's own words. */
    private static final String[] CELL_NAME = { "Water", "Deep water", "Coast", "Grass", "Dark grass", "Forest",
        "Rocks", "Human wall", "Orc wall" };
    private static final float NAME_X = 3162f, NAME_GAP = 6f, NAME_W = 204f, NAME_H = 48f, CAP_NAME = 24f;
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
        final float x = (PAL_X + (i % 3) * PAL_PITCH_X + (PAL_SIDE - palSide) / 2f) * unit;
        final float y = (palY + (i / 3) * palPitch) * unit;
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
                host.text(c, host.word(CELL_NAME[i]), NAME_X + (i % 3) * PAL_PITCH_X,
                          palY + (i / 3) * palPitch + palSide + NAME_GAP,
                          NAME_W, NAME_H, CAP_NAME, chosen ? VIEW_BOX : (CELL_KIND[i] < 0) ? INK_MUTED : INK, true);
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
                host.text(c, (i == 0) ? host.word(MIRROR_NAME[i]) : MIRROR_NAME[i], mirrorX(i), mirrorY(i), mirrorW(),
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
            if (painting.dab(px, py, kind, size, mirror)) {
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
        if (shows[SHOW_BUILDABLE]) {
            return MapRules.buildable(tile) ? PASS_LAND : PASS_NONE;
        }
        if (shows[SHOW_PASSABLE]) {
            return MapRules.land(tile) ? PASS_LAND : MapRules.water(tile) ? PASS_WATER : PASS_NONE;
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
        if (set == null || tilePx <= 0f) {
            return;
        }
        endStroke(true);
        final Terrain.Stroke s = set.begin(map, System.nanoTime());
        if (s.fill((x - canvasLeft() + originX) / tilePx, (y + originY) / tilePx, kind)) {
            painting = s;
            endStroke(true);
            if (host != null) {
                host.click();
            }
        }
    }

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
        final int unitIs = (at >= 0) ? MapRules.kindOf(map.unit(at).type) : -1;
        if (unitIs >= 0) {
            unitKind = unitIs;
            if (map.unit(at).owner < MapRules.PLAYERS) {
                owner = map.unit(at).owner;
            }
            tool = TOOL_UNITS;
        } else {
            final int found = Terrain.kindAt(map.tile(tx, ty), mx - tx, my - ty);
            boolean had = false;
            for (int k : CELL_KIND) {
                had |= k >= 0 && k == found;
            }
            if (!had) {
                return;     // a kind the palette has not: nothing is taken
            }
            kind = found;
            tool = TOOL_TERRAIN;
        }
        selected = -1;
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
    private static final float OWNER_KEY_H = 40f, OWNER_GAP = 48f, OWNER_PITCH = 108f;
    // The strip of the chosen unit at the canvas' foot; x in logical units, y from the strip's top.
    private static final float STRIP_X = 288f, STRIP_W = 2808f, STRIP_H = 120f, STRIP_UP = 144f;
    private static final float MINUS_X = 1740f, AMOUNT_X = 1872f, AMOUNT_W = 360f, PLUS_X = 2244f, STEP_W = 120f;
    private static final float REMOVE_X = 2748f, REMOVE_W = 324f, STRIP_CELL_Y = 12f, STRIP_CELL_H = 96f;

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
                    selected = -1;
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

    private int ownerRace() { return map.slot(MapModel.FIELD_RACE, owner); }

    /**
     * The name of a unit kind: the game's own (its string table names a kind by the kind's
     * number, unit_N), the launcher's word for a start location and for a kind without a name.
     */
    private static String nameOf(Host host, int type) {
        if (MapRules.isStart(type)) {
            return host.word("Start Location");
        }
        final String name = host.term("unit_" + type, plainName(type));
        return name.isEmpty() ? host.word("Unit") : name;
    }

    /** The English name of a kind of the palette, shown where the game's table has no text. */
    private static String plainName(int type) {
        switch (type) {
            case MapRules.GOLD_MINE: return "Gold Mine";
            case MapRules.OIL_PATCH: return "Oil Patch";
            case MapRules.PEASANT: return "Peasant";
            case MapRules.PEON: return "Peon";
            case MapRules.TOWN_HALL: return "Town Hall";
            case MapRules.GREAT_HALL: return "Great Hall";
            default: return "";
        }
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

    /** The player cells: two rows of four, one under the other, under the word "Player". */
    private float ownerY(int i) { return palY + 2f * palPitch + OWNER_GAP + (i / 4) * OWNER_PITCH; }

    private void ownerCell(int i, RectF r) {
        final float x = (BRUSH_X + (i % 4) * BRUSH_PITCH) * unit, y = ownerY(i) * unit;
        r.set(x, y, x + BRUSH_W * unit, y + BRUSH_H * unit);
    }

    private void drawUnitPalette(Canvas c) {
        for (int i = 0; i < MapRules.KINDS; ++i) {
            paletteCell(i, dst);
            final int type = MapRules.typeOf(i, ownerRace());
            final boolean chosen = i == unitKind;
            cell(c, dst, chosen);
            final float cx = dst.centerX(), cy = dst.centerY();
            final UnitArt.Picture p = picture(type);
            if (p != null) {
                // The kind's still picture, as large as the cell takes it, in the chosen player's colour.
                UnitFrames.fit(p.bodyAt, cx, cy, dst.width(), PICTURE_ROOM, spot);
                art.draw(c, p, spot[0], spot[1], spot[2], (host != null) ? host.colour(owner) : OTHER);
                if (host != null) {
                    host.text(c, nameOf(host, type), NAME_X + (i % 3) * PAL_PITCH_X,
                              palY + (i / 3) * palPitch + palSide + NAME_GAP, NAME_W, NAME_H, CAP_NAME,
                              chosen ? VIEW_BOX : INK, true);
                }
                continue;
            }
            // Without a picture: the kind as its footprint in the colour it has on the map.
            final float side = dst.width() * (MapRules.footprint(type) + 1) / 12f;
            fill.setColor((i == MapRules.KIND_MINE) ? MINE : (i == MapRules.KIND_PATCH) ? PATCH
                : (host != null) ? host.colour(owner) : OTHER);
            stroke.setColor(INK);
            stroke.setStrokeWidth(Math.max(1f, 2f * unit));
            if (i == MapRules.KIND_START) {
                c.drawCircle(cx, cy, side, fill);
                c.drawCircle(cx, cy, side, stroke);
            } else {
                c.drawRect(cx - side, cy - side, cx + side, cy + side, fill);
                c.drawRect(cx - side, cy - side, cx + side, cy + side, stroke);
            }
            if (host != null) {
                host.text(c, nameOf(host, type), NAME_X + (i % 3) * PAL_PITCH_X,
                          palY + (i / 3) * palPitch + palSide + NAME_GAP, NAME_W, NAME_H, CAP_NAME,
                          chosen ? VIEW_BOX : INK, true);
            }
        }
        if (host != null) {
            host.text(c, host.word("Player"), BRUSH_KEY_X, palY + 2f * palPitch, PALETTE_W, OWNER_KEY_H, CAP_KEY,
                      INK_MUTED, false);
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
        }
        stroke.setColor(LINE);
        stroke.setStrokeWidth(Math.max(1f, 2f * unit));
    }

    private void unitPaletteTap(float x, float y) {
        boolean chose = false;
        for (int i = 0; i < MapRules.KINDS; ++i) {
            paletteCell(i, dst);
            if (dst.contains(x, y)) {
                chose = unitKind != i || tool != TOOL_UNITS;
                unitKind = i;
                tool = TOOL_UNITS;
                selected = -1;
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

    /** The unit the ghost does not collide with: the one being moved. */
    private int ghostLeftOut() { return moving; }

    private void drawUnits(Canvas c, float left) {
        // The start locations last: one may lie under a hall or a worker.
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < map.unitCount(); ++i) {
                if (i != moving && MapRules.isStart(map.unit(i).type) == (pass == 1)) {
                    drawUnit(c, left, map.unit(i), i == selected);
                }
            }
        }
        if (ghostType >= 0) {
            // The footprint under the finger, tile by tile: green where the unit may stand, red where not.
            final int size = MapRules.footprint(ghostType), leftOut = ghostLeftOut();
            for (int yy = ghostY; yy < ghostY + size; ++yy) {
                for (int xx = ghostX; xx < ghostX + size; ++xx) {
                    fill.setColor((MapRules.tileFits(map, ghostType, xx, yy)
                                   && (MapRules.isStart(ghostType) || !MapRules.taken(map, xx, yy, leftOut)))
                        ? GHOST_OK : GHOST_NO);
                    c.drawRect(left + xx * tilePx - originX, yy * tilePx - originY,
                               left + (xx + 1) * tilePx - originX, (yy + 1) * tilePx - originY, fill);
                }
            }
            stroke.setColor(MapRules.mayPlace(map, ghostType, ghostX, ghostY, leftOut) ? 0xFF30C050 : 0xFFD03030);
            stroke.setStrokeWidth(Math.max(2f, 4f * unit));
            c.drawRect(left + ghostX * tilePx - originX, ghostY * tilePx - originY,
                       left + (ghostX + size) * tilePx - originX, (ghostY + size) * tilePx - originY, stroke);
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

    private boolean stripShown() { return tool == TOOL_SELECT && selected >= 0 && selected < map.unitCount(); }

    private float stripY() { return getHeight() / unit - STRIP_UP; }

    private void drawStrip(Canvas c) {
        if (!stripShown() || host == null) {
            return;
        }
        final MapModel.Unit u = map.unit(selected);
        final float y = stripY();
        fill.setColor(COLUMN);
        c.drawRect(STRIP_X * unit, y * unit, (STRIP_X + STRIP_W) * unit, (y + STRIP_H) * unit, fill);
        stroke.setColor(LINE);
        stroke.setStrokeWidth(Math.max(1f, 2f * unit));
        c.drawRect(STRIP_X * unit, y * unit, (STRIP_X + STRIP_W) * unit, (y + STRIP_H) * unit, stroke);
        final String who = (u.owner < MapRules.PLAYERS) ? host.word("Player") + " " + (u.owner + 1)
                                                        : host.word("Neutral");
        host.text(c, nameOf(host, u.type) + " - " + who, STRIP_X + 36f, y, MINUS_X - STRIP_X - 72f, STRIP_H,
                  CAP_CELL, INK, false);
        final float cy = y + STRIP_CELL_Y;
        if (MapRules.isResource(u.type)) {
            stripCell(c, MINUS_X, cy, STEP_W, "-");
            host.text(c, String.valueOf(u.data * MapModel.RESOURCE_UNIT), AMOUNT_X, cy, AMOUNT_W, STRIP_CELL_H,
                      CAP_CELL, VIEW_BOX, true);
            stripCell(c, PLUS_X, cy, STEP_W, "+");
        }
        stripCell(c, REMOVE_X, cy, REMOVE_W, host.word("Remove"));
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
        final MapModel.Unit u = map.unit(selected);
        MapModel.Step step = null;
        if (x >= REMOVE_X && x < REMOVE_X + REMOVE_W) {
            step = MapRules.erase(map, selected);
            selected = -1;
        } else if (MapRules.isResource(u.type) && x >= MINUS_X && x < MINUS_X + STEP_W) {
            step = MapRules.amount(map, selected, -1);
        } else if (MapRules.isResource(u.type) && x >= PLUS_X && x < PLUS_X + STEP_W) {
            step = MapRules.amount(map, selected, 1);
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
            ghostType = MapRules.typeOf(unitKind, ownerRace());
            moving = -1;
            grabX = grabY = (MapRules.footprint(ghostType) - 1) / 2f;
            unitMove(x, y);
        } else if (tool == TOOL_SELECT) {
            final int at = MapRules.unitAt(map, tx, ty);
            if (at != selected && host != null) {
                host.click();
            }
            selected = at;
            moving = -1;
            ghostType = -1;
            if (at >= 0) {
                // Moved only once the finger leaves the tile it came down on.
                grabX = tx - map.unit(at).x;
                grabY = ty - map.unit(at).y;
                ghostX = map.unit(at).x;
                ghostY = map.unit(at).y;
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
        } else if (tool == TOOL_SELECT && selected >= 0 && selected < map.unitCount()) {
            final int nx = tx - (int) grabX, ny = ty - (int) grabY;
            if (moving < 0 && (nx != ghostX || ny != ghostY)) {
                moving = selected;
                ghostType = map.unit(selected).type;
            }
            if (moving >= 0) {
                ghostX = nx;
                ghostY = ny;
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
        final int type = ghostType, was = moving;
        ghostType = -1;
        moving = -1;
        if (erasePending) {
            erasePending = false;
            if (keep) {
                eraseAt(eraseX, eraseY);
            }
        }
        if (keep && type >= 0) {
            final MapModel.Step step = (was >= 0) ? MapRules.move(map, was, ghostX, ghostY)
                : MapRules.place(map, unitKind, owner, ghostX, ghostY);
            if (step != null) {
                apply(step);
                if (host != null) {
                    host.click();
                }
            }
        }
        invalidate();
    }

    private void eraseAt(int tx, int ty) {
        final int at = MapRules.unitAt(map, tx, ty);
        if (at >= 0) {
            apply(MapRules.erase(map, at));
            if (host != null) {
                host.click();
            }
        }
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
        selected = -1;
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
                miniRect(dst);
                onMini = dst.contains(e.getX(), e.getY());
                inPalette = !onMini && e.getX() >= PALETTE_X * unit;
                inTools = e.getX() < canvasLeft();
                if (onMini) {
                    lookAt(e.getX(), e.getY());
                }
                markX = -1;
                inStrip = !onMini && !inPalette && !inTools && inStrip(e.getX(), e.getY());
                if (onMini || inPalette || inTools || inStrip || tilePx <= 0f) {
                    break;
                }
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
                    // A second finger soon after the first takes the begun stroke back.
                    endStroke(e.getEventTime() - paintBegan > 150);
                    unitUp(false);
                    tapPending = false;
                    inStrip = false;
                    inPalette = false;
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
                } else if (painting != null && !twoFingers) {
                    dab(e.getX(), e.getY());
                } else if (tapPending) {
                    tapPending = Math.hypot(e.getX() - tapX, e.getY() - tapY) <= TAP_SLOP * unit;
                } else if (!twoFingers && !inPalette && !inTools && !inStrip && !terrainPalette()) {
                    unitMove(e.getX(), e.getY());
                }
                break;
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                // The finger left of a two-finger gesture does nothing until all are lifted.
                if (e.getActionMasked() != MotionEvent.ACTION_POINTER_UP) {
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
                        } else {
                            unitPaletteTap(e.getX(), e.getY());
                        }
                    }
                    if (inTools && !twoFingers && e.getActionMasked() == MotionEvent.ACTION_UP
                        && !toolTap(e.getX(), e.getY()) && !showTap(e.getX(), e.getY())
                        && !balanceTap(e.getX(), e.getY())) {
                        mirrorTap(e.getX(), e.getY());
                    }
                    if (inStrip && !twoFingers && e.getActionMasked() == MotionEvent.ACTION_UP) {
                        stripTap(e.getX(), e.getY());
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
