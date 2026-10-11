package net.wargushd.game;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Shader;
import android.view.MotionEvent;
import android.view.View;

import org.json.JSONArray;
import org.json.JSONObject;

/**
 * The Victory / Defeat window, shown above the game after a game has reached its outcome.
 * It is filled from the last game of the result record the engine has just written and
 * answers with what the player chose.
 */
final class ResultView extends View {
    /** A side's name is fitted into its 460-wide column less this much, so that a long one
     *  ("Stormreaver Clan") does not end right at the relation word 20 further on. */
    private static final float NAME_FIT = 460f - 30f;

    /** What the window was closed with. */
    interface Listener {
        /** One of the game activity's commands: go on, restart, or leave. */
        void onCommand(int command);
    }

    private static final String[] ROMAN = {
        "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII", "XIII", "XIV",
    };

    // Layout, logical units.
    private static final float WIN_X = 240f, WIN_Y = 120f, WIN_W = 3360f, WIN_H_FULL = 2160f, INSET = 72f;
    private static final float TILE_W = 492f, TILE_H = 300f, TILE_Y = 372f, TILE_GAP = 72f;
    private static final float LINE_PITCH = 82.5f, TABLE_Y = 1200f, TABLE_H_FULL = 720f;
    private static final float ROW_H = 132f, ROW_PITCH = 138f, BUTTON_W = 720f, BUTTON_H = 120f;
    private static final float CAP_TITLE = 60f, CAP_BODY = 44f, CAP_MID = 36f, CAP_SMALL = 30f;

    private static final int SHADE = 0xAA000000, PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E;
    private static final int WELL = 0x78000000, ROW_FAINT = 0x10FFFFFF, ROW_OWN = 0x36F0D250;
    private static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, GOLD = 0xFFF0D250;
    private static final int WARN = 0xFFFF9680, SWATCH_LINE = 0xFFECE6D6;
    private static final int ADDED = 0xFF8CD696;

    private static final String[] COLUMNS = {
        "victorydefeat_units", "victorydefeat_buildings", "victorydefeat_gold", "victorydefeat_lumber",
        "victorydefeat_oil", "victorydefeat_kills", "victorydefeat_razings",
    };
    private static final String[] FIELDS = { "units", "buildings", "gold", "wood", "oil", "kills", "razings" };

    private final SkinKit kit;
    private final Listener listener;
    private Bitmap buttonPlain, buttonMarked;

    private boolean victory;
    /** A campaign mission rather than a scenario: which, and of which campaign. */
    private boolean campaign, expansion, orcSide;
    /** A game continued from a save: there is nothing to play again or go on to. */
    private boolean loaded;
    private int mission;
    private String code = "";
    private String map = "";
    private long score;
    private int rank;
    private boolean orc;
    private int opponents;
    private JSONArray players = new JSONArray();
    /** The table's rows: the own line first, the others in the game's order. */
    private final java.util.List<JSONObject> rows = new java.util.ArrayList<>();
    /** How far the table's rows are dragged up, logical units; and the drag in progress. */
    private float scroll, downY, downScroll;
    private boolean inTable, dragging;
    /** The game's progress file as text, or null. */
    private final String progressText;
    /** Missions of this campaign completed after this game, and its length; -1 when not known. */
    private int completed = -1, length;
    /** The profile as it stood before this game, or null. */
    private Profiles.Profile before;
    /** The profile's own line of this game. */
    private JSONObject own = new JSONObject();
    private Bitmap face;

    /** A game of a network room (the record's mode lan_host or lan_join): the LAN form. */
    private boolean lan;
    /** LAN: the game ran on this device alone, the one case in which it can be played again. */
    private boolean again;
    /** LAN: the teams among the rows; the winning team, or -1 when the record does not tell. */
    private int teams, winTeam = -1;
    /** LAN: the game's length in the engine's cycles. */
    private long cycles;

    /** The game's picture as it ended; drawn under the scrim. Null: whatever the engine shows. */
    private android.graphics.Bitmap behind;
    private final android.graphics.Paint behindPaint =
        new android.graphics.Paint(android.graphics.Paint.FILTER_BITMAP_FLAG);

