package net.wargushd.game;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.BitmapRegionDecoder;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Typeface;

import org.json.JSONObject;

import java.io.File;

/**
 * What the launcher's screens draw with.
 *
 * Layouts are written in logical units on a canvas 3840 wide whose origin is the screen's
 * top-left corner; one unit is screen width / 3840 pixels. Text is sized by the height of
 * its capitals, placed by the left edge of its ink, and light text carries the game's drop
 * shadow. Game art is never shipped: pictures, the typeface and the strings are read in
 * place from the folder the gate verified, and every lookup has a plain fallback.
 */
final class SkinKit {
    /** The game's drop shadow under light text, logical units. */
    private static final float SHADOW = 5f;

    private final File folder;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
    private final Paint image = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Rect bounds = new Rect();
    private final RectF box = new RectF();
    /** Text size that gives a capital letter a height of one pixel, for the loaded typeface. */
    private final float sizePerCap;
    private JSONObject strings;
    private static final java.util.HashMap<String, JSONObject> atlasFrames = new java.util.HashMap<>();
    private static final java.util.HashMap<String, BitmapRegionDecoder> atlasDecoders =
        new java.util.HashMap<>();

    /** Pixels per logical unit; set by begin(). */
    float unit = 1f;
    /** Height of the screen in logical units; set by begin(). */
    float height = 2400f;

    /** The game's eleven languages: the string table's name and the key of the language's name. */
    static final String[][] LANGUAGES = {
        { "enUS", "en-US" }, { "deDE", "de-DE" }, { "esES", "es-Es" }, { "esMX", "es-Mx" },
        { "frFR", "fr-FR" }, { "jaJP", "ja-JP" }, { "koKR", "ko-KR" }, { "ptBR", "pt-BR" },
        { "ruRU", "ru-RU" }, { "zhCN", "zh-CN" }, { "zhTW", "zh-TW" },
    };
    private static final String LANGUAGE_STORE = "options", LANGUAGE_KEY = "language";

    /**
     * The language in use: the one chosen on this device, else the device's own when the game has
     * it, else English.
     */
    static String language(Context c) {
        return language(c, ModStore.languageMod(c));
    }

    /** As language(c), with the language mod that is switched on (null when none is). */
    private static String language(Context c, ModStore.Mod mod) {
        final String chosen = c.getSharedPreferences(LANGUAGE_STORE, Context.MODE_PRIVATE)
            .getString(LANGUAGE_KEY, null);
        // A mod's language counts only while that mod is switched on (M2_LANGUAGE_MOD_SPEC).
        if (chosen != null && mod != null && chosen.equals(mod.language)) {
            return chosen;
        }
        if (chosen != null) {
            for (String[] l : LANGUAGES) {
                if (l[0].equals(chosen)) {
                    return chosen;
                }
            }
        }
        final java.util.Locale here = java.util.Locale.getDefault();
        final String exact = here.getLanguage() + here.getCountry();
        String sameTongue = null;
        for (String[] l : LANGUAGES) {
            if (l[0].equalsIgnoreCase(exact)) {
                return l[0];
            }
            if (sameTongue == null && l[0].substring(0, 2).equalsIgnoreCase(here.getLanguage())) {
                sameTongue = l[0];
            }
        }
        return (sameTongue != null) ? sameTongue : "enUS";
    }

    static void chooseLanguage(Context c, String code) {
        c.getSharedPreferences(LANGUAGE_STORE, Context.MODE_PRIVATE).edit()
            .putString(LANGUAGE_KEY, code).apply();
    }

    /**
     * The rows of the Languages list: the game's languages, then the language of the mod that is
     * switched on as { code, "", its own name }.
     */
    static String[][] languages(Context c) {
        final ModStore.Mod mod = ModStore.languageMod(c);
        if (mod == null) {
            return LANGUAGES;
        }
        final String[][] all = java.util.Arrays.copyOf(LANGUAGES, LANGUAGES.length + 1);
        all[LANGUAGES.length] = new String[] { mod.language, "", mod.languageName };
        return all;
    }

