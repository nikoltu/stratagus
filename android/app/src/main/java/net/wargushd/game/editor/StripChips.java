package net.wargushd.game.editor;

/**
 * The row of kind chips in the unit strip when units of several kinds are chosen (owner board
 * 6B, decision 88): chips of 440 at a pitch of 470 from x 560, Remove all at x 2700. More kinds
 * than the room holds scroll sideways under one finger (the board's "the chips scroll sideways
 * in the strip"). Logical units; no Android, so the JVM test reaches it.
 */
public final class StripChips {
    public static final float X = 560f, W = 440f, PITCH = 470f;
    /** The window the chips are seen through ends this far before Remove all. */
    public static final float GAP = PITCH - W;

    private StripChips() {}

    /** The right edge of the window the chips are seen through, for Remove all at allX. */
    public static float windowRight(float allX) { return allX - GAP; }

    /** How far the row may scroll for so many chips: 0 while they all fit. */
    public static float maxScroll(int chips, float allX) {
        final float right = (chips <= 0) ? X : X + chips * PITCH - GAP;
        return Math.max(0f, right - windowRight(allX));
    }

    /** A scroll held inside 0 .. maxScroll. */
    public static float clamp(float scroll, int chips, float allX) {
        return Math.max(0f, Math.min(maxScroll(chips, allX), scroll));
    }

    /** The left edge of chip i at a scroll. */
    public static float left(int i, float scroll) { return X + i * PITCH - scroll; }

    /**
     * The chip under x at a scroll, or -1: only inside the window and inside a chip (not in the
     * gap between two).
     */
    public static int at(float x, float scroll, int chips, float allX) {
        if (x < X || x >= windowRight(allX)) {
            return -1;
        }
        final float in = x - X + scroll;
        final int i = (int) Math.floor(in / PITCH);
        return (i >= 0 && i < chips && in - i * PITCH < W) ? i : -1;
    }
}
