package net.wargushd.game;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.MotionEvent;
import android.view.View;

import java.io.File;

/**
 * Preparing screen: brings the unpacked content in line with the installed package and
 * shows how far that is.
 *
 * The game folder is verified by the time this screen appears, so it is drawn with the
 * game's loading still and typeface, both read in place. Each bar reports work that was
 * really done; a part that does not exist yet says so instead of moving.
 */
public class PrepActivity extends Activity {
    /** Intent extra (debuggable builds): stay on the finished screen until it is touched. */
    public static final String EXTRA_HOLD = "net.wargushd.game.HOLD";

    // Layout, logical units; vertical positions are relative to the panel's top.
    private static final float PANEL_X = 720f, PANEL_W = 2400f, PANEL_H = 1080f;
    private static final float TEXT_X = 840f, TEXT_W = 2160f;
    private static final float TITLE_Y = 100f, ROW_Y = 300f, ROW_PITCH = 206.25f;
    private static final float BAR_DY = 84f, BAR_H = 24f;
    // The failed screen (decision 83, board 5A of 2026-10-18): the panel grows to 1380 and its
    // foot holds Choose another folder at the left and Try again (gold) at the right.
    private static final float PANEL_H_FAILED = 1380f, BTN_Y = 1164f, BTN_H = 120f;
    private static final float BTN_LEFT_W = 900f, BTN_RIGHT_W = 720f;
    private static final float CAP_TITLE = 60f, CAP_BODY = 44f;
    private static final float SHADOW = 5f;

    private static final int PANEL_FILL = 0xDE140F0A, PANEL_LINE = 0xFF92743E;
    private static final int GOLD = 0xFFF0D250, INK = 0xFFE8E2D4, WARN = 0xFFE8705A;
    private static final int TRACK = 0xFF50463A, BTN_FILL = 0xFF2A2218;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private PrepView view;
    private boolean hold;

    // Progress, written on the UI thread only.
    private int unpackDone, unpackTotal;
    private int artPresent = -1, artExpected;
    /** While the art is made: files of the package's list written so far, of how many (-1: not yet). */
    private int artMade = -1, artMaking;
    private boolean finished, failed;

    // The worker's latest counts, handed to the UI thread by one pending post at a time: a post
    // per file flooded the main thread's queue (LAUNCHER-PREPARE-NOT-RESPONDING).
    private final Object counts = new Object();
    private int nextUnpackDone, nextUnpackTotal, nextMade = -1, nextMaking;
    private boolean countsPosted;
    private final Runnable showCounts = new Runnable() {
        @Override
        public void run() {
            synchronized (counts) {
                countsPosted = false;
                unpackDone = nextUnpackDone;
                unpackTotal = nextUnpackTotal;
                artMade = nextMade;
                artMaking = nextMaking;
            }
            view.invalidate();
        }
    };

