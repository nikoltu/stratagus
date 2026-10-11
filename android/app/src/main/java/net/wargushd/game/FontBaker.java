package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.util.zip.CRC32;
import java.util.zip.Deflater;

/**
 * The engine's font sheets, made on the device from the game's own font files.
 *
 * Nothing made from the game's files is shipped with the app: the sheets the engine's bitmap
 * font system reads (ui/fonts/blq55.png, blq55_small.png, blq55_hud_small.png and their advance
 * tables) are drawn here from Data/Font/BLQ55.ttf of the folder the user chose.
 *
 * The sheet is what the engine expects: an 8-bit indexed picture, 16 glyphs to a row,
 * characters 32..255; index 0 is clear, index 228 is the glyph's body, indices 220..227 are
 * partial coverage with their alpha in the picture's transparency table. A glyph stands at its
 * pen origin (its own left bearing kept); every glyph of a sheet stands on ONE baseline, placed
 * so that a capital is centred in the cell's height. The advance table holds the face's own
 * advance of each glyph, one number a line.
 *
 * The five classic sheets (fonts/small, game, large and the two episode-title sheets) are the
 * game's own bitmap fonts Data/Font/FONT<n>.FNT laid out 15 glyphs to a row from character 32:
 * the file's colour indices 0..7 as they are, 255 where nothing is drawn.
 */
final class FontBaker {
    private FontBaker() {}

    private static final String FACE = "Data/Font/BLQ55.ttf";
    private static final int FIRST = 32, LAST = 255, COLUMNS = 16;
    private static final int INK = 228, RAMP0 = 220, LEVELS = INK - RAMP0 + 1;

    /**
     * { file stem, cell width, cell height, text size in pixels }
     *
     * blq55_hud_small is the small face of the in-game HUD (the hit-point figure under the
     * portrait). The game's Data/skins/skins.json names it `small_detail_font`, "size": 9, beside
     * `detail_font`, "size": 15, of the other HUD text: a line height in units of the classic
     * 640x480 HUD, and this face's ascent + descent is 1.302 of its text size. At the HUD step 3
     * (a screen 1440 to 1919 px high) that is 9 * 3 / 1.302 = 20.74 px, drawn there one to one;
     * the engine scales the sheet for another step (scripts/fonts.lua, SetAuthoredHud(1440)).
     * The same rule gives blq55_small its 52: 15 * 4.5 / 1.302 = 51.84 for the 2160-row HUD space.
     */
    private static final Object[][] SHEETS = {
        { "blq55", 64, 102, 62f },
        { "blq55_small", 64, 88, 52f },
        { "blq55_hud_small", 26, 36, 20.74f },
    };

    /** { the game's bitmap font, sheet stem } */
    private static final String[][] CLASSIC = {
        { "Data/Font/FONT6.FNT", "small" },
        { "Data/Font/FONT10.FNT", "game" },
        { "Data/Font/FONT12.FNT", "large" },
        { "Data/Font/FONT32.FNT", "small_episode_titles" },
        { "Data/Font/FONT50.FNT", "large_episode_titles" },
    };
    private static final int CLASSIC_COLUMNS = 15, CLEAR = 255;

    /** True when every sheet and table is in the data directory. */
    static boolean present(File dataDir) {
        for (String[] s : CLASSIC) {
            if (!new File(dataDir, "ui/fonts/" + s[1] + ".png").isFile()) {
                return false;
            }
        }
        for (Object[] s : SHEETS) {
            if (!new File(dataDir, "ui/fonts/" + s[0] + ".png").isFile()
                    || !new File(dataDir, "ui/fonts/" + s[0] + ".adv").isFile()) {
                return false;
            }
        }
        return true;
    }

    /**
     * Make the sheets from the game folder's face.
     *
     * @return true when all were written; false when the face cannot be read or a file
     *         cannot be written (the engine then finds no sheet and says so in its log).
     */
    static boolean bake(File gameFolder, File dataDir) {
        if (gameFolder == null) {
            return false;
        }
        final File ttf = new File(gameFolder, FACE);
        if (!ttf.isFile()) {
            return false;
        }
        try {
            final Typeface face = Typeface.createFromFile(ttf);
            for (Object[] s : SHEETS) {
                sheet(face, dataDir, (String) s[0], (Integer) s[1], (Integer) s[2], (Float) s[3]);
            }
            for (String[] s : CLASSIC) {
                classic(new File(gameFolder, s[0]), dataDir, s[1]);
            }
            return true;
        } catch (Exception e) {
            android.util.Log.i("WargusHD", "FONTS not made: " + e);
            return false;
        }
    }

