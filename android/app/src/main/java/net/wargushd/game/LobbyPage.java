package net.wargushd.game;

import android.graphics.Bitmap;
import android.graphics.Canvas;

import net.wargushd.game.lan.LanRoom;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.TreeSet;

/**
 * The room before a network game, drawn from the room's snapshot alone: eight seat rows at the
 * left, grouped under a heading per team (owner decision 91, 5B) with the open and closed seats
 * after the teams; the map with the settings and the room chat at the right; Leave Lobby, the
 * reason the game cannot start yet, and Ready (a joiner) or Start Game (the host). A computer
 * seat is named after a hero of its race and shows his portrait (BotNames).
 *
 * What the player does here goes straight to the room; the owner of the page sends what the
 * room then wants sent. While a message is typed the seats stay in view above the keyboard.
 */
final class LobbyPage {
    static final int ACTION_NONE = 0, ACTION_LEAVE = 1, ACTION_START = 2, ACTION_CHANGED = 3;
    static final int ACTION_REFUSED = 4, ACTION_CHAT = 5, ACTION_SEND = 6, ACTION_CHAT_CLOSE = 7;
    static final int ACTION_MAP = 8;

    // Layout, logical units (design 38.10, L8 - L13).
    private static final float X0 = 816f, TOP = 264f, BOTTOM_UP = 264f, SLOTS_W = 1688f;
    private static final float ROW_X = 840f, ROW_W = 1640f, ROW_H = 204f, PITCH = 228f;
    private static final float PORTRAIT_W = 188.8f, PORTRAIT_H = 156f;
    private static final float NAME_X = 1077f, NAME_W = 320f, SEAT_W = 440f, BOX_DY = 48f, BOX_H = 108f;
    private static final float COL_X = 1409f, COL_DY = 54f, COL = 96f;
    private static final float TEAM_X = 1565f, TEAM_BOX_X = 1529f, TEAM_BOX_W = 300f;
    private static final float RACE_X = 1847f, RACE_W = 360f;
    private static final float STATE_X = 2060f, STATE_NARROW_X = 2219f, ROW_END = 2444f;
    private static final float XFER_DY = 174f, XFER_H = 12f;
    private static final float RIGHT_X = 2552f, RIGHT_W = 1192f, MAP_H = 780f, CHAT_Y = 1092f;
    private static final float PIC = 420f, FACT_X = 3068f, FACT_W = 616f, CHANGE_H = 108f;
    private static final float FIELD_W = 880f, FIELD_H = 120f, SEND_DX = 928f, SEND_W = 240f;
    private static final float ACTION_W = 720f, ACTION_H = 120f, ACTION_UP = 204f, ACTION_X = 3024f;
    private static final float REASON_X = 1584f, REASON_W = 624f;
    private static final float POP_ROW = 96f, POP_PITCH = 108f, POP_SEAT_W = 760f, POP_TEAM_W = 951f;
    // A team's heading above its rows (5B): the gold line, and the space it takes before them.
    private static final float HEAD_PITCH = 96f, HEAD_TEXT_DY = 6f, HEAD_X = 864f;
    // A computer seat's player (bots mod, L-B, design/bots/BOT_LEVEL_UI_OPTIONS.html): the host's
    // badge in the line under the name, a joiner's plain text in the same place.
    private static final float BADGE_DY = 108f, BADGE_W = 250f, BADGE_H = 72f, BADGE_CAP = 32f;
    private static final float BOT_TEXT_DY = 122f, NAME_TOP_DY = 40f;
    // The message line above the keyboard (L13).
    private static final float LINE_X = 720f, LINE_W = 3120f, LINE_H = 168f, BOX_X = 816f, BOX_W = 2040f;
    private static final float LINE_SEND_X = 2904f, LINE_CANCEL_X = 3336f, LINE_BUTTON_W = 408f;
    /** The keyboard's part of the screen where its height cannot be asked (design: assumed). */
    private static final float KEYBOARD_PART = 0.41f;

    private static final int POP_NONE = 0, POP_SEAT = 1, POP_TEAM = 2, POP_COLOUR = 3;
    private static final String[] COLOUR_NAMES = {
        "Red", "Blue", "Green", "Violet", "Orange", "Black", "White", "Yellow" };

    private final LanRoom room;
    /** The computer players this device's mods offer, with their names. */
    private final ModStore.BotChoices bots;
    private LanRoom.Snapshot snap;
    /** The room's map as this device has it: its picture and facts, or null. */
    private Bitmap preview;
    private PudInfo facts;
    private final HashMap<Integer, Bitmap> portraits = new HashMap<Integer, Bitmap>();
    /** The heroes of the game's scripts, and the one each computer seat is named after (or null). */
    private final List<BotNames.Hero> heroes;
    private BotNames.Hero[] heroOf = new BotNames.Hero[LanRoom.SEATS];
    /** Where each seat's row stands under the seats' top, the team headings, the rows' whole height. */
    private final float[] seatTop = new float[LanRoom.SEATS];
    private final List<float[]> heads = new ArrayList<float[]>();
    private float rowsH;

    private float scroll;
    // Seat dragging.
    private float downY, downScroll;
    private boolean inSeats, dragging;

    /** The list open over the rows, the seat it is about and where it stands. */
    private int pop = POP_NONE, popSeat;
    private float popX, popY;