    /** The language chosen before a language mod took its place ("" = none was chosen). */
    private static final String LANGUAGE_BEFORE_MOD = "languageBeforeMod";
    /** The language mod whose language was already selected once (round 3.4.66 F2). */
    private static final String LANGUAGE_MOD_FOLLOWED = "languageModFollowed";

    /**
     * Follow the mods after one was switched, installed or removed: a language mod switched on
     * selects its language and keeps the one in use - once, when it comes on; a language the
     * player picks afterwards stays through switches of other mods. With none on, a mod's
     * language gives way to the one kept. @return true when the stored language changed (the
     * shell starts over).
     */
    static boolean followMods(Context c) {
        final android.content.SharedPreferences store =
            c.getSharedPreferences(LANGUAGE_STORE, Context.MODE_PRIVATE);
        final String chosen = store.getString(LANGUAGE_KEY, null);
        final ModStore.Mod mod = ModStore.languageMod(c);
        if (mod != null) {
            // Already followed: this mod's id is kept, or (stores from before the key) the
            // language before the mod is.
            final String followed = store.getString(LANGUAGE_MOD_FOLLOWED, null);
            if (mod.id.equals(followed) || (followed == null && store.contains(LANGUAGE_BEFORE_MOD))) {
                if (followed == null) {
                    store.edit().putString(LANGUAGE_MOD_FOLLOWED, mod.id).apply();
                }
                return false;
            }
            final android.content.SharedPreferences.Editor e = store.edit();
            e.putString(LANGUAGE_MOD_FOLLOWED, mod.id);
            if (mod.language.equals(chosen)) {
                e.apply();
                return false;
            }
            if (!store.contains(LANGUAGE_BEFORE_MOD)) {
                e.putString(LANGUAGE_BEFORE_MOD, isGameLanguage(chosen) ? chosen : "");
            }
            e.putString(LANGUAGE_KEY, mod.language).apply();
            return true;
        }
        if (store.contains(LANGUAGE_MOD_FOLLOWED)) {
            store.edit().remove(LANGUAGE_MOD_FOLLOWED).apply();
        }
        if (!store.contains(LANGUAGE_BEFORE_MOD) && (chosen == null || isGameLanguage(chosen))) {
            return false;
        }
        final String before = store.getString(LANGUAGE_BEFORE_MOD, "");
        final android.content.SharedPreferences.Editor e = store.edit().remove(LANGUAGE_BEFORE_MOD);
        if (chosen != null && !isGameLanguage(chosen)) {
            if (isGameLanguage(before)) {
                e.putString(LANGUAGE_KEY, before);
            } else {
                e.remove(LANGUAGE_KEY);
            }
            e.apply();
            return true;
        }
        e.apply();   // a game language was chosen while the mod was on: it stays
        return false;
    }

    private static boolean isGameLanguage(String code) {
        for (String[] l : LANGUAGES) {
            if (l[0].equals(code)) {
                return true;
            }
        }
        return false;
    }

    /** The face that has the language's letters: the game's own, or the game folder's Noto face. */
    static String fontOf(String language) {
        if (language.equals("jaJP")) {
            return "Data/Font/NotoSansJP-Regular.ttf";
        } else if (language.equals("koKR")) {
            return "Data/Font/NotoSansKR-Regular.ttf";
        } else if (language.equals("zhCN")) {
            return "Data/Font/NotoSansSC-Regular.ttf";
        } else if (language.equals("zhTW")) {
            return "Data/Font/NotoSansTC-Regular.ttf";
        }
        return "Data/Font/BLQ55.ttf";
    }

    private final String language;
    /** The package of the language in use when that is a mod's language, else null. */
    private final File modFolder;

