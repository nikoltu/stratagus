package net.wargushd.game;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeSet;
import java.util.zip.CRC32;
import java.util.zip.Deflater;

/**
 * The engine's font sheets, made on the device from the game's own font files.
 *
 * Nothing made from the game's files is shipped with the app: the sheets the engine's bitmap
 * font system reads (ui/fonts/blq55.png, blq55_small.png, blq55_hud_small.png and their advance
 * tables) are drawn here from the face the game itself draws the language in use with: the
 * game folder's Data/skins/skins.json names it per font and language (enUS BLQ55.ttf; the other
 * Latin languages and ruRU NotoSans-Regular.ttf; jaJP / koKR / zhCN / zhTW the Noto JP / KR / SC
 * / TC face). A language the table does not name (a mod's) takes the enUS face. (FONTBAKER-LANG)
 *
 * The sheet is what the engine expects: an 8-bit indexed picture, characters 32..255 from
 * index 0, 16 glyphs to a row; index 0 is clear, index 228 is the glyph's body, indices 220..227
 * are partial coverage with their alpha in the picture's transparency table. A glyph stands at its
 * pen origin (its own left bearing kept); every glyph of a sheet stands on ONE baseline, placed
 * so that a capital is centred in the cell's height. The advance table holds the face's own
 * advance of each glyph, one number a line, one line per cell of the grid.
 *
 * FONT-CJK: the characters the language's texts use that slots 32..255 do not hold (Cyrillic,
 * kana, hanzi, Hangul ...) follow in slots 256.. and are listed in `<stem>.map`, one decimal
 * codepoint a line (font.cpp CFont::LoadGlyphMap); the grid is then wider than 16 (FontScan).
 * The texts are the game's Data/Strings/<language>.json (a mod language: the mod's own table),
 * the launcher's own words in that language and the switched-on mods' tables in it.
 * The marker ui/fonts/blq55.layout holds the layout and what the sheets were made for: a change
 * of language, face or texts makes them again (refresh, from the launcher's shell).
 *
 * The five classic sheets (fonts/small, game, large and the two episode-title sheets) are the
 * game's own bitmap fonts Data/Font/FONT<n>.FNT laid out 15 glyphs to a row from character 32:
 * the file's colour indices 0..7 as they are, 255 where nothing is drawn.
 */
final class FontBaker {
    private FontBaker() {}

    /** The face when the game's skins table cannot be read: the one the sheets were made from before. */
    private static final String FACE = "Data/Font/BLQ55.ttf";
    private static final String SKINS = "Data/skins/skins.json";
    private static final int FIRST = 32, LAST = 255;
    private static final int INK = 228, RAMP0 = 220, LEVELS = INK - RAMP0 + 1;

