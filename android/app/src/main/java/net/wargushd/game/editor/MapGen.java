package net.wargushd.game.editor;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Random;

/**
 * The random balanced melee map. Nothing here knows the screen or Android, and nothing is read
 * from any existing map.
 *
 * Method:
 *   1. terrain is decided on the lattice of tile corners, one kind a corner;
 *   2. every random field is averaged over a symmetry group that carries each player's start
 *      onto every other player's start, so all players see the same terrain;
 *   3. each player gets the same base kit (clearing, gold mine, forest, expansion), made once
 *      for the first player and carried to the others by the group;
 *   4. ways between the players are cut along least-cost paths;
 *   5. the repair pass makes the four corners of every tile a set a tile exists for;
 *   6. the result is measured; a map below the fairness target or of the wrong kind is thrown
 *      away and the next number is tried.
 *
 * One set of options and one number always give the same map: the random numbers are made here
 * with whole-number arithmetic, and angles are taken from StrictMath.
 */
public final class MapGen {
    public static final int LAND = 0, COAST = 1, ISLANDS = 2;
    public static final int LOW = 0, MEDIUM = 1, HIGH = 2;
    /** The symmetries: chosen by the generator, none (fair by the kit alone), a turn a player, and the exact ones. */
    public static final int AUTO = 0, NONE = 1, ROT_N = 2, ROT180 = 3, MIRROR_X = 4, MIRROR_Y = 5, DIAG = 6,
        ANTI_DIAG = 7, ROT90 = 8, MIRROR_XY = 9, DIAG_XY = 10, D4 = 11;
    public static final String[] SYMMETRY_NAMES = { "auto", "none", "rot-n", "rot180", "mirror-x", "mirror-y",
        "diag", "anti-diag", "rot90", "mirror-xy", "diag-xy", "d4" };
    public static final String[] KIND_NAMES = { "land", "coast", "islands" };
    /** A map at or above this fairness is taken at once. */
    public static final double FAIRNESS_TARGET = 90.0;
    public static final int TRIES = 40;

    /** Gold of the home mine, the expansion mine, the contested mine and oil of a patch, by resources. */
    private static final int[][] RESOURCES = { { 30000, 20000, 0, 10000 }, { 45000, 40000, 0, 20000 },
        { 60000, 60000, 40000, 30000 } };
    private static final int[] I2 = { 1, 0, 0, 1 }, R90 = { 0, -1, 1, 0 }, R180 = { -1, 0, 0, -1 },
        R270 = { 0, 1, -1, 0 }, MX = { -1, 0, 0, 1 }, MY = { 1, 0, 0, -1 }, DI = { 0, 1, 1, 0 }, AD = { 0, -1, -1, 0 };
    private static final byte DW = Terrain.DEEP_WATER, LW = Terrain.WATER, LG = Terrain.GRASS,
        DG = Terrain.DARK_GRASS, FO = Terrain.FOREST, RK = Terrain.ROCKS;

    private MapGen() {}

    /** What the player asks for. */
    public static final class Options {
        public int players = 4, size = 96, kind = LAND, tileset, resources = MEDIUM, symmetry = AUTO;
        public long seed = 1;

        public Options() {}

        public Options(int players, int size, int kind, int tileset, int resources, int symmetry, long seed) {
            this.players = players; this.size = size; this.kind = kind; this.tileset = tileset;
            this.resources = resources; this.symmetry = symmetry; this.seed = seed;
        }
    }

    /** The most players a map of a size and kind takes; 0 when the kind does not fit the size. */
    public static int mostPlayers(int size, int kind) {
        final int most = (size == 32) ? 4 : (size == 64) ? 6 : (size == 96 || size == 128) ? 8 : 0;
        if (kind == ISLANDS) {
            return (size == 32) ? 0 : (size == 64) ? 4 : (size == 96) ? 7 : most;
        }
        return (kind == COAST && size < 64) ? 0 : most;
    }

    /**
     * The same for a level of resources. With high resources every player has a mine of his own
     * in the middle as well; on a 64 coast map with five or six players those clearings and the
     * ways between the players leave the sea in the middle no room for an oil patch a player
     * (counted over 20 numbers: no map for 11 of 20 with five players, 20 of 20 with six), so
     * such a map takes four.
     */
    public static int mostPlayers(int size, int kind, int resources) {
        final int most = mostPlayers(size, kind);
        return (kind == COAST && size == 64 && resources == HIGH) ? Math.min(most, 4) : most;
    }

    /** The exact symmetries a number of players has; empty for a number that has none. */
    public static int[] exactSymmetries(int players) {
        return (players == 2) ? new int[] { ANTI_DIAG, DIAG, MIRROR_X, MIRROR_Y, ROT180 }
            : (players == 4) ? new int[] { DIAG_XY, MIRROR_XY, ROT90 }
            : (players == 8) ? new int[] { D4 } : new int[0];
    }

    /** True when the options can be asked of the generator. */
    public static boolean allowed(Options o) {
        if (o.resources < LOW || o.resources > HIGH || o.players < 2
            || o.players > mostPlayers(o.size, o.kind, o.resources) || o.tileset < 0 || o.tileset > 3) {
            return false;
        }
        if (o.symmetry == AUTO || o.symmetry == NONE || o.symmetry == ROT_N) {
            return true;
        }
        for (int s : exactSymmetries(o.players)) {
            if (s == o.symmetry) {
                return true;
            }
        }
        return false;
    }

    /** A generated map and how it came about. */
    public static final class Result {
        public MapModel map;
        /** The number the map was made with: the asked one, or a later one of its row. */
        public long seedUsed;
        public int attempts, symmetry;
        public double fairness;
        public MapMetrics.Measure measure;
    }

