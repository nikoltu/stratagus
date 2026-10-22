package net.wargushd.game.editor;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.util.zip.CRC32;

/**
 * The map being edited, kept apart from the map file until it is saved.
 *
 * The working copy of maps/custom/NAME.pud is one file, maps/custom/.work/NAME.pud.journal: the
 * map as it was when editing began, then one record for every change, undo and redo, each put
 * on the disk as it happens. Opening a map whose journal is there plays it again, so the map,
 * the undo list and the redo list come back as they were - after a test game or a killed
 * process alike. Saving writes the map file and leaves the journal; discarding removes the
 * journal and the map file stays as it was; keeping the draft only closes the journal, so the
 * unsaved work is there again at the next opening.
 *
 * A new map that was never saved is a working copy alone: a journal with no map file beside it.
 *
 * A record is a kind byte, a 32-bit length, the bytes and their CRC-32. A record cut short by a
 * killed process is dropped when the journal is opened.
 */
public final class WorkingCopy implements java.io.Closeable {
    private static final byte[] MAGIC = { 'W', 'H', 'D', 'J', '1', '\n' };
    private static final int BASE = 'B', STEP = 'S', UNDO = 'U', REDO = 'R', SAVED = 'M';
    /** Steps the base map already shows (undo list) or does not show yet (redo list). */
    private static final int HISTORY = 'H', FUTURE = 'F', UNSAVED = 'D';
    /** A journal with more change records than this is written anew when it is opened. */
    private static final int REWRITE_ABOVE = 1000;

    private final File mapFile, journal;
    private PudFile pud;
    private RandomAccessFile out;
    private boolean unsaved;
    private boolean resumed;
    /**
     * The map file's bytes as last read or written (FIX-ROUND 3.4.89, big test 3.5.0 finding 8):
     * a change taken back by Undo leaves the map as saved, and "Save changes?" is not asked.
     * Null while the map has no file (a new map never saved).
     */
    private byte[] savedBytes;

    private WorkingCopy(File mapFile, File journal) {
        this.mapFile = mapFile;
        this.journal = journal;
    }

    /** Where the working copies of a folder of maps live. */
    public static File directory(File mapsDir) {
        return new File(mapsDir, ".work");
    }

    public static File journalOf(File mapFile) {
        return new File(directory(mapFile.getParentFile()), mapFile.getName() + ".journal");
    }

    /**
     * Open a map for editing: from its journal when there is a readable one, else from the map
     * file, starting a new journal.
     */
    public static WorkingCopy open(File mapFile) throws IOException {
        final File journal = journalOf(mapFile);
        final WorkingCopy w = new WorkingCopy(mapFile, journal);
        int changes = -1;
        if (journal.isFile()) {
            try {
                changes = w.replay();
            } catch (IOException e) {
                changes = -1;     // not a journal of this version: begin again from the map
            }
        }
        if (changes < 0) {
            w.pud = PudFile.read(mapFile);
            w.unsaved = false;
            w.rewrite();
        } else {
            w.resumed = true;
            if (changes > REWRITE_ABOVE) {
                w.rewrite();
            }
        }
        w.out = new RandomAccessFile(journal, "rw");
        w.out.seek(w.out.length());
        w.savedBytes = readSaved(mapFile);
        if (w.unsaved) {
            w.compareWithSaved();
        }
        return w;
    }

    /** The map file's bytes, or null when there is none or it cannot be read. */
    private static byte[] readSaved(File mapFile) {
        if (!mapFile.isFile()) {
            return null;
        }
        try {
            return java.nio.file.Files.readAllBytes(mapFile.toPath());
        } catch (IOException e) {
            return null;
        }
    }

    /**
     * Whether the map now differs from its file: unsaved only when it does. When it is the saved
     * map again the journal says so, so that a later opening agrees.
     */
    private void compareWithSaved() throws IOException {
        if (savedBytes == null || !java.util.Arrays.equals(pud.bytes(), savedBytes)) {
            return;
        }
        unsaved = false;
        if (out != null) {
            append(out, SAVED, new byte[0]);
        }
    }

