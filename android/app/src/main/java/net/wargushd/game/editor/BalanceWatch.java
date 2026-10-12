package net.wargushd.game.editor;

/**
 * Measures the balance of a map away from the caller's thread. The caller hands over the map
 * as the bytes of its file; one worker thread reads and measures them and hands the answer
 * back. A request that came while another was measured replaces whatever waits; every request
 * has a number, and the caller keeps only the answer whose number is the newest one. No
 * Android class.
 */
public final class BalanceWatch {
    /** What was measured. */
    public static final class Answer {
        /** The number of the request this answers. */
        public long number;
        /** The measurements; null when the bytes did not read as a map. */
        public MapMetrics.Measure measure;
        /** The fairness, 0 to 100; not a number below two starts. */
        public double score = Double.NaN;
        /** The part that takes most from the fairness (MapMetrics.PART_...), or -1. */
        public int weakest = -1;
        public long millis;
    }

    /** Told on the worker thread when a request is measured. */
    public interface Listener { void measured(Answer a); }

    private final Listener listener;
    private final Object lock = new Object();
    private byte[] waiting;
    private long waitingNumber, newest;
    private boolean closed;
    private Thread worker;

    public BalanceWatch(Listener listener) {
        this.listener = listener;
    }

    /** Ask for the map of these bytes to be measured; the number of the request is returned. */
    public long ask(byte[] pud) {
        synchronized (lock) {
            waiting = pud;
            waitingNumber = ++newest;
            if (worker == null && !closed) {
                worker = new Thread(new Runnable() {
                    @Override
                    public void run() {
                        work();
                    }
                }, "editor-balance");
                worker.setDaemon(true);
                worker.start();
            }
            lock.notifyAll();
            return newest;
        }
    }

    /** True when an answer is that of the newest request. */
    public boolean current(Answer a) {
        synchronized (lock) {
            return !closed && a != null && a.number == newest;
        }
    }

    /** No more answers; the worker ends after the map it is measuring. */
    public void close() {
        synchronized (lock) {
            closed = true;
            waiting = null;
            lock.notifyAll();
        }
    }

    /** The answer for a map's bytes; on the calling thread. */
    public static Answer measure(byte[] pud, long number) {
        final Answer a = new Answer();
        a.number = number;
        final long began = System.nanoTime();
        try {
            a.measure = MapMetrics.measure(PudFile.parse(pud).model());
            a.score = MapMetrics.fairness(a.measure);
            a.weakest = MapMetrics.weakest(a.measure);
        } catch (java.io.IOException | RuntimeException e) {
            a.measure = null;
        }
        a.millis = (System.nanoTime() - began) / 1000000L;
        return a;
    }

    /** The score as the tile shows it: a whole number, or "-" where there is none. */
    public static String shown(double score) {
        return Double.isNaN(score) ? "-" : String.valueOf((int) Math.round(score));
    }

    private void work() {
        for (;;) {
            final byte[] bytes;
            final long number;
            synchronized (lock) {
                while (waiting == null && !closed) {
                    try {
                        lock.wait();
                    } catch (InterruptedException e) {
                        return;
                    }
                }
                if (closed) {
                    return;
                }
                bytes = waiting;
                number = waitingNumber;
                waiting = null;
            }
            final Answer a = measure(bytes, number);
            // An answer overtaken while it was made is not handed on.
            if (current(a)) {
                listener.measured(a);
            }
        }
    }
}
