package net.wargushd.game.lan;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Set;

/**
 * The room before a network game: seats, colours, teams, ready, the map and its transfer, the
 * chat and the Start gate. One class for both sides; the host is the single writer and its
 * "state" line is the only truth a joiner has.
 *
 * No socket, no thread and no clock in here. The owner feeds what happened ("peer connected",
 * "peer sent a line", "peer lost", what the local player did, "tick" with the time in ms) and
 * afterwards takes the lines to send, the events for the view and a snapshot to draw. All calls
 * must come from one thread.
 */
public final class LanRoom {
    public static final int SEATS = 8;
    /** The fewest places a map can have. */
    public static final int MIN_PLAYERS = 2;
    public static final int COLOURS = 8;
    public static final int TEAMS = 8;

    /** A ping goes out this often. */
    public static final long PING_MS = 2000;
    /** This long without a line and the peer is lost. */
    public static final long SILENCE_MS = 6000;
    /** A connection that has not said hello after this long is dropped. */
    public static final long HELLO_MS = 5000;
    /** How long a seat is kept for a returning player after a game. */
    public static final long HOLD_MS = 60000;

    public static final int CHAT_TAIL = 50;
    public static final int CHAT_MAX = 200;
    public static final int NAME_MAX = 32;
    /** The engine knows a player by a name of at most this many bytes. */
    public static final int NET_NAME_BYTES = 15;
    /** Map chunks in flight to one joiner. */
    static final int WINDOW = 4;
    static final int MAP_TRIES = 2;

    /** On a joiner the host is this peer. */
    public static final int HOST_PEER = 0;

    // Seat kinds.
    public static final String OPEN = "open";
    public static final String CLOSED = "closed";
    public static final String COMPUTER = "computer";
    public static final String HUMAN = "human";
    /** Empty, kept for a returning player. */
    public static final String HELD = "held";

    // What a seat's device has of the room's map.
    public static final String MAP_NONE = "none";
    public static final String MAP_HAS = "has";
    public static final String MAP_RECV = "recv";
    public static final String MAP_DIFF = "diff";

    // Phases.
    public static final int CONNECTING = 0;
    public static final int LOBBY = 1;
    public static final int STARTED = 2;
    public static final int ENDED = 3;

    // The Start gate: "ok" or the first thing that is in the way.
    public static final String GATE_OK = "ok";
    public static final String GATE_PLAYERS = "need_players";
    public static final String GATE_MAP = "map_missing";
    public static final String GATE_COLOUR = "colour_clash";
    public static final String GATE_READY = "not_ready";
    public static final String GATE_TEAMS = "one_team";
    /** A computer seat plays a mod's computer player and a person lacks that mod as the host has it. */
    public static final String GATE_BOTS = "bot_mods";

    /** The most gameplay mods one device tells the room of. */
    public static final int GAMEPLAY_MODS_MAX = 16;
    /** The longest "<mod>:<id>" of a computer player (the engine's seat name stays under 64 bytes). */
    public static final int BOT_MAX = 55;

    // Refusals of a hello beyond the identity codes of LanIdentity.
    public static final String REFUSE_HELLO = "bad_hello";
    public static final String REFUSE_NAME = "name";
    public static final String REFUSE_NAME_TAKEN = "name_taken";
    public static final String REFUSE_FULL = "full";
    public static final String REFUSE_STARTED = "started";

    /** Why a joiner was put out when the new map had no place left for him. */
    public static final String REMOVED_NO_SEAT = "no_seat";

    private static final String[] RACES = { "map", "human", "orc", "random" };

    /** The local player as the room needs him. */
    public static final class Profile {
        public final String id;
        public final String name;
        public final int portrait;
        public final String race;
        public final int colour;

        public Profile(String id, String name, int portrait, String race, int colour) {
            this.id = id;
            this.name = name;
            this.portrait = portrait;
            this.race = race;
            this.colour = colour;
        }
    }

    /** The device's maps, as far as the room needs them. */
    /**
     * A gameplay mod as the room compares it (LAW 18.6): its id, version and the SHA-256 of its
     * manifest.json. Two devices have the same mod when all three are equal.
     */
    public static final class GameplayMod {
        public final String id;
        public final String version;
        public final String sha;

        public GameplayMod(String id, String version, String sha) {
            this.id = id;
            this.version = version;
            this.sha = (sha == null) ? "" : sha;
        }

        boolean same(GameplayMod o) {
            return o != null && id.equals(o.id) && version.equals(o.version) && sha.equals(o.sha);
        }

        Map<String, Object> toMap() {
            final Map<String, Object> m = new LinkedHashMap<String, Object>();
            m.put("id", id);
            m.put("v", version);
            m.put("sha", sha);
            return m;
        }

        /** One of a message's list, or null when it is not a sound one. */
        static GameplayMod from(Object o) {
            if (!(o instanceof Map)) {
                return null;
            }
            @SuppressWarnings("unchecked")
            final Map<String, Object> m = (Map<String, Object>) o;
            final String id = LanWire.str(m, "id");
            final String v = LanWire.str(m, "v");
            final String sha = LanWire.str(m, "sha");
            if (!isModId(id) || v == null || !v.matches("[0-9A-Za-z.+-]{1,32}") || sha == null
                || sha.length() > 64 || !LanWire.isHex(sha)) {
                return null;
            }
            return new GameplayMod(id, v, sha);
        }

        /** A message's list; null when it is not a sound one, empty when absent. */
        static List<GameplayMod> list(List<Object> l) {
            final List<GameplayMod> out = new ArrayList<GameplayMod>();
            if (l == null) {
                return out;
            }
            if (l.size() > GAMEPLAY_MODS_MAX) {
                return null;
            }
            for (Object o : l) {
                final GameplayMod g = from(o);
                if (g == null) {
                    return null;
                }
                out.add(g);
            }
            return out;
        }

        static List<Object> wire(List<GameplayMod> mods) {
            final List<Object> l = new ArrayList<Object>();
            for (GameplayMod g : mods) {
                l.add(g.toMap());
            }
            return l;
        }
    }

    /** A mod id: 3 to 40 of a-z 0-9 -. */
    static boolean isModId(String s) {
        return s != null && s.matches("[a-z0-9-]{3,40}");
    }

    /** "" (the original computer) or "<mod>:<id>" of a mod's computer player. */
    public static boolean isBot(String s) {
        if (s == null) {
            return false;
        }
        if (s.isEmpty()) {
            return true;
        }
        final int cut = s.indexOf(':');
        return s.length() <= BOT_MAX && cut > 0 && isModId(s.substring(0, cut)) && isModId(s.substring(cut + 1));
    }

    /** The mod of a computer player's key, or "" for the original computer. */
    public static String botMod(String bot) {
        final int cut = (bot == null) ? -1 : bot.indexOf(':');
        return (cut > 0) ? bot.substring(0, cut) : "";
    }

    public interface Maps {
        int NO = 0;
        int YES = 1;
        /** A file with that id is here but its content is another. */
        int DIFFERENT = 2;

        int find(String mapId, String hash);

        /** Keep a received map; the hash has been checked. False when it could not be kept. */
        boolean store(String mapId, String name, String hash, byte[] data);
    }

    /** One seat row. */
    public static final class Seat {
        public int index;
        public String kind = OPEN;
        public String id = "";
        public String name = "";
        /** The engine's name of this player. */
        public String netName = "";
        public int portrait;
        public String race = "map";
        public int colour;
        public int team;
        public boolean clash;
        public boolean ready;
        public boolean host;
        public String map = MAP_NONE;
        /** Per cent of the map received while map is "recv". */
        public int pct;
        /** A computer seat's player: "" the original's, else "<mod>:<id>" of a mod (owner decision 80). */
        public String bot = "";
        /** A person's seat has every mod the computer seats' players need, as the host has it. */
        public boolean modsOk = true;

        // Host side only.
        int peer = -1;
        long heldUntil;
        int mapTries;
        /** The gameplay mods the seat's device told in its hello. */
        List<GameplayMod> mods = new ArrayList<GameplayMod>();

        Seat(int index) {
            this.index = index;
            this.team = index + 1;
        }

        public boolean occupied() {
            return kind.equals(HUMAN) || kind.equals(COMPUTER) || kind.equals(HELD);
        }

        Seat copy() {
            final Seat s = new Seat(index);
            s.kind = kind;
            s.id = id;
            s.name = name;
            s.netName = netName;
            s.portrait = portrait;
            s.race = race;
            s.colour = colour;
            s.team = team;
            s.clash = clash;
            s.ready = ready;
            s.host = host;
            s.map = map;
            s.pct = pct;
            s.bot = bot;
            s.modsOk = modsOk;
            return s;
        }

        Map<String, Object> toMap() {
            return LanWire.msg(kind, "id", id, "name", name, "net", netName,
                "portrait", portrait, "race", race, "color", colour, "team", team,
                "clash", clash, "ready", ready, "host", host, "map", map, "pct", pct,
                "bot", bot, "mods_ok", modsOk);
        }
    }