    private static void sheet(Typeface face, File dataDir, String stem, int cw, int ch, float px)
            throws Exception {
        final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setTypeface(face);
        p.setTextSize(px);
        p.setHinting(Paint.HINTING_OFF);
        p.setLinearText(true);
        p.setColor(0xFFFFFFFF);

        // One baseline for the whole sheet: a capital's box centred in the cell's height.
        final Rect cap = new Rect();
        p.getTextBounds("H", 0, 1, cap);
        final int baseline = (ch + cap.height()) / 2;

        final int rows = (LAST - FIRST + COLUMNS) / COLUMNS;
        final int w = cw * COLUMNS, h = ch * rows;
        final byte[] index = new byte[w * h];
        final StringBuilder advances = new StringBuilder();

        final Bitmap cell = Bitmap.createBitmap(cw, ch, Bitmap.Config.ARGB_8888);
        final Canvas c = new Canvas(cell);
        final int[] row = new int[cw];
        for (int code = FIRST; code <= LAST; ++code) {
            // 173 (soft hyphen): the platform draws nothing for it; the face draws its hyphen.
            final String s = String.valueOf((char) (code == 173 ? 45 : code));
            // getRunAdvance, not measureText: measureText rounds every width up.
            advances.append(Math.round(p.getRunAdvance(s, 0, 1, 0, 1, false, 1))).append('\n');
            if (code >= 127 && code < 160) {
                continue;                              // control codes: no glyph
            }
            cell.eraseColor(0);
            c.drawText(s, 0f, baseline, p);
            final int gi = code - FIRST, cx = (gi % COLUMNS) * cw, cy = (gi / COLUMNS) * ch;
            for (int y = 0; y < ch; ++y) {
                cell.getPixels(row, 0, cw, 0, y, cw, 1);
                final int at = (cy + y) * w + cx;
                for (int x = 0; x < cw; ++x) {
                    final int v = row[x] >>> 24;      // coverage
                    if (v >= 128) {
                        index[at + x] = (byte) INK;
                    } else if (v >= 24) {
                        index[at + x] = (byte) (RAMP0 + Math.min(LEVELS - 1, v * LEVELS / 256));
                    }
                }
            }
        }
        cell.recycle();

        final byte[] palette = new byte[768];
        final byte[] alpha = new byte[256];
        java.util.Arrays.fill(alpha, (byte) 255);
        alpha[0] = 0;
        for (int k = 0; k < LEVELS; ++k) {
            final int i = RAMP0 + k;
            palette[i * 3] = palette[i * 3 + 1] = palette[i * 3 + 2] = (byte) 255;
            alpha[i] = (byte) Math.round(255f * (k + 1) / LEVELS);
        }
        write(dataDir, stem, png(index, w, h, palette, alpha));
        final File adv = new File(dataDir, "ui/fonts/" + stem + ".adv");
        try (OutputStream o = new FileOutputStream(adv)) {
            o.write(advances.toString().getBytes("US-ASCII"));
        }
        Provision.wrote(adv);
        android.util.Log.i("WargusHD", "FONTS made " + stem + " " + w + "x" + h + " baseline " + baseline);
    }

