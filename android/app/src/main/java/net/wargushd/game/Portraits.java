package net.wargushd.game;

import android.graphics.Bitmap;

/**
 * Character portraits in a player's colour.
 *
 * A portrait frame carries its team-colour region either baked in magenta or, for machines,
 * ships and flyers, baked in a faction colour and described by a separate mask frame. The
 * region is found by the same test the in-game interface uses, widened by the mask so the
 * dim fringe is included, and each of its pixels becomes colour x intensity.
 */
final class Portraits {
    private static final String DIR = "Data/Art/hd/HUD";
    /** Size of one portrait frame in the atlas. */
    static final int FRAME_W = 207, FRAME_H = 171;

    private Portraits() {}

    /**
     * The portrait of a slot with its team region in the given colour, or null when the
     * game's files cannot be read. A slot without a team region is returned unchanged.
     */
    static Bitmap tinted(SkinKit kit, int slot, int argb) {
        final Bitmap face = kit.atlasFrameIn(DIR, "Portrait-face", "forest_" + slot, FRAME_W, FRAME_H);
        if (face == null) {
            return null;
        }
        final Bitmap mask =
            kit.atlasFrameIn(DIR, "Portrait-mask", "forest_" + slot + "_team", FRAME_W, FRAME_H);
        return tint(face, mask, argb);
    }

    /** As tinted, for many slots at once: the frames are cut together. */
    static Bitmap[] tintedAll(SkinKit kit, int[] slots, int argb) {
        final String[] faces = new String[slots.length], masks = new String[slots.length];
        for (int i = 0; i < slots.length; ++i) {
            faces[i] = "forest_" + slots[i];
            masks[i] = "forest_" + slots[i] + "_team";
        }
        final Bitmap[] face = kit.atlasFramesIn(DIR, "Portrait-face", faces);
        final Bitmap[] mask = kit.atlasFramesIn(DIR, "Portrait-mask", masks);
        final Bitmap[] out = new Bitmap[slots.length];
        for (int i = 0; i < slots.length; ++i) {
            out[i] = (face[i] != null) ? tint(face[i], mask[i], argb) : null;
        }
        return out;
    }

    private static Bitmap tint(Bitmap face, Bitmap mask, int argb) {
        final int w = face.getWidth(), h = face.getHeight();
        final int[] px = new int[w * h];
        face.getPixels(px, 0, w, 0, 0, w, h);
        int[] mk = null;
        if (mask != null && mask.getWidth() == w && mask.getHeight() == h) {
            mk = new int[w * h];
            mask.getPixels(mk, 0, w, 0, 0, w, h);
        }

        // Pixels that pass the in-game test: saturated magenta.
        final boolean[] key = new boolean[px.length];
        int keyCount = 0;
        for (int i = 0; i < px.length; ++i) {
            final int a = px[i] >>> 24, r = (px[i] >> 16) & 0xFF, g = (px[i] >> 8) & 0xFF, b = px[i] & 0xFF;
            key[i] = a > 24 && r > 48 && b > 48 && g < r - 24 && g < b - 24 && Math.abs(r - b) < 120;
            if (key[i]) {
                ++keyCount;
            }
        }
        final boolean magentaBaked = keyCount >= 100;

        final int cr = (argb >> 16) & 0xFF, cg = (argb >> 8) & 0xFF, cb = argb & 0xFF;
        for (int i = 0; i < px.length; ++i) {
            final int a = px[i] >>> 24, r = (px[i] >> 16) & 0xFF, g = (px[i] >> 8) & 0xFF, b = px[i] & 0xFF;
            final boolean masked = mk != null && a > 24 && ((mk[i] >> 16) & 0xFF) > 8;
            final boolean selected;
            if (mk == null) {
                selected = key[i];
            } else if (magentaBaked) {
                final boolean loose =
                    g < r - 8 && g < b - 8 && Math.abs(r - b) < 120 && r > 12 && b > 12;
                selected = key[i] || (masked && loose);
            } else {
                selected = masked;
            }
            if (!selected) {
                continue;
            }
            final int intensity = key[i] ? (r + b) / 2 : Math.max(r, Math.max(g, b));
            px[i] = (a << 24) | ((cr * intensity / 255) << 16) | ((cg * intensity / 255) << 8)
                | (cb * intensity / 255);
        }
        final Bitmap out = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        out.setPixels(px, 0, w, 0, 0, w, h);
        return out;
    }
}