    /** Worker thread: the counts changed; the screen is told once per frame at most. */
    private void countsChanged() {
        synchronized (counts) {
            if (countsPosted) {
                return;
            }
            countsPosted = true;
        }
        ui.postDelayed(showCounts, 16L);
    }

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        hold = (getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0
            && getIntent().getBooleanExtra(EXTRA_HOLD, false);
        view = new PrepView(this);
        setContentView(view);
        Screen.useWhole(this);
        new Thread(new Runnable() {
            @Override
            public void run() {
                work();
            }
        }, "prepare").start();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            Screen.useWhole(this);
        }
    }

    @Override
    public void onBackPressed() {
        // The work is short and must not be left half done.
    }

    /** Worker thread: unpack, then count the interface art. */
    private void work() {
        // Below the main thread: the unpacking and the making of fonts and art (minutes on a slow
        // device) must leave the screen free to draw and take taps, or Android says the app is
        // not responding (LAUNCHER-PREPARE-NOT-RESPONDING).
        android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_BACKGROUND);
        final long began = android.os.SystemClock.uptimeMillis();
        final int written = Provision.unpack(this, new Provision.Progress() {
            @Override
            public void unpacked(int done, int total) {
                synchronized (counts) {
                    nextUnpackDone = done;
                    nextUnpackTotal = total;
                }
                countsChanged();
            }

            @Override
            public void made(int done, int expected) {
                synchronized (counts) {
                    nextMade = done;
                    nextMaking = expected;
                }
                countsChanged();
            }
        });
        final int[] art = Provision.countInterfaceArt(this);
        android.util.Log.i("WargusHD", "LAUNCHER prepare done in "
            + (android.os.SystemClock.uptimeMillis() - began) + " ms (worker thread)");
        ui.post(new Runnable() {
            @Override
            public void run() {
                showCounts.run();
                failed = written < 0;
                artPresent = art[0];
                artExpected = art[1];
                finished = true;
                view.invalidate();
                if (!failed && !hold) {
                    proceed();
                }
            }
        });
    }

    private void proceed() {
        startActivity(new Intent(this, SetupActivity.class));
        finish();
    }

    /** The gate, where another game folder is chosen; it comes back through the entry. */
    private void chooseAnotherFolder() {
        startActivity(new Intent(this, GateActivity.class));
        finish();
    }

    /** Backdrop, one panel, a title and three rows with a bar each. */
    private final class PrepView extends View {
        private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
        private final Paint image = new Paint(Paint.FILTER_BITMAP_FLAG);
        private final Rect bounds = new Rect();
        private final RectF box = new RectF();
        private final float sizePerCap;
        private volatile Bitmap backdrop;
        private boolean backdropTried;
        /** The launcher's own words in the language in use. */
        private final SkinKit kit;

        private float unit;
        private float panelTop;

        PrepView(Context c) {
            super(c);
            kit = new SkinKit(c);
            Typeface face = Typeface.DEFAULT;
            final String folder = GateActivity.rememberedFolder(c);
            if (folder != null) {
                try {
                    // The face that has the language's letters (the game's own, or a Noto face).
                    face = Typeface.createFromFile(new File(folder, SkinKit.fontOf(SkinKit.language(c))));
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

        /** The loading still, decoded no larger than the screen needs. */
        private void loadBackdrop(int w, int h) {
            final String folder = GateActivity.rememberedFolder(getContext());
            if (folder == null) {
                return;
            }
            final String path = new File(folder, "Data/Backgrounds/loadscreen.png").getPath();
            try {
                final BitmapFactory.Options o = new BitmapFactory.Options();
                o.inJustDecodeBounds = true;
                BitmapFactory.decodeFile(path, o);
                int sample = 1;
                while (o.outWidth / (sample * 2) >= w && o.outHeight / (sample * 2) >= h) {
                    sample *= 2;
                }
                o.inJustDecodeBounds = false;
                o.inSampleSize = sample;
                backdrop = BitmapFactory.decodeFile(path, o);
            } catch (RuntimeException e) {
                backdrop = null;
            } catch (OutOfMemoryError e) {
                backdrop = null;
            }
        }

        private RectF rect(float x, float y, float w, float h) {
            box.set(x * unit, panelTop + y * unit, (x + w) * unit, panelTop + (y + h) * unit);
            return box;
        }

        /** A line whose capitals start at y, with the game's drop shadow. */
        private void drawLine(Canvas c, String s, float y, float cap, int color) {
            text.setTextSize(cap * unit * sizePerCap);
            text.setTextAlign(Paint.Align.LEFT);
            text.getTextBounds(s, 0, Math.min(1, s.length()), bounds);
            final float x = TEXT_X * unit - bounds.left;
            final float baseline = panelTop + (y + cap) * unit;
            text.setColor(0xFF000000);
            c.drawText(s, x + SHADOW * unit, baseline + SHADOW * unit, text);
            text.setColor(color);
            c.drawText(s, x, baseline, text);
        }

        private void drawBar(Canvas c, float y, float fraction) {
            final float radius = BAR_H / 2f * unit;
            fill.setColor(TRACK);
            c.drawRoundRect(rect(TEXT_X, y, TEXT_W, BAR_H), radius, radius, fill);
            if (fraction > 0f) {
                fill.setColor(GOLD);
                c.drawRoundRect(rect(TEXT_X, y, TEXT_W * Math.min(1f, fraction), BAR_H),
                                radius, radius, fill);
            }
        }

        @Override
        protected void onDraw(Canvas c) {
            final int w = getWidth(), h = getHeight();
            unit = w / Screen.CANVAS_W;
            final float panelH = failed ? PANEL_H_FAILED : PANEL_H;
            panelTop = (h - panelH * unit) / 2f;

            c.drawColor(0xFF0D1114);
            if (!backdropTried) {
                // Decoded beside the main thread; the screen is drawn again when it is there.
                backdropTried = true;
                new Thread(new Runnable() {
                    @Override
                    public void run() {
                        loadBackdrop(w, h);
                        postInvalidate();
                    }
                }, "prepare-backdrop").start();
            }
            final Bitmap still = backdrop;
            if (still != null) {
                // Cover the screen, centred, keeping the picture's proportions.
                final float s = Math.max((float) w / still.getWidth(),
                                         (float) h / still.getHeight());
                final float bw = still.getWidth() * s, bh = still.getHeight() * s;
                box.set((w - bw) / 2f, (h - bh) / 2f, (w + bw) / 2f, (h + bh) / 2f);
                c.drawBitmap(still, null, box, image);
            }

            final RectF panel = rect(PANEL_X, 0f, PANEL_W, panelH);
            final float radius = 18f * unit;
            fill.setColor(PANEL_FILL);
            c.drawRoundRect(panel, radius, radius, fill);
            final float stroke = Math.max(1f, 2f * unit);
            line.setColor(PANEL_LINE);
            line.setStrokeWidth(stroke);
            c.drawRoundRect(panel.left + stroke / 2f, panel.top + stroke / 2f,
                            panel.right - stroke / 2f, panel.bottom - stroke / 2f,
                            radius, radius, line);

            drawLine(c, failed ? kit.own("Preparing game art - could not write the content")
                               : kit.own("Preparing game art"), TITLE_Y, CAP_TITLE, failed ? WARN : GOLD);

            // 1. The content the package carries.
            final String unpack;
            if (unpackTotal == 0) {
                unpack = finished ? kit.own("Content unpacked  -  nothing to unpack") : kit.own("Content unpacked");
            } else {
                unpack = kit.own("Content unpacked") + "  " + unpackDone + " / " + unpackTotal;
            }
            drawLine(c, unpack, ROW_Y, CAP_BODY, INK);
            drawBar(c, ROW_Y + BAR_DY, unpackTotal == 0 ? 0f : (float) unpackDone / unpackTotal);

            // 2. The engine's interface art: counted where it must be, not produced here yet.
            final float y1 = ROW_Y + ROW_PITCH;
            final String art;
            float artFraction = (artPresent <= 0 || artExpected == 0) ? 0f : (float) artPresent / artExpected;
            if (artPresent < 0 && artMade >= 0 && artMaking > 0) {
                // Being made: the files of the list written so far (design 6.0b, "n / 153").
                art = kit.own("In-game interface art  {0} / {1} present", artMade, artMaking);
                artFraction = (float) artMade / artMaking;
            } else if (artPresent < 0) {
                art = kit.own("In-game interface art");
            } else if (artExpected == 0) {
                art = kit.own("In-game interface art  -  no list in this package");
            } else {
                art = kit.own("In-game interface art  {0} / {1} present", artPresent, artExpected);
            }
            drawLine(c, art, y1, CAP_BODY, INK);
            drawBar(c, y1 + BAR_DY, artFraction);

            // 3. The launcher's own theme: nothing produces it yet.
            final float y2 = ROW_Y + 2 * ROW_PITCH;
            drawLine(c, kit.own("Launcher theme  -  not built yet"), y2, CAP_BODY, INK);
            drawBar(c, y2 + BAR_DY, 0f);

            // The way on after a failure: the same work again, or another game folder.
            if (failed) {
                drawButton(c, 0, kit.own("Choose another folder"), false);
                drawButton(c, 1, kit.own("Try again"), true);
            }
        }

        /** 0: Choose another folder at the panel's left; 1: Try again at its right. */
        private RectF button(int index) {
            return (index == 0) ? rect(TEXT_X, BTN_Y, BTN_LEFT_W, BTN_H)
                                : rect(TEXT_X + TEXT_W - BTN_RIGHT_W, BTN_Y, BTN_RIGHT_W, BTN_H);
        }

        /** A button with its label centred, shrunk to fit should it be wider than the button. */
        private void drawButton(Canvas c, int index, String label, boolean marked) {
            final RectF r = button(index);
            final float bw = r.width() / unit;
            final float radius = 14f * unit;
            fill.setColor(BTN_FILL);
            c.drawRoundRect(r, radius, radius, fill);
            final float stroke = Math.max(1f, 2f * unit);
            line.setColor(marked ? GOLD : PANEL_LINE);
            line.setStrokeWidth(marked ? stroke * 1.5f : stroke);
            c.drawRoundRect(r.left + stroke / 2f, r.top + stroke / 2f, r.right - stroke / 2f,
                            r.bottom - stroke / 2f, radius, radius, line);
            final float cx = r.centerX(), top = r.top, h = r.height();
            float cap = CAP_BODY;
            text.setTextSize(cap * unit * sizePerCap);
            final float room = (bw - 48f) * unit, wide = text.measureText(label);
            if (wide > room) {
                cap *= room / wide;
                text.setTextSize(cap * unit * sizePerCap);
            }
            text.setTextAlign(Paint.Align.CENTER);
            final float baseline = top + (h + cap * unit) / 2f;
            text.setColor(0xFF000000);
            c.drawText(label, cx + SHADOW * unit, baseline + SHADOW * unit, text);
            text.setColor(marked ? GOLD : INK);
            c.drawText(label, cx, baseline, text);
            text.setTextAlign(Paint.Align.LEFT);
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            if (e.getAction() != MotionEvent.ACTION_UP || !finished) {
                return true;
            }
            if (failed) {
                final float x = e.getX(), y = e.getY();
                if (button(1).contains(x, y)) {
                    recreate();   // try again
                } else if (button(0).contains(x, y)) {
                    chooseAnotherFolder();
                }
            } else if (hold) {
                proceed();
            }
            return true;
        }
    }
}