    /** A line to send, or a connection to close after what was sent to it. */
    public static final class Out {
        public final int peer;
        /** Null for a close. */
        public final String line;
        public final boolean close;

        Out(int peer, String line, boolean close) {
            this.peer = peer;
            this.line = line;
            this.close = close;
        }
    }

    /** Something the view should tell the player. */
    public static final class Event {
        /** "joined", "refused", "closed", "deny", "start", "map_stored", "map_failed", "moved". */
        public final String kind;
        public final String detail;

        Event(String kind, String detail) {
            this.kind = kind;
            this.detail = detail;
        }
    }

    /** One chat line. A line of the room itself has seat -1 and a code. */
    public static final class Chat {
        public int n;
        public int seat;
        public String name = "";
        public int colour;
        public String text = "";
        public String code = "";
    }

    /** What the view draws. */
    public static final class Snapshot {
        public boolean host;
        public int phase;
        /** Why the room ended, when it has. */
        public String endReason = "";
        public int mySeat = -1;
        public String roomId = "";
        public String roomName = "";
        public int epoch;
        public int rev;
        public String mapId = "";
        public String mapName = "";
        public String mapHash = "";
        public long mapSize;
        public boolean mapSendable;
        /** How many players the map has places for; the seats from here on are closed. */
        public int mapPlayers = SEATS;
        public int resources;
        public int units;
        public int tileset;
        public boolean fog;
        public int reveal;
        public Seat[] seats;
        public String gate = GATE_PLAYERS;
        /** The seats the gate reason is about. */
        public int[] gateWho = new int[0];
        public boolean canStart;
        public List<Chat> chat;
        /** Per cent of the map this device has received, -1 when it is not receiving. */
        public int receiving = -1;
        /** The shared state as one line: equal on host and joiners when they agree. */
        public String canon = "";
        /** The host offers mods' computer players: computer seats show their player. */
        public boolean bots;
        /** The host's mods that the computer seats' players need; every person must have them. */
        public List<GameplayMod> need = new ArrayList<GameplayMod>();
    }

    /** The final table of a start: what every device builds its game request from. */
    public static final class StartTable {
        public static final class Row {
            public int seat;
            /** "human" or "computer". */
            public String kind;
            public String netName;
            /** "human", "orc" or "map": never "random". */
            public String race;
            public int colour;
            public int team;
            /** A computer row's player: "" the original's, else "<mod>:<id>". */
            public String bot = "";
        }

        public String room = "";
        public int epoch;
        public String nonce = "";
        /** The host's address as the host knows it; may be empty. */
        public String hostAddress = "";
        public int enginePort = LanWire.ENGINE_PORT;
        public String mapId = "";
        public String mapHash = "";
        public String mapName = "";
        public int resources;
        public int units;
        public int tileset;
        public boolean fog;
        public int reveal;
        public final List<Row> rows = new ArrayList<Row>();

        Map<String, Object> toMap() {
            final List<Object> seats = new ArrayList<Object>();
            for (Row r : rows) {
                final Map<String, Object> m = new LinkedHashMap<String, Object>();
                m.put("seat", r.seat);
                m.put("kind", r.kind);
                m.put("net", r.netName);
                m.put("race", r.race);
                m.put("color", r.colour);
                m.put("team", r.team);
                m.put("bot", r.bot);
                seats.add(m);
            }
            final Map<String, Object> map = new LinkedHashMap<String, Object>();
            map.put("id", mapId);
            map.put("hash", mapHash);
            map.put("name", mapName);
            final Map<String, Object> set = new LinkedHashMap<String, Object>();
            set.put("res", resources);
            set.put("units", units);
            set.put("tileset", tileset);
            set.put("fog", fog);
            set.put("reveal", reveal);
            return LanWire.msg("start", "room", room, "epoch", epoch, "nonce", nonce,
                "host", hostAddress, "port", enginePort, "map", map, "set", set, "seats", seats);
        }

        /** The table as its wire line; equal lines mean equal tables. */
        public String line() {
            return LanWire.encode(toMap());
        }

        /** The table of a "start" message, or null when the message is not a sound one. */
        static StartTable parse(Map<String, Object> m) {
            final StartTable t = new StartTable();
            t.room = token(LanWire.str(m, "room"), 64);
            t.epoch = LanWire.range(m, "epoch", 0, Integer.MAX_VALUE);
            t.nonce = LanWire.str(m, "nonce");
            t.hostAddress = address(LanWire.str(m, "host"));
            t.enginePort = LanWire.range(m, "port", 1, 65535);
            final Map<String, Object> map = LanWire.obj(m, "map");
            final Map<String, Object> set = LanWire.obj(m, "set");
            final List<Object> seats = LanWire.arr(m, "seats");
            if (t.room.isEmpty() || t.epoch < 0 || t.nonce == null || !LanWire.isNonce(t.nonce)
                || t.enginePort < 1 || map == null || set == null || seats == null
                || seats.size() < 2 || seats.size() > SEATS) {
                return null;
            }
            t.mapId = LanWire.str(map, "id");
            t.mapHash = LanWire.str(map, "hash");
            t.mapName = LanWire.str(map, "name");
            t.resources = LanWire.range(set, "res", -1, 1000000);
            t.units = LanWire.range(set, "units", -1, 1000000);
            t.tileset = LanWire.range(set, "tileset", -1, 1000000);
            final Boolean fog = LanWire.flag(set, "fog");
            t.reveal = LanWire.range(set, "reveal", 0, 2);
            if (t.mapId == null || t.mapId.isEmpty() || t.mapId.length() > 200 || t.mapHash == null
                || t.mapHash.length() > 64 || !LanWire.isHex(t.mapHash) || t.mapName == null
                || t.resources < -1 || t.units < -1 || t.tileset < -1 || fog == null
                || t.reveal < 0) {
                return null;
            }
            t.fog = fog.booleanValue();
            final Set<Integer> colours = new HashSet<Integer>();
            final Set<String> names = new HashSet<String>();
            int last = -1;
            for (Object o : seats) {
                if (!(o instanceof Map)) {
                    return null;
                }
                @SuppressWarnings("unchecked")
                final Map<String, Object> s = (Map<String, Object>) o;
                final Row r = new Row();
                r.seat = LanWire.range(s, "seat", 0, SEATS - 1);
                r.kind = LanWire.str(s, "kind");
                r.netName = LanWire.str(s, "net");
                r.race = LanWire.str(s, "race");
                r.colour = LanWire.range(s, "color", 0, COLOURS - 1);
                r.team = LanWire.range(s, "team", 1, TEAMS);
                r.bot = LanWire.str(s, "bot");
                final boolean human = HUMAN.equals(r.kind);
                if (r.bot == null || !isBot(r.bot) || (human && !r.bot.isEmpty())) {
                    return null;
                }
                if (r.seat <= last || r.colour < 0 || r.team < 1 || r.netName == null
                    || !(human || COMPUTER.equals(r.kind))
                    || !("human".equals(r.race) || "orc".equals(r.race) || "map".equals(r.race))
                    || !colours.add(Integer.valueOf(r.colour))
                    || (human && (r.netName.isEmpty() || !r.netName.equals(netName(r.netName))
                                  || !names.add(r.netName.toLowerCase(java.util.Locale.ROOT))))) {
                    return null;
                }
                last = r.seat;
                t.rows.add(r);
            }
            return t;
        }

        public Row rowOf(int seat) {
            for (Row r : rows) {
                if (r.seat == seat) {
                    return r;
                }
            }
            return null;
        }

        public int humans() {
            int n = 0;
            for (Row r : rows) {
                if (HUMAN.equals(r.kind)) {
                    ++n;
                }
            }
            return n;
        }

        private static String lua(String s) {
            return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
        }

        private String tail() {
            return "  timeout = 60,\n"
                + "  room = { id = " + lua(room) + ", epoch = " + epoch + " },\n"
                + "  window = true,\n";
        }

        /**
         * The fields of the host's game request (mode lan_host), without the opening line, the
         * options block, the player's name and the token: those are added where every request
         * is written.
         */
        public String hostRequest() {
            final StringBuilder r = new StringBuilder();
            r.append("  mode = \"lan_host\",\n");
            r.append("  map = ").append(lua(mapId)).append(",\n");
            r.append("  map_hash = ").append(lua(mapHash)).append(",\n");
            r.append("  port = ").append(enginePort).append(",\n");
            for (int pass = 0; pass < 2; ++pass) {
                r.append(pass == 0 ? "  players = {\n" : "  computers = {\n");
                for (Row row : rows) {
                    if (HUMAN.equals(row.kind) != (pass == 0)) {
                        continue;
                    }
                    r.append("    { ");
                    if (pass == 0) {
                        r.append("name = ").append(lua(row.netName)).append(", ");
                    }
                    r.append("race = \"").append(row.race).append("\", color = ")
                        .append(row.colour).append(", team = ").append(row.team);
                    if (pass == 1 && !row.bot.isEmpty()) {
                        // A computer player of a mod (launch.lua: the seat's AIScript).
                        r.append(", bot = ").append(lua(row.bot));
                    }
                    r.append(" },\n");
                }
                r.append("  },\n");
            }
            r.append("  resources = ").append(resources).append(",\n");
            r.append("  units = ").append(units).append(",\n");
            r.append("  tileset = ").append(tileset).append(",\n");
            r.append("  fog = ").append(fog).append(",\n");
            r.append("  reveal = ").append(reveal).append(",\n");
            return r.append(tail()).toString();
        }

