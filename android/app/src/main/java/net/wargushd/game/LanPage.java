package net.wargushd.game;

import android.app.Activity;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.net.wifi.WifiManager;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

import net.wargushd.game.lan.LanIdentity;
import net.wargushd.game.lan.LanRoom;
import net.wargushd.game.lan.LanSockets;
import net.wargushd.game.lan.LanWire;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.List;

/**
 * The Multiplayer page: Join (the rooms found on the local network, Join by Address) and Create
 * (a map and the settings, as on the Custom Scenario page), and, once in a room, the room itself.
 *
 * The page owns everything of a room that is not the room's own logic: the sockets, the
 * discovery, the Wi-Fi multicast lock and the clock. The room is touched from the thread that
 * draws and from no other: socket threads only fill queues, which a tick every 250 ms empties;
 * what blocks (a scan, a connect, opening the server) runs on a thread of its own and hands its
 * result back through the handler. Everything is shut when the page is left or the shell pauses.
 */
final class LanPage {
    /** A stamp nobody computes yet. Every device sends this same word, so it never differs. */
    static final String STAMP_NOT_COMPUTED = "not-computed";

    // Layout, logical units (design 38.10, L1 - L4 and L6).
    private static final float RAIL_W = 720f, X0 = 816f, TOP = 264f, BOTTOM_UP = 264f, TITLE_Y = 84f;
    private static final float MODE_W = 960f, SEG_H = 108f, ADDR_X = 1904f, ADDR_W = 600f;
    private static final float LIST_Y = 420f, LIST_W = 1688f, CARD_X = 840f, CARD_W = 1640f, CARD_H = 228f;
    private static final float CARD_PITCH = 252f, PORTRAIT_W = 217.9f, PORTRAIT_H = 180f, CARD_TEXT_X = 1130f;
    private static final float CARD_END = 2444f, SCAN_H = 18f;
    private static final float MSG_X = 876f, MSG_W = 1568f, MSG_Y = 1020f, WIFI_X = 1300f, WIFI_Y = 1320f;
    private static final float DET_X = 2552f, DET_W = 1192f;
    private static final float ACTION_W = 720f, ACTION_H = 120f, ACTION_UP = 204f, RESCAN_X = 2256f, ACTION_X = 3024f;
    private static final float CREATE_LIST_W = 1440f, INFO_X = 2304f, INFO_W = 1440f, INFO_H = 300f;
    private static final float SET_Y = 612f, SET_ROW_DY = 48f, SET_PITCH = 150f, SET_H = 108f, VALUE_X = 2964f;
    private static final float VALUE_W = 720f, NOTE_DY = 828f;
    private static final float CAP_TITLE = 60f;
    private static final int INK_TITLE = 0xFF000000;

    /** One look for rooms lasts this long; a connect waits this long; the room's clock. */
    private static final int SCAN_MS = 1500, CONNECT_MS = 4000, TICK_MS = 250;
    /** A map file larger than this is not read for its hash. */
    private static final int MAP_READ_MAX = 16 * 1024 * 1024;

    /** Who starts the game with the room's request. */
    interface Starter {
        /**
         * Write a game request with these fields and start the game.
         *
         * @param engineName the name this device has in the engine's room
         * @return false when the game was not started
         */
        boolean startLanGame(String fields, String engineName);
    }

    private Activity activity;
    private Starter starter;
    private View view;
    private MenuAudio audio;
    private Profiles.Profile profile;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private boolean open;
    private WifiManager.MulticastLock lock;
    /** Keeps the Wi-Fi out of its power saving while the page is open (late and lost packets otherwise). */
    private WifiManager.WifiLock fastWifi;

    /** 0 Join, 1 Create. */
    private int mode;

    // Join.
    private List<LanWire.RoomInfo> rooms = new ArrayList<LanWire.RoomInfo>();
    private String selectedRoom = "";
    private boolean scanning, connected = true;
    private long scanBegan;
    private String ownAddress = "";
    /** Counts the scans and the connects begun: a result that is not the last one's is dropped. */
    private int scanSerial, joinSerial;
    private float cardScroll, downY, downScroll;
    private boolean inCards, dragging;
    private AddressSheet address;

    // Create.
    private final MapList maps = new MapList();
    private boolean mapsLoaded;
    private final LanSettings settings = new LanSettings();

    // The room.
    private LanRoom room;
    private LanSockets.Server server;
    private LanSockets.Responder responder;
    private LanSockets.Client client;
    private LobbyPage lobby;
    private RoomMapSheet mapSheet;
    /** True from the tap on Join Lobby or Create Game until the sockets stand or have failed. */
    private boolean connecting;
    /** The address typed on the sheet for this room, kept once the room is entered; or null. */
    private String typedAddress;
    /** Joiner: the address the room was reached at; the game is joined there too. */
    private String roomAddress = "";
    /** Which map the lobby's picture was made for. */
    private String shownMap = "";
    private final HashMap<String, String> hashes = new HashMap<String, String>();

