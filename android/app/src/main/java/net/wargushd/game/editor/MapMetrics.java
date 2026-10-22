package net.wargushd.game.editor;

import java.util.Arrays;

/**
 * Measurements of a map and the fairness score built on them: what each player has near the
 * start (gold, forest, room to build, the way to the others) and how far the players differ in
 * it. Nothing here knows the screen or Android.
 *
 * All distances are in tiles. A path distance is the shortest walk over land tiles in eight
 * directions, a diagonal step costing the square root of two and allowed only when both tiles
 * beside it are free. It is a geometric measure, not the game's own path finder.
 */
public final class MapMetrics {
    public static final int MOVE_WATER = 0, MOVE_COAST = 1, MOVE_LAND = 2, MOVE_FOREST = 3, MOVE_ROCK = 4,
        MOVE_WALL = 5, MOVE_UNKNOWN = 6;
    /** How the starts are joined over land: one start, all joined, none joined, some joined. */
    public static final int KIND_SINGLE = 0, KIND_LAND = 1, KIND_ISLANDS = 2, KIND_MIXED = 3;

    /** Path distance within which buildable tiles are counted. */
    static final double R_BUILD = 12.0;
    /** Straight distance within which forest tiles are counted, and the path distance of the land they touch. */
    static final double R_WOOD = 14.0, R_WOOD_PATH = 20.0;
    /** Softening of the gold share weight 1 / (d + soft)^2 and of the oil share weight. */
    private static final double GOLD_SOFT = 4.0, OIL_SOFT = 8.0;
    private static final double FAR = 1e9, DIAGONAL = Math.sqrt(2.0);
    private static final int RESOURCE_SIZE = 3;

    private MapMetrics() {}

    /** The movement class of a whole tile, by the flags of the tileset scripts. */
    public static int moveClass(int tile) {
        final int hi = tile >> 8;
        if (hi == 0) {
            final int slot = (tile >> 4) & 0xF;
            return (slot == 1 || slot == 2) ? MOVE_WATER : (slot >= 3 && slot <= 6) ? MOVE_LAND
                : (slot == 7) ? MOVE_FOREST : (slot == 8) ? MOVE_ROCK : (slot >= 9 && slot <= 12) ? MOVE_WALL
                : MOVE_UNKNOWN;
        }
        switch (hi) {
            case 1: return MOVE_WATER;
            case 2: return MOVE_COAST;
            case 3: case 5: case 6: return MOVE_LAND;
            case 7: return MOVE_FOREST;
            case 4: return MOVE_ROCK;
            case 8: case 9: return MOVE_WALL;
            default: return MOVE_UNKNOWN;
        }
    }

    /** What one player has. */
    public static final class Player {
        public int slot, x, y;
        /** Walk to the nearest gold mine; infinite when none can be reached. */
        public double goldWalk;
        /** The player's expected share of the map's gold and oil. */
        public double goldShare, oilShare;
        /** Forest tiles and buildable tiles near the start. */
        public int wood, build;
        /** Straight distance to the nearest other start; mean straight and mean path distance to all others. */
        public double enemyLine, enemyMeanLine, enemyMeanPath;
        /** Width of the way to the nearest opponent. */
        public int choke;
        /** The other starts this one is joined to over land, one bit a slot (bit 0 = slot 0). */
        public int reach;
    }

    /** The measurements of a map. */
    public static final class Measure {
        public int width, height, kind, mines, patches;
        public Player[] per;
    }

    /** A binary heap of (distance, cell) for the shortest-path searches. */
    static final class Heap {
        private double[] key = new double[1024];
        private int[] value = new int[1024];
        private int count;

        boolean isEmpty() { return count == 0; }
        double topKey() { return key[0]; }

        void add(double k, int v) {
            if (count == key.length) {
                key = Arrays.copyOf(key, count * 2);
                value = Arrays.copyOf(value, count * 2);
            }
            int i = count++;
            while (i > 0) {
                final int up = (i - 1) >> 1;
                if (key[up] <= k) {
                    break;
                }
                key[i] = key[up];
                value[i] = value[up];
                i = up;
            }
            key[i] = k;
            value[i] = v;
        }

