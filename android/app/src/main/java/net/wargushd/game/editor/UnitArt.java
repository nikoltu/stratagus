package net.wargushd.game.editor;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.BitmapRegionDecoder;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.nio.ByteBuffer;

/**
 * The still pictures of the units of the editor's palette, read at run time from the unit
 * atlases of the player's game folder. Only the rect of the one frame is decoded from a sheet,
 * at half size; nothing is written to the disk.
 *
 * A picture is the body and, where the unit carries its player's colour, the mask of it: the
 * mask's brightness becomes its opacity and it is drawn over the body in the player's colour,
 * as the engine draws its team mask.
 */
final class UnitArt {
    /** A frame is decoded at this reduction. */
    static final int REDUCE = 2;

    /** One unit's still picture. */
    static final class Picture {
        Bitmap body, mask;
        /** The frame records of UnitFrames.frame, in pixels of the sheet. */
        int[] bodyAt, maskAt;

        long bytes() {
            return ((body != null) ? body.getByteCount() : 0) + ((mask != null) ? mask.getByteCount() : 0);
        }

        void recycle() {
            if (body != null) {
                body.recycle();
            }
            if (mask != null) {
                mask.recycle();
            }
        }
    }

    private final Paint image = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint tint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final RectF to = new RectF();
    private final float[] edges = new float[4];

    /**
     * Draw a picture with its own canvas centred on (cx, cy) at s screen pixels a pixel of the
     * art; the mask in the given colour. A colour of 0 leaves the mask out.
     */
    void draw(Canvas c, Picture p, float cx, float cy, float s, int colour) {
        UnitFrames.place(p.bodyAt, cx, cy, s, edges);
        to.set(edges[0], edges[1], edges[2], edges[3]);
        c.drawBitmap(p.body, null, to, image);
        if (p.mask != null && colour != 0) {
            UnitFrames.place(p.maskAt, cx, cy, s, edges);
            to.set(edges[0], edges[1], edges[2], edges[3]);
            tint.setColor(colour | 0xFF000000);
            c.drawBitmap(p.mask, null, to, tint);
        }
    }

    /**
     * What one loading run keeps between pictures (Session 2026-10-19 d): the atlas texts read
     * and the one sheet opened last, so that the pictures of one atlas, asked one after the other
     * as the palette's groups are, open it once. Used by one thread at a time.
     */
    static final class Sheets {
        private final File dir;
        private final java.util.HashMap<String, String> texts = new java.util.HashMap<String, String>();
        private String openName;
        private BitmapRegionDecoder open;

        Sheets(File gameFolder) { dir = new File(gameFolder, "Data/Art/hd/unit"); }

        /** The text of ATLAS.json, or null; kept for the run. */
        String text(String atlas) {
            if (!texts.containsKey(atlas)) {
                texts.put(atlas, UnitArt.text(new File(dir, atlas + ".json")));
            }
            return texts.get(atlas);
        }

        /** One rect of ATLAS.png, decoded alone at REDUCE; null when it cannot be. */
        @SuppressWarnings("deprecation")
        Bitmap region(String atlas, int[] at) {
            try {
                if (!atlas.equals(openName)) {
                    closeSheet();
                    final File png = new File(dir, atlas + ".png");
                    if (!png.isFile()) {
                        return null;
                    }
                    open = BitmapRegionDecoder.newInstance(png.getPath(), false);
                    openName = atlas;
                }
                if (open == null || at[0] < 0 || at[1] < 0 || at[0] + at[2] > open.getWidth()
                    || at[1] + at[3] > open.getHeight()) {
                    return null;
                }
                final BitmapFactory.Options o = new BitmapFactory.Options();
                o.inSampleSize = REDUCE;
                o.inPreferredConfig = Bitmap.Config.ARGB_8888;
                return open.decodeRegion(new Rect(at[0], at[1], at[0] + at[2], at[1] + at[3]), o);
            } catch (java.io.IOException e) {
                return null;
            }
        }

        private void closeSheet() {
            if (open != null) {
                open.recycle();
            }
            open = null;
            openName = null;
        }

        /** Free the open sheet and the texts; the next picture reads them again. */
        void close() {
            closeSheet();
            texts.clear();
        }
    }

    /**
     * The picture of a unit type on a tileset, or null when any part of the way to it is
     * missing. Frame 0 of the type's family, found as the engine finds it (UnitFrames.frameOf).
     */
    static Picture load(Sheets sheets, String mapJson, int type, int tileset) {
        try {
            final String[] entry = UnitFrames.entry(mapJson, UnitFrames.file(type, tileset));
            if (entry == null) {
                return null;
            }
            final Picture p = new Picture();
            p.bodyAt = UnitFrames.frameOf(sheets.text(entry[0] + "_sprites"), entry[1], 0, "", tileset);
            if (p.bodyAt == null) {
                return null;
            }
            p.body = sheets.region(entry[0] + "_sprites", p.bodyAt);
            if (p.body == null) {
                return null;
            }
            // The mask is there for some units only.
            p.maskAt = UnitFrames.frameOf(sheets.text(entry[0] + "_masks"), entry[1], 0, "_team", tileset);
            if (p.maskAt != null) {
                final Bitmap lit = sheets.region(entry[0] + "_masks", p.maskAt);
                p.mask = (lit == null) ? null : opacity(lit);
            }
            return p;
        } catch (RuntimeException e) {
            return null;
        } catch (OutOfMemoryError e) {
            return null;
        }
    }

    /** A mask's brightness as opacity alone (77 red + 150 green + 29 blue, of 256, as the engine weighs it). */
    private static Bitmap opacity(Bitmap lit) {
        final int w = lit.getWidth(), h = lit.getHeight();
        final Bitmap out = Bitmap.createBitmap(w, h, Bitmap.Config.ALPHA_8);
        final int row = out.getRowBytes();
        final byte[] bytes = new byte[row * h];
        final int[] line = new int[w];
        for (int y = 0; y < h; ++y) {
            lit.getPixels(line, 0, w, 0, y, w, 1);
            for (int x = 0; x < w; ++x) {
                final int p = line[x];
                bytes[y * row + x] = (byte) ((((p >> 16) & 0xFF) * 77 + ((p >> 8) & 0xFF) * 150 + (p & 0xFF) * 29) >> 8);
            }
        }
        out.copyPixelsFromBuffer(ByteBuffer.wrap(bytes));
        lit.recycle();
        return out;
    }

    static String text(File f) {
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
}
