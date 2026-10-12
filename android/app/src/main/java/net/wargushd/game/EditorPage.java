package net.wargushd.game;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.view.MotionEvent;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import net.wargushd.game.editor.CanvasView;
import net.wargushd.game.editor.MapGen;
import net.wargushd.game.editor.MapModel;
import net.wargushd.game.editor.MapRules;
import net.wargushd.game.editor.PudFile;
import net.wargushd.game.editor.Terrain;
import net.wargushd.game.editor.WorkingCopy;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;

/**
 * The Map Editor section: the page of the player's own maps (maps/custom) - the list, the
 * chosen map's picture and facts, New Map, Duplicate, Edit Map, and by a long press on a row
 * Rename and Remove - and, over it, the editor's map view under a bar with Maps, Players, Undo,
 * Redo, Test and Save. Only files of the player's own store are ever written or removed.
 *
 * A map with unsaved work kept for later is a draft and is marked so in the list. A new map is
 * a draft alone until it is first saved: it is listed here and nowhere else.
 */
final class EditorPage {
    /** Takes a name typed in the shell's name sheet. */
    interface Named {
        void named(String name);
    }

    /** What the page asks of the shell that shows it. */
    interface Shell {
        void click();
        void error();
        void redraw();
        /** Open the name sheet; `then` gets the name when it is closed with OK. */
        void askName(String title, String initial, Named then);
        void later(Runnable r, long millis);
        void forget(Runnable r);
        /**
         * Start a test game on a map file written for it; `map` is the map being edited. The
         * game is not counted and shows no result window; the shell removes the file after it.
         *
         * @return true when the game was started
         */
        boolean test(File pud, File map);
    }

    // Layout of the page, logical units (the layout table's screen editor_maps).
    private static final float X0 = 816f, TITLE_Y = 84f, BOTTOM_UP = 264f;
    private static final float LIST_Y = 264f, LIST_W = 1192f;
    private static final float ROW_X = 840f, ROW_W = 1144f, ROW_H = 132f, ROW_PITCH = 138f, ROW_TOP = 24f;
    private static final float ROW_INSET = 36f, ROW_NAME_W = 588f, ACCENT_W = 10f;
    private static final float INFO_X = 2056f, INFO_Y = 264f, INFO_W = 1688f, INFO_H = 810f, INFO_PAD = 60f;
    private static final float PICTURE = 690f, LINE_PITCH = 82.5f;
    private static final float BUTTON_UP = 204f, BUTTON_W = 720f, BUTTON_H = 120f;
    private static final float[] BUTTON_X = { 816f, 2256f, 3024f };          // New Map, Duplicate, Edit Map
    private static final String[] BUTTON_WORD = { "New Map", "Duplicate", "Edit Map" };
    private static final int NEW = 0, DUPLICATE = 1, EDIT = 2;
    // Layout of the bar above the map view (screen editor_canvas).
    private static final float BAR_H = 168f, BACK_X = 48f, BAR_Y = 24f, BACK_W = 360f, BAR_BUTTON_H = 120f;
    private static final float NAME_X = 456f, NAME_W = 1716f, BAR_BUTTON_W = 300f, PLAYERS_X = 2196f;
    private static final float[] BAR_X = { 2520f, 2844f, 3492f };            // Undo, Redo, Save
    private static final float TEST_X = 3168f;
    // The mark of a draft in a row of the list.
    private static final float MARK_W = 132f, MARK_H = 60f, MARK_GAP = 12f, CAP_MARK = 28f;
    private static final float CAP_TITLE = 60f, CAP_BODY = 44f, CAP_MID = 36f;
    private static final long HOLD_MILLIS = 500L;

    private static final int PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E, BAR_FILL = 0xFF0E0B08;
    private static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, GOLD = 0xFFF0D250;
    private static final int INK_TITLE = 0xFF000000, ROW_SELECTED = 0x36F0D250, ROW_FAINT = 0x10FFFFFF;

    /** The tileset scripts, by the map file's tileset number. */
    private static final String[] ERA_SCRIPT = { "summer", "winter", "wasteland", "swamp" };
    private static final int GRASS_TILE = 0x50;

    private Activity activity;
    private SkinKit kit;
    private Shell shell;
    private final List<File> maps = new ArrayList<File>();
    /** By the list's rows: the map has unsaved work kept for later, or was never saved. */
    private final List<Boolean> drafts = new ArrayList<Boolean>();
    /** The facts of the maps that were never saved, read from their working copies. */
    private final java.util.Map<File, PudInfo> unsavedFacts = new java.util.HashMap<File, PudInfo>();
    private int selected;
    private float scroll;
    private int pressedRow = -1, pressedButton = -1;
    private float downY, downScroll;
    private boolean inList, dragging, heldOpened;
    private Bitmap buttonPlain, buttonMarked;

    // The page's sheets; at most one is open.
    private NewMapSheet newSheet;
    private RandomMapSheet randomSheet;
    private MapSheet mapSheet;
    private ConfirmSheet confirm;
    private Runnable confirmThen;

