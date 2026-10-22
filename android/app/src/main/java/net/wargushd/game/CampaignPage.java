package net.wargushd.game;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;

import org.json.JSONObject;

import java.io.File;

/**
 * The Campaign page: the two game logos and, under them, the four campaigns as cards. A
 * card shows the campaign's sketch, its name, how many missions are completed and which
 * mission comes next. The numbers are the active profile's own, read from the progress
 * file the game writes.
 */
final class CampaignPage {
    /** One of the four campaigns. */
    static final class Campaign {
        /** Key in the progress file. */
        final String key;
        /** Prefix of the mission-name strings ("human_3_name"). */
        final String code;
        /** String key of the card's caption. */
        final String captionKey;
        final String sketch;
        final int missions;
        /** Missions completed by the active profile. */
        int completed;

        Campaign(String key, String code, String captionKey, String sketch, int missions) {
            this.key = key;
            this.code = code;
            this.captionKey = captionKey;
            this.sketch = sketch;
            this.missions = missions;
        }
    }

    /** In the order of the game's own campaign screen: Orc then Human, base game then expansion. */
    final Campaign[] campaigns = {
        new Campaign("todOrc", "orc", "orc_campaign", "sketch_orc2", 14),
        new Campaign("todHuman", "human", "human_campaign", "sketch_human1", 14),
        new Campaign("bdpOrc", "xorc", "orc_campaign", "sketch_orc1", 12),
        new Campaign("bdpHuman", "xhuman", "human_campaign", "sketch_human2", 12),
    };

    private static final String[] ROMAN = {
        "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII", "XIII", "XIV",
    };

    // Layout, logical units.
    private static final float CONTENT_X = 816f, CONTENT_TOP = 264f, COLUMN_PITCH = 248f;
    private static final float CARD_W = 696f, CARD_PITCH = 744f, GROUP_W = 1440f, GROUP_PITCH = 1488f;
    private static final float LOGO_W = 1104f, LOGO_H = LOGO_W * 513f / 1260f;
    private static final float CARD_FOOT = 282f, SKETCH_ASPECT = 0.6f;
    private static final float CAP_BODY = 44f, CAP_SMALL = 30f;

    private static final int PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E, PAPER = 0xFFD8C8A4;
    private static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, GOLD = 0xFFF0D250;
    private static final int TRACK = 0xFF50463A;

    private final Bitmap[] logos = new Bitmap[2];
    private final Bitmap[] sketches = new Bitmap[4];
    private boolean loaded;
    /** Asks for the page to be drawn again; may be called from any thread. */
    Runnable redraw;

    /** Read the active profile's progress. A missing or unreadable file means none. */
    void readProgress(Context c, Profiles.Profile profile) {
        for (Campaign k : campaigns) {
            k.completed = 0;
        }
        if (profile == null) {
            return;
        }
        final String text = GameLaunch.readText(
            new File(Profiles.directory(c, profile), "wc2/GlobalSave.json"));
        if (text == null) {
            return;
        }
        try {
            final JSONObject done = new JSONObject(text).getJSONObject("missionsCompleted");
            for (Campaign k : campaigns) {
                k.completed = Math.max(0, Math.min(k.missions, done.optInt(k.key, 0)));
            }
        } catch (Exception e) {
            // Unreadable: shown as no progress.
        }
    }

    int completedTotal() {
        int n = 0;
        for (Campaign k : campaigns) {
            n += k.completed;
        }
        return n;
    }

    int missionsTotal() {
        int n = 0;
        for (Campaign k : campaigns) {
            n += k.missions;
        }
        return n;
    }

    private float cardTop() {
        return CONTENT_TOP + LOGO_H + 24f;
    }

    /** The cards run down to where the action bar's buttons would end: there is none on this page. */
    private float cardHeight(SkinKit kit) {
        return kit.height - 264f + 120f - cardTop();
    }

