package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.BitmapRegionDecoder;
import android.graphics.Canvas;
import android.graphics.Matrix;
import android.graphics.Rect;

import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;

/**
 * The engine's interface pictures, made on the device from the game's own atlases.
 *
 * Nothing made from the game's files is shipped with the app. The engine draws a picture at
 * its own pixel size, so each piece of the interface is cut from the atlas of the folder the
 * user chose (Data/skins/NAME.png with its frame table NAME.json) and brought to the size the
 * layout asks for, here, once, after the app's own files are unpacked.
 *
 * Per race: the stone buttons (widgets/) and the window set (remaster/): dialog buttons,
 * drop-down bars and arrow, scroll arrows, tracks and knobs, slider, check boxes, radio
 * buttons, text field, the parchment panels with their gold rim, the title banner, the dimming
 * tile and the eight pieces of the bracket frame, and the darker list row. For both: the top
 * bar's resource icons and the briefing's dimming tile.
 */
final class ArtBaker {
    private ArtBaker() {}

    private static final String[] RACES = { "human", "orc" };
    private static final String[] ATLAS = { "Data/skins/Modern_Widgets_Human", "Data/skins/Modern_Widgets_Orc" };
    /** Panels, rim and banner are the same for both races. */
    private static final String GRAPHICS = "Data/skins/Modern_Graphics";
    /**
     * Where the pictures go: every script request names them under ui/. The copies once also
     * written at the data root were never read (APK_CONTENT_IMPL F2); the next unpack prunes them.
     */
    private static final String ROOT = "ui/";

    private static final int BUTTON_EDGE = 14;
    /** How much darker than its grey value a disabled button is drawn (the game has no such frame). */
    private static final int GRAYED_DROP = 80;
    /** The grey rim is drawn in this bronze, its shading kept. */
    private static final int[] GOLD = { 212, 168, 92 };
    /** The dimming tile behind a window: black at this alpha. */
    private static final int DIM_ALPHA = 150;
    /** { file stem, width, height } */
    private static final Object[][] PANELS = {
        { "panel-small", 520, 300 }, { "panel-medium", 760, 560 },
        { "panel-large", 1120, 760 }, { "panel-xlarge", 1200, 960 },
    };
    private static final int PAPER_EDGE = 300, RIM_EDGE = 60, BRACKET = 74;
    /** The light line frame of the dark pages (the briefing): its corner piece's size. */
    private static final int LIGHT_BRACKET = 96;
    /** A list row that is not the chosen one: the dialog button at this share of its brightness. */
    private static final double ROW_DIM = 0.51;
    /**
     * The tile behind the briefing's objectives: black at this alpha. The objectives' frame
     * already darkens its middle (its art's own fill, alpha 92: what is behind keeps 0.64); the
     * original's box keeps 0.487 in all, so the tile adds the rest: 1 - 0.487 / 0.64 = 0.24.
     */
    private static final int SCRIM_ALPHA = 61;

    /** The top bar's resource icons: the HUD atlas' frames, at the size the bar draws them. */
    private static final String HUD_ATLAS = "Data/skins/Modern_HUD_Widgets";
    private static final String[] RESOURCE_FRAMES = { "icons_0", "icons_1", "icons_2", "icons_3" };
    private static final String FOOD_FRAME = "icons_4";
    private static final String RESOURCE_STRIP = "gold,wood,oil,mana.png", FOOD = "food.png";
    private static final int RESOURCE_ICON = 44;

    /** True when the pictures made here are in the data directory (the last of each set is asked). */
    static boolean present(File dataDir) {
        if (!new File(dataDir, "ui/" + FOOD).isFile() || !new File(dataDir, "ui/rm/briefing/scrim.png").isFile()
                || !new File(dataDir, "ui/rm/widgets/tooltip_frame240.png").isFile()
                || !new File(dataDir, "ui/rm/popup/message_orc.png").isFile()) {
            return false;
        }
        for (String race : RACES) {
            if (!new File(dataDir, "ui/" + race + "/widgets/button-half-grayed.png").isFile()
                    || !new File(dataDir, "ui/" + race + "/remaster/frame-light-bracket-r.png").isFile()
                    || !new File(dataDir, "ui/" + race + "/remaster/textinput-active.png").isFile()
                    || !new File(dataDir, "ui/" + race + "/remaster/textinput-hover.png").isFile()) {
                return false;
            }
        }
        return true;
    }

