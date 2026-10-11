package net.wargushd.game;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.os.Bundle;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * The launcher's shell: the game's painting, the rail at the left (logo, the seven sections,
 * the footer), the profile chip at the header's right, and the current section's page in
 * the content area. It opens with an empty content area and no section selected; the rail
 * is the hub.
 */
public class HomeActivity extends Activity implements LanPage.Starter {
    // Rail geometry, logical units.
    private static final float RAIL_W = 720f, EDGE_W = 6f, PAD = 48f;
    private static final float LOGO_W = 624f, LOGO_H = 416f;
    private static final float ROW_X = 48f, ROW_W = 618f, ROW_H = 132f, ROW_Y0 = 512f, ROW_PITCH = 144f;
    private static final float ROW_INSET = 36f, ROW_RADIUS = 10f;
    private static final float FOOT_H = 96f, FILES_UP = 324f, MUSIC_UP = 216f, VERSION_UP = 84f;
    private static final float CAP_TITLE = 60f, CAP_BODY = 44f, CAP_MID = 36f, CAP_SMALL = 30f;

    // Content area.
    private static final float CONTENT_X = 816f, CONTENT_TOP = 264f, CONTENT_BOTTOM_UP = 264f;
    private static final float TITLE_Y = 84f, PANEL_RADIUS = 18f, BOX_RADIUS = 14f;
    private static final float ACTION_W = 720f, ACTION_H = 120f, ACTION_UP = 204f, ACTION_RIGHT = 96f;

    // Profile chip at the right of the header.
    private static final float CHIP_W = 720f, CHIP_H = 180f, CHIP_Y = 48f, CHIP_RIGHT = 96f;
    private static final float PORTRAIT_W = 174.3f, PORTRAIT_H = 144f;

    // Custom Scenario page.
    private static final float LIST_W = 1440f, SRC_H = 108f, LIST_ROW_W = 1392f, LIST_PITCH = 138f;
    private static final float LIST_ROWS_DY = 156f, INFO_X = 2304f, INFO_W = 1440f, INFO_H = 300f;
    private static final float SET_Y = 612f, SET_ROW_DY = 48f, SET_PITCH = 150f, SET_H = 108f;
    private static final float VALUE_W = 720f, LINE_PITCH = 82.5f;

    private static final int RAIL_FILL = 0xEC0E0B08, LINE = 0xFF92743E, PANEL_FILL = 0xDE140F0A;
    private static final int INK = 0xFFE8E2D4, INK_MUTED = 0xFFCEC6B6, GOLD = 0xFFF0D250;
    private static final int INK_ON_GOLD = 0xFF1E1608, INK_TITLE = 0xFF000000;
    private static final int ROW_SELECTED = 0x36F0D250, ROW_FAINT = 0x10FFFFFF, SEG_FAINT = 0x0EFFFFFF;
    private static final int WELL = 0x78000000;
    private static final int DOT_OK = 0xFF78C878, DOT_BAD = 0xFFFF9680;
    private static final int SWITCH_ON = 0xFF78B060, SWITCH_OFF = 0xFF463E34, KNOB = 0xFFECE6D6;

    private static final int ENTRY_CAMPAIGN = 0;
    private static final int ENTRY_SCENARIO = 1;
    private static final int ENTRY_MULTIPLAYER = 2;
    private static final int ENTRY_LOAD = 3;
    private static final int ENTRY_OPTIONS = 6;
    private static final int PAGE_NONE = -1;

    private static final String PREFS = "launcher";
    private static final String PREF_MUSIC = "menuMusic";
    private static final String PREF_PENDING = "pendingToken";
    /** Set for one start: the shell was started over for a new language. */
    private static final String PREF_REOPEN_LANGUAGES = "reopenLanguages";
    /** Where the pending game was started from: the rail entry and, on Missions, the campaign. */
    private static final String PREF_ORIGIN = "originPage";
    private static final String PREF_ORIGIN_CAMPAIGN = "originCampaign";

    private SkinKit kit;
    private HomeView view;
    private String[] entries;
    private String version;
    private boolean filesOk;
    private boolean music;
    private MenuMusic menuMusic;
    private MenuAudio menuAudio;
    private Profiles.Profile profile;
    /** The section shown in the content area: an index into the rail, or PAGE_NONE. */
    private int page = PAGE_NONE;