    ResultView(Context c, SkinKit kit, String record, String card, String progress,
               android.graphics.Bitmap behind, Listener listener) {
        super(c);
        this.behind = behind;
        // With the system's animations switched off the window appears at once.
        animated = android.provider.Settings.Global.getFloat(
            c.getContentResolver(), android.provider.Settings.Global.ANIMATOR_DURATION_SCALE, 1f) > 0f;
        // Developer switch (debuggable builds): an empty file "slow_arrival" in the app's files
        // directory runs the arrival ten times slower, to be looked at.
        slowdown = ((c.getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0
                    && new java.io.File(c.getFilesDir(), "slow_arrival").exists()) ? 10L : 1L;
        this.kit = kit;
        this.listener = listener;
        this.progressText = progress;
        try {
            if (card != null) {
                before = Profiles.Profile.fromJson(new JSONObject(card));
            }
        } catch (Exception e) {
            // No profile box.
        }
        try {
            final JSONObject whole = new JSONObject(record);
            campaign = "campaign".equals(whole.optString("mode"));
            loaded = "load".equals(whole.optString("mode"));
            final JSONArray games = whole.getJSONArray("games");
            final JSONObject game = games.getJSONObject(games.length() - 1);
            // The profile came with the launch; the session's earlier games (Play Again, a
            // campaign played on) belong to "before this game" too.
            for (int i = 0; before != null && i < games.length() - 1; ++i) {
                final JSONObject earlier = games.getJSONObject(i);
                ++before.games;
                final String outcome = earlier.optString("outcome");
                if ("victory".equals(outcome)) {
                    ++before.victories;
                } else if ("defeat".equals(outcome) || "draw".equals(outcome)) {
                    ++before.defeats;
                }
                final JSONArray players = earlier.optJSONArray("players");
                for (int k = 0; players != null && k < players.length(); ++k) {
                    final JSONObject who = players.getJSONObject(k);
                    if (who.optBoolean("me", false)) {
                        before.units += who.optLong("units", 0L);
                        before.buildings += who.optLong("buildings", 0L);
                        before.kills += who.optLong("kills", 0L);
                        before.razings += who.optLong("razings", 0L);
                        before.score += earlier.optLong("score", who.optLong("score", 0L));
                    }
                }
                final int earlierRank = earlier.optInt("rank_index", 0);
                if (earlierRank > before.bestRank) {
                    before.bestRank = earlierRank;
                    before.bestRankOrc = "orc".equals(earlier.optString("race"));
                }
            }
            victory = "victory".equals(game.optString("outcome"));
            final String path = game.optString("map", "");
            mission = game.optInt("mission", 0);
            // campaigns/<human|orc|human-exp|orc-exp>/…: the strings name the four "human", "orc",
            // "xhuman", "xorc".
            final String[] part = path.split("/");
            if (campaign && part.length >= 2) {
                expansion = part[1].endsWith("-exp");
                orcSide = part[1].startsWith("orc");
                code = (expansion ? "x" : "") + (orcSide ? "orc" : "human");
            }
            final String base = path.substring(path.lastIndexOf('/') + 1);
            // A map's file name without its extension; a save's without ".sav" and what follows.
            final int cut = loaded ? base.lastIndexOf(".sav") : base.lastIndexOf('.');
            map = (cut > 0) ? base.substring(0, cut) : base;
            players = game.optJSONArray("players");
            if (players == null) {
                players = new JSONArray();
            }
            for (int i = 0; i < players.length(); ++i) {
                final JSONObject who = players.getJSONObject(i);
                if (who.optBoolean("me", false)) {
                    own = who;
                    score = who.optLong("score", 0L);
                    rows.add(0, who);
                } else {
                    ++opponents;
                    rows.add(who);
                }
            }
            if (campaign && progressText != null) {
                // The progress file names the four campaigns todHuman, todOrc, bdpHuman, bdpOrc.
                final String key = (expansion ? "bdp" : "tod") + (orcSide ? "Orc" : "Human");
                length = expansion ? 12 : 14;
                completed = new JSONObject(progressText).getJSONObject("missionsCompleted")
                    .optInt(key, -1);
            }
            // The game's own total (with the victory bonus) and rank, when the record has them.
            score = game.optLong("score", score);
            rank = game.optInt("rank_index", 0);
            orc = "orc".equals(game.optString("race"));
            final String mode = whole.optString("mode");
            if ("lan_host".equals(mode) || "lan_join".equals(mode)) {
                again = whole.optBoolean("solo", false);
                winTeam = game.optInt("win_team", -1);
                lanMapW = game.optInt("map_w", 0);
                lanMapH = game.optInt("map_h", 0);
                lanTerrain = game.optInt("terrain", -1);
                cycles = game.optLong("cycles", 0L);
                groupByTeam();
                lan = true;
            }
        } catch (Exception e) {
            // An unreadable record leaves the window with its frame and its way out.
        }
    }

    // The window's arrival (design 29.3), in milliseconds from its first frame: the scrim
    // 0-200; the window 60-360 (opacity, scale 92 -> 100 %, rising 48 units, decelerating); the
    // numbers count up 300-900 and the table's rows appear 60 apart; the buttons answer from 360.
    private static final long SCRIM_MS = 200L, WINDOW_FROM = 60L, WINDOW_MS = 300L;
    private static final long COUNT_FROM = 300L, COUNT_MS = 600L, ROW_STEP = 60L, BUTTONS_FROM = 360L;
    private final boolean animated;
    private final long slowdown;
    private long firstFrame = -1L;
    /** Milliseconds since the first frame; far beyond the arrival's end when not animated. */
    private long clock = Long.MAX_VALUE / 4;

    private static float part(long t, long from, long length) {
        return Math.max(0f, Math.min(1f, (t - from) / (float) length));
    }

    /** A number on its way up to its value while the numbers count. */
    private long counted(long value) {
        return Math.round(value * (double) part(clock, COUNT_FROM, COUNT_MS));
    }

    /** One line of the profile box: a name, the total after this game, what the game added. */
    private void delta(Canvas c, float px, float pw, int row, String name, long total, long added) {
        final float y = TILE_Y + 252f + 62f * row;
        final long shown = counted(added);
        kit.textTop(c, name, px + 48f, y, CAP_MID, INK_MUTED, true);
        kit.textTopRight(c, grouped(total - added + shown), px + 900f, y, CAP_MID, INK, true);
        kit.textTopRight(c, "+" + grouped(shown), px + pw - 48f, y, CAP_MID, ADDED, true);
    }

    /** A number with its thousands set apart by a space. */
    private static String grouped(long n) {
        final String digits = String.valueOf(Math.abs(n));
        final StringBuilder s = new StringBuilder();
        for (int i = 0; i < digits.length(); ++i) {
            if (i > 0 && (digits.length() - i) % 3 == 0) {
                s.append(' ');
            }
            s.append(digits.charAt(i));
        }
        return (n < 0 ? "-" : "") + s;
    }

    private float ix() { return WIN_X + INSET; }
    private float iw() { return WIN_W - INSET * 2f; }
    /**
     * The owner's choice of 2026-10-06 (option 2, "fitted"): the window keeps its 120 units
     * above and below inside the view it is shown in, which is lower than the design's 2400
     * where the system's strips are not used; the room is taken from the empty part between
     * the table and the buttons, and from the table only when that is used up.
     */
    private float winH() { return Math.min(WIN_H_FULL, kit.height - 2f * WIN_Y); }
    private float tableH() { return Math.min(TABLE_H_FULL, buttonY() - 48f - TABLE_Y); }
    private float buttonY() { return WIN_Y + winH() - INSET - BUTTON_H; }
    private float rowsTop() { return TABLE_Y + 96f; }
    private float rowsBottom() { return TABLE_Y + tableH() - 12f; }

    private float maxScroll() {
        return Math.max(0f, rows.size() * ROW_PITCH - 6f - (rowsBottom() - rowsTop()));
    }

    private void well(Canvas c, float x, float y, float w, float h) {
        kit.fill(c, x, y, w, h, WELL, 14f);
        kit.outline(c, x, y, w, h, LINE, 14f);
    }

    @Override
    protected void onDraw(Canvas c) {
        if (lan) {
            drawLan(c);
            return;
        }
        kit.begin(getWidth(), getHeight());
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        if (behind != null) {
            c.drawBitmap(behind, null, new android.graphics.Rect(0, 0, getWidth(), getHeight()), behindPaint);
        }
        if (animated) {
            final long now = android.os.SystemClock.uptimeMillis();
            if (firstFrame < 0L) {
                firstFrame = now;
            }
            clock = (now - firstFrame) / slowdown;
        }
        final int scrim = Math.round((SHADE >>> 24) * part(clock, 0L, SCRIM_MS));
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, (scrim << 24) | (SHADE & 0xFFFFFF), 0f);
        // The window on its way in: decelerating towards its place.
        final float way = part(clock, WINDOW_FROM, WINDOW_MS);
        final float eased = 1f - (1f - way) * (1f - way);
        final boolean arriving = eased < 1f;
        if (arriving) {
            c.saveLayerAlpha(null, Math.round(255f * eased));
            final float scale = 0.92f + 0.08f * eased;
            c.translate(0f, (1f - eased) * 48f * kit.unit);
            c.scale(scale, scale, (WIN_X + WIN_W / 2f) * kit.unit, (WIN_Y + winH() / 2f) * kit.unit);
        }
        kit.fill(c, WIN_X, WIN_Y, WIN_W, winH(), PANEL_FILL, 18f);
        kit.outline(c, WIN_X, WIN_Y, WIN_W, winH(), LINE, 18f);

        final float ix = ix(), iw = iw();
        kit.textTop(c, kit.string(victory ? "victory" : "defeat", victory ? "Victory!" : "Defeat"),
                    ix, 192f, CAP_TITLE, victory ? GOLD : WARN, true);
        final String who = kit.own(opponents == 1 ? "{0} opponent" : "{0} opponents", opponents);
        final String missionName = (mission >= 1 && mission <= ROMAN.length)
            ? ROMAN[mission - 1] + ". " + kit.string(code + "_" + mission + "_name", "") : map;
        if (campaign) {
            kit.textTop(c, kit.string(expansion ? "campaign_expansion" : "campaign_base",
                                      expansion ? "Expansion" : "Campaign")
                        + " - " + kit.string(orcSide ? "orc" : "human", orcSide ? "Orc" : "Human")
                        + " - " + missionName, ix, 288f, CAP_SMALL, INK_MUTED, true);
        } else {
            kit.textTop(c, map + " - " + kit.own(loaded ? "Saved game" : "Custom Scenario") + " - " + who, ix, 288f,
                        CAP_SMALL, INK_MUTED, true);
        }

        // Three tiles: outcome, rank, score. The game does not report a rank.
        final String[] names = {
            kit.string("victorydefeat_outcome", "Outcome"), kit.string("victorydefeat_rank", "Rank"),
            kit.string("victorydefeat_score", "Score"),
        };
        final String[] values = {
            kit.string(victory ? "rank_txt_41" : "rank_txt_40", victory ? "Won" : "Lost"),
            (rank > 0) ? kit.string(Profiles.rankKey(rank, orc), "-") : "-",
            grouped(counted(score)),
        };
        for (int i = 0; i < 3; ++i) {
            final float gx = ix + (TILE_W + TILE_GAP) * i;
            well(c, gx, TILE_Y, TILE_W, TILE_H);
            kit.textCentreTop(c, names[i], gx + 30f, TILE_Y + 66f, TILE_W - 60f, CAP_MID, INK_MUTED, true);
            kit.textCentreTop(c, kit.fit(values[i], CAP_TITLE, TILE_W - 60f), gx + 30f, TILE_Y + 162f,
                              TILE_W - 60f, CAP_TITLE, victory ? GOLD : INK, true);
        }
        if (campaign) {
            kit.textTop(c, kit.fit(missionName, CAP_BODY, 1620f), ix, 744f, CAP_BODY, GOLD, true);
            if (completed >= 0) {
                kit.textTop(c, kit.own(victory ? "Progress saved" : "Progress unchanged") + ": " + completed + " / "
                            + length, ix, 744f + LINE_PITCH, CAP_MID, INK_MUTED, true);
                if (victory && completed < length && completed < ROMAN.length) {
                    final String next = ROMAN[completed] + ". "
                        + kit.string(code + "_" + (completed + 1) + "_name", "");
                    kit.textTop(c, kit.fit(kit.own("Next") + ": " + next, CAP_MID, 1620f), ix, 744f + LINE_PITCH * 2f,
                                CAP_MID, INK, true);
                }
            }
        } else {
            kit.textTop(c, map, ix, 744f, CAP_BODY, GOLD, true);
            kit.textTop(c, who, ix, 744f + LINE_PITCH, CAP_MID, INK_MUTED, true);
        }

        // The profile after this game: what it had, with what this game added.
        final float px = ix + 1620f + 72f, pw = iw - 1620f - 72f;
        well(c, px, TILE_Y, pw, 780f);
        if (before != null) {
            if (face == null) {
                face = Portraits.tinted(kit, before.portrait, Profiles.COLORS[before.color]);
            }
            kit.picture(c, face, px + 48f, TILE_Y + 36f, 217.9f, 180f);
            kit.textTop(c, kit.fit(before.name, CAP_BODY, pw - 360f), px + 310f, TILE_Y + 60f, CAP_BODY,
                        GOLD, true);
            kit.textTop(c, kit.own("Profile after this game"), px + 310f, TILE_Y + 60f + LINE_PITCH, CAP_SMALL,
                        INK_MUTED, true);
            final long u = own.optLong("units", 0L), b = own.optLong("buildings", 0L);
            final long k = own.optLong("kills", 0L), z = own.optLong("razings", 0L);
            delta(c, px, pw, 0, kit.own("Games played"), before.games + 1, 1);
            if (victory) {
                delta(c, px, pw, 1, kit.own("Victories"), before.victories + 1, 1);
            } else {
                delta(c, px, pw, 1, kit.own("Defeats"), before.defeats + 1, 1);
            }
            delta(c, px, pw, 2, kit.string("victorydefeat_units", "Units"), before.units + u, u);
            delta(c, px, pw, 3, kit.string("victorydefeat_buildings", "Buildings"), before.buildings + b, b);
            delta(c, px, pw, 4, kit.string("victorydefeat_kills", "Kills"), before.kills + k, k);
            delta(c, px, pw, 5, kit.string("victorydefeat_razings", "Razings"), before.razings + z, z);
            delta(c, px, pw, 6, kit.string("victorydefeat_score", "Score"), before.score + score, score);
            // The best rank so far; marked when this game raised it.
            final float y = TILE_Y + 252f + 62f * 7;
            final boolean raised = rank > before.bestRank;
            final int best = raised ? rank : before.bestRank;
            final boolean bestOrc = raised ? orc : before.bestRankOrc;
            kit.textTop(c, kit.string("victorydefeat_rank", "Rank"), px + 48f, y, CAP_MID, INK_MUTED, true);
            kit.textTopRight(c, best > 0 ? kit.string(Profiles.rankKey(best, bestOrc), "-") : "-",
                             px + 900f, y, CAP_MID, GOLD, true);
            if (raised) {
                kit.textTopRight(c, "new", px + pw - 48f, y, CAP_MID, ADDED, true);
            }
        }

        // Every player of the game.
        well(c, ix, TABLE_Y, iw, tableH());
        final float nx = ix + 24f + 860f;
        final float cw = (ix + iw - 24f - 36f - nx) / COLUMNS.length;
        for (int k = 0; k < COLUMNS.length; ++k) {
            // The game's texts for some columns end in a colon (they are labels there); a
            // heading too wide for its column is made smaller.
            String head = kit.string(COLUMNS[k], "");
            if (head.endsWith(":") || head.endsWith("：")) {
                head = head.substring(0, head.length() - 1).trim();
            }
            kit.textTopRight(c, kit.fit(head, CAP_SMALL, cw - 36f), nx + cw * k + cw - 24f, TABLE_Y + 36f,
                             CAP_SMALL, INK_MUTED, true);
        }
        // The table reaches down to the buttons (four rows on a tablet); with more players the
        // rows are dragged inside it, and then a count "shown / all" stands in the heading and
        // the lower edge fades while rows wait below (owner decision 2026-10-10).
        final int whole = Math.max(1, (int) ((rowsBottom() - rowsTop() + 6f) / ROW_PITCH));
        if (rows.size() > whole) {
            kit.textTopRight(c, whole + " / " + rows.size(), ix + 24f + 600f, TABLE_Y + 36f,
                             CAP_SMALL, INK_MUTED, true);
        }
        c.save();
        c.clipRect(kit.rect(ix + 24f, rowsTop(), iw - 48f, rowsBottom() - rowsTop()));
        for (int i = 0; i < rows.size(); ++i) {
            final JSONObject p = rows.get(i);
            final boolean me = p.optBoolean("me", false);
            final float rx = ix + 24f, ry = rowsTop() + ROW_PITCH * i - scroll, rw = iw - 48f;
            if (ry + ROW_H < rowsTop() || ry > rowsBottom() || clock < COUNT_FROM + ROW_STEP * i) {
                continue;
            }
            kit.fill(c, rx, ry, rw, ROW_H, me ? ROW_OWN : ROW_FAINT, 10f);
            final int color = p.optInt("color", -1);
            if (color >= 0 && color < Profiles.COLORS.length) {
                kit.fill(c, rx + 24f, ry + 30f, 72f, 72f, Profiles.COLORS[color], 12f);
                kit.outline(c, rx + 24f, ry + 30f, 72f, 72f, SWATCH_LINE, 12f);
            }
            kit.textMiddle(c, kit.fit(p.optString("name", ""), CAP_BODY, NAME_FIT), rx + 120f, ry, ROW_H,
                           CAP_BODY, me ? GOLD : INK, true);
            // The relation in the game's own words; it has none for a neutral player.
            final String relation = p.optString("relation", me ? "you" : "");
            if (relation.equals("you") || relation.equals("enemy") || relation.equals("ally")) {
                kit.textMiddle(c, kit.string("clan_relation_" + relation, ""), rx + 600f, ry, ROW_H,
                               CAP_SMALL, me ? INK : INK_MUTED, true);
            }
            for (int k = 0; k < FIELDS.length; ++k) {
                kit.textMiddleRight(c, grouped(counted(p.optLong(FIELDS[k], 0L))), nx + cw * k + cw - 24f, ry,
                                    ROW_H, CAP_MID, INK, true);
            }
        }
        c.restore();
        if (scroll < maxScroll() - 1f) {
            final RectF edge = kit.rect(ix + 24f, rowsBottom() - 84f, iw - 48f, 84f);
            final Paint shade = new Paint();
            shade.setShader(new LinearGradient(0f, edge.top, 0f, edge.bottom, 0x00100C08, 0xF0100C08,
                                               Shader.TileMode.CLAMP));
            c.drawRect(edge, shade);
        }

        // Actions: leaving at the left, going on at the right. In a campaign the game goes on
        // or runs the mission again; a scenario is played again with the same map and settings.
        final float by = buttonY();
        kit.nine(c, buttonPlain, ix, by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.own(campaign ? "Mission List" : "Back to Launcher"), ix, by, BUTTON_W, BUTTON_H,
                       CAP_BODY, INK, true);
        final float ax = ix + iw - BUTTON_W;
        if (campaign) {
            kit.nine(c, buttonMarked, ax, by, BUTTON_W, BUTTON_H, 14);
            kit.textCentre(c, victory ? kit.string("common_continue", "Continue")
                                      : kit.string("defeat_btn", "Restart"),
                           ax, by, BUTTON_W, BUTTON_H, CAP_BODY, GOLD, true);
        } else if (!loaded) {
            kit.nine(c, buttonMarked, ax, by, BUTTON_W, BUTTON_H, 14);
            kit.textCentre(c, kit.own("Play Again"), ax, by, BUTTON_W, BUTTON_H, CAP_BODY, GOLD, true);
        }
        if (arriving) {
            c.restore();
        }
        if (clock < COUNT_FROM + COUNT_MS + ROW_STEP * rows.size()) {
            postInvalidateOnAnimation();
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (lan) {
            return touchLan(e);
        }
        final float ty = e.getY() / kit.unit;
        if (e.getAction() == MotionEvent.ACTION_DOWN) {
            downY = ty;
            downScroll = scroll;
            dragging = false;
            inTable = ty >= rowsTop() && ty < rowsBottom();
        } else if (e.getAction() == MotionEvent.ACTION_MOVE && inTable) {
            if (Math.abs(ty - downY) > 16f) {
                dragging = true;
            }
            if (dragging) {
                scroll = Math.max(0f, Math.min(maxScroll(), downScroll - (ty - downY)));
                invalidate();
            }
        }
        if (e.getAction() == MotionEvent.ACTION_UP && !dragging && clock >= BUTTONS_FROM) {
            final float x = e.getX() / kit.unit, y = ty;
            if (y >= buttonY() && y < buttonY() + BUTTON_H) {
                final float ax = ix() + iw() - BUTTON_W;
                if (x >= ix() && x < ix() + BUTTON_W) {
                    listener.onCommand(WargusHDActivity.CMD_EXIT_BACK);
                } else if (!loaded && x >= ax && x < ax + BUTTON_W) {
                    listener.onCommand(!campaign ? WargusHDActivity.CMD_PLAY_AGAIN
                                       : victory ? WargusHDActivity.CMD_CONTINUE
                                                 : WargusHDActivity.CMD_RESTART);
                }
            }
        }
        // Nothing under the window reacts while it is shown.
        return true;
    }

    // The LAN form (design 38.10 L18, board result_a): the same window with lower tiles, the
    // winning team's line, the profile box closer set, and a table of eight rows of 96 grouped
    // by team with a Team column. It is drawn and touched on its own path; the form of every
    // other game is not changed by it.
    private static final float LAN_TILE_H = 240f, LAN_SUM_Y = 660f, LAN_PROFILE_H = 684f;
    private static final float LAN_DELTA_Y = 606f, LAN_DELTA_PITCH = 54f;
    private static final float LAN_TABLE_Y = 1104f, LAN_TABLE_H_FULL = 924f;
    private static final float LAN_ROW_H = 96f, LAN_ROW_PITCH = 102f;
    /** Widths of a row's name and relation, and of the first line under the tiles. */
    private static final float LAN_NAME_W = 330f, LAN_RELATION_W = 200f, LAN_SUM_W = 1620f;
    /** The LAN game's map: size in tiles and terrain index, 0 / -1 when the record has none. */
    private int lanMapW, lanMapH, lanTerrain = -1;
    /** The engine's cycles in a second of a game at its normal speed (src/include/settings.h). */
    private static final int CYCLES_PER_SECOND = 30;

    /** A row's team as the room gave it, or -1 when the record has none for it. */
    private static int team(JSONObject p) {
        return p.optInt("team", -1);
    }

    /**
     * The rows in the order of their teams, the own line first within its team; rows without
     * a team last. The teams are counted on the way.
     */
    private void groupByTeam() {
        final java.util.List<JSONObject> sorted = new java.util.ArrayList<>();
        final java.util.TreeSet<Integer> seen = new java.util.TreeSet<>();
        for (JSONObject p : rows) {
            if (team(p) >= 0) {
                seen.add(team(p));
            }
        }
        // The own line stands first among the rows already, so each team keeps it first.
        for (int t : seen) {
            for (JSONObject p : rows) {
                if (team(p) == t) {
                    sorted.add(p);
                }
            }
        }
        for (JSONObject p : rows) {
            if (team(p) < 0) {
                sorted.add(p);
            }
        }
        rows.clear();
        rows.addAll(sorted);
        teams = seen.size();
    }

    /** The game's length as minutes and seconds (hours before them when it has any), or "". */
    private String length() {
        if (cycles <= 0L) {
            return "";
        }
        final long seconds = cycles / CYCLES_PER_SECOND;
        final long h = seconds / 3600L, m = seconds / 60L % 60L, s = seconds % 60L;
        return ((h > 0L) ? h + ":" + (m < 10L ? "0" : "") + m : String.valueOf(m))
            + ":" + (s < 10L ? "0" : "") + s;
    }

    // The table gives way to the buttons in a lower view, as in the other form.
    private float lanTableH() { return Math.min(LAN_TABLE_H_FULL, buttonY() - 60f - LAN_TABLE_Y); }
    private float lanRowsTop() { return LAN_TABLE_Y + 84f; }
    private float lanRowsBottom() { return LAN_TABLE_Y + lanTableH() - 30f; }

    private float lanMaxScroll() {
        return Math.max(0f, rows.size() * LAN_ROW_PITCH - 6f - (lanRowsBottom() - lanRowsTop()));
    }

    /** One line of the LAN form's profile box; as delta, on that form's closer lines. */
    private void lanDelta(Canvas c, float px, float pw, int row, String name, long total, long added) {
        final float y = LAN_DELTA_Y + LAN_DELTA_PITCH * row;
        final long shown = counted(added);
        kit.textTop(c, name, px + 48f, y, CAP_MID, INK_MUTED, true);
        kit.textTopRight(c, grouped(total - added + shown), px + 900f, y, CAP_MID, INK, true);
        kit.textTopRight(c, "+" + grouped(shown), px + pw - 48f, y, CAP_MID, ADDED, true);
    }

    /**
     * Where a mark for a player who left the game or lost the connection is to be drawn on his
     * row. Its look is not decided (design 38.12, open point 4), so nothing is drawn and such
     * a player is an ordinary row. quit is the record's word for how he went - "left", "lost",
     * "never_started" - or "" for a player who stayed.
     */
    private void leaverMark(Canvas c, String quit, float rx, float ry, float rw) {
    }

    private void drawLan(Canvas c) {
        kit.begin(getWidth(), getHeight());
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        if (behind != null) {
            c.drawBitmap(behind, null, new android.graphics.Rect(0, 0, getWidth(), getHeight()), behindPaint);
        }
        if (animated) {
            final long now = android.os.SystemClock.uptimeMillis();
            if (firstFrame < 0L) {
                firstFrame = now;
            }
            clock = (now - firstFrame) / slowdown;
        }
        final int scrim = Math.round((SHADE >>> 24) * part(clock, 0L, SCRIM_MS));
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, (scrim << 24) | (SHADE & 0xFFFFFF), 0f);
        // The window on its way in, as in the other form.
        final float way = part(clock, WINDOW_FROM, WINDOW_MS);
        final float eased = 1f - (1f - way) * (1f - way);
        final boolean arriving = eased < 1f;
        if (arriving) {
            c.saveLayerAlpha(null, Math.round(255f * eased));
            final float scale = 0.92f + 0.08f * eased;
            c.translate(0f, (1f - eased) * 48f * kit.unit);
            c.scale(scale, scale, (WIN_X + WIN_W / 2f) * kit.unit, (WIN_Y + winH() / 2f) * kit.unit);
        }
        kit.fill(c, WIN_X, WIN_Y, WIN_W, winH(), PANEL_FILL, 18f);
        kit.outline(c, WIN_X, WIN_Y, WIN_W, winH(), LINE, 18f);

        final float ix = ix(), iw = iw();
        kit.textTop(c, kit.string(victory ? "victory" : "defeat", victory ? "Victory!" : "Defeat"),
                    ix, 192f, CAP_TITLE, victory ? GOLD : WARN, true);
        // The players and their teams; the teams are left out when the record names none.
        final String count = (teams > 0) ? kit.own("{0} players, {1} teams", rows.size(), teams)
                                         : kit.own("{0} players", rows.size());
        final String length = length();
        kit.textTop(c, map + " - " + kit.own("LAN") + " - " + count + (length.isEmpty() ? "" : " - " + length),
                    ix, 288f, CAP_SMALL, INK_MUTED, true);

        // Three tiles: outcome, rank, score.
        final String[] names = {
            kit.string("victorydefeat_outcome", "Outcome"), kit.string("victorydefeat_rank", "Rank"),
            kit.string("victorydefeat_score", "Score"),
        };
        final String[] values = {
            kit.string(victory ? "rank_txt_41" : "rank_txt_40", victory ? "Won" : "Lost"),
            (rank > 0) ? kit.string(Profiles.rankKey(rank, orc), "-") : "-",
            grouped(counted(score)),
        };
        for (int i = 0; i < 3; ++i) {
            final float gx = ix + (TILE_W + TILE_GAP) * i;
            well(c, gx, TILE_Y, TILE_W, LAN_TILE_H);
            kit.textCentreTop(c, names[i], gx + 30f, TILE_Y + 42f, TILE_W - 60f, CAP_MID, INK_MUTED, true);
            kit.textCentreTop(c, kit.fit(values[i], CAP_TITLE, TILE_W - 60f), gx + 30f, TILE_Y + 126f,
                              TILE_W - 60f, CAP_TITLE, victory ? GOLD : INK, true);
        }
        // The winning team with its players, when the record tells which it is; then the
        // players in their teams, and the game's length.
        if (winTeam >= 0) {
            final StringBuilder winners = new StringBuilder();
            for (JSONObject p : rows) {
                if (team(p) == winTeam) {
                    winners.append(winners.length() > 0 ? ", " : "").append(p.optString("name", ""));
                }
            }
            kit.textTop(c, kit.fit(kit.own("Team {0} wins: {1}", winTeam, winners), CAP_BODY, LAN_SUM_W),
                        ix, LAN_SUM_Y, CAP_BODY, GOLD, true);
        }
        if (teams > 0) {
            // "128 x 128 - Winter - 8 players in 5 teams": size and terrain as far as the record has them.
            final StringBuilder facts = new StringBuilder();
            if (lanMapW > 0 && lanMapH > 0) {
                facts.append(lanMapW).append(" x ").append(lanMapH).append(" - ");
            }
            if (lanTerrain >= 0) {
                facts.append(LanSettings.tilesetName(kit, lanTerrain)).append(" - ");
            }
            facts.append(kit.own("{0} players in {1} teams", rows.size(), teams));
            kit.textTop(c, kit.fit(facts.toString(), CAP_MID, LAN_SUM_W), ix,
                        LAN_SUM_Y + LINE_PITCH, CAP_MID, INK_MUTED, true);
        }
        if (!length.isEmpty()) {
            kit.textTop(c, kit.own("Length {0}", length), ix, LAN_SUM_Y + LINE_PITCH * 2f, CAP_MID,
                        INK_MUTED, true);
        }

        // The profile after this game: what it had, with what this game added.
        final float px = ix + 1620f + 72f, pw = iw - 1620f - 72f;
        well(c, px, TILE_Y, pw, LAN_PROFILE_H);
        if (before != null) {
            if (face == null) {
                face = Portraits.tinted(kit, before.portrait, Profiles.COLORS[before.color]);
            }
            kit.picture(c, face, px + 48f, TILE_Y + 24f, 203.4f, 168f);
            kit.textTop(c, kit.fit(before.name, CAP_BODY, pw - 360f), px + 310f, TILE_Y + 48f, CAP_BODY,
                        GOLD, true);
            kit.textTop(c, kit.own("Profile after this game"), px + 310f, TILE_Y + 48f + LINE_PITCH, CAP_SMALL,
                        INK_MUTED, true);
            final long u = own.optLong("units", 0L), b = own.optLong("buildings", 0L);
            final long k = own.optLong("kills", 0L), z = own.optLong("razings", 0L);
            lanDelta(c, px, pw, 0, kit.own("Games played"), before.games + 1, 1);
            if (victory) {
                lanDelta(c, px, pw, 1, kit.own("Victories"), before.victories + 1, 1);
            } else {
                lanDelta(c, px, pw, 1, kit.own("Defeats"), before.defeats + 1, 1);
            }
            lanDelta(c, px, pw, 2, kit.string("victorydefeat_units", "Units"), before.units + u, u);
            lanDelta(c, px, pw, 3, kit.string("victorydefeat_buildings", "Buildings"), before.buildings + b, b);
            lanDelta(c, px, pw, 4, kit.string("victorydefeat_kills", "Kills"), before.kills + k, k);
            lanDelta(c, px, pw, 5, kit.string("victorydefeat_razings", "Razings"), before.razings + z, z);
            lanDelta(c, px, pw, 6, kit.string("victorydefeat_score", "Score"), before.score + score, score);
            // The best rank so far; marked when this game raised it.
            final float y = LAN_DELTA_Y + LAN_DELTA_PITCH * 7;
            final boolean raised = rank > before.bestRank;
            final int best = raised ? rank : before.bestRank;
            final boolean bestOrc = raised ? orc : before.bestRankOrc;
            kit.textTop(c, kit.string("victorydefeat_rank", "Rank"), px + 48f, y, CAP_MID, INK_MUTED, true);
            kit.textTopRight(c, best > 0 ? kit.string(Profiles.rankKey(best, bestOrc), "-") : "-",
                             px + 900f, y, CAP_MID, GOLD, true);
            if (raised) {
                kit.textTopRight(c, "new", px + pw - 48f, y, CAP_MID, ADDED, true);
            }
        }

        // Every player of the game, team by team.
        well(c, ix, LAN_TABLE_Y, iw, lanTableH());
        final float nx = ix + 24f + 860f;
        final float cw = (ix + iw - 24f - 36f - nx) / COLUMNS.length;
        kit.textTop(c, kit.own("Team"), ix + 48f, LAN_TABLE_Y + 24f, CAP_SMALL, INK_MUTED, true);
        for (int k = 0; k < COLUMNS.length; ++k) {
            // As in the other form: a label's colon is dropped, a wide heading made smaller.
            String head = kit.string(COLUMNS[k], "");
            if (head.endsWith(":") || head.endsWith("：")) {
                head = head.substring(0, head.length() - 1).trim();
            }
            kit.textTopRight(c, kit.fit(head, CAP_SMALL, cw - 36f), nx + cw * k + cw - 24f, LAN_TABLE_Y + 24f,
                             CAP_SMALL, INK_MUTED, true);
        }
        // Eight rows fit a full window. In a lower view, or with more rows than that, the rows
        // are dragged inside the table as in the other form, with the count in the heading.
        final float top = lanRowsTop(), bottom = lanRowsBottom();
        final int whole = Math.max(1, (int) ((bottom - top + 6f) / LAN_ROW_PITCH));
        if (rows.size() > whole) {
            kit.textTopRight(c, whole + " / " + rows.size(), nx - 24f, LAN_TABLE_Y + 24f,
                             CAP_SMALL, INK_MUTED, true);
        }
        c.save();
        c.clipRect(kit.rect(ix + 24f, top, iw - 48f, bottom - top));
        for (int i = 0; i < rows.size(); ++i) {
            final JSONObject p = rows.get(i);
            final boolean me = p.optBoolean("me", false);
            final float rx = ix + 24f, ry = top + LAN_ROW_PITCH * i - scroll, rw = iw - 48f;
            if (ry + LAN_ROW_H < top || ry > bottom || clock < COUNT_FROM + ROW_STEP * i) {
                continue;
            }
            kit.fill(c, rx, ry, rw, LAN_ROW_H, me ? ROW_OWN : ROW_FAINT, 10f);
            if (team(p) >= 0) {
                kit.textMiddle(c, kit.own("Team {0}", team(p)), rx + 24f, ry, LAN_ROW_H, CAP_MID,
                               me ? INK : INK_MUTED, true);
            }
            final int color = p.optInt("color", -1);
            if (color >= 0 && color < Profiles.COLORS.length) {
                kit.fill(c, rx + 212f, ry + 12f, 72f, 72f, Profiles.COLORS[color], 12f);
                kit.outline(c, rx + 212f, ry + 12f, 72f, 72f, SWATCH_LINE, 12f);
            }
            kit.textMiddle(c, kit.fit(p.optString("name", ""), CAP_BODY, LAN_NAME_W), rx + 308f, ry, LAN_ROW_H,
                           CAP_BODY, me ? GOLD : INK, true);
            // The relation in the game's own words; it has none for a neutral player.
            final String relation = p.optString("relation", me ? "you" : "");
            if (relation.equals("you") || relation.equals("enemy") || relation.equals("ally")) {
                kit.textMiddle(c, kit.fit(kit.string("clan_relation_" + relation, ""), CAP_MID, LAN_RELATION_W),
                               rx + 648f, ry, LAN_ROW_H, CAP_MID, me ? INK : INK_MUTED, true);
            }
            for (int k = 0; k < FIELDS.length; ++k) {
                kit.textMiddleRight(c, grouped(counted(p.optLong(FIELDS[k], 0L))), nx + cw * k + cw - 24f, ry,
                                    LAN_ROW_H, CAP_MID, INK, true);
            }
            leaverMark(c, p.optString("quit", ""), rx, ry, rw);
        }
        c.restore();
        if (scroll < lanMaxScroll() - 1f) {
            final RectF edge = kit.rect(ix + 24f, bottom - 84f, iw - 48f, 84f);
            final Paint shade = new Paint();
            shade.setShader(new LinearGradient(0f, edge.top, 0f, edge.bottom, 0x00100C08, 0xF0100C08,
                                               Shader.TileMode.CLAMP));
            c.drawRect(edge, shade);
        }

        // Actions: leaving at the left. Playing again stands at the right only where it works
        // today: a room's game that ran on this device alone. With other people the room is
        // gone when the game ends, and the button is left out.
        final float by = buttonY();
        kit.nine(c, buttonPlain, ix, by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.own("Leave Lobby"), ix, by, BUTTON_W, BUTTON_H, CAP_BODY, INK, true);
        if (again) {
            final float ax = ix + iw - BUTTON_W;
            kit.nine(c, buttonMarked, ax, by, BUTTON_W, BUTTON_H, 14);
            kit.textCentre(c, kit.own("Play Again"), ax, by, BUTTON_W, BUTTON_H, CAP_BODY, GOLD, true);
        }
        if (arriving) {
            c.restore();
        }
        if (clock < COUNT_FROM + COUNT_MS + ROW_STEP * rows.size()) {
            postInvalidateOnAnimation();
        }
    }