    SkinKit(Context c) {
        final String path = GateActivity.rememberedFolder(c);
        folder = (path == null) ? null : new File(path);
        final ModStore.Mod mod = ModStore.languageMod(c);
        language = language(c, mod);
        modFolder = (mod != null && language.equals(mod.language))
            ? new File(ModStore.directory(c), mod.id) : null;
        ownWords = (modFolder != null)
            ? readJson(new File(modFolder, "strings/launcher_" + language + ".json"))
            : readOwn(c, language);
        Typeface face = Typeface.DEFAULT;
        if (folder != null) {
            try {
                face = Typeface.createFromFile(new File(folder, fontOf(language)));
            } catch (RuntimeException e) {
                face = Typeface.DEFAULT;
            }
        }
        text.setTypeface(face);
        // The design's tables set every line by the glyphs' plain advances; the font's pair
        // kerning is left out so that a line is as wide as its table says.
        text.setFontFeatureSettings("'kern' 0");
        // Unhinted, linear advances: hinting rounds every glyph's advance to whole pixels,
        // which shortened a line by about 4 % against its table.
        text.setHinting(Paint.HINTING_OFF);
        text.setLinearText(true);
        line.setStyle(Paint.Style.STROKE);
        text.setTextSize(1000f);
        // The capital's height is taken from the glyph's outline. getTextBounds gives the box of
        // the drawn pixels, which is taller, and text came out about 3.5 % too small.
        final android.graphics.Path outline = new android.graphics.Path();
        final android.graphics.RectF box = new android.graphics.RectF();
        text.getTextPath("H", 0, 1, 0f, 0f, outline);
        outline.computeBounds(box, true);
        text.getTextBounds("H", 0, 1, bounds);
        sizePerCap = 1000f / ((box.height() > 0f) ? box.height() : bounds.height());
    }

    /** Call at the start of every draw with the view's size in pixels. */
    void begin(int widthPx, int heightPx) {
        unit = widthPx / Screen.CANVAS_W;
        height = heightPx / unit;
    }

    /** The pixel rect of a logical rect. The returned object is reused by the next call. */
    RectF rect(float x, float y, float w, float h) {
        box.set(x * unit, y * unit, (x + w) * unit, (y + h) * unit);
        return box;
    }

    void fill(Canvas c, float x, float y, float w, float h, int color, float radius) {
        fill.setColor(color);
        if (radius > 0f) {
            c.drawRoundRect(rect(x, y, w, h), radius * unit, radius * unit, fill);
        } else {
            c.drawRect(rect(x, y, w, h), fill);
        }
    }

    /** A rounded outline two units wide that stays inside the rect. */
    void outline(Canvas c, float x, float y, float w, float h, int color, float radius) {
        final float width = Math.max(1f, 2f * unit);
        final float half = width / 2f;
        final RectF r = rect(x, y, w, h);
        line.setColor(color);
        line.setStrokeWidth(width);
        c.drawRoundRect(r.left + half, r.top + half, r.right - half, r.bottom - half,
                        radius * unit, radius * unit, line);
    }

    private void prepare(float cap) {
        text.setTextSize(cap * unit * sizePerCap);
        text.setTextAlign(Paint.Align.LEFT);
    }

    private void put(Canvas c, String s, float xPx, float baselinePx, int color, boolean shadow) {
        if (shadow) {
            text.setColor(0xFF000000);
            c.drawText(s, xPx + SHADOW * unit, baselinePx + SHADOW * unit, text);
        }
        text.setColor(color);
        c.drawText(s, xPx, baselinePx, text);
    }

    /** The outline's box of the first n characters, in pixels from the pen's start. */
    private final android.graphics.Path inkPath = new android.graphics.Path();
    private final RectF ink = new RectF();

    /**
     * Text is placed by its outline, not by the box of its drawn pixels: that box is rounded
     * outwards to whole pixels, and a right-aligned figure ended up to 3 units short of its edge.
     */
    private void ink(String s, int n) {
        text.getTextPath(s, 0, n, 0f, 0f, inkPath);
        inkPath.computeBounds(ink, true);
        if (ink.isEmpty()) {
            text.getTextBounds(s, 0, n, bounds);
            ink.set(bounds);
        }
    }

    /** Text whose ink starts at x and whose capitals start at y. */
    void textTop(Canvas c, String s, float x, float y, float cap, int color, boolean shadow) {
        final float use = fittedCap(cap);
        if (s == null || s.isEmpty()) {
            return;
        }
        prepare(use);
        ink(s, 1);
        put(c, s, x * unit - ink.left, (y + (cap + use) / 2f) * unit, color, shadow);
    }

