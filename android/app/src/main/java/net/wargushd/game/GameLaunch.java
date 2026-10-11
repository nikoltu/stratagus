package net.wargushd.game;

import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.security.SecureRandom;
import java.util.List;

/**
 * The one way the game activity is started.
 *
 * The game runs in its own process and must only ever be started on purpose: Android may
 * re-create a dead activity from a remembered Intent, which would drop the player into a
 * game nobody asked for. Every start therefore carries a token that is valid once. The
 * starter writes it to a file and puts it in the Intent; the game activity accepts the
 * start only if both agree, and deletes the file while checking.
 */
final class GameLaunch {
    /** Intent extra: the one-shot token of this start. */
    static final String EXTRA_TOKEN = "net.wargushd.game.TOKEN";
    /** Intent extra: absolute path of the request file; absent for a start into the menus. */
    static final String EXTRA_REQUEST = "net.wargushd.game.REQUEST";

    /** Intent extra: uptime clock value at which the starter's loading view appeared. */
    static final String EXTRA_T0 = "net.wargushd.game.T0";

    private GameLaunch() {}

    /** Directory the starter writes into: the request and the pending token. */
    static File launchDir(Context c) {
        return new File(c.getFilesDir(), "stratagus/launch");
    }

    /** The request file handed to the engine. */
    static File requestFile(Context c) {
        return new File(launchDir(c), "request.lua");
    }

    /** Intent extra: absolute path of the profile's directory; absent for the default one. */
    static final String EXTRA_PROFILE_DIR = "net.wargushd.game.PROFILE_DIR";

    /** Where profile directories live. */
    static File profilesDir(Context c) {
        return new File(c.getFilesDir(), "profiles");
    }

    /**
     * The directory of one profile, or null if the id is not a plain name. The engine keeps
     * everything that belongs to a player under it: progress, saves, preferences.
     */
    static File profileDir(Context c, String id) {
        if (id == null || !id.matches("[a-z0-9_]{1,32}")) {
            return null;
        }
        return new File(profilesDir(c), id);
    }

    /** True if the path names a directory directly inside the profiles directory. */
    static boolean isProfileDir(Context c, String path) {
        if (path == null) {
            return false;
        }
        try {
            final File dir = new File(path).getCanonicalFile();
            return profilesDir(c).getCanonicalFile().equals(dir.getParentFile());
        } catch (Exception e) {
            return false;
        }
    }

    /**
     * The record the engine leaves when it ends. The engine writes it under its user
     * directory, which is the profile's directory when one is given.
     */
    static File resultFile(Context c, File profileDir) {
        final File user = (profileDir != null) ? profileDir : new File(c.getFilesDir(), "stratagus");
        return new File(user, "wc2/launch/result.json");
    }

    /**
     * The names of the players a network game still waits for while it loads, one per line.
     * The engine writes it beside the result record and then sends its message.
     */
    static File loadingFile(Context c, File profileDir) {
        return new File(resultFile(c, profileDir).getParentFile(), "loading.txt");
    }

    private static File tokenFile(Context c) {
        return new File(launchDir(c), "token");
    }

    /** A fresh token: 16 random bytes in hex. */
    static String newToken() {
        final byte[] raw = new byte[16];
        new SecureRandom().nextBytes(raw);
        final StringBuilder s = new StringBuilder(32);
        for (byte b : raw) {
            s.append(String.format("%02x", b));
        }
        return s.toString();
    }

    /**
     * End a game that is still alive from an earlier start. The game activity is a single
     * instance: while one lives, a new start would only bring it to the front and the new
     * request would never be read. The engine cannot be started twice in one process, so
     * the process is ended; the start itself then clears what is left of the old task.
     */
    /** Whether the game's own process is alive (false too when that cannot be found out). */
    static boolean gameRunning(Context c) {
        try {
            final ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
            final List<ActivityManager.RunningAppProcessInfo> running = am.getRunningAppProcesses();
            final String game = c.getPackageName() + ":game";
            for (int i = 0; running != null && i < running.size(); ++i) {
                if (game.equals(running.get(i).processName)) {
                    return true;
                }
            }
        } catch (RuntimeException e) {
            // Not allowed to look.
        }
        return false;
    }

    private static void endRunningGame(Context c) {
        try {
            final ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
            final List<ActivityManager.RunningAppProcessInfo> running = am.getRunningAppProcesses();
            if (running != null) {
                final String game = c.getPackageName() + ":game";
                for (ActivityManager.RunningAppProcessInfo p : running) {
                    if (game.equals(p.processName)) {
                        android.os.Process.killProcess(p.pid);
                    }
                }
            }
        } catch (RuntimeException e) {
            // Nothing to end, or not allowed to look: the start goes on as before.
        }
    }

    /**
     * Start the game. With a request the engine runs what the request describes and no
     * menu; without one it starts into its own menus. With a profile directory the engine
     * uses it as its user directory. A non-zero t0 says the starter is showing the loading
     * view since that moment.
     *
     * @return false if the token could not be stored (the game is then not started).
     */
    static boolean start(Context c, String token, File request, File profileDir, long t0) {
        endRunningGame(c);
        if (!writeText(tokenFile(c), token)) {
            return false;
        }
        final Intent i = new Intent(c, WargusHDActivity.class);
        // Always a fresh instance from this Intent, never an old one brought back.
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        i.putExtra(EXTRA_TOKEN, token);
        if (request != null) {
            i.putExtra(EXTRA_REQUEST, request.getAbsolutePath());
        }
        if (profileDir != null) {
            if (!profileDir.isDirectory() && !profileDir.mkdirs()) {
                return false;
            }
            i.putExtra(EXTRA_PROFILE_DIR, profileDir.getAbsolutePath());
        }
        if (t0 != 0L) {
            // The starter already shows the loading view; the game's window continues it.
            i.putExtra(EXTRA_T0, t0);
            i.addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION);
        }
        c.startActivity(i);
        return true;
    }

    /**
     * Check the token of a start and use it up. True only for the first activity created
     * from an Intent whose token is the pending one.
     */
    static boolean consumeToken(Context c, Intent intent) {
        final File f = tokenFile(c);
        final String pending = readText(f);
        // Use it up whatever the outcome: a token is good for one attempt.
        f.delete();
        final String given = (intent == null) ? null : intent.getStringExtra(EXTRA_TOKEN);
        return given != null && pending != null && !given.isEmpty() && given.equals(pending.trim());
    }

    static boolean writeText(File f, String text) {
        final File dir = f.getParentFile();
        if (dir != null && !dir.isDirectory() && !dir.mkdirs()) {
            return false;
        }
        try (FileOutputStream out = new FileOutputStream(f)) {
            out.write(text.getBytes("UTF-8"));
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    static String readText(File f) {
        if (!f.isFile()) {
            return null;
        }
        final byte[] buf = new byte[(int) f.length()];
        try (FileInputStream in = new FileInputStream(f)) {
            int off = 0;
            while (off < buf.length) {
                final int n = in.read(buf, off, buf.length - off);
                if (n < 0) {
                    break;
                }
                off += n;
            }
            return new String(buf, 0, off, "UTF-8");
        } catch (Exception e) {
            return null;
        }
    }
}
