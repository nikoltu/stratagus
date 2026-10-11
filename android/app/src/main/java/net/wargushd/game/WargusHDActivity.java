package net.wargushd.game;

import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.RelativeLayout;
import android.widget.TextView;

import org.libsdl.app.SDLActivity;

/**
 * Entry activity for the Android build. The whole engine (including SDL2) is
 * linked statically into a single libmain.so, so only "main" is loaded; SDL's
 * default {"SDL2", "main"} would try to dlopen a separate libSDL2.so that does
 * not exist here.
 */
public class WargusHDActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "main"
        };
    }

    /** Path of the request file of an accepted start; null for a start into the menus. */
    private String requestPath;
    /** The profile's directory of an accepted start; null for the default user directory. */
    private String profilePath;

    /**
     * Command-line arguments for the engine. A start that came with a request runs the
     * start script on it; the arguments are built here and never taken from the caller.
     */
    @Override
    protected String[] getArguments() {
        final java.util.ArrayList<String> args = new java.util.ArrayList<String>();
        if (profilePath != null) {
            args.add("-u");
            args.add(profilePath);
        }
        // The engine always starts through the launch script: without a request that script
        // records "no-request" and the engine leaves, back to the launcher (design phase P9).
        args.add("-c");
        args.add("scripts/launch.lua");
        if (requestPath != null) {
            args.add("-G");
            args.add(requestPath);
        }
        return args.toArray(new String[0]);
    }

    // Messages from the engine (LauncherPost in the scripts) and commands to it.
    /** The game is being loaded; the loading view belongs above the engine until LOADING_END. */
    static final int MSG_LOADING_BEGIN = 1;
    static final int MSG_LOADING_END = 2;
    /** A game has ended; its record number is the value. */
    static final int MSG_RESULT_READY = 3;
    /** The engine is about to leave; the value is the reason. */
    static final int MSG_ENGINE_EXIT = 4;
    /**
     * A network game has not begun: the value is how many players still load (their names are
     * in the loading file); 0 = nobody any more.
     */
    static final int MSG_LOADING_WAIT = 5;
    /** Debuggable builds: show the value in a label above the engine. */
    static final int MSG_TEST = 0x7F00;

    static final int CMD_CONTINUE = 1;
    static final int CMD_RESTART = 2;
    static final int CMD_PLAY_AGAIN = 3;
    static final int CMD_EXIT_BACK = 4;
    static final int CMD_EXIT_REMATCH = 5;
    /** To the engine itself: the copy of the game's last picture is taken (or not wanted). */
    static final int CMD_FRAME_TAKEN = 9;
    static final int MSG_GAME_ENDED = 4;
    /** The game's picture as it ended, for behind the result window. */
    private android.graphics.Bitmap lastFrame;

    /** Leaves a command for the engine; the engine takes it when its script asks. */
    private static native void nativeLauncherCommand(int command);

    /** Tells the engine that this activity draws the loading screen. */
    private static native void nativeLauncherHost(boolean hosted);

    /** The loading screen above the whole window while the engine starts or loads. */
    private LoadingView loadingView;
    private SkinKit loadingKit;
    /** The loading screen was put up by MSG_LOADING_WAIT itself and goes with its end. */
    private boolean loadingForWait;

    /** The players a network game still waits for, on the loading screen. */
    private void showLoadingWait(int count) {
        if (count <= 0) {
            if (loadingView != null) {
                loadingView.setWaitLine(null);
                if (loadingForWait) {
                    loadingView.dismiss(200L);
                    loadingView = null;
                }
            }
            loadingForWait = false;
            return;
        }
        if (loadingView == null) {
            // The engine's own loading has been reported over already: the screen comes back
            // for the wait and goes with it.
            showLoading(android.os.SystemClock.uptimeMillis(), true);
            loadingForWait = true;
        }
        final java.io.File profileDir = (profilePath != null) ? new java.io.File(profilePath) : null;
        final String names = GameLaunch.readText(GameLaunch.loadingFile(this, profileDir));
        final StringBuilder line = new StringBuilder();
        if (names != null) {
            for (String name : names.split("\n")) {
                if (!name.trim().isEmpty()) {
                    line.append(line.length() > 0 ? ", " : "").append(name.trim());
                }
            }
        }
        if (loadingView != null) {
            loadingView.setWaitLine(line.length() > 0 ? line.toString()
                                    : loadingKit.own("{0} players", count));
        }
    }

    private void showLoading(long t0, boolean fadeIn) {
        if (loadingView != null && loadingView.getParent() != null) {
            return;
        }
        if (loadingKit == null) {
            loadingKit = new SkinKit(this);
        }
        loadingView = new LoadingView(this, loadingKit, t0);
        ((android.view.ViewGroup) getWindow().getDecorView()).addView(loadingView,
            new android.view.ViewGroup.LayoutParams(
                android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                android.view.ViewGroup.LayoutParams.MATCH_PARENT));
        if (fadeIn) {
            loadingView.setAlpha(0f);
            loadingView.animate().alpha(1f).setDuration(120L).start();
        }
    }

    /**
     * Views shown above the engine's surface (loading, result window). Empty and
     * click-through while nothing is shown.
     */
    private FrameLayout overlayHost;

    private void createOverlayHost() {
        if (mLayout == null) {
            return;
        }
        overlayHost = new FrameLayout(this);
        mLayout.addView(overlayHost, new RelativeLayout.LayoutParams(
            RelativeLayout.LayoutParams.MATCH_PARENT, RelativeLayout.LayoutParams.MATCH_PARENT));
    }

    @Override
    protected boolean onUnhandledMessage(int command, Object param) {
        if (command < 0x8000) {
            return super.onUnhandledMessage(command, param);
        }
        final int what = command - 0x8000;
        final int value = (param instanceof Integer) ? (Integer) param : 0;
        Log.i("WargusHD", "LAUNCH message " + what + " value " + value);
        if (what == MSG_LOADING_BEGIN) {
            showLoading(android.os.SystemClock.uptimeMillis(), true);
        } else if (what == MSG_LOADING_END && loadingView != null) {
            loadingView.dismiss(200L);
            loadingView = null;
            loadingForWait = false;
        }
        if (what == MSG_LOADING_WAIT) {
            showLoadingWait(value);
        }
        if (what == MSG_GAME_ENDED) {
            takeLastFrame();
        }
        if (what == MSG_RESULT_READY) {
            showResult();
        }
        if (what == MSG_TEST
            && (getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0) {
            showTestLabel(value);
        }
        return true;
    }

    /** Copy the game's surface as it is now; the engine waits for the answer (a second at most). */
    private void takeLastFrame() {
        lastFrame = null;
        try {
            final android.view.SurfaceView surface = (android.view.SurfaceView) (Object) mSurface;
            if (surface == null || surface.getWidth() <= 0 || surface.getHeight() <= 0) {
                nativeLauncherCommand(CMD_FRAME_TAKEN);
                return;
            }
            final android.graphics.Bitmap copy = android.graphics.Bitmap.createBitmap(
                surface.getWidth(), surface.getHeight(), android.graphics.Bitmap.Config.ARGB_8888);
            android.view.PixelCopy.request(surface, copy,
                new android.view.PixelCopy.OnPixelCopyFinishedListener() {
                    @Override
                    public void onPixelCopyFinished(int result) {
                        Log.i("WargusHD", "LAUNCH last frame copy " + result);
                        if (result != android.view.PixelCopy.SUCCESS) {
                            nativeLauncherCommand(CMD_FRAME_TAKEN);
                            return;
                        }
                        lastFrame = copy;
                        keepFrameForALook(copy);
                        coverWithLastFrame(copy);
                    }
                }, new android.os.Handler(android.os.Looper.getMainLooper()));
        } catch (RuntimeException e) {
            nativeLauncherCommand(CMD_FRAME_TAKEN);
        }
    }

    /** The copied picture held above the engine until the result window stands (or does not come). */
    private View frameCover;

    /**
     * The engine clears its screen as soon as it is answered, and the result window needs a
     * moment to be built: the copy is put above the engine first, and the engine is answered
     * only when that has been drawn. Only when a window was asked for; a cover that no window
     * follows is taken away again.
     */
    private void coverWithLastFrame(final android.graphics.Bitmap copy) {
        final String request = (requestPath != null) ? GameLaunch.readText(new java.io.File(requestPath)) : null;
        if (overlayHost == null || request == null || !request.contains("window = true")) {
            nativeLauncherCommand(CMD_FRAME_TAKEN);
            return;
        }
        removeFrameCover();
        final android.widget.ImageView cover = new android.widget.ImageView(this);
        cover.setScaleType(android.widget.ImageView.ScaleType.FIT_XY);
        cover.setImageBitmap(copy);
        frameCover = cover;
        overlayHost.addView(cover, 0, new FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        // Two frames on: the cover is on screen.
        android.view.Choreographer.getInstance().postFrameCallback(new android.view.Choreographer.FrameCallback() {
            @Override
            public void doFrame(long t) {
                android.view.Choreographer.getInstance().postFrameCallback(new android.view.Choreographer.FrameCallback() {
                    @Override
                    public void doFrame(long t2) {
                        nativeLauncherCommand(CMD_FRAME_TAKEN);
                    }
                });
            }
        });
        cover.postDelayed(new Runnable() {
            @Override
            public void run() {
                // No window came (a game left without an outcome): the engine's own picture again.
                if (frameCover == cover && !resultShown) {
                    removeFrameCover();
                }
            }
        }, 2500L);
    }

    private boolean resultShown;

    private void removeFrameCover() {
        if (frameCover != null && overlayHost != null) {
            overlayHost.removeView(frameCover);
        }
        frameCover = null;
    }

    /**
     * Developer switch (debuggable builds): with an empty file "keep_last_frame" in the app's
     * files directory the copied picture is also written there as last_frame.png.
     */
    private void keepFrameForALook(android.graphics.Bitmap copy) {
        if ((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0
            || !new java.io.File(getFilesDir(), "keep_last_frame").exists()) {
            return;
        }
        try (java.io.FileOutputStream out =
                 new java.io.FileOutputStream(new java.io.File(getFilesDir(), "last_frame.png"))) {
            copy.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, out);
        } catch (java.io.IOException e) {
            // A look only.
        }
    }

    /** The result window over the game: filled from the record the engine has just written. */
    private void showResult() {
        if (overlayHost == null) {
            // Nothing can show it: let the engine go on.
            nativeLauncherCommand(CMD_EXIT_BACK);
            return;
        }
        final java.io.File profileDir = (profilePath != null) ? new java.io.File(profilePath) : null;
        final String record = GameLaunch.readText(GameLaunch.resultFile(this, profileDir));
        final String card = (requestPath != null) ? GameLaunch.readText(
            new java.io.File(new java.io.File(requestPath).getParentFile(), "profile.json")) : null;
        if (loadingKit == null) {
            loadingKit = new SkinKit(this);
        }
        final ResultView[] shown = new ResultView[1];
        // The campaign progress the game has just written, for the window's summary.
        final String progress = (profileDir != null)
            ? GameLaunch.readText(new java.io.File(profileDir, "wc2/GlobalSave.json")) : null;
        shown[0] = new ResultView(this, loadingKit, record != null ? record : "", card, progress, lastFrame,
                                  new ResultView.Listener() {
            @Override
            public void onCommand(int command) {
                overlayHost.removeView(shown[0]);
                resultShown = false;
                removeFrameCover();
                nativeLauncherCommand(command);
            }
        });
        resultShown = true;
        overlayHost.addView(shown[0], new FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
    }

    /** A label above the engine that shows a number and answers a touch with a command. */
    private void showTestLabel(final int value) {
        if (overlayHost == null) {
            return;
        }
        overlayHost.removeAllViews();
        final TextView label = new TextView(this);
        label.setText("engine message " + value);
        label.setTextSize(28);
        label.setTextColor(0xFFFFFFFF);
        label.setBackgroundColor(0xC0202020);
        final int pad = (int) (24 * getResources().getDisplayMetrics().density);
        label.setPadding(pad, pad, pad, pad);
        label.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                nativeLauncherCommand(CMD_CONTINUE);
                overlayHost.removeAllViews();
            }
        });
        overlayHost.addView(label, new FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT,
            Gravity.CENTER));
    }

    // LAN multiplayer plumbing. Android does not deliver multicast/broadcast
    // datagrams to an app unless it holds a WifiManager MulticastLock, so the
    // engine's own mDNS LAN host discovery (mdns_wrapper) cannot see peers on the
    // Wi-Fi without it. The lock is held only while the activity is in the
    // foreground so battery is not wasted when the game is backgrounded. The
    // in-engine multiplayer lobby and protocol are unchanged; this only makes
    // their existing network traffic reach the device. (A high-performance
    // WifiLock would additionally need the WAKE_LOCK permission and is not
    // required for discovery, so it is deliberately not used.)
    private WifiManager.MulticastLock multicastLock;
    /** A LAN game only: keeps the Wi-Fi out of its power saving (late and lost packets otherwise). */
    private WifiManager.WifiLock fastWifi;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // The game is started on purpose or not at all. An activity re-created by the
        // system, or created from an Intent whose token is not the pending one, hands
        // over to the entry activity instead of starting the engine. This process holds
        // nothing but the game, so it simply ends.
        if (savedInstanceState != null || !GameLaunch.consumeToken(this, getIntent())) {
            Log.i("WargusHD", "LAUNCH refused: no valid start token");
            try {
                startActivity(new Intent(this, SetupActivity.class)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            } catch (RuntimeException ignored) {
            }
            finish();
            System.exit(0);
        }
        requestPath = getIntent().getStringExtra(GameLaunch.EXTRA_REQUEST);
        final String profile = getIntent().getStringExtra(GameLaunch.EXTRA_PROFILE_DIR);
        if (GameLaunch.isProfileDir(this, profile)) {
            profilePath = profile;
        }

        // The engine's debug command channel exists only in a build marked debuggable: the engine
        // looks for this file, which is made here at every start and removed by any other build.
        final java.io.File bridgeMark = new java.io.File(getFilesDir(), "debug_bridge");
        try {
            if ((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0) {
                bridgeMark.createNewFile();
            } else {
                bridgeMark.delete();
            }
        } catch (java.io.IOException | RuntimeException ignored) {
        }
        super.onCreate(savedInstanceState);
        Screen.hideBars(this);
        createOverlayHost();
        final long t0 = getIntent().getLongExtra(GameLaunch.EXTRA_T0, 0L);
        if (t0 != 0L) {
            // The starter is showing the loading view: continue it here and let the engine
            // leave the loading screen to this activity.
            overridePendingTransition(0, 0);
            showLoading(t0, false);
            nativeLauncherHost(true);
        }
        final WifiManager wifi =
            (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
        if (wifi != null) {
            multicastLock = wifi.createMulticastLock("wargushd-lan");
            multicastLock.setReferenceCounted(false);
            if (isLanRequest()) {
                try {
                    fastWifi = wifi.createWifiLock(android.os.Build.VERSION.SDK_INT >= 29
                        ? WifiManager.WIFI_MODE_FULL_LOW_LATENCY : WifiManager.WIFI_MODE_FULL_HIGH_PERF,
                        "wargushd-lan");
                    fastWifi.setReferenceCounted(false);
                } catch (RuntimeException e) {
                    fastWifi = null;
                }
                Log.i("WargusHD", "LAUNCH lan wifi lock " + (fastWifi != null));
            }
        }
    }

    /** Whether the request this game was started with is a LAN one (its mode begins with "lan_"). */
    private boolean isLanRequest() {
        if (requestPath == null) {
            return false;
        }
        try (java.io.FileInputStream in = new java.io.FileInputStream(requestPath)) {
            final byte[] head = new byte[8192];
            final int n = in.read(head);
            return n > 0 && new String(head, 0, n, java.nio.charset.StandardCharsets.UTF_8)
                .contains("mode = " + (char) 34 + "lan_");
        } catch (java.io.IOException e) {
            return false;
        }
    }

    /** The system bars stay hidden over the game, as over the launcher. */
    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            Screen.hideBars(this);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (multicastLock != null && !multicastLock.isHeld()) {
            multicastLock.acquire();
        }
        if (fastWifi != null && !fastWifi.isHeld()) {
            fastWifi.acquire();
        }
    }

    @Override
    protected void onPause() {
        if (multicastLock != null && multicastLock.isHeld()) {
            multicastLock.release();
        }
        if (fastWifi != null && fastWifi.isHeld()) {
            fastWifi.release();
        }
        super.onPause();
    }
}