    /** Text whose ink starts at x, with its capitals' box centred in a region h high at y. */
    void textMiddle(Canvas c, String s, float x, float y, float h, float cap, int color,
                    boolean shadow) {
        final float use = fittedCap(cap);
        if (s == null || s.isEmpty()) {
            return;
        }
        prepare(use);
        ink(s, 1);
        put(c, s, x * unit - ink.left, (y + (h + use) / 2f) * unit, color, shadow);
    }

    /** As textMiddle, but the ink ends at xRight. */
    void textMiddleRight(Canvas c, String s, float xRight, float y, float h, float cap, int color,
                         boolean shadow) {
        final float use = fittedCap(cap);
        if (s == null || s.isEmpty()) {
            return;
        }
        prepare(use);
        ink(s, s.length());
        put(c, s, xRight * unit - ink.right, (y + (h + use) / 2f) * unit, color, shadow);
    }

    /** Text centred by its ink width in a region, with its capitals' box centred vertically. */
    void textCentre(Canvas c, String s, float x, float y, float w, float h, float cap, int color,
                    boolean shadow) {
        // A centred text never runs out of its region: one that no caller fitted is fitted here.
        if (fitted == 0f && s != null) {
            s = fit(s, cap, w - 24f);
        }
        final float use = fittedCap(cap);
        if (s == null || s.isEmpty()) {
            return;
        }
        prepare(use);
        ink(s, s.length());
        final float left = (x + w / 2f) * unit - ink.width() / 2f - ink.left;
        put(c, s, left, (y + (h + use) / 2f) * unit, color, shadow);
    }

    /** Text centred by its ink width in a region w wide, its capitals starting at y. */
    void textCentreTop(Canvas c, String s, float x, float y, float w, float cap, int color,
                       boolean shadow) {
        if (fitted == 0f && s != null) {
            s = fit(s, cap, w - 24f);
        }
        final float use = fittedCap(cap);
        if (s == null || s.isEmpty()) {
            return;
        }
        prepare(use);
        ink(s, s.length());
        final float left = (x + w / 2f) * unit - ink.width() / 2f - ink.left;
        put(c, s, left, (y + (cap + use) / 2f) * unit, color, shadow);
    }

    /** Text whose ink ends at xRight and whose capitals start at y. */
    void textTopRight(Canvas c, String s, float xRight, float y, float cap, int color,
                      boolean shadow) {
        final float use = fittedCap(cap);
        if (s == null || s.isEmpty()) {
            return;
        }
        prepare(use);
        ink(s, s.length());
        put(c, s, xRight * unit - ink.right, (y + (cap + use) / 2f) * unit, color, shadow);
    }

    /** How wide a text runs at a cap height, in logical units. */
    float width(String s, float cap) {
        prepare(cap);
        return text.measureText(s) / unit;
    }

    /** A text too long for its place is drawn no smaller than this part of its capital height. */
    private static final float FIT_FLOOR = 0.70f;
    /** A heading may be made smaller still before it is cut (owner's decision 84, CAPTION-FIT-B). */
    private static final float HEADING_FLOOR = 0.50f;
    /** The capital height fit() chose for the text drawn next, and the height it was asked at. */
    private float fitted, fittedFor;

    /**
     * A text made to fit a width (logical units) at a cap height: when it is too long its
     * letters are made smaller, down to FIT_FLOOR of the height, and only then is it cut,
     * ending in an ellipsis. The smaller height is kept for the placement routine that draws
     * the returned text next (it must be called with the same cap height); the text stays
     * centred on the line the full height would have had.
     */
    String fit(String s, float cap, float width) {
        return fit(s, cap, width, FIT_FLOOR);
    }

    /** As fit, for a heading: its letters may shrink down to HEADING_FLOOR of the height. */
    String fitHeading(String s, float cap, float width) {
        return fit(s, cap, width, HEADING_FLOOR);
    }