    // Custom Scenario state.
    private final List<File> maps = new ArrayList<File>();
    private final List<PudInfo> mapInfo = new ArrayList<PudInfo>();
    // The owner's choice of 2026-10-06 (option D): All / Remastered / My maps.
    private int source = 1;          // 0 all, 1 the game folder's maps, 2 the player's imported maps
    private int selectedMap = 0;
    private float listScroll;        // logical units
    private int race, opponents, resources, units;   // 0 = map default
    private int tileset;   // 0 = map default, 1 forest, 2 winter, 3 wasteland, 4 orc swamp
    /** The list is to be moved so that the selected map is in view (done at the next draw). */
    private boolean revealMap;
    private final CampaignPage campaign = new CampaignPage();
    private long createdAt;
    private boolean firstFrameTold;
    private final MissionsPage missions = new MissionsPage();
    private final LoadPage load = new LoadPage();
    private final OptionsPage options = new OptionsPage();
    private final LanPage lan = new LanPage();
    /** The gate was opened from Options: what the pages show of the folder is read again after it. */
    private boolean folderVisit;
    /** This resume follows a game whose record was just read. */
    private boolean cameBack;
    private String folderBefore;
    /** The question shown over the shell, or null; while it is shown nothing under it reacts. */
    private ConfirmSheet sheet;
    /** The licence texts, opened from Options, About. */
    private LicencesSheet licences;
    /** What a confirmed question does. */
    private Runnable sheetAction;
    private final ProfileSheet profileSheet = new ProfileSheet();
    /** True while the profile sheet is shown over the shell. */
    private boolean profileOpen;
    private final GallerySheet gallery = new GallerySheet();
    /** True while the portrait gallery is shown in place of the profile sheet. */
    private boolean galleryOpen;
    /** The list of profiles shown over the profile sheet, or null. */
    private SwitchSheet switchSheet;
    /** The dialog after a campaign's last mission was won, or null; and the campaign it is about. */
    private CampaignCompleteSheet completeSheet;
    private CampaignPage.Campaign completeCampaign;
    /** The campaign a session has just saved progress in, for the Missions page's notice, or null. */
    private CampaignPage.Campaign savedCampaign;
    /** The last mission won in the session that has just been collected. */
    private int wonMission;
    /** The name being typed over the profile sheet, or null. */
    private NameSheet nameSheet;
    /** The sheet of one of the player's own maps (long press on its row), and that map. */
    private MapSheet mapSheet;
    private File sheetMap;
    /** The map the name sheet renames, or null when it names a profile. */
    private File renamingMap;
    /** True when the typed name is for a new profile, false when it renames the active one. */
    private boolean nameIsNew;
    /** The first profile has just been made and its step has not been shown yet. */
    private boolean firstProfile;
    /** The name sheet on screen is the first profile's: the profile sheet follows it. */
    private boolean firstStep;
    /** True while the Campaign section shows one campaign's missions instead of the four cards. */
    private boolean inMissions;
    /** The loading view shown from the tap on Start until the shell is back in front. */
    private LoadingView loading;

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        createdAt = android.os.SystemClock.uptimeMillis();
        kit = new SkinKit(this);
        entries = new String[] {
            kit.string("campaign", "Campaign"),
            kit.string("customscenario", "Custom Scenario"),
            kit.string("multiplayer", "Multiplayer"),
            kit.string("load_game", "Load Game"),
            kit.own("Mods"),
            kit.own("Map Editor"),
            kit.string("options", "Options"),
        };
        try {
            version = "WargusHD "
                + getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
        } catch (Exception e) {
            version = "WargusHD";
        }
        music = prefs().getBoolean(PREF_MUSIC, true);
        menuMusic = new MenuMusic(this);
        menuMusic.setEnabled(music);
        menuAudio = new MenuAudio(this);
        menuAudio.setEnabled(music);
        profile = Profiles.active(this);
        if (profile == null) {
            // The first profile starts with these and the player is asked at once: the name
            // first, then the profile sheet for the portrait and the colour.
            profile = Profiles.create(this, "Player", 8, 1);
            firstProfile = (profile != null);
        }
        if (prefs().getBoolean(PREF_REOPEN_LANGUAGES, false)) {
            prefs().edit().remove(PREF_REOPEN_LANGUAGES).apply();
            page = ENTRY_OPTIONS;
            options.showTab(4);
        }
        view = new HomeView(this);
        setContentView(view);
        Screen.useWhole(this);
    }

    @Override
    protected void onResume() {
        super.onResume();
        filesOk = GateActivity.isSatisfied(this);
        if (folderVisit) {
            folderVisit = false;
            final String now = GateActivity.rememberedFolder(this);
            if (now != null && !now.equals(folderBefore)) {
                // Font, skins and lists all come from the folder: start the shell over on it.
                recreate();
                return;
            }
            options.read(this, profile);
        }
        if (loading != null) {
            ((android.view.ViewGroup) loading.getParent()).removeView(loading);
            loading = null;
        }
        savedCampaign = null;
        wonMission = 0;
        final boolean unexpected = restoreOrigin();
        cameBack = false;
        collectResult();
        campaign.readProgress(this, profile);
        if (inMissions && missions.campaign() != null) {
            // Back from a game: show the campaign as it stands now.
            missions.open(missions.campaign());
            if (savedCampaign == missions.campaign()) {
                missions.notice = kit.own("Progress saved") + " — " + savedCampaign.completed + " / "
                    + savedCampaign.missions;
            } else if (cameBack) {
                // Back from a campaign game that was left: where the campaign stands (design 26.3).
                final CampaignPage.Campaign k = missions.campaign();
                missions.notice = kit.own("Progress saved") + " — " + k.completed + " / " + k.missions;
            }
        }
        if (savedCampaign != null && wonMission == savedCampaign.missions) {
            // The campaign's last mission was won in that session.
            completeCampaign = savedCampaign;
            completeSheet = new CampaignCompleteSheet(
                savedCampaign.code.endsWith("orc"),
                MissionsPage.listed(kit, savedCampaign, savedCampaign.missions));
        }
        load.refresh(this, profile);
        options.read(this, profile);
        if (page == ENTRY_MULTIPLAYER) {
            lan.open(this, view, menuAudio, profile);
        }
        if (unexpected) {
            tell(kit.own("The game closed unexpectedly"), kit.own("Nothing was recorded for it."));
        } else if (errorReason != null) {
            tell(kit.own("The game could not be started"), startFailure(errorReason));
        } else if ("net_left".equals(netReason)) {
            tell(kit.own("The game ended"), kit.own("You left the game. It is counted as a defeat."));
        } else if ("net_lost".equals(netReason)) {
            tell(kit.own("The game ended"),
                 kit.own("The connection to the game was lost. The game is not counted."));
        } else if ("net_no_result".equals(netReason)) {
            tell(kit.own("The game ended"),
                 kit.own("No other team started. The game has no result and is not counted."));
        }
        errorReason = null;
        netReason = null;
        view.invalidate();
        menuMusic.resume();
    }

    @Override
    protected void onPause() {
        lan.close();
        // When a game was just started the music is already fading out by itself.
        if (loading == null) {
            menuMusic.pause();
        }
        super.onPause();
    }

    @Override
    protected void onStop() {
        menuMusic.pause();
        super.onStop();
    }

    /** What a failed start is told as: a sentence for the network reasons, the code otherwise. */
    private String startFailure(String reason) {
        final String words;
        if ("net_timeout".equals(reason)) {
            words = "The other players did not answer in time.";
        } else if ("net_unreachable".equals(reason) || "net_server_gone".equals(reason)) {
            words = "The host could not be reached.";
        } else if ("net_bad_map".equals(reason)) {
            words = "This device does not have the same map as the host.";
        } else if ("net_version".equals(reason)) {
            words = "The host runs another version of the game.";
        } else if ("net_full".equals(reason)) {
            words = "The game is full.";
        } else if ("net_port".equals(reason)) {
            words = "The network port is in use on this device.";
        } else if ("net_stranger".equals(reason)) {
            words = "A device that is not in the room tried to join.";
        } else {
            return kit.own("Reason: {0}", reason);
        }
        return kit.own(words);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            Screen.useWhole(this);
            if (firstProfile) {
                firstProfile = false;
                firstStep = true;
                openNameSheet(false);
            }
        }
    }

    @Override
    public void onBackPressed() {
        if (licences != null) {
            licences = null;
            view.invalidate();
        } else if (sheet != null) {
            sheet = null;
            view.invalidate();
        } else if (completeSheet != null) {
            completeSheet = null;
            view.invalidate();
        } else if (nameSheet != null) {
            closeNameSheet(false);
        } else if (mapSheet != null) {
            mapSheet = null;
            view.invalidate();
        } else if (switchSheet != null) {
            switchSheet = null;
            view.invalidate();
        } else if (galleryOpen) {
            galleryOpen = false;
            view.invalidate();
        } else if (profileOpen) {
            profileOpen = false;
            view.invalidate();
        } else if (page == ENTRY_CAMPAIGN && inMissions) {
            inMissions = false;
            view.invalidate();
        } else if (page == ENTRY_MULTIPLAYER && lan.back()) {
            view.invalidate();
        } else if (page != PAGE_NONE) {
            page = PAGE_NONE;
            view.invalidate();
        } else {
            super.onBackPressed();
        }
    }

    private SharedPreferences prefs() {
        return getSharedPreferences(PREFS, MODE_PRIVATE);
    }

    /** The Multiplayer section is shown only where the file lan_preview is in the app's files. */
    private boolean lanPreview() {
        return new File(getFilesDir(), "lan_preview").exists();
    }

    // ----- maps -----

    private void loadMaps() {
        final long began = android.os.SystemClock.uptimeMillis();
        maps.clear();
        mapInfo.clear();
        final List<File> dirs = new ArrayList<File>();
        final String folder = GateActivity.rememberedFolder(this);
        if (source != 2 && folder != null) {
            dirs.add(new File(folder, "Data/Maps"));
        }
        if (source != 1) {
            dirs.add(MapStore.directory(this));
        }
        for (File dir : dirs) {
            collectMaps(dir, 0, null);
        }
        selectedMap = 0;
        listScroll = 0f;
        opponents = 0;
        android.util.Log.i("WargusHD", "LAUNCHER maps " + maps.size() + " listed in "
            + (android.os.SystemClock.uptimeMillis() - began) + " ms");
    }

    /**
     * The maps of a folder: its own first, then those of its sub-folders, each in name order
     * (the game's own catalogue reads the game folder's maps the same way, sub-folders included).
     */
    private void collectMaps(File dir, int depth, String skipTopFolder) {
        final File[] files = dir.listFiles();
        if (files == null || depth > 3) {
            return;
        }
        Arrays.sort(files);
        for (File f : files) {
            if (f.isFile() && f.getName().toLowerCase().endsWith(".pud")) {
                maps.add(f);
                mapInfo.add(PudInfo.of(f));
            }
        }
        for (File f : files) {
            if (f.isDirectory() && !(depth == 0 && f.getName().equals(skipTopFolder))) {
                collectMaps(f, depth + 1, null);
            }
        }
    }

    private static String mapName(File f) {
        final String n = f.getName();
        return n.substring(0, n.length() - 4);
    }

    private String playersText(PudInfo i) {
        return (i.players > 0) ? kit.own(i.players == 1 ? "{0} player" : "{0} players", i.players) : "";
    }

    private String factsText(PudInfo i) {
        final StringBuilder s = new StringBuilder();
        if (i.width > 0 && i.height > 0) {
            s.append(i.width).append(" x ").append(i.height);
        }
        if (i.players > 0) {
            s.append(s.length() > 0 ? " - " : "").append(playersText(i));
        }
        if (i.tileset >= 0) {
            final String[] keys = { "customscenario_tileset_forest", "customscenario_tileset_winter",
                                    "customscenario_tileset_wasteland", "customscenario_tileset_orcswamp" };
            final String[] plain = { "Forest", "Winter", "Wasteland", "Orc Swamp" };
            s.append(s.length() > 0 ? " - " : "").append(kit.string(keys[i.tileset], plain[i.tileset]));
        }
        return s.toString();
    }

    // ----- settings -----

    private String mapDefault() {
        return kit.string("customscenario_mapdefault", "Map Default");
    }

    private String raceText() {
        switch (race) {
            case 1: return kit.string("customscenario_race_human", "Human");
            case 2: return kit.string("customscenario_race_orc", "Orc");
            // The original's list is Random, Human, Orc; it has no "Map Default"
            // (work/agent-orig-7/ORIG7.md item 4).
            default: return kit.string("customscenario_random", "Random");
        }
    }

    private String opponentsText() {
        if (opponents == 0) {
            return mapDefault();
        }
        return kit.string("customscenario_opponents_" + opponents,
                          kit.own(opponents == 1 ? "{0} Opponent" : "{0} Opponents", opponents));
    }

    private String resourcesText() {
        switch (resources) {
            case 1: return kit.string("customscenario_resources_low", "Low");
            case 2: return kit.string("customscenario_resources_medium", "Medium");
            case 3: return kit.string("customscenario_resources_high", "High");
            // The original's list is Random, Low, Medium, High and opens on Random; it has no
            // "Map Default" (work/agent-orig-3/ORIG3.md item 2).
            default: return kit.string("customscenario_random", "Random");
        }
    }

    /** The original's list: Map Default, Forest, Winter, Wasteland, Orc Swamp (work/agent-orig-7). */
    private String tilesetText() {
        final String[] keys = { "customscenario_tileset_forest", "customscenario_tileset_winter",
                                "customscenario_tileset_wasteland", "customscenario_tileset_orcswamp" };
        final String[] plain = { "Forest", "Winter", "Wasteland", "Orc Swamp" };
        return (tileset >= 1 && tileset <= 4) ? kit.string(keys[tileset - 1], plain[tileset - 1]) : mapDefault();
    }

    private String unitsText() {
        return (units == 1) ? kit.string("customscenario_units_peasant", "One Peasant Only")
                            : mapDefault();
    }

    /** The most opponents the selected map can hold. */
    private int maxOpponents() {
        if (selectedMap >= mapInfo.size()) {
            return 0;
        }
        return Math.max(0, Math.min(7, mapInfo.get(selectedMap).players - 1));
    }

    // ----- start and return -----

    private static String luaString(String s) {
        return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    private static final int REQ_IMPORT_MAP = 2001;

    /** Let the player pick a map file; it is copied into the player's own maps. */
    private void pickMap() {
        final android.content.Intent i = new android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(android.content.Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        try {
            startActivityForResult(i, REQ_IMPORT_MAP);
        } catch (RuntimeException e) {
            tell(kit.own("No file picker on this device"), kit.own("A map cannot be chosen here."));
        }
    }

    @Override
    protected void onActivityResult(int request, int result, android.content.Intent data) {
        super.onActivityResult(request, result, data);
        if (request != REQ_IMPORT_MAP || result != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        final File stored = MapStore.importFrom(this, data.getData());
        if (stored == null) {
            final String name = MapStore.nameOf(this, data.getData());
            tell(kit.own("That file is not a map"), kit.own("{0} was not imported.", name != null ? name : kit.own("The file")));
            return;
        }
        android.util.Log.i("WargusHD", "LAUNCHER map imported " + stored.getName());
        // Imported maps are listed under My maps: show it there, selected and in view.
        page = ENTRY_SCENARIO;
        source = 2;
        loadMaps();
        for (int i = 0; i < maps.size(); ++i) {
            if (maps.get(i).equals(stored)) {
                selectedMap = i;
            }
        }
        opponents = Math.min(opponents, maxOpponents());
        revealMap = true;
        view.invalidate();
    }

    /** Open the Custom Scenario page with the settings of the last scenario started. */
    private void openScenario() {
        final SharedPreferences p = prefs();
        source = Math.max(0, Math.min(2, p.getInt("scenarioSourceTab", source)));
        loadMaps();
        final String last = p.getString("scenarioMap", null);
        for (int i = 0; last != null && i < maps.size(); ++i) {
            if (maps.get(i).getPath().equals(last)) {
                selectedMap = i;
            }
        }
        // 0 = Random, 1 = Human, 2 = Orc. A value stored by an older build: 3 was Random and
        // 0 was "Map Default", which the list no longer has - both read Random now.
        race = p.getInt("scenarioRace", 0);
        race = (race == 1 || race == 2) ? race : 0;
        resources = Math.max(0, Math.min(3, p.getInt("scenarioResources", 0)));
        units = Math.max(0, Math.min(1, p.getInt("scenarioUnits", 0)));
        tileset = Math.max(0, Math.min(4, p.getInt("scenarioTileset", 0)));
        opponents = Math.max(0, Math.min(maxOpponents(), p.getInt("scenarioOpponents", 0)));
        revealMap = true;
    }

    private void startScenario() {
        if (profile == null || selectedMap >= maps.size()) {
            return;
        }
        final File map = maps.get(selectedMap);
        // The last used scenario settings are the launcher's own, kept for the next visit.
        prefs().edit().putInt("scenarioSourceTab", source).putString("scenarioMap", map.getPath())
            .putInt("scenarioRace", race).putInt("scenarioOpponents", opponents)
            .putInt("scenarioResources", resources).putInt("scenarioUnits", units)
            .putInt("scenarioTileset", tileset).apply();
        final String folder = GateActivity.rememberedFolder(this);
        // The engine resolves maps of the game folder by their path inside it.
        final String path;
        if (folder != null && map.getPath().startsWith(folder + "/")) {
            path = map.getPath().substring(folder.length() + 1);
        } else {
            // A map of the app's own data: its path inside the data directory.
            final String data = Provision.dataDir(this).getPath() + "/";
            path = map.getPath().startsWith(data) ? map.getPath().substring(data.length())
                                                  : "maps/" + map.getName();
        }
        final StringBuilder r = new StringBuilder();
        r.append("  mode = \"scenario\",\n");
        r.append("  map = ").append(luaString(path)).append(",\n");
        final String[] races = { "random", "human", "orc" };
        r.append("  race = \"").append(races[race]).append("\",\n");
        if (opponents > 0) {
            r.append("  opponents = ").append(opponents).append(",\n");
        }
        // Random draws one of the three amounts for this game (the original's own odds are not
        // measured: three samples gave Low once and Medium twice).
        final int amount = (resources > 0) ? resources - 1 : (int) (Math.random() * 3);
        r.append("  resources = ").append(amount).append(",\n");
        r.append("  units = ").append(units - 1).append(",\n");
        if (tileset > 0) {
            // The engine's own names of the four tilesets (scripts/tilesets/<name>.lua).
            final String[] names = { "summer", "winter", "wasteland", "swamp" };
            r.append("  tileset = \"").append(names[tileset - 1]).append("\",\n");
        }
        r.append("  color = ").append(profile.color).append(",\n");
        // The result window after the game is shown above the game by its own activity.
        r.append("  window = true,\n");
        launch(r.toString());
    }

    private void startMission() {
        if (profile == null || missions.campaign() == null) {
            return;
        }
        final StringBuilder r = new StringBuilder();
        r.append("  mode = \"campaign\",\n");
        r.append("  campaign = \"").append(missions.campaign().key).append("\",\n");
        r.append("  mission = ").append(missions.mission()).append(",\n");
        // The engine counts difficulty from 1: easy, normal, hard.
        r.append("  difficulty = ").append(missions.difficulty + 1).append(",\n");
        r.append("  chain = true,\n");
        r.append("  window = true,\n");
        launch(r.toString());
    }

    private void openSwitchSheet() {
        switchSheet = new SwitchSheet(Profiles.list(this), profile.id);
    }

    /** Ask before deleting the profile chosen in the list; the list is shown again afterwards. */
    private void askDeleteProfile() {
        final Profiles.Profile gone = switchSheet.chosen();
        if (gone == null) {
            return;
        }
        sheet = new ConfirmSheet(kit.own("Delete this profile?"), kit.own("{0} - its progress and saved games", gone.name),
                                 kit.string("common_cancel", "Cancel"),
                                 kit.string("common_delete", "Delete"));
        sheetAction = new Runnable() {
            @Override
            public void run() {
                Profiles.delete(HomeActivity.this, gone);
                openSwitchSheet();
            }
        };
    }

    private void openNameSheet(boolean forNew) {
        nameIsNew = forNew;
        nameSheet = new NameSheet(firstStep ? kit.own("Your name") : forNew ? kit.own("New profile") : kit.own("Rename profile"),
                                  forNew ? "" : profile.name);
        view.requestFocus();
        final InputMethodManager keys = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        keys.restartInput(view);
        keys.showSoftInput(view, 0);
        view.invalidate();
    }

    /** True for a map the player imported: one of the files of the store. */
    private boolean isOwnMap(File map) {
        return map != null && map.getParentFile() != null
            && map.getParentFile().equals(MapStore.directory(this));
    }

    private void openMapNameSheet(File map) {
        renamingMap = map;
        nameSheet = new NameSheet(kit.own("Rename map"), mapName(map));
        view.requestFocus();
        final InputMethodManager keys = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        keys.restartInput(view);
        keys.showSoftInput(view, 0);
        view.invalidate();
    }

    /** Give an imported map another name; a name that is taken or empty changes nothing. */
    private void renameMap(File map, String wanted) {
        final StringBuilder clean = new StringBuilder();
        for (char ch : Profiles.clean(wanted).toCharArray()) {
            // Characters a file name cannot carry are left out.
            if ("/\\:*?\"<>|".indexOf(ch) < 0) {
                clean.append(ch);
            }
        }
        final String name = clean.toString().trim();
        final File target = new File(map.getParentFile(), name + ".pud");
        if (name.isEmpty() || target.equals(map)) {
            return;
        }
        if (target.exists() || !map.renameTo(target)) {
            tell(kit.own("The map was not renamed"), kit.own("{0} is already there.", name));
            return;
        }
        loadMaps();
        for (int i = 0; i < maps.size(); ++i) {
            if (maps.get(i).equals(target)) {
                selectedMap = i;
            }
        }
        revealMap = true;
    }

    private void askRemoveMap(final File map) {
        sheet = new ConfirmSheet(kit.own("Remove this map?"), mapName(map),
                                 kit.string("common_cancel", "Cancel"),
                                 kit.string("common_delete", "Delete"));
        sheetAction = new Runnable() {
            @Override
            public void run() {
                if (map.delete()) {
                    android.util.Log.i("WargusHD", "LAUNCHER map removed " + map.getName());
                }
                loadMaps();
                view.invalidate();
            }
        };
    }

    /** Close the name sheet; with `use`, make the new profile or rename the active one. */
    private void closeNameSheet(boolean use) {
        final NameSheet done = nameSheet;
        nameSheet = null;
        ((InputMethodManager) getSystemService(INPUT_METHOD_SERVICE))
            .hideSoftInputFromWindow(view.getWindowToken(), 0);
        if (renamingMap != null) {
            final File map = renamingMap;
            renamingMap = null;
            if (use && done != null) {
                renameMap(map, done.name());
            }
            Screen.useWhole(this);
            view.invalidate();
            return;
        }
        if (use && done != null && !done.name().trim().isEmpty()) {
            if (nameIsNew) {
                // The same portrait the first profile starts with; the colour after the last one made.
                final Profiles.Profile made =
                    Profiles.create(this, done.name(), 8, Profiles.count(this) % Profiles.COLORS.length);
                if (made != null) {
                    profile = made;
                    profileChanged();
                }
            } else {
                profile.name = Profiles.clean(done.name());
                Profiles.update(this, profile);
            }
        }
        if (firstStep) {
            // Whatever was done with the name, the portrait and the colour come next.
            firstStep = false;
            profileOpen = true;
        }
        Screen.useWhole(this);
        view.invalidate();
    }

    /** Another profile is the active one: everything shown that is the profile's own follows. */
    private void profileChanged() {
        campaign.readProgress(this, profile);
        if (inMissions && missions.campaign() != null) {
            missions.open(missions.campaign());
        }
        load.refresh(this, profile);
        options.read(this, profile);
        view.portraitChanged();
    }

    private void askDeleteSave() {
        final LoadPage.Save save = load.selectedSave();
        if (save == null) {
            return;
        }
        sheet = new ConfirmSheet(kit.own("Delete this saved game?"), save.name,
                                 kit.string("common_cancel", "Cancel"),
                                 kit.string("common_delete", "Delete"));
        sheetAction = new Runnable() {
            @Override
            public void run() {
                load.deleteSelected(HomeActivity.this, profile);
            }
        };
    }

    private void startSave() {
        final LoadPage.Save save = load.selectedSave();
        if (profile == null || save == null) {
            return;
        }
        // A save that has gone since the list was read is not started: the list is read again.
        if (!new File(new File(Profiles.directory(this, profile), "wc2/save"), save.file).isFile()) {
            android.util.Log.i("WargusHD", "LAUNCH save gone " + save.file);
            load.refresh(this, profile);
            tell(kit.own("The saved game is gone"), kit.own("{0} is no longer on this device.", save.name));
            return;
        }
        launch("  mode = \"load\",\n  file = \"" + save.file + "\",\n  window = true,\n");
    }

    /** Write a request with the given fields and a fresh token, and start the game with it. */
    private void launch(String fields) {
        launch(fields, profile.name);
    }

    /**
     * A network game from the Multiplayer page's room: the player's name in the request is the
     * one the seat has in the engine's room.
     */
    @Override
    public boolean startLanGame(String fields, String engineName) {
        return profile != null && launch(fields, engineName);
    }

    /**
     * Write a request with the given fields, the given player's name and a fresh token, and
     * start the game with it.
     *
     * @return true when the game was started
     */
    private boolean launch(String fields, String playerName) {
        // No game is started without the game's files: the engine would come up on nothing.
        if (!GateActivity.isSatisfied(this)) {
            filesOk = false;
            android.util.Log.i("WargusHD", "LAUNCH refused: game files missing");
            tell(kit.own("The game files are missing"), kit.own("Choose the game folder in Options, Game Files."));
            return false;
        }
        menuAudio.play(MenuAudio.BUTTON);
        final String token = GameLaunch.newToken();
        final StringBuilder r = new StringBuilder("LaunchRequest = {\n");
        r.append(fields);
        r.append(options.requestBlock());
        r.append("  language = \"").append(SkinKit.language(this)).append("\",\n");
        // The profile's name is the player's name in the game (design 22.7). A Lua string:
        // the backslash and the quote are escaped; control characters cannot be in a name.
        r.append("  playerName = \"")
            .append(playerName.replace("\\", "\\\\").replace("\"", "\\\""))
            .append("\",\n");
        r.append("  token = \"").append(token).append("\",\n");
        r.append("}\n");

        final File dir = Profiles.directory(this, profile);
        final File request = GameLaunch.requestFile(this);
        GameLaunch.resultFile(this, dir).delete();
        if (!GameLaunch.writeText(request, r.toString())) {
            return false;
        }
        // Beside the request: the profile as it stands, for the result window in the game.
        try {
            GameLaunch.writeText(new File(request.getParentFile(), "profile.json"),
                                 profile.toJson().toString());
        } catch (Exception e) {
            // Without it the window shows the game alone.
        }
        prefs().edit().putString(PREF_PENDING, token)
            .putInt(PREF_ORIGIN, page)
            .putString(PREF_ORIGIN_CAMPAIGN,
                       (inMissions && missions.campaign() != null) ? missions.campaign().key : null)
            .apply();
        // The loading view goes up at once; the game's window takes it over without a break.
        final long t0 = android.os.SystemClock.uptimeMillis();
        loading = new LoadingView(this, kit, t0);
        addContentView(loading, new android.view.ViewGroup.LayoutParams(
            android.view.ViewGroup.LayoutParams.MATCH_PARENT,
            android.view.ViewGroup.LayoutParams.MATCH_PARENT));
        if (GameLaunch.start(this, token, request, dir, t0)) {
            overridePendingTransition(0, 0);
            menuMusic.fadeOutForGame();
            return true;
        }
        prefs().edit().remove(PREF_PENDING).apply();
        ((android.view.ViewGroup) loading.getParent()).removeView(loading);
        loading = null;
        return false;
    }

    /** Take in the record of the game this shell started: it feeds the profile's numbers. */
    /**
     * A game is pending and this shell was started anew meanwhile: land on the section the game
     * was started from. A pending game that neither runs nor left a record ended unexpectedly;
     * it is forgotten here.
     *
     * @return true when the pending game ended unexpectedly
     */
    private boolean restoreOrigin() {
        final String pending = prefs().getString(PREF_PENDING, null);
        if (pending == null || profile == null) {
            return false;
        }
        if (page == PAGE_NONE) {
            page = prefs().getInt(PREF_ORIGIN, PAGE_NONE);
            if (page == ENTRY_SCENARIO) {
                openScenario();
            }
            final String key = prefs().getString(PREF_ORIGIN_CAMPAIGN, null);
            inMissions = false;
            for (CampaignPage.Campaign k : campaign.campaigns) {
                if (page == ENTRY_CAMPAIGN && k.key.equals(key)) {
                    campaign.readProgress(this, profile);
                    missions.open(k);
                    inMissions = true;
                }
            }
        }
        final File f = GameLaunch.resultFile(this, Profiles.directory(this, profile));
        if (!f.exists() && !GameLaunch.gameRunning(this)) {
            prefs().edit().remove(PREF_PENDING).apply();
            android.util.Log.i("WargusHD", "LAUNCH pending game left no record");
            return true;
        }
        return false;
    }

    /** What is chosen and shown, as one text: two different texts mean a tap changed something. */
    private String stateStamp() {
        final LoadPage.Save save = load.selectedSave();
        return page + "/" + inMissions + "/" + profileOpen + "/" + galleryOpen + "/" + (sheet != null)
            + "/" + (nameSheet != null) + "/" + (mapSheet != null) + "/" + (switchSheet != null) + "/" + (completeSheet != null)
            + "/" + (licences != null) + "/" + source + "/" + selectedMap + "/" + race + "/" + opponents
            + "/" + resources + "/" + units + "/" + tileset + "/" + missions.mission() + "/" + missions.difficulty
            + "/" + (missions.campaign() != null ? missions.campaign().key : "")
            + "/" + (save != null ? save.file : "")
            + "/" + (profile != null ? profile.color + "." + profile.portrait + "." + profile.name : "");
    }

    /** The machine reason of a record that ended in an error; told once. */
    private String errorReason;
    /** How a network game ended when it had no outcome of its own (the record's "net"). */
    private String netReason;

    /** Say one thing on a sheet with OK, above whatever page is shown. */
    private void tell(String title, String body) {
        sheet = ConfirmSheet.notice(title, body);
        sheetAction = null;
        menuAudio.play(MenuAudio.ERROR);
        if (view != null) {
            view.invalidate();
        }
    }

    private void collectResult() {
        final String pending = prefs().getString(PREF_PENDING, null);
        if (pending == null || profile == null) {
            return;
        }
        final File f = GameLaunch.resultFile(this, Profiles.directory(this, profile));
        final String text = GameLaunch.readText(f);
        if (text == null) {
            return;   // still running, or it ended without a record
        }
        f.delete();
        prefs().edit().remove(PREF_PENDING).apply();
        cameBack = true;
        try {
            final JSONObject record = new JSONObject(text);
            if (!pending.equals(record.optString("token"))) {
                return;
            }
            if ("error".equals(record.optString("exit"))) {
                errorReason = record.optString("error", "unknown");
            }
            netReason = record.has("net") ? record.optString("net") : null;
            final JSONArray games = record.optJSONArray("games");
            for (int i = 0; games != null && i < games.length(); ++i) {
                final JSONObject game = games.getJSONObject(i);
                // A network game this device was cut off from, or one no other team started,
                // is not counted at all. One the player left is recorded as a defeat.
                final String net = game.optString("net");
                if ("net_lost".equals(net) || "net_no_result".equals(net)) {
                    continue;
                }
                ++profile.games;
                final String outcome = game.optString("outcome");
                if ("victory".equals(outcome)) {
                    ++profile.victories;
                    if ("campaign".equals(record.optString("mode"))) {
                        // campaigns/<human|orc|human-exp|orc-exp>/…: which campaign saved progress.
                        final String[] part = game.optString("map").split("/");
                        if (part.length >= 2) {
                            final String key = (part[1].endsWith("-exp") ? "bdp" : "tod")
                                + (part[1].startsWith("orc") ? "Orc" : "Human");
                            for (CampaignPage.Campaign k : campaign.campaigns) {
                                if (k.key.equals(key)) {
                                    savedCampaign = k;
                                    wonMission = game.optInt("mission", 0);
                                }
                            }
                        }
                    }
                } else if ("defeat".equals(outcome) || "draw".equals(outcome)) {
                    // A draw (a surrender ends as one) is shown and counted as a defeat (design 29.4).
                    ++profile.defeats;
                }
                // The profile's own line among the game's players.
                final JSONArray players = game.optJSONArray("players");
                for (int k = 0; players != null && k < players.length(); ++k) {
                    final JSONObject who = players.getJSONObject(k);
                    if (who.optBoolean("me", false)) {
                        profile.units += who.optLong("units", 0L);
                        profile.buildings += who.optLong("buildings", 0L);
                        profile.kills += who.optLong("kills", 0L);
                        profile.razings += who.optLong("razings", 0L);
                        // The game's score with its bonus when the record has it.
                        profile.score += game.optLong("score", who.optLong("score", 0L));
                    }
                }
                final int rank = game.optInt("rank_index", 0);
                if (rank > profile.bestRank) {
                    profile.bestRank = rank;
                    profile.bestRankOrc = "orc".equals(game.optString("race"));
                }
            }
            profile.lastUsed = System.currentTimeMillis();
            Profiles.update(this, profile);
        } catch (Exception e) {
            // A record that cannot be read changes nothing.
        }
    }

    // ----- the view -----

    private final class HomeView extends View {
        private volatile Bitmap backdropStart, backdropPage;
        private Bitmap logo, portrait, action;
        private boolean loaded;
        /** Rail entry under the finger, or -1. */
        private int pressed = -1;
        private android.graphics.Bitmap plainButton;
        // List dragging.
        private float downX, downY, downScroll;
        /** True from the moment a held row opened its sheet until that finger is lifted. */
        private boolean heldOpened;
        /** A row of the player's own maps held down for half a second opens the map's sheet. */
        private final Runnable held = new Runnable() {
            @Override
            public void run() {
                final int row = (int) ((downY - listTop() + listScroll) / LIST_PITCH);
                if (page == ENTRY_SCENARIO && inList && !dragging && row >= 0 && row < maps.size()
                        && isOwnMap(maps.get(row))) {
                    selectedMap = row;
                    sheetMap = maps.get(row);
                    mapSheet = new MapSheet(mapName(sheetMap));
                    heldOpened = true;
                    menuAudio.play(MenuAudio.BUTTON);
                    invalidate();
                }
            }
        };
        private boolean inList, dragging;

        HomeView(Context c) {
            super(c);
            // The name sheet types into this view.
            setFocusable(true);
            setFocusableInTouchMode(true);
            if (android.os.Build.VERSION.SDK_INT >= 26) {
                // No system frame around the whole shell while it holds the focus.
                setDefaultFocusHighlightEnabled(false);
            }
        }

        private float filesY() { return kit.height - FILES_UP; }
        private float musicY() { return kit.height - MUSIC_UP; }
        private float contentBottom() { return kit.height - CONTENT_BOTTOM_UP; }
        private float listTop() { return CONTENT_TOP + LIST_ROWS_DY; }
        private float listBottom() { return contentBottom() - 24f; }
        private float actionX() { return Screen.CANVAS_W - ACTION_RIGHT - ACTION_W; }
        private float actionY() { return kit.height - ACTION_UP; }
        private float valueX() { return INFO_X + INFO_W - 60f - VALUE_W; }
        /**
         * The owner's choice of 2026-10-06 (preview, option C): the map's picture has a panel of
         * its own under the facts; the settings stand at the foot of the page.
         */
        private float setTop() { return contentBottom() - (SET_ROW_DY + SET_PITCH * 5f); }
        private float setRowY(int i) { return setTop() + SET_ROW_DY + SET_PITCH * i; }

        private float maxScroll() {
            return Math.max(0f, maps.size() * LIST_PITCH - 6f - (listBottom() - listTop()));
        }

        /** The chip's portrait is made again from the active profile. */
        void portraitChanged() {
            if (profile != null) {
                portrait = Portraits.tinted(kit, profile.portrait, Profiles.COLORS[profile.color]);
            }
            invalidate();
        }

        @Override
        public boolean onCheckIsTextEditor() {
            return nameSheet != null || lan.typing();
        }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo out) {
            if (nameSheet == null) {
                return lan.input(this, out);
            }
            out.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
                | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD;
            out.imeOptions = EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI
                | EditorInfo.IME_FLAG_NO_FULLSCREEN;
            // Text is taken as it is committed; nothing is kept for the keyboard to edit.
            return new BaseInputConnection(this, false) {
                @Override
                public boolean commitText(CharSequence text, int newCursorPosition) {
                    if (nameSheet != null) {
                        nameSheet.type(text);
                        invalidate();
                    }
                    return true;
                }

                @Override
                public boolean deleteSurroundingText(int before, int after) {
                    if (nameSheet != null) {
                        nameSheet.erase(before);
                        invalidate();
                    }
                    return true;
                }

                @Override
                public boolean performEditorAction(int action) {
                    closeNameSheet(true);
                    return true;
                }
            };
        }

        @Override
        public boolean onKeyDown(int code, KeyEvent e) {
            if (nameSheet == null) {
                if (lan.key(code, e)) {
                    invalidate();
                    return true;
                }
                return super.onKeyDown(code, e);
            }
            if (code == KeyEvent.KEYCODE_DEL) {
                nameSheet.erase(1);
            } else if (code == KeyEvent.KEYCODE_ENTER) {
                closeNameSheet(true);
                return true;
            } else if (e.getUnicodeChar() != 0) {
                nameSheet.type(String.valueOf((char) e.getUnicodeChar()));
            } else {
                return super.onKeyDown(code, e);
            }
            invalidate();
            return true;
        }

        /**
         * The start state's pictures, read side by side and waited for, so that the first frame
         * is whole; the pages' backdrop, which the start state does not show, comes when ready.
         */
        private void load(final int w, final int h) {
            loaded = true;
            final long t0 = android.os.SystemClock.uptimeMillis();
            final int logoW = Math.round(LOGO_W * kit.unit), logoH = Math.round(LOGO_H * kit.unit);
            final Thread[] readers = {
                new Thread(new Runnable() {
                    @Override
                    public void run() {
                        logo = kit.atlasFrame("Modern_Graphics", "logo_main", logoW, logoH);
                    }
                }, "home-logo"),
                new Thread(new Runnable() {
                    @Override
                    public void run() {
                        // The button frame keeps its own pixels: its borders are drawn unscaled
                        // in units.
                        action = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
                        if (profile != null) {
                            portrait =
                                Portraits.tinted(kit, profile.portrait, Profiles.COLORS[profile.color]);
                        }
                    }
                }, "home-button-portrait"),
            };
            for (Thread t : readers) {
                t.start();
            }
            if (page == PAGE_NONE) {
                backdropStart = kit.file("Data/Backgrounds/main_background_characters.png", w, h);
                new Thread(new Runnable() {
                    @Override
                    public void run() {
                        backdropPage = kit.file("Data/Backgrounds/main_background.png", w, h);
                        postInvalidate();
                    }
                }, "home-page-backdrop").start();
            } else {
                backdropPage = kit.file("Data/Backgrounds/main_background.png", w, h);
                new Thread(new Runnable() {
                    @Override
                    public void run() {
                        backdropStart =
                            kit.file("Data/Backgrounds/main_background_characters.png", w, h);
                        postInvalidate();
                    }
                }, "home-start-backdrop").start();
            }
            for (Thread t : readers) {
                try {
                    t.join();
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
            }
            android.util.Log.i("WargusHD", "LAUNCHER home load: create to draw " + (t0 - createdAt)
                + " ms, pictures " + (android.os.SystemClock.uptimeMillis() - t0));
        }

        private void panel(Canvas c, float x, float y, float w, float h) {
            kit.fill(c, x, y, w, h, PANEL_FILL, PANEL_RADIUS);
            kit.outline(c, x, y, w, h, LINE, PANEL_RADIUS);
        }

        private void well(Canvas c, float x, float y, float w, float h) {
            kit.fill(c, x, y, w, h, WELL, BOX_RADIUS);
            kit.outline(c, x, y, w, h, LINE, BOX_RADIUS);
        }

        @Override
        protected void onDraw(Canvas c) {
            final int w = getWidth(), h = getHeight();
            kit.begin(w, h);
            if (!loaded) {
                load(w, h);
            }
            kit.backdrop(c, page == PAGE_NONE ? backdropStart : backdropPage, w, h, 0xFF0D1114);

            drawRail(c);
            drawChip(c);
            if (page == ENTRY_SCENARIO) {
                drawScenario(c);
            } else if (page == ENTRY_LOAD) {
                load.draw(c, kit);
            } else if (page == ENTRY_OPTIONS) {
                options.draw(c, kit);
            } else if (page == ENTRY_MULTIPLAYER) {
                lan.draw(c, kit, profile);
            } else if (page == ENTRY_CAMPAIGN && inMissions) {
                missions.draw(c, kit);
            } else if (page == ENTRY_CAMPAIGN) {
                kit.textTop(c, kit.string("campaign", "Campaign"), CONTENT_X, TITLE_Y, CAP_TITLE,
                            INK_TITLE, false);
                campaign.draw(c, kit);
            }
            if (galleryOpen) {
                gallery.draw(c, kit);
            } else if (profileOpen && profile != null) {
                profileSheet.draw(c, kit, profile, campaign.campaigns);
            }
            if (completeSheet != null) {
                completeSheet.draw(c, kit);
            }
            if (switchSheet != null) {
                switchSheet.draw(c, kit);
            }
            if (mapSheet != null) {
                mapSheet.draw(c, kit);
            }
            if (nameSheet != null) {
                nameSheet.draw(c, kit);
            }
            if (licences != null) {
                licences.draw(c, kit);
            }
            if (sheet != null) {
                sheet.draw(c, kit);
            }
            if (!firstFrameTold) {
                firstFrameTold = true;
                android.util.Log.i("WargusHD", "LAUNCHER home first frame in "
                    + (android.os.SystemClock.uptimeMillis() - createdAt) + " ms");
            }
        }

        private void drawRail(Canvas c) {
            kit.fill(c, 0f, 0f, RAIL_W, kit.height, RAIL_FILL, 0f);
            kit.fill(c, RAIL_W - EDGE_W, 0f, EDGE_W, kit.height, LINE, 0f);
            kit.picture(c, logo, PAD, PAD, LOGO_W, LOGO_H);

            for (int i = 0; i < entries.length; ++i) {
                final float y = ROW_Y0 + i * ROW_PITCH;
                final boolean selected = (i == page);
                if (selected || i == pressed) {
                    kit.fill(c, ROW_X, y, ROW_W, ROW_H, ROW_SELECTED, ROW_RADIUS);
                }
                if (selected) {
                    kit.fill(c, ROW_X, y, 10f, ROW_H, GOLD, 0f);
                }
                // The campaign's entry shares its row with the count at the right.
                final float room = ROW_W - 2f * ROW_INSET - (i == ENTRY_CAMPAIGN ? 200f : 0f);
                kit.textMiddle(c, kit.fit(entries[i], CAP_BODY, room), ROW_X + ROW_INSET, y, ROW_H, CAP_BODY,
                               selected ? GOLD : INK, true);
                if (i == ENTRY_CAMPAIGN) {
                    // Missions completed in all four campaigns.
                    kit.textMiddleRight(c, campaign.completedTotal() + " / " + campaign.missionsTotal(),
                                        ROW_X + ROW_W - 36f, y, ROW_H, CAP_SMALL, INK_MUTED, true);
                }
            }

            final float fy = filesY();
            kit.fill(c, ROW_X, fy, ROW_W, FOOT_H, ROW_FAINT, ROW_RADIUS);
            kit.fill(c, ROW_X + 30f, fy + 33f, 30f, 30f, filesOk ? DOT_OK : DOT_BAD, 15f);
            kit.textMiddle(c, filesOk ? kit.own("Game files: OK") : kit.own("Game files: missing"),
                           ROW_X + 84f, fy, FOOT_H, CAP_SMALL, INK, true);

            final float my = musicY();
            kit.fill(c, ROW_X, my, ROW_W, FOOT_H, ROW_FAINT, ROW_RADIUS);
            kit.textMiddle(c, kit.own("Menu music"), ROW_X + 30f, my, FOOT_H, CAP_SMALL, INK, true);
            final float sx = ROW_X + ROW_W - 180f;
            kit.fill(c, sx, my + 12f, 144f, 72f, music ? SWITCH_ON : SWITCH_OFF, 36f);
            kit.outline(c, sx, my + 12f, 144f, 72f, LINE, 36f);
            kit.fill(c, sx + (music ? 78f : 6f), my + 18f, 60f, 60f, KNOB, 30f);

            kit.textTop(c, version, PAD, kit.height - VERSION_UP, CAP_SMALL, INK_MUTED, true);
        }

        /** The portrait carries the colour, so the chip has no separate swatch. */
        private void drawChip(Canvas c) {
            if (profile == null) {
                return;
            }
            final float cx = Screen.CANVAS_W - CHIP_RIGHT - CHIP_W;
            panel(c, cx, CHIP_Y, CHIP_W, CHIP_H);
            kit.picture(c, portrait, cx + 18f, CHIP_Y + 18f, PORTRAIT_W, PORTRAIT_H);
            kit.textTop(c, profile.name, cx + 216f, CHIP_Y + 36f, CAP_BODY, INK, true);
            final String stats = (profile.games == 0)
                ? kit.own("No games yet")
                : kit.own(profile.games == 1 ? "{0} game - {1} won" : "{0} games - {1} won",
                          profile.games, profile.victories);
            kit.textTop(c, stats, cx + 216f, CHIP_Y + 108f, CAP_SMALL, INK_MUTED, true);
        }

        private void drawScenario(Canvas c) {
            if (revealMap) {
                revealMap = false;
                final float row = selectedMap * LIST_PITCH, room = listBottom() - listTop();
                if (row + LIST_PITCH > listScroll + room) {
                    listScroll = Math.min(maxScroll(), row + LIST_PITCH - room);
                }
            }
            kit.textTop(c, kit.string("customscenario_title", "Custom Game Setup"),
                        CONTENT_X, TITLE_Y, CAP_TITLE, INK_TITLE, false);

            // Left: sources and the map list.
            final float bottom = contentBottom();
            panel(c, CONTENT_X, CONTENT_TOP, LIST_W, bottom - CONTENT_TOP);
            final float sx = CONTENT_X + 24f, sy = CONTENT_TOP + 24f;
            well(c, sx, sy, LIST_ROW_W, SRC_H);
            final String[] sources = { kit.own("All"), kit.own("Remastered"), kit.own("My maps") };
            final float segW = LIST_ROW_W / sources.length;
            for (int i = 0; i < sources.length; ++i) {
                final float x = sx + segW * i + 6f;
                final boolean on = (i == source);
                kit.fill(c, x, sy + 6f, segW - 12f, SRC_H - 12f, on ? GOLD : SEG_FAINT, 10f);
                kit.textCentre(c, sources[i], x, sy + 6f, segW - 12f, SRC_H - 12f, CAP_MID,
                               on ? INK_ON_GOLD : INK, !on);
            }

            c.save();
            c.clipRect(kit.rect(sx, listTop(), LIST_ROW_W, listBottom() - listTop()));
            if (maps.isEmpty()) {
                // The owner's choice of 2026-10-06 (option B): one line in the middle of the list.
                kit.textCentre(c, kit.own("No maps here"), sx, listTop(), LIST_ROW_W, listBottom() - listTop(),
                               CAP_BODY, INK_MUTED, true);
            }
            for (int i = 0; i < maps.size(); ++i) {
                final float y = listTop() + i * LIST_PITCH - listScroll;
                if (y + ROW_H < listTop() || y > listBottom()) {
                    continue;
                }
                final boolean sel = (i == selectedMap);
                kit.fill(c, sx, y, LIST_ROW_W, ROW_H, sel ? ROW_SELECTED : ROW_FAINT, ROW_RADIUS);
                if (sel) {
                    kit.fill(c, sx, y, 10f, ROW_H, GOLD, 0f);
                }
                final String right = playersText(mapInfo.get(i));
                kit.textMiddle(c, kit.fit(mapName(maps.get(i)), CAP_BODY, LIST_ROW_W - ROW_INSET - 520f),
                               sx + ROW_INSET, y, ROW_H, CAP_BODY, INK, true);
                kit.textMiddleRight(c, right, sx + LIST_ROW_W - ROW_INSET, y, ROW_H, CAP_MID,
                                    sel ? INK : INK_MUTED, true);
            }
            c.restore();

            // Right: the selected map, then the settings.
            panel(c, INFO_X, CONTENT_TOP, INFO_W, INFO_H);
            if (selectedMap < maps.size()) {
                kit.textTop(c, kit.fit(mapName(maps.get(selectedMap)), CAP_BODY, INFO_W - 120f),
                            INFO_X + 60f, CONTENT_TOP + 66f, CAP_BODY, GOLD, true);
                kit.textTop(c, factsText(mapInfo.get(selectedMap)), INFO_X + 60f,
                            CONTENT_TOP + 66f + LINE_PITCH, CAP_MID, INK_MUTED, true);
            }

            final float picH = setTop() - 48f - SET_Y;
            panel(c, INFO_X, SET_Y, INFO_W, picH);
            if (selectedMap < maps.size()) {
                final String folder = GateActivity.rememberedFolder(HomeActivity.this);
                final Bitmap preview = MapPreview.of(folder != null ? new File(folder) : null,
                    Provision.dataDir(HomeActivity.this), maps.get(selectedMap));
                if (preview != null) {
                    // The whole map, its sides kept, centred in the panel with 48 units around.
                    final float room = picH - 96f;
                    final float k = Math.min(room / preview.getHeight(), (INFO_W - 96f) / preview.getWidth());
                    final float pw = preview.getWidth() * k, ph = preview.getHeight() * k;
                    final float px = INFO_X + (INFO_W - pw) / 2f, py = SET_Y + (picH - ph) / 2f;
                    kit.pictureSharp(c, preview, px, py, pw, ph);
                    kit.outline(c, px, py, pw, ph, LINE, 0f);
                }
            }
            panel(c, INFO_X, setTop(), INFO_W, bottom - setTop());
            final String[] labels = {
                kit.string("customscenario_race", "Your Race:"),
                kit.string("customscenario_opponents", "Opponents:"),
                kit.string("customscenario_tileset", "Map Tileset:"),
                kit.string("customscenario_resources", "Resources:"),
                kit.string("customscenario_units", "Units:"),
            };
            final String[] values = { raceText(), opponentsText(), tilesetText(), resourcesText(),
                                      unitsText() };
            for (int i = 0; i < labels.length; ++i) {
                final float y = setRowY(i);
                kit.textMiddle(c, labels[i], INFO_X + 60f, y, SET_H, CAP_BODY, INK, true);
                well(c, valueX(), y, VALUE_W, SET_H);
                kit.textMiddle(c, values[i], valueX() + 36f, y, SET_H, CAP_BODY, INK, true);
            }

            if (plainButton == null) {
                plainButton = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
            }
            // The owner's choice of 2026-10-06 (option C): the import at the left of the row.
            kit.nine(c, plainButton, CONTENT_X, actionY(), ACTION_W, ACTION_H, 14);
            kit.textCentre(c, kit.own("Import a Map"), CONTENT_X, actionY(), ACTION_W, ACTION_H, CAP_BODY, INK, true);
            // With no map to start, the button is drawn as the Load page draws its disabled ones.
            final boolean canStart = selectedMap < maps.size();
            kit.nine(c, canStart ? action : plainButton, actionX(), actionY(), ACTION_W, ACTION_H, 14);
            if (!canStart) {
                kit.fill(c, actionX(), actionY(), ACTION_W, ACTION_H, 0x96000000, 0f);
            }
            kit.textCentre(c, kit.string("customscenario_start", "Start Game"),
                           actionX(), actionY(), ACTION_W, ACTION_H, CAP_BODY,
                           canStart ? GOLD : 0xFF9C9486, canStart);
        }

        private int entryAt(float x, float y) {
            if (x < ROW_X || x > ROW_X + ROW_W) {
                return -1;
            }
            for (int i = 0; i < entries.length; ++i) {
                final float top = ROW_Y0 + i * ROW_PITCH;
                if (y >= top && y < top + ROW_H) {
                    return i;
                }
            }
            return -1;
        }

        private static boolean in(float x, float y, float rx, float ry, float rw, float rh) {
            return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
        }

        /** A tap in the content area of the Custom Scenario page. */
        private void tapScenario(float x, float y) {
            final float sx = CONTENT_X + 24f, sy = CONTENT_TOP + 24f;
            if (in(x, y, sx, sy, LIST_ROW_W, SRC_H)) {
                final int seg = Math.min(2, (int) ((x - sx) / (LIST_ROW_W / 3f)));
                if (seg != source) {
                    source = seg;
                    loadMaps();
                }
            } else if (in(x, y, sx, listTop(), LIST_ROW_W, listBottom() - listTop())) {
                final int row = (int) ((y - listTop() + listScroll) / LIST_PITCH);
                if (row >= 0 && row < maps.size()) {
                    selectedMap = row;
                    opponents = Math.min(opponents, maxOpponents());
                }
            } else if (in(x, y, actionX(), actionY(), ACTION_W, ACTION_H)) {
                startScenario();
            } else if (in(x, y, CONTENT_X, actionY(), ACTION_W, ACTION_H)) {
                menuAudio.play(MenuAudio.BUTTON);
                pickMap();
            } else {
                for (int i = 0; i < 5; ++i) {
                    if (in(x, y, valueX(), setRowY(i), VALUE_W, SET_H)) {
                        switch (i) {
                            case 0: race = (race + 1) % 3; break;
                            case 1: opponents = (opponents + 1) % (maxOpponents() + 1); break;
                            case 2: tileset = (tileset + 1) % 5; break;
                            case 3: resources = (resources + 1) % 4; break;
                            case 4: units = (units + 1) % 2; break;
                            default: break;
                        }
                    }
                }
            }
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            // A tap that changed what is chosen or shown, and made no sound of its own, clicks.
            final boolean lifted = (e.getAction() == MotionEvent.ACTION_UP);
            final String before = lifted ? stateStamp() : null;
            final int asked = menuAudio.asked;
            final boolean handled = handleTouch(e);
            if (page != ENTRY_MULTIPLAYER) {
                lan.close();
            }
            if (lifted && menuAudio.asked == asked && !before.equals(stateStamp())) {
                menuAudio.play(MenuAudio.BUTTON);
            }
            return handled;
        }

        private boolean handleTouch(MotionEvent e) {
            final float x = e.getX() / kit.unit, y = e.getY() / kit.unit;
            if (licences != null && sheet == null) {
                if (e.getAction() == MotionEvent.ACTION_DOWN) {
                    licences.down(x, y, kit);
                } else if (e.getAction() == MotionEvent.ACTION_MOVE) {
                    if (licences.move(x, y, kit)) {
                        invalidate();
                    }
                } else if (e.getAction() == MotionEvent.ACTION_UP && licences.up(x, y, kit)) {
                    menuAudio.play(MenuAudio.BUTTON);
                    licences = null;
                    invalidate();
                }
                return true;
            }
            if (sheet != null) {
                if (e.getAction() == MotionEvent.ACTION_UP) {
                    final int answer = sheet.up(x, y, kit);
                    if (answer != ConfirmSheet.NONE) {
                        sheet = null;
                        if (answer == ConfirmSheet.CONFIRM && sheetAction != null) {
                            sheetAction.run();
                        }
                        sheetAction = null;
                        invalidate();
                    }
                }
                return true;
            }
            if (nameSheet != null) {
                if (e.getAction() == MotionEvent.ACTION_UP) {
                    final int answer = nameSheet.up(x, y);
                    if (answer != NameSheet.NONE) {
                        closeNameSheet(answer == NameSheet.OK);
                    }
                }
                return true;
            }
            if (mapSheet != null) {
                if (e.getAction() == MotionEvent.ACTION_UP && !heldOpened) {
                    final int answer = mapSheet.up(x, y, kit);
                    if (answer != MapSheet.NONE) {
                        final File map = sheetMap;
                        mapSheet = null;
                        sheetMap = null;
                        if (answer == MapSheet.RENAME) {
                            openMapNameSheet(map);
                        } else if (answer == MapSheet.REMOVE) {
                            askRemoveMap(map);
                        }
                        invalidate();
                    }
                }
                if (e.getAction() == MotionEvent.ACTION_UP) {
                    // The finger that held the row down has now been lifted.
                    heldOpened = false;
                }
                return true;
            }
            if (completeSheet != null) {
                if (e.getAction() == MotionEvent.ACTION_UP) {
                    final int answer = completeSheet.up(x, y, kit);
                    if (answer != CampaignCompleteSheet.NONE) {
                        // Both lead to the Campaign section: the campaign's missions, or the four cards.
                        completeSheet = null;
                        page = ENTRY_CAMPAIGN;
                        inMissions = (answer == CampaignCompleteSheet.MISSION_LIST);
                        if (inMissions) {
                            missions.open(completeCampaign);
                        }
                        invalidate();
                    }
                }
                return true;
            }
            if (switchSheet != null) {
                if (e.getAction() == MotionEvent.ACTION_DOWN) {
                    switchSheet.down(x, y, kit);
                } else if (e.getAction() == MotionEvent.ACTION_MOVE) {
                    if (switchSheet.move(x, y, kit)) {
                        invalidate();
                    }
                } else if (e.getAction() == MotionEvent.ACTION_UP) {
                    final int answer = switchSheet.up(x, y, kit);
                    if (answer == SwitchSheet.DELETE) {
                        askDeleteProfile();
                    } else if (answer == SwitchSheet.SWITCH) {
                        final Profiles.Profile to = switchSheet.chosen();
                        switchSheet = null;
                        if (to != null && !to.id.equals(profile.id)) {
                            Profiles.setActive(HomeActivity.this, to);
                            profile = to;
                            profileChanged();
                        }
                    } else if (answer == SwitchSheet.CANCEL) {
                        switchSheet = null;
                    }
                    invalidate();
                }
                return true;
            }
            if (galleryOpen) {
                if (e.getAction() == MotionEvent.ACTION_UP) {
                    final int answer = gallery.up(x, y, kit);
                    if (answer != GallerySheet.NONE) {
                        galleryOpen = false;
                    }
                    if (answer == GallerySheet.OK && profile != null && profile.portrait != gallery.slot()) {
                        profile.portrait = gallery.slot();
                        Profiles.update(HomeActivity.this, profile);
                        portrait = Portraits.tinted(kit, profile.portrait, Profiles.COLORS[profile.color]);
                    }
                    invalidate();
                }
                return true;
            }
            if (profileOpen) {
                if (e.getAction() == MotionEvent.ACTION_UP) {
                    final int answer = profileSheet.up(x, y, kit);
                    if (answer == ProfileSheet.PORTRAIT && profile != null) {
                        galleryOpen = true;
                        gallery.open(kit, profile.color, profile.portrait, new Runnable() {
                            @Override
                            public void run() {
                                postInvalidate();
                            }
                        });
                    } else if (answer == ProfileSheet.SWITCH) {
                        openSwitchSheet();
                    } else if (answer == ProfileSheet.NEW || answer == ProfileSheet.RENAME) {
                        openNameSheet(answer == ProfileSheet.NEW);
                    } else if (answer == ProfileSheet.CLOSE) {
                        profileOpen = false;
                    } else if (answer == ProfileSheet.COLOR && profile != null
                               && profile.color != profileSheet.pickedColor) {
                        profile.color = profileSheet.pickedColor;
                        Profiles.update(HomeActivity.this, profile);
                        // The chip's portrait follows the colour.
                        portrait = Portraits.tinted(kit, profile.portrait, Profiles.COLORS[profile.color]);
                    }
                    invalidate();
                }
                return true;
            }
            if (page == ENTRY_MULTIPLAYER && lan.touch(e.getAction(), x, y, kit)) {
                invalidate();
                return true;
            }
            if (e.getAction() == MotionEvent.ACTION_UP && profile != null
                && in(x, y, Screen.CANVAS_W - CHIP_RIGHT - CHIP_W, CHIP_Y, CHIP_W, CHIP_H)
                && in(downX, downY, Screen.CANVAS_W - CHIP_RIGHT - CHIP_W, CHIP_Y, CHIP_W, CHIP_H)) {
                profileOpen = true;
                invalidate();
                return true;
            }
            switch (e.getAction()) {
                case MotionEvent.ACTION_DOWN:
                    downX = x;
                    downY = y;
                    downScroll = listScroll;
                    dragging = false;
                    inList = page == ENTRY_SCENARIO
                        && in(x, y, CONTENT_X + 24f, listTop(), LIST_ROW_W, listBottom() - listTop());
                    removeCallbacks(held);
                    if (inList) {
                        postDelayed(held, 500L);
                    }
                    pressed = entryAt(x, y);
                    if (page == ENTRY_CAMPAIGN && inMissions) {
                        missions.down(x, y, kit);
                    } else if (page == ENTRY_LOAD) {
                        load.down(x, y, kit);
                    } else if (page == ENTRY_OPTIONS && x >= RAIL_W) {
                        options.down(x, y);
                    }
                    invalidate();
                    break;
                case MotionEvent.ACTION_MOVE:
                    if (page == ENTRY_CAMPAIGN && inMissions && x >= RAIL_W) {
                        if (missions.move(x, y, kit)) {
                            invalidate();
                        }
                    } else if (page == ENTRY_LOAD && x >= RAIL_W) {
                        if (load.move(x, y, kit)) {
                            invalidate();
                        }
                    } else if (page == ENTRY_OPTIONS && x >= RAIL_W) {
                        if (options.move(x, y)) {
                            invalidate();
                        }
                    } else if (inList) {
                        if (Math.abs(y - downY) > 16f) {
                            dragging = true;
                            removeCallbacks(held);
                        }
                        if (dragging) {
                            listScroll = Math.max(0f, Math.min(maxScroll(), downScroll - (y - downY)));
                            invalidate();
                        }
                    } else {
                        final int at = entryAt(x, y);
                        if (at != pressed) {
                            pressed = at;
                            invalidate();
                        }
                    }
                    break;
                case MotionEvent.ACTION_UP:
                    removeCallbacks(held);
                    if (x < RAIL_W) {
                        // The game-files row is a line of text only (owner, 2026-10-06): the folder
                        // is reached through Options, Game Files.
                        if (in(x, y, ROW_X, musicY(), ROW_W, FOOT_H)) {
                            music = !music;
                            prefs().edit().putBoolean(PREF_MUSIC, music).apply();
                            menuMusic.setEnabled(music);
                            menuAudio.setEnabled(music);
                            menuAudio.play(MenuAudio.BUTTON);
                        } else if (pressed >= 0 && entryAt(x, y) == pressed
                                   && pressed != ENTRY_SCENARIO && pressed != ENTRY_CAMPAIGN
                                   && pressed != ENTRY_OPTIONS && pressed != ENTRY_LOAD
                                   && !(pressed == ENTRY_MULTIPLAYER && lanPreview())) {
                            // A section that is not built yet.
                            menuAudio.play(MenuAudio.ERROR);
                        } else if (pressed == ENTRY_MULTIPLAYER && entryAt(x, y) == ENTRY_MULTIPLAYER) {
                            menuAudio.play(MenuAudio.BUTTON);
                            page = ENTRY_MULTIPLAYER;
                            lan.open(HomeActivity.this, HomeView.this, menuAudio, profile);
                        } else if (pressed == ENTRY_SCENARIO && entryAt(x, y) == ENTRY_SCENARIO) {
                            menuAudio.play(MenuAudio.BUTTON);
                            if (page != ENTRY_SCENARIO) {
                                page = ENTRY_SCENARIO;
                                openScenario();
                            }
                        } else if (pressed == ENTRY_CAMPAIGN && entryAt(x, y) == ENTRY_CAMPAIGN) {
                            menuAudio.play(MenuAudio.BUTTON);
                            page = ENTRY_CAMPAIGN;
                            inMissions = false;
                        } else if (pressed == ENTRY_OPTIONS && entryAt(x, y) == ENTRY_OPTIONS) {
                            menuAudio.play(MenuAudio.BUTTON);
                            page = ENTRY_OPTIONS;
                        } else if (pressed == ENTRY_LOAD && entryAt(x, y) == ENTRY_LOAD) {
                            menuAudio.play(MenuAudio.BUTTON);
                            page = ENTRY_LOAD;
                            load.refresh(HomeActivity.this, profile);
                        }
                        // The other sections are not built yet; a tap only shows its feedback.
                    } else if (page == ENTRY_SCENARIO && !dragging) {
                        tapScenario(x, y);
                    } else if (page == ENTRY_OPTIONS) {
                        if (options.up(x, y)) {
                            menuAudio.play(MenuAudio.BUTTON);
                        }
                        if (options.takeLicencesAsked()) {
                            licences = new LicencesSheet(HomeActivity.this);
                        }
                        final String tongue = options.takeLanguageAsked();
                        if (tongue != null) {
                            // Every text and the face come with the language: the shell starts
                            // over and comes back on this tab.
                            SkinKit.chooseLanguage(HomeActivity.this, tongue);
                            prefs().edit().putBoolean(PREF_REOPEN_LANGUAGES, true).apply();
                            recreate();
                            return true;
                        }
                        if (options.takeFolderAsked()) {
                            folderVisit = true;
                            folderBefore = GateActivity.rememberedFolder(HomeActivity.this);
                            startActivity(new android.content.Intent(HomeActivity.this, GateActivity.class)
                                .putExtra(GateActivity.EXTRA_RETURN, true));
                        }
                    } else if (page == ENTRY_LOAD) {
                        final int action = load.up(x, y, kit);
                        if (action == LoadPage.ACTION_LOAD) {
                            startSave();
                        } else if (action == LoadPage.ACTION_DELETE) {
                            askDeleteSave();
                        }
                    } else if (page == ENTRY_CAMPAIGN && inMissions) {
                        final int action = missions.up(x, y, kit);
                        if (action == MissionsPage.ACTION_PREVIOUS) {
                            inMissions = false;
                        } else if (action == MissionsPage.ACTION_START) {
                            startMission();
                        }
                    } else if (page == ENTRY_CAMPAIGN) {
                        final CampaignPage.Campaign card = campaign.cardAt(x, y, kit);
                        if (card != null) {
                            missions.open(card);
                            inMissions = true;
                        }
                    }
                    pressed = -1;
                    inList = false;
                    dragging = false;
                    invalidate();
                    break;
                case MotionEvent.ACTION_CANCEL:
                    pressed = -1;
                    inList = false;
                    dragging = false;
                    invalidate();
                    break;
                default:
                    break;
            }
            return true;
        }
    }
}