    private static final class Rejected extends RuntimeException {
        private static final long serialVersionUID = 1L;

        Rejected(String why) { super(why, null, false, false); }
    }

    /** The random numbers: the same on every device. */
    private static final class Rng {
        private long state;

        Rng(long seed) { state = seed; }

        long next() {
            state += 0x9E3779B97F4A7C15L;
            long z = state;
            z = (z ^ (z >>> 30)) * 0xBF58476D1CE4E5B9L;
            z = (z ^ (z >>> 27)) * 0x94D049BB133111EBL;
            return z ^ (z >>> 31);
        }

        /** From 0 up to 1. */
        double unit() { return (next() >>> 11) * 0x1.0p-53; }
        int below(int n) { return (int) ((next() >>> 33) % n); }
        /** Mean 0, variance 1, bell shaped: four units summed. */
        double bell() { return (unit() + unit() + unit() + unit() - 2.0) * 1.7320508075688772; }
    }

    /** The symmetry of one map: one transform a player, about the map's centre. */
    private static final class Layout {
        final int n, size, style, fields;
        final double c;
        final double[][] mats;
        final boolean exact;

        Layout(int n, int size, int asked, Rng rng) {
            this.n = n;
            this.size = size;
            c = size / 2.0;
            int s = asked;
            if (s == AUTO) {
                final int[] own = exactSymmetries(n);
                s = (own.length > 0) ? own[rng.below(own.length)] : ROT_N;
            }
            style = s;
            exact = s != ROT_N && s != NONE;
            mats = new double[n][];
            if (!exact) {
                for (int k = 0; k < n; ++k) {
                    final double a = 2.0 * Math.PI * k / n;
                    mats[k] = new double[] { StrictMath.cos(a), -StrictMath.sin(a), StrictMath.sin(a), StrictMath.cos(a) };
                }
            } else {
                final int[][] group = (s == ROT180) ? new int[][] { I2, R180 } : (s == MIRROR_X) ? new int[][] { I2, MX }
                    : (s == MIRROR_Y) ? new int[][] { I2, MY } : (s == DIAG) ? new int[][] { I2, DI }
                    : (s == ANTI_DIAG) ? new int[][] { I2, AD } : (s == ROT90) ? new int[][] { I2, R90, R180, R270 }
                    : (s == MIRROR_XY) ? new int[][] { I2, MX, MY, R180 } : (s == DIAG_XY) ? new int[][] { I2, DI, AD, R180 }
                    : new int[][] { I2, R90, R180, R270, MX, MY, DI, AD };
                for (int k = 0; k < n; ++k) {
                    mats[k] = new double[] { group[k][0], group[k][1], group[k][2], group[k][3] };
                }
            }
            fields = (s == NONE) ? 1 : n;
        }

        double[] apply(int k, double x, double y) {
            final double[] m = mats[k];
            return new double[] { m[0] * (x - c) + m[1] * (y - c) + c, m[2] * (x - c) + m[3] * (y - c) + c };
        }

        /** In the first player's frame, the point player k sees at (x, y). All transforms are orthogonal. */
        double pullX(int k, double x, double y) { return mats[k][0] * (x - c) + mats[k][2] * (y - c) + c; }
        double pullY(int k, double x, double y) { return mats[k][1] * (x - c) + mats[k][3] * (y - c) + c; }
    }

