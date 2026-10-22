package net.wargushd.game.editor;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/**
 * A map as the editor holds it: size, tileset, one tile number a tile, the units and the sixteen
 * player slots, as a PUD file stores them. Nothing here knows the screen or Android.
 *
 * Every change is made through a {@link Step}: {@link #apply} runs it and puts it on the undo
 * list, {@link #undo} and {@link #redo} move along that list. A step holds what it replaced, so
 * it can be taken back, and it can be written out and read back for the working copy's journal.
 */
public final class MapModel {
    public static final int SLOTS = 16;
    /** The slot that owns gold mines, oil patches and critters. */
    public static final int NEUTRAL_SLOT = 15;
    public static final int OWNER_NEUTRAL = 2, OWNER_NOBODY = 3, OWNER_COMPUTER = 4, OWNER_PERSON = 5;
    /**
     * The rescue kinds of a slot (engine: rescue-passive / rescue-active). Both join the person whose
     * unit reaches them; 7 also runs the computer's AI against the computers (owner decision 93, 1A).
     */
    public static final int OWNER_RESCUE_WAITS = 6, OWNER_RESCUE_FIGHTS = 7;
    public static final int RACE_HUMAN = 0, RACE_ORC = 1, RACE_NEUTRAL = 2;
    /** A gold mine or an oil patch stores its amount in units of this size. */
    public static final int RESOURCE_UNIT = 2500;
    /** How many steps can be taken back. */
    public static final int UNDO_LIMIT = 200;

    /** The settings of a slot a step can change. */
    public static final int FIELD_OWNER = 0, FIELD_RACE = 1, FIELD_GOLD = 2, FIELD_LUMBER = 3,
        FIELD_OIL = 4, FIELD_AI = 5;
    private static final int FIELDS = 6;

    /** One unit record: position in tiles, the file's type number, owner slot and data word. */
    public static final class Unit {
        public final int x, y, type, owner, data;

        public Unit(int x, int y, int type, int owner, int data) {
            this.x = x; this.y = y; this.type = type; this.owner = owner; this.data = data;
        }

        @Override public boolean equals(Object o) {
            if (!(o instanceof Unit)) {
                return false;
            }
            final Unit u = (Unit) o;
            return x == u.x && y == u.y && type == u.type && owner == u.owner && data == u.data;
        }

        @Override public int hashCode() {
            return ((((x * 31 + y) * 31 + type) * 31 + owner) * 31) + data;
        }
    }

    private int width, height;
    private int tileset;
    private String description = "";
    /** Tile numbers row by row, each 0..65535. */
    private int[] tiles;
    private final ArrayList<Unit> units = new ArrayList<Unit>();
    /** The slots' settings: [field][slot]. */
    private final int[][] slots = new int[FIELDS][SLOTS];

    private final ArrayList<Step> undone = new ArrayList<Step>();   // steps that can be redone
    private final ArrayList<Step> done = new ArrayList<Step>();     // steps that can be undone

    /** An empty map: every tile the given number, no units, every slot owned by nobody. */
    public MapModel(int width, int height, int tileset, int tile) {
        if (width <= 0 || height <= 0 || width > 1024 || height > 1024) {
            throw new IllegalArgumentException("map size " + width + " x " + height);
        }
        this.width = width;
        this.height = height;
        this.tileset = tileset;
        tiles = new int[width * height];
        java.util.Arrays.fill(tiles, tile & 0xFFFF);
        java.util.Arrays.fill(slots[FIELD_OWNER], OWNER_NOBODY);
        slots[FIELD_OWNER][NEUTRAL_SLOT] = OWNER_NEUTRAL;
        slots[FIELD_RACE][NEUTRAL_SLOT] = RACE_NEUTRAL;
    }

    // ----- reading -----

