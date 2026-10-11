package net.wargushd.game.lan;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.InterfaceAddress;
import java.net.NetworkInterface;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.atomic.AtomicReference;

/**
 * The sockets under the room: the host's TCP server, a joiner's TCP connection and the UDP
 * discovery. Only java.net and java.io, so the same code runs on a PC.
 *
 * Nothing here knows a rule. Every connection has a thread that reads lines and a thread that
 * writes them; what happens is queued as NetEvent and taken with poll() by the one thread that
 * owns the room.
 */
public final class LanSockets {
    private LanSockets() {}

    /** Connections a server keeps at once: the seats and a few that are just asking. */
    static final int MAX_PEERS = 16;
    /** A socket that says nothing for this long is gone, whatever the room's pings say. */
    static final int READ_TIMEOUT_MS = 12000;

    public static final class NetEvent {
        public static final int CONNECTED = 0;
        public static final int LINE = 1;
        public static final int LOST = 2;

        public final int kind;
        public final int peer;
        /** The line, or for CONNECTED the peer's address. */
        public final String text;

        NetEvent(int kind, int peer, String text) {
            this.kind = kind;
            this.peer = peer;
            this.text = text;
        }
    }

    /** One TCP connection with its two threads. */
    private static final class Conn {
        private static final Object CLOSE = new Object();

        final int peer;
        final Socket socket;
        final ConcurrentLinkedQueue<NetEvent> events;
        final LinkedBlockingQueue<Object> outbox = new LinkedBlockingQueue<Object>();
        private volatile boolean closedHere;
        private volatile boolean lostTold;
        /** The reading thread has seen the other side's end (or an error). */
        private volatile boolean readEnded;
        /** After the last line: how long the other side is given to take it and close. */
        private static final int LINGER_MS = 3000;

        Conn(int peer, Socket socket, ConcurrentLinkedQueue<NetEvent> events) {
            this.peer = peer;
            this.socket = socket;
            this.events = events;
        }

        void start() {
            final Thread r = new Thread(this::read, "lan-read-" + peer);
            r.setDaemon(true);
            r.start();
            final Thread w = new Thread(this::write, "lan-write-" + peer);
            w.setDaemon(true);
            w.start();
        }

        private void lost() {
            // Told once, and not when this side closed the connection itself.
            if (!lostTold && !closedHere) {
                lostTold = true;
                events.add(new NetEvent(NetEvent.LOST, peer, null));
            }
            outbox.offer(CLOSE);
        }

        private void read() {
            final LanWire.Framer framer = new LanWire.Framer();
            final byte[] buf = new byte[16384];
            final List<String> lines = new ArrayList<String>();
            try {
                final InputStream in = socket.getInputStream();
                for (int n; (n = in.read(buf)) > 0; ) {
                    framer.feed(buf, 0, n, lines);
                    for (String line : lines) {
                        events.add(new NetEvent(NetEvent.LINE, peer, line));
                    }
                    lines.clear();
                }
            } catch (IOException | RuntimeException e) {
                // Gone or silent for too long: the same to the room.
            }
            readEnded = true;
            lost();
        }

        private void write() {
            try {
                final OutputStream o = socket.getOutputStream();
                for (;;) {
                    final Object next = outbox.take();
                    if (next == CLOSE) {
                        break;
                    }
                    o.write(LanWire.frame((String) next));
                    if (outbox.isEmpty()) {
                        o.flush();
                    }
                }
                o.flush();
                // A plain close while the other side still sends (its pings) is answered by the
                // system with a reset, and a reset can throw away the last line before it is
                // read there. So: end only our direction, let the other side read to the end and
                // close; this side goes on reading meanwhile.
                socket.shutdownOutput();
                for (int waited = 0; !readEnded && waited < LINGER_MS; waited += 50) {
                    Thread.sleep(50);
                }
            } catch (IOException | InterruptedException | RuntimeException e) {
                lost();
            }
            try {
                socket.close();
            } catch (IOException e) {
                // Closed already.
            }
        }

        void send(String line) {
            outbox.offer(line);
        }

        /** Close after everything queued has been written. */
        void close() {
            closedHere = true;
            outbox.offer(CLOSE);
        }
    }

    private static void prepare(Socket s) throws IOException {
        s.setTcpNoDelay(true);
        s.setSoTimeout(READ_TIMEOUT_MS);
    }

    /** The host's side: accepts connections and numbers them from 1. */
    public static final class Server {
        private final ServerSocket server;
        private final ConcurrentLinkedQueue<NetEvent> events = new ConcurrentLinkedQueue<NetEvent>();
        private final Map<Integer, Conn> conns = new ConcurrentHashMap<Integer, Conn>();
        private volatile boolean stopped;
        private int nextPeer = 1;

