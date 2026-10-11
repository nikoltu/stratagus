package net.wargushd.game;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;

/**
 * The loading screen: the game's loading still and its four-frame worker animation, and
 * nothing else - except, while a network game waits for players who still load, one line
 * that names them.
 *
 * The same view is shown first by the launcher and then, above the engine's surface, by the
 * game activity. Which animation frame is on screen depends only on the system's uptime
 * clock and on the moment loading began, so the picture does not jump when one window
 * takes over from the other.
 */
final class LoadingView extends View {
    /** How long one animation frame stays, milliseconds. */
    private static final long FRAME_MS = 150L;
    private static final int FRAMES = 4;
    /** One frame of the animation atlas, in its own pixels. */
    private static final float FRAME_W = 581f, FRAME_H = 362f;
    /** The animation's height and its inset from the right and bottom edges, as shares of the screen height. */
    private static final float ANIM_HEIGHT = 0.22f, ANIM_INSET = 0.06f;

    private final SkinKit kit;
    private final long t0;
    private final Paint image = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final RectF box = new RectF();
    private final Bitmap[] frames = new Bitmap[FRAMES];
    private Bitmap still;
    private boolean loaded;
    /** The line shown while a network game waits for players who still load; null = none. */
    private String waitLine;
    /** That line: capital height, and its inset from the left and bottom edges, in canvas units. */
    private static final float WAIT_CAP = 48f, WAIT_INSET = 144f;
    private static final int WAIT_INK = 0xFFE8E2D4;

    /** Show the names of the players a network game still waits for, or null for no line. */
    void setWaitLine(String names) {
        waitLine = (names == null || names.isEmpty()) ? null : names;
        invalidate();
    }

    /**
     * @param t0  uptime clock value (SystemClock.uptimeMillis) at which loading began
     */
    LoadingView(Context c, SkinKit kit, long t0) {
        super(c);
        this.kit = kit;
        this.t0 = t0;
    }

    /** Fade the view out and take it out of its parent. */
    void dismiss(long millis) {
        animate().alpha(0f).setDuration(millis).withEndAction(new Runnable() {
            @Override
            public void run() {
                if (getParent() instanceof ViewGroup) {
                    ((ViewGroup) getParent()).removeView(LoadingView.this);
                }
            }
        }).start();
    }

    @Override
    protected void onDraw(Canvas c) {
        final int w = getWidth(), h = getHeight();
        if (!loaded) {
            loaded = true;
            still = kit.file("Data/Backgrounds/loadscreen.png", w, h);
            final int fh = Math.round(h * ANIM_HEIGHT);
            final int fw = Math.round(fh * FRAME_W / FRAME_H);
            for (int i = 0; i < FRAMES; ++i) {
                frames[i] = kit.atlasFrameIn("Data/Art/hd/loading", "Animation",
                                             String.valueOf(i + 1), fw, fh);
            }
        }
        kit.backdrop(c, still, w, h, 0xFF000000);

        final long elapsed = Math.max(0L, SystemClock.uptimeMillis() - t0);
        final Bitmap frame = frames[(int) ((elapsed / FRAME_MS) % FRAMES)];
        if (frame != null) {
            final float fh = h * ANIM_HEIGHT;
            final float fw = fh * FRAME_W / FRAME_H;
            final float m = h * ANIM_INSET;
            box.set(w - fw - m, h - fh - m, w - m, h - m);
            c.drawBitmap(frame, null, box, image);
        }
        if (waitLine != null) {
            kit.begin(w, h);
            kit.textTop(c, kit.own("Waiting for {0}", waitLine), WAIT_INSET,
                        kit.height - WAIT_INSET - WAIT_CAP, WAIT_CAP, WAIT_INK, true);
        }
        postInvalidateOnAnimation();
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        // Nothing under the loading screen may be touched through it.
        return true;
    }
}