    /** True while a message is typed: the layout with the keyboard. */
    boolean typing;
    private final StringBuilder message = new StringBuilder();
    /** The keyboard's height in logical units, or 0 when it is not known. */
    float keyboard;
    /**
     * Owner decision 101 (D1): a language mod takes no part in a network game, so the game's texts
     * are the English ones there; while one is switched on the room says so in one line.
     */
    boolean englishTexts;

    LobbyPage(LanRoom room, ModStore.BotChoices bots, List<BotNames.Hero> heroes) {
        this.room = room;
        this.bots = (bots != null) ? bots : new ModStore.BotChoices();
        this.heroes = (heroes != null) ? heroes : new ArrayList<BotNames.Hero>();
    }

    /** The room as it stands now. */
    void show(LanRoom.Snapshot s) {
        snap = s;
        heroOf = heroesOf(s, heroes);
        arrange();
    }

    /** The hero each seat of a snapshot is named after: null for a seat that is not a computer's. */
    static BotNames.Hero[] heroesOf(LanRoom.Snapshot s, List<BotNames.Hero> heroes) {
        final boolean[] computer = new boolean[s.seats.length];
        final String[] race = new String[s.seats.length];
        for (int i = 0; i < s.seats.length; ++i) {
            computer[i] = s.seats[i].kind.equals(LanRoom.COMPUTER);
            race[i] = s.seats[i].race;
        }
        return BotNames.assign(heroes, computer, race);
    }

    /**
     * The rows under their teams: each team that has a seat, lowest first, under its heading with
     * its seats in seat order; then the seats nobody holds, without a heading.
     */
    private void arrange() {
        heads.clear();
        final TreeSet<Integer> teams = new TreeSet<Integer>();
        for (LanRoom.Seat s : snap.seats) {
            if (s.occupied()) {
                teams.add(Integer.valueOf(s.team));
            }
        }
        float y = 0f;
        for (int t : teams) {
            int count = 0;
            for (LanRoom.Seat s : snap.seats) {
                count += (s.occupied() && s.team == t) ? 1 : 0;
            }
            heads.add(new float[] { y, t, count });
            y += HEAD_PITCH;
            for (int i = 0; i < LanRoom.SEATS; ++i) {
                if (snap.seats[i].occupied() && snap.seats[i].team == t) {
                    seatTop[i] = y;
                    y += PITCH;
                }
            }
        }
        for (int i = 0; i < LanRoom.SEATS; ++i) {
            if (!snap.seats[i].occupied()) {
                seatTop[i] = y;
                y += PITCH;
            }
        }
        rowsH = y;
    }

    /** The host's name, for the page's title. */
    String hostName() {
        return (snap != null) ? snap.seats[0].name : "";
    }

    /** The picture and the facts of the room's map on this device; both null when it is not here. */
    void showMap(Bitmap picture, PudInfo info) {
        preview = picture;
        facts = info;
    }

    // ----- the message -----

    void type(CharSequence text) {
        for (int i = 0; i < text.length() && message.length() < LanRoom.CHAT_MAX; ++i) {
            final char ch = text.charAt(i);
            if (ch >= 0x20 && ch != 0x7F) {
                message.append(ch);
            }
        }
    }

    void erase(int count) {
        message.setLength(Math.max(0, message.length() - Math.max(0, count)));
    }

    /** The message as typed so far. */
    String message() {
        return message.toString();
    }

    /** The typed message; the line is empty afterwards. */
    String takeMessage() {
        final String s = message.toString().trim();
        message.setLength(0);
        return s;
    }

    // ----- who may change what -----

    private boolean isMine(int seat) {
        return seat == snap.mySeat;
    }

    private boolean isPlayer(LanRoom.Seat s) {
        return s.kind.equals(LanRoom.HUMAN) || s.kind.equals(LanRoom.COMPUTER);
    }

    /** Colour and team: the own ones until Ready; the host also those of every other seat. */
    private boolean mayPick(int seat) {
        final LanRoom.Seat s = snap.seats[seat];
        return isPlayer(s) && (snap.host || (isMine(seat) && !s.ready));
    }

    private boolean mayRace(int seat) {
        final LanRoom.Seat s = snap.seats[seat];
        return isMine(seat) ? (snap.host || !s.ready) : (snap.host && s.kind.equals(LanRoom.COMPUTER));
    }

    private boolean maySeat(int seat) {
        return snap.host && !isMine(seat);
    }

    /**
     * A computer seat shows its player while the host offers mods' computer players (the room
     * says so, on the host and on every joiner). With no such mod the row is as it always was.
     */
    private boolean showsBot(LanRoom.Seat s) {
        return s.kind.equals(LanRoom.COMPUTER) && snap.bots;
    }

    /** The player after this one: Original, then the mods' players in their order, then Original. */
    private String nextBot(String now) {
        final int at = bots.indexOf(now);
        return (at + 1 < bots.bots.size()) ? bots.bots.get(at + 1).key() : "";
    }

    /** The name of a mod and its version, for the lines about a missing mod. */
    private String needText(SkinKit kit) {
        if (snap.need == null || snap.need.isEmpty()) {
            return "";
        }
        final LanRoom.GameplayMod g = snap.need.get(0);
        return kit.own("{0} {1}", bots.modName(g.id), g.version);
    }

    // ----- texts -----

    private String seatName(SkinKit kit, LanRoom.Seat s) {
        if (s.kind.equals(LanRoom.COMPUTER)) {
            final BotNames.Hero h = heroOf[s.index];
            return (h != null) ? h.botName() : kit.own("Computer");
        }
        return s.name;
    }

