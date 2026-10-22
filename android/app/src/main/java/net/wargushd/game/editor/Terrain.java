package net.wargushd.game.editor;

import java.util.Random;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The terrain of a map as one kind a tile corner. A tile is derived from its four corners;
 * neighbouring tiles share their corners, so they always fit. Nothing here knows the screen or
 * Android.
 *
 * Eight kinds form a tree with coast in the middle:
 *
 *     deep water - water - coast - grass - dark grass
 *                            |        |
 *                  dark coast, rocks   forest
 *
 * The tileset has a mixed tile for every two kinds joined in the tree and for no others, so the
 * four corners of a tile may hold one kind, or two that are joined.
 *
 * Tile numbers: 0x0010..0x008F solid tiles, sixteen a kind; 0x0100..0x07DF mixed tiles, 256 a
 * pair, bits 4..7 = (mask of the quarters that carry the pair's first kind: TL 1, TR 2, BL 4,
 * BR 8) - 1; the low four bits are the variant. Walls and every other number are not part of
 * the model: such a tile is never rewritten and the corners it touches are never changed.
 */
public final class Terrain {
    public static final int DEEP_WATER = 0, WATER = 1, COAST = 2, DARK_COAST = 3, GRASS = 4,
        DARK_GRASS = 5, FOREST = 6, ROCKS = 7;
    public static final int KINDS = 8;
    /** The brush sizes, in corners across. */
    public static final int[] BRUSHES = { 1, 2, 3, 5 };
    /** How a stroke is repeated: not at all, on two, four or eight sides of the map. */
    public static final int MIRROR_OFF = 0, MIRROR_2 = 1, MIRROR_4 = 2, MIRROR_8 = 3;

    /** Corner flags: a tile outside the model touches the corner; the tiles around it disagree. */
    public static final int LOCKED = 1, CONFLICT = 2;

    /** The kind one step nearer to coast. */
    private static final int[] PARENT = { WATER, COAST, COAST, COAST, COAST, GRASS, GRASS, COAST };
    /** Which of two kinds that cannot meet gives way in the repair pass: the lower. */
    private static final int[] PRIORITY = { 1, 7, 99, 5, 4, 2, 3, 6 };
    private static final int[] SOLID_BASE = { 0x20, 0x10, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 };
    /** Kind by solid slot (tile >> 4), -1 for none. */
    private static final int[] SOLID_KIND = { -1, WATER, DEEP_WATER, COAST, DARK_COAST, GRASS,
        DARK_GRASS, FOREST, ROCKS, -1, -1, -1, -1, -1, -1, -1 };
    /** First and second kind by mixed slot (tile >> 8). */
    private static final int[] MIXED_FIRST = { -1, DEEP_WATER, WATER, DARK_COAST, ROCKS, COAST, DARK_GRASS, FOREST };
    private static final int[] MIXED_SECOND = { -1, WATER, COAST, COAST, COAST, GRASS, GRASS, GRASS };
    /** Steps between two kinds along the tree. */
    private static final int[][] DIST = new int[KINDS][KINDS];
    /** Decorated variants are taken this often where a plain one exists. */
    private static final double DECORATED = 0.04;

    static {
        for (int a = 0; a < KINDS; ++a) {
            for (int b = 0; b < KINDS; ++b) {
                int x = a, y = b, n = 0;
                while (x != y) {
                    // The deeper one climbs; coast is the root.
                    if (depth(x) >= depth(y)) {
                        x = PARENT[x];
                    } else {
                        y = PARENT[y];
                    }
                    ++n;
                }
                DIST[a][b] = n;
            }
        }
    }

    private static int depth(int kind) {
        int n = 0;
        while (kind != COAST) {
            kind = PARENT[kind];
            ++n;
        }
        return n;
    }

    /** True when a tile exists that holds both kinds (or they are one). */
    public static boolean legal(int a, int b) { return DIST[a][b] <= 1; }

    /** The kind one step from a on the way to b. */
    static int toward(int a, int b) {
        if (a == b) {
            return a;
        }
        if (DIST[PARENT[a]][b] < DIST[a][b]) {
            return PARENT[a];
        }
        int x = b;
        while (PARENT[x] != a) {
            x = PARENT[x];
        }
        return x;
    }

