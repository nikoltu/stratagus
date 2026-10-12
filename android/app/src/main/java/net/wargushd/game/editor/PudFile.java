package net.wargushd.game.editor;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * A PUD file read into a {@link MapModel} and written back.
 *
 * A PUD file is a sequence of sections: a four-character tag, a 32-bit little-endian length and
 * that many bytes. The sections the model is made of are DIM (size), ERA / ERAX (tileset), DESC
 * (description), OWNR, SIDE, SGLD, SLBR, SOIL, AIPL (the sixteen slots), MTXM (tiles) and UNIT.
 * Every other section is carried as the bytes it came with and written back untouched, in the
 * place it had: TYPE, VER, UDTA, ALOW, UGRD, SQM, OILM, REGM, SIGN and any tag not known here.
 *
 * Writing keeps the file's own bytes wherever the model still says what the file said: a
 * section is encoded anew only when its encoding differs from the one made right after reading.
 * So a map read and written back unchanged is the same file byte for byte, also where a section
 * is longer than usual or holds bytes the model has no place for.
 */
public final class PudFile {
    /** The tags the model is made of, in the order a new file gets them. */
    private static final String[] MODELLED = {
        "DESC", "OWNR", "ERA ", "DIM ", "SIDE", "SGLD", "SLBR", "SOIL", "AIPL", "MTXM", "UNIT" };
    private static final int DESCRIPTION_BYTES = 32;
    /** The usual lengths of the unit-data and upgrade sections (a new map's are empty but this long). */
    private static final int UNIT_DATA_BYTES = 5696, UPGRADE_DATA_BYTES = 782;

    private static final class Section {
        final String tag;
        byte[] body;
        /** For a section the model is read from: its encoding from the model as read; else null. */
        byte[] asRead;

        Section(String tag, byte[] body) { this.tag = tag; this.body = body; }
    }

    private final ArrayList<Section> sections = new ArrayList<Section>();
    /** Bytes after the last whole section (a damaged file); written back as they are. */
    private byte[] tail = new byte[0];
    /** For a modelled tag the file lacks: its encoding from the model as read. */
    private final java.util.HashMap<String, byte[]> absent = new java.util.HashMap<String, byte[]>();
    private final ArrayList<String> remarks = new ArrayList<String>();
    private MapModel model;

    private PudFile() {}

    public MapModel model() { return model; }

    /** What was odd about the file as read; empty for a regular file. */
    public List<String> remarks() { return remarks; }

    /** The file's tags in order. */
    public List<String> tags() {
        final ArrayList<String> out = new ArrayList<String>();
        for (Section s : sections) {
            out.add(s.tag);
        }
        return out;
    }

    /**
     * The modelled sections whose bytes in the file are not what the model encodes to (a
     * description with bytes after its end, a longer section): they are kept as read.
     */
    public List<String> keptAsRead() {
        final ArrayList<String> out = new ArrayList<String>();
        for (Section s : sections) {
            if (s.asRead != null && !Arrays.equals(s.asRead, s.body)) {
                out.add(s.tag);
            }
        }
        return out;
    }

    // ----- reading -----

    public static PudFile read(File f) throws IOException {
        if (f.length() > 64L * 1024 * 1024) {
            throw new IOException("too large for a map");
        }
        final byte[] all = new byte[(int) f.length()];
        try (InputStream in = new FileInputStream(f)) {
            int got = 0;
            while (got < all.length) {
                final int n = in.read(all, got, all.length - got);
                if (n < 0) {
                    throw new IOException("file ended early");
                }
                got += n;
            }
        }
        return parse(all);
    }