    /**
     * A seat's name fitted to a width. A hero's name is cut before its BOT, which always stays;
     * any other name is fitted as before.
     */
    private String fittedName(SkinKit kit, LanRoom.Seat s, float cap, float w) {
        final BotNames.Hero h = s.kind.equals(LanRoom.COMPUTER) ? heroOf[s.index] : null;
        return (h != null) ? fitBot(kit, h.botName(), cap, w) : kit.fit(seatName(kit, s), cap, w);
    }

    /** "ChogallBOT" cut to a width by the hero's part: "ChogaBOT"; at least one letter stays. */
    static String fitBot(SkinKit kit, String botName, float cap, float w) {
        if (!botName.endsWith(BotNames.SUFFIX)) {
            return kit.fit(botName, cap, w);
        }
        String hero = botName.substring(0, botName.length() - BotNames.SUFFIX.length());
        while (hero.length() > 1 && kit.width(hero + BotNames.SUFFIX, cap) > w) {
            hero = hero.substring(0, hero.length() - 1);
        }
        return hero + BotNames.SUFFIX;
    }

    private static String raceText(SkinKit kit, String race) {
        if (race.equals("human")) {
            return kit.string("customscenario_race_human", "Human");
        } else if (race.equals("orc")) {
            return kit.string("customscenario_race_orc", "Orc");
        } else if (race.equals("random")) {
            return kit.string("customscenario_random", "Random");
        }
        return kit.string("customscenario_mapdefault", "Map Default");
    }

    /** What a seat's row says at its right; `narrow` where the race control leaves less room. */
    private String stateText(SkinKit kit, LanRoom.Seat s, boolean mine, boolean narrow) {
        if (s.kind.equals(LanRoom.HELD)) {
            return kit.own("Away");
        }
        if (s.host || s.kind.equals(LanRoom.COMPUTER)) {
            return kit.own("Ready");
        }
        if (s.clash) {
            return kit.own("Colour taken");
        }
        if (!s.modsOk) {
            return kit.own("Needs the mod");
        }
        if (s.map.equals(LanRoom.MAP_RECV)) {
            final int pct = (mine && snap.receiving >= 0) ? snap.receiving : s.pct;
            return narrow ? pct + " %" : kit.own("Receiving {0} %", pct);
        }
        if (s.map.equals(LanRoom.MAP_DIFF)) {
            return kit.own("Different map file");
        }
        if (s.map.equals(LanRoom.MAP_NONE)) {
            return kit.own("No map");
        }
        return s.ready ? kit.own("Ready") : kit.own("Waiting");
    }

    private String names(SkinKit kit, int[] seats) {
        final StringBuilder b = new StringBuilder();
        for (int i : seats) {
            if (i >= 0 && i < LanRoom.SEATS) {
                b.append(b.length() > 0 ? ", " : "").append(seatName(kit, snap.seats[i]));
            }
        }
        return b.toString();
    }

    /** The line beside the primary button: why the game does not start yet. */
    private String reason(SkinKit kit) {
        if (!snap.host) {
            final LanRoom.Seat me = mySeat();
            if (me != null && (snap.receiving >= 0 || me.map.equals(LanRoom.MAP_RECV))) {
                return kit.own("Receiving the map");
            }
            if (me != null && me.clash) {
                return kit.own("Choose another colour");
            }
            if (me != null && !me.modsOk) {
                // LAW 18.6: the computer players' mod must be the same on every device.
                return kit.own("Ready needs the mod {0}", needText(kit));
            }
            int people = 0, ready = 0;
            for (LanRoom.Seat s : snap.seats) {
                if (s.kind.equals(LanRoom.HUMAN)) {
                    ++people;
                    ready += (s.host || s.ready) ? 1 : 0;
                }
            }
            return kit.own("{0} of {1} players ready", ready, people);
        }
        if (snap.gate.equals(LanRoom.GATE_OK)) {
            return kit.own("All players ready");
        } else if (snap.gate.equals(LanRoom.GATE_PLAYERS)) {
            return kit.own("Waiting for players");
        } else if (snap.gate.equals(LanRoom.GATE_MAP)) {
            return kit.own("Receiving the map");
        } else if (snap.gate.equals(LanRoom.GATE_COLOUR)) {
            return kit.own("Two players have one colour");
        } else if (snap.gate.equals(LanRoom.GATE_TEAMS)) {
            return kit.own("All players are in one team");
        } else if (snap.gate.equals(LanRoom.GATE_BOTS)) {
            return kit.own("{0} needs the mod {1}", names(kit, snap.gateWho), needText(kit));
        }
        return kit.own("Waiting for {0}", names(kit, snap.gateWho));
    }

    private LanRoom.Seat mySeat() {
        return (snap.mySeat >= 0 && snap.mySeat < LanRoom.SEATS) ? snap.seats[snap.mySeat] : null;
    }

    /** A joiner may be Ready once the map is here and the colour is nobody else's. */
    private boolean canReady() {
        final LanRoom.Seat me = mySeat();
        return me != null && !me.clash && me.map.equals(LanRoom.MAP_HAS) && me.modsOk;
    }

    // ----- drawing -----

    private float panelBottom(SkinKit kit) {
        return typing ? lineY(kit) - 24f : kit.height - BOTTOM_UP;
    }

    private float lineY(SkinKit kit) {
        final float up = (keyboard > 0f) ? keyboard : kit.height * KEYBOARD_PART;
        return Math.max(TOP + PITCH + 72f, kit.height - up - LINE_H);
    }