    private String fit(String s, float cap, float width, float floor) {
        fitted = 0f;
        if (s == null) {
            return "";
        }
        prepare(cap);
        final float max = width * unit;
        final float full = text.measureText(s);
        if (full <= max) {
            return s;
        }
        fitted = cap * Math.max(floor, max / full);
        fittedFor = cap;
        prepare(fitted);
        // The width does not fall exactly with the size (round 3.4.66 F1: "Getötete Gegner"
        // needs 90 % and was cut by a hair): narrow again on what was measured, to the floor.
        for (int i = 0; i < 4 && fitted > cap * floor; ++i) {
            final float now = text.measureText(s);
            if (now <= max) {
                return s;
            }
            fitted = Math.max(cap * floor, fitted * (max / now) * 0.995f);
            prepare(fitted);
        }
        if (text.measureText(s) <= max + 0.5f) {
            return s;
        }
        final String dots = "...";
        int n = s.length();
        while (n > 0 && text.measureText(s, 0, n) + text.measureText(dots) > max) {
            --n;
        }
        return s.substring(0, n).trim() + dots;
    }

    /** The height to draw at: the one fit() kept for this text, else the one asked. */
    private float fittedCap(float cap) {
        final float use = (fitted > 0f && fittedFor == cap) ? fitted : cap;
        fitted = 0f;
        return use;
    }

    /**
     * Draw a frame picture stretched over a logical rect with its borders kept: the corners
     * are not scaled along the edges they do not belong to. `inset` is the border's width in
     * the picture's own pixels and, on screen, in logical units.
     */
    void nine(Canvas c, Bitmap b, float x, float y, float w, float h, int inset) {
        if (b == null) {
            return;
        }
        final int bw = b.getWidth(), bh = b.getHeight();
        final float d = Math.min(inset * unit, Math.min(w, h) * unit / 2f);
        final int[] sx = { 0, inset, bw - inset, bw };
        final int[] sy = { 0, inset, bh - inset, bh };
        final float x0 = x * unit, y0 = y * unit, x1 = (x + w) * unit, y1 = (y + h) * unit;
        final float[] dx = { x0, x0 + d, x1 - d, x1 };
        final float[] dy = { y0, y0 + d, y1 - d, y1 };
        for (int j = 0; j < 3; ++j) {
            for (int i = 0; i < 3; ++i) {
                bounds.set(sx[i], sy[j], sx[i + 1], sy[j + 1]);
                box.set(dx[i], dy[j], dx[i + 1], dy[j + 1]);
                c.drawBitmap(b, bounds, box, image);
            }
        }
    }

    /** Draw a picture so that it covers the whole view, centred, keeping its proportions. */
    void backdrop(Canvas c, Bitmap b, int widthPx, int heightPx, int fallbackColor) {
        c.drawColor(fallbackColor);
        if (b == null) {
            return;
        }
        final float s = Math.max((float) widthPx / b.getWidth(), (float) heightPx / b.getHeight());
        final float bw = b.getWidth() * s, bh = b.getHeight() * s;
        box.set((widthPx - bw) / 2f, (heightPx - bh) / 2f, (widthPx + bw) / 2f, (heightPx + bh) / 2f);
        c.drawBitmap(b, null, box, image);
    }

    /** Draw a picture into a logical rect. */
    /** As picture, with every pixel of the bitmap kept sharp (a map drawn one pixel a tile). */
    void pictureSharp(Canvas c, Bitmap b, float x, float y, float w, float h) {
        if (b != null) {
            c.drawBitmap(b, null, rect(x, y, w, h), sharp);
        }
    }
    private final Paint sharp = new Paint();
    {
        sharp.setFilterBitmap(false);
    }

    void picture(Canvas c, Bitmap b, float x, float y, float w, float h) {
        if (b != null) {
            c.drawBitmap(b, null, rect(x, y, w, h), image);
        }
    }

    /** A picture file of the game, decoded no larger than needed to cover w x h pixels. */
    Bitmap file(String relative, int widthPx, int heightPx) {
        if (folder == null) {
            return null;
        }
        final String path = new File(folder, relative).getPath();
        try {
            final BitmapFactory.Options o = new BitmapFactory.Options();
            o.inJustDecodeBounds = true;
            BitmapFactory.decodeFile(path, o);
            o.inSampleSize = sample(o.outWidth, o.outHeight, widthPx, heightPx);
            o.inJustDecodeBounds = false;
            return BitmapFactory.decodeFile(path, o);
        } catch (RuntimeException e) {
            return null;
        } catch (OutOfMemoryError e) {
            return null;
        }
    }