    /** The map view shown over the shell, or null; the map it shows; "Save changes?" over it. */
    private CanvasView canvas;
    private WorkingCopy work;
    private File shown;
    private ChoiceSheet leave;
    /** The sheet of the player slots and the sheet of what the checks found; over the map view. */
    private PlayersSheet players;
    private FaultSheet faults;
    private BalanceSheet balance;
    private boolean leaveAfterSave;
    /** A line shown in the bar in place of the map's name until the map is next changed, or null. */
    private String barNotice;

    /** Read the folder of the player's own maps again. */
    void open(Activity a, SkinKit k, Shell s) {
        activity = a;
        kit = k;
        shell = s;
        scroll = 0f;
        newSheet = null;
        closeRandom();
        mapSheet = null;
        confirm = null;
        confirmThen = null;
        load(null);
    }

    /** Read the list; `keep` stays the chosen map when it is still there. */
    private void load(File keep) {
        maps.clear();
        drafts.clear();
        unsavedFacts.clear();
        final File dir = MapStore.directory(activity);
        final java.util.TreeSet<File> all = new java.util.TreeSet<File>();
        final File[] files = dir.listFiles();
        for (int i = 0; files != null && i < files.length; ++i) {
            if (files[i].isFile() && files[i].getName().toLowerCase().endsWith(".pud")) {
                all.add(files[i]);
            }
        }
        // A working copy without a map file is a new map that was never saved.
        final File[] journals = WorkingCopy.directory(dir).listFiles();
        for (int i = 0; journals != null && i < journals.length; ++i) {
            final String n = journals[i].getName();
            if (journals[i].isFile() && n.toLowerCase().endsWith(".pud.journal")) {
                all.add(new File(dir, n.substring(0, n.length() - ".journal".length())));
            }
        }
        for (File f : all) {
            final WorkingCopy w = WorkingCopy.peek(f);
            final boolean only = !f.isFile();
            if (only && w == null) {
                continue;     // neither a map nor a readable working copy
            }
            maps.add(f);
            drafts.add(w != null && (only || w.unsaved()));
            if (only) {
                final MapModel m = w.model();
                int players = 0;
                for (int slot = 0; slot < MapRules.PLAYERS; ++slot) {
                    final int owner = m.slot(MapModel.FIELD_OWNER, slot);
                    players += (owner == MapModel.OWNER_PERSON || owner == MapModel.OWNER_COMPUTER) ? 1 : 0;
                }
                unsavedFacts.put(f, PudInfo.of(m.width(), m.height(), m.tileset(), players));
            }
        }
        selected = Math.max(0, Math.min(maps.size() - 1, selected));
        for (int i = 0; keep != null && i < maps.size(); ++i) {
            if (maps.get(i).equals(keep)) {
                selected = i;
                // Bring the row into the list's window.
                final float top = i * ROW_PITCH, room = listH() - 2f * ROW_TOP;
                if (top < scroll || top + ROW_H > scroll + room) {
                    scroll = Math.max(0f, top + ROW_H - room);
                }
            }
        }
    }

    private File chosen() {
        return (selected >= 0 && selected < maps.size()) ? maps.get(selected) : null;
    }

    /** What is known of a map: read from its file, or from its working copy when it was never saved. */
    private PudInfo factsOf(File map) {
        final PudInfo unsaved = unsavedFacts.get(map);
        return (unsaved != null) ? unsaved : PudInfo.of(map);
    }

    boolean canvasOpen() { return canvas != null; }

    private boolean sheetOpen() {
        return newSheet != null || randomSheet != null || mapSheet != null || confirm != null;
    }

    /** Put the sheet of the random map away; its worker stops and what it still makes is dropped. */
    private void closeRandom() {
        if (randomSheet != null) {
            randomSheet.close();
            randomSheet = null;
        }
    }

    private void openRandom(int size, int tileset) {
        final Activity a = activity;
        randomSheet = new RandomMapSheet(new RandomMapSheet.Host() {
            @Override
            public void post(Runnable r) {
                a.runOnUiThread(r);
            }

            @Override
            public void redraw() {
                shell.redraw();
            }

            @Override
            public File dataDir() {
                return Provision.dataDir(a);
            }

            @Override
            public File gameFolder() {
                final String folder = GateActivity.rememberedFolder(a);
                return (folder == null) ? null : new File(folder);
            }
        }, size, tileset);
    }

    /** Android Back: closes a sheet, or leaves the map view; false when the plain page is shown. */
    boolean back() {
        if (canvas != null) {
            if (over()) {
                leave = null;
                players = null;
                faults = null;
                balance = null;
                canvas.invalidate();
            } else {
                askLeave();
            }
            return true;
        }
        if (sheetOpen()) {
            newSheet = null;
            closeRandom();
            mapSheet = null;
            confirm = null;
            confirmThen = null;
            return true;
        }
        return false;
    }

    // ----- the map view -----

    private static final int LEAVE_SAVE = 0, LEAVE_KEEP = 1, LEAVE_DISCARD = 2;