    private float rowY(int seat) {
        return TOP + 24f + seatTop[seat] - scroll;
    }

    private float maxScroll(SkinKit kit) {
        return Math.max(0f, 24f + rowsH - (PITCH - ROW_H) + 24f - (panelBottom(kit) - TOP));
    }

    private float actionY(SkinKit kit) { return kit.height - ACTION_UP; }

    private Bitmap portrait(SkinKit kit, LanRoom.Seat s) {
        // A computer seat shows its hero (his icon's slot of the player's portrait atlas).
        final BotNames.Hero h = s.kind.equals(LanRoom.COMPUTER) ? heroOf[s.index] : null;
        final int slot = (h != null) ? h.slot : s.portrait;
        final Integer key = Integer.valueOf(slot * 8 + (s.colour & 7));
        if (!portraits.containsKey(key)) {
            portraits.put(key, Portraits.tinted(kit, slot, Profiles.COLORS[s.colour & 7]));
        }
        return portraits.get(key);
    }

    void draw(Canvas c, SkinKit kit) {
        if (snap == null) {
            return;
        }
        final float bottom = panelBottom(kit);
        scroll = Math.min(scroll, maxScroll(kit));
        LanKit.panel(c, kit, X0, TOP, SLOTS_W, bottom - TOP);
        c.save();
        c.clipRect(kit.rect(X0, TOP + 12f, SLOTS_W, bottom - TOP - 24f));
        for (float[] h : heads) {
            final float hy = TOP + 24f + h[0] - scroll;
            if (hy + HEAD_PITCH >= TOP && hy <= bottom) {
                final int count = (int) h[2];
                final String head = (count == 1) ? kit.own("Team {0} - 1 player", (int) h[1])
                                                 : kit.own("Team {0} - {1} players", (int) h[1], count);
                kit.textTop(c, kit.fit(head, LanKit.CAP_MID, ROW_W - 48f), HEAD_X, hy + HEAD_TEXT_DY,
                            LanKit.CAP_MID, LanKit.GOLD, true);
            }
        }
        int below = 0;
        for (int i = 0; i < LanRoom.SEATS; ++i) {
            final float y = rowY(i);
            if (y + ROW_H > bottom) {
                ++below;
            }
            if (y + ROW_H >= TOP && y <= bottom) {
                drawSeat(c, kit, i, y);
            }
        }
        c.restore();
        if (below > 0) {
            kit.fill(c, X0 + 12f, bottom - 60f, SLOTS_W - 24f, 48f, LanKit.POP_FILL, 0f);
            kit.textTop(c, kit.own(below == 1 ? "{0} more seat - scroll" : "{0} more seats - scroll", below),
                        X0 + 60f, bottom - 51f, LanKit.CAP_SMALL, LanKit.INK_MUTED, true);
        }
        if (typing) {
            drawChat(c, kit, TOP, bottom - TOP, false);
            drawLine(c, kit);
        } else {
            drawMap(c, kit);
            drawChat(c, kit, CHAT_Y, bottom - CHAT_Y, true);
            drawActions(c, kit);
        }
        if (pop != POP_NONE) {
            drawPop(c, kit);
        }
    }

