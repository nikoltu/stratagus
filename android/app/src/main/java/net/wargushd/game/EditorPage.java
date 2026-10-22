package net.wargushd.game;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.view.MotionEvent;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import net.wargushd.game.editor.CanvasView;
import net.wargushd.game.editor.EngineRuns;
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
        /**
         * Start one of the engine's runs for the editor (EDITOR_STATUS.md, Engine E13/E17):
         * "export" (pud null) or "validate" of a map file written for it; `map` is the map being
         * edited, or null. Nothing is counted; the shell hands the run's files to
         * {@link EditorPage#afterEngineRun} when it is over.
         *
         * @return true when the run was started
         */
        boolean engineRun(String mode, File pud, File map);
        /** What an export depends on: the app's version and the mods a scenario game takes. */
        String engineKey();
    }

    // Layout of the page, logical units (the layout table's screen editor_maps).
    private static final float X0 = 816f, TITLE_Y = 84f, BOTTOM_UP = 264f;
    private static final float LIST_Y = 420f, LIST_W = 1192f;
    // The segment above the list (table 6.17 kind): Maps / Campaigns, and the game folder's maps as a
    // third (owner decision 94, E4 = option A). Campaigns is stage 3 (6.17d): shown, not live yet.
    private static final float KIND_Y = 264f, KIND_H = 108f, KIND_IN = 6f;
    private static final int KIND_MAPS = 0, KIND_CAMPAIGNS = 1, KIND_GAME = 2;
    private static final String[] KIND_WORD = { "Maps", "Campaigns", "Game maps" };
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
    /** The list shown: the player's own maps, or the game folder's maps (read only). */
    private int kind = KIND_MAPS;
    private int pressedRow = -1, pressedButton = -1, pressedKind = -1;
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
    /** The number sheet of a mine's or a patch's amount (owner decision 94, E1), or null. */
    private NumberSheet number;
    private boolean leaveAfterSave;
    /** A line shown in the bar in place of the map's name until the map is next changed, or null. */
    private String barNotice;
    /** The map Edit Map opens when the engine's export it waits for is over, or null. */
    private File editAfterExport;
    /** The key of the export installed in MapRules, or null (one for the process). */
    private static String installedKey;

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
        installExport();
        load(null);
    }

    // ----- the engine's export (E13): the tables units are placed by -----

    private File exportFile() { return new File(activity.getFilesDir(), "editor/export.json"); }
    private File exportKeyFile() { return new File(activity.getFilesDir(), "editor/export.key"); }

    /**
     * Put the kept export into the rules when it was made for the app and mods of now; tables
     * of another key are refused (DESIGN 31.7), the built-in rules hold until the next export.
     */
    private void installExport() {
        final String key = shell.engineKey();
        if (key.equals(installedKey) && MapRules.engine() != null) {
            return;
        }
        EngineRuns.Tables t = null;
        if (key.equals(GameLaunch.readText(exportKeyFile()))) {
            t = EngineRuns.readExport(GameLaunch.readText(exportFile()), null);
        }
        MapRules.useEngine(t);
        installedKey = (t != null) ? key : null;
    }

    /** True when no export was asked for the app and mods of now (a failed one counts as asked). */
    private boolean exportOwed() {
        return !shell.engineKey().equals(GameLaunch.readText(exportKeyFile()));
    }

    /** Read the list; `keep` stays the chosen map when it is still there. */
    private void load(File keep) {
        maps.clear();
        drafts.clear();
        unsavedFacts.clear();
        if (keep != null && MapStore.isOwn(activity, keep)) {
            // A map of the player's own was made, copied or left: the list of My maps shows it.
            kind = KIND_MAPS;
        }
        if (kind == KIND_GAME) {
            loadGame();
            return;
        }
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

    /** The game folder's maps (E4): its own first, then those of its sub-folders, each in name order (as MapList). */
    private void loadGame() {
        final String folder = GateActivity.rememberedFolder(activity);
        if (folder != null) {
            collectGame(new File(folder, "Data/Maps"), 0);
        }
        selected = Math.max(0, Math.min(maps.size() - 1, selected));
    }

    private void collectGame(File dir, int depth) {
        final File[] files = dir.listFiles();
        if (files == null || depth > 3) {
            return;
        }
        java.util.Arrays.sort(files);
        for (File f : files) {
            if (f.isFile() && f.getName().toLowerCase().endsWith(".pud")) {
                maps.add(f);
                drafts.add(false);
            }
        }
        for (File f : files) {
            if (f.isDirectory()) {
                collectGame(f, depth + 1);
            }
        }
    }

    private boolean gameList() { return kind == KIND_GAME; }

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
        return newSheet != null || randomSheet != null || mapSheet != null || confirm != null || rulesSheet != null;
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
            if (!over() && canvas.dropPending()) {
                return true;
            }
            if (players != null && players.closeList()) {
                // Back closes the controller list first, then the Players sheet (owner decision 93, 1A).
                canvas.invalidate();
                shell.redraw();
                return true;
            }
            if (over()) {
                leave = null;
                players = null;
                faults = null;
                balance = null;
                number = null;
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
            rulesSheet = null;
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

    private boolean over() {
        return leave != null || players != null || faults != null || balance != null || number != null;
    }

    /**
     * Save the map when the checks find nothing; else show what they found.
     *
     * @return true when the map file was written
     */
    private boolean save(boolean leaving) {
        // Faults, and when none blocks, the warnings of the map as it plays (they never block).
        // ... and what would stop the game's own loader on the bytes to be written (Session 2026-10-19 d).
        final List<MapRules.Fault> found = MapRules.review(canvas.model(), work.pud().bytes());
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
        final List<MapRules.Fault> found = MapRules.check(canvas.model(), work.pud().bytes());
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
        // The game's own check of the file first (E17, DESIGN 31.7: run on Test); the test game
        // follows when it is over (afterEngineRun).
        android.util.Log.i("WargusHD", "LAUNCHER editor validate " + shown.getName());
        barNotice = null;
        if (!shell.engineRun("validate", pud, shown)) {
            pud.delete();
            barNotice = kit.own("The game could not check the map.");
            shell.error();
        }
    }

    /** Start the test game on the file the game has checked. */
    private void startTest() {
        final File pud = MapStore.testFile(activity);
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

    /**
     * One of the engine's runs is over (E13 / E17). `result` is its result.json (null when it left
     * none), `part` its export.json or validate.json (null when there is none), `token` the
     * run's. An export is kept for the key it was asked for and put into the rules; a check
     * shows its lines, remarks with Show, and offers Test; a run that did not finish is said as
     * a line that blocks.
     */
    void afterEngineRun(String mode, File map, String result, String part, String token) {
        if (activity == null) {
            return;
        }
        final String failure = EngineRuns.failure(result, token);
        if ("export".equals(mode)) {
            final File wanted = editAfterExport;
            editAfterExport = null;
            final EngineRuns.Tables t = (failure == null) ? EngineRuns.readExport(part, token) : null;
            // Asked for this key, read or not: a failed export is not asked for again until the
            // app or the mods change.
            final File keep = exportFile();
            if (t == null || !GameLaunch.writeText(keep, part)) {
                keep.delete();
            }
            GameLaunch.writeText(exportKeyFile(), shell.engineKey());
            installedKey = null;
            installExport();
            android.util.Log.i("WargusHD", "LAUNCHER editor export "
                + ((t != null) ? t.unitCount() + " types, stamp " + t.stamp : "failed " + failure));
            if (t == null) {
                tell(kit.own("The game's tables were not read"),
                     kit.own("The editor places units by its own rules ({0}).",
                             (failure != null) ? failure : "unreadable"));
            } else if (wanted != null && canvas == null && wanted.isFile()) {
                show(wanted);
            }
            return;
        }
        if (canvas == null && map != null && (map.isFile() || WorkingCopy.journalOf(map).isFile())) {
            show(map);
        }
        if (canvas == null) {
            MapStore.testFile(activity).delete();
            return;
        }
        List<MapRules.Fault> found = (failure == null) ? EngineRuns.readValidate(part, token) : null;
        if (found == null) {
            found = new ArrayList<MapRules.Fault>();
            found.add(EngineRuns.runFault((failure != null) ? failure : "no-report"));
        }
        android.util.Log.i("WargusHD", "LAUNCHER editor validate over, " + found.size() + " lines"
            + ((failure != null) ? ", " + failure : ""));
        if (found.isEmpty()) {
            // Not from inside the shell's resume.
            shell.later(new Runnable() {
                @Override
                public void run() {
                    if (canvas != null && !over()) {
                        startTest();
                    }
                }
            }, 0L);
            return;
        }
        if (MapRules.blocked(found)) {
            MapStore.testFile(activity).delete();
        }
        faults = new FaultSheet(found, true);
        leaveAfterSave = false;
        shell.error();
        canvas.invalidate();
    }

    /** A finger was lifted over the sheet that lies on the map view. */
    private void overUp(float x, float y) {
        if (leave != null) {
            leaveUp(x, y);
        } else if (number != null) {
            final int answer = number.up(x, y, kit);
            if (answer == NumberSheet.NONE) {
                return;
            }
            shell.click();
            if (answer == NumberSheet.OK) {
                final int data = MapRules.typedAmount(number.digits());
                number = null;
                canvas.typeAmount(data);
            } else if (answer == NumberSheet.CANCEL) {
                number = null;
            }
            canvas.invalidate();
            shell.redraw();
        } else if (players != null) {
            final int answer = players.up(x, y, kit, canvas.model());
            if (answer == PlayersSheet.NONE) {
                return;
            }
            shell.click();
            if (answer == PlayersSheet.CLOSE) {
                players = null;
            } else if (answer == PlayersSheet.CHANGE) {
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
            if (sheet.forTest() && answer != FaultSheet.SAVE) {
                // The file written for a test that does not go on.
                MapStore.testFile(activity).delete();
            }
            if (answer == FaultSheet.SHOW) {
                canvas.show(sheet.showX, sheet.showY);
            } else if (answer == FaultSheet.SAVE && sheet.forTest()) {
                startTest();
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
        } else if (number != null) {
            number.draw(c, kit);
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
        number = null;
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
            public void notice(long millis, String english, Object... values) {
                final String line = kit.own(english, values);
                barNotice = line;
                shell.redraw();
                shell.later(new Runnable() {
                    @Override
                    public void run() {
                        if (line.equals(barNotice)) {
                            barNotice = null;
                            shell.redraw();
                        }
                    }
                }, millis);
            }

            @Override
            public void amount(String title, long current) {
                if (!EditorPage.this.over()) {
                    number = new NumberSheet(title, current);
                    canvas.invalidate();
                    shell.redraw();
                }
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
        } else if (map != null && button == DUPLICATE && map.isFile() && !gameList()) {
            shell.click();
            duplicate(map);
        } else if (map != null && button == EDIT && gameList()) {
            if (factsOf(map).width > 0) {
                shell.click();
                editGameMap(map);
            }
        } else if (map != null && button == EDIT) {
            shell.click();
            edit(map);
        }
    }

    /** Open one of the player's own maps in the map view. */
    private void edit(File map) {
        // Once for each app version and mod set the engine's tables are read first (E13).
        if (exportOwed() && shell.engineRun("export", null, null)) {
            editAfterExport = map;
            return;
        }
        show(map);
    }

    /**
     * Edit Map on a map of the game folder (E4): copy it into My maps first, then open the copy.
     * The game folder is only read (LAW 18.3).
     */
    private void editGameMap(File map) {
        final File copy = net.wargushd.game.editor.GameMapCopy.copy(map, MapStore.directory(activity));
        if (copy == null) {
            tell(kit.own("The map was not copied"), kit.own("The map could not be written."));
            return;
        }
        android.util.Log.i("WargusHD", "LAUNCHER editor game map copied " + map.getName() + " -> " + copy.getName());
        load(copy);
        edit(copy);
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
        } else if (rulesSheet != null) {
            if (rulesSheet.up(x, y, kit) != EditorRulesSheet.NONE) {
                shell.click();
                rulesSheet = null;
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

    // ----- the map's property rows right of the picture (owner decision 89, 7A) -----
    // Five rows 824 x 120 at a pitch of 138 from y 324 beside the 690 picture, in place of the facts:
    // Name, Description, Tileset, Size, Rules (LAUNCHER_LAYOUT_TABLES.md 6.17). The key at cap 28
    // above the value at cap 40; a "v" on the rows that open something; Tileset and Size shown only
    // (a darker field, no "v": re-tiling and resizing an existing map are not designed).
    private static final float PROP_X = 2860f, PROP_Y = 324f, PROP_W = 824f, PROP_H = 120f, PROP_PITCH = 138f;
    private static final float PROP_IN = 24f, PROP_KEY_DOWN = 20f, PROP_VALUE_DOWN = 68f, CAP_KEY = 28f, CAP_VALUE = 40f;
    private static final float PROP_MARK_X = 3620f, PROP_MARK_W = 60f, CAP_PROP_MARK = 30f;
    private static final float UNDER_Y = 1036f, CAP_UNDER = 30f;
    private static final float PL_Y = 1122f, PL_H = 1014f, PL_GAP = 24f, PL_HEAD_Y = 52f;
    private static final int PROP_FILL = 0x78000000, PROP_FILL_FIXED = 0x33000000;
    private static final int PROP_NAME = 0, PROP_DESCRIPTION = 1, PROP_TILESET = 2, PROP_SIZE = 3, PROP_RULES = 4;
    private static final String[] PROP_KEY = { "Name", "Description", "Tileset", "Size", "Rules" };
    private int pressedProp = -1;
    private EditorRulesSheet rulesSheet;
    /** The descriptions of the maps, read once a version of the file or of its working copy. */
    private final java.util.Map<String, String> descriptions = new java.util.HashMap<String, String>();

    private static boolean editable(int row) { return row == PROP_NAME || row == PROP_DESCRIPTION || row == PROP_RULES; }

    /** The description of a map as it is shown: that of its working copy for a draft. */
    private String descriptionOf(File map, boolean draft) {
        final File from = draft ? WorkingCopy.journalOf(map) : map;
        final String key = from.getPath() + "|" + from.length() + "|" + from.lastModified();
        String d = descriptions.get(key);
        if (d == null) {
            try {
                final WorkingCopy w = draft ? WorkingCopy.peek(map) : null;
                d = (w != null) ? w.model().description() : PudFile.read(map).model().description();
            } catch (java.io.IOException | RuntimeException e) {
                d = "";
            }
            if (descriptions.size() > 64) {
                descriptions.clear();
            }
            descriptions.put(key, d);
        }
        return d;
    }

    private void drawRows(Canvas c, File map, PudInfo facts, boolean draft) {
        final boolean read = facts.width > 0;
        final String[] value = {
            nameOf(map),
            read ? descriptionOf(map, draft) : "",
            (read && facts.tileset >= 0) ? LanSettings.tilesetName(kit, facts.tileset) : "",
            read ? facts.width + " x " + facts.height : "",
            // Where a preset is kept in a .pud is not designed; every map is Standard melee in stage 1a.
            kit.own("Standard melee"),
        };
        for (int i = 0; i < PROP_KEY.length; ++i) {
            final float y = PROP_Y + i * PROP_PITCH;
            // A map of the game folder is shown only: no row opens anything (E4).
            final boolean live = !gameList() && editable(i) && (read || i == PROP_NAME);
            kit.fill(c, PROP_X, y, PROP_W, PROP_H, (editable(i) && !gameList()) ? PROP_FILL : PROP_FILL_FIXED, 10f);
            kit.textTop(c, kit.own(PROP_KEY[i]), PROP_X + PROP_IN, y + PROP_KEY_DOWN, CAP_KEY, INK_MUTED, true);
            final float vw = PROP_MARK_X - PROP_X - 2f * PROP_IN;
            kit.textTop(c, kit.fit(value[i], CAP_VALUE, vw), PROP_X + PROP_IN, y + PROP_VALUE_DOWN, CAP_VALUE,
                        (i == PROP_NAME) ? GOLD : editable(i) ? INK : INK_MUTED, true);
            if (live) {
                kit.textCentre(c, "v", PROP_MARK_X, y, PROP_MARK_W, PROP_H, CAP_PROP_MARK, GOLD, false);
            }
        }
    }

    /** The property row at a point, or -1; only the rows that open something. */
    private int propAt(float x, float y) {
        final File map = chosen();
        if (map == null || gameList() || x < PROP_X || x >= PROP_X + PROP_W || y < PROP_Y) {
            return -1;
        }
        final int i = (int) Math.floor((y - PROP_Y) / PROP_PITCH);
        if (i < 0 || i >= PROP_KEY.length || y - PROP_Y - i * PROP_PITCH >= PROP_H || !editable(i)) {
            return -1;
        }
        return (i == PROP_NAME || factsOf(map).width > 0) ? i : -1;
    }

    private void propTap(int row) {
        final File map = chosen();
        if (map == null) {
            return;
        }
        shell.click();
        if (row == PROP_NAME) {
            // As Rename of the row's sheet.
            shell.askName(kit.own("Rename map"), nameOf(map), new Named() {
                @Override
                public void named(String name) {
                    rename(map, name);
                }
            });
        } else if (row == PROP_DESCRIPTION) {
            final boolean draft = selected < drafts.size() && drafts.get(selected);
            shell.askName(kit.own("Description"), descriptionOf(map, draft), new Named() {
                @Override
                public void named(String text) {
                    describe(map, text);
                }
            });
        } else if (row == PROP_RULES) {
            rulesSheet = new EditorRulesSheet();
        }
    }

    /**
     * The map's description changed, as a step of its working copy: a draft keeps it as one more
     * unsaved change; a map without a draft is written at once (the maps page has no Save) and
     * keeps no working copy.
     */
    private void describe(File map, String text) {
        final String t = (text == null) ? "" : text.trim();
        final boolean hadDraft = WorkingCopy.journalOf(map).isFile();
        try {
            final WorkingCopy w = WorkingCopy.open(map);
            try {
                w.apply(w.model().changeHead(t, w.model().tileset()));
                if (!hadDraft) {
                    w.save();
                }
            } finally {
                if (hadDraft) {
                    w.keep();
                } else {
                    w.discard();
                }
            }
        } catch (java.io.IOException | RuntimeException e) {
            tell(kit.own("The map was not changed"), kit.own("The map could not be written."));
            return;
        }
        load(map);
    }

    // ----- the Players rows of the panel (owner decision 94, E3 = option A) -----
    // Read only, only the slots that are not Nobody; below them "Edit in the map's Players".
    // Owner decision 95 (E7 = option A): compact cells in two columns, filled row by row - colour,
    // "Player n", then controller and race as text with no fields; three rows hold six slots. A map
    // with more slots than cells shows one slot fewer and a last cell "+n" (a number, no new word).
    private static final float PL_CELLS_DOWN = 104f, PL_CELL_H = 56f, PL_CELL_PITCH = 62f, PL_CELL_GAP = 24f;
    private static final float PL_SWATCH_IN = 12f, PL_SWATCH = 40f, PL_NAME_IN = 68f, PL_NAME_W = 200f, PL_KEY_IN = 280f;
    private static final float PL_NOTE_GAP = 24f, CAP_NOTE = 30f, CAP_CELL = 30f;
    /** The rows of cells the panel shows at most (E7: three rows, six slots). */
    private static final int PL_ROWS_MOST = 3;
    /** By the file's (or working copy's) path, length and time: the eight slots' owner and race, or null. */
    private final java.util.Map<String, int[][]> slotFacts = new java.util.HashMap<String, int[][]>();

    /** The owners and races of a map's eight player slots, from its working copy for a draft; null when unread. */
    private int[][] slotsOf(File map, boolean draft) {
        final File from = draft ? WorkingCopy.journalOf(map) : map;
        final String key = from.getPath() + "|" + from.length() + "|" + from.lastModified();
        if (slotFacts.containsKey(key)) {
            return slotFacts.get(key);
        }
        int[][] out = null;
        try {
            final WorkingCopy w = draft ? WorkingCopy.peek(map) : null;
            final MapModel m = (w != null) ? w.model() : PudFile.read(map).model();
            out = new int[3][];
            out[0] = MapRules.playerRows(m, MapRules.PLAYERS);
            out[1] = new int[MapRules.PLAYERS];
            out[2] = new int[MapRules.PLAYERS];
            for (int s = 0; s < MapRules.PLAYERS; ++s) {
                out[1][s] = m.slot(MapModel.FIELD_OWNER, s);
                out[2][s] = m.slot(MapModel.FIELD_RACE, s);
            }
        } catch (java.io.IOException | RuntimeException e) {
            out = null;
        }
        if (slotFacts.size() > 64) {
            slotFacts.clear();
        }
        slotFacts.put(key, out);
        return out;
    }

    private String raceWord(int race) {
        return (race == MapModel.RACE_HUMAN) ? kit.string("customscenario_race_human", "Human")
            : (race == MapModel.RACE_ORC) ? kit.string("customscenario_race_orc", "Orc") : kit.own("Neutral");
    }

    private void drawPlayerRows(Canvas c, File map, boolean draft, float plH) {
        final int[][] slots = slotsOf(map, draft);
        if (slots == null) {
            return;
        }
        // As many rows as the panel holds above the note, at most three.
        final float room = plH - PL_CELLS_DOWN - (PL_NOTE_GAP + CAP_NOTE + PL_NOTE_GAP);
        final int fit = (room < PL_CELL_H) ? 0 : 1 + (int) Math.floor((room - PL_CELL_H) / PL_CELL_PITCH);
        final int cells = 2 * Math.min(PL_ROWS_MOST, fit);
        // slots[0]: every slot not Nobody, in order (MapRules.playerRows).
        final int shown = MapRules.playerCellsShown(slots[0].length, cells);
        final int used = Math.min(cells, slots[0].length);
        final float cw = (INFO_W - 2f * INFO_PAD - PL_CELL_GAP) / 2f;
        float bottom = PL_Y + PL_CELLS_DOWN;
        for (int i = 0; i < used; ++i) {
            final float x = INFO_X + INFO_PAD + (i % 2) * (cw + PL_CELL_GAP);
            final float y = PL_Y + PL_CELLS_DOWN + (i / 2) * PL_CELL_PITCH;
            kit.fill(c, x, y, cw, PL_CELL_H, ROW_FAINT, 8f);
            bottom = y + PL_CELL_H;
            if (i >= shown) {
                kit.textCentre(c, "+" + (slots[0].length - shown), x, y, cw, PL_CELL_H, CAP_CELL, INK_MUTED, true);
                continue;
            }
            final int s = slots[0][i];
            final float sy = y + (PL_CELL_H - PL_SWATCH) / 2f;
            kit.fill(c, x + PL_SWATCH_IN, sy, PL_SWATCH, PL_SWATCH, Profiles.COLORS[s & 7], 6f);
            kit.outline(c, x + PL_SWATCH_IN, sy, PL_SWATCH, PL_SWATCH, LINE, 6f);
            kit.textMiddle(c, kit.fit(kit.own("Player") + " " + (s + 1), CAP_CELL, PL_NAME_W), x + PL_NAME_IN, y,
                           PL_CELL_H, CAP_CELL, INK, true);
            final String says = PlayersSheet.ownerWord(kit, slots[1][s]) + " · " + raceWord(slots[2][s]);
            kit.textMiddle(c, kit.fit(says, CAP_CELL, cw - PL_KEY_IN - 16f), x + PL_KEY_IN, y, PL_CELL_H, CAP_CELL,
                           INK_MUTED, true);
        }
        if (fit > 0 || used > 0) {
            kit.textTop(c, kit.fit(kit.own("Edit in the map's Players"), CAP_NOTE, INFO_W - 2f * INFO_PAD),
                        INFO_X + INFO_PAD, bottom + PL_NOTE_GAP, CAP_NOTE, INK_MUTED, true);
        }
    }

    // ----- the picture of a draft (owner decision 88, 4A) -----

    /** The Draft corner: a triangle of this side in the picture's top right, its word turned along the cut. */
    private static final float CORNER = 260f, CORNER_TEXT_IN = 70f, CORNER_TEXT_DOWN = 90f, CAP_CORNER = 34f;
    private static final int CORNER_FILL = 0xFFFF7A3D, CORNER_INK = 0xFF111111;
    private final android.graphics.Paint cornerPaint = new android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG);
    private final android.graphics.Path cornerPath = new android.graphics.Path();
    /** Pictures of drafts made from their working copies, kept by the journal's path, length and time. */
    private final java.util.Map<String, Bitmap> draftPictures = new java.util.HashMap<String, Bitmap>();

    /** The picture of a map's working copy as it stands; null when it cannot be made. */
    private Bitmap draftPicture(String folder, File map) {
        final File journal = WorkingCopy.journalOf(map);
        final String key = journal.getPath() + "|" + journal.length() + "|" + journal.lastModified();
        if (draftPictures.containsKey(key)) {
            return draftPictures.get(key);
        }
        // One picture a map: the older one of the same journal goes.
        for (java.util.Iterator<java.util.Map.Entry<String, Bitmap>> it = draftPictures.entrySet().iterator(); it.hasNext();) {
            final java.util.Map.Entry<String, Bitmap> e = it.next();
            if (e.getKey().startsWith(journal.getPath() + "|")) {
                if (e.getValue() != null) {
                    e.getValue().recycle();
                }
                it.remove();
            }
        }
        final WorkingCopy w = WorkingCopy.peek(map);
        final Bitmap made = (w == null || folder == null) ? null
            : MapPreview.of(new File(folder), Provision.dataDir(activity), w.model());
        draftPictures.put(key, made);
        return made;
    }

    /** The orange Draft corner at the top right (x, y) of a picture. */
    private void draftCorner(Canvas c, float x, float y) {
        final float u = kit.unit;
        cornerPath.reset();
        cornerPath.moveTo((x - CORNER) * u, y * u);
        cornerPath.lineTo(x * u, y * u);
        cornerPath.lineTo(x * u, (y + CORNER) * u);
        cornerPath.close();
        cornerPaint.setColor(CORNER_FILL);
        c.drawPath(cornerPath, cornerPaint);
        final float cx = x - CORNER_TEXT_IN, cy = y + CORNER_TEXT_DOWN;
        c.save();
        c.rotate(45f, cx * u, cy * u);
        kit.textCentre(c, kit.fit(kit.own("Draft"), CAP_CORNER, CORNER * 0.7f), cx - CORNER * 0.35f, cy - CAP_CORNER,
                       CORNER * 0.7f, 2f * CAP_CORNER, CAP_CORNER, CORNER_INK, false);
        c.restore();
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
        drawKinds(c);

        final float h = listH();
        panel(c, X0, LIST_Y, LIST_W, h);
        if (maps.isEmpty()) {
            final String empty = !gameList() ? kit.own("No maps of your own yet.")
                : (GateActivity.rememberedFolder(activity) == null) ? kit.own("Choose the game folder in Options, Game Files.")
                : kit.own("No maps in the game folder.");
            kit.textTop(c, kit.fit(empty, CAP_MID, LIST_W - 2f * INFO_PAD),
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
            final boolean draft = selected < drafts.size() && drafts.get(selected);
            final Bitmap preview = draft ? draftPicture(folder, map)
                : MapPreview.of(folder != null ? new File(folder) : null, Provision.dataDir(activity), map);
            if (preview != null) {
                final float s = Math.min(PICTURE / preview.getWidth(), PICTURE / preview.getHeight());
                final float pw = preview.getWidth() * s, ph = preview.getHeight() * s;
                kit.pictureSharp(c, preview, px + (PICTURE - pw) / 2f, py + (PICTURE - ph) / 2f, pw, ph);
                kit.outline(c, px + (PICTURE - pw) / 2f, py + (PICTURE - ph) / 2f, pw, ph, LINE, 0f);
                if (draft) {
                    draftCorner(c, px + (PICTURE + pw) / 2f, py + (PICTURE - ph) / 2f);
                }
            } else {
                kit.outline(c, px, py, PICTURE, PICTURE, LINE, 0f);
            }
            final PudInfo facts = factsOf(map);
            drawRows(c, map, facts, draft);
            // Under the picture: the draft's state, or that the file is not a map (owner decision 89, 7A).
            final String under = (facts.width <= 0) ? kit.own("This file could not be read as a map.")
                : !draft ? null : kit.own(map.isFile() ? "Draft: has changes that are not saved" : "Draft: not saved yet");
            if (under != null) {
                kit.textTop(c, kit.fit(under, CAP_UNDER, INFO_W - 2f * INFO_PAD), px, UNDER_Y, CAP_UNDER,
                            (facts.width <= 0) ? INK_MUTED : GOLD, true);
            }
            // The Players panel below (table 6.17 pl): its header; "up to N players" moved here from the facts.
            final float plH = Math.min(PL_H, buttonY() - PL_GAP - PL_Y);
            if (plH > PL_HEAD_Y + CAP_MID) {
                panel(c, INFO_X, PL_Y, INFO_W, plH);
                final String players = (facts.players > 0) ? kit.own("Players - up to {0}", facts.players)
                                                           : kit.own("No players yet");
                kit.textTop(c, kit.fit(players, CAP_MID, INFO_W - 2f * INFO_PAD), px, PL_Y + PL_HEAD_Y, CAP_MID, GOLD,
                            true);
                if (facts.width > 0) {
                    drawPlayerRows(c, map, draft, plH);
                }
            }
        }
        if (rulesSheet != null) {
            rulesSheet.draw(c, kit);
        }

        final float by = buttonY();
        for (int i = 0; i < 3; ++i) {
            final boolean live = i == NEW || (map != null && (gameList() ? i == EDIT && factsOf(map).width > 0
                                                                         : (i != DUPLICATE || map.isFile())));
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

    /** The live segment at a point (Maps, Game maps), or -1; Campaigns is not live yet. */
    private int kindAt(float x, float y) {
        final int i = LanKit.segmentAt(x, y, KIND_WORD.length, X0, KIND_Y, LIST_W, KIND_H);
        return (i == KIND_CAMPAIGNS) ? -1 : i;
    }

    private void chooseKind(int k) {
        if (k == kind) {
            return;
        }
        shell.click();
        kind = k;
        selected = 0;
        scroll = 0f;
        load(null);
    }

    /** The segment above the list: Maps, Campaigns (not live: stage 3), Game maps. */
    private void drawKinds(Canvas c) {
        LanKit.well(c, kit, X0, KIND_Y, LIST_W, KIND_H);
        final float segW = LIST_W / KIND_WORD.length;
        for (int i = 0; i < KIND_WORD.length; ++i) {
            final float sx = X0 + segW * i + KIND_IN, sw = segW - 2f * KIND_IN, sh = KIND_H - 2f * KIND_IN;
            final boolean on = i == kind;
            kit.fill(c, sx, KIND_Y + KIND_IN, sw, sh, on ? GOLD : LanKit.SEG_FAINT, 10f);
            kit.textCentre(c, kit.fit(kit.own(KIND_WORD[i]), CAP_BODY, sw - 24f), sx, KIND_Y + KIND_IN, sw, sh, CAP_BODY,
                           on ? LanKit.INK_ON_GOLD : (i == KIND_CAMPAIGNS) ? LanKit.INK_DISABLED : INK, !on);
        }
    }

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
            if (canvas == null && !sheetOpen() && !gameList() && inList && !dragging && pressedRow >= 0
                && pressedRow < maps.size()) {
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
                pressedProp = propAt(x, y);
                pressedKind = kindAt(x, y);
                shell.forget(held);
                if (pressedRow >= 0) {
                    shell.later(held, HOLD_MILLIS);
                }
                return inList || pressedButton >= 0 || pressedProp >= 0 || pressedKind >= 0;
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
                return inList || pressedButton >= 0 || pressedProp >= 0 || pressedKind >= 0;
            case MotionEvent.ACTION_UP: {
                shell.forget(held);
                final boolean was = inList || pressedButton >= 0 || pressedProp >= 0 || pressedKind >= 0;
                if (pressedKind >= 0 && kindAt(x, y) == pressedKind) {
                    chooseKind(pressedKind);
                }
                pressedKind = -1;
                if (pressedProp >= 0 && propAt(x, y) == pressedProp) {
                    propTap(pressedProp);
                }
                pressedProp = -1;
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
                pressedProp = -1;
                pressedKind = -1;
                inList = false;
                dragging = false;
                return false;
        }
    }
}