        /**
         * The fields of a joiner's game request (mode lan_join) for the player in the given
         * seat. The address is the one this device reached the room at; the table's own is
         * used only when none is given.
         */
        public String joinRequest(int seat, String hostSeenAt) {
            final Row me = rowOf(seat);
            if (me == null || !HUMAN.equals(me.kind)) {
                return null;
            }
            final String at = address(hostSeenAt);
            final StringBuilder r = new StringBuilder();
            r.append("  mode = \"lan_join\",\n");
            r.append("  host = ").append(lua(at.isEmpty() ? hostAddress : at)).append(",\n");
            r.append("  port = ").append(enginePort).append(",\n");
            r.append("  race = \"").append(me.race).append("\",\n");
            r.append("  expect = { map = ").append(lua(mapId)).append(", map_hash = ")
                .append(lua(mapHash)).append(", color = ").append(me.colour).append(", team = ")
                .append(me.team).append(", players = ").append(humans()).append(" },\n");
            return r.append(tail()).toString();
        }
    }

    private static final class Peer {
        final int id;
        boolean admitted;
        long lastHeard;
        long lastPing;
        LanWire.MapOut xfer;

        Peer(int id, long now) {
            this.id = id;
            this.lastHeard = now;
            this.lastPing = now;
        }
    }

    // ---- state ------------------------------------------------------------------------------

    private final boolean host;
    private final LanIdentity identity;
    private final Profile me;
    private final Maps maps;

    private int phase;
    private String endReason = "";
    private String roomId = "";
    private String roomName = "";
    private int epoch;
    private int rev;
    private final Seat[] seats = new Seat[SEATS];
    private int mySeat = -1;

    private String mapId = "";
    private String mapName = "";
    private String mapHash = "";
    private long mapSize;
    private boolean mapSendable;
    /** The places of the map, MIN_PLAYERS to SEATS; SEATS while nobody has said. */
    private int mapPlayers = SEATS;
    private byte[] mapData;
    private int resources = -1;
    private int units = -1;
    private int tileset = -1;
    private boolean fog = true;
    private int reveal;
    private LanIdentity hostIdentity;
    /** This device's gameplay mods (setGameplayMods); a joiner tells them in its hello. */
    private List<GameplayMod> ownMods = new ArrayList<GameplayMod>();
    /** The host offers mods' computer players (setBotsOffered); a joiner reads it from the state. */
    private boolean botsOffered;
    /** The host's mods the computer seats' players need (host: worked out in gate()). */
    private List<GameplayMod> need = new ArrayList<GameplayMod>();

    private String gate = GATE_PLAYERS;
    private int[] gateWho = new int[0];

    private final List<Chat> chat = new ArrayList<Chat>();
    private int chatSeq;
    private final List<Out> out = new ArrayList<Out>();
    private final List<Event> events = new ArrayList<Event>();
    private StartTable started;

    // Host side.
    private final Map<Integer, Peer> peers = new LinkedHashMap<Integer, Peer>();
    private boolean dirty;
    private int xferSeq;

    // Joiner side.
    private boolean connected;
    private long lastHeard;
    private long lastPing;
    private String rejoinRoom = "";
    private int rejoinEpoch = -1;
    private String reportedHash = "";
    private LanWire.MapIn rx;

    /** Lines that were not understood. */
    public int badLines;
    /** Errors caught at the room's edge; always 0 unless the room has a fault. */
    public int faults;

    private LanRoom(boolean host, LanIdentity identity, Profile me, Maps maps) {
        this.host = host;
        this.identity = identity;
        this.me = me;
        this.maps = maps;
        for (int i = 0; i < SEATS; ++i) {
            seats[i] = new Seat(i);
        }
    }

    /** A new room on the host. The room id is any short token that is new every time. */
    public static LanRoom host(String roomId, LanIdentity identity, Profile me) {
        final LanRoom r = new LanRoom(true, identity, me, null);
        r.roomId = token(roomId, 64);
        r.hostIdentity = identity;
        final String name = clean(me.name, NAME_MAX);
        r.roomName = name + "'s game";
        final Seat s = r.seats[0];
        s.kind = HUMAN;
        s.host = true;
        s.id = token(me.id, 40);
        s.name = name;
        s.netName = netName(name);
        s.portrait = Math.max(0, Math.min(255, me.portrait));
        s.race = race(me.race);
        s.colour = (me.colour >= 0 && me.colour < COLOURS) ? me.colour : 0;
        s.map = MAP_HAS;
        r.mySeat = 0;
        r.phase = LOBBY;
        r.gate();
        return r;
    }

    /** A joiner's room; it starts to live when its connection to the host is reported. */
    public static LanRoom client(LanIdentity identity, Profile me, Maps maps) {
        return new LanRoom(false, identity, me, maps);
    }

    /**
     * This device's switched-on gameplay mods for network games, as { id, version, SHA-256 of
     * manifest.json }. Call before the connection stands (a joiner sends them in its hello); on
     * the host, before any computer seat plays a mod's player.
     */
    public void setGameplayMods(List<String[]> mods) {
        final List<GameplayMod> l = new ArrayList<GameplayMod>();
        for (int i = 0; mods != null && i < mods.size() && l.size() < GAMEPLAY_MODS_MAX; ++i) {
            final String[] m = mods.get(i);
            if (m != null && m.length == 3 && isModId(m[0]) && m[1] != null
                && m[1].matches("[0-9A-Za-z.+-]{1,32}") && m[2] != null && m[2].length() <= 64
                && LanWire.isHex(m[2])) {
                l.add(new GameplayMod(m[0], m[1], m[2]));
            }
        }
        ownMods = l;
        if (host) {
            seats[0].mods = l;
            dirty = true;
            flush();
        }
    }

    /** Host: whether the computer seats may play mods' computer players (the host has such a mod on). */
    public void setBotsOffered(boolean offered) {
        if (!host || botsOffered == offered) {
            return;
        }
        botsOffered = offered;
        if (!offered) {
            for (Seat s : seats) {
                s.bot = "";
            }
        }
        dirty = true;
        flush();
    }

    /** Joiner, before connecting: ask for the seat held in this room after a game. */
    public void rejoin(String room, int roomEpoch) {
        rejoinRoom = token(room, 64);
        rejoinEpoch = roomEpoch;
    }

    // ---- cleaning ---------------------------------------------------------------------------

    /** Text without control characters and broken pairs, trimmed, at most max characters. */
    static String clean(String s, int max) {
        if (s == null) {
            return "";
        }
        final StringBuilder b = new StringBuilder();
        for (int i = 0; i < s.length(); ++i) {
            final char c = s.charAt(i);
            if (Character.isHighSurrogate(c)) {
                if (i + 1 < s.length() && Character.isLowSurrogate(s.charAt(i + 1))) {
                    b.append(c).append(s.charAt(++i));
                }
            } else if (c >= 0x20 && c != 0x7f && !Character.isLowSurrogate(c)
                       && c != 0x2028 && c != 0x2029) {
                b.append(c);
            }
        }
        return LanWire.cut(b.toString().trim(), max).trim();
    }

    /** The engine's name for a shown name: at most 15 bytes, cut between characters. */
    public static String netName(String name) {
        final String s = clean(name, NAME_MAX);
        int end = 0;
        int bytes = 0;
        while (end < s.length()) {
            final int cp = s.codePointAt(end);
            final int n = (cp < 0x80) ? 1 : (cp < 0x800) ? 2 : (cp < 0x10000) ? 3 : 4;
            if (bytes + n > NET_NAME_BYTES) {
                break;
            }
            bytes += n;
            end += Character.charCount(cp);
        }
        return s.substring(0, end).trim();
    }