    private void drawSeat(Canvas c, SkinKit kit, int i, float y) {
        final LanRoom.Seat s = snap.seats[i];
        final boolean mine = isMine(i);
        kit.fill(c, ROW_X, y, ROW_W, ROW_H, mine ? LanKit.ROW_SELECTED : LanKit.ROW_FAINT, LanKit.ROW_RADIUS);
        if (!s.occupied()) {
            final String kind = s.kind.equals(LanRoom.CLOSED) ? kit.own("Closed") : kit.own("Open");
            // A seat the map has no place for is closed for the host as well: no box to tap.
            if (snap.host && i < snap.mapPlayers) {
                LanKit.value(c, kit, kind, NAME_X, y + BOX_DY, SEAT_W, BOX_H, LanKit.CAP_BODY);
            } else {
                kit.textMiddle(c, kind, NAME_X, y, ROW_H, LanKit.CAP_BODY, LanKit.INK_MUTED, true);
            }
            return;
        }
        kit.picture(c, portrait(kit, s), ROW_X + 24f, y + 24f, PORTRAIT_W, PORTRAIT_H);
        final String sub = s.host ? (mine ? kit.own("Host - you") : kit.own("Host")) : mine ? kit.own("You") : null;
        final String name = fittedName(kit, s, LanKit.CAP_BODY, NAME_W);
        if (sub != null) {
            kit.textTop(c, name, NAME_X, y + NAME_TOP_DY, LanKit.CAP_BODY, LanKit.INK, true);
            kit.textTop(c, sub, NAME_X, y + 122f, LanKit.CAP_SMALL, LanKit.INK_MUTED, true);
        } else if (showsBot(s)) {
            kit.textTop(c, name, NAME_X, y + NAME_TOP_DY, LanKit.CAP_BODY, LanKit.INK, true);
            final String player = bots.nameOf(s.bot, kit.own("Original"));
            if (snap.host) {
                // The host's badge: a tap goes to the next player (owner decision 80).
                kit.fill(c, NAME_X, y + BADGE_DY, BADGE_W, BADGE_H, LanKit.WELL, BADGE_H / 2f);
                kit.outline(c, NAME_X, y + BADGE_DY, BADGE_W, BADGE_H, LanKit.GOLD, BADGE_H / 2f);
                kit.textCentre(c, kit.fit(player, BADGE_CAP, BADGE_W - 36f), NAME_X, y + BADGE_DY, BADGE_W,
                               BADGE_H, BADGE_CAP, LanKit.GOLD, true);
            } else {
                kit.textTop(c, kit.fit(player, LanKit.CAP_SMALL, NAME_W), NAME_X, y + BOT_TEXT_DY,
                            LanKit.CAP_SMALL, LanKit.GOLD, true);
            }
        } else {
            kit.textMiddle(c, name, NAME_X, y, ROW_H, LanKit.CAP_BODY, LanKit.INK, true);
        }

        if (s.clash) {
            // The colour is somebody else's: a ring around the swatch.
            kit.outline(c, COL_X - 9f, y + COL_DY - 9f, COL + 18f, COL + 18f, LanKit.GOLD, 16f);
        }
        LanKit.swatch(c, kit, s.colour, COL_X, y + COL_DY, COL);
        final String team = kit.own("Team {0}", s.team);
        if (mayPick(i)) {
            LanKit.value(c, kit, team, TEAM_BOX_X, y + BOX_DY, TEAM_BOX_W, BOX_H, LanKit.CAP_MID);
        } else {
            kit.textMiddle(c, team, TEAM_X, y, ROW_H, LanKit.CAP_MID, LanKit.INK, true);
        }
        final boolean control = mayRace(i);
        if (control) {
            final String[] races = { kit.string("customscenario_race_human", "Human"),
                                     kit.string("customscenario_race_orc", "Orc") };
            LanKit.segments(c, kit, races, s.race.equals("human") ? 0 : s.race.equals("orc") ? 1 : -1,
                            RACE_X, y + BOX_DY, RACE_W, BOX_H, LanKit.CAP_MID);
        } else {
            // "Random" is wider than the column at the body size: it goes one size down, uncut.
            final String raceWord = raceText(kit, s.race);
            final float raceCap = kit.width(raceWord, LanKit.CAP_BODY) <= 200f ? LanKit.CAP_BODY : LanKit.CAP_MID;
            kit.textMiddle(c, kit.fit(raceWord, raceCap, 200f), RACE_X, y, ROW_H, raceCap, LanKit.INK, true);
        }
        final float sx = control ? STATE_NARROW_X : STATE_X;
        final String state = stateText(kit, s, mine, control);
        final boolean good = state.equals(kit.own("Ready"));
        kit.textMiddle(c, kit.fit(state, LanKit.CAP_MID, ROW_END - sx), sx, y, ROW_H, LanKit.CAP_MID,
                       good ? LanKit.GOLD : LanKit.INK_MUTED, true);
        if (s.map.equals(LanRoom.MAP_RECV)) {
            final int pct = (mine && snap.receiving >= 0) ? snap.receiving : s.pct;
            LanKit.bar(c, kit, NAME_X, y + XFER_DY, ROW_END - NAME_X, XFER_H, pct / 100f);
        }
    }

    private void drawMap(Canvas c, SkinKit kit) {
        LanKit.panel(c, kit, RIGHT_X, TOP, RIGHT_W, MAP_H);
        final float px = RIGHT_X + 60f, py = TOP + 60f;
        LanKit.well(c, kit, px, py, PIC, PIC);
        if (preview != null) {
            // The whole map, its sides kept, centred in its place.
            final float k = Math.min(PIC / preview.getWidth(), PIC / preview.getHeight());
            final float pw = preview.getWidth() * k, ph = preview.getHeight() * k;
            kit.pictureSharp(c, preview, px + (PIC - pw) / 2f, py + (PIC - ph) / 2f, pw, ph);
        }
        String name = snap.mapName;
        if (name.toLowerCase().endsWith(".pud")) {
            name = name.substring(0, name.length() - 4);
        }
        kit.textTop(c, kit.fit(name, LanKit.CAP_MID, FACT_W), FACT_X, TOP + 66f, LanKit.CAP_MID, LanKit.GOLD, true);
        if (facts != null) {
            final StringBuilder size = new StringBuilder();
            if (facts.width > 0 && facts.height > 0) {
                size.append(facts.width).append(" x ").append(facts.height);
            }
            final int era = (snap.tileset >= 0) ? snap.tileset : facts.tileset;
            if (era >= 0) {
                size.append(size.length() > 0 ? " - " : "").append(LanSettings.tilesetName(kit, era));
            }
            kit.textTop(c, kit.fit(size.toString(), LanKit.CAP_MID, FACT_W), FACT_X, TOP + 148.5f,
                        LanKit.CAP_MID, LanKit.INK_MUTED, true);
            if (facts.players > 0) {
                kit.textTop(c, kit.own("Up to {0} players", facts.players), FACT_X, TOP + 210f,
                            LanKit.CAP_MID, LanKit.INK_MUTED, true);
            }
        } else {
            kit.textTop(c, kit.fit(kit.own("Not on this device"), LanKit.CAP_MID, FACT_W), FACT_X, TOP + 148.5f,
                        LanKit.CAP_MID, LanKit.INK_MUTED, true);
        }
        if (snap.host) {
            LanKit.button(c, kit, kit.own("Change..."), FACT_X, TOP + 372f, FACT_W, CHANGE_H, LanKit.CAP_BODY,
                          LanKit.PLAIN);
        }
        final LanSettings set = new LanSettings();
        set.from(snap);
        final String[] lines = {
            kit.own("Resources: {0}", set.value(kit, 0)),
            kit.own("Units: {0}", set.value(kit, 1)),
            kit.own("Fog of War: {0}", snap.fog ? kit.own("on") : kit.own("off")),
            kit.own("Terrain: {0}", set.value(kit, 4)),
        };
        for (int i = 0; i < lines.length; ++i) {
            kit.textTop(c, kit.fit(lines[i], LanKit.CAP_MID, RIGHT_W - 120f), px, TOP + 516f + 62f * i,
                        LanKit.CAP_MID, LanKit.INK, true);
        }
    }