    /**
     * { file stem, cell width, cell height, text size in pixels, the game's font (skins.json id) }
     *
     * blq55_hud_small is the small face of the in-game HUD (the hit-point figure under the
     * portrait). The game's Data/skins/skins.json names it `small_detail_font`, "size": 9, beside
     * `detail_font`, "size": 15, of the other HUD text: a line height in units of the classic
     * 640x480 HUD, and this face's ascent + descent is 1.302 of its text size. At the HUD step 3
     * (a screen 1440 to 1919 px high) that is 9 * 3 / 1.302 = 20.74 px, drawn there one to one;
     * the engine scales the sheet for another step (scripts/fonts.lua, SetAuthoredHud(1440)).
     * The same rule gives blq55_small its 52: 15 * 4.5 / 1.302 = 51.84 for the 2160-row HUD space.
     * The cells and sizes are the engine's (scripts/fonts.lua) for every face.
     */
    private static final Object[][] SHEETS = {
        { "blq55", 64, 102, 62f, "default_font" },
        { "blq55_small", 64, 88, 52f, "detail_font" },
        { "blq55_hud_small", 26, 36, 20.74f, "small_detail_font" },
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

    /**
     * True when every sheet and table is in the data directory, made in the current layout
     * (SheetLayout): sheets of an older layout are made again. (Whether they are of the language
     * in use is asked by present(Context, File).)
     */
    static boolean present(File dataDir) {
        if (!FontScan.markerLayout(GameLaunch.readText(new File(dataDir, SheetLayout.MARKER)), SheetLayout.LAYOUT)) {
            return false;
        }
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

    /** As present(dataDir), and made for the language, faces and texts in use now. */
    static boolean present(Context c, File gameFolder, File dataDir) {
        if (!present(dataDir)) {
            return false;
        }
        final String marker = GameLaunch.readText(new File(dataDir, SheetLayout.MARKER));
        return marker != null && marker.equals(FontScan.markerText(SheetLayout.LAYOUT, plan(c, gameFolder).key));
    }

    // ---------------------------------------------------------------- what is made for the language

    /** What the sheets are made from for the language in use. */
    private static final class Plan {
        String language;
        /** Per sheet (SHEETS order): the face file. */
        final File[] faces = new File[SHEETS.length];
        /** The texts the language draws: files, and the launcher's own words (an asset). */
        final List<File> texts = new ArrayList<File>();
        String ownWordsAsset;
        /** The language, the faces and the texts' sizes and times: equal keys, equal sheets. */
        String key;
    }

    private static Plan plan(Context c, File game) {
        final Plan p = new Plan();
        final ModStore.Mod mod = ModStore.languageMod(c);
        p.language = SkinKit.language(c);
        final boolean modLanguage = mod != null && p.language.equals(mod.language);
        // The faces the game draws this language with.
        Object skins = null;
        final String table = GameLaunch.readText(new File(game, SKINS));
        if (table != null) {
            try {
                skins = FontScan.parse(table);
            } catch (RuntimeException e) {
                android.util.Log.i("WargusHD", "FONTS skins table not read: " + e);
            }
        }
        for (int i = 0; i < SHEETS.length; ++i) {
            final String named = FontScan.faceFor(skins, (String) SHEETS[i][4], p.language);
            final File face = (named == null) ? null : inData(game, named);
            p.faces[i] = (face != null && face.isFile()) ? face : new File(game, FACE);
        }
        // The texts the engine and the launcher draw in it.
        if (modLanguage) {
            final File dir = new File(ModStore.directory(c), mod.id);
            p.texts.add(new File(dir, "strings/" + p.language + ".json"));
            p.texts.add(new File(dir, "strings/launcher_" + p.language + ".json"));
        } else {
            p.texts.add(new File(game, "Data/Strings/" + p.language + ".json"));
            p.ownWordsAsset = "launcher_strings/" + p.language + ".json";
        }
        for (String id : ModStore.enabled(c)) {
            final File f = new File(new File(ModStore.directory(c), id), "strings/" + p.language + ".json");
            if (!p.texts.contains(f)) {
                p.texts.add(f);
            }
        }
        final StringBuilder k = new StringBuilder(p.language);
        for (File f : p.faces) {
            k.append('|').append(f.getPath()).append(':').append(f.length());
        }
        for (File f : p.texts) {
            if (f.isFile()) {
                k.append('|').append(f.getPath()).append(':').append(f.length()).append(':').append(f.lastModified());
            }
        }
        if (p.ownWordsAsset != null) {
            k.append('|').append(p.ownWordsAsset);   // the package's own: a new package makes all again
        }
        final CRC32 crc = new CRC32();
        try {
            crc.update(k.toString().getBytes("UTF-8"));
        } catch (java.io.UnsupportedEncodingException e) {
            crc.update(k.toString().getBytes());
        }
        p.key = p.language + " " + Long.toHexString(crc.getValue());
        return p;
    }

    /**
     * A path skins.json gives ("font/NotoSans-Regular.ttf", relative to Data) in the game folder;
     * the table writes "font" where the folder has "Font", so each part is matched without regard
     * to case when the exact one is not there. null when it is not found.
     */
    private static File inData(File game, String rel) {
        File at = new File(game, "Data");
        for (String part : rel.replace('\\', '/').split("/")) {
            if (part.isEmpty() || part.equals(".")) {
                continue;
            }
            if (part.equals("..")) {
                return null;
            }
            File next = new File(at, part);
            if (!next.exists()) {
                next = null;
                final String[] names = at.list();
                for (int i = 0; names != null && i < names.length && next == null; ++i) {
                    if (names[i].equalsIgnoreCase(part)) {
                        next = new File(at, names[i]);
                    }
                }
                if (next == null) {
                    return null;
                }
            }
            at = next;
        }
        return at;
    }

    /** The characters after slot 255: those of the plan's texts the slots 32..255 do not hold. */
    private static int[] extras(Context c, Plan p) {
        final TreeSet<Integer> set = new TreeSet<Integer>();
        for (File f : p.texts) {
            final String json = GameLaunch.readText(f);
            if (json == null) {
                continue;
            }
            try {
                FontScan.addJsonValues(FontScan.parse(json), set);
            } catch (RuntimeException e) {
                FontScan.addText(json, set);    // not JSON: every character of it
            }
        }
        if (p.ownWordsAsset != null) {
            try (java.io.InputStream in = c.getAssets().open(p.ownWordsAsset)) {
                final ByteArrayOutputStream out = new ByteArrayOutputStream();
                final byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                }
                final String json = out.toString("UTF-8");
                try {
                    FontScan.addJsonValues(FontScan.parse(json), set);
                } catch (RuntimeException e) {
                    FontScan.addText(json, set);
                }
            } catch (Exception e) {
                // English, or a language without the launcher's own words
            }
        }
        final int[] out = new int[set.size()];
        int i = 0;
        for (int cp : set) {
            out[i++] = cp;
        }
        return out;
    }

    // ---------------------------------------------------------------- making

    private static final Object BAKING = new Object();

    /**
     * Make the sheets from the game folder's faces for the language in use.
     *
     * @return true when all were written; false when a face cannot be read or a file
     *         cannot be written (the engine then finds no sheet and says so in its log).
     */
    static boolean bake(Context c, File gameFolder, File dataDir) {
        if (gameFolder == null) {
            return false;
        }
        synchronized (BAKING) {
            try {
                final long began = android.os.SystemClock.uptimeMillis();
                final Plan p = plan(c, gameFolder);
                for (File f : p.faces) {
                    if (!f.isFile()) {
                        return false;
                    }
                }
                final int[] extra = extras(c, p);
                // While the sheets change, the marker says the layout but no language: the shell
                // makes them again if this is cut short.
                final File marker = new File(dataDir, SheetLayout.MARKER);
                if (marker.isFile()) {
                    GameLaunch.writeText(marker, FontScan.markerText(SheetLayout.LAYOUT, "making"));
                }
                final Map<String, Typeface> faces = new HashMap<String, Typeface>();
                for (int i = 0; i < SHEETS.length; ++i) {
                    final Object[] s = SHEETS[i];
                    Typeface face = faces.get(p.faces[i].getPath());
                    if (face == null) {
                        face = Typeface.createFromFile(p.faces[i]);
                        faces.put(p.faces[i].getPath(), face);
                    }
                    sheet(face, dataDir, (String) s[0], (Integer) s[1], (Integer) s[2], (Float) s[3], extra);
                }
                for (String[] s : CLASSIC) {
                    classic(new File(gameFolder, s[0]), dataDir, s[1]);
                }
                // Last: the marker says the sheets above are in this layout (scripts/fonts.lua)
                // and for what they were made.
                try (OutputStream o = new FileOutputStream(marker)) {
                    o.write(FontScan.markerText(SheetLayout.LAYOUT, p.key).getBytes("UTF-8"));
                }
                Provision.wrote(marker);
                android.util.Log.i("WargusHD", "FONTS made for " + p.language + " from " + p.faces[0].getName()
                    + ", " + extra.length + " characters after slot 255, in "
                    + (android.os.SystemClock.uptimeMillis() - began) + " ms");
                return true;
            } catch (Exception e) {
                android.util.Log.i("WargusHD", "FONTS not made: " + e);
                return false;
            } catch (OutOfMemoryError e) {
                android.util.Log.i("WargusHD", "FONTS not made: " + e);
                return false;
            }
        }
    }

    private static Thread refresh;

    /**
     * From the launcher's shell (it starts over when the language changes): when the sheets are
     * not of the language, faces and texts in use, make them again on a background thread.
     * Cheap when they are.
     */
    static void refreshIfStale(Context context) {
        final Context c = context.getApplicationContext();
        final String folder = GateActivity.rememberedFolder(c);
        if (folder == null) {
            return;
        }
        synchronized (FontBaker.class) {
            if (refresh != null && refresh.isAlive()) {
                return;     // it reads the language when it runs; a later change comes back here
            }
            refresh = new Thread(new Runnable() {
                @Override
                public void run() {
                    android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_BACKGROUND);
                    final File data = Provision.dataDir(c);
                    if (!present(c, new File(folder), data)) {
                        android.util.Log.i("WargusHD", "FONTS sheets not of the language in use: made again");
                        bake(c, new File(folder), data);
                    }
                }
            }, "fonts");
            refresh.start();
        }
    }

