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
        for (int n = 2; target.exists(); ++n) {
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
}