    /** The chat panel; `field` adds the message field and Send at its foot. */
    private void drawChat(Canvas c, SkinKit kit, float y, float h, boolean field) {
        LanKit.panel(c, kit, RIGHT_X, y, RIGHT_W, h);
        final float x = RIGHT_X + 60f, w = RIGHT_W - 120f;
        kit.textTop(c, kit.own("Room chat"), x, y + 36f, LanKit.CAP_SMALL, LanKit.INK_MUTED, true);
        if (englishTexts) {
            // At the right end of the panel's heading row, fitted into the room the heading leaves.
            // (A fitted text fills its room: it then starts where the room starts.)
            final String english = kit.own("Game texts are in English in network games");
            final float noteRoom = w - kit.width(kit.own("Room chat"), LanKit.CAP_SMALL) - 48f;
            final float noteW = Math.min(noteRoom, kit.width(english, LanKit.CAP_SMALL));
            final String note = kit.fit(english, LanKit.CAP_SMALL, noteRoom);
            kit.textTop(c, note, x + w - noteW, y + 36f, LanKit.CAP_SMALL, LanKit.GOLD, true);
        }
        final float foot = y + h - (field ? 168f : 24f);
        final int room = Math.max(0, (int) ((foot - (y + 108f)) / LanKit.LINE_PITCH));
        final List<LanRoom.Chat> chat = snap.chat;
        final int size = (chat == null) ? 0 : chat.size();
        for (int i = Math.max(0, size - room); i < size; ++i) {
            final LanRoom.Chat line = chat.get(i);
            final float ly = y + 108f + (i - Math.max(0, size - room)) * LanKit.LINE_PITCH;
            if (line.seat < 0) {
                // A line of the room itself.
                final String said = line.code.equals("map_received")
                    ? kit.own("{0} received the map", line.name.isEmpty() ? line.text : line.name) : line.text;
                kit.textTop(c, kit.fit(said, LanKit.CAP_MID, w), x, ly, LanKit.CAP_MID, LanKit.INK_MUTED, true);
            } else {
                LanKit.swatch(c, kit, line.colour, x, ly, 36f);
                kit.textTop(c, kit.fit(line.name + ": " + line.text, LanKit.CAP_MID, w - 54f), x + 54f, ly,
                            LanKit.CAP_MID, LanKit.INK, true);
            }
        }
        if (field) {
            final float fy = y + h - 144f;
            kit.fill(c, RIGHT_X + 24f, fy, FIELD_W, FIELD_H, LanKit.FIELD, 14f);
            kit.outline(c, RIGHT_X + 24f, fy, FIELD_W, FIELD_H, LanKit.LINE, 14f);
            kit.textMiddle(c, kit.own("Message"), x, fy, FIELD_H, LanKit.CAP_BODY, LanKit.INK_DISABLED, true);
            LanKit.button(c, kit, kit.own("Send"), RIGHT_X + SEND_DX, fy, SEND_W, FIELD_H, LanKit.CAP_BODY,
                          LanKit.PLAIN);
        }
    }

    /** The message line that stands above the keyboard. */
    private void drawLine(Canvas c, SkinKit kit) {
        final float y = lineY(kit);
        kit.fill(c, LINE_X, y, LINE_W, LINE_H, 0xEC0E0B08, 0f);
        kit.fill(c, BOX_X, y + 30f, BOX_W, BOX_H, LanKit.FIELD, 14f);
        kit.outline(c, BOX_X, y + 30f, BOX_W, BOX_H, LanKit.GOLD, 14f);
        // A message longer than the box shows its end.
        String shown = message.toString();
        while (shown.length() > 1 && kit.width(shown, LanKit.CAP_BODY) > BOX_W - 96f) {
            shown = shown.substring(1);
        }
        kit.textMiddle(c, shown, BOX_X + 36f, y + 30f, BOX_H, LanKit.CAP_BODY, LanKit.INK, true);
        final float caret = BOX_X + 36f + (shown.isEmpty() ? 0f : kit.width(shown, LanKit.CAP_BODY) + 8f);
        kit.fill(c, caret, y + 60f, 4f, BOX_H - 60f, LanKit.GOLD, 0f);
        LanKit.button(c, kit, kit.own("Send"), LINE_SEND_X, y + 24f, LINE_BUTTON_W, ACTION_H, LanKit.CAP_BODY,
                      LanKit.PRIMARY);
        LanKit.button(c, kit, kit.string("common_cancel", "Cancel"), LINE_CANCEL_X, y + 24f, LINE_BUTTON_W,
                      ACTION_H, LanKit.CAP_BODY, LanKit.PLAIN);
    }