    /** What the page has to say, above everything of its own; or null. */
    private ConfirmSheet notice;

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            if (!open) {
                return;
            }
            if (room != null) {
                pump();
            }
            if (room != null || scanning || connecting) {
                view.invalidate();
            }
            handler.postDelayed(this, TICK_MS);
        }
    };

    // ----- opening and closing -----

    /** The page comes into view: hold the multicast lock, start the clock, look for rooms. */
    void open(Activity a, View v, MenuAudio sounds, Profiles.Profile p) {
        activity = a;
        starter = (a instanceof Starter) ? (Starter) a : null;
        view = v;
        audio = sounds;
        profile = p;
        if (open) {
            return;
        }
        open = true;
        try {
            final WifiManager wifi =
                (WifiManager) a.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            lock = wifi.createMulticastLock("wargushd-lan");
            lock.setReferenceCounted(false);
            lock.acquire();
        } catch (RuntimeException e) {
            // Without the lock a room is still reached by its address.
            lock = null;
        }
        try {
            final WifiManager wifi =
                (WifiManager) a.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            fastWifi = wifi.createWifiLock(android.os.Build.VERSION.SDK_INT >= 29
                ? WifiManager.WIFI_MODE_FULL_LOW_LATENCY : WifiManager.WIFI_MODE_FULL_HIGH_PERF, "wargushd-lan");
            fastWifi.setReferenceCounted(false);
            fastWifi.acquire();
        } catch (RuntimeException e) {
            fastWifi = null;
        }
        android.util.Log.i("WargusHD", "LAUNCHER lan page open, multicast lock " + (lock != null)
            + ", wifi lock " + (fastWifi != null));
        handler.postDelayed(tick, TICK_MS);
        if (mode == 0) {
            scan();
        }
    }

    /** The page is left or the shell pauses: leave the room, stop every thread, free the lock. */
    void close() {
        if (!open) {
            return;
        }
        leaveRoom();
        open = false;
        ++scanSerial;
        ++joinSerial;
        scanning = false;
        connecting = false;
        address = null;
        notice = null;
        handler.removeCallbacks(tick);
        if (lock != null) {
            try {
                lock.release();
            } catch (RuntimeException e) {
                // Released already.
            }
            lock = null;
        }
        if (fastWifi != null) {
            try {
                fastWifi.release();
            } catch (RuntimeException e) {
                // Released already.
            }
            fastWifi = null;
        }
        android.util.Log.i("WargusHD", "LAUNCHER lan page closed");
    }

    /** Leave the room if there is one, send what that takes and close its sockets. */
    private void leaveRoom() {
        if (room != null) {
            room.leave();
            if (server != null) {
                LanSockets.flush(room, server);
            } else if (client != null) {
                LanSockets.flush(room, client);
            }
        }
        shut();
    }

    /** Close the sockets (each after what was sent to it) and forget the room. */
    private void shut() {
        if (lobby != null && lobby.typing) {
            hideKeyboard();
        }
        if (server != null) {
            server.stop();
        }
        if (responder != null) {
            responder.stop();
        }
        if (client != null) {
            client.close();
        }
        room = null;
        server = null;
        responder = null;
        client = null;
        lobby = null;
        mapSheet = null;
        typedAddress = null;
        roomAddress = "";
        shownMap = "";
    }

    private void tell(String title, String body) {
        notice = ConfirmSheet.notice(title, body);
        audio.play(MenuAudio.ERROR);
        view.invalidate();
    }

    // ----- identity -----

    private LanIdentity identity;

    /** What this device is, for the rooms it meets: made once. */
    private LanIdentity identity() {
        if (identity == null) {
            long code = 0;
            try {
                code = activity.getPackageManager().getPackageInfo(activity.getPackageName(), 0).versionCode;
            } catch (Exception e) {
                code = 0;
            }
            // The content, game-data and mod stamps are not computed anywhere yet.
            identity = new LanIdentity(code, STAMP_NOT_COMPUTED, STAMP_NOT_COMPUTED, STAMP_NOT_COMPUTED);
        }
        return identity;
    }

    private LanRoom.Profile me() {
        return new LanRoom.Profile(profile.id, profile.name, profile.portrait, "random", profile.color);
    }

    // ----- the device's maps -----

    private static byte[] bytes(File f) {
        if (f == null || f.length() <= 0 || f.length() > MAP_READ_MAX) {
            return null;
        }
        final byte[] all = new byte[(int) f.length()];
        try (InputStream in = new FileInputStream(f)) {
            int at = 0;
            for (int got; at < all.length && (got = in.read(all, at, all.length - at)) > 0; ) {
                at += got;
            }
            return (at == all.length) ? all : null;
        } catch (IOException | RuntimeException e) {
            return null;
        }
    }

    /** The SHA-256 of a map file, or "" when it cannot be read; kept while the file is unchanged. */
    private String hash(File f) {
        final String key = f.getPath() + "|" + f.length() + "|" + f.lastModified();
        String h = hashes.get(key);
        if (h == null) {
            final byte[] all = bytes(f);
            h = (all == null) ? "" : LanWire.sha256(all);
            hashes.put(key, h);
        }
        return h;
    }

    /** The file a map id names on this device: inside the game folder, else the data directory. */
    private File fileOf(String mapId) {
        if (mapId == null || mapId.isEmpty() || mapId.contains("..") || mapId.contains("\\")
                || mapId.startsWith("/")) {
            return null;
        }
        final String folder = GateActivity.rememberedFolder(activity);
        if (folder != null && new File(folder, mapId).isFile()) {
            return new File(folder, mapId);
        }
        final File own = new File(Provision.dataDir(activity), mapId);
        return own.isFile() ? own : null;
    }

    private final LanRoom.Maps deviceMaps = new LanRoom.Maps() {
        @Override
        public int find(String mapId, String hash) {
            final File f = fileOf(mapId);
            if (f == null) {
                return NO;
            }
            return hash(f).equals(hash) ? YES : DIFFERENT;
        }

        /** A received map goes to the player's own maps under the host's name for it. */
        @Override
        public boolean store(String mapId, String name, String hash, byte[] data) {
            final String dir = "maps/custom/";
            if (mapId == null || !mapId.startsWith(dir) || mapId.indexOf('/', dir.length()) >= 0
                    || mapId.contains("..") || !mapId.toLowerCase().endsWith(".pud")) {
                return false;
            }
            final File target = new File(MapStore.directory(activity), mapId.substring(dir.length()));
            // A map of the player's own is never written over.
            if (target.exists() || (!target.getParentFile().isDirectory() && !target.getParentFile().mkdirs())) {
                return false;
            }
            try (OutputStream out = new FileOutputStream(target)) {
                out.write(data);
            } catch (IOException | RuntimeException e) {
                target.delete();
                return false;
            }
            android.util.Log.i("WargusHD", "LAUNCHER lan map received " + target.getName());
            return true;
        }
    };

    // ----- looking for rooms -----

    /** This device's address on a local network, or null when no interface is up. */
    private static String localAddress() {
        try {
            final Enumeration<NetworkInterface> all = NetworkInterface.getNetworkInterfaces();
            for (NetworkInterface n : (all == null) ? Collections.<NetworkInterface>emptyList()
                                                      : Collections.list(all)) {
                if (!n.isUp() || n.isLoopback()) {
                    continue;
                }
                for (InetAddress a : Collections.list(n.getInetAddresses())) {
                    if (a instanceof Inet4Address && !a.isLoopbackAddress()) {
                        return a.getHostAddress();
                    }
                }
            }
        } catch (IOException | RuntimeException e) {
            // As when none is up.
        }
        return null;
    }

    private void scan() {
        if (!open || scanning || room != null || connecting) {
            return;
        }
        scanning = true;
        scanBegan = SystemClock.uptimeMillis();
        final int serial = ++scanSerial;
        new Thread(new Runnable() {
            @Override
            public void run() {
                final String own = localAddress();
                final List<LanWire.RoomInfo> found = (own == null)
                    ? new ArrayList<LanWire.RoomInfo>()
                    : LanSockets.scan(LanSockets.broadcastTargets(), LanWire.PORT, 3, 400, SCAN_MS);
                handler.post(new Runnable() {
                    @Override
                    public void run() {
                        scanned(serial, own, found);
                    }
                });
            }
        }, "lan-scan").start();
    }

    private void scanned(int serial, String own, List<LanWire.RoomInfo> found) {
        if (serial != scanSerial || !open) {
            return;
        }
        scanning = false;
        connected = (own != null);
        ownAddress = (own != null) ? own : "";
        rooms = found;
        cardScroll = 0f;
        if (selected() == null) {
            selectedRoom = rooms.isEmpty() ? "" : rooms.get(0).roomId;
        }
        android.util.Log.i("WargusHD", "LAUNCHER lan scan: " + rooms.size() + " rooms, connected " + connected);
        view.invalidate();
    }

    private LanWire.RoomInfo selected() {
        for (LanWire.RoomInfo r : rooms) {
            if (r.roomId.equals(selectedRoom)) {
                return r;
            }
        }
        return null;
    }

    // ----- entering a room -----

    private void join(final String host, final int port, final boolean typed) {
        if (connecting || room != null || profile == null) {
            return;
        }
        connecting = true;
        ++scanSerial;
        scanning = false;
        final int serial = ++joinSerial;
        new Thread(new Runnable() {
            @Override
            public void run() {
                LanSockets.Client made = null;
                try {
                    made = new LanSockets.Client(host, port, CONNECT_MS);
                } catch (IOException | RuntimeException e) {
                    made = null;
                }
                final LanSockets.Client c = made;
                handler.post(new Runnable() {
                    @Override
                    public void run() {
                        joined(serial, c, host, typed);
                    }
                });
            }
        }, "lan-connect").start();
    }

    private void joined(int serial, LanSockets.Client c, String host, boolean typed) {
        if (serial != joinSerial || !open) {
            if (c != null) {
                c.close();
            }
            return;
        }
        connecting = false;
        if (c == null) {
            tell(kitWords("The room could not be reached"), host);
            return;
        }
        client = c;
        typedAddress = typed ? host : null;
        roomAddress = host;
        room = LanRoom.client(identity(), me(), deviceMaps);
        pump();
    }

    /** Open the server and the discovery answer, then the room with the chosen map as its host. */
    private void create() {
        final File map = maps.chosen();
        if (connecting || room != null || profile == null || map == null) {
            return;
        }
        connecting = true;
        ++scanSerial;
        scanning = false;
        final int serial = ++joinSerial;
        new Thread(new Runnable() {
            @Override
            public void run() {
                LanSockets.Server s = null;
                LanSockets.Responder r = null;
                try {
                    s = new LanSockets.Server(LanWire.PORT, null);
                    r = new LanSockets.Responder(LanWire.PORT, null);
                } catch (IOException | RuntimeException e) {
                    if (s != null) {
                        s.stop();
                    }
                    s = null;
                }
                final LanSockets.Server madeServer = s;
                final LanSockets.Responder madeResponder = r;
                handler.post(new Runnable() {
                    @Override
                    public void run() {
                        created(serial, madeServer, madeResponder, map);
                    }
                });
            }
        }, "lan-open").start();
    }

    private void created(int serial, LanSockets.Server s, LanSockets.Responder r, File map) {
        if (serial != joinSerial || !open) {
            if (s != null) {
                s.stop();
                r.stop();
            }
            return;
        }
        connecting = false;
        if (s == null) {
            tell(kitWords("The room could not be opened"),
                 kitWords("Port {0} is in use on this device.").replace("{0}", String.valueOf(LanWire.PORT)));
            return;
        }
        server = s;
        responder = r;
        final String id = "r" + Long.toHexString(System.currentTimeMillis())
            + Integer.toHexString((int) (Math.random() * 0x10000));
        room = LanRoom.host(id, identity(), me());
        room.setSoloStart(soloStartAllowed());
        giveMap(map);
        room.hostSetSettings(settings.resources, settings.units, settings.tileset, settings.fog,
                             settings.reveal);
        android.util.Log.i("WargusHD", "LAUNCHER lan room opened on port " + s.port());
        pump();
    }

    /** Host: the room's map. A map of the player's own goes with its bytes, so that it can be sent. */
    private void giveMap(File map) {
        final boolean own = map.getParentFile() != null
            && map.getParentFile().equals(MapStore.directory(activity));
        final byte[] data = own ? bytes(map) : null;
        // The map's own number of players is the room's number of seats; unknown counts as eight.
        room.hostSetMap(MapList.path(activity, map), map.getName(), hash(map), map.length(), data,
                        PudInfo.of(map).players);
    }

    // ----- the room's clock -----

    /** Take what the sockets heard to the room, let its time pass, send what it wants sent. */
    private void pump() {
        if (room == null) {
            return;
        }
        final long now = SystemClock.elapsedRealtime();
        if (server != null) {
            LanSockets.feed(room, server.poll(), now);
            room.tick(now);
            LanSockets.flush(room, server);
            responder.set(room.discoverable() ? room.roomInfo(server.port()) : null);
        } else {
            LanSockets.feed(room, client.poll(), now);
            room.tick(now);
            LanSockets.flush(room, client);
        }
        boolean started = false;
        for (LanRoom.Event e : room.takeEvents()) {
            android.util.Log.i("WargusHD", "LAUNCHER lan event " + e.kind + " " + e.detail);
            if (e.kind.equals("deny") || e.kind.equals("map_failed")) {
                audio.play(MenuAudio.ERROR);
            } else if (e.kind.equals("start")) {
                started = true;
            }
        }
        final LanRoom.Snapshot s = room.snapshot();
        if (started) {
            // Joiner: the host's final table has arrived. This device's request is made from it.
            final LanRoom.StartTable table = room.startTable();
            final LanRoom.StartTable.Row mine = (table == null) ? null : table.rowOf(s.mySeat);
            final String fields = (mine == null) ? null : table.joinRequest(s.mySeat, roomAddress);
            handOver(fields, (mine == null) ? null : mine.netName, "lan_join");
            return;
        }
        if (s.phase == LanRoom.ENDED) {
            ended(s.endReason);
            return;
        }
        if (s.phase != LanRoom.LOBBY) {
            return;
        }
        if (lobby == null) {
            lobby = new LobbyPage(room);
            if (typedAddress != null) {
                AddressSheet.remember(activity, typedAddress, s.seats[0].name);
            }
        }
        lobby.show(s);
        final String myMap = (s.mySeat >= 0) ? s.seats[s.mySeat].map : "";
        final String key = s.mapHash + "|" + myMap;
        if (!key.equals(shownMap)) {
            shownMap = key;
            final File f = myMap.equals(LanRoom.MAP_HAS) ? fileOf(s.mapId) : null;
            final String folder = GateActivity.rememberedFolder(activity);
            final Bitmap picture = (f == null) ? null
                : MapPreview.of(folder != null ? new File(folder) : null, Provision.dataDir(activity), f);
            lobby.showMap(picture, f != null ? PudInfo.of(f) : null);
        }
    }

    // ----- the hand-over to the game -----

    /**
     * Preview only: where the files lan_preview and lan_solo_start are both in the app's files,
     * the host may start with one person and at least one computer seat.
     */
    private boolean soloStartAllowed() {
        final File files = activity.getFilesDir();
        return new File(files, "lan_preview").exists() && new File(files, "lan_solo_start").exists();
    }

    /** Host: Start Game with the gate met. The final table goes to everybody, then the game starts. */
    private void startAsHost() {
        final LanRoom.StartTable table =
            room.hostStart(System.nanoTime(), ownAddress.isEmpty() ? null : ownAddress);
        if (table == null) {
            audio.play(MenuAudio.ERROR);
            return;
        }
        // The table is on its way before the sockets are closed.
        LanSockets.flush(room, server);
        final LanRoom.StartTable.Row mine = table.rowOf(0);
        handOver(table.hostRequest(), (mine == null) ? null : mine.netName, "lan_host");
    }

    /** The room is closed and the game is started with the request made from its final table. */
    private void handOver(String fields, String engineName, String mode) {
        shut();
        final boolean begun = fields != null && engineName != null && starter != null
            && starter.startLanGame(fields, engineName);
        android.util.Log.i("WargusHD", "LAUNCH " + mode + " request from the room, started " + begun);
        if (!begun) {
            tell(kitWords("The game could not be started"), "");
            scan();
        }
    }

    /** The room is over: back to the list of rooms, with the reason when there is one to tell. */
    private void ended(String reason) {
        shut();
        if (reason.equals("host_closed")) {
            tell(kitWords("The host closed the room"), "");
        } else if (reason.equals("removed")) {
            tell(kitWords("The host removed you from the room"), "");
        } else if (reason.equals(LanRoom.REMOVED_NO_SEAT)) {
            tell(kitWords("The host changed the map"), kitWords("The new map has no free place."));
        } else if (reason.equals("host_lost")) {
            tell(kitWords("Connection lost"), kitWords("No answer from the host."));
        } else if (reason.startsWith("refused:")) {
            tell(kitWords("Joining is not possible"), kitWords(refusal(reason.substring(8))));
        }
        mode = 0;
        scan();
        view.invalidate();
    }

    private static String refusal(String code) {
        if (code.equals(LanIdentity.PROTOCOL) || code.equals(LanIdentity.VERSION)) {
            return "Use the same version on both devices.";
        } else if (code.equals(LanIdentity.CONTENT) || code.equals(LanIdentity.GAME_FILES)) {
            return "The game files differ.";
        } else if (code.equals(LanIdentity.MODS)) {
            return "The mods differ.";
        } else if (code.equals(LanRoom.REFUSE_NAME_TAKEN)) {
            return "A player with this name is in the room.";
        } else if (code.equals(LanRoom.REFUSE_FULL)) {
            return "The room is full.";
        } else if (code.equals(LanRoom.REFUSE_STARTED)) {
            return "The game has started.";
        }
        return "The host refused.";
    }

    /** The kit of the last draw, for texts made outside a draw. */
    private SkinKit words;

    private String kitWords(String english) {
        return (words != null) ? words.own(english) : english;
    }

    // ----- the keyboard -----

    /** True while a chat message is typed: the view is a text editor for it. */
    boolean typing() {
        return open && lobby != null && lobby.typing;
    }

    /** True once the system keyboard was seen up for the message being typed. */
    private boolean keysSeen;

    private void showKeyboard() {
        lobby.typing = true;
        keysSeen = false;
        view.requestFocus();
        final InputMethodManager keys = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
        keys.restartInput(view);
        keys.showSoftInput(view, 0);
    }

    private void hideKeyboard() {
        lobby.typing = false;
        keysSeen = false;
        ((InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE))
            .hideSoftInputFromWindow(view.getWindowToken(), 0);
        Screen.useWhole(activity);
    }

    /** Send the typed message; the line stays open for the next one. */
    private void send() {
        final String text = lobby.takeMessage();
        if (!text.isEmpty() && room != null) {
            room.say(text);
            pump();
        }
    }

    /** The connection the keyboard types the message through, or null when none is typed. */
    InputConnection input(View v, EditorInfo out) {
        if (!typing()) {
            return null;
        }
        out.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD;
        out.imeOptions = EditorInfo.IME_ACTION_SEND | EditorInfo.IME_FLAG_NO_EXTRACT_UI
            | EditorInfo.IME_FLAG_NO_FULLSCREEN;
        // Text is taken as it is committed; nothing is kept for the keyboard to edit.
        return new BaseInputConnection(v, false) {
            @Override
            public boolean commitText(CharSequence text, int newCursorPosition) {
                if (typing()) {
                    lobby.type(text);
                    view.invalidate();
                }
                return true;
            }

            @Override
            public boolean deleteSurroundingText(int before, int after) {
                if (typing()) {
                    lobby.erase(before);
                    view.invalidate();
                }
                return true;
            }

            @Override
            public boolean performEditorAction(int action) {
                if (typing()) {
                    send();
                    view.invalidate();
                }
                return true;
            }
        };
    }

    /** A key of a real keyboard while a message is typed. @return true when it was taken */
    boolean key(int code, KeyEvent e) {
        if (!typing()) {
            return false;
        }
        if (code == KeyEvent.KEYCODE_DEL) {
            lobby.erase(1);
        } else if (code == KeyEvent.KEYCODE_ENTER) {
            send();
        } else if (e.getUnicodeChar() != 0) {
            lobby.type(String.valueOf((char) e.getUnicodeChar()));
        } else {
            return false;
        }
        return true;
    }

    // ----- drawing -----

    private float bottom(SkinKit kit) { return kit.height - BOTTOM_UP; }
    private float actionY(SkinKit kit) { return kit.height - ACTION_UP; }
    private float setRowY(int i) { return SET_Y + SET_ROW_DY + SET_PITCH * i; }

    private float maxCardScroll(SkinKit kit) {
        return Math.max(0f, rooms.size() * CARD_PITCH - (bottom(kit) - LIST_Y - 24f));
    }

    /** True while a room is being entered: the sockets are opening or the host has not answered. */
    private boolean busy() {
        return connecting || (room != null && lobby == null);
    }

    /** The word at a card's right: Open, or why the room cannot be joined. */
    private String stateWord(SkinKit kit, LanWire.RoomInfo r, String blocked) {
        if (blocked == null) {
            return kit.own("Open");
        } else if (blocked.equals("full")) {
            return kit.own("Full");
        } else if (blocked.equals(LanIdentity.PROTOCOL) || blocked.equals(LanIdentity.VERSION)) {
            return kit.own("Different version");
        } else if (blocked.equals(LanIdentity.MODS)) {
            return kit.own("Different mods");
        }
        return kit.own("Different game files");
    }

    void draw(Canvas c, SkinKit kit, Profiles.Profile p) {
        words = kit;
        profile = p;
        if (!open) {
            return;
        }
        if (lobby != null) {
            if (lobby.typing && android.os.Build.VERSION.SDK_INT >= 30 && view.getRootWindowInsets() != null) {
                final android.view.WindowInsets insets = view.getRootWindowInsets();
                if (insets.isVisible(android.view.WindowInsets.Type.ime())) {
                    keysSeen = true;
                    lobby.keyboard = insets.getInsets(android.view.WindowInsets.Type.ime()).bottom / kit.unit;
                } else if (keysSeen) {
                    // The keyboard was closed from outside (Back, its own hide key): the page
                    // returns to its normal layout; what was typed is dropped, as with Cancel.
                    hideKeyboard();
                }
            }
            kit.textTop(c, kit.own("{0}'s game", lobby.hostName()), X0, TITLE_Y, CAP_TITLE,
                        INK_TITLE, false);
            lobby.draw(c, kit);
            if (mapSheet != null) {
                mapSheet.draw(c, kit);
            }
        } else {
            kit.textTop(c, kit.string("multiplayer", "Multiplayer"), X0, TITLE_Y, CAP_TITLE, INK_TITLE, false);
            final String[] modes = { kit.own("Join"), kit.own("Create") };
            LanKit.segments(c, kit, modes, mode, X0, TOP, MODE_W, SEG_H, LanKit.CAP_BODY);
            if (mode == 0) {
                drawJoin(c, kit);
            } else {
                drawCreate(c, kit);
            }
        }
        if (address != null) {
            address.draw(c, kit);
        }
        if (notice != null) {
            notice.draw(c, kit);
        }
    }

    private void drawJoin(Canvas c, SkinKit kit) {
        final float bottom = bottom(kit);
        LanKit.button(c, kit, kit.own("Join by Address..."), ADDR_X, TOP, ADDR_W, SEG_H, LanKit.CAP_MID,
                      LanKit.PLAIN);
        LanKit.panel(c, kit, X0, LIST_Y, LIST_W, bottom - LIST_Y);
        if (scanning) {
            LanKit.bar(c, kit, CARD_X, LIST_Y + 24f, CARD_W, SCAN_H,
                       (SystemClock.uptimeMillis() - scanBegan) / (float) SCAN_MS);
        }
        final LanIdentity mine = identity();
        if (!connected) {
            kit.textTop(c, kit.own("Not connected to a local network"), MSG_X, MSG_Y, LanKit.CAP_BODY,
                        LanKit.INK, true);
            kit.textTop(c, kit.fit(kit.own("A LAN game needs every device on the same Wi-Fi network"),
                                   LanKit.CAP_MID, MSG_W), MSG_X, MSG_Y + LanKit.LINE_PITCH, LanKit.CAP_MID,
                        LanKit.INK_MUTED, true);
            kit.textTop(c, kit.own("or on one device's hotspot."), MSG_X, MSG_Y + LanKit.LINE_PITCH * 2f,
                        LanKit.CAP_MID, LanKit.INK_MUTED, true);
            LanKit.button(c, kit, kit.own("Wi-Fi Settings"), WIFI_X, WIFI_Y, ACTION_W, ACTION_H, LanKit.CAP_BODY,
                          LanKit.PLAIN);
        } else if (rooms.isEmpty()) {
            kit.textTop(c, kit.own("No rooms found"), MSG_X, MSG_Y, LanKit.CAP_BODY, LanKit.INK, true);
            kit.textTop(c, kit.fit(kit.own("A room appears here when a player on this network"), LanKit.CAP_MID,
                                   MSG_W), MSG_X, MSG_Y + LanKit.LINE_PITCH, LanKit.CAP_MID, LanKit.INK_MUTED, true);
            kit.textTop(c, kit.own("creates a game. This device: {0}", ownAddress), MSG_X,
                        MSG_Y + LanKit.LINE_PITCH * 2f, LanKit.CAP_MID, LanKit.INK_MUTED, true);
        } else {
            cardScroll = Math.min(cardScroll, maxCardScroll(kit));
            c.save();
            c.clipRect(kit.rect(X0, LIST_Y + 12f, LIST_W, bottom - LIST_Y - 24f));
            for (int i = 0; i < rooms.size(); ++i) {
                final float y = LIST_Y + 24f + (scanning ? SCAN_H + 12f : 0f) + i * CARD_PITCH - cardScroll;
                if (y + CARD_H >= LIST_Y && y <= bottom) {
                    drawCard(c, kit, rooms.get(i), y, mine);
                }
            }
            c.restore();
        }

        // The selected room.
        LanKit.panel(c, kit, DET_X, TOP, DET_W, bottom - TOP);
        final float dx = DET_X + 60f, dw = DET_W - 120f;
        final LanWire.RoomInfo r = connected ? selected() : null;
        final String blocked = (r != null) ? r.blockedFor(mine) : null;
        if (r == null) {
            kit.textTop(c, kit.own("No room selected"), dx, TOP + 66f, LanKit.CAP_BODY, LanKit.INK_MUTED, true);
        } else {
            kit.textTop(c, kit.fit(kit.own("{0}'s game", r.hostName), LanKit.CAP_BODY, dw), dx, TOP + 66f,
                        LanKit.CAP_BODY, LanKit.GOLD, true);
            final List<String> lines = new ArrayList<String>();
            lines.add(MapList.name(new File(r.mapName)));
            lines.add(kit.own("Players {0} / {1}", r.seatsUsed, r.seatsTotal));
            if (blocked != null && (blocked.equals(LanIdentity.PROTOCOL) || blocked.equals(LanIdentity.VERSION))) {
                lines.add(kit.own("This room runs version {0}.", r.identity.appVersion));
                lines.add(kit.own("This device has version {0}.", mine.appVersion));
                lines.add(kit.own("Use the same version on both devices."));
            } else if (blocked != null && !blocked.equals("full")) {
                lines.add(kit.own(refusal(blocked)));
            } else if (blocked != null) {
                lines.add(kit.own("The room is full."));
            }
            lines.add(r.address);
            for (int i = 0; i < lines.size(); ++i) {
                kit.textTop(c, kit.fit(lines.get(i), LanKit.CAP_MID, dw), dx,
                            TOP + 148.5f + LanKit.LINE_PITCH * i, LanKit.CAP_MID, LanKit.INK_MUTED, true);
            }
            final String verdict = busy() ? kit.own("Connecting...")
                : blocked == null ? kit.own("Same version and game files")
                : blocked.equals("full") ? kit.own("The room is full.") : kit.own("Joining is not possible");
            kit.textTop(c, verdict, dx, bottom - 96f, LanKit.CAP_MID, blocked == null ? LanKit.INK : LanKit.GOLD,
                        true);
        }

        LanKit.button(c, kit, kit.own("Rescan"), RESCAN_X, actionY(kit), ACTION_W, ACTION_H, LanKit.CAP_BODY,
                      connected && !scanning && !busy() ? LanKit.PLAIN : LanKit.DISABLED);
        LanKit.button(c, kit, kit.own("Join Lobby"), ACTION_X, actionY(kit), ACTION_W, ACTION_H, LanKit.CAP_BODY,
                      r != null && blocked == null && !busy() ? LanKit.PRIMARY : LanKit.DISABLED);
    }

    private final HashMap<Integer, Bitmap> portraits = new HashMap<Integer, Bitmap>();

    private void drawCard(Canvas c, SkinKit kit, LanWire.RoomInfo r, float y, LanIdentity mine) {
        final boolean sel = r.roomId.equals(selectedRoom);
        final String blocked = r.blockedFor(mine);
        kit.fill(c, CARD_X, y, CARD_W, CARD_H, sel ? LanKit.ROW_SELECTED : LanKit.ROW_FAINT, LanKit.ROW_RADIUS);
        if (sel) {
            kit.fill(c, CARD_X, y, 10f, CARD_H, LanKit.GOLD, 0f);
        }
        final Integer key = Integer.valueOf(r.portrait * 8 + (r.colour & 7));
        if (!portraits.containsKey(key)) {
            portraits.put(key, Portraits.tinted(kit, r.portrait, Profiles.COLORS[r.colour & 7]));
        }
        kit.picture(c, portraits.get(key), CARD_X + 24f, y + 24f, PORTRAIT_W, PORTRAIT_H);
        final int ink = (blocked == null) ? LanKit.INK : LanKit.INK_DISABLED;
        kit.textTop(c, kit.fit(kit.own("{0}'s game", r.hostName), LanKit.CAP_BODY, 900f), CARD_TEXT_X, y + 54.8f,
                    LanKit.CAP_BODY, ink, true);
        kit.textTop(c, kit.fit(MapList.name(new File(r.mapName)), LanKit.CAP_MID, 600f), CARD_TEXT_X,
                    y + 137.2f, LanKit.CAP_MID, LanKit.INK_MUTED, true);
        kit.textMiddleRight(c, r.seatsUsed + " / " + r.seatsTotal, CARD_END, y + 40f, 60f, LanKit.CAP_BODY, ink,
                            true);
        kit.textMiddleRight(c, stateWord(kit, r, blocked), CARD_END, y + 126f, 48f, LanKit.CAP_MID,
                            blocked == null ? LanKit.GOLD : LanKit.INK_MUTED, true);
    }

    private void drawCreate(Canvas c, SkinKit kit) {
        if (!mapsLoaded) {
            mapsLoaded = true;
            maps.load(activity);
        }
        final float bottom = bottom(kit);
        LanKit.panel(c, kit, X0, LIST_Y, CREATE_LIST_W, bottom - LIST_Y);
        maps.draw(c, kit, X0 + 24f, LIST_Y + 24f, bottom - 24f);

        // Right: the selected map, then the settings.
        LanKit.panel(c, kit, INFO_X, TOP, INFO_W, INFO_H);
        final File map = maps.chosen();
        if (map != null) {
            kit.textTop(c, kit.fit(MapList.name(map), LanKit.CAP_BODY, INFO_W - 120f), INFO_X + 60f, TOP + 66f,
                        LanKit.CAP_BODY, LanKit.GOLD, true);
            kit.textTop(c, MapList.facts(kit, maps.info.get(maps.selected), -1), INFO_X + 60f,
                        TOP + 66f + LanKit.LINE_PITCH, LanKit.CAP_MID, LanKit.INK_MUTED, true);
        }
        LanKit.panel(c, kit, INFO_X, SET_Y, INFO_W, bottom - SET_Y);
        for (int i = 0; i < LanSettings.ROWS; ++i) {
            kit.textMiddle(c, LanSettings.label(kit, i), INFO_X + 60f, setRowY(i), SET_H, LanKit.CAP_BODY,
                           LanKit.INK, true);
            LanKit.value(c, kit, settings.value(kit, i), VALUE_X, setRowY(i), VALUE_W, SET_H, LanKit.CAP_BODY);
        }
        kit.textTop(c, kit.fit(kit.own("Race, colour and team are chosen in the room."), LanKit.CAP_MID,
                               INFO_W - 120f), INFO_X + 60f, SET_Y + NOTE_DY, LanKit.CAP_MID, LanKit.INK_MUTED,
                    true);
        LanKit.button(c, kit, kit.own("Create Game"), ACTION_X, actionY(kit), ACTION_W, ACTION_H, LanKit.CAP_BODY,
                      map != null && !busy() ? LanKit.PRIMARY : LanKit.DISABLED);
    }

    // ----- touch -----

    /**
     * A touch while the page is shown, in logical units.
     *
     * @return true when the page took it; false for a touch that is the shell's (the rail and
     *         the header while no room and no sheet is open)
     */
    boolean touch(int action, float x, float y, SkinKit kit) {
        if (!open) {
            return false;
        }
        final boolean up = (action == MotionEvent.ACTION_UP);
        if (notice != null) {
            if (up && notice.up(x, y, kit) != ConfirmSheet.NONE) {
                notice = null;
                audio.play(MenuAudio.BUTTON);
            }
            return true;
        }
        if (address != null) {
            if (up) {
                final int answer = address.up(x, y, kit);
                audio.play(MenuAudio.BUTTON);
                if (answer == AddressSheet.JOIN) {
                    final String to = address.address();
                    address = null;
                    join(to, LanWire.PORT, true);
                } else if (answer == AddressSheet.CANCEL) {
                    address = null;
                }
            }
            return true;
        }
        if (room != null || connecting) {
            // In a room nothing of the shell reacts: the room is left by Leave Lobby or Back.
            if (lobby != null) {
                touchRoom(action, x, y, kit);
            }
            return true;
        }
        if (x < RAIL_W || y < TOP) {
            return false;
        }
        if (action == MotionEvent.ACTION_DOWN) {
            downY = y;
            downScroll = cardScroll;
            dragging = false;
            inCards = mode == 0 && LanKit.in(x, y, X0, LIST_Y, LIST_W, bottom(kit) - LIST_Y);
            if (mode == 1) {
                maps.down(x, y, X0 + 24f, LIST_Y + 24f, bottom(kit) - 24f);
            }
        } else if (action == MotionEvent.ACTION_MOVE) {
            if (mode == 1) {
                maps.move(y, LIST_Y + 24f, bottom(kit) - 24f);
            } else if (inCards && maxCardScroll(kit) > 0f) {
                dragging = dragging || Math.abs(y - downY) > 16f;
                if (dragging) {
                    cardScroll = Math.max(0f, Math.min(maxCardScroll(kit), downScroll - (y - downY)));
                }
            }
        } else if (up) {
            final int seg = LanKit.segmentAt(x, y, 2, X0, TOP, MODE_W, SEG_H);
            if (seg >= 0 && seg != mode) {
                mode = seg;
                audio.play(MenuAudio.BUTTON);
                if (mode == 0) {
                    scan();
                }
            } else if (mode == 0 && !dragging) {
                tapJoin(x, y, kit);
            } else if (mode == 1) {
                tapCreate(x, y, kit);
            }
            dragging = false;
            inCards = false;
        }
        return true;
    }

    private void tapJoin(float x, float y, SkinKit kit) {
        final LanWire.RoomInfo r = selected();
        if (LanKit.in(x, y, ADDR_X, TOP, ADDR_W, SEG_H)) {
            audio.play(MenuAudio.BUTTON);
            address = new AddressSheet(activity);
        } else if (LanKit.in(x, y, RESCAN_X, actionY(kit), ACTION_W, ACTION_H)) {
            // Also where the page says there is no network: the look is how that is found again.
            audio.play(scanning ? MenuAudio.ERROR : MenuAudio.BUTTON);
            scan();
        } else if (LanKit.in(x, y, ACTION_X, actionY(kit), ACTION_W, ACTION_H)) {
            if (connected && r != null && r.blockedFor(identity()) == null) {
                audio.play(MenuAudio.BUTTON);
                join(r.address, r.port, false);
            } else {
                audio.play(MenuAudio.ERROR);
            }
        } else if (!connected) {
            if (LanKit.in(x, y, WIFI_X, WIFI_Y, ACTION_W, ACTION_H)) {
                audio.play(MenuAudio.BUTTON);
                try {
                    activity.startActivity(
                        new android.content.Intent(android.provider.Settings.ACTION_WIFI_SETTINGS));
                } catch (RuntimeException e) {
                    tell(kit.own("No Wi-Fi settings on this device"), "");
                }
            }
        } else if (LanKit.in(x, y, CARD_X, LIST_Y + 12f, CARD_W, bottom(kit) - LIST_Y - 24f)) {
            final float top = LIST_Y + 24f + (scanning ? SCAN_H + 12f : 0f);
            final int card = (int) Math.floor((y - top + cardScroll) / CARD_PITCH);
            if (card >= 0 && card < rooms.size() && (y - top + cardScroll) - card * CARD_PITCH < CARD_H) {
                selectedRoom = rooms.get(card).roomId;
                audio.play(MenuAudio.BUTTON);
            }
        }
    }

    private void tapCreate(float x, float y, SkinKit kit) {
        if (maps.up(activity, x, y, X0 + 24f, LIST_Y + 24f, bottom(kit) - 24f)) {
            audio.play(MenuAudio.BUTTON);
        } else if (LanKit.in(x, y, ACTION_X, actionY(kit), ACTION_W, ACTION_H)) {
            audio.play(maps.chosen() != null ? MenuAudio.BUTTON : MenuAudio.ERROR);
            create();
        } else {
            for (int i = 0; i < LanSettings.ROWS; ++i) {
                if (LanKit.in(x, y, VALUE_X, setRowY(i), VALUE_W, SET_H)) {
                    settings.next(i);
                    audio.play(MenuAudio.BUTTON);
                }
            }
        }
    }

    private void touchRoom(int action, float x, float y, SkinKit kit) {
        if (mapSheet != null) {
            if (action == MotionEvent.ACTION_DOWN) {
                mapSheet.down(x, y, kit);
            } else if (action == MotionEvent.ACTION_MOVE) {
                mapSheet.move(y, kit);
            } else if (action == MotionEvent.ACTION_UP) {
                final int answer = mapSheet.up(activity, x, y, kit);
                audio.play(MenuAudio.BUTTON);
                if (answer == RoomMapSheet.APPLY) {
                    // The map first, then the settings; each clears every Ready.
                    giveMap(mapSheet.chosen());
                    final LanSettings set = mapSheet.settings;
                    room.hostSetSettings(set.resources, set.units, set.tileset, set.fog, set.reveal);
                    mapSheet = null;
                    pump();
                } else if (answer == RoomMapSheet.CANCEL) {
                    mapSheet = null;
                }
            }
            return;
        }
        if (action == MotionEvent.ACTION_DOWN) {
            lobby.down(x, y, kit);
        } else if (action == MotionEvent.ACTION_MOVE) {
            lobby.move(x, y, kit);
        } else if (action == MotionEvent.ACTION_UP) {
            switch (lobby.up(x, y, kit)) {
                case LobbyPage.ACTION_LEAVE:
                    audio.play(MenuAudio.BUTTON);
                    leaveRoom();
                    scan();
                    break;
                case LobbyPage.ACTION_START:
                    startAsHost();
                    break;
                case LobbyPage.ACTION_CHANGED:
                    audio.play(MenuAudio.BUTTON);
                    pump();
                    break;
                case LobbyPage.ACTION_REFUSED:
                    audio.play(MenuAudio.ERROR);
                    break;
                case LobbyPage.ACTION_CHAT:
                    audio.play(MenuAudio.BUTTON);
                    showKeyboard();
                    break;
                case LobbyPage.ACTION_SEND:
                    audio.play(MenuAudio.BUTTON);
                    send();
                    break;
                case LobbyPage.ACTION_CHAT_CLOSE:
                    audio.play(MenuAudio.BUTTON);
                    lobby.takeMessage();
                    hideKeyboard();
                    break;
                case LobbyPage.ACTION_MAP:
                    audio.play(MenuAudio.BUTTON);
                    openMapSheet();
                    break;
                default:
                    break;
            }
        }
    }

    private void openMapSheet() {
        final LanRoom.Snapshot s = room.snapshot();
        final LanSettings now = new LanSettings();
        now.from(s);
        mapSheet = new RoomMapSheet(activity, s.mapId, now);
    }

    /**
     * Back while the page is shown: close what is open on it, innermost first; in a room, leave
     * it.
     *
     * @return false when nothing of the page was open: the page itself is closed and left
     */
    boolean back() {
        if (!open) {
            return false;
        }
        if (notice != null) {
            notice = null;
        } else if (address != null) {
            address = null;
        } else if (mapSheet != null) {
            mapSheet = null;
        } else if (typing()) {
            lobby.takeMessage();
            hideKeyboard();
        } else if (lobby != null && lobby.back()) {
            return true;
        } else if (room != null || connecting) {
            ++joinSerial;
            connecting = false;
            leaveRoom();
            scan();
        } else {
            close();
            return false;
        }
        return true;
    }
}
