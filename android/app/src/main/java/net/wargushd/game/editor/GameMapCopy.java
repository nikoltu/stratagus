package net.wargushd.game.editor;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Edit Map on a map of the game folder (owner decision 94, E4 = option A of
 * EDITOR_NEXT_IMPL.md 4.4; EDITOR_DESIGN.md 5 "Retail maps"): the map is first copied into the
 * player's own maps, under its own name or, when that is taken, with a number, and the copy is
 * opened. Only the store is written; the game folder is only read. No Android, so the JVM test
 * reaches it.
 */
public final class GameMapCopy {
    private GameMapCopy() {}

    /** A map file's name without its .pud (any case). */
    public static String stem(String fileName) {
        return fileName.toLowerCase(java.util.Locale.ROOT).endsWith(".pud")
            ? fileName.substring(0, fileName.length() - 4) : fileName;
    }

    /** A name is taken by a map file or by a draft (a working copy) of that name. */
    private static boolean taken(File map) {
        return map.exists() || WorkingCopy.journalOf(map).exists();
    }

    /**
     * The file the copy of a game map gets in the store: "name.pud", else "name (2).pud",
     * "name (3).pud" ... - the rule of the store's own new maps and duplicates.
     */
    public static File target(File store, String fileName) {
        final String stem = stem(fileName);
        File out = new File(store, stem + ".pud");
        for (int n = 2; taken(out); ++n) {
            out = new File(store, stem + " (" + n + ").pud");
        }
        return out;
    }

    /**
     * Copy a map into the store; the copy, or null when it could not be written. A source inside
     * the store itself is not copied (it is the player's own already).
     */
    public static File copy(File source, File store) {
        if (source == null || !source.isFile() || store == null) {
            return null;
        }
        final File parent = source.getAbsoluteFile().getParentFile();
        if (parent != null && parent.equals(store.getAbsoluteFile())) {
            return null;
        }
        if (!store.isDirectory() && !store.mkdirs()) {
            return null;
        }
        final File out = target(store, source.getName());
        try (InputStream in = new FileInputStream(source); OutputStream o = new FileOutputStream(out)) {
            final byte[] buffer = new byte[65536];
            for (int got; (got = in.read(buffer)) > 0; ) {
                o.write(buffer, 0, got);
            }
        } catch (IOException | RuntimeException e) {
            out.delete();
            return null;
        }
        return out;
    }
}