    /**
     * Begin a new map as a working copy alone: the journal is written with the map as its base
     * and marked unsaved; no map file is made until it is saved.
     *
     * @throws IOException when the map or a working copy of that name is already there
     */
    public static WorkingCopy create(File mapFile, PudFile pud) throws IOException {
        final File journal = journalOf(mapFile);
        if (mapFile.exists() || journal.exists()) {
            throw new IOException("a map of that name is already there");
        }
        final WorkingCopy w = new WorkingCopy(mapFile, journal);
        w.pud = pud;
        w.unsaved = true;
        w.rewrite();
        w.out = new RandomAccessFile(journal, "rw");
        w.out.seek(w.out.length());
        return w;
    }

    /**
     * Read a map's working copy without opening it for changes: the map as the journal leaves
     * it and whether it holds unsaved work. Null when there is no readable journal.
     */
    public static WorkingCopy peek(File mapFile) {
        final File journal = journalOf(mapFile);
        if (!journal.isFile()) {
            return null;
        }
        final WorkingCopy w = new WorkingCopy(mapFile, journal);
        try {
            w.replay();
        } catch (IOException | RuntimeException e) {
            return null;
        }
        return w;
    }

    /** True when the map has a working copy with unsaved work, or is a working copy alone. */
    public static boolean hasDraft(File mapFile) {
        final WorkingCopy w = peek(mapFile);
        return w != null && (w.unsaved || !mapFile.isFile());
    }

    /** True for a new map that was never saved: a working copy with no map file. */
    public static boolean draftOnly(File mapFile) {
        return !mapFile.isFile() && journalOf(mapFile).isFile();
    }

    public MapModel model() { return pud.model(); }
    public PudFile pud() { return pud; }
    public File mapFile() { return mapFile; }
    /** True when the map came from a journal left by an earlier session. */
    public boolean resumed() { return resumed; }
    /** True when something was changed, undone or redone since the map file was last written. */
    public boolean unsaved() { return unsaved; }

    // ----- changes -----

    /** Make a change and put it in the journal. A null step is nothing. */
    public void apply(MapModel.Step s) throws IOException {
        if (s == null) {
            return;
        }
        final byte[] bytes = MapModel.encode(s);
        pud.model().apply(s);
        unsaved = true;
        append(out, STEP, bytes);
        compareWithSaved();
    }

    public boolean undo() throws IOException {
        if (!pud.model().undo()) {
            return false;
        }
        unsaved = true;
        append(out, UNDO, new byte[0]);
        compareWithSaved();
        return true;
    }

    public boolean redo() throws IOException {
        if (!pud.model().redo()) {
            return false;
        }
        unsaved = true;
        append(out, REDO, new byte[0]);
        compareWithSaved();
        return true;
    }

    /** Write the map file from the working copy. The undo list stays. */
    public void save() throws IOException {
        final byte[] data = pud.bytes();
        PudFile.writeBytes(mapFile, data);
        savedBytes = data;
        unsaved = false;
        append(out, SAVED, new byte[0]);
    }

    /**
     * Stop editing and keep the working copy as a draft: the map file is as it was last saved
     * (or is not there at all), and the next opening brings the map and its undo list back.
     */
    public void keep() throws IOException {
        close();
    }

    /**
     * Stop editing and forget the working copy; the map file is as it was last saved. Of a new
     * map that was never saved nothing is left.
     */
    public void discard() throws IOException {
        close();
        if (journal.exists() && !journal.delete()) {
            throw new IOException("could not remove the working copy");
        }
    }

    @Override public void close() throws IOException {
        if (out != null) {
            out.close();
            out = null;
        }
    }

    // ----- the journal -----

