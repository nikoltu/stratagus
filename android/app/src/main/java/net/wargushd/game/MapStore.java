package net.wargushd.game;

import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The player's own maps: one folder, maps/custom under the data directory, where an imported
 * map is copied to (and where maps made in the editor or received from a host will live).
 */
final class MapStore {
    private MapStore() {}

    static File directory(Context c) {
        return new File(Provision.dataDir(c), "maps/custom");
    }

    /** The name a picked document shows, or null when it cannot be told. */
    static String nameOf(Context c, Uri document) {
        try (Cursor q = c.getContentResolver().query(
                 document, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (q != null && q.moveToFirst()) {
                return q.getString(0);
            }
        } catch (RuntimeException e) {
            // Not told.
        }
        final String last = document.getLastPathSegment();
        return (last == null) ? null : last.substring(last.lastIndexOf('/') + 1);
    }

    /**
     * Copy a picked map into the store. Only a file named *.pud whose header reads as a map is
     * kept; a name already there gets a number.
     *
     * @return the stored file, or null when the document is not a map or could not be copied
     */
    static File importFrom(Context c, Uri document) {
        final String name = nameOf(c, document);
        if (name == null || !name.toLowerCase().endsWith(".pud")) {
            return null;
        }
        final File dir = directory(c);
        if (!dir.isDirectory() && !dir.mkdirs()) {
            return null;
        }
        final String stem = name.substring(0, name.length() - 4);
        File target = new File(dir, stem + ".pud");
        for (int n = 2; taken(target); ++n) {
            target = new File(dir, stem + " (" + n + ").pud");
        }
        try (InputStream in = c.getContentResolver().openInputStream(document);
             OutputStream out = new FileOutputStream(target)) {
            if (in == null) {
                target.delete();
                return null;
            }
            final byte[] buffer = new byte[65536];
            for (int got; (got = in.read(buffer)) > 0; ) {
                out.write(buffer, 0, got);
            }
        } catch (IOException | RuntimeException e) {
            target.delete();
            return null;
        }
        final PudInfo facts = PudInfo.read(target);
        if (facts.width <= 0 || facts.height <= 0) {
            target.delete();
            return null;
        }
        return target;
    }

    /** True for one of the player's own maps: a file of the store itself. */
    static boolean isOwn(Context c, File map) {
        return map != null && map.getParentFile() != null && map.getParentFile().equals(directory(c));
    }

    /** A map's name as a file can carry it: the rule for typed names, less what a file name cannot hold. */
    static String cleanName(String wanted) {
        final StringBuilder clean = new StringBuilder();
        for (char ch : Profiles.clean(wanted).toCharArray()) {
            if ("/\\:*?\"<>|".indexOf(ch) < 0) {
                clean.append(ch);
            }
        }
        return clean.toString().trim();
    }

    /**
     * The file a new map of a name gets; a name already there - as a map or as a draft that was
     * never saved - gets a number. Null when the store cannot be made.
     */
    static File free(Context c, String stem) {
        final File dir = directory(c);
        if (!dir.isDirectory() && !dir.mkdirs()) {
            return null;
        }
        File target = new File(dir, stem + ".pud");
        for (int n = 2; taken(target); ++n) {
            target = new File(dir, stem + " (" + n + ").pud");
        }
        return target;
    }

    private static boolean taken(File map) {
        return map.exists() || net.wargushd.game.editor.WorkingCopy.journalOf(map).exists();
    }

    /** Copy one of the player's own maps, as it was last saved, under a free name. Null when it could not be done. */
    static File duplicate(Context c, File map) {
        if (!isOwn(c, map) || !map.isFile()) {
            return null;
        }
        final String n = map.getName();
        final File target = free(c, n.substring(0, n.length() - 4));
        if (target == null) {
            return null;
        }
        try (InputStream in = new java.io.FileInputStream(map);
             OutputStream out = new FileOutputStream(target)) {
            final byte[] buffer = new byte[65536];
            for (int got; (got = in.read(buffer)) > 0; ) {
                out.write(buffer, 0, got);
            }
        } catch (IOException | RuntimeException e) {
            target.delete();
            return null;
        }
        return target;
    }

    /**
     * Give one of the player's own maps another file name; its working copy goes with it. A
     * draft that was never saved is its working copy alone.
     */
    static boolean rename(Context c, File map, File target) {
        final File journal = net.wargushd.game.editor.WorkingCopy.journalOf(map);
        final File journalTo = net.wargushd.game.editor.WorkingCopy.journalOf(target);
        if (!isOwn(c, map) || !isOwn(c, target) || taken(target)) {
            return false;
        }
        if (!map.isFile()) {
            return journal.isFile() && journal.renameTo(journalTo);
        }
        if (!map.renameTo(target)) {
            return false;
        }
        if (journal.isFile()) {
            journal.renameTo(journalTo);
        }
        return true;
    }

    /** Remove one of the player's own maps and its working copy. Nothing outside the store is ever removed. */
    static boolean remove(Context c, File map) {
        if (!isOwn(c, map)) {
            return false;
        }
        final File journal = net.wargushd.game.editor.WorkingCopy.journalOf(map);
        if (!map.isFile()) {
            return journal.isFile() && journal.delete();
        }
        if (!map.delete()) {
            return false;
        }
        if (journal.isFile()) {
            journal.delete();
        }
        return true;
    }

    /** Where the editor writes the map of a test game: a folder no list of maps reads. */
    static File testFile(Context c) {
        return new File(Provision.dataDir(c), "maps/editor-test/test.pud");
    }
}