    private static double dist(double[] a, double[] b) {
        return Math.sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]));
    }

    /** The nearest centre of a footprint x footprint block of tiles. */
    private static double[] snap(double[] p, int footprint) {
        final double half = footprint / 2.0;
        return new double[] { Math.rint(p[0] - half) + half, Math.rint(p[1] - half) + half };
    }

    private static int topLeft(double centre, int footprint) { return (int) Math.rint(centre - footprint / 2.0); }

    private static double[] turned(double[] v, double degrees) {
        final double a = StrictMath.toRadians(degrees), cos = StrictMath.cos(a), sin = StrictMath.sin(a);
        return new double[] { v[0] * cos - v[1] * sin, v[0] * sin + v[1] * cos };
    }

    private static double leastPairDistance(double[][] p) {
        double least = Double.POSITIVE_INFINITY;
        for (int i = 0; i < p.length; ++i) {
            for (int j = 0; j < i; ++j) {
                least = Math.min(least, dist(p[i], p[j]));
            }
        }
        return least;
    }

    /** Smooth noise over the square a turned lattice can reach, mean 0 and deviation 1. */
    private static double[] noise(Rng rng, double wavelength, int m) {
        double[] a = new double[m * m];
        for (int i = 0; i < a.length; ++i) {
            a[i] = rng.bell();
        }
        final double sigma = wavelength / 2.5;
        final int radius = (int) (4.0 * sigma + 0.5);
        final double[] kernel = new double[2 * radius + 1];
        double sum = 0.0;
        for (int i = -radius; i <= radius; ++i) {
            kernel[i + radius] = StrictMath.exp(-0.5 * i * i / (sigma * sigma));
            sum += kernel[i + radius];
        }
        for (int i = 0; i < kernel.length; ++i) {
            kernel[i] /= sum;
        }
        // Blurred along the rows, then along the columns; the square wraps round.
        for (int pass = 0; pass < 2; ++pass) {
            final double[] b = new double[m * m];
            final int step = (pass == 0) ? 1 : m;
            for (int line = 0; line < m; ++line) {
                final int first = (pass == 0) ? line * m : line;
                for (int i = 0; i < m; ++i) {
                    double v = 0.0;
                    for (int k = -radius; k <= radius; ++k) {
                        v += kernel[k + radius] * a[first + Math.floorMod(i + k, m) * step];
                    }
                    b[first + i * step] = v;
                }
            }
            a = b;
        }
        double mean = 0.0, var = 0.0;
        for (double v : a) {
            mean += v;
        }
        mean /= a.length;
        for (double v : a) {
            var += (v - mean) * (v - mean);
        }
        final double deviation = Math.sqrt(var / a.length);
        for (int i = 0; i < a.length; ++i) {
            a[i] = (a[i] - mean) / deviation;
        }
        return a;
    }

    /** A noise field on the corner lattice that every transform of the layout leaves as it is. */
    private static double[] field(Rng rng, Layout lay, double wavelength) {
        final int size = lay.size, n1 = size + 1, off = size / 4 + 2, m = n1 + 2 * off;
        final double[] a = noise(rng, wavelength, m);
        final double[] out = new double[n1 * n1], parts = new double[lay.fields];
        final double scale = Math.sqrt(lay.fields);
        for (int y = 0; y < n1; ++y) {
            for (int x = 0; x < n1; ++x) {
                for (int k = 0; k < lay.fields; ++k) {
                    final double qx = lay.pullX(k, x, y) + off, qy = lay.pullY(k, x, y) + off;
                    if (lay.exact) {
                        parts[k] = a[Math.floorMod((int) Math.rint(qy), m) * m + Math.floorMod((int) Math.rint(qx), m)];
                    } else {
                        final int x0 = (int) Math.floor(qx), y0 = (int) Math.floor(qy);
                        final double fx = qx - x0, fy = qy - y0;
                        final int xa = Math.floorMod(x0, m), xb = Math.floorMod(x0 + 1, m);
                        final int ya = Math.floorMod(y0, m), yb = Math.floorMod(y0 + 1, m);
                        parts[k] = (a[ya * m + xa] * (1.0 - fx) + a[ya * m + xb] * fx) * (1.0 - fy)
                            + (a[yb * m + xa] * (1.0 - fx) + a[yb * m + xb] * fx) * fy;
                    }
                }
                // Summed in one order at a point and at its images.
                Arrays.sort(parts);
                double sum = 0.0;
                for (double p : parts) {
                    sum += p;
                }
                out[y * n1 + x] = sum / scale;
            }
        }
        return out;
    }

    /** The given fraction of the "where" points with the highest field values. */
    private static boolean[] top(double[] field, boolean[] where, double fraction) {
        final boolean[] out = new boolean[where.length];
        int count = 0;
        for (boolean b : where) {
            count += b ? 1 : 0;
        }
        if (fraction <= 0.0 || count == 0) {
            return out;
        }
        final double[] v = new double[count];
        for (int i = 0, j = 0; i < where.length; ++i) {
            if (where[i]) {
                v[j++] = field[i];
            }
        }
        Arrays.sort(v);
        final double at = (count - 1) * (1.0 - fraction);
        final int lo = (int) Math.floor(at), hi = Math.min(count - 1, lo + 1);
        final double threshold = v[lo] + (v[hi] - v[lo]) * (at - lo);
        for (int i = 0; i < where.length; ++i) {
            out[i] = where[i] && field[i] >= threshold;
        }
        return out;
    }

    /** One try: the lattice of kinds and where everything stands. */
    private static final class Draft {
        Layout lay;
        byte[] kind;
        double[][] starts;
        final ArrayList<double[]> mines = new ArrayList<double[]>();      // centre x, y, amount
        final ArrayList<double[]> patches = new ArrayList<double[]>();
    }

    private static void set(byte[] t, boolean[] mask, byte kind) {
        for (int i = 0; i < t.length; ++i) {
            if (mask[i]) {
                t[i] = kind;
            }
        }
    }

    private static void disc(byte[] t, int n1, double[] centre, double radius, byte kind) {
        final int x0 = Math.max(0, (int) Math.floor(centre[0] - radius)), x1 = Math.min(n1 - 1, (int) Math.ceil(centre[0] + radius));
        final int y0 = Math.max(0, (int) Math.floor(centre[1] - radius)), y1 = Math.min(n1 - 1, (int) Math.ceil(centre[1] + radius));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                if ((x - centre[0]) * (x - centre[0]) + (y - centre[1]) * (y - centre[1]) <= radius * radius) {
                    t[y * n1 + x] = kind;
                }
            }
        }
    }

    /** The lattice points of the cheapest path in eight directions between two points. */
    private static int[] leastCostPath(double[] cost, int n1, double[] a, double[] b) {
        final int src = (int) Math.rint(a[1]) * n1 + (int) Math.rint(a[0]);
        final int dst = (int) Math.rint(b[1]) * n1 + (int) Math.rint(b[0]);
        final double[] d = new double[n1 * n1];
        final int[] from = new int[n1 * n1];
        Arrays.fill(d, Double.POSITIVE_INFINITY);
        Arrays.fill(from, -1);
        d[src] = 0.0;
        final MapMetrics.Heap heap = new MapMetrics.Heap();
        heap.add(0.0, src);
        while (!heap.isEmpty()) {
            final double at = heap.topKey();
            final int cell = heap.pop();
            if (at > d[cell]) {
                continue;
            }
            if (cell == dst) {
                break;
            }
            final int x = cell % n1, y = cell / n1;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    final int nx = x + dx, ny = y + dy;
                    if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= n1 || ny >= n1) {
                        continue;
                    }
                    final int next = ny * n1 + nx;
                    final double to = at + (cost[cell] + cost[next]) * 0.5 * ((dx != 0 && dy != 0) ? 1.4142 : 1.0);
                    if (to < d[next]) {
                        d[next] = to;
                        from[next] = cell;
                        heap.add(to, next);
                    }
                }
            }
        }
        int count = 0;
        for (int at = dst; at >= 0; at = from[at]) {
            ++count;
        }
        final int[] out = new int[count];
        for (int at = dst, i = 0; at >= 0; at = from[at]) {
            out[i++] = at;
        }
        return out;
    }

    /** True where every lattice point within three (a 7 x 7 block) is set; beyond the edge counts as given. */
    private static boolean[] allAround(boolean[] in, int n1, boolean beyond) {
        final boolean[] rows = new boolean[in.length], out = new boolean[in.length];
        for (int pass = 0; pass < 2; ++pass) {
            final boolean[] from = (pass == 0) ? in : rows, to = (pass == 0) ? rows : out;
            for (int y = 0; y < n1; ++y) {
                for (int x = 0; x < n1; ++x) {
                    boolean all = true;
                    for (int k = -3; k <= 3 && all; ++k) {
                        final int xx = (pass == 0) ? x + k : x, yy = (pass == 0) ? y : y + k;
                        if (xx < 0 || yy < 0 || xx >= n1 || yy >= n1) {
                            // "Nearest": the edge value goes on beyond the edge, so nothing is lost there.
                            all = beyond;
                        } else {
                            all = from[yy * n1 + xx];
                        }
                    }
                    to[y * n1 + x] = all;
                }
            }
        }
        return out;
    }

    private static Draft draft(Options opt, long seed) {
        final Rng rng = new Rng(seed), place = new Rng(seed ^ 0x5DEECE66DL);
        final int size = opt.size, n = opt.players, n1 = size + 1, count = n1 * n1;
        final double scale = size / 128.0;
        final Layout lay = new Layout(n, size, opt.symmetry, place);
        final double c = lay.c;
        final int[] amounts = RESOURCES[opt.resources];

        final double rClear = (size == 32) ? 4.5 : (size == 64) ? 5.5 : 7.0;     // radius of the buildable clearing
        final double rMine = Math.max(5.0, rClear - 0.5);                        // start to mine centre
        final double margin = rClear + ((size == 32) ? 1.5 : (size == 64) ? 4.0 : 5.0);

        // A start for the first player whose images are far from each other and from the map's edge.
        final ArrayList<double[]> found = new ArrayList<double[]>();
        double best = 0.0;
        for (int i = 0; i < 400; ++i) {
            final double px = margin + (size - 2.0 * margin) * place.unit(), py = margin + (size - 2.0 * margin) * place.unit();
            final double[] p = snap(new double[] { px, py }, 1);
            final double[][] orbit = new double[n][];
            boolean inside = true;
            for (int k = 0; k < n; ++k) {
                orbit[k] = lay.apply(k, p[0], p[1]);
                inside &= orbit[k][0] >= margin - 0.5 && orbit[k][0] <= size - margin + 0.5
                    && orbit[k][1] >= margin - 0.5 && orbit[k][1] <= size - margin + 0.5;
            }
            if (inside) {
                final double least = leastPairDistance(orbit);
                found.add(new double[] { p[0], p[1], least });
                best = Math.max(best, least);
            }
        }
        if (found.isEmpty()) {
            throw new Rejected("no start position found");
        }
        final ArrayList<double[]> roomy = new ArrayList<double[]>();
        for (double[] f : found) {
            if (f[2] >= 0.85 * best) {
                roomy.add(f);
            }
        }
        final double[] pick = roomy.get(place.below(roomy.size()));
        final double[] s0 = { pick[0], pick[1] };
        final double spacing = pick[2];
        final double[][] starts = new double[n][];
        for (int k = 0; k < n; ++k) {
            starts[k] = snap(lay.apply(k, s0[0], s0[1]), 1);
        }
        if (spacing < 2.0 * rClear + ((size >= 64) ? 6 : 3)) {
            throw new Rejected("starts too close");
        }
        final double[] near = new double[count];        // distance to the nearest start
        for (int y = 0; y < n1; ++y) {
            for (int x = 0; x < n1; ++x) {
                double least = Double.POSITIVE_INFINITY;
                for (double[] s : starts) {
                    least = Math.min(least, (x - s[0]) * (x - s[0]) + (y - s[1]) * (y - s[1]));
                }
                near[y * n1 + x] = Math.sqrt(least);
            }
        }

        // ---- the base kit of the first player; -1 = untouched ----
        final double norm = Math.sqrt((s0[0] - c) * (s0[0] - c) + (s0[1] - c) * (s0[1] - c));
        if (norm == 0.0) {
            throw new Rejected("the start lies on the centre");
        }
        final double[] outward = { (s0[0] - c) / norm, (s0[1] - c) / norm };
        double[] mine0 = null;
        int mineTurn = 0;
        for (int deg : new int[] { 35, -35, 70, -70, 0, 110, -110, 150, -150, 180 }) {
            final double[] v = turned(outward, deg);
            final double[] cand = snap(new double[] { s0[0] + rMine * v[0], s0[1] + rMine * v[1] }, 3);
            boolean ok = cand[0] >= 3.5 && cand[0] <= size - 3.5 && cand[1] >= 3.5 && cand[1] <= size - 3.5;
            for (int k = 1; k < n && ok; ++k) {
                ok = dist(cand, starts[k]) >= dist(cand, s0);
            }
            if (ok) {
                mine0 = cand;
                mineTurn = deg;
                break;
            }
        }
        if (mine0 == null) {
            throw new Rejected("no room for the home mine");
        }
        final byte[] kit = new byte[count];
        Arrays.fill(kit, (byte) -1);
        final double[] forestDir = turned(outward, (mineTurn != 0) ? -mineTurn : 180);
        final double arcCos = StrictMath.cos(StrictMath.toRadians(80.0));
        for (int y = 0; y < n1; ++y) {
            for (int x = 0; x < n1; ++x) {
                final double vx = x - s0[0], vy = y - s0[1], rr = Math.sqrt(vx * vx + vy * vy);
                if (rr > rClear + 0.6 && rr <= rClear + 4.0
                    && (vx * forestDir[0] + vy * forestDir[1]) / Math.max(rr, 1e-9) >= arcCos) {
                    kit[y * n1 + x] = FO;
                }
            }
        }
        disc(kit, n1, s0, rClear, LG);
        disc(kit, n1, mine0, 3.3, LG);

        // The expansion: toward the centre and to one side, nearer to this player than to any other.
        double[] exp0 = null;
        if (size >= 64) {
            final double far = 0.15 * size + 7.0;
            for (int deg : new int[] { 140, -140, 115, -115, 165, -165, 180, 90, -90 }) {
                final double[] v = turned(outward, deg);
                final double[] cand = snap(new double[] { s0[0] + far * v[0], s0[1] + far * v[1] }, 3);
                if (cand[0] < 6.0 || cand[0] > size - 6.0 || cand[1] < 6.0 || cand[1] > size - 6.0) {
                    continue;
                }
                final double[][] images = new double[n][];
                double others = Double.POSITIVE_INFINITY;
                for (int k = 0; k < n; ++k) {
                    images[k] = snap(lay.apply(k, cand[0], cand[1]), 3);
                    if (k > 0) {
                        others = Math.min(others, dist(cand, starts[k]));
                    }
                }
                if (others < dist(cand, s0) + 4.0 || leastPairDistance(images) < 12.0) {
                    continue;
                }
                exp0 = cand;
                break;
            }
        }
        if (exp0 != null) {
            disc(kit, n1, exp0, (opt.kind == ISLANDS) ? 5.5 : 4.3, LG);
        }

        // ---- the terrain round the kits ----
        final double[] fForest = field(rng, lay, 9.0 + 6.0 * scale), fRock = field(rng, lay, 12.0 + 8.0 * scale);
        final double[] fWater = field(rng, lay, 18.0 + 14.0 * scale), fDark = field(rng, lay, 8.0), fDeep = field(rng, lay, 10.0);
        final byte[] t = new byte[count];
        Arrays.fill(t, LG);
        final boolean[] where = new boolean[count];
        if (opt.kind == ISLANDS) {
            final double rIsle = Math.max(rClear + 6.0, Math.min(spacing / 2.0 - 3.5, 0.17 * size + 5.0));
            if (rIsle > spacing / 2.0 - 2.0) {
                throw new Rejected("no room for separate islands");
            }
            for (int i = 0; i < count; ++i) {
                where[i] = near[i] > rIsle + 7.0;
            }
            final boolean[] islets = top(fRock, where, 0.05);
            for (int y = 0; y < n1; ++y) {
                for (int x = 0; x < n1; ++x) {
                    final int i = y * n1 + x;
                    final boolean land = (1.0 - near[i] / rIsle + 0.16 * fWater[i]) > 0.0 || near[i] <= rClear + 4.5;
                    final boolean edge = x < 2 || y < 2 || x > size - 2 || y > size - 2;
                    t[i] = ((land || islets[i]) && !edge) ? LG : LW;
                    where[i] = t[i] == LG && near[i] > rClear + 4.5;
                }
            }
            set(t, top(fForest, where, 0.45), FO);
        } else {
            final boolean[] away = new boolean[count];
            for (int i = 0; i < count; ++i) {
                away[i] = near[i] > rClear + 5.0;
            }
            if (opt.kind == COAST) {
                // A sea toward the middle.
                final double[] sea = new double[count];
                for (int y = 0; y < n1; ++y) {
                    for (int x = 0; x < n1; ++x) {
                        final int i = y * n1 + x;
                        final double rad = Math.sqrt((x - c) * (x - c) + (y - c) * (y - c)) / (0.5 * size);
                        sea[i] = fWater[i] + 1.3 * Math.max(0.0, Math.min(1.0, 1.0 - rad / 0.55));
                        where[i] = near[i] > rClear + 6.0;
                    }
                }
                set(t, top(sea, where, 0.27), LW);
            } else {
                set(t, top(fWater, away, 0.02), LW);        // a few ponds
            }
            for (int i = 0; i < count; ++i) {
                where[i] = t[i] == LG && away[i];
            }
            set(t, top(fRock, where, 0.05), RK);
            for (int i = 0; i < count; ++i) {
                where[i] = t[i] == LG && away[i];
            }
            set(t, top(fForest, where, 0.24), FO);
        }

        // ---- the kit carried to every player ----
        final int[][] image = new int[n][count];        // the first player's lattice point seen by player k, or -1
        for (int k = 0; k < n; ++k) {
            for (int y = 0; y < n1; ++y) {
                for (int x = 0; x < n1; ++x) {
                    final int ix = (int) Math.rint(lay.pullX(k, x, y)), iy = (int) Math.rint(lay.pullY(k, x, y));
                    image[k][y * n1 + x] = (ix >= 0 && iy >= 0 && ix <= size && iy <= size) ? iy * n1 + ix : -1;
                }
            }
            for (int i = 0; i < count; ++i) {
                if (image[k][i] >= 0 && kit[image[k][i]] >= 0) {
                    t[i] = kit[image[k][i]];
                }
            }
        }

        final Draft out = new Draft();
        out.lay = lay;
        out.starts = starts;
        for (int k = 0; k < n; ++k) {
            // The image of the first player's mine; where the symmetry is not exact on the lattice,
            // the neighbouring position whose distance to the own start is nearest to the first player's.
            final double[] img = snap(lay.apply(k, mine0[0], mine0[1]), 3);
            double[] home = img;
            if (!lay.exact) {
                final double want = dist(mine0, s0);
                double bestOff = Double.POSITIVE_INFINITY, bestFar = Double.POSITIVE_INFINITY;
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        final double[] cand = { img[0] + dx, img[1] + dy };
                        if (cand[0] < 2.5 || cand[0] > size - 2.5 || cand[1] < 2.5 || cand[1] > size - 2.5) {
                            continue;
                        }
                        final double off = Math.rint(Math.abs(dist(cand, starts[k]) - want) * 1000.0) / 1000.0;
                        final double far = dist(cand, img);
                        if (off < bestOff || (off == bestOff && far < bestFar)) {
                            bestOff = off;
                            bestFar = far;
                            home = cand;
                        }
                    }
                }
            }
            out.mines.add(new double[] { home[0], home[1], amounts[0] });
            if (exp0 != null) {
                final double[] e = snap(lay.apply(k, exp0[0], exp0[1]), 3);
                out.mines.add(new double[] { e[0], e[1], amounts[1] });
            }
        }
        for (double[] m : out.mines) {
            disc(t, n1, m, 3.3, LG);
        }
        for (double[] s : starts) {
            disc(t, n1, s, 2.2, LG);
        }
        if (amounts[2] > 0 && size >= 64 && opt.kind != ISLANDS) {
            final double rMid = 0.09 * size + 3.0;
            final double angle = StrictMath.atan2(s0[1] - c, s0[0] - c) + Math.PI / n + 0.2;
            final double[] mid0 = snap(new double[] { c + rMid * StrictMath.cos(angle), c + rMid * StrictMath.sin(angle) }, 3);
            final double[][] mids = new double[n][];
            for (int k = 0; k < n; ++k) {
                mids[k] = snap(lay.apply(k, mid0[0], mid0[1]), 3);
            }
            if (leastPairDistance(mids) >= 6.0) {
                for (double[] m : mids) {
                    disc(t, n1, m, 3.3, LG);
                    out.mines.add(new double[] { m[0], m[1], amounts[2] });
                }
            }
        }

        // ---- ways between the players ----
        if (opt.kind != ISLANDS) {
            final double[] centre = { c, c };
            if (opt.kind == LAND) {
                disc(t, n1, centre, Math.max(3.0, 0.05 * size), LG);
            }
            final double[] cost = new double[count];
            for (int i = 0; i < count; ++i) {
                cost[i] = ((t[i] == FO) ? 5.0 : (t[i] == RK) ? 9.0 : (t[i] == LW || t[i] == DW) ? 14.0 : 1.0)
                    + 0.35 * Math.abs(fDark[i]);
            }
            final byte[] cut = new byte[count], cut0 = new byte[count];
            for (int k = 0; k < (lay.exact ? 1 : n); ++k) {
                final ArrayList<double[]> ends = new ArrayList<double[]>();
                if (opt.kind == LAND) {
                    ends.add(centre);
                } else {
                    ends.add(starts[(k + 1) % n]);
                    if (n > 2) {
                        int nearest = -1;
                        for (int j = 0; j < n; ++j) {
                            if (j != k && (nearest < 0 || dist(starts[k], starts[j]) < dist(starts[k], starts[nearest]))) {
                                nearest = j;
                            }
                        }
                        ends.add(starts[nearest]);
                    }
                }
                if (exp0 != null) {
                    ends.add(lay.apply(k, exp0[0], exp0[1]));
                }
                for (double[] end : ends) {
                    for (int at : leastCostPath(cost, n1, starts[k], end)) {
                        disc(lay.exact ? cut0 : cut, n1, new double[] { at % n1, at / n1 }, 1.6, (byte) 1);
                    }
                }
            }
            if (lay.exact) {
                for (int k = 0; k < n; ++k) {
                    for (int i = 0; i < count; ++i) {
                        if (image[k][i] >= 0 && cut0[image[k][i]] != 0) {
                            cut[i] = 1;
                        }
                    }
                }
            }
            for (int i = 0; i < count; ++i) {
                if (cut[i] != 0) {
                    t[i] = LG;
                }
            }
        }

        // ---- the terrain finished ----
        for (int i = 0; i < count; ++i) {
            where[i] = t[i] == LG;
        }
        set(t, top(fDark, where, 0.18), DG);
        if (!Terrain.repair(t, n1, n1)) {
            throw new Rejected("the repair pass did not settle");
        }
        for (int i = 0; i < count; ++i) {
            where[i] = t[i] == LW;
        }
        final boolean[] deep = allAround(where, n1, true);
        for (int i = 0; i < count; ++i) {
            if (deep[i] && fDeep[i] > -0.3) {
                t[i] = DW;
            }
        }
        if (!Terrain.repair(t, n1, n1)) {
            throw new Rejected("the repair pass did not settle");
        }
        out.kind = t;

        // ---- oil ----
        if (opt.kind != LAND) {
            final boolean[] water = new boolean[count];
            for (int i = 0; i < count; ++i) {
                water[i] = t[i] == LW || t[i] == DW;
            }
            final boolean[] roomy3 = allAround(water, n1, false);       // a 3 x 3 patch and a margin
            final ArrayList<double[]> points = new ArrayList<double[]>();
            for (int i = 0; i < count; ++i) {
                if (roomy3[i]) {
                    final double[] p = { i % n1 + 0.5, i / n1 + 0.5, 0.0 };
                    p[2] = Math.abs(dist(p, s0) - (rClear + 9.0));
                    points.add(p);
                }
            }
            java.util.Collections.sort(points, new java.util.Comparator<double[]>() {
                @Override public int compare(double[] a, double[] b) { return Double.compare(a[2], b[2]); }
            });
            double[][] chosen = null;
            for (int i = 0; i < Math.min(400, points.size()) && chosen == null; ++i) {
                final double[] p = snap(points.get(i), 3);
                boolean ok = true;
                for (int k = 1; k < n && ok; ++k) {
                    ok = dist(p, starts[k]) >= dist(p, s0) + 3.0;
                }
                if (!ok) {
                    continue;
                }
                final double[][] images = new double[n][];
                for (int k = 0; k < n && ok; ++k) {
                    images[k] = snap(lay.apply(k, p[0], p[1]), 3);
                    final int x = topLeft(images[k][0], 3), y = topLeft(images[k][1], 3);
                    ok = x >= 1 && y >= 1 && x + 3 <= size - 1 && y + 3 <= size - 1;
                    for (int yy = y; yy < y + 4 && ok; ++yy) {
                        for (int xx = x; xx < x + 4 && ok; ++xx) {
                            ok = water[yy * n1 + xx];
                        }
                    }
                }
                if (ok && leastPairDistance(images) >= 5.0) {
                    chosen = images;
                }
            }
            if (chosen == null) {
                throw new Rejected("no water for the oil patches");
            }
            for (double[] q : chosen) {
                out.patches.add(new double[] { q[0], q[1], amounts[3] });
            }
        }
        return out;
    }

    /** The map of a draft: tiles from the corners, the units, the sixteen slots. */
    private static MapModel model(Options opt, long seed, Draft d, Terrain terrain) {
        final int size = opt.size, n = opt.players;
        final MapModel m = new MapModel(size, size, opt.tileset, 0);
        final int[] tiles;
        try {
            tiles = terrain.tilesFromCorners(d.kind, size, size, new Random(seed + 7919));
        } catch (IllegalStateException e) {
            throw new Rejected(e.getMessage());
        }
        for (int i = 0; i < tiles.length; ++i) {
            m.loadTile(i, tiles[i]);
        }
        m.loadDescription("Random " + KIND_NAMES[opt.kind] + " " + n + "p #" + seed);
        for (int s = 0; s < MapModel.SLOTS; ++s) {
            m.loadSlot(MapModel.FIELD_GOLD, s, MapRules.START_GOLD);
            m.loadSlot(MapModel.FIELD_LUMBER, s, MapRules.START_LUMBER);
            m.loadSlot(MapModel.FIELD_OIL, s, MapRules.START_OIL);
            if (s < n) {
                m.loadSlot(MapModel.FIELD_OWNER, s, MapModel.OWNER_PERSON);
                m.loadSlot(MapModel.FIELD_RACE, s, s % 2);
                m.loadSlot(MapModel.FIELD_AI, s, (opt.kind == ISLANDS) ? MapRules.AI_SEA : MapRules.AI_LAND);
            }
        }
        final ArrayList<MapModel.Unit> units = new ArrayList<MapModel.Unit>();
        for (int k = 0; k < n; ++k) {
            final boolean orc = k % 2 != 0;
            final int sx = topLeft(d.starts[k][0], 1), sy = topLeft(d.starts[k][1], 1);
            units.add(new MapModel.Unit(sx, sy, orc ? MapRules.ORC_START : MapRules.HUMAN_START, k, 0));
            units.add(new MapModel.Unit(sx + ((d.starts[k][0] < d.lay.c) ? 1 : -1), sy,
                                        orc ? MapRules.PEON : MapRules.PEASANT, k, 1));
        }
        for (double[] mine : d.mines) {
            units.add(new MapModel.Unit(topLeft(mine[0], 3), topLeft(mine[1], 3), MapRules.GOLD_MINE,
                                        MapModel.NEUTRAL_SLOT, (int) mine[2] / MapModel.RESOURCE_UNIT));
        }
        for (double[] patch : d.patches) {
            units.add(new MapModel.Unit(topLeft(patch[0], 3), topLeft(patch[1], 3), MapRules.OIL_PATCH,
                                        MapModel.NEUTRAL_SLOT, (int) patch[2] / MapModel.RESOURCE_UNIT));
        }
        for (MapModel.Unit u : units) {
            final int foot = MapRules.footprint(u.type);
            if (u.x < 0 || u.y < 0 || u.x + foot > size || u.y + foot > size) {
                throw new Rejected("a unit would lie outside the map");
            }
            m.loadUnit(u);
        }
        return m;
    }

    /**
     * Balancing for layouts that are not exactly symmetric: players with more forest near the
     * start than the poorest player lose their outermost forest corners.
     */
    private static boolean trimWood(Draft d, MapMetrics.Measure measure) {
        final int n1 = d.lay.size + 1;
        int least = Integer.MAX_VALUE;
        for (MapMetrics.Player p : measure.per) {
            least = Math.min(least, p.wood);
        }
        boolean changed = false;
        for (int k = 0; k < d.starts.length; ++k) {
            final int extra = measure.per[k].wood - (int) (least * 1.04);
            if (extra <= 0) {
                continue;
            }
            final double[] s = d.starts[k];
            final ArrayList<double[]> forest = new ArrayList<double[]>();
            for (int i = 0; i < d.kind.length; ++i) {
                final double far = Math.sqrt((i % n1 - s[0]) * (i % n1 - s[0]) + (i / n1 - s[1]) * (i / n1 - s[1]));
                if (d.kind[i] == FO && far <= MapMetrics.R_WOOD) {
                    forest.add(new double[] { i, far });
                }
            }
            java.util.Collections.sort(forest, new java.util.Comparator<double[]>() {
                @Override public int compare(double[] a, double[] b) { return Double.compare(b[1], a[1]); }
            });
            for (int i = 0; i < Math.min(forest.size(), Math.max(1, extra / 2)); ++i) {
                d.kind[(int) forest.get(i)[0]] = LG;
                changed = true;
            }
        }
        return changed;
    }

    /** One try with one number; null when the number gives no map, and why[0] says why. */
    private static Result attempt(Options opt, long seed, Terrain terrain, String[] why) {
        try {
            final Draft d = draft(opt, seed);
            final Result r = new Result();
            r.map = model(opt, seed, d, terrain);
            r.measure = MapMetrics.measure(r.map);
            for (int pass = 0; pass < 6 && !d.lay.exact; ++pass) {
                if (MapMetrics.woodSpread(r.measure) <= 0.06 || !trimWood(d, r.measure)) {
                    break;
                }
                if (!Terrain.repair(d.kind, opt.size + 1, opt.size + 1)) {
                    why[0] = "the repair pass did not settle";
                    return null;
                }
                r.map = model(opt, seed, d, terrain);
                r.measure = MapMetrics.measure(r.map);
            }
            r.seedUsed = seed;
            r.symmetry = d.lay.style;
            r.fairness = MapMetrics.fairness(r.measure);
            return r;
        } catch (Rejected e) {
            why[0] = e.getMessage();
            return null;
        }
    }

    /** Null when a try is a map that may be given out: sound, of the kind asked, a mine in every player's reach. */
    private static String unsound(Options opt, Result r) {
        if (MapRules.blocked(MapRules.check(r.map))) {
            return "the map check blocks it";
        }
        if (r.measure.per.length != opt.players) {
            return "a player is missing";
        }
        if (r.measure.kind != ((opt.kind == ISLANDS) ? MapMetrics.KIND_ISLANDS : MapMetrics.KIND_LAND)) {
            return "not the kind asked";
        }
        for (MapMetrics.Player p : r.measure.per) {
            if (Double.isInfinite(p.goldWalk)) {
                return "a player cannot reach a mine";
            }
        }
        return null;
    }

    /** Asked between two tries; true ends the work. */
    public interface Stop {
        boolean now();
    }

    /** How a request ended. */
    public static final int MADE = 0, NOT_ALLOWED = 1, NO_MAP = 2, OUT_OF_TIME = 3, STOPPED = 4;

    /** The answer to a request: always one of the kinds above, a map only with MADE. */
    public static final class Outcome {
        public int kind;
        public Result result;
        /** Tries made, and the milliseconds they took. */
        public int tries;
        public long millis;
        /** Why each try that gave no map was thrown away: reason and count, in the order met. */
        public final ArrayList<String> reasons = new ArrayList<String>();
        public final ArrayList<int[]> counts = new ArrayList<int[]>();

        void lost(String why) {
            final int at = reasons.indexOf(why);
            if (at >= 0) {
                ++counts.get(at)[0];
            } else {
                reasons.add(why);
                counts.add(new int[] { 1 });
            }
        }
    }

    /**
     * The map for a set of options: the first of the numbers seed, seed + 1000003, .. that
     * reaches the fairness target, else the fairest of the tries. Null when the options are
     * not allowed or no try gave a sound map.
     *
     * @param terrain the tileset of the options ({@link Terrain#tileTable})
     */
    public static Result generate(Options opt, Terrain terrain) {
        return make(opt, terrain, TRIES, 0, null).result;
    }

    public static Result generate(Options opt, Terrain terrain, int tries) {
        return make(opt, terrain, tries, 0, null).result;
    }

    /**
     * The same, with the way it ended. The work is bounded three ways: by the number of tries
     * (never more than TRIES), by the time (0 = no limit; looked at between tries) and by the
     * caller's stop. A request that ran out of time or was stopped gives no map, even if a try
     * had given one below the target: which map a number gives must not depend on how fast the
     * device is.
     */
    public static Outcome make(Options opt, Terrain terrain, int tries, long limitMillis, Stop stop) {
        final Outcome out = new Outcome();
        if (!allowed(opt)) {
            out.kind = NOT_ALLOWED;
            return out;
        }
        final long began = System.nanoTime();
        final String[] why = new String[1];
        Result best = null;
        out.kind = NO_MAP;
        for (int i = 0; i < Math.min(tries, TRIES); ++i) {
            out.millis = (System.nanoTime() - began) / 1000000L;
            if (stop != null && stop.now()) {
                out.kind = STOPPED;
                return out;
            }
            if (limitMillis > 0 && out.millis >= limitMillis) {
                out.kind = OUT_OF_TIME;
                return out;
            }
            ++out.tries;
            final Result r = attempt(opt, opt.seed + i * 1000003L, terrain, why);
            final String fault = (r == null) ? why[0] : unsound(opt, r);
            if (fault != null) {
                out.lost(fault);
                continue;
            }
            r.attempts = i + 1;
            if (best == null || r.fairness > best.fairness) {
                best = r;
            }
            if (r.fairness >= FAIRNESS_TARGET) {
                break;
            }
        }
        out.millis = (System.nanoTime() - began) / 1000000L;
        if (best != null) {
            out.kind = MADE;
            out.result = best;
        }
        return out;
    }
}