    /** Before a game starts: wait for a making refreshIfStale began, so the engine reads whole sheets. */
    static void awaitRefresh() {
        final Thread t;
        synchronized (FontBaker.class) {
            t = refresh;
        }
        if (t == null || !t.isAlive()) {
            return;
        }
        final long began = android.os.SystemClock.uptimeMillis();
        try {
            t.join();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        android.util.Log.i("WargusHD", "FONTS game start waited " + (android.os.SystemClock.uptimeMillis() - began)
            + " ms for the sheets");
    }

    private static void sheet(Typeface face, File dataDir, String stem, int cw, int ch, float px, int[] extraAll)
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

        // The grid: 16 a row with nothing after slot 255, wider with more (FontScan.columns).
        final int columns = FontScan.columns(extraAll.length, cw, ch);
        final int room = FontScan.capacity(columns, cw, ch);
        final int[] extra = (extraAll.length <= room) ? extraAll : java.util.Arrays.copyOf(extraAll, room);
        if (extra.length < extraAll.length) {
            android.util.Log.i("WargusHD", "FONTS " + stem + ": " + (extraAll.length - room)
                + " characters do not fit the sheet; left out");
        }
        final int glyphs = (LAST - FIRST + 1) + extra.length;
        final int rows = (glyphs + columns - 1) / columns;
        final int w = cw * columns, h = ch * rows;
        final byte[] index = new byte[w * h];
        final StringBuilder advances = new StringBuilder();

        final Bitmap cell = Bitmap.createBitmap(cw, ch, Bitmap.Config.ARGB_8888);
        final Canvas c = new Canvas(cell);
        final int[] row = new int[cw];
        for (int gi = 0; gi < columns * rows; ++gi) {
            final String s;
            if (gi < LAST - FIRST + 1) {
                // The slot's character (SheetLayout): Latin-1, the Lithuanian letters and marks in
                // 0x80..0x9D, nothing in the other control codes.
                final int code = FIRST + gi;
                final char drawn = SheetLayout.charAt(code);
                final String a = String.valueOf(drawn != 0 ? drawn : (char) code);
                // getRunAdvance, not measureText: measureText rounds every width up.
                advances.append(Math.round(p.getRunAdvance(a, 0, 1, 0, 1, false, 1))).append('\n');
                if (drawn == 0) {
                    continue;                              // control codes: no glyph
                }
                s = a;
            } else if (gi < glyphs) {
                s = new String(Character.toChars(extra[gi - (LAST - FIRST + 1)]));
                advances.append(Math.round(p.getRunAdvance(s, 0, s.length(), 0, s.length(), false, s.length())))
                    .append('\n');
            } else {
                advances.append("0\n");                    // the grid's empty cells after the last glyph
                continue;
            }
            cell.eraseColor(0);
            c.drawText(s, 0f, baseline, p);
            final int cx = (gi % columns) * cw, cy = (gi / columns) * ch;
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
        // An old map must not outlive its sheet: gone first, written again last.
        final File map = new File(dataDir, "ui/fonts/" + stem + ".map");
        map.delete();
        new File(dataDir, "fonts/" + stem + ".map").delete();
        write(dataDir, stem, png(index, w, h, palette, alpha));
        final File adv = new File(dataDir, "ui/fonts/" + stem + ".adv");
        try (OutputStream o = new FileOutputStream(adv)) {
            o.write(advances.toString().getBytes("US-ASCII"));
        }
        Provision.wrote(adv);
        if (extra.length > 0) {
            try (OutputStream o = new FileOutputStream(map)) {
                o.write(FontScan.mapText(extra).getBytes("US-ASCII"));
            }
            Provision.wrote(map);
        }
        android.util.Log.i("WargusHD", "FONTS made " + stem + " " + w + "x" + h + " baseline " + baseline
            + (extra.length > 0 ? ", map " + extra.length : ""));
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