    /** The campaign whose card is at a point, or null. */
    Campaign cardAt(float x, float y, SkinKit kit) {
        final float top = cardTop();
        if (y < top || y >= top + cardHeight(kit)) {
            return null;
        }
        for (int i = 0; i < campaigns.length; ++i) {
            final float cx = CONTENT_X + i * CARD_PITCH;
            if (x >= cx && x < cx + CARD_W) {
                return campaigns[i];
            }
        }
        return null;
    }

    void draw(Canvas c, SkinKit kit) {
        if (!loaded) {
            // Cut out of the atlas off the main thread: the page is drawn without its pictures
            // first and again when they are there.
            loaded = true;
            final SkinKit k = kit;
            final int lw = Math.round(LOGO_W * kit.unit), lh = Math.round(LOGO_H * kit.unit);
            final int sw = Math.round(648f * kit.unit), sh = Math.round(1080f * kit.unit);
            new Thread(new Runnable() {
                @Override
                public void run() {
                    final long began = android.os.SystemClock.uptimeMillis();
                    logos[0] = k.atlasFrame("Modern_Graphics", "logo_tod", lw, lh);
                    logos[1] = k.atlasFrame("Modern_Graphics", "logo_btdp", lw, lh);
                    for (int i = 0; i < campaigns.length; ++i) {
                        sketches[i] = k.atlasFrame("Modern_Graphics", campaigns[i].sketch, sw, sh);
                    }
                    // CAMPAIGN-PAGE-FIRST-OPEN-SLOW: the first open's cost, beside the tap's own log.
                    android.util.Log.i("WargusHD", "LAUNCHER campaign pictures in "
                        + (android.os.SystemClock.uptimeMillis() - began) + " ms");
                    if (redraw != null) {
                        redraw.run();
                    }
                }
            }, "campaign-pictures").start();
        }

        for (int g = 0; g < 2; ++g) {
            final float gx = CONTENT_X + g * GROUP_PITCH;
            kit.picture(c, logos[g], gx + (GROUP_W - LOGO_W) / 2f, CONTENT_TOP, LOGO_W, LOGO_H);
        }

        final float top = cardTop(), ch = cardHeight(kit);
        final float paperH = ch - CARD_FOOT;
        // The sketch is fitted into the paper, never stretched.
        final float sw = Math.min((paperH - 24f) * SKETCH_ASPECT, CARD_W - 48f);
        final float sh = sw / SKETCH_ASPECT;
        for (int i = 0; i < campaigns.length; ++i) {
            final Campaign k = campaigns[i];
            final float x = CONTENT_X + i * CARD_PITCH;
            kit.fill(c, x, top, CARD_W, ch, PANEL_FILL, 18f);
            kit.outline(c, x, top, CARD_W, ch, LINE, 18f);
            kit.fill(c, x + 12f, top + 12f, CARD_W - 24f, paperH, PAPER, 10f);
            kit.picture(c, sketches[i], x + (CARD_W - sw) / 2f, top + 12f + (paperH - sh) / 2f, sw, sh);

            final float ty = top + paperH + 60f;
            kit.textCentreTop(c, kit.string(k.captionKey, k.code), x, ty, CARD_W, CAP_BODY, INK, true);

            final float barW = CARD_W - 310f;
            kit.fill(c, x + 60f, ty + 92f, barW, 18f, TRACK, 9f);
            if (k.completed > 0) {
                kit.fill(c, x + 60f, ty + 92f, barW * k.completed / k.missions, 18f, GOLD, 9f);
            }
            kit.textTopRight(c, k.completed + " / " + k.missions, x + CARD_W - 60f, ty + 86f,
                             CAP_SMALL, INK_MUTED, true);
            if (k.completed < k.missions) {
                final int next = k.completed + 1;
                final String name = kit.string(k.code + "_" + next + "_name", "");
                kit.textTop(c, kit.fit(ROMAN[next - 1] + ". " + name, CAP_SMALL, CARD_W - 120f),
                            x + 60f, ty + 152f, CAP_SMALL, INK, true);
            }
        }
    }
}