    /** Written into the data directory when a bake has ended well: its date is the art's. In
     *  one of the makers' places (Provision.madePlace), so that it is kept with the pictures. */
    private static final String STAMP = "ui/rm/art_baked";

    /** When the pictures were last made, in epoch milliseconds; 0 when no bake has left its stamp. */
    static long bakedAt(File dataDir) {
        final File f = new File(dataDir, STAMP);
        return f.isFile() ? f.lastModified() : 0L;
    }

    /** Make the pictures; false when an atlas cannot be read or a file cannot be written. */
    static boolean bake(File gameFolder, File dataDir) {
        if (gameFolder == null) {
            return false;
        }
        final long t0 = System.currentTimeMillis();
        final int[] made = { 0 };
        try {
            final Atlas g = new Atlas(gameFolder, GRAPHICS);
            final Bitmap paper = g.frame("frame_paper01");
            final Bitmap rim = tinted(g.frame("frame_small02"), GOLD);
            final Bitmap banner = g.frame("title_banner_blank");
            final Bitmap lightRim = g.frame("frame_small04");
            // The in-game menu's own panel (the game's skin "fe_pause_menu_<race>": the whole
            // sprite, no slicing) and the tooltip's frame at a fifth of its size (skin
            // "hud_tooltip": scale 5).
            final Out plain = new Out(dataDir, null, made);
            for (String race : RACES) {
                final Bitmap panel = g.frame("pause_bg_" + race);
                plain.saveAt("ui/rm/pause/bg_" + race + ".png", panel);
                panel.recycle();
            }
            // The end-of-game and message popups (skins "fe_popup_*"): the whole sprites.
            for (String race : RACES) {
                for (String kind : new String[] { "victory", "defeat", "message" }) {
                    final Bitmap popup = g.frame("popup_" + kind + "_" + race);
                    plain.saveAt("ui/rm/popup/" + kind + "_" + race + ".png", popup);
                    popup.recycle();
                }
            }
            final Bitmap tip = g.frame("tooltip_frame");
            // Whole sprite, 240x240: the engine scales the 70-pixel border to the HUD scale when it
            // draws (skin "hud_tooltip": 9slice 70, scale 5). A new file name, so an install that
            // holds the earlier 48-pixel picture makes its pictures again.
            plain.saveAt("ui/rm/widgets/tooltip_frame240.png", tip);
            g.close();
            for (int r = 0; r < RACES.length; ++r) {
                race(new Atlas(gameFolder, ATLAS[r]), new Out(dataDir, RACES[r], made), paper, rim, banner, lightRim);
            }
            resources(new Atlas(gameFolder, HUD_ATLAS), new Out(dataDir, null, made));
            final Bitmap scrim = Bitmap.createBitmap(64, 64, Bitmap.Config.ARGB_8888);
            scrim.eraseColor(SCRIM_ALPHA << 24);
            new Out(dataDir, null, made).saveAt("ui/rm/briefing/scrim.png", scrim);
            android.util.Log.i("WargusHD", "ART made " + made[0] + " in " + (System.currentTimeMillis() - t0) + " ms");
            final File stampFile = new File(dataDir, STAMP);
            try (java.io.FileOutputStream stamp = new java.io.FileOutputStream(stampFile)) {
                stamp.write(String.valueOf(System.currentTimeMillis()).getBytes("US-ASCII"));
                Provision.wrote(stampFile);
            } catch (Exception e) {
                // The pictures are made; Options then shows the content's date instead.
            }
            return true;
        } catch (Exception | OutOfMemoryError e) {
            android.util.Log.i("WargusHD", "ART not made after " + made[0] + ": " + e);
            return false;
        }
    }