    /** @throws IOException when the bytes hold no size or no tiles, i.e. are not a map */
    public static PudFile parse(byte[] data) throws IOException {
        final PudFile p = new PudFile();
        int at = 0;
        while (at < data.length) {
            if (at + 8 > data.length) {
                p.remarks.add((data.length - at) + " bytes after the last section");
                break;
            }
            final long length = u32(data, at + 4);
            if (at + 8 + length > data.length) {
                p.remarks.add("a section runs past the end of the file");
                break;
            }
            final String tag = new String(data, at, 4, StandardCharsets.ISO_8859_1);
            p.sections.add(new Section(tag, Arrays.copyOfRange(data, at + 8, at + 8 + (int) length)));
            at += 8 + (int) length;
        }
        p.tail = Arrays.copyOfRange(data, at, data.length);
        if (p.sections.isEmpty() || !p.sections.get(0).tag.equals("TYPE")
            || !startsWith(p.sections.get(0).body, "WAR2 MAP")) {
            p.remarks.add("the first section is not TYPE \"WAR2 MAP\"");
        }

        final byte[] dim = p.body("DIM ");
        if (dim == null || dim.length < 4) {
            throw new IOException("no map size");
        }
        final int w = u16(dim, 0), h = u16(dim, 2);
        final byte[] mtxm = p.body("MTXM");
        if (w <= 0 || h <= 0 || w > 1024 || h > 1024 || mtxm == null || mtxm.length < w * h * 2) {
            throw new IOException("no tiles for a map of " + w + " x " + h);
        }
        final MapModel m = new MapModel(w, h, 0, 0);
        for (int i = 0; i < w * h; ++i) {
            m.loadTile(i, u16(mtxm, i * 2));
        }
        if (mtxm.length != w * h * 2) {
            p.remarks.add("MTXM is longer than width x height");
        }

        // The expansion's tag, when present, overrides the base one.
        final byte[] era = (p.body("ERAX") != null) ? p.body("ERAX") : p.body("ERA ");
        if (era != null && era.length >= 2) {
            final int e = u16(era, 0);
            if (e > 3) {
                p.remarks.add("tileset number " + e + " read as 0");
            }
            m.loadTileset(e <= 3 ? e : 0);
        }
        final byte[] desc = p.body("DESC");
        if (desc != null) {
            int end = 0;
            while (end < desc.length && desc[end] != 0) {
                ++end;
            }
            m.loadDescription(new String(desc, 0, end, StandardCharsets.ISO_8859_1));
        }
        p.slots(m, "OWNR", MapModel.FIELD_OWNER, 1);
        p.slots(m, "SIDE", MapModel.FIELD_RACE, 1);
        p.slots(m, "AIPL", MapModel.FIELD_AI, 1);
        p.slots(m, "SGLD", MapModel.FIELD_GOLD, 2);
        p.slots(m, "SLBR", MapModel.FIELD_LUMBER, 2);
        p.slots(m, "SOIL", MapModel.FIELD_OIL, 2);
        final byte[] unit = p.body("UNIT");
        if (unit != null) {
            for (int i = 0; i + 8 <= unit.length; i += 8) {
                m.loadUnit(new MapModel.Unit(u16(unit, i), u16(unit, i + 2), unit[i + 4] & 0xFF,
                                             unit[i + 5] & 0xFF, u16(unit, i + 6)));
            }
            if (unit.length % 8 != 0) {
                p.remarks.add("UNIT length is not a multiple of 8");
            }
        }
        p.model = m;
        p.rememberAsRead();
        return p;
    }

