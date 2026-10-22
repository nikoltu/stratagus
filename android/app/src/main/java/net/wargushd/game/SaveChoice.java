package net.wargushd.game;

import java.io.File;
import java.util.List;

/**
 * Which saved game the Load Game page has selected after it reads its directory again. Plain
 * Java (tested on the JVM): the page's selection is a file, not a place in the list, so a save
 * that disappears behind the page can never hand its row to the next save (LOAD-WRONG-SAVE-ONCE).
 */
final class SaveChoice {
    /** No row is selected. */
    static final int NONE = -1;

    private SaveChoice() {}

    /** The outcome of a re-read: the row to select, and the save that was selected and is gone. */
    static final class Pick {
        int index = NONE;
        /** The name of the file that was selected before and is no longer listed, else null. */
        String vanished;
    }

    /**
     * @param dir        the save directory just read
     * @param names      the file names listed, in the page's order
     * @param kept       the path of the save selected before the re-read, or null
     * @param remembered the path of the save last started from the page (the launcher's
     *                   preference), or null
     */
    static Pick select(File dir, List<String> names, String kept, String remembered) {
        final Pick p = new Pick();
        // A selection made in another directory (another profile) is not this list's.
        if (kept != null && inDir(dir, kept)) {
            final int at = indexOf(dir, names, kept);
            if (at >= 0) {
                p.index = at;
            } else {
                // Gone: nothing is selected, so Load cannot start some other save in its place.
                p.vanished = new File(kept).getName();
            }
            return p;
        }
        if (remembered != null) {
            final int at = indexOf(dir, names, remembered);
            if (at >= 0) {
                p.index = at;
                return p;
            }
        }
        p.index = names.isEmpty() ? NONE : 0;
        return p;
    }

    /**
     * The file a Load is to start: the selected save's own path, only when it is still a file in
     * the profile's save directory; null when it is not (the page then refuses with a notice).
     */
    static File resolve(File dir, String path) {
        if (path == null || !inDir(dir, path)) {
            return null;
        }
        final File f = new File(path);
        return f.isFile() ? f : null;
    }

    private static boolean inDir(File dir, String path) {
        final File parent = new File(path).getParentFile();
        return parent != null && parent.getPath().equals(dir.getPath());
    }

    private static int indexOf(File dir, List<String> names, String path) {
        for (int i = 0; i < names.size(); ++i) {
            if (new File(dir, names.get(i)).getPath().equals(path)) {
                return i;
            }
        }
        return NONE;
    }
}