    private static void race(Atlas a, Out o, Bitmap paper, Bitmap rim, Bitmap banner, Bitmap lightRim)
            throws Exception {
        // The stone button in three sizes; pressed is the atlas' active frame, grayed is made.
        final Bitmap large = a.frame("large"), active = a.frame("large_active");
        final Object[][] buttons = {
            { "widgets/button-large", 900, 112 }, { "widgets/button-half", 430, 96 },
            { "remaster/button-dialog", 340, 96 },
        };
        for (Object[] b : buttons) {
            final int w = (Integer) b[1], h = (Integer) b[2];
            final Bitmap normal = nineSlice(large, w, h, BUTTON_EDGE, BUTTON_EDGE, BUTTON_EDGE, BUTTON_EDGE);
            o.save(b[0] + "-normal.png", normal);
            o.save(b[0] + "-pressed.png", nineSlice(active, w, h, BUTTON_EDGE, BUTTON_EDGE, BUTTON_EDGE, BUTTON_EDGE));
            o.save(b[0] + "-grayed.png", grayed(normal));
            if ("remaster/button-dialog".equals(b[0])) {
                o.saveAt("ui/rm/widgets/" + o.race + "_row_unselected.png", dimmed(normal, ROW_DIM));
            }
        }

        // Drop-down bars: stretched by the engine to the field's width at their own height.
        final Bitmap closed = a.frame("dropdown_closed_default");
        o.save("remaster/dropdown-closed.png", nineSlice(closed, 1008, 72, 14, 14, 96, 8));
        o.save("remaster/dropdown-closed-pressed.png", nineSlice(a.frame("dropdown_closed_active"), 1008, 72, 14, 14, 96, 8));
        o.save("remaster/dropdown-open.png", nineSlice(a.frame("dropdown_open_default"), 1008, 72, 14, 14, 14, 8));
        // The arrow: the right square of the closed bar, as it is.
        o.save("remaster/dropdown-arrow.png",
               Bitmap.createBitmap(closed, closed.getWidth() - closed.getHeight(), 0, closed.getHeight(), closed.getHeight()));

        // Scroll arrows; the atlas has up and down only, left and right are up turned.
        final Bitmap up = a.frame("scroll_up");
        o.save("remaster/scroll-up.png", scaled(up, 28, 28));
        o.save("remaster/scroll-down.png", scaled(a.frame("scroll_down"), 28, 28));
        o.save("remaster/scroll-left.png", scaled(turned(up, -90), 28, 28));
        o.save("remaster/scroll-right.png", scaled(turned(up, 90), 28, 28));

        // Track and knob; the slider's track is a sliver of the vertical groove laid flat.
        final Bitmap groove = a.frame("scroll_v"), knob = a.frame("scroll_slider");
        o.save("remaster/slider-track.png",
               scaled(Bitmap.createBitmap(groove, 0, groove.getHeight() / 2 - 2, groove.getWidth(), 4), 360, 28));
        o.save("remaster/slider-knob.png", scaled(knob, 44, 44));
        o.save("remaster/slider-knob-large.png", knob);          // at the atlas' own size
        o.save("remaster/scroll-vtrack.png", nineSlice(groove, 28, 400, 0, 18, 0, 20));
        o.save("remaster/scroll-knob.png", scaled(knob, 26, 26));
        // The scroll bar of a bare scrolling column (the option windows) and the drop-down's
        // rows: the atlas' own pieces at their own size, under the names the skin asks for first
        // (guichan.lua RM.skinScrollArea: ui/rm/widgets/<race>_<frame>.png).
        for (String part : new String[] { "scroll_up", "scroll_down", "scroll_v", "scrollbar_cursor",
                                          "dropdown_open_default", "dropdown_open_hover", "dropdown_open_active" }) {
            o.saveAt("ui/rm/widgets/" + o.race + "_" + part + ".png", a.frame(part));
        }

        // Check boxes and radio buttons at the atlas' own size.
        final String[][] marks = {
            { "check", "checkbox-unselected" }, { "check_enabled", "checkbox-selected" },
            { "check_active", "checkbox-unselected-pressed" }, { "check_enabled_active", "checkbox-selected-pressed" },
            { "radio", "radio-unselected" }, { "radio_enabled", "radio-selected" },
            { "radio_active", "radio-unselected-pressed" }, { "radio_enabled_active", "radio-selected-pressed" },
        };
        for (String[] m : marks) {
            o.save("remaster/" + m[1] + ".png", a.frame(m[0]));
        }

        o.save("remaster/textinput.png", nineSlice(a.frame("textinput_normal"), 1008, 64, 32, 20, 32, 15));
        // The field's sprites at their own size (252 x 126), for a field the engine nine-slices
        // itself (skin "widget_edit": 9slice 32, 20, 32, 15): the 1008 x 64 picture above squeezes
        // the sprite's 91-row middle into 29 rows. New names, so an install that holds only the
        // older picture makes its pictures again.
        o.save("remaster/textinput-normal.png", a.frame("textinput_normal"));
        o.save("remaster/textinput-active.png", a.frame("textinput_active"));
        // The unfocused field: the original draws flat (36, 36, 56) = the textinput_hover sprite
        // (ORIG47 section 1, Save window just opened / after a list row is picked).
        o.save("remaster/textinput-hover.png", a.frame("textinput_hover"));
        a.close();

        // A window: the parchment with its torn corners kept, the gold rim over it.
        for (Object[] p : PANELS) {
            final int w = (Integer) p[1], h = (Integer) p[2];
            final Bitmap panel = nineSlice(paper, w, h, PAPER_EDGE, PAPER_EDGE, PAPER_EDGE, PAPER_EDGE);
            final Bitmap over = nineSlice(rim, w, h, RIM_EDGE, RIM_EDGE, RIM_EDGE, RIM_EDGE);
            new Canvas(panel).drawBitmap(over, 0, 0, null);
            over.recycle();
            o.save("remaster/" + p[0] + ".png", panel);
        }
        o.save("remaster/title-banner.png", nineSlice(banner, 1200, 194, 40, 40, 40, 40));

        final Bitmap dim = Bitmap.createBitmap(64, 64, Bitmap.Config.ARGB_8888);
        dim.eraseColor(DIM_ALPHA << 24);
        o.save("remaster/backdrop.png", dim);

        // The rim alone in eight pieces, and the light line frame, as it is in the atlas, alike.
        pieces(o, "remaster/frame-bracket-", rim, BRACKET);
        pieces(o, "remaster/frame-light-bracket-", lightRim, LIGHT_BRACKET);
        // The light frame also has a middle: the even fill inside its glow.
        o.save("remaster/frame-light-bracket-c.png",
               Bitmap.createBitmap(lightRim, lightRim.getWidth() / 2 - 2, lightRim.getHeight() / 2 - 2, 4, 4));
    }