    /**
     * One frame of a texture atlas (Data/skins/NAME.png with its NAME.json), cut out with a
     * region decoder so that the atlas itself is never decoded whole, and reduced while
     * decoding to about the size it will be shown at.
     */
    Bitmap atlasFrame(String atlas, String frame, int widthPx, int heightPx) {
        return atlasFrameIn("Data/skins", atlas, frame, widthPx, heightPx);
    }

    /** As atlasFrame, for an atlas in another directory of the game folder. */
    Bitmap atlasFrameIn(String dir, String atlas, String frame, int widthPx, int heightPx) {
        if (folder == null) {
            return null;
        }
        try {
            // The frame table and the picture's decoder are opened once per atlas and kept.
            final String name = dir + "/" + atlas, held = folder.getPath() + "/" + name;
            JSONObject frames;
            BitmapRegionDecoder d;
            synchronized (atlasFrames) {
                frames = atlasFrames.get(held);
                if (frames == null) {
                    final String json = GameLaunch.readText(new File(folder, name + ".json"));
                    if (json == null) {
                        return null;
                    }
                    frames = new JSONObject(strip(json)).getJSONObject("frames");
                    atlasFrames.put(held, frames);
                }
                d = atlasDecoders.get(held);
                if (d == null) {
                    d = BitmapRegionDecoder.newInstance(new File(folder, name + ".png").getPath(), false);
                    atlasDecoders.put(held, d);
                }
            }
            final JSONObject f = frames.getJSONObject(frame).getJSONObject("frame");
            final Rect r = new Rect(f.getInt("x"), f.getInt("y"),
                                    f.getInt("x") + f.getInt("w"), f.getInt("y") + f.getInt("h"));
            final BitmapFactory.Options o = new BitmapFactory.Options();
            o.inSampleSize = sample(r.width(), r.height(), widthPx, heightPx);
            o.inPreferredConfig = Bitmap.Config.ARGB_8888;
            return d.decodeRegion(r, o);
        } catch (Exception e) {
            return null;
        } catch (OutOfMemoryError e) {
            return null;
        }
    }

    /**
     * Several frames of one atlas at full size, cut from as few decoded bands as hold them
     * (a region of a PNG is reached by decoding the rows above it, so one frame at a time is
     * slow). A frame that is missing gives null; when the atlas cannot be read, all are null.
     */
    Bitmap[] atlasFramesIn(String dir, String atlas, String[] names) {
        final Bitmap[] out = new Bitmap[names.length];
        if (folder == null) {
            return out;
        }
        try {
            final String name = dir + "/" + atlas, held = folder.getPath() + "/" + name;
            JSONObject frames;
            BitmapRegionDecoder d;
            synchronized (atlasFrames) {
                frames = atlasFrames.get(held);
                if (frames == null) {
                    final String json = GameLaunch.readText(new File(folder, name + ".json"));
                    if (json == null) {
                        return out;
                    }
                    frames = new JSONObject(strip(json)).getJSONObject("frames");
                    atlasFrames.put(held, frames);
                }
                d = atlasDecoders.get(held);
                if (d == null) {
                    d = BitmapRegionDecoder.newInstance(new File(folder, name + ".png").getPath(), false);
                    atlasDecoders.put(held, d);
                }
            }
            final Rect[] rects = new Rect[names.length];
            final java.util.ArrayList<Integer> order = new java.util.ArrayList<>();
            for (int i = 0; i < names.length; ++i) {
                final JSONObject holder = frames.optJSONObject(names[i]);
                if (holder == null) {
                    continue;
                }
                final JSONObject f = holder.getJSONObject("frame");
                rects[i] = new Rect(f.getInt("x"), f.getInt("y"),
                                    f.getInt("x") + f.getInt("w"), f.getInt("y") + f.getInt("h"));
                order.add(i);
            }
            java.util.Collections.sort(order, new java.util.Comparator<Integer>() {
                @Override
                public int compare(Integer a, Integer b) {
                    return rects[a].top - rects[b].top;
                }
            });
            final BitmapFactory.Options o = new BitmapFactory.Options();
            o.inPreferredConfig = Bitmap.Config.ARGB_8888;
            int from = 0;
            while (from < order.size()) {
                // A band: the frames whose rows overlap, one after another.
                final Rect band = new Rect(rects[order.get(from)]);
                int to = from + 1;
                while (to < order.size() && rects[order.get(to)].top < band.bottom) {
                    band.union(rects[order.get(to)]);
                    ++to;
                }
                final Bitmap whole = d.decodeRegion(band, o);
                for (int n = from; n < to; ++n) {
                    final Rect r = rects[order.get(n)];
                    out[order.get(n)] = Bitmap.createBitmap(whole, r.left - band.left, r.top - band.top,
                                                            r.width(), r.height());
                }
                whole.recycle();
                from = to;
            }
        } catch (Exception e) {
            // what was cut stays; the rest is null
        } catch (OutOfMemoryError e) {
            // as above
        }
        return out;
    }