    /**
     * Leave the map view: at once when everything is saved, else after "Save changes?" - Save,
     * Keep draft (the unsaved work stays in the working copy and the map is marked a draft),
     * Discard (the working copy is removed), Cancel.
     */
    private void askLeave() {
        canvas.rest();
        if (canvas.unsaved()) {
            leave = new ChoiceSheet(kit.own("Save changes?"), nameOf(shown), kit.string("common_save", "Save"), kit.own("Keep draft"),
                                    kit.own("Discard"), kit.string("common_cancel", "Cancel"));
            canvas.invalidate();
        } else {
            close();
        }
    }

    private void leaveUp(float x, float y) {
        final int answer = leave.up(x, y, kit);
        if (answer == ChoiceSheet.NONE) {
            return;
        }
        shell.click();
        leave = null;
        boolean go = false;
        if (answer == LEAVE_SAVE) {
            // A map that could not be written, or that the checks stop, stays open.
            go = save(true);
        } else if (answer == LEAVE_KEEP) {
            // Closing the view leaves the working copy where it is.
            android.util.Log.i("WargusHD", "LAUNCHER editor draft kept " + shown.getName());
            go = true;
        } else if (answer == LEAVE_DISCARD) {
            canvas.discard();
            go = true;
        }
        canvas.invalidate();
        shell.redraw();
        if (go) {
            // Not from inside the view's own touch.
            shell.later(new Runnable() {
                @Override
                public void run() {
                    close();
                    shell.redraw();
                }
            }, 0L);
        }
    }

    private boolean over() { return leave != null || players != null || faults != null || balance != null; }

    /**
     * Save the map when the checks find nothing; else show what they found.
     *
     * @return true when the map file was written
     */
    private boolean save(boolean leaving) {
        final List<MapRules.Fault> found = MapRules.check(canvas.model());
        if (found.isEmpty()) {
            canvas.save();
            return !canvas.trouble();
        }
        faults = new FaultSheet(found);
        leaveAfterSave = leaving;
        shell.error();
        return false;
    }

    /**
     * Test: with nothing that blocks, write the map as it stands into the file of the test game
     * and have the shell start it; else show what the checks found. The map view stays as it
     * is under the game, so the view and the undo list are there when the game is over.
     */
    private void test() {
        canvas.rest();
        final List<MapRules.Fault> found = MapRules.check(canvas.model());
        if (MapRules.blocked(found)) {
            faults = new FaultSheet(found, true);
            leaveAfterSave = false;
            shell.error();
            return;
        }
        final File pud = MapStore.testFile(activity);
        final File dir = pud.getParentFile();
        try {
            if (!dir.isDirectory() && !dir.mkdirs()) {
                throw new java.io.IOException("no folder for the test map");
            }
            work.pud().write(pud);
        } catch (java.io.IOException | RuntimeException e) {
            pud.delete();
            android.util.Log.i("WargusHD", "LAUNCHER editor test map not written");
            barNotice = kit.own("The map could not be written.");
            shell.error();
            return;
        }
        android.util.Log.i("WargusHD", "LAUNCHER editor test " + shown.getName());
        barNotice = null;
        if (!shell.test(pud, shown)) {
            pud.delete();
            barNotice = kit.own("The test game could not be started.");
            shell.error();
        }
    }

    /**
     * The shell is back from a test game of a map. The map view is normally still there; when
     * the shell was made anew meanwhile, the map is opened again from its working copy.
     */
    void afterTest(File map, String failure) {
        if (canvas == null && activity != null && map != null
            && (map.isFile() || WorkingCopy.journalOf(map).isFile())) {
            show(map);
        }
        if (canvas != null) {
            // Said in the bar: a sheet of the shell would lie under the map view.
            if (failure != null) {
                barNotice = "closed".equals(failure) ? kit.own("The test game closed unexpectedly.")
                                                     : kit.own("The test game could not be started ({0}).", failure);
                shell.error();
            }
            canvas.invalidate();
        }
    }

    /** A finger was lifted over the sheet that lies on the map view. */
    private void overUp(float x, float y) {
        if (leave != null) {
            leaveUp(x, y);
        } else if (players != null) {
            final int answer = players.up(x, y, kit, canvas.model());
            if (answer == PlayersSheet.NONE) {
                return;
            }
            shell.click();
            if (answer == PlayersSheet.CLOSE) {
                players = null;
            } else {
                canvas.apply(players.step);
            }
            canvas.invalidate();
            shell.redraw();
        } else if (balance != null) {
            if (balance.up(x, y, kit) == BalanceSheet.CLOSE) {
                shell.click();
                balance = null;
                canvas.invalidate();
                shell.redraw();
            }
        } else if (faults != null) {
            final FaultSheet sheet = faults;
            final int answer = sheet.up(x, y, kit);
            if (answer == FaultSheet.NONE) {
                return;
            }
            shell.click();
            faults = null;
            final boolean leaving = leaveAfterSave;
            leaveAfterSave = false;
            if (answer == FaultSheet.SHOW) {
                canvas.show(sheet.showX, sheet.showY);
            } else if (answer == FaultSheet.SAVE) {
                canvas.save();
                if (leaving && !canvas.trouble()) {
                    // Not from inside the view's own touch.
                    shell.later(new Runnable() {
                        @Override
                        public void run() {
                            close();
                            shell.redraw();
                        }
                    }, 0L);
                }
            }
            canvas.invalidate();
            shell.redraw();
        }
    }