    /** A frame in eight pieces: four corners of c pixels, four slivers from the middle of each side. */
    private static void pieces(Out o, String stem, Bitmap frame, int c) throws Exception {
        final int sw = frame.getWidth(), sh = frame.getHeight(), mx = sw / 2, my = sh / 2;
        final int[][] cut = {
            { 0, 0, c, c }, { sw - c, 0, c, c }, { 0, sh - c, c, c }, { sw - c, sh - c, c, c },
            { mx - 2, 0, 4, c }, { mx - 2, sh - c, 4, c }, { 0, my - 2, c, 4 }, { sw - c, my - 2, c, 4 },
        };
        final String[] piece = { "tl", "tr", "bl", "br", "t", "b", "l", "r" };
        for (int i = 0; i < cut.length; ++i) {
            o.save(stem + piece[i] + ".png",
                   Bitmap.createBitmap(frame, cut[i][0], cut[i][1], cut[i][2], cut[i][3]));
        }
    }

    /** Gold, lumber, oil and mana side by side in one strip; food on its own. */
    private static void resources(Atlas hud, Out o) throws Exception {
        final Bitmap strip = Bitmap.createBitmap(RESOURCE_ICON * RESOURCE_FRAMES.length, RESOURCE_ICON,
                                                 Bitmap.Config.ARGB_8888);
        final Canvas sc = new Canvas(strip);
        for (int i = 0; i < RESOURCE_FRAMES.length; ++i) {
            final Bitmap icon = scaled(hud.frame(RESOURCE_FRAMES[i]), RESOURCE_ICON, RESOURCE_ICON);
            sc.drawBitmap(icon, i * RESOURCE_ICON, 0, null);
            icon.recycle();
        }
        o.save(RESOURCE_STRIP, strip);
        o.save(FOOD, scaled(hud.frame(FOOD_FRAME), RESOURCE_ICON, RESOURCE_ICON));
        hud.close();
    }

    /** Where a set's pictures go: under ui/, inside the race's folder when there is one. */
    private static final class Out {
        private final File dataDir;
        private final String prefix;
        final String race;
        private final int[] made;

        Out(File dataDir, String race, int[] made) {
            this.dataDir = dataDir;
            this.race = race;
            this.prefix = race == null ? "" : race + "/";
            this.made = made;
        }

        void save(String path, Bitmap b) throws Exception {
            write(ROOT + prefix + path, b);
            ++made[0];
        }

        /** A picture with one place only, given from the data directory. */
        void saveAt(String path, Bitmap b) throws Exception {
            write(path, b);
            ++made[0];
        }