    /**
     * The four quarter kinds of a tile number as q(TL) | q(TR) << 4 | q(BL) << 8 | q(BR) << 12,
     * or -1 for a tile that is not part of the model (a wall, an unknown number).
     */
    public static int quad(int tile) {
        final int hi = tile >> 8, sub = (tile >> 4) & 0xF;
        if (hi == 0) {
            final int k = SOLID_KIND[sub];
            return (k < 0) ? -1 : k * 0x1111;
        }
        if (hi >= MIXED_FIRST.length || sub > 13) {
            return -1;
        }
        final int a = MIXED_FIRST[hi], b = MIXED_SECOND[hi], mask = sub + 1;
        int q = 0;
        for (int i = 0; i < 4; ++i) {
            q |= (((mask >> i) & 1) != 0 ? a : b) << (4 * i);
        }
        return q;
    }

    /**
     * The kind a tile shows at a point of it (fx, fy from 0 to 1, from its top left): the kind
     * of the quarter the point lies in, or -1 for a tile that is not part of the model.
     */
    public static int kindAt(int tile, float fx, float fy) {
        final int q = quad(tile);
        if (q < 0) {
            return -1;
        }
        final int quarter = ((fx >= 0.5f) ? 1 : 0) + ((fy >= 0.5f) ? 2 : 0);
        return (q >> (4 * quarter)) & 0xF;
    }

    /** The tile number without its variant for four corner kinds, or -1 when no tile holds them. */
    public static int base(int tl, int tr, int bl, int br) {
        final int[] q = { tl, tr, bl, br };
        int other = -1;
        for (int i = 1; i < 4; ++i) {
            if (q[i] != tl) {
                if (other >= 0 && q[i] != other) {
                    return -1;
                }
                other = q[i];
            }
        }
        if (other < 0) {
            return SOLID_BASE[tl];
        }
        for (int slot = 1; slot < MIXED_FIRST.length; ++slot) {
            final int a = MIXED_FIRST[slot], b = MIXED_SECOND[slot];
            if ((a == tl && b == other) || (a == other && b == tl)) {
                int mask = 0;
                for (int i = 0; i < 4; ++i) {
                    mask |= (q[i] == a) ? (1 << i) : 0;
                }
                return (slot << 8) | ((mask - 1) << 4);
            }
        }
        return -1;
    }

    /**
     * The tile table of a tileset script (the rows of its "slots"): the picture number of every
     * tile number, 0 where the tileset has no such tile. Null when the text is not such a script.
     */
    public static int[] tileTable(java.io.File script) {
        if (!script.isFile() || script.length() > 8L * 1024 * 1024) {
            return null;
        }
        final byte[] all = new byte[(int) script.length()];
        try (java.io.InputStream in = new java.io.FileInputStream(script)) {
            int got = 0;
            while (got < all.length) {
                final int n = in.read(all, got, all.length - got);
                if (n < 0) {
                    return null;
                }
                got += n;
            }
        } catch (java.io.IOException e) {
            return null;
        }
        return tileTable(new String(all, java.nio.charset.StandardCharsets.ISO_8859_1));
    }

    /** As above, of the script's text. */
    public static int[] tileTable(String script) {
        final int slots = (script == null) ? -1 : script.indexOf("\"slots\"");
        if (slots < 0) {
            return null;
        }
        final String text = script.substring(slots).replaceAll("--[^\\n]*", "");
        final int first = text.indexOf("\"solid\"");      // the "special" block has no rows
        if (first < 0) {
            return null;
        }
        final Matcher rows = Pattern.compile("\\{([\\s\\d,]*)\\}").matcher(text.substring(first));
        final java.util.ArrayList<Integer> flat = new java.util.ArrayList<Integer>();
        final Pattern number = Pattern.compile("\\d+");
        while (rows.find()) {
            final Matcher n = number.matcher(rows.group(1));
            int got = 0;
            while (got < 16 && n.find()) {
                flat.add(Integer.parseInt(n.group()));
                ++got;
            }
            for (; got < 16; ++got) {
                flat.add(0);
            }
        }
        final int[] table = new int[flat.size()];
        for (int i = 0; i < table.length; ++i) {
            table[i] = flat.get(i);
        }
        return table;
    }

    private final int[] table;

    /** @param table the tileset's tile table ({@link #tileTable}) */
    public Terrain(int[] table) {
        this.table = table;
    }

    public boolean hasPicture(int tile) {
        return tile >= 0 && tile < table.length && table[tile] != 0;
    }