    /** Draw the sheet that lies on the map view; laid out on the whole screen. */
    private void drawOver(Canvas c) {
        if (leave != null) {
            leave.draw(c, kit);
        } else if (players != null) {
            players.draw(c, kit, canvas.model());
        } else if (faults != null) {
            faults.draw(c, kit);
        } else if (balance != null) {
            balance.draw(c, kit);
        }
    }

    private void close() {
        if (canvas == null) {
            return;
        }
        players = null;
        faults = null;
        balance = null;
        leaveAfterSave = false;
        barNotice = null;
        final ViewGroup parent = (ViewGroup) canvas.getParent();
        if (parent != null) {
            parent.removeView(canvas);
        }
        canvas.release();
        canvas = null;
        leave = null;
        try {
            work.close();
        } catch (java.io.IOException e) {
            // Nothing more to be done with it here.
        }
        work = null;
        final File was = shown;
        shown = null;
        load(was);
    }

    private void show(File map) {
        final String folder = GateActivity.rememberedFolder(activity);
        try {
            work = WorkingCopy.open(map);
            canvas = new CanvasView(activity, work, (folder == null) ? new File("") : new File(folder),
                                    Provision.dataDir(activity));
        } catch (java.io.IOException e) {
            work = null;
            canvas = null;
            tell(kit.own("The map was not opened"), kit.own("This file could not be read as a map."));
            return;
        }
        shown = map;
        canvas.setHost(new CanvasView.Host() {
            @Override
            public String word(String english) {
                return kit.own(english);
            }

            @Override
            public String term(String key, String english) {
                return kit.string(key, english);
            }

            @Override
            public void text(Canvas c, String s, float x, float y, float w, float h, float cap, int color,
                             boolean centred) {
                if (centred) {
                    kit.textCentre(c, kit.fit(s, cap, w), x, y, w, h, cap, color, true);
                } else {
                    kit.textMiddle(c, kit.fit(s, cap, w), x, y, h, cap, color, true);
                }
            }

            @Override
            public void changed() {
                barNotice = null;
                shell.redraw();
            }

            @Override
            public void click() {
                shell.click();
            }

            @Override
            public void over(Canvas c) {
                if (EditorPage.this.over()) {
                    // The sheet is laid out on the whole screen; the view begins under the bar.
                    c.save();
                    c.translate(0f, -BAR_H * kit.unit);
                    drawOver(c);
                    c.restore();
                }
            }

            @Override
            public boolean modal() {
                return EditorPage.this.over();
            }

            @Override
            public void touchOver(int action, float x, float y) {
                if (action == MotionEvent.ACTION_UP && EditorPage.this.over()) {
                    overUp(x / kit.unit, y / kit.unit + BAR_H);
                }
            }

            @Override
            public int colour(int slot) {
                return Profiles.COLORS[slot & 7];
            }

            @Override
            public void balance(net.wargushd.game.editor.BalanceWatch.Answer measured) {
                if (!EditorPage.this.over()) {
                    balance = new BalanceSheet(measured);
                    canvas.invalidate();
                }
            }
        });
        final FrameLayout.LayoutParams place = new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
        place.topMargin = Math.round(BAR_H * kit.unit);
        activity.addContentView(canvas, place);
    }

    // ----- what the page does with a map -----

    private static String nameOf(File f) {
        final String n = f.getName();
        return n.substring(0, n.length() - 4);
    }

    /** Say one thing on a sheet with OK. */
    private void tell(String title, String body) {
        confirm = ConfirmSheet.notice(title, body);
        confirmThen = null;
        shell.error();
    }

    /** Write a new, empty map of the player's own: grass all over, no units, every slot nobody's. */
    private void create(String wanted, int size, int tileset) {
        final String name = MapStore.cleanName(wanted);
        if (name.isEmpty()) {
            return;
        }
        final File target = MapStore.free(activity, name);
        try {
            if (target == null) {
                throw new java.io.IOException("no folder for the player's maps");
            }
            final MapModel m = MapRules.blank(size, size, tileset, GRASS_TILE);
            // The grass in the variants the tileset has pictures for, as a map of the game has it.
            final int[] table = Terrain.tileTable(new File(Provision.dataDir(activity),
                "scripts/tilesets/wargus/" + ERA_SCRIPT[tileset] + ".lua"));
            if (table != null) {
                final Terrain terrain = new Terrain(table);
                final Random rng = new Random();
                final MapModel.TileEdit edit = m.editTiles();
                for (int y = 0; y < size; ++y) {
                    for (int x = 0; x < size; ++x) {
                        final int tile = terrain.variant(GRASS_TILE, rng);
                        if (tile >= 0) {
                            edit.set(x, y, tile);
                        }
                    }
                }
                final MapModel.Step step = edit.step();
                if (step != null) {
                    m.apply(step);
                }
            }
            // A working copy alone, with no step to undo: the map file is written by the first Save.
            final PudFile pud = PudFile.parse(PudFile.create(m, new Random().nextInt()).bytes());
            WorkingCopy.create(target, pud).close();
        } catch (java.io.IOException | RuntimeException e) {
            tell(kit.own("The map was not made"), kit.own("The map could not be written."));
            return;
        }
        android.util.Log.i("WargusHD", "LAUNCHER editor map made " + target.getName() + " " + size + " era " + tileset);
        load(target);
    }

