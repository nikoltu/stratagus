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
    private static final float CAP_TITLE = 60f, CAP_BODY = 44f;
    private static final float SHADOW = 5f;

    private static final int PANEL_FILL = 0xDE140F0A, PANEL_LINE = 0xFF92743E;
    private static final int GOLD = 0xFFF0D250, INK = 0xFFE8E2D4;
    private static final int TRACK = 0xFF50463A;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private PrepView view;
    private boolean hold;

    // Progress, written on the UI thread only.
    private int unpackDone, unpackTotal;
    private int artPresent = -1, artExpected;
    private boolean finished, failed;

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
        final int written = Provision.unpack(this, new Provision.Progress() {
            @Override
            public void unpacked(final int done, final int total) {
                ui.post(new Runnable() {
                    @Override
                    public void run() {
                        unpackDone = done;
                        unpackTotal = total;
                        view.invalidate();
                    }
                });
            }
        });
        final int[] art = Provision.countInterfaceArt(this);
        ui.post(new Runnable() {
            @Override
            public void run() {
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

    /** Backdrop, one panel, a title and three rows with a bar each. */
    private final class PrepView extends View {
        private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
        private final Paint image = new Paint(Paint.FILTER_BITMAP_FLAG);
        private final Rect bounds = new Rect();
        private final RectF box = new RectF();
        private final float sizePerCap;
        private Bitmap backdrop;
        private boolean backdropTried;

        private float unit;
        private float panelTop;

        PrepView(Context c) {
            super(c);
            Typeface face = Typeface.DEFAULT;
            final String folder = GateActivity.rememberedFolder(c);
            if (folder != null) {
                try {
                    face = Typeface.createFromFile(new File(folder, "Data/Font/BLQ55.ttf"));
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
            backdropTried = true;
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
            panelTop = (h - PANEL_H * unit) / 2f;

            c.drawColor(0xFF0D1114);
            if (!backdropTried) {
                loadBackdrop(w, h);
            }
            if (backdrop != null) {
                // Cover the screen, centred, keeping the picture's proportions.
                final float s = Math.max((float) w / backdrop.getWidth(),
                                         (float) h / backdrop.getHeight());
                final float bw = backdrop.getWidth() * s, bh = backdrop.getHeight() * s;
                box.set((w - bw) / 2f, (h - bh) / 2f, (w + bw) / 2f, (h + bh) / 2f);
                c.drawBitmap(backdrop, null, box, image);
            }

            final RectF panel = rect(PANEL_X, 0f, PANEL_W, PANEL_H);
            final float radius = 18f * unit;
            fill.setColor(PANEL_FILL);
            c.drawRoundRect(panel, radius, radius, fill);
            final float stroke = Math.max(1f, 2f * unit);
            line.setColor(PANEL_LINE);
            line.setStrokeWidth(stroke);
            c.drawRoundRect(panel.left + stroke / 2f, panel.top + stroke / 2f,
                            panel.right - stroke / 2f, panel.bottom - stroke / 2f,
                            radius, radius, line);

            drawLine(c, failed ? "Preparing game art - could not write the content"
                               : "Preparing game art", TITLE_Y, CAP_TITLE, GOLD);

            // 1. The content the package carries.
            final String unpack;
            if (unpackTotal == 0) {
                unpack = finished ? "Content unpacked  -  nothing to unpack" : "Content unpacked";
            } else {
                unpack = "Content unpacked  " + unpackDone + " / " + unpackTotal;
            }
            drawLine(c, unpack, ROW_Y, CAP_BODY, INK);
            drawBar(c, ROW_Y + BAR_DY, unpackTotal == 0 ? 0f : (float) unpackDone / unpackTotal);

            // 2. The engine's interface art: counted where it must be, not produced here yet.
            final float y1 = ROW_Y + ROW_PITCH;
            final String art;
            if (artPresent < 0) {
                art = "In-game interface art";
            } else if (artExpected == 0) {
                art = "In-game interface art  -  no list in this package";
            } else {
                art = "In-game interface art  " + artPresent + " / " + artExpected + " present";
            }
            drawLine(c, art, y1, CAP_BODY, INK);
            drawBar(c, y1 + BAR_DY,
                    (artPresent <= 0 || artExpected == 0) ? 0f : (float) artPresent / artExpected);

            // 3. The launcher's own theme: nothing produces it yet.
            final float y2 = ROW_Y + 2 * ROW_PITCH;
            drawLine(c, "Launcher theme  -  not built yet", y2, CAP_BODY, INK);
            drawBar(c, y2 + BAR_DY, 0f);
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            if (e.getAction() != MotionEvent.ACTION_UP || !finished) {
                return true;
            }
            if (failed) {
                recreate();   // try again
            } else if (hold) {
                proceed();
            }
            return true;
        }
    }
}