        int pop() {
            final int top = value[0];
            final double k = key[--count];
            final int v = value[count];
            int i = 0;
            while (true) {
                int down = i * 2 + 1;
                if (down >= count) {
                    break;
                }
                if (down + 1 < count && key[down + 1] < key[down]) {
                    ++down;
                }
                if (key[down] >= k) {
                    break;
                }
                key[i] = key[down];
                value[i] = value[down];
                i = down;
            }
            key[i] = k;
            value[i] = v;
            return top;
        }
    }

    /** Path distances from a cell over the free cells; infinite where there is no way. */
    private static double[] distances(boolean[] free, int w, int h, int source) {
        final double[] d = new double[w * h];
        Arrays.fill(d, Double.POSITIVE_INFINITY);
        d[source] = 0.0;
        if (!free[source]) {
            return d;
        }
        final Heap heap = new Heap();
        heap.add(0.0, source);
        while (!heap.isEmpty()) {
            final double at = heap.topKey();
            final int cell = heap.pop();
            if (at > d[cell]) {
                continue;
            }
            final int x = cell % w, y = cell / w;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    final int nx = x + dx, ny = y + dy;
                    if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= w || ny >= h || !free[ny * w + nx]) {
                        continue;
                    }
                    final boolean diagonal = dx != 0 && dy != 0;
                    if (diagonal && (!free[y * w + nx] || !free[ny * w + x])) {
                        continue;
                    }
                    final double next = at + (diagonal ? DIAGONAL : 1.0);
                    if (next < d[ny * w + nx]) {
                        d[ny * w + nx] = next;
                        heap.add(next, ny * w + nx);
                    }
                }
            }
        }
        return d;
    }

    /** The free cell nearest to (x, y) within four cells, or (x, y) itself when there is none. */
    private static int snap(boolean[] free, int w, int h, int x, int y) {
        int best = -1, bestD = Integer.MAX_VALUE;
        for (int dy = -4; dy <= 4; ++dy) {
            for (int dx = -4; dx <= 4; ++dx) {
                final int xx = x + dx, yy = y + dy;
                if (xx >= 0 && yy >= 0 && xx < w && yy < h && free[yy * w + xx] && dx * dx + dy * dy < bestD) {
                    bestD = dx * dx + dy * dy;
                    best = yy * w + xx;
                }
            }
        }
        return (best >= 0) ? best : y * w + x;
    }

    /** The smallest distance on the ring of cells round a footprint at (x, y). */
    private static double ringMin(double[] dist, int w, int h, int x, int y, int size) {
        double least = Double.POSITIVE_INFINITY;
        for (int yy = Math.max(0, y - 1); yy < Math.min(h, y + size + 1); ++yy) {
            for (int xx = Math.max(0, x - 1); xx < Math.min(w, x + size + 1); ++xx) {
                if (xx >= x && xx < x + size && yy >= y && yy < y + size) {
                    continue;
                }
                least = Math.min(least, dist[yy * w + xx]);
            }
        }
        return least;
    }

    /** For every cell the steps (eight directions) to the nearest cell that is not free; the map's edge counts. */
    private static int[] clearance(boolean[] free, int w, int h) {
        final int[] c = new int[w * h];
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                final int i = y * w + x;
                if (!free[i]) {
                    continue;
                }
                int v = (x == 0 || y == 0) ? 0 : Math.min(Math.min(c[i - 1], c[i - w]), c[i - w - 1]);
                if (x == w - 1 || y == 0) {
                    v = 0;
                } else {
                    v = Math.min(v, c[i - w + 1]);
                }
                c[i] = v + 1;
            }
        }
        for (int y = h - 1; y >= 0; --y) {
            for (int x = w - 1; x >= 0; --x) {
                final int i = y * w + x;
                if (!free[i]) {
                    continue;
                }
                int v = (x == w - 1 || y == h - 1) ? 0 : Math.min(Math.min(c[i + 1], c[i + w]), c[i + w + 1]);
                if (x == 0 || y == h - 1) {
                    v = 0;
                } else {
                    v = Math.min(v, c[i + w - 1]);
                }
                c[i] = Math.min(c[i], v + 1);
            }
        }
        return c;
    }

    /** The largest clearance c such that a and b are joined through cells of clearance c or more; 0 if never. */
    private static int bottleneck(int[] clear, int w, int h, int a, int b) {
        int lo = 0, hi = 0;
        for (int v : clear) {
            hi = Math.max(hi, v);
        }
        final int[] queue = new int[w * h];
        final boolean[] seen = new boolean[w * h];
        while (lo < hi) {
            final int mid = (lo + hi + 1) / 2;
            Arrays.fill(seen, false);
            int head = 0, tail = 0;
            final int ax = a % w, ay = a / w, bx = b % w, by = b / w;
            for (int y = Math.max(0, ay - 4); y < Math.min(h, ay + 5); ++y) {
                for (int x = Math.max(0, ax - 4); x < Math.min(w, ax + 5); ++x) {
                    if (clear[y * w + x] >= mid) {
                        seen[y * w + x] = true;
                        queue[tail++] = y * w + x;
                    }
                }
            }
            boolean joined = false;
            while (head < tail && !joined) {
                final int cell = queue[head++], x = cell % w, y = cell / w;
                joined = Math.abs(x - bx) <= 4 && Math.abs(y - by) <= 4;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        final int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h || seen[ny * w + nx] || clear[ny * w + nx] < mid) {
                            continue;
                        }
                        seen[ny * w + nx] = true;
                        queue[tail++] = ny * w + nx;
                    }
                }
            }
            if (joined) {
                lo = mid;
            } else {
                hi = mid - 1;
            }
        }
        return lo;
    }

    private static double hypot(double x, double y) { return Math.sqrt(x * x + y * y); }

    /** Every measurement of a map. */
    public static Measure measure(MapModel m) {
        final int w = m.width(), h = m.height();
        final int[] move = new int[w * h];
        final boolean[] free = new boolean[w * h];
        for (int i = 0; i < w * h; ++i) {
            move[i] = moveClass(m.tileAt(i));
            free[i] = move[i] == MOVE_LAND;
        }
        final int[] startX = new int[MapRules.PLAYERS], startY = new int[MapRules.PLAYERS];
        final boolean[] has = new boolean[MapRules.PLAYERS];
        int mines = 0, patches = 0;
        for (int i = 0; i < m.unitCount(); ++i) {
            final MapModel.Unit u = m.unit(i);
            if (MapRules.isStart(u.type) && u.owner < MapRules.PLAYERS) {
                has[u.owner] = true;
                startX[u.owner] = u.x;
                startY[u.owner] = u.y;
            }
            mines += (u.type == MapRules.GOLD_MINE) ? 1 : 0;
            patches += (u.type == MapRules.OIL_PATCH) ? 1 : 0;
        }
        final int[] mineX = new int[mines], mineY = new int[mines], patchX = new int[patches], patchY = new int[patches];
        final double[] mineGold = new double[mines], patchOil = new double[patches];
        mines = 0;
        patches = 0;
        for (int i = 0; i < m.unitCount(); ++i) {
            final MapModel.Unit u = m.unit(i);
            if (u.type == MapRules.GOLD_MINE) {
                mineX[mines] = u.x;
                mineY[mines] = u.y;
                mineGold[mines++] = (double) u.data * MapModel.RESOURCE_UNIT;
                for (int yy = Math.max(0, u.y); yy < Math.min(h, u.y + RESOURCE_SIZE); ++yy) {
                    for (int xx = Math.max(0, u.x); xx < Math.min(w, u.x + RESOURCE_SIZE); ++xx) {
                        free[yy * w + xx] = false;
                    }
                }
            } else if (u.type == MapRules.OIL_PATCH) {
                patchX[patches] = u.x;
                patchY[patches] = u.y;
                patchOil[patches++] = (double) u.data * MapModel.RESOURCE_UNIT;
            }
        }
        int n = 0;
        for (boolean b : has) {
            n += b ? 1 : 0;
        }
        final Measure out = new Measure();
        out.width = w;
        out.height = h;
        out.mines = mines;
        out.patches = patches;
        out.per = new Player[n];
        final int[] src = new int[n];
        final double[][] dist = new double[n][];
        for (int s = 0, i = 0; s < MapRules.PLAYERS; ++s) {
            if (has[s]) {
                final Player p = new Player();
                p.slot = s;
                p.x = startX[s];
                p.y = startY[s];
                out.per[i] = p;
                src[i] = snap(free, w, h, Math.min(p.x, w - 1), Math.min(p.y, h - 1));
                dist[i] = distances(free, w, h, src[i]);
                ++i;
            }
        }
        final int[] clear = clearance(free, w, h);

        // Every mine's gold is split between the starts by 1 / (walk + soft)^2; the oil by the straight distance.
        final double[][] mineD = new double[n][mines];
        for (int j = 0; j < mines; ++j) {
            double total = 0.0;
            for (int i = 0; i < n; ++i) {
                mineD[i][j] = ringMin(dist[i], w, h, mineX[j], mineY[j], RESOURCE_SIZE);
                if (!Double.isInfinite(mineD[i][j])) {
                    total += 1.0 / ((mineD[i][j] + GOLD_SOFT) * (mineD[i][j] + GOLD_SOFT));
                }
            }
            for (int i = 0; i < n && total > 0.0; ++i) {
                if (!Double.isInfinite(mineD[i][j])) {
                    out.per[i].goldShare += mineGold[j] / ((mineD[i][j] + GOLD_SOFT) * (mineD[i][j] + GOLD_SOFT)) / total;
                }
            }
        }
        for (int j = 0; j < patches; ++j) {
            double total = 0.0;
            final double[] weight = new double[n];
            for (int i = 0; i < n; ++i) {
                final double d = hypot(patchX[j] + 1 - out.per[i].x, patchY[j] + 1 - out.per[i].y) + OIL_SOFT;
                weight[i] = 1.0 / (d * d);
                total += weight[i];
            }
            for (int i = 0; i < n; ++i) {
                out.per[i].oilShare += patchOil[j] * weight[i] / total;
            }
        }

        int pairs = 0, joined = 0;
        for (int i = 0; i < n; ++i) {
            final Player p = out.per[i];
            final double[] d = dist[i];
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    final int at = y * w + x;
                    if (MapRules.buildable(m.tileAt(at)) && d[at] <= R_BUILD) {
                        ++p.build;
                    }
                    if (move[at] != MOVE_FOREST || hypot(x - p.x, y - p.y) > R_WOOD) {
                        continue;
                    }
                    // A forest tile counts when land beside it can be walked to.
                    double near = FAR;
                    for (int yy = Math.max(0, y - 1); yy <= Math.min(h - 1, y + 1); ++yy) {
                        for (int xx = Math.max(0, x - 1); xx <= Math.min(w - 1, x + 1); ++xx) {
                            near = Math.min(near, d[yy * w + xx]);
                        }
                    }
                    p.wood += (near <= R_WOOD_PATH) ? 1 : 0;
                }
            }
            p.goldWalk = Double.POSITIVE_INFINITY;
            for (int j = 0; j < mines; ++j) {
                p.goldWalk = Math.min(p.goldWalk, mineD[i][j]);
            }
            p.enemyLine = Double.POSITIVE_INFINITY;
            double lineSum = 0.0, pathSum = 0.0, nearestPath = Double.POSITIVE_INFINITY;
            int nearest = -1;
            for (int k = 0; k < n; ++k) {
                if (k == i) {
                    continue;
                }
                final double line = hypot(out.per[k].x - p.x, out.per[k].y - p.y), path = d[src[k]];
                p.enemyLine = Math.min(p.enemyLine, line);
                lineSum += line;
                pathSum += path;
                if (!Double.isInfinite(path)) {
                    p.reach |= 1 << out.per[k].slot;
                }
                if (path < nearestPath) {
                    nearestPath = path;
                    nearest = k;
                }
                if (k > i) {
                    ++pairs;
                    joined += Double.isInfinite(path) ? 0 : 1;
                }
            }
            p.enemyMeanLine = (n > 1) ? lineSum / (n - 1) : Double.POSITIVE_INFINITY;
            p.enemyMeanPath = (n > 1) ? pathSum / (n - 1) : Double.POSITIVE_INFINITY;
            p.choke = (nearest >= 0) ? bottleneck(clear, w, h, src[i], src[nearest]) : 0;
        }
        out.kind = (pairs == 0) ? KIND_SINGLE : (joined == pairs) ? KIND_LAND : (joined == 0) ? KIND_ISLANDS
            : KIND_MIXED;
        return out;
    }

    /** (best player - worst player) / (mean + floor), cut at 1; 1 when some have it and some have not. */
    public static double spread(double[] v, double floor) {
        if (v.length == 0) {
            return 0.0;
        }
        int infinite = 0;
        double sum = 0.0, least = Double.POSITIVE_INFINITY, most = Double.NEGATIVE_INFINITY;
        for (double x : v) {
            infinite += (Double.isInfinite(x) || Double.isNaN(x)) ? 1 : 0;
            sum += x;
            least = Math.min(least, x);
            most = Math.max(most, x);
        }
        if (infinite > 0) {
            return (infinite == v.length) ? 0.0 : 1.0;
        }
        final double mean = sum / v.length;
        return (mean + floor <= 0.0) ? 0.0 : Math.min(1.0, (most - least) / (mean + floor));
    }

    /** The spread of the forest tiles near the starts. */
    static double woodSpread(Measure m) {
        final double[] v = new double[m.per.length];
        for (int i = 0; i < v.length; ++i) {
            v[i] = m.per[i].wood;
        }
        return spread(v, 10.0);
    }

    /**
     * The fairness of a measured map, 0 to 100: 100 x (1 - weighted mean of the spreads of gold
     * share, walk to the mine, forest, room to build, distance to the others, width of the way
     * to the nearest opponent, oil share). 100 = every player has the same numbers. The oil
     * share is left out on a map without oil and the way's width on a map that is not all
     * joined by land; the other weights are scaled up. Not a number for fewer than two players.
     */
    public static double fairness(Measure m) {
        final int n = m.per.length;
        if (n < 2) {
            return Double.NaN;
        }
        final double[] weight = { 0.30, 0.15, 0.15, 0.15, 0.10, 0.05, 0.10 };
        final double[] floor = { 0.0, 4.0, 10.0, 10.0, 0.0, 1.0, 0.0 };
        double total = 0.0, sum = 0.0;
        final double[] v = new double[n];
        for (int c = 0; c < weight.length; ++c) {
            if ((c == 6 && m.patches == 0) || (c == 5 && m.kind != KIND_LAND)) {
                continue;
            }
            for (int i = 0; i < n; ++i) {
                final Player p = m.per[i];
                v[i] = (c == 0) ? p.goldShare : (c == 1) ? p.goldWalk : (c == 2) ? p.wood : (c == 3) ? p.build
                    : (c == 4) ? ((m.kind == KIND_LAND) ? p.enemyMeanPath : p.enemyMeanLine)
                    : (c == 5) ? p.choke : p.oilShare;
            }
            sum += spread(v, floor[c]) * weight[c];
            total += weight[c];
        }
        return 100.0 * (1.0 - sum / total);
    }

    /** The parts of the fairness, in the order of its weights. */
    public static final int PART_GOLD = 0, PART_WALK = 1, PART_WOOD = 2, PART_ROOM = 3, PART_DISTANCE = 4,
        PART_WAY = 5, PART_OIL = 6, PARTS = 7;

    /**
     * What each part takes from the fairness of a measured map, in points of the 100: the
     * part's spread times its scaled weight; not a number for a part that is left out. The
     * fairness is 100 less their sum. Null for fewer than two players.
     */
    public static double[] parts(Measure m) {
        final int n = m.per.length;
        if (n < 2) {
            return null;
        }
        final double[] weight = { 0.30, 0.15, 0.15, 0.15, 0.10, 0.05, 0.10 };
        final double[] floor = { 0.0, 4.0, 10.0, 10.0, 0.0, 1.0, 0.0 };
        final double[] out = new double[PARTS];
        double total = 0.0;
        final double[] v = new double[n];
        for (int c = 0; c < PARTS; ++c) {
            if ((c == PART_OIL && m.patches == 0) || (c == PART_WAY && m.kind != KIND_LAND)) {
                out[c] = Double.NaN;
                continue;
            }
            for (int i = 0; i < n; ++i) {
                final Player p = m.per[i];
                v[i] = (c == 0) ? p.goldShare : (c == 1) ? p.goldWalk : (c == 2) ? p.wood : (c == 3) ? p.build
                    : (c == 4) ? ((m.kind == KIND_LAND) ? p.enemyMeanPath : p.enemyMeanLine)
                    : (c == 5) ? p.choke : p.oilShare;
            }
            out[c] = spread(v, floor[c]) * weight[c];
            total += weight[c];
        }
        for (int c = 0; c < PARTS; ++c) {
            out[c] = out[c] * 100.0 / total;
        }
        return out;
    }

    /** The part that takes most from the fairness, or -1 when none takes anything. */
    public static int weakest(Measure m) {
        final double[] parts = parts(m);
        int worst = -1;
        for (int c = 0; parts != null && c < PARTS; ++c) {
            if (!Double.isNaN(parts[c]) && parts[c] > 1e-9 && (worst < 0 || parts[c] > parts[worst])) {
                worst = c;
            }
        }
        return worst;
    }
}
