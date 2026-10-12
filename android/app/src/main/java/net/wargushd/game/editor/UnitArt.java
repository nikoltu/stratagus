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

    /** The picture of a unit type on a tileset, or null when any part of the way to it is missing. */
    static Picture load(File gameFolder, String mapJson, int type, int tileset) {
        try {
            final String[] entry = UnitFrames.entry(mapJson, UnitFrames.file(type, tileset));
            if (entry == null) {
                return null;
            }
            final File dir = new File(gameFolder, "Data/Art/hd/unit");
            final Picture p = new Picture();
            p.bodyAt = UnitFrames.frame(text(new File(dir, entry[0] + "_sprites.json")), entry[1] + "_0");
            if (p.bodyAt == null) {
                return null;
            }
            p.body = region(new File(dir, entry[0] + "_sprites.png"), p.bodyAt);
            if (p.body == null) {
                return null;
            }
            // The mask is there for some units only.
            p.maskAt = UnitFrames.frame(text(new File(dir, entry[0] + "_masks.json")), entry[1] + "_0_team");
            if (p.maskAt != null) {
                final Bitmap lit = region(new File(dir, entry[0] + "_masks.png"), p.maskAt);
                p.mask = (lit == null) ? null : opacity(lit);
            }
            return p;
        } catch (RuntimeException e) {
            return null;
        } catch (OutOfMemoryError e) {
            return null;
        }
    }

    /** One rect of a sheet, decoded alone. */
    @SuppressWarnings("deprecation")
    private static Bitmap region(File png, int[] at) {
        if (!png.isFile()) {
            return null;
        }
        BitmapRegionDecoder sheet = null;
        try {
            sheet = BitmapRegionDecoder.newInstance(png.getPath(), false);
            if (sheet == null || at[0] < 0 || at[1] < 0 || at[0] + at[2] > sheet.getWidth()
                || at[1] + at[3] > sheet.getHeight()) {
                return null;
            }
            final BitmapFactory.Options o = new BitmapFactory.Options();
            o.inSampleSize = REDUCE;
            o.inPreferredConfig = Bitmap.Config.ARGB_8888;
            return sheet.decodeRegion(new Rect(at[0], at[1], at[0] + at[2], at[1] + at[3]), o);
        } catch (java.io.IOException e) {
            return null;
        } finally {
            if (sheet != null) {
                sheet.recycle();
            }
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