    /** The sheet into both font folders the scripts name. */
    private static void write(File dataDir, String stem, byte[] png) throws Exception {
        for (String dir : new String[] { "ui/fonts", "fonts" }) {
            final File out = new File(dataDir, dir + "/" + stem + ".png");
            final File parent = out.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                throw new java.io.IOException("cannot make " + parent);
            }
            try (OutputStream o = new FileOutputStream(out)) {
                o.write(png);
            }
            Provision.wrote(out);
        }
    }

    /**
     * One of the game's bitmap fonts as a sheet. The file: "FONT ", the last character, the
     * widest and tallest glyph, one offset per glyph from character 32; a glyph is its width,
     * height, x and y offset and a stream of bytes, each "skip this many pixels, then one pixel
     * of this colour" (skip in the upper five bits, colour in the lower three).
     */
    private static void classic(File fnt, File dataDir, String stem) throws Exception {
        final byte[] d = new byte[(int) fnt.length()];
        try (java.io.DataInputStream in = new java.io.DataInputStream(new java.io.FileInputStream(fnt))) {
            in.readFully(d);
        }
        final int count = (d[5] & 0xFF) - 32;
        final int mw = d[6] & 0xFF, mh = d[7] & 0xFF;
        final int w = mw * CLASSIC_COLUMNS, h = (count + CLASSIC_COLUMNS - 1) / CLASSIC_COLUMNS * mh;
        final byte[] index = new byte[w * h];
        java.util.Arrays.fill(index, (byte) CLEAR);
        for (int i = 0; i < count; ++i) {
            final int at = 8 + i * 4;
            final int o = (d[at] & 0xFF) | (d[at + 1] & 0xFF) << 8 | (d[at + 2] & 0xFF) << 16 | (d[at + 3] & 0xFF) << 24;
            if (o == 0) {
                continue;                              // no glyph for this character
            }
            final int gw = d[o] & 0xFF, gh = d[o + 1] & 0xFF;
            final int base = (i % CLASSIC_COLUMNS) * mw + (i / CLASSIC_COLUMNS) * w * mh
                + (d[o + 2] & 0xFF) + (d[o + 3] & 0xFF) * w;
            int bp = o + 4, x = 0, y = 0;
            glyph:
            for (;;) {
                final int c = d[bp++] & 0xFF;
                x += c >> 3;
                while (x >= gw) {
                    x -= gw;
                    if (++y >= gh) {
                        break glyph;
                    }
                }
                index[base + y * w + x] = (byte) (c & 7);
                if (++x >= gw) {
                    x -= gw;
                    if (++y >= gh) {
                        break;
                    }
                }
            }
        }
        // The eight indices as a plain grey ramp (the engine recolours them); 255 is clear.
        final byte[] palette = new byte[768];
        final byte[] alpha = new byte[256];
        java.util.Arrays.fill(alpha, (byte) 255);
        alpha[CLEAR] = 0;
        for (int i = 0; i < 8; ++i) {
            palette[i * 3] = palette[i * 3 + 1] = palette[i * 3 + 2] = (byte) (i * 255 / 7);
        }
        write(dataDir, stem, png(index, w, h, palette, alpha));
        android.util.Log.i("WargusHD", "FONTS made " + stem + " " + w + "x" + h);
    }

    /** An 8-bit indexed PNG with the given palette and per-index alpha. */
    private static byte[] png(byte[] index, int w, int h, byte[] palette, byte[] alpha) throws Exception {
        final ByteArrayOutputStream out = new ByteArrayOutputStream();
        out.write(new byte[] { (byte) 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A });
        final ByteArrayOutputStream head = new ByteArrayOutputStream();
        writeInt(head, w);
        writeInt(head, h);
        head.write(new byte[] { 8, 3, 0, 0, 0 });      // 8 bits, indexed, no interlace
        chunk(out, "IHDR", head.toByteArray());

        chunk(out, "PLTE", palette);
        chunk(out, "tRNS", alpha);

        final byte[] raw = new byte[(w + 1) * h];
        for (int y = 0; y < h; ++y) {
            System.arraycopy(index, y * w, raw, y * (w + 1) + 1, w);   // filter 0 before each row
        }
        final Deflater z = new Deflater(Deflater.BEST_COMPRESSION);
        z.setInput(raw);
        z.finish();
        final ByteArrayOutputStream data = new ByteArrayOutputStream();
        final byte[] buf = new byte[65536];
        while (!z.finished()) {
            data.write(buf, 0, z.deflate(buf));
        }
        z.end();
        chunk(out, "IDAT", data.toByteArray());
        chunk(out, "IEND", new byte[0]);
        return out.toByteArray();
    }

    private static void chunk(OutputStream out, String type, byte[] body) throws Exception {
        final byte[] name = type.getBytes("US-ASCII");
        final CRC32 crc = new CRC32();
        crc.update(name);
        crc.update(body);
        writeInt(out, body.length);
        out.write(name);
        out.write(body);
        writeInt(out, (int) crc.getValue());
    }

    private static void writeInt(OutputStream out, int v) throws java.io.IOException {
        out.write(v >>> 24);
        out.write(v >>> 16);
        out.write(v >>> 8);
        out.write(v);
    }
}