    private void drawActions(Canvas c, SkinKit kit) {
        final float y = actionY(kit);
        LanKit.button(c, kit, kit.own("Leave Lobby"), X0, y, ACTION_W, ACTION_H, LanKit.CAP_BODY, LanKit.PLAIN);
        kit.textMiddle(c, kit.fit(reason(kit), LanKit.CAP_SMALL, REASON_W), REASON_X, y, ACTION_H,
                       LanKit.CAP_SMALL, LanKit.INK, true);
        if (snap.host) {
            LanKit.button(c, kit, kit.string("customscenario_start", "Start Game"), ACTION_X, y, ACTION_W,
                          ACTION_H, LanKit.CAP_BODY, snap.canStart ? LanKit.PRIMARY : LanKit.DISABLED);
        } else {
            final LanRoom.Seat me = mySeat();
            final boolean ready = me != null && me.ready;
            LanKit.button(c, kit, ready ? kit.own("Not Ready") : kit.own("Ready"), ACTION_X, y, ACTION_W,
                          ACTION_H, LanKit.CAP_BODY,
                          !canReady() ? LanKit.DISABLED : ready ? LanKit.PLAIN : LanKit.PRIMARY);
        }
    }

    // ----- the lists over the rows -----

    private int popRows() {
        if (pop == POP_SEAT) {
            // Remove frees a seat a person or a computer holds (owner 2026-10-17); no "allied" computer
            // kind: the team selector does that.
            final String kind = snap.seats[popSeat].kind;
            return (kind.equals(LanRoom.HUMAN) || kind.equals(LanRoom.COMPUTER)) ? 4 : 3;
        }
        return (pop == POP_TEAM) ? LanRoom.TEAMS : LanRoom.COLOURS;
    }

    private float popW() { return (pop == POP_TEAM) ? POP_TEAM_W : POP_SEAT_W; }
    private float popH() { return popRows() * POP_PITCH + 24f; }

    /** Open a list under a seat's row, or above it where it would run off the page. */
    private void open(int kind, int seat, float x, SkinKit kit) {
        pop = kind;
        popSeat = seat;
        popX = x;
        popY = rowY(seat) + 162f;
        if (popY + popH() > kit.height - BOTTOM_UP - 12f) {
            popY = Math.max(TOP + 12f, rowY(seat) + 42f - popH());
        }
    }

    /** The players of a team, by name. */
    private String members(SkinKit kit, int team) {
        final StringBuilder b = new StringBuilder();
        for (LanRoom.Seat s : snap.seats) {
            if (isPlayer(s) && s.team == team) {
                b.append(b.length() > 0 ? ", " : "").append(seatName(kit, s));
            }
        }
        return (b.length() > 0) ? b.toString() : kit.own("empty");
    }

    /** The seat that holds a colour, or null. */
    private LanRoom.Seat holder(int colour) {
        for (LanRoom.Seat s : snap.seats) {
            if (isPlayer(s) && s.colour == colour && s.index != popSeat) {
                return s;
            }
        }
        return null;
    }

    private void drawPop(Canvas c, SkinKit kit) {
        final float w = popW();
        LanKit.pop(c, kit, popX, popY, w, popH());
        final LanRoom.Seat s = snap.seats[popSeat];
        final String[] kinds = { kit.own("Open"), kit.own("Closed"), kit.own("Computer"), kit.own("Remove") };
        for (int r = 0; r < popRows(); ++r) {
            final float y = popY + 12f + POP_PITCH * r;
            final boolean now = (pop == POP_TEAM && s.team == r + 1) || (pop == POP_COLOUR && s.colour == r);
            kit.fill(c, popX + 12f, y, w - 24f, POP_ROW, now ? LanKit.ROW_SELECTED : LanKit.ROW_FAINT,
                     LanKit.ROW_RADIUS);
            if (pop == POP_SEAT) {
                kit.textMiddle(c, kinds[r], popX + 48f, y, POP_ROW, LanKit.CAP_MID, LanKit.INK, true);
            } else if (pop == POP_TEAM) {
                kit.textMiddle(c, kit.own("Team {0}", r + 1), popX + 59f, y, POP_ROW, LanKit.CAP_MID,
                               LanKit.INK, true);
                kit.textMiddle(c, kit.fit(members(kit, r + 1), LanKit.CAP_SMALL, 600f), popX + 311f, y, POP_ROW,
                               LanKit.CAP_SMALL, LanKit.INK_MUTED, true);
            } else {
                LanKit.swatch(c, kit, r, popX + 36f, y + 18f, 60f);
                kit.textMiddle(c, kit.own(COLOUR_NAMES[r]), popX + 132f, y, POP_ROW, LanKit.CAP_MID,
                               LanKit.INK, true);
                final LanRoom.Seat held = holder(r);
                if (held != null) {
                    kit.textMiddleRight(c, fittedName(kit, held, LanKit.CAP_SMALL, 300f), popX + w - 48f,
                                        y, POP_ROW, LanKit.CAP_SMALL, LanKit.INK_MUTED, true);
                }
            }
        }
    }

    /** A row of the open list was chosen. */
    private void choose(int row) {
        final boolean mine = isMine(popSeat);
        if (pop == POP_SEAT) {
            if (row == 3) {
                if (snap.seats[popSeat].kind.equals(LanRoom.HUMAN)) {
                    room.hostKick(popSeat);
                } else {
                    room.hostSetSeat(popSeat, LanRoom.OPEN);
                }
            } else {
                room.hostSetSeat(popSeat, row == 0 ? LanRoom.OPEN : row == 1 ? LanRoom.CLOSED : LanRoom.COMPUTER);
            }
        } else if (pop == POP_TEAM) {
            if (mine) {
                room.pickTeam(row + 1);
            } else {
                room.hostSetTeam(popSeat, row + 1);
            }
        } else if (mine) {
            room.pickColour(row);
        } else {
            room.hostSetColour(popSeat, row);
        }
    }