    /**
     * Make the draft of a generated map: the map as the generator gave it - terrain, players,
     * start positions, workers, mines, oil - as a working copy alone, as a new empty map is.
     */
    private void createRandom(String wanted, MapGen.Result made) {
        final String name = MapStore.cleanName(wanted);
        if (name.isEmpty() || made == null) {
            return;
        }
        final File target = MapStore.free(activity, name);
        try {
            if (target == null) {
                throw new java.io.IOException("no folder for the player's maps");
            }
            final PudFile pud = PudFile.parse(PudFile.create(made.map, (int) made.seedUsed).bytes());
            WorkingCopy.create(target, pud).close();
        } catch (java.io.IOException | RuntimeException e) {
            tell(kit.own("The map was not made"), kit.own("The map could not be written."));
            return;
        }
        android.util.Log.i("WargusHD", "LAUNCHER editor random map made " + target.getName() + " "
            + made.map.width() + " era " + made.map.tileset() + " number " + made.seedUsed);
        load(target);
    }

    private void duplicate(File map) {
        final File copy = MapStore.duplicate(activity, map);
        if (copy == null) {
            tell(kit.own("The map was not copied"), kit.own("The map could not be written."));
            return;
        }
        load(copy);
    }

    private void rename(File map, String wanted) {
        final String name = MapStore.cleanName(wanted);
        final File target = new File(map.getParentFile(), name + ".pud");
        if (name.isEmpty() || target.equals(map)) {
            return;
        }
        if (!MapStore.rename(activity, map, target)) {
            tell(kit.own("The map was not renamed"), kit.own("{0} is already there.", name));
            return;
        }
        load(target);
    }

    private void askRemove(final File map) {
        confirm = new ConfirmSheet(kit.own("Remove this map?"), nameOf(map),
                                   kit.string("common_cancel", "Cancel"), kit.string("common_delete", "Delete"));
        confirmThen = new Runnable() {
            @Override
            public void run() {
                if (MapStore.remove(activity, map)) {
                    android.util.Log.i("WargusHD", "LAUNCHER map removed " + map.getName());
                }
                load(null);
            }
        };
    }

    private void press(int button) {
        final File map = chosen();
        if (button == NEW) {
            shell.click();
            newSheet = new NewMapSheet();
        } else if (map != null && button == DUPLICATE && map.isFile()) {
            shell.click();
            duplicate(map);
        } else if (map != null && button == EDIT) {
            shell.click();
            show(map);
        }
    }

    /** A finger was lifted over the open sheet. */
    private void sheetUp(float x, float y) {
        if (newSheet != null) {
            final int answer = newSheet.up(x, y, kit);
            if (answer == NewMapSheet.NONE) {
                return;
            }
            shell.click();
            if (answer == NewMapSheet.CHOSE) {
                return;
            }
            if (answer == NewMapSheet.RANDOM) {
                final int index = newSheet.size, era = newSheet.tileset;
                newSheet = null;
                openRandom(index, era);
                return;
            }
            final int size = NewMapSheet.SIZES[newSheet.size], tileset = newSheet.tileset;
            newSheet = null;
            if (answer == NewMapSheet.CREATE) {
                shell.askName(kit.own("Name of the map"), kit.own("Untitled"), new Named() {
                    @Override
                    public void named(String name) {
                        create(name, size, tileset);
                    }
                });
            }
        } else if (randomSheet != null) {
            final int answer = randomSheet.up(x, y, kit);
            if (answer == RandomMapSheet.NONE) {
                return;
            }
            shell.click();
            if (answer == RandomMapSheet.EMPTY) {
                newSheet = new NewMapSheet();
                newSheet.size = randomSheet.size;
                newSheet.tileset = randomSheet.tileset;
                closeRandom();
            } else if (answer == RandomMapSheet.CANCEL) {
                closeRandom();
            } else if (answer == RandomMapSheet.CREATE) {
                // The sheet stays under the name sheet: a name not given loses no map.
                final MapGen.Result made = randomSheet.made();
                shell.askName(kit.own("Name of the map"), kit.own("Untitled"), new Named() {
                    @Override
                    public void named(String name) {
                        if (!MapStore.cleanName(name).isEmpty()) {
                            closeRandom();
                        }
                        createRandom(name, made);
                    }
                });
            }
        } else if (mapSheet != null) {
            final int answer = mapSheet.up(x, y, kit);
            final File map = chosen();
            if (answer == MapSheet.NONE) {
                return;
            }
            shell.click();
            mapSheet = null;
            if (map != null && answer == MapSheet.RENAME) {
                shell.askName(kit.own("Rename map"), nameOf(map), new Named() {
                    @Override
                    public void named(String name) {
                        rename(map, name);
                    }
                });
            } else if (map != null && answer == MapSheet.REMOVE) {
                askRemove(map);
            }
        } else if (confirm != null) {
            final int answer = confirm.up(x, y, kit);
            if (answer == ConfirmSheet.NONE) {
                return;
            }
            shell.click();
            final Runnable then = confirmThen;
            confirm = null;
            confirmThen = null;
            if (answer == ConfirmSheet.CONFIRM && then != null) {
                then.run();
            }
        }
    }