    private boolean touchLan(MotionEvent e) {
        final float ty = e.getY() / kit.unit;
        if (e.getAction() == MotionEvent.ACTION_DOWN) {
            downY = ty;
            downScroll = scroll;
            dragging = false;
            inTable = ty >= lanRowsTop() && ty < lanRowsBottom();
        } else if (e.getAction() == MotionEvent.ACTION_MOVE && inTable) {
            if (Math.abs(ty - downY) > 16f) {
                dragging = true;
            }
            if (dragging) {
                scroll = Math.max(0f, Math.min(lanMaxScroll(), downScroll - (ty - downY)));
                invalidate();
            }
        }
        if (e.getAction() == MotionEvent.ACTION_UP && !dragging && clock >= BUTTONS_FROM) {
            final float x = e.getX() / kit.unit, y = ty;
            if (y >= buttonY() && y < buttonY() + BUTTON_H) {
                final float ax = ix() + iw() - BUTTON_W;
                if (x >= ix() && x < ix() + BUTTON_W) {
                    listener.onCommand(WargusHDActivity.CMD_EXIT_BACK);
                } else if (again && x >= ax && x < ax + BUTTON_W) {
                    listener.onCommand(WargusHDActivity.CMD_PLAY_AGAIN);
                }
            }
        }
        // Nothing under the window reacts while it is shown.
        return true;
    }
}
