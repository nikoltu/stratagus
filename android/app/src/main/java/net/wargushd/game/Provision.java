package net.wargushd.game;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;

/**
 * The content the package carries, unpacked into files/data so the engine can read it.
 *
 * It is unpacked again whenever the package is installed or updated, so an update can
 * never leave an older version's scripts in place. The work is counted, so a screen can
 * show how far it is.
 */
final class Provision {
    private static final String ASSET_ROOT = "wargushd";
    /** Shipped list of the interface pictures made on the device, relative to files/data (for the count shown). */
    private static final String ART_LIST = ASSET_ROOT + "/classb_paths.txt";

    /** Receives progress on the worker thread. */
    interface Progress {
        void unpacked(int done, int total);
    }

    private Provision() {}

    private static File stampFile(Context c) {
        return new File(c.getFilesDir(), ".provision");
    }

    static File dataDir(Context c) {
        return new File(c.getFilesDir(), "data");
    }

    /** Identity of the installed package; changes with every install and update. */
    private static String wantedStamp(Context c) {
        try {
            final PackageInfo pi = c.getPackageManager().getPackageInfo(c.getPackageName(), 0);
            return pi.versionCode + ":" + pi.lastUpdateTime;
        } catch (Exception e) {
            return null;
        }
    }

    /** True once the fonts and pictures made from the game folder were tried in this run. */
    private static volatile boolean madeTried;

    /** True when the unpacked content does not belong to the installed package. */
    static boolean needed(Context c) {
        final String want = wantedStamp(c);
        if (want == null) {
            return false;
        }
        final String have = GameLaunch.readText(stampFile(c));
        if (have == null || !have.trim().equals(want)) {
            return true;
        }
        // What is made on the device from the game folder must be there too.
        // (Asked once a run: when the making fails, unpacking again would not help.)
        return !madeTried && GateActivity.rememberedFolder(c) != null
            && !(FontBaker.present(dataDir(c)) && ArtBaker.present(dataDir(c)));
    }

    /**
     * Unpack everything and write the stamp.
     *
     * @return the number of files written; 0 when the package carries no content (whatever
     *         is in files/data is then left alone); -1 when a file could not be written.
     */
    static int unpack(Context c, Progress progress) {
        final AssetManager assets = c.getAssets();
        final List<String> files = new ArrayList<String>();
        collect(assets, ASSET_ROOT, files);
        if (files.isEmpty()) {
            return 0;
        }
        final File dest = dataDir(c);
        final byte[] buf = new byte[65536];
        int done = 0;
        progress.unpacked(0, files.size());
        for (String path : files) {
            final File out = new File(dest, path.substring(ASSET_ROOT.length() + 1));
            final File dir = out.getParentFile();
            if (dir != null && !dir.isDirectory() && !dir.mkdirs()) {
                return -1;
            }
            try (InputStream in = assets.open(path);
                 FileOutputStream fo = new FileOutputStream(out)) {
                int n;
                while ((n = in.read(buf)) > 0) {
                    fo.write(buf, 0, n);
                }
            } catch (Exception e) {
                return -1;
            }
            progress.unpacked(++done, files.size());
        }
        // Fonts and interface pictures are not shipped: they are made from the game folder,
        // by this package's own recipe, over what an earlier one made.
        final java.util.HashSet<String> carried = new java.util.HashSet<String>();
        for (String path : files) {
            carried.add(new File(dest, path.substring(ASSET_ROOT.length() + 1)).getPath());
        }
        boolean madeAll = false;
        synchronized (MADE) {
            MADE.clear();
        }
        final String folder = GateActivity.rememberedFolder(c);
        if (folder != null) {
            madeTried = true;
            final boolean fonts = FontBaker.bake(new File(folder), dest);
            madeAll = ArtBaker.bake(new File(folder), dest) && fonts;
        }
        synchronized (MADE) {
            carried.addAll(MADE);
        }
        // What an older version left in files/data and this package neither carries nor has
        // just made is removed: the engine would otherwise find and use it (an old tileset
        // sheet in place of the game folder's own art). When the making did not go through (the
        // game folder could not be read), what was made before is left in place: old fonts and
        // pictures are better than none. The app's own folders are kept.
        final int removed = prune(dest, dest, carried, !madeAll);
        android.util.Log.i("WargusHD", "PROVISION unpacked " + done + " removed " + removed
                           + (madeAll ? "" : " (made files kept: the making did not go through)"));
        final String want = wantedStamp(c);
        if (want != null) {
            GameLaunch.writeText(stampFile(c), want);
        }
        return done;
    }

    /** What the makers wrote in this run. */
    private static final java.util.HashSet<String> MADE = new java.util.HashSet<String>();

    /** A maker reports a file it wrote under files/data. */
    static void wrote(File f) {
        synchronized (MADE) {
            MADE.add(f.getPath());
        }
    }

    /** True for a path (relative to files/data) in one of the places the makers write to. */
    private static boolean madePlace(String rel) {
        final String p = rel.startsWith("ui/") ? rel.substring(3) : rel;
        return p.startsWith("fonts/") || p.contains("/remaster/") || p.startsWith("rm/")
            || p.matches("(human|orc)/widgets/button-(large|half)-.*")
            || p.equals("food.png") || p.equals("gold,wood,oil,mana.png");
    }

    /** Folders under files/data that the app writes itself. */
    private static final String[] OWN = { "maps/custom", "launch" };

    private static int prune(File root, File dir, java.util.Set<String> carried, boolean spareMade) {
        final File[] entries = dir.listFiles();
        if (entries == null) {
            return 0;
        }
        int removed = 0;
        for (File e : entries) {
            if (e.isDirectory()) {
                boolean own = false;
                for (String o : OWN) {
                    own |= e.equals(new File(root, o));
                }
                if (own) {
                    continue;
                }
                removed += prune(root, e, carried, spareMade);
                final String[] left = e.list();
                if (left != null && left.length == 0) {
                    e.delete();
                }
            } else if (!carried.contains(e.getPath())
                       && !(spareMade && madePlace(e.getPath().substring(root.getPath().length() + 1)))
                       && e.delete()) {
                ++removed;
            }
        }
        return removed;
    }

    /** All files below an asset directory. A name that lists no children is a file. */
    private static void collect(AssetManager assets, String path, List<String> out) {
        String[] names;
        try {
            names = assets.list(path);
        } catch (Exception e) {
            names = null;
        }
        if (names == null || names.length == 0) {
            return;
        }
        for (String name : names) {
            final String child = path + "/" + name;
            String[] sub;
            try {
                sub = assets.list(child);
            } catch (Exception e) {
                sub = null;
            }
            if (sub != null && sub.length > 0) {
                collect(assets, child, out);
            } else {
                out.add(child);
            }
        }
    }

    /**
     * How many of the interface-art files the engine expects are present in files/data.
     *
     * @return { present, expected }; expected is 0 when the package carries no list.
     */
    static int[] countInterfaceArt(Context c) {
        int present = 0;
        int expected = 0;
        final File data = dataDir(c);
        try (BufferedReader in = new BufferedReader(
                 new InputStreamReader(c.getAssets().open(ART_LIST), "UTF-8"))) {
            String line;
            while ((line = in.readLine()) != null) {
                final String path = line.trim();
                if (path.isEmpty()) {
                    continue;
                }
                ++expected;
                if (new File(data, path).isFile()) {
                    ++present;
                }
            }
        } catch (Exception e) {
            // No list in this package: nothing to report.
        }
        return new int[] { present, expected };
    }
}