        /** Port 0 takes any free port; a null address listens on all. */
        public Server(int port, InetAddress bind) throws IOException {
            server = new ServerSocket();
            server.setReuseAddress(true);
            server.bind(new InetSocketAddress(bind, port), MAX_PEERS);
            final Thread t = new Thread(this::accept, "lan-accept");
            t.setDaemon(true);
            t.start();
        }

        public int port() {
            return server.getLocalPort();
        }

        private void accept() {
            while (!stopped) {
                try {
                    final Socket s = server.accept();
                    if (conns.size() >= MAX_PEERS) {
                        s.close();
                        continue;
                    }
                    prepare(s);
                    final int peer = nextPeer++;
                    final Conn c = new Conn(peer, s, events);
                    conns.put(Integer.valueOf(peer), c);
                    events.add(new NetEvent(NetEvent.CONNECTED, peer,
                                            s.getInetAddress().getHostAddress()));
                    c.start();
                } catch (IOException | RuntimeException e) {
                    // One bad connection does not end the accepting; a closed server does.
                }
            }
        }

        /** What happened since the last call, in order. */
        public List<NetEvent> poll() {
            final List<NetEvent> l = new ArrayList<NetEvent>();
            for (NetEvent e; (e = events.poll()) != null; ) {
                if (e.kind == NetEvent.LOST) {
                    conns.remove(Integer.valueOf(e.peer));
                }
                l.add(e);
            }
            return l;
        }

        public void send(int peer, String line) {
            final Conn c = conns.get(Integer.valueOf(peer));
            if (c != null) {
                c.send(line);
            }
        }

        public void close(int peer) {
            final Conn c = conns.remove(Integer.valueOf(peer));
            if (c != null) {
                c.close();
            }
        }

        /** Stop accepting and close every connection after what was sent to it. */
        public void stop() {
            stopped = true;
            try {
                server.close();
            } catch (IOException e) {
                // Closed already.
            }
            for (Conn c : conns.values()) {
                c.close();
            }
            conns.clear();
        }
    }

    /** A joiner's side: one connection; the host is peer LanRoom.HOST_PEER. */
    public static final class Client {
        private final ConcurrentLinkedQueue<NetEvent> events = new ConcurrentLinkedQueue<NetEvent>();
        private final Conn conn;
        /** The address the host was reached at. */
        public final String hostAddress;

        public Client(String host, int port, int timeoutMs) throws IOException {
            final Socket s = new Socket();
            try {
                s.connect(new InetSocketAddress(host, port), timeoutMs);
                prepare(s);
            } catch (IOException e) {
                s.close();
                throw e;
            }
            hostAddress = s.getInetAddress().getHostAddress();
            conn = new Conn(LanRoom.HOST_PEER, s, events);
            events.add(new NetEvent(NetEvent.CONNECTED, LanRoom.HOST_PEER, hostAddress));
            conn.start();
        }

        public List<NetEvent> poll() {
            final List<NetEvent> l = new ArrayList<NetEvent>();
            for (NetEvent e; (e = events.poll()) != null; ) {
                l.add(e);
            }
            return l;
        }

        public void send(String line) {
            conn.send(line);
        }

        public void close() {
            conn.close();
        }
    }

    // ---- between the sockets and a room -----------------------------------------------------

    /** Hand polled events to a room. */
    public static void feed(LanRoom room, List<NetEvent> events, long now) {
        for (NetEvent e : events) {
            if (e.kind == NetEvent.CONNECTED) {
                room.peerConnected(e.peer, now);
            } else if (e.kind == NetEvent.LINE) {
                room.peerLine(e.peer, e.text, now);
            } else {
                room.peerLost(e.peer, now);
            }
        }
    }

    /** Send what a host's room wants sent. */
    public static void flush(LanRoom room, Server server) {
        for (LanRoom.Out o : room.takeOut()) {
            if (o.close) {
                server.close(o.peer);
            } else {
                server.send(o.peer, o.line);
            }
        }
    }

    /** Send what a joiner's room wants sent. */
    public static void flush(LanRoom room, Client client) {
        for (LanRoom.Out o : room.takeOut()) {
            if (o.close) {
                client.close();
            } else {
                client.send(o.line);
            }
        }
    }

    // ---- discovery --------------------------------------------------------------------------

    /** Answers probes with the room that was set last; silent while none is set. */
    public static final class Responder {
        private final DatagramSocket socket;
        private final AtomicReference<LanWire.RoomInfo> info = new AtomicReference<LanWire.RoomInfo>();
        private volatile boolean stopped;