    private static void append(RandomAccessFile to, int kind, byte[] payload) throws IOException {
        final ByteArrayOutputStream bytes = new ByteArrayOutputStream(payload.length + 9);
        final DataOutputStream d = new DataOutputStream(bytes);
        d.writeByte(kind);
        d.writeInt(payload.length);
        d.write(payload);
        final CRC32 crc = new CRC32();
        crc.update(payload, 0, payload.length);
        d.writeInt((int) crc.getValue());
        to.write(bytes.toByteArray());
        to.getFD().sync();
    }

    /**
     * Read the journal into the map and its lists; cut off a last record that is not whole.
     *
     * @return the number of change records played
     * @throws IOException when the file is not a journal or has no base map
     */
    private int replay() throws IOException {
        int changes = 0;
        try (RandomAccessFile in = new RandomAccessFile(journal, "rw")) {
            final byte[] magic = new byte[MAGIC.length];
            if (in.length() < MAGIC.length) {
                throw new IOException("not a journal");
            }
            in.readFully(magic);
            if (!java.util.Arrays.equals(magic, MAGIC)) {
                throw new IOException("not a journal");
            }
            long good = in.getFilePointer();
            unsaved = false;
            while (true) {
                final long left = in.length() - in.getFilePointer();
                if (left < 9) {
                    break;
                }
                final int kind = in.readUnsignedByte();
                final int length = in.readInt();
                if (length < 0 || length + 4L > in.length() - in.getFilePointer()) {
                    break;
                }
                final byte[] payload = new byte[length];
                in.readFully(payload);
                final CRC32 crc = new CRC32();
                crc.update(payload, 0, length);
                if (in.readInt() != (int) crc.getValue()) {
                    break;
                }
                if (pud == null && kind != BASE) {
                    throw new IOException("journal without a base map");
                }
                final MapModel m = (pud == null) ? null : pud.model();
                switch (kind) {
                    case BASE: pud = PudFile.parse(payload); break;
                    case STEP: m.apply(step(payload)); unsaved = true; ++changes; break;
                    case UNDO: m.undo(); unsaved = true; ++changes; break;
                    case REDO: m.redo(); unsaved = true; ++changes; break;
                    case HISTORY: m.recallDone(step(payload)); break;
                    case FUTURE: m.recallUndone(step(payload)); break;
                    case SAVED: unsaved = false; break;
                    case UNSAVED: unsaved = true; break;
                    default: throw new IOException("journal record of unknown kind " + kind);
                }
                good = in.getFilePointer();
            }
            if (pud == null) {
                throw new IOException("journal without a base map");
            }
            if (good < in.length()) {
                in.setLength(good);
            }
        }
        return changes;
    }

    private static MapModel.Step step(byte[] payload) throws IOException {
        return MapModel.decode(new DataInputStream(new ByteArrayInputStream(payload)));
    }

    /**
     * Write the journal anew from what is in memory: the map as it stands as the base, the undo
     * list as steps it already shows, the redo list as steps it does not.
     */
    private void rewrite() throws IOException {
        final File dir = journal.getParentFile();
        if (!dir.isDirectory() && !dir.mkdirs()) {
            throw new IOException("could not make the working folder");
        }
        final File tmp = new File(journal.getPath() + ".tmp");
        try (RandomAccessFile to = new RandomAccessFile(tmp, "rw")) {
            to.setLength(0);
            to.write(MAGIC);
            append(to, BASE, pud.bytes());
            for (MapModel.Step s : pud.model().doneSteps()) {
                append(to, HISTORY, MapModel.encode(s));
            }
            for (MapModel.Step s : pud.model().undoneSteps()) {
                append(to, FUTURE, MapModel.encode(s));
            }
            if (unsaved) {
                append(to, UNSAVED, new byte[0]);
            }
        }
        java.nio.file.Files.move(tmp.toPath(), journal.toPath(),
                                 java.nio.file.StandardCopyOption.REPLACE_EXISTING);
    }
}