    /**
     * A variant of a tile: any that the tileset has a picture for. For a solid tile the first
     * three (four for water, deep water and rocks) nearly always and a decorated one now and then.
     * -1 when the tileset has no picture for the tile at all.
     */
    public int variant(int base, Random rng) {
        final int[] plain = new int[16], fancy = new int[16];
        int plains = 0, fancies = 0;
        final boolean solid = base < 0x100;
        final int limit = !solid ? 16 : (base == 0x10 || base == 0x20 || base == 0x80) ? 4 : 3;
        for (int v = 0; v < 16; ++v) {
            if (hasPicture(base + v)) {
                if (v < limit) {
                    plain[plains++] = v;
                } else {
                    fancy[fancies++] = v;
                }
            }
        }
        if (plains == 0 && fancies == 0) {
            return -1;
        }
        if (plains == 0 || (fancies > 0 && rng.nextDouble() < DECORATED)) {
            return base + fancy[rng.nextInt(fancies)];
        }
        return base + plain[rng.nextInt(plains)];
    }

    /** The tile for four corner kinds, or -1 when there is none. */
    public int tileFor(int tl, int tr, int bl, int br, Random rng) {
        final int b = base(tl, tr, bl, br);
        return (b < 0) ? -1 : variant(b, rng);
    }

    // ----- corners of a whole map -----

    /** The corner lattice of a map: (width + 1) x (height + 1) kinds, row by row, and their flags. */
    public static final class Corners {
        public final int width, height;     // of the map, in tiles
        public final byte[] kind;           // -1 where only tiles outside the model touch the corner
        public final byte[] flag;

        Corners(int width, int height) {
            this.width = width;
            this.height = height;
            kind = new byte[(width + 1) * (height + 1)];
            flag = new byte[kind.length];
        }

        public int index(int cx, int cy) { return cy * (width + 1) + cx; }
    }

    /**
     * Read the corners from the tiles. A corner takes the kind most of the tiles around it give
     * it (of two equally frequent kinds the one that wins in the repair pass); where they do not
     * all agree it is flagged, and the tiles keep their numbers until something is painted there.
     */
    public static Corners cornersOf(int[] tiles, int width, int height) {
        final Corners c = new Corners(width, height);
        for (int cy = 0; cy <= height; ++cy) {
            for (int cx = 0; cx <= width; ++cx) {
                readCorner(tiles, c, cx, cy);
            }
        }
        return c;
    }

    private static void readCorner(int[] tiles, Corners c, int cx, int cy) {
        final int w = c.width, h = c.height;
        int n = 0, flags = 0;
        int v0 = -1, v1 = -1, v2 = -1, v3 = -1;
        for (int i = 0; i < 4; ++i) {
            // The tile above left gives its bottom right quarter, and so on round the corner.
            final int tx = cx - 1 + (i & 1), ty = cy - 1 + (i >> 1);
            if (tx < 0 || ty < 0 || tx >= w || ty >= h) {
                continue;
            }
            final int q = quad(tiles[ty * w + tx]);
            if (q < 0) {
                flags |= LOCKED;
                continue;
            }
            final int k = (q >> (4 * (3 - i))) & 0xF;
            switch (n++) {
                case 0: v0 = k; break;
                case 1: v1 = k; break;
                case 2: v2 = k; break;
                default: v3 = k; break;
            }
        }
        int best = -1, bestCount = 0;
        final int[] v = { v0, v1, v2, v3 };
        for (int i = 0; i < n; ++i) {
            int count = 0;
            for (int j = 0; j < n; ++j) {
                count += (v[j] == v[i]) ? 1 : 0;
            }
            if (count < n) {
                flags |= CONFLICT;
            }
            if (count > bestCount || (count == bestCount && PRIORITY[v[i]] > PRIORITY[best])) {
                best = v[i];
                bestCount = count;
            }
        }
        final int at = c.index(cx, cy);
        c.kind[at] = (byte) best;
        c.flag[at] = (byte) flags;
    }

