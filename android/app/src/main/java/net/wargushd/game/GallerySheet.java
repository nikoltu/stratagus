package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

/**
 * The portrait gallery, opened from the profile sheet: the chosen portrait large at the
 * left, every character portrait of the game at the right, all in the profile's colour.
 */
final class GallerySheet {
    static final int NONE = 0, CANCEL = 1, OK = 2;

    /** Every character portrait of the game's atlas: the unit slots, then the heroes. */
    static final int[] SLOTS = new int[48];
    static {
        for (int i = 0; i < 38; ++i) {
            SLOTS[i] = i;
        }
        for (int i = 0; i < 10; ++i) {
            SLOTS[38 + i] = 186 + i;
        }
    }

    // Layout, logical units: the kit's wide modal sheet, centred, inner padding 96.
    private static final float W = 3120f, H = 1560f, PAD = 96f;
    private static final float FACE_W = 756f, FACE_H = 624.5f;
    private static final float CELL_W = 207f, CELL_H = 171f, CELL_DX = 243f, CELL_DY = 207f;
    private static final int COLUMNS = 8;
    private static final float BUTTON_W = 720f, BUTTON_H = 120f, GAP = 48f;
    private static final float CAP_BODY = 44f, CAP_MID = 36f;

    private static final int SHADE = 0xAA000000, PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E;
    private static final int WELL = 0x78000000, PART_FAINT = 0x0EFFFFFF;
    private static final int INK = 0xFFE8E2D4, GOLD = 0xFFF0D250, INK_ON_GOLD = 0xFF1E1608;

    private final Bitmap[] cells = new Bitmap[SLOTS.length];
    private Bitmap frame, buttonPlain, buttonMarked;
    private int color = -1;
    /** Index into SLOTS of the chosen portrait. */
    private int selected;
    /** Fixed / Random (design 22.2): Random draws a new portrait at each launcher start. */
    private boolean random;
    /** Slot -> key of the game's string that names it (PortraitNames); null until read. */
    private volatile java.util.Map<Integer, String> nameKeys;
    /** Counts openings, so that a tinting run started for an earlier one stops. */
    private int run;

    /**
     * Show the gallery for a colour with a portrait chosen. The portraits are tinted off the
     * drawing thread; `redraw` is called from there as they become ready.
     */
    void open(final SkinKit kit, final int colorIndex, int slot, boolean randomMode, final java.io.File dataDir,
              final Runnable redraw) {
        random = randomMode;
        if (nameKeys == null) {
            new Thread(new Runnable() {
                @Override
                public void run() {
                    nameKeys = PortraitNames.read(dataDir);
                    redraw.run();
                }
            }, "gallery-names").start();
        }
        selected = 0;
        for (int i = 0; i < SLOTS.length; ++i) {
            if (SLOTS[i] == slot) {
                selected = i;
            }
        }
        if (colorIndex == color) {
            return;
        }
        color = colorIndex;
        final int mine = ++run;
        java.util.Arrays.fill(cells, null);
        new Thread(new Runnable() {
            @Override
            public void run() {
                // The chosen one first: it is also the preview.
                final int first = selected;
                final long began = android.os.SystemClock.uptimeMillis();
                cells[first] = Portraits.tinted(kit, SLOTS[first], Profiles.COLORS[colorIndex]);
                redraw.run();
                final Bitmap[] all = Portraits.tintedAll(kit, SLOTS, Profiles.COLORS[colorIndex]);
                if (mine == run) {
                    System.arraycopy(all, 0, cells, 0, cells.length);
                    redraw.run();
                }
                if (mine == run) {
                    android.util.Log.i("WargusHD", "LAUNCHER gallery " + SLOTS.length + " portraits in "
                        + (android.os.SystemClock.uptimeMillis() - began) + " ms");
                }
            }
        }, "gallery").start();
    }

    /** The slot of the chosen portrait. */
    int slot() {
        return SLOTS[selected];
    }

    /** True when Random is chosen. */
    boolean random() {
        return random;
    }

