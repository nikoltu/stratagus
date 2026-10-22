package net.wargushd.game.editor;

/**
 * The owner cells of the one-kind unit strip (owner decision 94, E2 = option A of
 * EDITOR_NEXT_IMPL.md 4.2): eight colour cells 96 x 96 at a pitch of 104 between the name and
 * the next cell of the strip (the AI word at x 1740, Remove at 2748); the current owner ringed
 * gold; a tap gives the chosen units to that player slot. Logical units, x from the screen's
 * left; no Android, so the JVM test reaches it.
 */
public final class StripOwners {
    public static final int COUNT = 8;
    public static final float CELL = 96f, PITCH = 104f;
    /** The first cell's left edge: the row ends 16 before the AI word cell (x 1740). */
    public static final float X = 1740f - 16f - (COUNT - 1) * PITCH - CELL;
    /** The room the name keeps before the cells. */
    public static final float NAME_GAP = 24f;

    private StripOwners() {}

    /** The left edge of the cell of slot i. */
    public static float left(int i) { return X + i * PITCH; }

    /** The right edge of the last cell. */
    public static float right() { return left(COUNT - 1) + CELL; }

    /** The cell under x, or -1: only inside a cell, not in the gap between two. */
    public static int at(float x) {
        if (x < X) {
            return -1;
        }
        final float in = x - X;
        final int i = (int) Math.floor(in / PITCH);
        return (i >= 0 && i < COUNT && in - i * PITCH < CELL) ? i : -1;
    }
}