    /** A plain token (letters, digits, "_", "-", "."), or "" when the text is not one. */
    static String token(String s, int max) {
        if (s == null || s.isEmpty() || s.length() > max) {
            return "";
        }
        for (int i = 0; i < s.length(); ++i) {
            final char c = s.charAt(i);
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                  || c == '_' || c == '-' || c == '.')) {
                return "";
            }
        }
        return s;
    }

    /** An IPv4 address in digits and dots, or "". */
    static String address(String s) {
        if (s == null || s.length() < 7 || s.length() > 15) {
            return "";
        }
        for (int i = 0; i < s.length(); ++i) {
            final char c = s.charAt(i);
            if (!((c >= '0' && c <= '9') || c == '.')) {
                return "";
            }
        }
        return s;
    }

    private static String race(String s) {
        for (String r : RACES) {
            if (r.equals(s)) {
                return r;
            }
        }
        return "map";
    }

    private static boolean isRace(String s) {
        for (String r : RACES) {
            if (r.equals(s)) {
                return true;
            }
        }
        return false;
    }

    // ---- what the owner takes ---------------------------------------------------------------

    /** The lines to send and the connections to close, in order. */
    public List<Out> takeOut() {
        final List<Out> l = new ArrayList<Out>(out);
        out.clear();
        return l;
    }

    public List<Event> takeEvents() {
        final List<Event> l = new ArrayList<Event>(events);
        events.clear();
        return l;
    }

    public boolean isHost() {
        return host;
    }

    public int phase() {
        return phase;
    }

    public String endReason() {
        return endReason;
    }

    /** The final table once the game has been started, else null. */
    public StartTable startTable() {
        return started;
    }

    /** True while the room may be shown to devices that look for rooms. */
    public boolean discoverable() {
        return host && phase == LOBBY;
    }

    /** What the room answers to a discovery probe. */
    public LanWire.RoomInfo roomInfo(int port) {
        final LanWire.RoomInfo r = new LanWire.RoomInfo();
        r.roomId = roomId;
        r.epoch = epoch;
        r.hostName = seats[0].name;
        r.portrait = seats[0].portrait;
        r.colour = seats[0].colour;
        r.mapName = mapName;
        r.mapHash = LanWire.cut(mapHash, 16);
        r.seatsMax = mapPlayers;
        for (Seat s : seats) {
            if (s.occupied()) {
                ++r.seatsUsed;
            }
            if (!s.kind.equals(CLOSED)) {
                ++r.seatsTotal;
            }
        }
        r.identity = identity;
        r.port = port;
        return r;
    }

    private Map<String, Object> stateMap() {
        final Map<String, Object> map = new LinkedHashMap<String, Object>();
        map.put("id", mapId);
        map.put("name", mapName);
        map.put("hash", mapHash);
        map.put("size", mapSize);
        map.put("send", mapSendable);
        map.put("players", mapPlayers);
        final Map<String, Object> set = new LinkedHashMap<String, Object>();
        set.put("res", resources);
        set.put("units", units);
        set.put("tileset", tileset);
        set.put("fog", fog);
        set.put("reveal", reveal);
        final Map<String, Object> ident = new LinkedHashMap<String, Object>();
        if (hostIdentity != null) {
            hostIdentity.into(ident);
        }
        final List<Object> slots = new ArrayList<Object>();
        for (Seat s : seats) {
            final Map<String, Object> m = s.toMap();
            // toMap puts the kind where a message has its type.
            m.put("kind", m.remove("t"));
            slots.add(m);
        }
        final List<Object> who = new ArrayList<Object>();
        for (int w : gateWho) {
            who.add(Integer.valueOf(w));
        }
        return LanWire.msg("state", "room", roomId, "epoch", epoch, "rev", rev,
            "name", roomName, "ident", ident, "map", map, "set", set, "slots", slots,
            "gate", gate, "who", who, "bots", botsOffered, "need", GameplayMod.wire(need));
    }

    public Snapshot snapshot() {
        final Snapshot s = new Snapshot();
        s.host = host;
        s.phase = phase;
        s.endReason = endReason;
        s.mySeat = mySeat;
        s.roomId = roomId;
        s.roomName = roomName;
        s.epoch = epoch;
        s.rev = rev;
        s.mapId = mapId;
        s.mapName = mapName;
        s.mapHash = mapHash;
        s.mapSize = mapSize;
        s.mapSendable = mapSendable;
        s.mapPlayers = mapPlayers;
        s.resources = resources;
        s.units = units;
        s.tileset = tileset;
        s.fog = fog;
        s.reveal = reveal;
        s.seats = new Seat[SEATS];
        for (int i = 0; i < SEATS; ++i) {
            s.seats[i] = seats[i].copy();
        }
        s.gate = gate;
        s.gateWho = gateWho.clone();
        s.canStart = host && phase == LOBBY && GATE_OK.equals(gate);
        s.chat = new ArrayList<Chat>(chat);
        s.receiving = (rx == null) ? -1 : rx.percent();
        s.canon = LanWire.encode(stateMap());
        s.bots = botsOffered;
        s.need = new ArrayList<GameplayMod>(need);
        return s;
    }

    // ---- small helpers ----------------------------------------------------------------------

    private void send(int peer, Map<String, Object> m) {
        out.add(new Out(peer, LanWire.encode(m), false));
    }

    private void sendLine(int peer, String line) {
        out.add(new Out(peer, line, false));
    }

    private void close(int peer) {
        out.add(new Out(peer, null, true));
    }

    private void toAll(String line) {
        for (Peer p : peers.values()) {
            if (p.admitted) {
                sendLine(p.id, line);
            }
        }
    }

    private Seat seatOfPeer(int peer) {
        for (Seat s : seats) {
            if (s.kind.equals(HUMAN) && s.peer == peer) {
                return s;
            }
        }
        return null;
    }

    private Seat holder(int colour, Seat except) {
        for (Seat s : seats) {
            if (s != except && s.occupied() && s.colour == colour) {
                return s;
            }
        }
        return null;
    }

    private int freeColour(Seat except) {
        for (int c = 0; c < COLOURS; ++c) {
            if (holder(c, except) == null) {
                return c;
            }
        }
        return 0;
    }

    private void free(Seat s) {
        final Seat fresh = new Seat(s.index);
        seats[s.index] = fresh;
        dirty = true;
    }

    private void end(String reason) {
        if (phase != ENDED) {
            phase = ENDED;
            endReason = reason;
            rx = null;
            events.add(new Event("closed", reason));
        }
    }

    private void addChat(Chat c) {
        chat.add(c);
        while (chat.size() > CHAT_TAIL) {
            chat.remove(0);
        }
    }

    // ---- the gate ---------------------------------------------------------------------------

    /** A clash mark stays only while somebody else holds the colour; equal colours are marked. */
    private void fixClash() {
        for (Seat s : seats) {
            if (!s.occupied() || holder(s.colour, s) == null) {
                s.clash = false;
            }
        }
        for (Seat s : seats) {
            if (!s.occupied() || s.clash) {
                continue;
            }
            boolean marked = false;
            Seat first = s;
            for (Seat o : seats) {
                if (o != s && o.occupied() && o.colour == s.colour) {
                    marked |= o.clash;
                    if (o.index < first.index) {
                        first = o;
                    }
                }
            }
            // Nobody of an equal group is marked: all but the first are.
            if (!marked && first != s) {
                s.clash = true;
            }
        }
        for (Seat s : seats) {
            if (s.clash) {
                s.ready = false;
            }
        }
    }

    /**
     * Host: the mods the computer seats' players need, and for every person whether his device
     * has each of them with the host's version and manifest (LAW 18.6). A person without them is
     * not ready. With no computer seat on a mod's player nothing is needed: the mods then do not
     * stand between anybody and the game.
     */
    private void fixMods() {
        final List<GameplayMod> wanted = new ArrayList<GameplayMod>();
        boolean hostLacks = false;
        for (Seat s : seats) {
            if (!s.kind.equals(COMPUTER) || s.index >= mapPlayers || s.bot.isEmpty()) {
                continue;
            }
            final String mod = botMod(s.bot);
            GameplayMod mine = null;
            for (GameplayMod g : ownMods) {
                if (g.id.equals(mod)) {
                    mine = g;
                }
            }
            if (mine == null) {
                hostLacks = true;
                continue;
            }
            boolean listed = false;
            for (GameplayMod g : wanted) {
                listed |= g.id.equals(mine.id);
            }
            if (!listed) {
                wanted.add(mine);
            }
        }
        need = wanted;
        for (Seat s : seats) {
            boolean ok = true;
            if (s.kind.equals(HUMAN)) {
                if (s.host) {
                    ok = !hostLacks;
                } else {
                    for (GameplayMod g : wanted) {
                        boolean has = false;
                        for (GameplayMod h : s.mods) {
                            has |= g.same(h);
                        }
                        ok &= has;
                    }
                }
            }
            s.modsOk = ok;
            if (!ok) {
                s.ready = false;
            }
        }
    }

    private void gate() {
        fixClash();
        fixMods();
        final List<Integer> who = new ArrayList<Integer>();
        String g = GATE_OK;
        int people = 0;
        final Set<Integer> teams = new HashSet<Integer>();
        for (Seat s : seats) {
            if (s.kind.equals(HUMAN) || s.kind.equals(HELD)) {
                ++people;
            }
            if (s.occupied()) {
                teams.add(Integer.valueOf(s.team));
            }
        }
        // Two people at the least, computers or not: a host alone does not start (owner decision 91, 3A).
        if (people < 2) {
            g = GATE_PLAYERS;
        }
        if (g.equals(GATE_OK)) {
            for (Seat s : seats) {
                if (s.kind.equals(HUMAN) && !s.host && !s.map.equals(MAP_HAS)) {
                    who.add(Integer.valueOf(s.index));
                }
            }
            if (mapHash.isEmpty() || !who.isEmpty()) {
                g = GATE_MAP;
            }
        }
        if (g.equals(GATE_OK)) {
            for (Seat s : seats) {
                if (s.occupied() && (s.clash || holder(s.colour, s) != null && s.clash)) {
                    who.add(Integer.valueOf(s.index));
                }
            }
            if (!who.isEmpty()) {
                g = GATE_COLOUR;
            }
        }
        if (g.equals(GATE_OK)) {
            for (Seat s : seats) {
                if (s.kind.equals(HUMAN) && !s.modsOk) {
                    who.add(Integer.valueOf(s.index));
                }
            }
            if (!who.isEmpty()) {
                g = GATE_BOTS;
            }
        }
        if (g.equals(GATE_OK)) {
            for (Seat s : seats) {
                if ((s.kind.equals(HUMAN) && !s.host && !s.ready) || s.kind.equals(HELD)) {
                    who.add(Integer.valueOf(s.index));
                }
            }
            if (!who.isEmpty()) {
                g = GATE_READY;
            }
        }
        if (g.equals(GATE_OK) && teams.size() < 2) {
            g = GATE_TEAMS;
        }
        gate = g;
        gateWho = new int[who.size()];
        for (int i = 0; i < gateWho.length; ++i) {
            gateWho[i] = who.get(i).intValue();
        }
    }

    /** Host: after a change, one new state to everybody. */
    private void flush() {
        if (!host || !dirty) {
            return;
        }
        dirty = false;
        if (phase != LOBBY) {
            return;
        }
        gate();
        ++rev;
        toAll(LanWire.encode(stateMap()));
    }

    // ---- events from the wire ---------------------------------------------------------------

    /** Host: somebody connected. Joiner: the connection to the host stands (peer HOST_PEER). */
    public void peerConnected(int peer, long now) {
        if (host) {
            if (phase == ENDED || peers.containsKey(Integer.valueOf(peer))) {
                return;
            }
            peers.put(Integer.valueOf(peer), new Peer(peer, now));
            return;
        }
        if (connected || phase != CONNECTING) {
            return;
        }
        connected = true;
        lastHeard = now;
        lastPing = now;
        final Map<String, Object> m = LanWire.msg("hello", "v", LanWire.PROTOCOL);
        identity.into(m);
        m.put("id", token(me.id, 40));
        m.put("name", clean(me.name, NAME_MAX));
        m.put("portrait", Math.max(0, Math.min(255, me.portrait)));
        m.put("race", race(me.race));
        m.put("color", me.colour);
        // The gameplay mods of this device (protocol 2): the host compares them before Ready.
        m.put("gm", GameplayMod.wire(ownMods));
        if (!rejoinRoom.isEmpty()) {
            m.put("room", rejoinRoom);
            m.put("epoch", rejoinEpoch);
        }
        send(HOST_PEER, m);
    }

    /** One line arrived from a peer. Nothing a peer sends can throw out of here. */
    public void peerLine(int peer, String line, long now) {
        try {
            final Map<String, Object> m = LanWire.decode(line);
            final String t = LanWire.str(m, "t");
            if (t == null) {
                ++badLines;
                return;
            }
            if (host) {
                hostLine(peer, t, m, now);
                flush();
            } else if (peer == HOST_PEER && connected) {
                clientLine(t, m, now);
            }
        } catch (RuntimeException e) {
            ++faults;
        }
    }

    /** The connection to a peer is gone. */
    public void peerLost(int peer, long now) {
        if (host) {
            final Peer p = peers.remove(Integer.valueOf(peer));
            if (p != null && p.admitted) {
                final Seat s = seatOfPeer(peer);
                if (s != null) {
                    free(s);
                }
            }
            flush();
        } else if (peer == HOST_PEER && phase < STARTED) {
            end("host_lost");
        }
    }

    /** Time passes: pings, silent peers, held seats. Call a few times a second. */
    public void tick(long now) {
        if (host) {
            if (phase != LOBBY) {
                return;
            }
            for (Peer p : new ArrayList<Peer>(peers.values())) {
                final long limit = p.admitted ? SILENCE_MS : HELLO_MS;
                if (now - p.lastHeard > limit) {
                    peers.remove(Integer.valueOf(p.id));
                    final Seat s = p.admitted ? seatOfPeer(p.id) : null;
                    if (s != null) {
                        free(s);
                    }
                    close(p.id);
                } else if (p.admitted && now - p.lastPing >= PING_MS) {
                    p.lastPing = now;
                    send(p.id, LanWire.msg("ping"));
                }
            }
            for (Seat s : seats) {
                if (s.kind.equals(HELD) && now >= s.heldUntil) {
                    free(s);
                }
            }
            flush();
            return;
        }
        if (!connected || phase >= STARTED) {
            return;
        }
        if (now - lastHeard > SILENCE_MS) {
            close(HOST_PEER);
            end("host_lost");
        } else if (now - lastPing >= PING_MS) {
            lastPing = now;
            send(HOST_PEER, LanWire.msg("ping"));
        }
    }

    // ---- host: lines ------------------------------------------------------------------------

    private void refuse(Peer p, String reason) {
        send(p.id, LanWire.msg("refuse", "reason", reason));
        close(p.id);
        peers.remove(Integer.valueOf(p.id));
    }

    private void hostLine(int peer, String t, Map<String, Object> m, long now) {
        final Peer p = peers.get(Integer.valueOf(peer));
        if (p == null || phase != LOBBY) {
            return;
        }
        if (p.admitted || t.equals("hello")) {
            p.lastHeard = now;
        }
        if (!p.admitted) {
            if (t.equals("hello")) {
                hello(p, m);
            }
            return;
        }
        final Seat s = seatOfPeer(peer);
        if (s == null) {
            return;
        }
        if (t.equals("ping")) {
            send(peer, LanWire.msg("pong"));
        } else if (t.equals("color")) {
            final int want = LanWire.range(m, "want", 0, COLOURS - 1);
            if (want < 0 || s.ready || (want == s.colour && !s.clash)) {
                return;
            }
            final Seat h = holder(want, s);
            if (h != null) {
                send(peer, LanWire.msg("deny", "want", want, "held_by", h.index));
                return;
            }
            s.colour = want;
            s.clash = false;
            dirty = true;
        } else if (t.equals("team")) {
            final int want = LanWire.range(m, "want", 1, TEAMS);
            if (want >= 1 && !s.ready && want != s.team) {
                s.team = want;
                dirty = true;
            }
        } else if (t.equals("race")) {
            final String want = LanWire.str(m, "want");
            if (isRace(want) && !s.ready && !want.equals(s.race)) {
                s.race = want;
                dirty = true;
            }
        } else if (t.equals("ready")) {
            final Boolean on = LanWire.flag(m, "on");
            if (on == null || on.booleanValue() == s.ready) {
                return;
            }
            // Not with a clashing colour, not before the map is here, and not without the mods
            // the computer seats' players need.
            if (on.booleanValue() && (s.clash || !s.map.equals(MAP_HAS) || !s.modsOk)) {
                return;
            }
            s.ready = on.booleanValue();
            dirty = true;
        } else if (t.equals("say")) {
            say(s, LanWire.str(m, "text"));
        } else if (t.equals("leave")) {
            peers.remove(Integer.valueOf(peer));
            free(s);
            close(peer);
        } else if (t.equals("map")) {
            mapReport(p, s, m);
        } else if (t.equals("map_ack")) {
            mapAck(p, s, m);
        }
    }

    private void hello(Peer p, Map<String, Object> m) {
        final long v = LanWire.num(m, "v", -1);
        if (v < 0) {
            refuse(p, REFUSE_HELLO);
            return;
        }
        if (v != LanWire.PROTOCOL) {
            refuse(p, LanIdentity.PROTOCOL);
            return;
        }
        final LanIdentity theirs = LanIdentity.from(m);
        final List<GameplayMod> theirMods = GameplayMod.list(LanWire.arr(m, "gm"));
        if (theirs == null || theirMods == null) {
            refuse(p, REFUSE_HELLO);
            return;
        }
        final String differs = identity.differsFrom(theirs);
        if (differs != null) {
            refuse(p, differs);
            return;
        }
        final String id = token(LanWire.str(m, "id"), 40);
        final String name = clean(LanWire.str(m, "name"), NAME_MAX);
        final String net = netName(name);
        if (net.isEmpty()) {
            refuse(p, REFUSE_NAME);
            return;
        }
        // A returning player gets the seat that was kept for him.
        Seat held = null;
        if (!id.isEmpty() && roomId.equals(LanWire.str(m, "room"))
            && LanWire.num(m, "epoch", -1) == epoch) {
            for (Seat s : seats) {
                if (s.kind.equals(HELD) && s.id.equals(id)) {
                    held = s;
                    break;
                }
            }
        }
        for (Seat s : seats) {
            if (s != held && (s.kind.equals(HUMAN) || s.kind.equals(HELD))
                && s.netName.equalsIgnoreCase(net)) {
                refuse(p, REFUSE_NAME_TAKEN);
                return;
            }
        }
        Seat s = held;
        if (s == null) {
            for (Seat o : seats) {
                if (o.kind.equals(OPEN)) {
                    s = o;
                    break;
                }
            }
            if (s == null) {
                refuse(p, REFUSE_FULL);
                return;
            }
            s.id = id;
            s.portrait = Math.max(0, LanWire.range(m, "portrait", 0, 255));
            s.race = race(LanWire.str(m, "race"));
            final int colour = LanWire.range(m, "color", 0, COLOURS - 1);
            // The seat takes the colour it brings; a colour somebody holds is a clash.
            s.colour = (colour < 0) ? freeColour(s) : colour;
            s.clash = holder(s.colour, s) != null;
        }
        s.name = name;
        s.netName = net;
        s.kind = HUMAN;
        s.peer = p.id;
        s.mods = theirMods;
        s.ready = false;
        s.map = MAP_NONE;
        s.pct = 0;
        s.mapTries = 0;
        s.heldUntil = 0;
        p.admitted = true;
        send(p.id, LanWire.msg("welcome", "seat", s.index, "room", roomId, "epoch", epoch,
                               "net", net));
        dirty = true;
    }

    private void say(Seat s, String text) {
        final String clean = clean(text, CHAT_MAX);
        if (clean.isEmpty()) {
            return;
        }
        final Chat c = new Chat();
        c.n = ++chatSeq;
        c.seat = s.index;
        c.name = s.name;
        c.colour = s.colour;
        c.text = clean;
        addChat(c);
        toAll(LanWire.encode(LanWire.msg("say", "n", c.n, "seat", c.seat, "name", c.name,
                                         "color", c.colour, "text", c.text, "code", "")));
    }

    /** A line of the room itself, like "<name> received the map". */
    private void note(String code, String name) {
        final Chat c = new Chat();
        c.n = ++chatSeq;
        c.seat = -1;
        c.name = name;
        c.code = code;
        addChat(c);
        toAll(LanWire.encode(LanWire.msg("say", "n", c.n, "seat", -1, "name", name,
                                         "color", 0, "text", "", "code", code)));
    }

    private void mapReport(Peer p, Seat s, Map<String, Object> m) {
        final String hash = LanWire.str(m, "hash");
        final String have = LanWire.str(m, "have");
        if (hash == null || have == null || mapHash.isEmpty() || !hash.equals(mapHash)) {
            return;
        }
        final String before = s.map;
        if (have.equals("yes")) {
            if (p.xfer != null && p.xfer.doneSent) {
                note("map_received", s.name);
            }
            p.xfer = null;
            s.map = MAP_HAS;
            s.pct = 0;
        } else if (have.equals("no") || have.equals("diff")) {
            s.ready = false;
            p.xfer = null;
            s.pct = 0;
            if (mapData != null && s.mapTries < MAP_TRIES) {
                ++s.mapTries;
                s.map = MAP_RECV;
                p.xfer = new LanWire.MapOut(++xferSeq, mapName, mapHash, mapData);
                sendLine(p.id, p.xfer.offer());
                for (int i = 0; i < WINDOW; ++i) {
                    final String chunk = p.xfer.nextChunk();
                    if (chunk == null) {
                        break;
                    }
                    sendLine(p.id, chunk);
                }
            } else {
                s.map = have.equals("diff") ? MAP_DIFF : MAP_NONE;
            }
        } else {
            return;
        }
        dirty |= !before.equals(s.map) || s.map.equals(MAP_RECV);
    }

    private void mapAck(Peer p, Seat s, Map<String, Object> m) {
        final LanWire.MapOut x = p.xfer;
        if (x == null || LanWire.num(m, "x", -1) != x.xid) {
            return;
        }
        final long off = LanWire.num(m, "off", -1);
        if (off <= x.acked || off > x.sent) {
            return;
        }
        x.acked = (int) off;
        final String chunk = x.nextChunk();
        if (chunk != null) {
            sendLine(p.id, chunk);
        }
        if (x.acked == x.size() && !x.doneSent) {
            sendLine(p.id, x.done());
        }
        // The row's number moves in steps of five.
        final int pct = (int) ((long) x.acked * 100 / x.size()) / 5 * 5;
        if (pct != s.pct) {
            s.pct = pct;
            dirty = true;
        }
    }

    // ---- joiner: lines ----------------------------------------------------------------------

    private void clientLine(String t, Map<String, Object> m, long now) {
        if (phase >= STARTED) {
            return;
        }
        lastHeard = now;
        if (t.equals("ping")) {
            send(HOST_PEER, LanWire.msg("pong"));
        } else if (t.equals("refuse")) {
            final String reason = reason(LanWire.str(m, "reason"));
            events.add(new Event("refused", reason));
            close(HOST_PEER);
            end("refused:" + reason);
        } else if (t.equals("closed")) {
            close(HOST_PEER);
            end(reason(LanWire.str(m, "reason")));
        } else if (t.equals("welcome")) {
            final int seat = LanWire.range(m, "seat", 1, SEATS - 1);
            if (phase == CONNECTING && seat >= 1) {
                mySeat = seat;
                phase = LOBBY;
                events.add(new Event("joined", String.valueOf(seat)));
            }
        } else if (phase != LOBBY) {
            return;
        } else if (t.equals("moved")) {
            // The map has fewer places now and the host gave this player another seat.
            final int seat = LanWire.range(m, "seat", 1, SEATS - 1);
            if (seat >= 1 && seat != mySeat) {
                mySeat = seat;
                events.add(new Event("moved", String.valueOf(seat)));
            }
        } else if (t.equals("state")) {
            state(m);
        } else if (t.equals("deny")) {
            final int want = LanWire.range(m, "want", 0, COLOURS - 1);
            final int by = LanWire.range(m, "held_by", 0, SEATS - 1);
            if (want >= 0 && by >= 0) {
                events.add(new Event("deny", want + ":" + by));
            }
        } else if (t.equals("say")) {
            final Chat c = new Chat();
            c.n = LanWire.range(m, "n", 1, Integer.MAX_VALUE);
            c.seat = LanWire.range(m, "seat", -1, SEATS - 1);
            c.name = clean(LanWire.str(m, "name"), NAME_MAX);
            c.colour = Math.max(0, LanWire.range(m, "color", 0, COLOURS - 1));
            c.text = clean(LanWire.str(m, "text"), CHAT_MAX);
            c.code = token(LanWire.str(m, "code"), 24);
            // Lines come in the host's order; an old number is not shown twice.
            if (c.n > chatSeq && c.seat >= -1 && !(c.text.isEmpty() && c.code.isEmpty())) {
                chatSeq = c.n;
                addChat(c);
            }
        } else if (t.equals("map_offer")) {
            final LanWire.MapIn in = LanWire.MapIn.fromOffer(m);
            rx = (in != null && in.hash.equals(mapHash) && !mapHash.isEmpty()) ? in : null;
        } else if (t.equals("map_chunk")) {
            if (rx == null || LanWire.num(m, "x", -1) != rx.xid) {
                return;
            }
            if (rx.chunk(m)) {
                send(HOST_PEER, LanWire.msg("map_ack", "x", rx.xid, "off", rx.got));
            } else {
                mapFailed();
            }
        } else if (t.equals("map_done")) {
            if (rx == null || LanWire.num(m, "x", -1) != rx.xid) {
                return;
            }
            final LanWire.MapIn in = rx;
            if (in.complete() && maps != null && maps.store(mapId, in.name, in.hash, in.data)) {
                rx = null;
                events.add(new Event("map_stored", in.name));
                send(HOST_PEER, LanWire.msg("map", "hash", in.hash, "have", "yes"));
            } else {
                mapFailed();
            }
        } else if (t.equals("start")) {
            final StartTable table = StartTable.parse(m);
            if (table == null || !table.room.equals(roomId) || table.epoch != epoch) {
                return;
            }
            final StartTable.Row mine = table.rowOf(mySeat);
            if (mine == null || !HUMAN.equals(mine.kind)) {
                return;
            }
            // No table names a seat the map has no place for.
            for (StartTable.Row r : table.rows) {
                if (r.seat >= mapPlayers) {
                    return;
                }
            }
            started = table;
            phase = STARTED;
            rx = null;
            events.add(new Event("start", table.nonce));
            close(HOST_PEER);
        }
    }

    private void mapFailed() {
        final String hash = rx.hash;
        rx = null;
        events.add(new Event("map_failed", ""));
        send(HOST_PEER, LanWire.msg("map", "hash", hash, "have", "no"));
    }

    private static String reason(String s) {
        final String r = token(s, 24);
        return r.isEmpty() ? "unknown" : r;
    }

    /** Take a state line whole or not at all. */
    private void state(Map<String, Object> m) {
        final String room = token(LanWire.str(m, "room"), 64);
        final int ep = LanWire.range(m, "epoch", 0, Integer.MAX_VALUE);
        final int rv = LanWire.range(m, "rev", 1, Integer.MAX_VALUE);
        final String name = LanWire.str(m, "name");
        final LanIdentity ident = LanIdentity.from(LanWire.obj(m, "ident"));
        final Map<String, Object> map = LanWire.obj(m, "map");
        final Map<String, Object> set = LanWire.obj(m, "set");
        final List<Object> slots = LanWire.arr(m, "slots");
        final String g = token(LanWire.str(m, "gate"), 24);
        final List<Object> who = LanWire.arr(m, "who");
        final Boolean offered = LanWire.flag(m, "bots");
        final List<GameplayMod> needed = GameplayMod.list(LanWire.arr(m, "need"));
        if (room.isEmpty() || ep < 0 || rv < 1 || name == null || name.length() > NAME_MAX + 8
            || !name.equals(clean(name, NAME_MAX + 8)) || ident == null || map == null
            || set == null || slots == null || slots.size() != SEATS || g.isEmpty()
            || who == null || who.size() > SEATS || offered == null || needed == null) {
            return;
        }
        // An older snapshot never replaces a newer one.
        if (!roomId.isEmpty() && (!room.equals(roomId) || ep != epoch || rv <= rev)) {
            return;
        }
        final String id = LanWire.str(map, "id");
        final String mname = LanWire.str(map, "name");
        final String hash = LanWire.str(map, "hash");
        final long size = LanWire.num(map, "size", -1);
        final Boolean sendable = LanWire.flag(map, "send");
        // A host that does not say how many places the map has: all eight.
        final int places = map.containsKey("players")
            ? LanWire.range(map, "players", MIN_PLAYERS, SEATS) : SEATS;
        final int res = LanWire.range(set, "res", -1, 1000000);
        final int un = LanWire.range(set, "units", -1, 1000000);
        final int ts = LanWire.range(set, "tileset", -1, 1000000);
        final Boolean fg = LanWire.flag(set, "fog");
        final int rvl = LanWire.range(set, "reveal", 0, 2);
        if (id == null || id.length() > 200 || !id.equals(clean(id, 200)) || mname == null
            || mname.length() > 96 || !mname.equals(clean(mname, 96)) || hash == null
            || hash.length() > 64 || !LanWire.isHex(hash) || size < 0 || sendable == null
            || places < MIN_PLAYERS || res < -1 || un < -1 || ts < -1 || fg == null || rvl < 0) {
            return;
        }
        final Seat[] fresh = new Seat[SEATS];
        for (int i = 0; i < SEATS; ++i) {
            if (!(slots.get(i) instanceof Map)) {
                return;
            }
            @SuppressWarnings("unchecked")
            final Seat s = seat(i, (Map<String, Object>) slots.get(i));
            if (s == null) {
                return;
            }
            fresh[i] = s;
        }
        final int[] w = new int[who.size()];
        for (int i = 0; i < w.length; ++i) {
            final Object o = who.get(i);
            if (!(o instanceof Long) || ((Long) o).longValue() < 0
                || ((Long) o).longValue() >= SEATS) {
                return;
            }
            w[i] = ((Long) o).intValue();
        }
        roomId = room;
        epoch = ep;
        rev = rv;
        roomName = name;
        hostIdentity = ident;
        mapId = id;
        mapName = mname;
        mapHash = hash;
        mapSize = size;
        mapSendable = sendable.booleanValue();
        mapPlayers = places;
        resources = res;
        units = un;
        tileset = ts;
        fog = fg.booleanValue();
        reveal = rvl;
        System.arraycopy(fresh, 0, seats, 0, SEATS);
        gate = g;
        gateWho = w;
        botsOffered = offered.booleanValue();
        need = needed;
        // A new map: say whether this device has it.
        if (!mapHash.isEmpty() && !mapHash.equals(reportedHash)) {
            reportedHash = mapHash;
            rx = null;
            final int found = (maps == null) ? Maps.NO : maps.find(mapId, mapHash);
            send(HOST_PEER, LanWire.msg("map", "hash", mapHash, "have",
                (found == Maps.YES) ? "yes" : (found == Maps.DIFFERENT) ? "diff" : "no"));
        }
    }

    private static Seat seat(int index, Map<String, Object> m) {
        final Seat s = new Seat(index);
        s.kind = LanWire.str(m, "kind");
        s.id = LanWire.str(m, "id");
        s.name = LanWire.str(m, "name");
        s.netName = LanWire.str(m, "net");
        s.portrait = LanWire.range(m, "portrait", 0, 255);
        s.race = LanWire.str(m, "race");
        s.colour = LanWire.range(m, "color", 0, COLOURS - 1);
        s.team = LanWire.range(m, "team", 1, TEAMS);
        final Boolean clash = LanWire.flag(m, "clash");
        final Boolean ready = LanWire.flag(m, "ready");
        final Boolean hostFlag = LanWire.flag(m, "host");
        s.map = LanWire.str(m, "map");
        s.pct = LanWire.range(m, "pct", 0, 100);
        s.bot = LanWire.str(m, "bot");
        final Boolean modsOk = LanWire.flag(m, "mods_ok");
        final boolean kindOk = OPEN.equals(s.kind) || CLOSED.equals(s.kind)
            || COMPUTER.equals(s.kind) || HUMAN.equals(s.kind) || HELD.equals(s.kind);
        final boolean mapOk = MAP_NONE.equals(s.map) || MAP_HAS.equals(s.map)
            || MAP_RECV.equals(s.map) || MAP_DIFF.equals(s.map);
        if (!kindOk || !mapOk || s.id == null || !s.id.equals(token(s.id, 40)) || s.name == null
            || !s.name.equals(clean(s.name, NAME_MAX)) || s.netName == null
            || !s.netName.equals(netName(s.netName)) || s.portrait < 0 || !isRace(s.race)
            || s.colour < 0 || s.team < 1 || clash == null || ready == null || hostFlag == null
            || s.pct < 0 || !isBot(s.bot) || (!s.bot.isEmpty() && !COMPUTER.equals(s.kind))
            || modsOk == null) {
            return null;
        }
        s.modsOk = modsOk.booleanValue();
        s.clash = clash.booleanValue();
        s.ready = ready.booleanValue();
        s.host = hostFlag.booleanValue();
        return s;
    }

    // ---- what the local player does ---------------------------------------------------------

    /** Joiner: Ready on or off. The host has no Ready: Start is it. */
    public void setReady(boolean on) {
        if (!host && phase == LOBBY) {
            send(HOST_PEER, LanWire.msg("ready", "on", on));
        }
    }

    /** Pick the own colour. A joiner asks; the host's pick of a held colour swaps the two. */
    public void pickColour(int colour) {
        if (host) {
            hostSetColour(0, colour);
        } else if (phase == LOBBY && colour >= 0 && colour < COLOURS) {
            send(HOST_PEER, LanWire.msg("color", "want", colour));
        }
    }

    public void pickRace(String race) {
        if (host) {
            hostSetRace(0, race);
        } else if (phase == LOBBY && isRace(race)) {
            send(HOST_PEER, LanWire.msg("race", "want", race));
        }
    }

    public void pickTeam(int team) {
        if (host) {
            hostSetTeam(0, team);
        } else if (phase == LOBBY && team >= 1 && team <= TEAMS) {
            send(HOST_PEER, LanWire.msg("team", "want", team));
        }
    }

    /** A chat line to the whole room. */
    public void say(String text) {
        if (phase != LOBBY) {
            return;
        }
        if (host) {
            say(seats[0], text);
        } else {
            final String clean = clean(text, CHAT_MAX);
            if (!clean.isEmpty()) {
                send(HOST_PEER, LanWire.msg("say", "text", clean));
            }
        }
    }

    /** Leave the room. On the host this closes it for everybody. */
    public void leave() {
        if (host) {
            hostClose();
        } else if (phase < STARTED) {
            if (connected) {
                send(HOST_PEER, LanWire.msg("leave"));
                close(HOST_PEER);
            }
            end("left");
        }
    }

    private boolean hostMay() {
        return host && phase == LOBBY;
    }

    private void kick(Seat s) {
        kick(s, "removed");
    }

    private void kick(Seat s, String reason) {
        final Peer p = peers.remove(Integer.valueOf(s.peer));
        if (p != null) {
            send(p.id, LanWire.msg("closed", "reason", reason));
            close(p.id);
        }
    }

    /**
     * Host: make a seat Open, Closed or Computer. A player sitting there is removed. A seat the
     * map has no place for stays closed.
     */
    public void hostSetSeat(int seat, String kind) {
        if (!hostMay() || seat < 1 || seat >= mapPlayers
            || !(OPEN.equals(kind) || CLOSED.equals(kind) || COMPUTER.equals(kind))) {
            return;
        }
        Seat s = seats[seat];
        if (s.kind.equals(kind)) {
            return;
        }
        if (s.kind.equals(HUMAN)) {
            kick(s);
        }
        free(s);
        s = seats[seat];
        s.kind = kind;
        if (kind.equals(COMPUTER)) {
            s.colour = freeColour(s);
            s.race = "random";
            s.map = MAP_HAS;
        }
        flush();
    }

    /**
     * Host: the player of a Computer seat: "" the original's, else "<mod>:<id>" of one of the
     * host's own gameplay mods. Every Ready is cleared: the game is another one.
     */
    public void hostSetBot(int seat, String bot) {
        if (!hostMay() || seat < 1 || seat >= mapPlayers || !seats[seat].kind.equals(COMPUTER)
            || !isBot(bot) || bot.equals(seats[seat].bot) || (!bot.isEmpty() && !botsOffered)) {
            return;
        }
        if (!bot.isEmpty()) {
            boolean mine = false;
            for (GameplayMod g : ownMods) {
                mine |= g.id.equals(botMod(bot));
            }
            if (!mine) {
                return;
            }
        }
        seats[seat].bot = bot;
        unreadyAll();
        flush();
    }

    /** Host: remove the player of a seat; the seat is open again. */
    public void hostKick(int seat) {
        if (!hostMay() || seat < 1 || seat >= SEATS || !seats[seat].kind.equals(HUMAN)) {
            return;
        }
        kick(seats[seat]);
        free(seats[seat]);
        flush();
    }

    /** Host: set any seat's colour. A free colour is given; a held one swaps the two seats. */
    public void hostSetColour(int seat, int colour) {
        if (!hostMay() || seat < 0 || seat >= SEATS || colour < 0 || colour >= COLOURS
            || !seats[seat].occupied()) {
            return;
        }
        final Seat s = seats[seat];
        final Seat h = holder(colour, s);
        if (h != null) {
            h.colour = s.colour;
        }
        s.colour = colour;
        s.clash = false;
        dirty = true;
        flush();
    }

    /** Host: set any seat's team. A player moved by the host is not ready any more. */
    public void hostSetTeam(int seat, int team) {
        if (!hostMay() || seat < 0 || seat >= SEATS || team < 1 || team > TEAMS
            || !seats[seat].occupied() || seats[seat].team == team) {
            return;
        }
        seats[seat].team = team;
        seats[seat].ready = false;
        dirty = true;
        flush();
    }

    /** Host: the race of the own seat or of a Computer seat. */
    public void hostSetRace(int seat, String race) {
        if (!hostMay() || seat < 0 || seat >= SEATS || !isRace(race)
            || !(seat == 0 || seats[seat].kind.equals(COMPUTER))) {
            return;
        }
        seats[seat].race = race;
        dirty = true;
        flush();
    }

    private void unreadyAll() {
        for (Seat s : seats) {
            s.ready = false;
        }
        dirty = true;
    }

    /**
     * The seats follow the places of the map. With fewer places: a person beyond them moves to
     * the first open seat inside, with colour, team and race, and is told the new seat; when no
     * seat is open he is removed with the reason "no_seat". A seat kept for a returning player
     * moves the same way or is given up; a computer beyond is dropped. All seats beyond are
     * closed. With more places the added seats are open.
     */
    private void fitSeats(int places) {
        for (int i = mapPlayers; i < places; ++i) {
            if (seats[i].kind.equals(CLOSED)) {
                free(seats[i]);
            }
        }
        mapPlayers = places;
        for (int i = places; i < SEATS; ++i) {
            final Seat s = seats[i];
            Seat to = null;
            if (s.kind.equals(HUMAN) || s.kind.equals(HELD)) {
                for (int j = 1; j < places && to == null; ++j) {
                    if (seats[j].kind.equals(OPEN)) {
                        to = seats[j];
                    }
                }
            }
            if (to != null) {
                s.index = to.index;
                seats[to.index] = s;
                if (s.kind.equals(HUMAN)) {
                    send(s.peer, LanWire.msg("moved", "seat", s.index));
                }
            } else if (s.kind.equals(HUMAN)) {
                kick(s, REMOVED_NO_SEAT);
            }
            seats[i] = new Seat(i);
            seats[i].kind = CLOSED;
        }
        dirty = true;
    }

    /** Host: choose a map without saying how many places it has: all eight seats can be used. */
    public void hostSetMap(String id, String name, String hash, long size, byte[] data) {
        hostSetMap(id, name, hash, size, data, SEATS);
    }

    /**
     * Host: choose the map. With the file's bytes the map can be sent to a joiner who lacks it
     * (maps/custom, *.pud); without them (a map of the game folder) it cannot. Every Ready is
     * cleared and every joiner says again whether the map is there. The map has places for
     * that many players (a number outside MIN_PLAYERS..SEATS counts as SEATS): see fitSeats.
     */
    public void hostSetMap(String id, String name, String hash, long size, byte[] data,
                           int players) {
        if (!hostMay() || id == null || hash == null || hash.length() > 64
            || !LanWire.isHex(hash)) {
            return;
        }
        final int places = (players < MIN_PLAYERS || players > SEATS) ? SEATS : players;
        if (places != mapPlayers) {
            fitSeats(places);
        }
        final boolean sendable = data != null && data.length > 0 && data.length <= LanWire.MAX_MAP
            && LanWire.isMapName(name);
        final boolean other = !hash.equals(mapHash);
        mapId = clean(id, 200);
        mapName = clean(name, 96);
        mapHash = hash;
        mapData = sendable ? data : null;
        mapSize = sendable ? data.length : Math.max(0, size);
        mapSendable = sendable;
        for (Peer p : peers.values()) {
            if (other) {
                p.xfer = null;
            }
        }
        for (Seat s : seats) {
            // The same file again: who has it, has it, and a transfer goes on.
            if (s.kind.equals(HUMAN) && !s.host && other) {
                s.map = MAP_NONE;
                s.pct = 0;
                s.mapTries = 0;
            }
        }
        unreadyAll();
        flush();
    }

    /** Host: the game settings (-1 = the map's own). Every Ready is cleared. */
    public void hostSetSettings(int res, int un, int ts, boolean fg, int rvl) {
        if (!hostMay()) {
            return;
        }
        resources = Math.max(-1, Math.min(1000000, res));
        units = Math.max(-1, Math.min(1000000, un));
        tileset = Math.max(-1, Math.min(1000000, ts));
        fog = fg;
        reveal = Math.max(0, Math.min(2, rvl));
        unreadyAll();
        flush();
    }

    /** Host: shut the room. Every joiner is told. */
    public void hostClose() {
        if (!host || phase == ENDED) {
            return;
        }
        final String line = LanWire.encode(LanWire.msg("closed", "reason", "host_closed"));
        for (Peer p : peers.values()) {
            if (p.admitted && phase == LOBBY) {
                sendLine(p.id, line);
            }
            close(p.id);
        }
        peers.clear();
        end("host_closed");
    }

    /**
     * Host: Start Game. Nothing happens while the gate is not met. Otherwise the final table
     * is made (a "random" race is drawn here, once, from the seed), sent to everybody and
     * returned; the room's connections are closed after it.
     *
     * @param hostAddress the host's own IPv4 address if it is known, else null
     */
    public StartTable hostStart(long seed, String hostAddress) {
        if (!hostMay()) {
            return null;
        }
        gate();
        if (!GATE_OK.equals(gate)) {
            return null;
        }
        final Random rnd = new Random(seed);
        final StartTable t = new StartTable();
        t.room = roomId;
        t.epoch = epoch;
        final String hex = Long.toHexString(rnd.nextLong());
        t.nonce = "0000000000000000".substring(hex.length()) + hex;
        t.hostAddress = address(hostAddress);
        t.mapId = mapId;
        t.mapHash = mapHash;
        t.mapName = mapName;
        t.resources = resources;
        t.units = units;
        t.tileset = tileset;
        t.fog = fog;
        t.reveal = reveal;
        for (Seat s : seats) {
            // Never a seat the map has no place for.
            if (s.index >= mapPlayers || (!s.kind.equals(HUMAN) && !s.kind.equals(COMPUTER))) {
                continue;
            }
            final StartTable.Row r = new StartTable.Row();
            r.seat = s.index;
            r.kind = s.kind;
            r.netName = s.kind.equals(HUMAN) ? s.netName : "";
            r.race = s.race.equals("random") ? (rnd.nextBoolean() ? "human" : "orc") : s.race;
            r.colour = s.colour;
            r.team = s.team;
            r.bot = s.kind.equals(COMPUTER) ? s.bot : "";
            t.rows.add(r);
        }
        final String line = t.line();
        for (Peer p : peers.values()) {
            if (p.admitted) {
                sendLine(p.id, line);
            }
            close(p.id);
        }
        peers.clear();
        started = t;
        phase = STARTED;
        events.add(new Event("start", t.nonce));
        return t;
    }

    /**
     * Host, after the game (Play Again): the same room with the epoch one higher. Every
     * joiner's seat is kept for him for HOLD_MS, with its colour, race and team.
     */
    public void hostReopen(long now) {
        if (!host || phase != STARTED) {
            return;
        }
        phase = LOBBY;
        started = null;
        ++epoch;
        for (Seat s : seats) {
            s.ready = false;
            if (s.kind.equals(HUMAN) && !s.host) {
                s.kind = HELD;
                s.peer = -1;
                s.heldUntil = now + HOLD_MS;
                s.map = MAP_NONE;
                s.pct = 0;
            }
        }
        dirty = true;
        flush();
    }
}
