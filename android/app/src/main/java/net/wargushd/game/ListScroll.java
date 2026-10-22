package net.wargushd.game;

/**
 * Scrolling a list so that one row can be seen. Plain Java: checked on a PC without Android.
 */
final class ListScroll {
    private ListScroll() {}

    /**
     * The scroll at which the row from `rowTop` to `rowBottom` (list coordinates) is in a view
     * `view` high, moved as little as possible from `scroll`, within 0..`max`. A row already in
     * view leaves the scroll as it is, so the rows above it (newer saves, say) stay in sight.
     */
    static float reveal(float rowTop, float rowBottom, float scroll, float view, float max) {
        float s = scroll;
        if (rowBottom > s + view) {
            s = rowBottom - view;
        }
        if (rowTop < s) {
            s = rowTop;
        }
        return Math.max(0f, Math.min(max, s));
    }
}