        public Responder(int port, InetAddress bind) throws IOException {
            socket = new DatagramSocket(null);
            socket.setReuseAddress(true);
            socket.bind(new InetSocketAddress(bind, port));
            final Thread t = new Thread(this::run, "lan-discovery");
            t.setDaemon(true);
            t.start();
        }

        public int port() {
            return socket.getLocalPort();
        }

        /** The room to announce, or null to stop answering (the game has started). */
        public void set(LanWire.RoomInfo room) {
            info.set(room);
        }

        private void run() {
            final byte[] buf = new byte[LanWire.MAX_DATAGRAM + 1];
            while (!stopped) {
                try {
                    final DatagramPacket p = new DatagramPacket(buf, buf.length);
                    socket.receive(p);
                    final String nonce = LanWire.parseProbe(p.getData(), p.getLength());
                    final LanWire.RoomInfo room = info.get();
                    if (nonce != null && room != null) {
                        final byte[] out = LanWire.reply(nonce, room);
                        socket.send(new DatagramPacket(out, out.length, p.getSocketAddress()));
                    }
                } catch (IOException | RuntimeException e) {
                    // A bad datagram is nobody's; a closed socket ends the loop.
                }
            }
        }

        public void stop() {
            stopped = true;
            socket.close();
        }
    }

    /** The broadcast address of every IPv4 interface that is up, and the limited broadcast. */
    public static List<InetAddress> broadcastTargets() {
        final List<InetAddress> l = new ArrayList<InetAddress>();
        try {
            final Enumeration<NetworkInterface> all = NetworkInterface.getNetworkInterfaces();
            for (NetworkInterface n : (all == null) ? Collections.<NetworkInterface>emptyList()
                                                      : Collections.list(all)) {
                if (!n.isUp() || n.isLoopback()) {
                    continue;
                }
                for (InterfaceAddress a : n.getInterfaceAddresses()) {
                    if (a.getBroadcast() != null && !l.contains(a.getBroadcast())) {
                        l.add(a.getBroadcast());
                    }
                }
            }
            final InetAddress limited = InetAddress.getByName("255.255.255.255");
            if (!l.contains(limited)) {
                l.add(limited);
            }
        } catch (IOException | RuntimeException e) {
            // What was found so far.
        }
        return l;
    }

    /**
     * Look for rooms: a few probes to every target, replies collected for a while, one entry
     * per room id. Blocks for about collectMs; not for the thread that draws.
     */
    public static List<LanWire.RoomInfo> scan(List<InetAddress> targets, int port, int probes,
                                              int gapMs, int collectMs) {
        final Map<String, LanWire.RoomInfo> found = new LinkedHashMap<String, LanWire.RoomInfo>();
        final byte[] seed = new byte[8];
        new SecureRandom().nextBytes(seed);
        final StringBuilder hex = new StringBuilder();
        for (byte b : seed) {
            hex.append(Character.forDigit((b >> 4) & 15, 16)).append(Character.forDigit(b & 15, 16));
        }
        final String nonce = hex.toString();
        final byte[] probe = LanWire.probe(nonce);
        try (DatagramSocket socket = new DatagramSocket()) {
            socket.setBroadcast(true);
            final long start = System.nanoTime() / 1000000;
            final byte[] buf = new byte[LanWire.MAX_DATAGRAM + 1];
            int sent = 0;
            for (;;) {
                final long now = System.nanoTime() / 1000000 - start;
                if (now >= collectMs) {
                    break;
                }
                if (sent < probes && now >= (long) sent * gapMs) {
                    ++sent;
                    for (InetAddress to : targets) {
                        try {
                            socket.send(new DatagramPacket(probe, probe.length, to, port));
                        } catch (IOException e) {
                            // That interface cannot send; the others may.
                        }
                    }
                }
                final long next = (sent < probes) ? (long) sent * gapMs : collectMs;
                socket.setSoTimeout((int) Math.max(1, Math.min(next, collectMs) - now));
                try {
                    final DatagramPacket p = new DatagramPacket(buf, buf.length);
                    socket.receive(p);
                    final LanWire.RoomInfo r = LanWire.parseReply(p.getData(), p.getLength(), nonce);
                    if (r != null) {
                        r.address = p.getAddress().getHostAddress();
                        found.put(r.roomId, r);
                    }
                } catch (SocketTimeoutException e) {
                    // Time for the next probe or the end.
                }
            }
        } catch (IOException | RuntimeException e) {
            // No socket: nothing found.
        }
        return new ArrayList<LanWire.RoomInfo>(found.values());
    }
}