    /** The largest power-of-two reduction that still leaves at least the wanted size. */
    private static int sample(int srcW, int srcH, int wantW, int wantH) {
        int s = 1;
        while (srcW / (s * 2) >= wantW && srcH / (s * 2) >= wantH) {
            s *= 2;
        }
        return s;
    }

    /** A text of the game's own string table in the language in use, or the fallback. */
    String string(String key, String fallback) {
        if (strings == null) {
            strings = new JSONObject();
            if (folder != null) {
                // A mod's language: the game's English table, the mod's texts laid over it.
                final String json = GameLaunch.readText(new File(folder,
                    "Data/Strings/" + (modFolder != null ? "enUS" : language) + ".json"));
                if (json != null) {
                    try {
                        strings = new JSONObject(strip(json));
                    } catch (Exception e) {
                        strings = new JSONObject();
                    }
                }
            }
            if (modFolder != null) {
                final JSONObject own = readJson(new File(modFolder, "strings/" + language + ".json"));
                for (java.util.Iterator<String> keys = own.keys(); keys.hasNext();) {
                    final String name = keys.next();
                    final String text = own.optString(name, "");
                    if (!text.isEmpty()) {
                        try {
                            strings.put(name, text);
                        } catch (Exception e) {
                            // not taken: the English text stays
                        }
                    }
                }
            }
        }
        final String s = (key == null) ? "" : strings.optString(key, "");
        return s.isEmpty() ? own(fallback) : s;
    }

    /** The launcher's own words in the language in use: English text to that language's. */
    private final JSONObject ownWords;

    private static JSONObject readOwn(Context c, String language) {
        try (java.io.InputStream in = c.getAssets().open("launcher_strings/" + language + ".json")) {
            final java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            final byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
            return new JSONObject(strip(out.toString("UTF-8")));
        } catch (Exception e) {
            // English, or a language without a file: the words stay as they are written.
            return new JSONObject();
        }
    }

    /**
     * A word of the launcher's own, for which the game's string table has no text: its text
     * in the language in use, or the English it is written in. Texts with a changing part
     * carry {0}, {1} for it (see own(String, Object...)).
     */
    String own(String english) {
        if (english == null) {
            return "";
        }
        final String s = ownWords.optString(english, "");
        return s.isEmpty() ? english : s;
    }

    /** As own, with {0}, {1} ... replaced by the given values. */
    String own(String english, Object... values) {
        String s = own(english);
        for (int i = 0; i < values.length; ++i) {
            s = s.replace("{" + i + "}", String.valueOf(values[i]));
        }
        return s;
    }

    /** A JSON object from a file; an empty one when it cannot be read. */
    private static JSONObject readJson(File f) {
        final String json = GameLaunch.readText(f);
        if (json != null) {
            try {
                return new JSONObject(strip(json));
            } catch (Exception e) {
                // not JSON: as absent
            }
        }
        return new JSONObject();
    }

    /** A JSON text without a leading byte-order mark. */
    private static String strip(String json) {
        return (!json.isEmpty() && json.charAt(0) == '﻿') ? json.substring(1) : json;
    }
}