    private float x() { return (Screen.CANVAS_W - W) / 2f; }
    private float y(SkinKit kit) { return kit.height / 2f - H / 2f; }
    private float px() { return x() + PAD; }
    private float py(SkinKit kit) { return y(kit) + PAD; }
    private float gx() { return px() + FACE_W + 144f; }
    private float cellX(int i) { return gx() + CELL_DX * (i % COLUMNS); }
    private float cellY(SkinKit kit, int i) { return py(kit) + CELL_DY * (i / COLUMNS); }
    private static final float MODE_H = 108f;
    private float modeY(SkinKit kit) { return py(kit) + 896f; }
    private float buttonY(SkinKit kit) { return y(kit) + H - PAD - BUTTON_H; }
    private float okX() { return x() + W - PAD - BUTTON_W; }
    private float cancelX() { return okX() - GAP - BUTTON_W; }

    void draw(Canvas c, SkinKit kit) {
        if (frame == null) {
            frame = kit.atlasFrame("Modern_HUD_Widgets", "portrait_button", 80, 80);
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        kit.fill(c, x(), y(kit), W, H, PANEL_FILL, 18f);
        kit.outline(c, x(), y(kit), W, H, LINE, 18f);

        final float px = px(), py = py(kit);
        kit.textTop(c, kit.own("Portrait"), px, py, CAP_BODY, GOLD, true);
        kit.nine(c, frame, px - 12f, py + 84f, FACE_W + 24f, FACE_H + 23.5f, 25);
        kit.picture(c, cells[selected], px, py + 96f, FACE_W, FACE_H);

        // The game's name of the unit or hero (table pname: [456, 1296, 756, 44], cap 44).
        final java.util.Map<Integer, String> names = nameKeys;
        final String key = (names != null) ? names.get(SLOTS[selected]) : null;
        if (key != null) {
            kit.textTop(c, kit.fit(kit.string(key, ""), CAP_BODY, FACE_W), px, py + 780f, CAP_BODY, INK, true);
        }

        // One choice of two: Fixed / Random.
        final float my = modeY(kit), mh = MODE_H, pw = FACE_W / 2f;
        kit.fill(c, px, my, FACE_W, mh, WELL, 14f);
        kit.outline(c, px, my, FACE_W, mh, LINE, 14f);
        kit.fill(c, px + (random ? pw : 0f) + 6f, my + 6f, pw - 12f, mh - 12f, GOLD, 10f);
        kit.fill(c, px + (random ? 0f : pw) + 6f, my + 6f, pw - 12f, mh - 12f, PART_FAINT, 10f);
        kit.textCentre(c, kit.own("Fixed"), px + 6f, my + 6f, pw - 12f, mh - 12f, CAP_MID,
                       random ? INK : INK_ON_GOLD, random);
        kit.textCentre(c, kit.own("Random"), px + pw + 6f, my + 6f, pw - 12f, mh - 12f, CAP_MID,
                       random ? INK_ON_GOLD : INK, !random);

        kit.fill(c, cellX(selected) - 12f, cellY(kit, selected) - 12f, CELL_W + 24f, CELL_H + 24f, GOLD,
                 10f);
        for (int i = 0; i < SLOTS.length; ++i) {
            kit.picture(c, cells[i], cellX(i), cellY(kit, i), CELL_W, CELL_H);
        }

        final float by = buttonY(kit);
        kit.nine(c, buttonPlain, cancelX(), by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.string("common_cancel", "Cancel"), cancelX(), by, BUTTON_W, BUTTON_H,
                       CAP_BODY, INK, true);
        kit.nine(c, buttonMarked, okX(), by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.string("common_ok", "OK"), okX(), by, BUTTON_W, BUTTON_H, CAP_BODY, GOLD,
                       true);
    }

    private static boolean in(float x, float y, float rx, float ry, float rw, float rh) {
        return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
    }

    /** A finger was lifted at (x, y), in logical units. */
    int up(float x, float y, SkinKit kit) {
        if (in(x, y, px(), modeY(kit), FACE_W, MODE_H)) {
            random = x >= px() + FACE_W / 2f;
            return NONE;
        }
        for (int i = 0; i < SLOTS.length; ++i) {
            if (in(x, y, cellX(i), cellY(kit, i), CELL_W, CELL_H)) {
                selected = i;
                return NONE;
            }
        }
        if (in(x, y, cancelX(), buttonY(kit), BUTTON_W, BUTTON_H)) {
            return CANCEL;
        }
        if (in(x, y, okX(), buttonY(kit), BUTTON_W, BUTTON_H)) {
            return OK;
        }
        return NONE;
    }
}