    public int width() { return width; }
    public int height() { return height; }
    /** 0 forest, 1 winter, 2 wasteland, 3 swamp. */
    public int tileset() { return tileset; }
    public String description() { return description; }
    public int tile(int x, int y) { return tiles[y * width + x]; }
    public int tileAt(int index) { return tiles[index]; }
    public int unitCount() { return units.size(); }
    public Unit unit(int index) { return units.get(index); }
    public int slot(int field, int slot) { return slots[field][slot]; }

    // ----- loading: sets a value without a step; for the file reader and tests -----

    void loadTileset(int value) { tileset = value; }
    void loadDescription(String value) { description = (value == null) ? "" : value; }
    void loadTile(int index, int value) { tiles[index] = value & 0xFFFF; }
    void loadUnit(Unit u) { units.add(u); }
    void loadSlot(int field, int slot, int value) { slots[field][slot] = value; }

    // ----- change steps -----

    /** One change that can be taken back. */
    public abstract static class Step {
        abstract void run(MapModel m);
        abstract void back(MapModel m);
        abstract int kind();
        abstract void write(DataOutputStream out) throws IOException;
    }

    /** Tiles replaced: a brush stroke or a fill is one such step. */
    public static final class TileStep extends Step {
        private final int[] index, before, after;

        private TileStep(int[] index, int[] before, int[] after) {
            this.index = index; this.before = before; this.after = after;
        }

        public int count() { return index.length; }

        @Override void run(MapModel m) {
            for (int i = 0; i < index.length; ++i) {
                m.tiles[index[i]] = after[i];
            }
        }

        @Override void back(MapModel m) {
            for (int i = index.length - 1; i >= 0; --i) {
                m.tiles[index[i]] = before[i];
            }
        }

        @Override int kind() { return 1; }

        @Override void write(DataOutputStream out) throws IOException {
            out.writeInt(index.length);
            for (int i = 0; i < index.length; ++i) {
                out.writeInt(index[i]);
                out.writeShort(before[i]);
                out.writeShort(after[i]);
            }
        }

        static TileStep read(DataInputStream in) throws IOException {
            final int n = in.readInt();
            if (n < 0 || n > 1024 * 1024) {
                throw new IOException("tile step of " + n + " tiles");
            }
            final int[] index = new int[n], before = new int[n], after = new int[n];
            for (int i = 0; i < n; ++i) {
                index[i] = in.readInt();
                before[i] = in.readUnsignedShort();
                after[i] = in.readUnsignedShort();
            }
            return new TileStep(index, before, after);
        }
    }

    /**
     * Collects the tiles of one stroke. A tile set more than once keeps its first "before";
     * a tile set back to what it was is left out of the step.
     */
    public final class TileEdit {
        private final java.util.LinkedHashMap<Integer, Integer> wanted = new java.util.LinkedHashMap<Integer, Integer>();

        public TileEdit set(int x, int y, int tile) {
            if (x >= 0 && y >= 0 && x < width && y < height) {
                wanted.put(y * width + x, tile & 0xFFFF);
            }
            return this;
        }

        /** The step, or null when nothing would change. */
        public TileStep step() {
            int n = 0;
            for (java.util.Map.Entry<Integer, Integer> e : wanted.entrySet()) {
                if (tiles[e.getKey()] != e.getValue()) {
                    ++n;
                }
            }
            if (n == 0) {
                return null;
            }
            final int[] index = new int[n], before = new int[n], after = new int[n];
            int i = 0;
            for (java.util.Map.Entry<Integer, Integer> e : wanted.entrySet()) {
                if (tiles[e.getKey()] != e.getValue()) {
                    index[i] = e.getKey();
                    before[i] = tiles[e.getKey()];
                    after[i] = e.getValue();
                    ++i;
                }
            }
            return new TileStep(index, before, after);
        }
    }

    public TileEdit editTiles() { return new TileEdit(); }

    /**
     * A unit placed (before null), removed (after null) or changed, at a place in the unit list.
     * The list's order is the file's order, so a removed unit comes back where it was.
     */
    public static final class UnitStep extends Step {
        private final int at;
        private final Unit before, after;