    /**
     * The repair pass on a whole lattice of kinds: of two neighbouring corners (eight
     * directions) that no tile can hold, the one of lower priority is replaced by the kind one
     * step nearer to coast; all replacements of a pass happen at once; repeated until none is
     * left. False when it did not settle in forty passes.
     */
    public static boolean repair(byte[] kind, int cornersWide, int cornersHigh) {
        final boolean[] demote = new boolean[kind.length];
        for (int pass = 0; pass < 40; ++pass) {
            boolean any = false;
            for (int y = 0; y < cornersHigh; ++y) {
                for (int x = 0; x < cornersWide; ++x) {
                    final int a = kind[y * cornersWide + x];
                    boolean d = false;
                    for (int dy = -1; dy <= 1 && !d; ++dy) {
                        for (int dx = -1; dx <= 1 && !d; ++dx) {
                            final int nx = x + dx, ny = y + dy;
                            if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= cornersWide || ny >= cornersHigh) {
                                continue;
                            }
                            final int b = kind[ny * cornersWide + nx];
                            d = !legal(a, b) && PRIORITY[a] < PRIORITY[b];
                        }
                    }
                    demote[y * cornersWide + x] = d;
                    any |= d;
                }
            }
            if (!any) {
                return true;
            }
            for (int i = 0; i < kind.length; ++i) {
                if (demote[i]) {
                    kind[i] = (byte) PARENT[kind[i]];
                }
            }
        }
        return false;
    }

    /**
     * Every tile of a map from a lattice of kinds (as a generator makes one).
     *
     * @throws IllegalStateException when some four corners have no tile
     */
    public int[] tilesFromCorners(byte[] kind, int width, int height, Random rng) {
        final int[] out = new int[width * height];
        final int cw = width + 1;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                final int t = tileFor(kind[y * cw + x], kind[y * cw + x + 1], kind[(y + 1) * cw + x],
                                      kind[(y + 1) * cw + x + 1], rng);
                if (t < 0) {
                    throw new IllegalStateException("no tile for the corners of " + x + "," + y);
                }
                out[y * width + x] = t;
            }
        }
        return out;
    }

    // ----- painting -----

    /** The first corner a brush of a size covers along one axis, for a finger at a map position in tiles. */
    public static int brushStart(float position, int size) {
        return ((size & 1) != 0) ? Math.round(position) - size / 2
                                 : (int) Math.floor(position) - size / 2 + 1;
    }

    public Stroke begin(MapModel map, long seed) {
        return new Stroke(map, seed);
    }

    /**
     * The top left tiles of a block of size x size tiles at (x, y) and of its images: mirrored
     * left to right (MIRROR_2), also top to bottom (MIRROR_4), also across the diagonals
     * (MIRROR_8, only on a square map), as the corners of a stroke are mirrored. The block
     * itself first; an image that falls on a block already in the list is left out.
     */
    public static int[][] images(int x, int y, int size, int w, int h, int mirror) {
        final int ix = w - x - size, iy = h - y - size;
        final java.util.ArrayList<int[]> out = new java.util.ArrayList<int[]>();
        addImage(out, x, y);
        if (mirror >= MIRROR_2) {
            addImage(out, ix, y);
        }
        if (mirror >= MIRROR_4) {
            addImage(out, x, iy);
            addImage(out, ix, iy);
        }
        if (mirror >= MIRROR_8 && w == h) {
            addImage(out, y, x);
            addImage(out, iy, x);
            addImage(out, y, ix);
            addImage(out, iy, ix);
        }
        return out.toArray(new int[0][]);
    }

    private static void addImage(java.util.List<int[]> out, int x, int y) {
        for (int[] p : out) {
            if (p[0] == x && p[1] == y) {
                return;
            }
        }
        out.add(new int[] { x, y });
    }

    /** The first tile a wall brush of a size (tiles across) covers along one axis, for a finger at a map position. */
    public static int wallStart(float position, int size) {
        return (int) Math.floor(position - (size - 1) / 2f);
    }

    /**
     * One brush stroke: the map's tiles as the stroke has left them so far, and at its end the
     * step that makes the change on the map. The map itself is not changed by the stroke.
     */
    public final class Stroke {
        private final MapModel map;
        private final int w, h;
        private final int[] tiles;
        private final Corners c;
        private final Random rng;
        private final boolean[] touched;
        private int[] list = new int[256], saved = new int[256];
        private byte[] savedKind = new byte[256];
        private int count, savedCount;
        private int bx0, by0, bx1, by1;
        /** Fill: how many corners every corner lies from the filled area; null while a brush paints. */
        private byte[] far;
        private int minX = Integer.MAX_VALUE, minY = Integer.MAX_VALUE, maxX = -1, maxY = -1;

        private Stroke(MapModel map, long seed) {
            this.map = map;
            w = map.width();
            h = map.height();
            tiles = new int[w * h];
            for (int i = 0; i < tiles.length; ++i) {
                tiles[i] = map.tileAt(i);
            }
            c = cornersOf(tiles, w, h);
            rng = new Random(seed);
            touched = new boolean[c.kind.length];
        }

        public int tile(int x, int y) { return tiles[y * w + x]; }
        /** The kind at a corner, -1 for none. */
        public int kind(int cx, int cy) { return c.kind[c.index(cx, cy)]; }
        public int flags(int cx, int cy) { return c.flag[c.index(cx, cy)]; }

        /** The tiles changed so far lie in minX..maxX, minY..maxY; maxX is -1 when there are none. */
        public int minX() { return minX; }
        public int minY() { return minY; }
        public int maxX() { return maxX; }
        public int maxY() { return maxY; }

        /** The step for the map, or null when the stroke changed nothing. */
        public MapModel.TileStep step() {
            final MapModel.TileEdit e = map.editTiles();
            for (int y = Math.max(0, minY); y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    if (tiles[y * w + x] != map.tile(x, y)) {
                        e.set(x, y, tiles[y * w + x]);
                    }
                }
            }
            return e.step();
        }

        private void touch(int at) {
            if (!touched[at]) {
                touched[at] = true;
                if (count == list.length) {
                    list = java.util.Arrays.copyOf(list, count * 2);
                }
                list[count++] = at;
            }
        }

        private void set(int at, int kind) {
            if (savedCount == saved.length) {
                saved = java.util.Arrays.copyOf(saved, savedCount * 2);
                savedKind = java.util.Arrays.copyOf(savedKind, savedCount * 2);
            }
            saved[savedCount] = at;
            savedKind[savedCount++] = c.kind[at];
            c.kind[at] = (byte) kind;
            touch(at);
        }

        /** How many corners a corner lies from the brush; 0 inside it. */
        private int ring(int at) {
            if (far != null) {
                return far[at];
            }
            final int cx = at % (w + 1), cy = at / (w + 1);
            final int dx = Math.max(Math.max(bx0 - cx, cx - bx1), 0), dy = Math.max(Math.max(by0 - cy, cy - by1), 0);
            return Math.max(dx, dy);
        }

        private boolean take(boolean ok) {
            if (!ok) {
                for (int i = savedCount - 1; i >= 0; --i) {
                    c.kind[saved[i]] = savedKind[i];
                }
            }
            for (int i = 0; i < count; ++i) {
                touched[list[i]] = false;
            }
            count = 0;
            savedCount = 0;
            return ok;
        }

        /**
         * Paint a kind with a brush of a size (corners across) at a map position in tiles.
         *
         * The corners under the brush take the kind. Every other corner within three of the
         * brush moves along the tree toward the painted kind until it is no further from it
         * than it lies from the brush: so the painted kind stays and the kinds between appear
         * round it. Then every tile with a changed corner is derived anew; a tile whose four
         * quarters already say what its corners say keeps its number.
         *
         * @return false when nothing was changed (also when no arrangement could be found
         *         beside tiles that are not part of the model)
         */
        public boolean dab(float x, float y, int kind, int size) {
            return dab(x, y, kind, size, MIRROR_OFF);
        }

        /**
         * A dab and its images: mirrored left to right (MIRROR_2), also top to bottom
         * (MIRROR_4), also across the diagonals (MIRROR_8; on a map that is not square as
         * MIRROR_4). The images are taken of the corners the brush covers, so a corner and
         * its image always get the same kind.
         *
         * @return false when nothing was changed
         */
        public boolean dab(float x, float y, int kind, int size, int mirror) {
            final int sx = brushStart(x, size), sy = brushStart(y, size);
            final int mx = w - (sx + size - 1), my = h - (sy + size - 1);
            boolean changed = dabAt(sx, sy, kind, size);
            if (mirror >= MIRROR_2 && mx != sx) {
                changed |= dabAt(mx, sy, kind, size);
            }
            if (mirror >= MIRROR_4) {
                changed |= dabAt(sx, my, kind, size);
                changed |= dabAt(mx, my, kind, size);
            }
            if (mirror >= MIRROR_8 && w == h) {
                changed |= dabAt(sy, sx, kind, size);
                changed |= dabAt(my, sx, kind, size);
                changed |= dabAt(sy, mx, kind, size);
                changed |= dabAt(my, mx, kind, size);
            }
            return changed;
        }

        private boolean dabAt(int startX, int startY, int kind, int size) {
            bx0 = startX;
            by0 = startY;
            bx1 = bx0 + size - 1;
            by1 = by0 + size - 1;
            if (bx1 < 0 || by1 < 0 || bx0 > w || by0 > h) {
                return false;
            }
            final int reach = 3;
            for (int cy = Math.max(0, by0 - reach); cy <= Math.min(h, by1 + reach); ++cy) {
                for (int cx = Math.max(0, bx0 - reach); cx <= Math.min(w, bx1 + reach); ++cx) {
                    final int at = c.index(cx, cy);
                    if ((c.flag[at] & LOCKED) != 0 || c.kind[at] < 0) {
                        continue;
                    }
                    final int far = ring(at);
                    if (far == 0) {
                        set(at, kind);
                        continue;
                    }
                    int k = c.kind[at];
                    while (DIST[k][kind] > far) {
                        k = toward(k, kind);
                    }
                    if (k != c.kind[at]) {
                        set(at, k);
                    }
                }
            }
            return derive();
        }

        /**
         * Fill: every corner of the kind under the finger that is joined to it through corners
         * of that kind (left, right, up, down) takes the chosen kind; the corners round the
         * filled area move toward it as round a brush, so that the kinds between appear. One
         * stroke, one step.
         *
         * @return false when nothing was changed: the kind is already there, the corner under
         *         the finger is not part of the model, or no arrangement could be found
         */
        public boolean fill(float x, float y, int kind) {
            final int cw = w + 1, ch = h + 1;
            final int sx = Math.max(0, Math.min(w, Math.round(x))), sy = Math.max(0, Math.min(h, Math.round(y)));
            final int start = c.index(sx, sy), from = c.kind[start];
            if (from < 0 || from == kind || (c.flag[start] & LOCKED) != 0) {
                return false;
            }
            final int reach = 3;
            final byte[] dist = new byte[c.kind.length];
            java.util.Arrays.fill(dist, (byte) (reach + 1));
            int[] queue = new int[c.kind.length];
            int head = 0, tail = 0;
            dist[start] = 0;
            queue[tail++] = start;
            while (head < tail) {
                final int at = queue[head++], cx = at % cw, cy = at / cw;
                for (int d = 0; d < 4; ++d) {
                    final int nx = cx + ((d == 0) ? -1 : (d == 1) ? 1 : 0), ny = cy + ((d == 2) ? -1 : (d == 3) ? 1 : 0);
                    if (nx < 0 || ny < 0 || nx >= cw || ny >= ch) {
                        continue;
                    }
                    final int next = ny * cw + nx;
                    if (dist[next] != 0 && c.kind[next] == from && (c.flag[next] & LOCKED) == 0) {
                        dist[next] = 0;
                        queue[tail++] = next;
                    }
                }
            }
            filled = tail;
            // Rings round the filled area, eight directions, as far as the kinds between reach.
            head = 0;
            while (head < tail) {
                final int at = queue[head++], cx = at % cw, cy = at / cw;
                if (dist[at] >= reach) {
                    continue;
                }
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        final int nx = cx + dx, ny = cy + dy;
                        if (nx < 0 || ny < 0 || nx >= cw || ny >= ch) {
                            continue;
                        }
                        final int next = ny * cw + nx;
                        if (dist[next] > dist[at] + 1) {
                            dist[next] = (byte) (dist[at] + 1);
                            queue[tail++] = next;
                        }
                    }
                }
            }
            far = dist;
            for (int i = 0; i < tail; ++i) {
                final int at = queue[i];
                if ((c.flag[at] & LOCKED) != 0 || c.kind[at] < 0) {
                    continue;
                }
                if (dist[at] == 0) {
                    set(at, kind);
                    continue;
                }
                int k = c.kind[at];
                while (DIST[k][kind] > dist[at]) {
                    k = toward(k, kind);
                }
                if (k != c.kind[at]) {
                    set(at, k);
                }
            }
            final boolean changed = derive();
            far = null;
            return changed;
        }

        private int filled;

        /** How many corners the last fill gave the chosen kind. */
        public int filled() { return filled; }

        // ----- walls -----

        /**
         * Paint a wall of a race (Walls.HUMAN, Walls.ORC) on the size x size tiles under a map
         * position and on their images. Each wall tile and each wall beside it takes the tile
         * its neighbours ask for (Walls.tileFor); the corners round a wall are not changed and
         * are held from then on, as round any tile outside the corner model.
         *
         * @return false when nothing was changed
         */
        public boolean wall(float x, float y, int race, int size, int mirror) {
            final java.util.LinkedHashSet<Integer> set = new java.util.LinkedHashSet<Integer>();
            for (int[] p : images(wallStart(x, size), wallStart(y, size), size, w, h, mirror)) {
                for (int ty = p[1]; ty < p[1] + size; ++ty) {
                    for (int tx = p[0]; tx < p[0] + size; ++tx) {
                        if (tx >= 0 && ty >= 0 && tx < w && ty < h && Walls.raceOf(tiles[ty * w + tx]) != race) {
                            set.add(ty * w + tx);
                        }
                    }
                }
            }
            if (set.isEmpty()) {
                return false;
            }
            for (int at : set) {
                // The engine first lays the plain wall of direction 0, then fixes it and its neighbours.
                tiles[at] = Walls.base(race, 0);
            }
            fixWalls(set);
            return true;
        }

        /**
         * Take away the wall on a tile and on its images: the tile becomes the ground the tiles
         * round it give its four corners (grass where they give none, or where no tile holds
         * that set), and the walls beside it are fixed.
         *
         * @return false when there was no wall
         */
        public boolean unwall(int x, int y, int mirror) {
            final java.util.LinkedHashSet<Integer> set = new java.util.LinkedHashSet<Integer>();
            for (int[] p : images(x, y, 1, w, h, mirror)) {
                if (p[0] >= 0 && p[1] >= 0 && p[0] < w && p[1] < h && Walls.isWall(tiles[p[1] * w + p[0]])) {
                    set.add(p[1] * w + p[0]);
                }
            }
            if (set.isEmpty()) {
                return false;
            }
            final int cw = w + 1;
            for (int at : set) {
                final int tx = at % w, ty = at / w;
                // The corners as the other tiles round them read them: the wall itself gives none.
                final int[] k = { c.kind[ty * cw + tx], c.kind[ty * cw + tx + 1], c.kind[(ty + 1) * cw + tx],
                    c.kind[(ty + 1) * cw + tx + 1] };
                for (int i = 0; i < 4; ++i) {
                    if (k[i] < 0) {
                        k[i] = GRASS;
                    }
                }
                int tile = tileFor(k[0], k[1], k[2], k[3], rng);
                if (tile < 0) {
                    tile = variant(SOLID_BASE[GRASS], rng);
                }
                tiles[at] = (tile < 0) ? SOLID_BASE[GRASS] : tile;
            }
            fixWalls(set);
            return true;
        }

        /** Fix the walls among the changed tiles and beside them; mark the changes; read their corners anew. */
        private void fixWalls(java.util.Set<Integer> changed) {
            final java.util.LinkedHashSet<Integer> fix = new java.util.LinkedHashSet<Integer>();
            for (int at : changed) {
                final int tx = at % w, ty = at / w;
                fix.add(at);
                if (tx > 0) {
                    fix.add(at - 1);
                }
                if (tx < w - 1) {
                    fix.add(at + 1);
                }
                if (ty > 0) {
                    fix.add(at - w);
                }
                if (ty < h - 1) {
                    fix.add(at + w);
                }
            }
            final int[] was = new int[fix.size()];
            int n = 0;
            for (int at : fix) {
                was[n++] = tiles[at];
            }
            // Directions from the tiles as they stand, written after all are found (the engine fixes one by one,
            // which gives the same: a wall's direction depends only on which neighbours are walls, not on their tiles).
            final int[] to = new int[fix.size()];
            n = 0;
            for (int at : fix) {
                final int race = Walls.raceOf(tiles[at]);
                to[n++] = (race < 0) ? tiles[at]
                    : Walls.tileFor(race, Walls.direction(tiles, w, h, at % w, at / w, race), tiles[at], Terrain.this);
            }
            n = 0;
            for (int at : fix) {
                tiles[at] = to[n++];
            }
            n = 0;
            for (int at : fix) {
                final int tx = at % w, ty = at / w;
                final boolean other = changed.contains(at) || to[n] != was[n];
                ++n;
                if (!other) {
                    continue;
                }
                minX = Math.min(minX, tx);
                minY = Math.min(minY, ty);
                maxX = Math.max(maxX, tx);
                maxY = Math.max(maxY, ty);
                for (int j = 0; j < 4; ++j) {
                    readCorner(tiles, c, tx + (j & 1), ty + (j >> 1));
                }
            }
        }

        /** Settle the corners set so far and write the tiles they ask for; all or nothing. */
        private boolean derive() {
            if (!settle()) {
                return take(false);
            }

            // The tiles to write, found before any is written so that a failure leaves all as it was.
            int n = 0;
            int[] at = new int[count * 4], to = new int[count * 4];
            final int cw = w + 1;
            for (int i = 0; i < count; ++i) {
                final int cx = list[i] % cw, cy = list[i] / cw;
                for (int j = 0; j < 4; ++j) {
                    final int tx = cx - 1 + (j & 1), ty = cy - 1 + (j >> 1);
                    if (tx < 0 || ty < 0 || tx >= w || ty >= h) {
                        continue;
                    }
                    final int q = quad(tiles[ty * w + tx]);
                    final int tl = c.kind[ty * cw + tx], tr = c.kind[ty * cw + tx + 1];
                    final int bl = c.kind[(ty + 1) * cw + tx], br = c.kind[(ty + 1) * cw + tx + 1];
                    if (q < 0 || q == (tl | tr << 4 | bl << 8 | br << 12)) {
                        continue;
                    }
                    final int tile = tileFor(tl, tr, bl, br, rng);
                    if (tile < 0) {
                        return take(false);
                    }
                    at[n] = ty * w + tx;
                    to[n++] = tile;
                }
            }
            boolean changed = false;
            for (int i = 0; i < n; ++i) {
                // A tile reached from two of its corners is written once.
                if (quad(tiles[at[i]]) == quad(to[i])) {
                    continue;
                }
                tiles[at[i]] = to[i];
                changed = true;
                final int tx = at[i] % w, ty = at[i] / w;
                minX = Math.min(minX, tx);
                minY = Math.min(minY, ty);
                maxX = Math.max(maxX, tx);
                maxY = Math.max(maxY, ty);
            }
            for (int i = 0; i < n; ++i) {
                final int tx = at[i] % w, ty = at[i] / w;
                for (int j = 0; j < 4; ++j) {
                    readCorner(tiles, c, tx + (j & 1), ty + (j >> 1));
                }
            }
            for (int i = 0; i < count; ++i) {
                readCorner(tiles, c, list[i] % cw, list[i] / cw);
            }
            take(true);
            return changed;
        }

        /**
         * Make the four corners of every tile that will be derived anew a set a tile exists
         * for. After a paint on a regular map there is nothing to do; this is for what the map
         * brought with it: corners its tiles disagree on, and corners that may not change.
         */
        private boolean settle() {
            final int cw = w + 1;
            final int[] four = new int[4];
            for (int pass = 0; pass < 64; ++pass) {
                boolean any = false;
                for (int i = 0; i < count; ++i) {
                    final int cx = list[i] % cw, cy = list[i] / cw;
                    for (int j = 0; j < 4; ++j) {
                        final int tx = cx - 1 + (j & 1), ty = cy - 1 + (j >> 1);
                        if (tx < 0 || ty < 0 || tx >= w || ty >= h || quad(tiles[ty * w + tx]) < 0) {
                            continue;
                        }
                        four[0] = ty * cw + tx;
                        four[1] = four[0] + 1;
                        four[2] = four[0] + cw;
                        four[3] = four[2] + 1;
                        for (int a = 0; a < 4; ++a) {
                            // A corner the tiles disagreed on is settled with the tiles round it.
                            if ((c.flag[four[a]] & CONFLICT) != 0 && !touched[four[a]]) {
                                touch(four[a]);
                                any = true;
                            }
                            for (int b = a + 1; b < 4; ++b) {
                                final int ka = c.kind[four[a]], kb = c.kind[four[b]];
                                if (legal(ka, kb)) {
                                    continue;
                                }
                                final boolean la = (c.flag[four[a]] & LOCKED) != 0, lb = (c.flag[four[b]] & LOCKED) != 0;
                                if (la && lb) {
                                    return false;
                                }
                                final int ra = ring(four[a]), rb = ring(four[b]);
                                // The one further from the brush gives way; at one distance, the weaker.
                                final boolean moveA = lb || (!la && (ra > rb || (ra == rb && PRIORITY[ka] < PRIORITY[kb])));
                                if (moveA) {
                                    set(four[a], toward(ka, kb));
                                } else {
                                    set(four[b], toward(kb, ka));
                                }
                                any = true;
                            }
                        }
                    }
                }
                if (!any) {
                    return true;
                }
            }
            return false;
        }
    }
}