        private void write(String path, Bitmap b) throws Exception {
            {
                final File out = new File(dataDir, path);
                final File parent = out.getParentFile();
                if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                    throw new java.io.IOException("cannot make " + parent);
                }
                try (OutputStream o = new java.io.BufferedOutputStream(new FileOutputStream(out), 1 << 16)) {
                    png(b, o);
                }
                Provision.wrote(out);
            }
        }
    }

    /**
     * The picture as a PNG (8-bit RGBA, unfiltered rows), packed for speed, not for size: the
     * platform's own PNG writer packs as hard as it can and took most of the making's time on
     * the large panels, and these files are read once at the game's start and never sent anywhere.
     */
    private static void png(Bitmap b, OutputStream out) throws Exception {
        final int w = b.getWidth(), h = b.getHeight();
        out.write(new byte[] { (byte) 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A });
        final java.io.ByteArrayOutputStream head = new java.io.ByteArrayOutputStream();
        writeInt(head, w);
        writeInt(head, h);
        head.write(new byte[] { 8, 6, 0, 0, 0 });      // 8 bits, RGBA, no interlace
        chunk(out, "IHDR", head.toByteArray(), head.size());

        final java.util.zip.Deflater z = new java.util.zip.Deflater(java.util.zip.Deflater.BEST_SPEED);
        final java.io.ByteArrayOutputStream data = new java.io.ByteArrayOutputStream(w * h);
        final int[] px = new int[w];
        final byte[] row = new byte[1 + w * 4];
        final byte[] buf = new byte[1 << 16];
        for (int y = 0; y < h; ++y) {
            b.getPixels(px, 0, w, 0, y, w, 1);         // ARGB, not premultiplied
            for (int x = 0, i = 1; x < w; ++x) {
                final int p = px[x];
                row[i++] = (byte) (p >> 16);
                row[i++] = (byte) (p >> 8);
                row[i++] = (byte) p;
                row[i++] = (byte) (p >>> 24);
            }
            z.setInput(row);
            while (!z.needsInput()) {
                data.write(buf, 0, z.deflate(buf));
            }
        }
        z.finish();
        while (!z.finished()) {
            data.write(buf, 0, z.deflate(buf));
        }
        z.end();
        chunk(out, "IDAT", data.toByteArray(), data.size());
        chunk(out, "IEND", new byte[0], 0);
    }

    private static void chunk(OutputStream out, String type, byte[] body, int length) throws Exception {
        final byte[] name = type.getBytes("US-ASCII");
        final java.util.zip.CRC32 crc = new java.util.zip.CRC32();
        crc.update(name);
        crc.update(body, 0, length);
        writeInt(out, length);
        out.write(name);
        out.write(body, 0, length);
        writeInt(out, (int) crc.getValue());
    }

    private static void writeInt(OutputStream out, int v) throws java.io.IOException {
        out.write(v >>> 24);
        out.write(v >>> 16);
        out.write(v >>> 8);
        out.write(v);
    }

    /** One atlas of the game folder: its frame table and a decoder for regions of its picture. */
    private static final class Atlas {
        private final JSONObject frames;
        private final BitmapRegionDecoder decoder;

        Atlas(File gameFolder, String name) throws Exception {
            String json = GameLaunch.readText(new File(gameFolder, name + ".json"));
            if (json == null) {
                throw new java.io.IOException("no frame table " + name);
            }
            if (!json.isEmpty() && json.charAt(0) == '﻿') {
                json = json.substring(1);
            }
            frames = new JSONObject(json).getJSONObject("frames");
            decoder = BitmapRegionDecoder.newInstance(new File(gameFolder, name + ".png").getPath(), false);
        }

        /** The named frame at its full size. */
        Bitmap frame(String name) throws Exception {
            final JSONObject f = frames.getJSONObject(name).getJSONObject("frame");
            final Rect r = new Rect(f.getInt("x"), f.getInt("y"),
                                    f.getInt("x") + f.getInt("w"), f.getInt("y") + f.getInt("h"));
            final BitmapFactory.Options o = new BitmapFactory.Options();
            o.inPreferredConfig = Bitmap.Config.ARGB_8888;
            final Bitmap b = decoder.decodeRegion(r, o);
            if (b == null) {
                throw new java.io.IOException("frame " + name + " not decoded");
            }
            return b;
        }

        void close() {
            decoder.recycle();
        }
    }

    /**
     * The picture at w x h. Reducing goes by halves first (each half is an even mean of four
     * pixels), so that a picture brought to a third of its size is not thinned to dots.
     */
    private static Bitmap scaled(Bitmap src, int w, int h) {
        Bitmap cur = src;
        while (cur.getWidth() >= w * 2 && cur.getHeight() >= h * 2) {
            cur = Bitmap.createScaledBitmap(cur, cur.getWidth() / 2, cur.getHeight() / 2, true);
        }
        return (cur.getWidth() == w && cur.getHeight() == h) ? cur : Bitmap.createScaledBitmap(cur, w, h, true);
    }

    /** The picture turned by a quarter: 90 clockwise, -90 the other way. */
    private static Bitmap turned(Bitmap src, int degrees) {
        final Matrix m = new Matrix();
        m.postRotate(degrees);
        return Bitmap.createBitmap(src, 0, 0, src.getWidth(), src.getHeight(), m, false);
    }

    /**
     * The picture brought to w x h with its four corners kept as they are (l, t, r, b pixels
     * from each side) and its sides and middle stretched between them.
     */
    private static Bitmap nineSlice(Bitmap src, int w, int h, int l, int t, int r, int b) {
        final int sw = src.getWidth(), sh = src.getHeight();
        l = Math.min(l, Math.min(sw / 2, w / 2));
        r = Math.min(r, Math.min(sw / 2, w / 2));
        t = Math.min(t, Math.min(sh / 2, h / 2));
        b = Math.min(b, Math.min(sh / 2, h / 2));
        final int[] sx = { 0, l, sw - r, sw }, sy = { 0, t, sh - b, sh };
        final int[] dx = { 0, l, w - r, w }, dy = { 0, t, h - b, h };
        final Bitmap out = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        final Canvas c = new Canvas(out);
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                final int pw = sx[col + 1] - sx[col], ph = sy[row + 1] - sy[row];
                final int dw = dx[col + 1] - dx[col], dh = dy[row + 1] - dy[row];
                if (pw <= 0 || ph <= 0 || dw <= 0 || dh <= 0) {
                    continue;
                }
                final Bitmap part = Bitmap.createBitmap(src, sx[col], sy[row], pw, ph);
                final Bitmap fit = scaled(part, dw, dh);
                c.drawBitmap(fit, dx[col], dy[row], null);
                if (fit != part) {
                    fit.recycle();
                }
                if (part != src) {
                    part.recycle();
                }
            }
        }
        return out;
    }

    /** The picture in greys, darkened: a button that cannot be pressed. */
    private static Bitmap grayed(Bitmap src) {
        final int w = src.getWidth(), h = src.getHeight();
        final int[] px = new int[w * h];
        src.getPixels(px, 0, w, 0, 0, w, h);
        for (int i = 0; i < px.length; ++i) {
            final int a = px[i] >>> 24;
            if (a == 0) {
                continue;
            }
            final int lum = Math.max(0, (int) luminance(px[i]) - GRAYED_DROP);
            px[i] = a << 24 | lum << 16 | lum << 8 | lum;
        }
        final Bitmap out = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        out.setPixels(px, 0, w, 0, 0, w, h);
        return out;
    }

    /** The picture at a share of its brightness, its alpha as it is. */
    private static Bitmap dimmed(Bitmap src, double share) {
        final int w = src.getWidth(), h = src.getHeight();
        final int[] px = new int[w * h];
        src.getPixels(px, 0, w, 0, 0, w, h);
        for (int i = 0; i < px.length; ++i) {
            px[i] = (px[i] & 0xFF000000) | (int) ((px[i] >> 16 & 0xFF) * share) << 16
                | (int) ((px[i] >> 8 & 0xFF) * share) << 8 | (int) ((px[i] & 0xFF) * share);
        }
        final Bitmap out = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        out.setPixels(px, 0, w, 0, 0, w, h);
        return out;
    }

    /** The picture's light and dark kept, its colour replaced by the given one. */
    private static Bitmap tinted(Bitmap src, int[] rgb) {
        final int w = src.getWidth(), h = src.getHeight();
        final int[] px = new int[w * h];
        src.getPixels(px, 0, w, 0, 0, w, h);
        for (int i = 0; i < px.length; ++i) {
            final int a = px[i] >>> 24;
            if (a == 0) {
                continue;
            }
            final double lum = luminance(px[i]) / 255.0;
            px[i] = a << 24 | (int) (rgb[0] * lum) << 16 | (int) (rgb[1] * lum) << 8 | (int) (rgb[2] * lum);
        }
        final Bitmap out = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        out.setPixels(px, 0, w, 0, 0, w, h);
        return out;
    }

    private static double luminance(int argb) {
        return 0.299 * (argb >> 16 & 0xFF) + 0.587 * (argb >> 8 & 0xFF) + 0.114 * (argb & 0xFF);
    }
}