        private UnitStep(int at, Unit before, Unit after) {
            this.at = at; this.before = before; this.after = after;
        }

        @Override void run(MapModel m) { put(m, before, after); }
        @Override void back(MapModel m) { put(m, after, before); }

        private void put(MapModel m, Unit from, Unit to) {
            if (from == null) {
                m.units.add(at, to);
            } else if (to == null) {
                m.units.remove(at);
            } else {
                m.units.set(at, to);
            }
        }

        @Override int kind() { return 2; }

        @Override void write(DataOutputStream out) throws IOException {
            out.writeInt(at);
            writeUnit(out, before);
            writeUnit(out, after);
        }

        static UnitStep read(DataInputStream in) throws IOException {
            final int at = in.readInt();
            final Unit before = readUnit(in);
            final Unit after = readUnit(in);
            return new UnitStep(at, before, after);
        }

        private static void writeUnit(DataOutputStream out, Unit u) throws IOException {
            out.writeBoolean(u != null);
            if (u != null) {
                out.writeShort(u.x); out.writeShort(u.y);
                out.writeByte(u.type); out.writeByte(u.owner);
                out.writeShort(u.data);
            }
        }

        private static Unit readUnit(DataInputStream in) throws IOException {
            if (!in.readBoolean()) {
                return null;
            }
            final int x = in.readUnsignedShort(), y = in.readUnsignedShort();
            final int type = in.readUnsignedByte(), owner = in.readUnsignedByte();
            return new Unit(x, y, type, owner, in.readUnsignedShort());
        }
    }

    /** A unit added at the end of the list. */
    public UnitStep placeUnit(Unit u) { return new UnitStep(units.size(), null, u); }

    public UnitStep removeUnit(int index) { return new UnitStep(index, units.get(index), null); }

    /** The step, or null when the unit is already so. */
    public UnitStep changeUnit(int index, Unit to) {
        return units.get(index).equals(to) ? null : new UnitStep(index, units.get(index), to);
    }

    /** One setting of one slot changed. */
    public static final class SlotStep extends Step {
        private final int field, slot, before, after;

        private SlotStep(int field, int slot, int before, int after) {
            this.field = field; this.slot = slot; this.before = before; this.after = after;
        }

        @Override void run(MapModel m) { m.slots[field][slot] = after; }
        @Override void back(MapModel m) { m.slots[field][slot] = before; }
        @Override int kind() { return 3; }

        @Override void write(DataOutputStream out) throws IOException {
            out.writeByte(field); out.writeByte(slot);
            out.writeInt(before); out.writeInt(after);
        }

        static SlotStep read(DataInputStream in) throws IOException {
            final int field = in.readUnsignedByte(), slot = in.readUnsignedByte();
            if (field >= FIELDS || slot >= SLOTS) {
                throw new IOException("slot step for field " + field + " slot " + slot);
            }
            final int before = in.readInt();
            return new SlotStep(field, slot, before, in.readInt());
        }
    }

    /** The step, or null when the slot is already so. */
    public SlotStep changeSlot(int field, int slot, int value) {
        return (slots[field][slot] == value) ? null : new SlotStep(field, slot, slots[field][slot], value);
    }

    /** The description or the tileset changed. */
    public static final class HeadStep extends Step {
        private final String textBefore, textAfter;
        private final int tilesetBefore, tilesetAfter;

        private HeadStep(String textBefore, String textAfter, int tilesetBefore, int tilesetAfter) {
            this.textBefore = textBefore; this.textAfter = textAfter;
            this.tilesetBefore = tilesetBefore; this.tilesetAfter = tilesetAfter;
        }

        @Override void run(MapModel m) { m.description = textAfter; m.tileset = tilesetAfter; }
        @Override void back(MapModel m) { m.description = textBefore; m.tileset = tilesetBefore; }
        @Override int kind() { return 4; }