    /** A new file for a model made in the editor: the sections of the game's own maps, in their order. */
    public static PudFile create(MapModel m, int id) {
        final PudFile p = new PudFile();
        p.model = m;
        final byte[] type = new byte[16];
        System.arraycopy("WAR2 MAP".getBytes(StandardCharsets.US_ASCII), 0, type, 0, 8);
        type[10] = 0x0A;
        type[11] = (byte) 0xFF;
        put32(type, 12, id);
        p.sections.add(new Section("TYPE", type));
        p.sections.add(new Section("VER ", new byte[] { 0x11, 0 }));
        for (String tag : new String[] { "DESC", "OWNR", "ERA ", "DIM " }) {
            p.sections.add(new Section(tag, p.encode(tag)));
        }
        // Unit and upgrade data: the "use the defaults" word and nothing else.
        final byte[] unitData = new byte[UNIT_DATA_BYTES], upgradeData = new byte[UPGRADE_DATA_BYTES];
        unitData[0] = 1;
        upgradeData[0] = 1;
        p.sections.add(new Section("UDTA", unitData));
        p.sections.add(new Section("UGRD", upgradeData));
        for (String tag : new String[] { "SIDE", "SGLD", "SLBR", "SOIL", "AIPL", "MTXM" }) {
            p.sections.add(new Section(tag, p.encode(tag)));
        }
        final int count = m.width() * m.height();
        p.sections.add(new Section("SQM ", new byte[count * 2]));
        p.sections.add(new Section("OILM", new byte[count]));
        p.sections.add(new Section("REGM", new byte[count * 2]));
        p.sections.add(new Section("UNIT", p.encode("UNIT")));
        p.rememberAsRead();
        return p;
    }

    private void slots(MapModel m, String tag, int field, int bytes) {
        final byte[] b = body(tag);
        if (b == null) {
            return;
        }
        for (int s = 0; s < MapModel.SLOTS && (s + 1) * bytes <= b.length; ++s) {
            m.loadSlot(field, s, bytes == 1 ? (b[s] & 0xFF) : u16(b, s * 2));
        }
    }

    /** The section the model reads a tag from: the last of that tag, as a later one replaces an earlier. */
    private Section governing(String tag) {
        if (tag.equals("ERA ")) {
            final Section x = last("ERAX");
            if (x != null) {
                return x;
            }
        }
        return last(tag);
    }

    private Section last(String tag) {
        for (int i = sections.size() - 1; i >= 0; --i) {
            if (sections.get(i).tag.equals(tag)) {
                return sections.get(i);
            }
        }
        return null;
    }

    private byte[] body(String tag) {
        final Section s = last(tag);
        return (s == null) ? null : s.body;
    }

    private void rememberAsRead() {
        for (String tag : MODELLED) {
            final Section s = governing(tag);
            if (s != null) {
                s.asRead = encode(tag);
            } else {
                absent.put(tag, encode(tag));
            }
        }
    }

    // ----- writing -----

    /** A modelled section's bytes from the model alone. */
    private byte[] encode(String tag) {
        final MapModel m = model;
        if (tag.equals("DIM ")) {
            return words(m.width(), m.height());
        } else if (tag.equals("ERA ")) {
            return words(m.tileset());
        } else if (tag.equals("DESC")) {
            final byte[] out = new byte[DESCRIPTION_BYTES];
            final byte[] text = m.description().getBytes(StandardCharsets.ISO_8859_1);
            System.arraycopy(text, 0, out, 0, Math.min(text.length, DESCRIPTION_BYTES - 1));
            return out;
        } else if (tag.equals("OWNR")) {
            return slotBytes(MapModel.FIELD_OWNER);
        } else if (tag.equals("SIDE")) {
            return slotBytes(MapModel.FIELD_RACE);
        } else if (tag.equals("AIPL")) {
            return slotBytes(MapModel.FIELD_AI);
        } else if (tag.equals("SGLD")) {
            return slotWords(MapModel.FIELD_GOLD);
        } else if (tag.equals("SLBR")) {
            return slotWords(MapModel.FIELD_LUMBER);
        } else if (tag.equals("SOIL")) {
            return slotWords(MapModel.FIELD_OIL);
        } else if (tag.equals("MTXM")) {
            final int n = m.width() * m.height();
            final byte[] out = new byte[n * 2];
            for (int i = 0; i < n; ++i) {
                put16(out, i * 2, m.tileAt(i));
            }
            return out;
        } else if (tag.equals("UNIT")) {
            final byte[] out = new byte[m.unitCount() * 8];
            for (int i = 0; i < m.unitCount(); ++i) {
                final MapModel.Unit u = m.unit(i);
                put16(out, i * 8, u.x);
                put16(out, i * 8 + 2, u.y);
                out[i * 8 + 4] = (byte) u.type;
                out[i * 8 + 5] = (byte) u.owner;
                put16(out, i * 8 + 6, u.data);
            }
            return out;
        }
        throw new IllegalArgumentException(tag);
    }

