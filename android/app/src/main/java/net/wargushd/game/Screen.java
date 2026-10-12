package net.wargushd.game;

import android.app.Activity;
import android.os.Build;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/** What every launcher screen needs from the window it is shown in. */
final class Screen {
    /** Width of the logical canvas every launcher layout is written in. */
    static final float CANVAS_W = 3840f;

    private Screen() {}

    /**
     * The layouts are designed for the whole screen: draw under the system bars and hide
     * them. Call from onCreate and again whenever the window regains focus.
     */
    static void useWhole(Activity a) {
        final Window w = a.getWindow();
        if (Build.VERSION.SDK_INT >= 28) {
            w.getAttributes().layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
        if (Build.VERSION.SDK_INT >= 30) {
            w.setDecorFitsSystemWindows(false);
            final WindowInsetsController c = w.getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            w.getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }

    /**
     * The game window keeps its own layout (the engine's picture stays clear of a display
     * cutout) and only hides the system bars; on a low screen it also takes the rows the hidden
     * bars leave. Call from onCreate and on regained focus.
     */
    static void hideBars(Activity a) {
        final Window w = a.getWindow();
        if (Build.VERSION.SDK_INT >= 30) {
            // A low screen has too few rows to leave the hidden status bar's strip empty.
            final android.util.DisplayMetrics m = a.getResources().getDisplayMetrics();
            if (Math.min(m.widthPixels, m.heightPixels) < 1000) {
                w.setDecorFitsSystemWindows(false);
            }
            final WindowInsetsController c = w.getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
    }
}