        @Override void write(DataOutputStream out) throws IOException {
            out.writeUTF(textBefore); out.writeUTF(textAfter);
            out.writeInt(tilesetBefore); out.writeInt(tilesetAfter);
        }

        static HeadStep read(DataInputStream in) throws IOException {
            final String a = in.readUTF(), b = in.readUTF();
            final int c = in.readInt();
            return new HeadStep(a, b, c, in.readInt());
        }
    }

    /** The step, or null when nothing would change. */
    public HeadStep changeHead(String text, int newTileset) {
        final String t = (text == null) ? "" : text;
        return (t.equals(description) && newTileset == tileset)
            ? null : new HeadStep(description, t, tileset, newTileset);
    }

    /** Several steps taken back and redone as one (a mirrored stroke, a group moved). */
    public static final class GroupStep extends Step {
        private final Step[] parts;

        public GroupStep(List<Step> parts) { this.parts = parts.toArray(new Step[0]); }

        @Override void run(MapModel m) {
            for (Step s : parts) {
                s.run(m);
            }
        }

        @Override void back(MapModel m) {
            for (int i = parts.length - 1; i >= 0; --i) {
                parts[i].back(m);
            }
        }

        @Override int kind() { return 5; }

        @Override void write(DataOutputStream out) throws IOException {
            out.writeInt(parts.length);
            for (Step s : parts) {
                out.write(encode(s));
            }
        }

        static GroupStep read(DataInputStream in) throws IOException {
            final int n = in.readInt();
            if (n < 0 || n > 65536) {
                throw new IOException("group of " + n + " steps");
            }
            final ArrayList<Step> parts = new ArrayList<Step>();
            for (int i = 0; i < n; ++i) {
                parts.add(decode(in));
            }
            return new GroupStep(parts);
        }
    }

    /** A step as bytes, for the journal. */
    public static byte[] encode(Step s) throws IOException {
        final ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        final DataOutputStream out = new DataOutputStream(bytes);
        out.writeByte(s.kind());
        s.write(out);
        out.flush();
        return bytes.toByteArray();
    }

    /** The step written by {@link #encode}. */
    public static Step decode(DataInputStream in) throws IOException {
        final int kind = in.readUnsignedByte();
        switch (kind) {
            case 1: return TileStep.read(in);
            case 2: return UnitStep.read(in);
            case 3: return SlotStep.read(in);
            case 4: return HeadStep.read(in);
            case 5: return GroupStep.read(in);
            default: throw new IOException("step of unknown kind " + kind);
        }
    }

    // ----- the undo list -----

    /** Make a change. Whatever had been undone can no longer be redone. A null step is nothing. */
    public void apply(Step s) {
        if (s == null) {
            return;
        }
        s.run(this);
        done.add(s);
        undone.clear();
        while (done.size() > UNDO_LIMIT) {
            done.remove(0);
        }
    }

    public boolean canUndo() { return !done.isEmpty(); }
    public boolean canRedo() { return !undone.isEmpty(); }
    public int undoCount() { return done.size(); }
    public int redoCount() { return undone.size(); }

    public boolean undo() {
        if (done.isEmpty()) {
            return false;
        }
        final Step s = done.remove(done.size() - 1);
        s.back(this);
        undone.add(s);
        return true;
    }

    public boolean redo() {
        if (undone.isEmpty()) {
            return false;
        }
        final Step s = undone.remove(undone.size() - 1);
        s.run(this);
        done.add(s);
        return true;
    }

    /** The steps that can be undone, oldest first; for the working copy when it rewrites itself. */
    List<Step> doneSteps() { return done; }
    /** The steps that can be redone, in the order they would be redone. */
    List<Step> undoneSteps() {
        final ArrayList<Step> order = new ArrayList<Step>(undone);
        java.util.Collections.reverse(order);
        return order;
    }

    /** Put a step on the undo list without running it: the map already shows it. */
    void recallDone(Step s) { done.add(s); }
    /** Put a step last on the redo list without running it. */
    void recallUndone(Step s) { undone.add(0, s); }
}