    private byte[] slotBytes(int field) {
        final byte[] out = new byte[MapModel.SLOTS];
        for (int s = 0; s < MapModel.SLOTS; ++s) {
            out[s] = (byte) model.slot(field, s);
        }
        return out;
    }

    private byte[] slotWords(int field) {
        final byte[] out = new byte[MapModel.SLOTS * 2];
        for (int s = 0; s < MapModel.SLOTS; ++s) {
            put16(out, s * 2, model.slot(field, s));
        }
        return out;
    }

    /** The file as the model stands now. */
    public byte[] bytes() {
        final ByteArrayOutputStream out = new ByteArrayOutputStream();
        for (Section s : sections) {
            byte[] body = s.body;
            if (s.asRead != null) {
                final String tag = s.tag.equals("ERAX") ? "ERA " : s.tag;
                final byte[] now = encode(tag);
                if (!Arrays.equals(now, s.asRead)) {
                    // Changed: the new encoding, then whatever the file had beyond a section of
                    // the length it had when read (bytes the model has no place for).
                    body = now;
                    if (s.body.length > s.asRead.length) {
                        body = Arrays.copyOf(now, now.length + s.body.length - s.asRead.length);
                        System.arraycopy(s.body, s.asRead.length, body, now.length,
                                         s.body.length - s.asRead.length);
                    }
                }
            }
            section(out, s.tag, body);
        }
        // A modelled section the file lacked is added only when the model no longer says the default.
        for (String tag : MODELLED) {
            final byte[] was = absent.get(tag);
            if (was != null) {
                final byte[] now = encode(tag);
                if (!Arrays.equals(now, was)) {
                    section(out, tag, now);
                }
            }
        }
        out.write(tail, 0, tail.length);
        return out.toByteArray();
    }

    /** Write the file beside the target and put it in its place, so a failed write leaves the old file. */
    public void write(File target) throws IOException {
        writeBytes(target, bytes());
    }

    static void writeBytes(File target, byte[] data) throws IOException {
        final File tmp = new File(target.getPath() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(data);
            out.getFD().sync();
        }
        try {
            java.nio.file.Files.move(tmp.toPath(), target.toPath(),
                                     java.nio.file.StandardCopyOption.REPLACE_EXISTING);
        } catch (IOException e) {
            tmp.delete();
            throw e;
        }
    }

    private static void section(ByteArrayOutputStream out, String tag, byte[] body) {
        final byte[] head = new byte[8];
        System.arraycopy(tag.getBytes(StandardCharsets.ISO_8859_1), 0, head, 0, 4);
        put32(head, 4, body.length);
        out.write(head, 0, 8);
        out.write(body, 0, body.length);
    }

    // ----- bytes -----

    private static boolean startsWith(byte[] b, String text) {
        final byte[] t = text.getBytes(StandardCharsets.US_ASCII);
        return b.length >= t.length && Arrays.equals(Arrays.copyOf(b, t.length), t);
    }

    private static int u16(byte[] b, int at) {
        return (b[at] & 0xFF) | ((b[at + 1] & 0xFF) << 8);
    }

    private static long u32(byte[] b, int at) {
        return (b[at] & 0xFFL) | ((b[at + 1] & 0xFFL) << 8) | ((b[at + 2] & 0xFFL) << 16)
            | ((b[at + 3] & 0xFFL) << 24);
    }

    private static void put16(byte[] b, int at, int v) {
        b[at] = (byte) v;
        b[at + 1] = (byte) (v >> 8);
    }

    private static void put32(byte[] b, int at, int v) {
        put16(b, at, v);
        put16(b, at + 2, v >> 16);
    }

    private static byte[] words(int... values) {
        final byte[] b = new byte[values.length * 2];
        for (int i = 0; i < values.length; ++i) {
            put16(b, i * 2, values[i]);
        }
        return b;
    }
}