    // ----- drawing -----

    private float listH() { return kit.height - BOTTOM_UP - LIST_Y; }
    private float buttonY() { return kit.height - BUTTON_UP; }

    private float maxScroll() {
        return Math.max(0f, maps.size() * ROW_PITCH - (ROW_PITCH - ROW_H) + 2f * ROW_TOP - listH());
    }

    private void panel(Canvas c, float x, float y, float w, float h) {
        kit.fill(c, x, y, w, h, PANEL_FILL, 18f);
        kit.outline(c, x, y, w, h, LINE, 18f);
    }

    private void drawBar(Canvas c) {
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, BAR_H, BAR_FILL, 0f);
        kit.fill(c, 0f, BAR_H - 2f, Screen.CANVAS_W, 2f, LINE, 0f);
        kit.fill(c, BACK_X, BAR_Y, BACK_W, BAR_BUTTON_H, ROW_FAINT, 14f);
        kit.outline(c, BACK_X, BAR_Y, BACK_W, BAR_BUTTON_H, LINE, 14f);
        kit.textCentre(c, kit.own("Maps"), BACK_X, BAR_Y, BACK_W, BAR_BUTTON_H, CAP_BODY, INK, true);
        final boolean wrong = canvas.failed() || canvas.trouble() || barNotice != null;
        final String line = canvas.failed()
            ? kit.own("The terrain pictures of the game folder could not be read.")
            : canvas.trouble() ? kit.own("The map could not be written.")
            : (barNotice != null) ? barNotice : nameOf(shown);
        kit.textMiddle(c, kit.fit(line, CAP_BODY, NAME_W), NAME_X, BAR_Y, BAR_BUTTON_H, CAP_BODY,
                       wrong ? INK_MUTED : GOLD, true);
        kit.fill(c, TEST_X, BAR_Y, BAR_BUTTON_W, BAR_BUTTON_H, ROW_FAINT, 14f);
        kit.outline(c, TEST_X, BAR_Y, BAR_BUTTON_W, BAR_BUTTON_H, LINE, 14f);
        kit.textCentre(c, kit.fit(kit.own("Test"), CAP_BODY, BAR_BUTTON_W - 24f), TEST_X, BAR_Y, BAR_BUTTON_W,
                       BAR_BUTTON_H, CAP_BODY, INK, true);
        final String[] words = { kit.own("Undo"), kit.own("Redo"), kit.string("common_save", "Save") };
        final boolean[] live = { canvas.canUndo(), canvas.canRedo(), canvas.unsaved() };
        for (int i = 0; i < 3; ++i) {
            kit.fill(c, BAR_X[i], BAR_Y, BAR_BUTTON_W, BAR_BUTTON_H, ROW_FAINT, 14f);
            kit.outline(c, BAR_X[i], BAR_Y, BAR_BUTTON_W, BAR_BUTTON_H, LINE, 14f);
            kit.textCentre(c, words[i], BAR_X[i], BAR_Y, BAR_BUTTON_W, BAR_BUTTON_H, CAP_BODY,
                           live[i] ? INK : INK_MUTED, true);
        }
        kit.fill(c, PLAYERS_X, BAR_Y, BAR_BUTTON_W, BAR_BUTTON_H, ROW_FAINT, 14f);
        kit.outline(c, PLAYERS_X, BAR_Y, BAR_BUTTON_W, BAR_BUTTON_H, LINE, 14f);
        kit.textCentre(c, kit.fit(kit.string("mp_lobby_players", "Players"), CAP_BODY, BAR_BUTTON_W - 24f), PLAYERS_X, BAR_Y, BAR_BUTTON_W,
                       BAR_BUTTON_H, CAP_BODY, INK, true);
        // The part of an open sheet that lies on the bar; the rest is drawn over the view.
        drawOver(c);
    }

    void draw(Canvas c, SkinKit k) {
        kit = k;
        if (canvas != null) {
            drawBar(c);
            return;
        }
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        kit.textTop(c, kit.own("Map Editor"), X0, TITLE_Y, CAP_TITLE, INK_TITLE, false);

        final float h = listH();
        panel(c, X0, LIST_Y, LIST_W, h);
        if (maps.isEmpty()) {
            kit.textTop(c, kit.fit(kit.own("No maps of your own yet."), CAP_MID, LIST_W - 2f * INFO_PAD),
                        X0 + INFO_PAD, LIST_Y + 66f, CAP_MID, INK_MUTED, true);
        }
        scroll = Math.max(0f, Math.min(maxScroll(), scroll));
        c.save();
        c.clipRect(kit.rect(X0, LIST_Y + 6f, LIST_W, h - 12f));
        for (int i = 0; i < maps.size(); ++i) {
            final float y = LIST_Y + ROW_TOP + i * ROW_PITCH - scroll;
            if (y + ROW_H < LIST_Y || y > LIST_Y + h) {
                continue;
            }
            final boolean chosen = i == selected;
            kit.fill(c, ROW_X, y, ROW_W, ROW_H, (chosen || i == pressedRow) ? ROW_SELECTED : ROW_FAINT, 10f);
            if (chosen) {
                kit.fill(c, ROW_X, y, ACCENT_W, ROW_H, GOLD, 0f);
            }
            if (drafts.get(i)) {
                final float mx = ROW_X + ROW_INSET + ROW_NAME_W + MARK_GAP, my = y + (ROW_H - MARK_H) / 2f;
                kit.outline(c, mx, my, MARK_W, MARK_H, GOLD, 10f);
                kit.textCentre(c, kit.fit(kit.own("draft"), CAP_MARK, MARK_W - 12f), mx, my, MARK_W, MARK_H, CAP_MARK,
                               GOLD, true);
            }
            final PudInfo facts = factsOf(maps.get(i));
            final String right = (facts.width > 0) ? facts.width + " x " + facts.height + " - " + facts.players : "";
            kit.textMiddle(c, kit.fit(nameOf(maps.get(i)), CAP_BODY, ROW_NAME_W), ROW_X + ROW_INSET, y, ROW_H,
                           CAP_BODY, INK, true);
            kit.textMiddleRight(c, right, ROW_X + ROW_W - ROW_INSET, y, ROW_H, CAP_MID, chosen ? INK : INK_MUTED, true);
        }
        c.restore();

        // The chosen map: its picture and what its file says.
        panel(c, INFO_X, INFO_Y, INFO_W, INFO_H);
        final File map = chosen();
        if (map != null) {
            final float px = INFO_X + INFO_PAD, py = INFO_Y + INFO_PAD;
            final String folder = GateActivity.rememberedFolder(activity);
            final Bitmap preview = MapPreview.of(folder != null ? new File(folder) : null,
                                                 Provision.dataDir(activity), map);
            if (preview != null) {
                final float s = Math.min(PICTURE / preview.getWidth(), PICTURE / preview.getHeight());
                final float pw = preview.getWidth() * s, ph = preview.getHeight() * s;
                kit.pictureSharp(c, preview, px + (PICTURE - pw) / 2f, py + (PICTURE - ph) / 2f, pw, ph);
                kit.outline(c, px + (PICTURE - pw) / 2f, py + (PICTURE - ph) / 2f, pw, ph, LINE, 0f);
            } else {
                kit.outline(c, px, py, PICTURE, PICTURE, LINE, 0f);
            }
            final float tx = px + PICTURE + INFO_PAD, tw = INFO_X + INFO_W - INFO_PAD - tx;
            final PudInfo facts = factsOf(map);
            kit.textTop(c, kit.fit(nameOf(map), CAP_BODY, tw), tx, py + 6f, CAP_BODY, GOLD, true);
            float ty = py + 6f + LINE_PITCH * 1.2f;
            if (facts.width > 0 && facts.height > 0) {
                kit.textTop(c, facts.width + " x " + facts.height, tx, ty, CAP_MID, INK, true);
                ty += LINE_PITCH;
            }
            if (facts.tileset >= 0) {
                kit.textTop(c, kit.fit(LanSettings.tilesetName(kit, facts.tileset), CAP_MID, tw), tx, ty, CAP_MID, INK,
                            true);
                ty += LINE_PITCH;
            }
            final String players = (facts.players > 0) ? kit.own("up to {0} players", facts.players)
                                                       : kit.own("No players yet");
            kit.textTop(c, kit.fit(players, CAP_MID, tw), tx, ty, CAP_MID, INK_MUTED, true);
            ty += LINE_PITCH;
            if (selected < drafts.size() && drafts.get(selected)) {
                kit.textTop(c, kit.fit(kit.own(map.isFile() ? "Draft: has changes that are not saved"
                                                            : "Draft: not saved yet"), CAP_MID, tw),
                            tx, ty, CAP_MID, GOLD, true);
                ty += LINE_PITCH;
            }
            if (facts.width <= 0) {
                kit.textTop(c, kit.fit(kit.own("This file could not be read as a map."), CAP_MID, tw), tx, ty, CAP_MID,
                            INK_MUTED, true);
            }
        }

        final float by = buttonY();
        for (int i = 0; i < 3; ++i) {
            final boolean live = i == NEW || (map != null && (i != DUPLICATE || map.isFile()));
            kit.nine(c, (i == EDIT && live) ? buttonMarked : buttonPlain, BUTTON_X[i], by, BUTTON_W, BUTTON_H, 14);
            kit.textCentre(c, kit.own(BUTTON_WORD[i]), BUTTON_X[i], by, BUTTON_W, BUTTON_H, CAP_BODY,
                           !live ? INK_MUTED : (i == EDIT) ? GOLD : INK, true);
        }

        if (newSheet != null) {
            newSheet.draw(c, kit);
        }
        if (randomSheet != null) {
            randomSheet.draw(c, kit);
        }
        if (mapSheet != null) {
            mapSheet.draw(c, kit);
        }
        if (confirm != null) {
            confirm.draw(c, kit);
        }
    }

    // ----- touches -----

    private int rowAt(float x, float y) {
        if (x < ROW_X || x >= ROW_X + ROW_W || y < LIST_Y || y >= LIST_Y + listH()) {
            return -1;
        }
        final float in = y - LIST_Y - ROW_TOP + scroll;
        final int row = (int) Math.floor(in / ROW_PITCH);
        return (in >= 0f && row < maps.size() && in - row * ROW_PITCH < ROW_H) ? row : -1;
    }

    private int buttonAt(float x, float y) {
        final float by = buttonY();
        for (int i = 0; i < 3; ++i) {
            if (x >= BUTTON_X[i] && x < BUTTON_X[i] + BUTTON_W && y >= by && y < by + BUTTON_H) {
                return i;
            }
        }
        return -1;
    }

    /** A row held down for half a second opens the map's sheet: Rename, Remove. */
    private final Runnable held = new Runnable() {
        @Override
        public void run() {
            if (canvas == null && !sheetOpen() && inList && !dragging && pressedRow >= 0 && pressedRow < maps.size()) {
                selected = pressedRow;
                pressedRow = -1;
                mapSheet = new MapSheet(nameOf(maps.get(selected)), "One of your maps");
                heldOpened = true;
                shell.click();
                shell.redraw();
            }
        }
    };

    /** @return true when the touch was the page's (the caller redraws and looks no further) */
    boolean touch(int action, float x, float y, SkinKit k) {
        kit = k;
        if (canvas != null) {
            // Only the bar reaches here; the map view takes its own touches.
            if (action != MotionEvent.ACTION_UP) {
                return true;
            }
            if (over()) {
                overUp(x, y);
            } else if (y >= BAR_Y && y < BAR_Y + BAR_BUTTON_H) {
                if (x >= BACK_X && x < BACK_X + BACK_W) {
                    shell.click();
                    askLeave();
                } else if (x >= BAR_X[0] && x < BAR_X[0] + BAR_BUTTON_W && canvas.canUndo()) {
                    shell.click();
                    canvas.undo();
                } else if (x >= BAR_X[1] && x < BAR_X[1] + BAR_BUTTON_W && canvas.canRedo()) {
                    shell.click();
                    canvas.redo();
                } else if (x >= BAR_X[2] && x < BAR_X[2] + BAR_BUTTON_W && canvas.unsaved()) {
                    shell.click();
                    save(false);
                    canvas.invalidate();
                } else if (x >= TEST_X && x < TEST_X + BAR_BUTTON_W) {
                    shell.click();
                    test();
                    canvas.invalidate();
                } else if (x >= PLAYERS_X && x < PLAYERS_X + BAR_BUTTON_W) {
                    shell.click();
                    players = new PlayersSheet();
                    canvas.invalidate();
                }
            }
            return true;
        }
        if (sheetOpen()) {
            if (action == MotionEvent.ACTION_UP) {
                if (heldOpened) {
                    // The finger that held the row down has now been lifted.
                    heldOpened = false;
                } else {
                    sheetUp(x, y);
                }
            }
            return true;
        }
        switch (action) {
            case MotionEvent.ACTION_DOWN:
                inList = x >= X0 && x < X0 + LIST_W && y >= LIST_Y && y < LIST_Y + listH();
                dragging = false;
                downY = y;
                downScroll = scroll;
                pressedRow = inList ? rowAt(x, y) : -1;
                pressedButton = buttonAt(x, y);
                shell.forget(held);
                if (pressedRow >= 0) {
                    shell.later(held, HOLD_MILLIS);
                }
                return inList || pressedButton >= 0;
            case MotionEvent.ACTION_MOVE:
                if (inList) {
                    if (Math.abs(y - downY) > 16f) {
                        dragging = true;
                        pressedRow = -1;
                        shell.forget(held);
                    }
                    if (dragging) {
                        scroll = Math.max(0f, Math.min(maxScroll(), downScroll - (y - downY)));
                    }
                }
                return inList || pressedButton >= 0;
            case MotionEvent.ACTION_UP: {
                shell.forget(held);
                final boolean was = inList || pressedButton >= 0;
                final int row = (inList && !dragging) ? rowAt(x, y) : -1;
                if (row >= 0 && row == pressedRow && row != selected) {
                    selected = row;
                    shell.click();
                }
                if (pressedButton >= 0 && buttonAt(x, y) == pressedButton) {
                    press(pressedButton);
                }
                pressedRow = -1;
                pressedButton = -1;
                inList = false;
                dragging = false;
                return was;
            }
            default:
                shell.forget(held);
                pressedRow = -1;
                pressedButton = -1;
                inList = false;
                dragging = false;
                return false;
        }
    }
}