    // ----- touch -----

    void down(float x, float y, SkinKit kit) {
        downY = y;
        downScroll = scroll;
        dragging = false;
        inSeats = pop == POP_NONE && LanKit.in(x, y, X0, TOP, SLOTS_W, panelBottom(kit) - TOP);
    }

    /** @return true when the seats moved */
    boolean move(float x, float y, SkinKit kit) {
        if (!inSeats || maxScroll(kit) <= 0f) {
            return false;
        }
        if (Math.abs(y - downY) > 16f) {
            dragging = true;
        }
        if (dragging) {
            scroll = Math.max(0f, Math.min(maxScroll(kit), downScroll - (y - downY)));
        }
        return dragging;
    }

    /** A finger was lifted at (x, y), in logical units. */
    int up(float x, float y, SkinKit kit) {
        final boolean dragged = dragging;
        dragging = false;
        inSeats = false;
        if (snap == null || dragged) {
            return ACTION_NONE;
        }
        if (pop != POP_NONE) {
            // A tap on a row chooses it; a tap anywhere else only closes the list.
            for (int r = 0; r < popRows(); ++r) {
                if (LanKit.in(x, y, popX + 12f, popY + 12f + POP_PITCH * r, popW() - 24f, POP_ROW)) {
                    choose(r);
                }
            }
            pop = POP_NONE;
            return ACTION_CHANGED;
        }
        if (typing) {
            final float ly = lineY(kit) + 24f;
            if (LanKit.in(x, y, LINE_SEND_X, ly, LINE_BUTTON_W, ACTION_H)) {
                return ACTION_SEND;
            }
            return LanKit.in(x, y, LINE_CANCEL_X, ly, LINE_BUTTON_W, ACTION_H) ? ACTION_CHAT_CLOSE : ACTION_NONE;
        }
        final float ay = actionY(kit), bottom = panelBottom(kit);
        if (LanKit.in(x, y, X0, ay, ACTION_W, ACTION_H)) {
            return ACTION_LEAVE;
        }
        if (LanKit.in(x, y, ACTION_X, ay, ACTION_W, ACTION_H)) {
            if (snap.host) {
                return snap.canStart ? ACTION_START : ACTION_REFUSED;
            }
            if (!canReady()) {
                return ACTION_REFUSED;
            }
            room.setReady(!mySeat().ready);
            return ACTION_CHANGED;
        }
        if (snap.host && LanKit.in(x, y, FACT_X, TOP + 372f, FACT_W, CHANGE_H)) {
            return ACTION_MAP;
        }
        if (LanKit.in(x, y, RIGHT_X + 24f, bottom - 144f, RIGHT_W - 48f, FIELD_H)) {
            // The field and Send both open the message line: there is nothing to send yet.
            return ACTION_CHAT;
        }
        for (int i = 0; i < LanRoom.SEATS; ++i) {
            final float ry = rowY(i);
            if (!LanKit.in(x, y, ROW_X, ry, ROW_W, ROW_H) || y < TOP + 12f || y > bottom - 12f) {
                continue;
            }
            final LanRoom.Seat s = snap.seats[i];
            if (!s.occupied()) {
                if (snap.host && i < snap.mapPlayers && LanKit.in(x, y, NAME_X, ry + BOX_DY, SEAT_W, BOX_H)) {
                    open(POP_SEAT, i, NAME_X, kit);
                    return ACTION_CHANGED;
                }
                return ACTION_NONE;
            }
            final int race = mayRace(i) ? LanKit.segmentAt(x, y, 2, RACE_X, ry + BOX_DY, RACE_W, BOX_H) : -1;
            final boolean badge = snap.host && showsBot(s);
            if (badge && LanKit.in(x, y, NAME_X, ry + BADGE_DY, BADGE_W, BADGE_H)) {
                room.hostSetBot(i, nextBot(s.bot));
            } else if (mayPick(i) && LanKit.in(x, y, COL_X, ry + COL_DY, COL, COL)) {
                open(POP_COLOUR, i, COL_X, kit);
            } else if (mayPick(i) && LanKit.in(x, y, TEAM_BOX_X, ry + BOX_DY, TEAM_BOX_W, BOX_H)) {
                open(POP_TEAM, i, TEAM_BOX_X, kit);
            } else if (race >= 0) {
                // A tap on the lit race puts it back to Random.
                final String want = (race == 0) ? "human" : "orc";
                final String to = s.race.equals(want) ? "random" : want;
                if (isMine(i)) {
                    room.pickRace(to);
                } else {
                    room.hostSetRace(i, to);
                }
            } else if (maySeat(i) && LanKit.in(x, y, NAME_X, ry, NAME_W, badge ? BADGE_DY - 12f : ROW_H)) {
                // With the badge the name box is the part above it: the two never share a tap.
                open(POP_SEAT, i, NAME_X, kit);
            } else {
                return ACTION_NONE;
            }
            return ACTION_CHANGED;
        }
        return ACTION_NONE;
    }

    /** Back: close the open list. @return true when there was one */
    boolean back() {
        if (pop == POP_NONE) {
            return false;
        }
        pop = POP_NONE;
        return true;
    }
}
