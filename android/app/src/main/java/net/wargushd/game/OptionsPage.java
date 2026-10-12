package net.wargushd.game;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;

import java.io.File;

/**
 * The Options page: a strip of tabs and, under it, rows of a caption and a control.
 *
 * The settings are the game's own preferences, kept once for the device (not per profile).
 * A value that was never set here is the game's default. (The preferences file the game
 * writes for a profile is not read back: it can hold values the game changed for itself
 * for a while, such as tips switched off during a campaign mission.)
 */
final class OptionsPage {
    private static final int SLIDER = 0, SWITCH = 1, CHOICE = 2;

    /** One setting: the game's preference it stands for and how it is shown. */
    static final class Setting {
        final String pref, captionKey, caption;
        final int kind, min, max, fallback;
        /** For a choice: string key and fallback of each option, in the order of the stored index. */
        final String[][] options;
        /** For a choice over a yes/no preference: the first option means yes. */
        final boolean firstIsTrue;
        int value;

        private Setting(String pref, String captionKey, String caption, int kind, int min, int max,
                        int fallback, String[][] options, boolean firstIsTrue) {
            this.pref = pref;
            this.captionKey = captionKey;
            this.caption = caption;
            this.kind = kind;
            this.min = min;
            this.max = max;
            this.fallback = fallback;
            this.options = options;
            this.firstIsTrue = firstIsTrue;
        }

        static Setting slider(String pref, String key, String caption, int min, int max, int fallback) {
            return new Setting(pref, key, caption, SLIDER, min, max, fallback, null, false);
        }

        static Setting toggle(String pref, String key, String caption, boolean fallback) {
            return new Setting(pref, key, caption, SWITCH, 0, 1, fallback ? 1 : 0, null, false);
        }

        static Setting choice(String pref, String key, String caption, int fallback, boolean firstIsTrue,
                              String[]... options) {
            return new Setting(pref, key, caption, CHOICE, 0, options.length - 1, fallback, options,
                               firstIsTrue);
        }
    }

    private static final String STORE = "options";

    /** Tabs in the order of the strip: string key (null = the launcher's own word), fallback. */
    private static final String[][] TABS = {
        { "option_sound_title", "Sound" }, { "option_speed_title", "Speeds" },
        { "option_preference_title", "Preferences" }, { "option_gameplay_title", "Gameplay" },
        { "option_preference_languages", "Languages" }, { null, "Game Files" }, { null, "About" },
    };

    private final Setting[][] pages = {
        {
            Setting.slider("MusicVolume", "option_music_volume", "Music Volume", 0, 255, 128),
            Setting.slider("EffectsVolume", "option_sfx_volume", "SFX Volume", 0, 255, 128),
            Setting.toggle("UnitSpeech", "option_sound_unit_speech", "Unit Speech", true),
            Setting.toggle("UnitAck", "option_sound_unit_acknowledgement", "Unit Acknowledgements", true),
            Setting.toggle("BuildingSound", "option_sound_building_sound", "Building Sound", true),
            Setting.toggle("Subtitles", "option_sound_subtitles", "Subtitles", false),
            Setting.toggle("StereoSound", "option_sound_stereo_sfx", "Stereo SFX", true),
        },
        {
            Setting.slider("GameSpeed", "option_speed_game_speed", "Game Speed", 0, 100, 75),
            Setting.slider("MouseScrollSpeed", "option_speed_mouse_scroll", "Mouse Scroll", 1, 10, 1),
            Setting.slider("KeyScrollSpeed", "option_speed_key_scroll", "Key Scroll", 1, 10, 4),
        },
        {
            Setting.choice("MinimapWithTerrain", "option_preference_minimap_mode", "Minimap Mode", 0, true,
                           new String[] { "option_preference_minimap_terrain_on", "Terrain Info On" },
                           new String[] { "option_preference_minimap_terrain_off", "Terrain Info Off" }),
            Setting.toggle("FogOfWar", "option_preference_fog_of_war", "Fog of War", true),
            Setting.toggle("ShowTips", "option_preference_tips", "Tips", true),
            Setting.toggle("PlayIntro", "option_preference_intro", "Play Introduction", false),
            Setting.toggle("HideObjectivesHud", "option_preference_hide_objectives",
                           "Hide Objectives HUD Display", false),
        },
        {
            Setting.toggle("ShowCommandKey", "option_gameplay_show_button_hotkeys", "Show Button Hotkeys",
                           true),
            // Default 2 = "Only When Not Full": the original's own default (its Warcraft2.ini holds
            // show_healthbars = 1 with healthbars_only_when_damaged = 1; scripts/ui.lua sets the
            // same). This value goes into every launch request and replaces the game's default.
            Setting.choice("HealthBarMode", "option_gameplay_healthbar_show", "Show Healthbars", 2, false,
                           new String[] { "option_gameplay_healthbar_show_always", "Always" },
                           new String[] { "option_gameplay_healthbar_show_never", "Never" },
                           new String[] { "option_gameplay_healthbar_show_not_full", "Only When Not Full" }),
            Setting.choice("HealthBarLocation", "option_gameplay_healthbar_location", "Healthbar Location",
                           0, false,
                           new String[] { "option_gameplay_healthbar_location_above", "Above" },
                           new String[] { "option_gameplay_healthbar_location_below", "Below" }),
            Setting.choice("ResourceBarAlign", "option_resourcebar_align_title", "Resource Bar Alignment",
                           1, false,
                           new String[] { "option_resourcebar_align_left", "Left" },
                           new String[] { "option_resourcebar_align_center", "Center" },
                           new String[] { "option_resourcebar_align_right", "Right" }),
            Setting.choice("PortraitIconScaled", "option_portrait_style_title", "Portrait Icon Style", 1,
                           true,
                           new String[] { "option_portrait_style_scaled", "Scaled" },
                           new String[] { "option_portrait_style_cropped", "Cropped" }),
        },
        {}, {}, {},
    };

    // Layout, logical units.
    private static final float X0 = 816f, RIGHT = 3744f, TOP = 264f, STRIP_H = 108f;
    private static final float PAGE_Y = TOP + STRIP_H + 48f, ROW_PITCH = 156f, ROW_H = 108f;
    private static final float CONTROL_W = 1200f, SWITCH_W = 168f, SWITCH_H = 96f;
    private static final float CAP_TITLE = 60f, CAP_BODY = 44f, CAP_MID = 36f;

    private static final int PANEL_FILL = 0xDE140F0A, LINE = 0xFF92743E, WELL = 0x78000000;
    private static final int INK = 0xFFE8E2D4, GOLD = 0xFFF0D250, INK_ON_GOLD = 0xFF1E1608;
    private static final int INK_TITLE = 0xFF000000, PART_FAINT = 0x0EFFFFFF, TRACK = 0xFF50463A;
    private static final int KNOB = 0xFFECE6D6, SWITCH_ON = 0xFF78B060, SWITCH_OFF = 0xFF463E34;

    private int tab;

    /** Open on a tab (0 Sound, 4 Languages, 5 Game Files, 6 About). */
    void showTab(int which) {
        tab = Math.max(0, Math.min(TABS.length - 1, which));
    }

    // The Languages tab (owner's rule): one row to a language, the one in use marked.
    private static final int LANGUAGE_TAB = 4;
    private static final float LANG_PITCH = 138f, LANG_H = 120f;
    private static final int ROW_SELECTED = 0x36F0D250, ROW_FAINT = 0x10FFFFFF;
    private String languageInUse = "enUS", languageAsked;

    /** The language a tap has just chosen, once; null when none was. */
    String takeLanguageAsked() {
        final String asked = languageAsked;
        languageAsked = null;
        return asked;
    }

    private void drawLanguages(Canvas c, SkinKit kit) {
        final float x = X0 + 60f, w = RIGHT - X0 - 120f;
        for (int i = 0; i < SkinKit.LANGUAGES.length; ++i) {
            final float y = PAGE_Y + 48f + LANG_PITCH * i;
            final boolean inUse = SkinKit.LANGUAGES[i][0].equals(languageInUse);
            kit.fill(c, x, y, w, LANG_H, inUse ? ROW_SELECTED : ROW_FAINT, 10f);
            if (inUse) {
                kit.fill(c, x, y, 10f, LANG_H, GOLD, 0f);
                kit.textMiddleRight(c, kit.own("In use"), x + w - 36f, y, LANG_H, CAP_SMALL, INK_MUTED, true);
            }
            kit.textMiddle(c, kit.string(SkinKit.LANGUAGES[i][1], SkinKit.LANGUAGES[i][0]), x + 36f, y,
                           LANG_H, CAP_BODY, inUse ? GOLD : INK, true);
        }
    }
    private SharedPreferences store;
    /** The slider being dragged, or null. */
    private Setting dragged;

    /** Take every setting's value: the device's own, else the game's default. */
    void read(Context c, Profiles.Profile profile) {
        store = c.getSharedPreferences(STORE, Context.MODE_PRIVATE);
        languageInUse = SkinKit.language(c);
        readAbout(c);
        // The Game Speed setting changed its scale (a 15..75 number, default 30, became the knob
        // position 0..100, default 75): a value stored on the old scale is dropped once.
        if (store.getInt("GameSpeedScale", 0) != 2) {
            store.edit().remove("GameSpeed").putInt("GameSpeedScale", 2).apply();
        }
        for (Setting[] page : pages) {
            for (Setting s : page) {
                s.value = store.getInt(s.pref, s.fallback);
                s.value = Math.max(s.min, Math.min(s.max, s.value));
            }
        }
    }

    /** The About tab's two lines that are facts of this installation. */
    private void readAbout(Context c) {
        aboutVersion = "WargusHD";
        aboutDate = "";
        try {
            final android.content.pm.PackageInfo pi =
                c.getPackageManager().getPackageInfo(c.getPackageName(), 0);
            aboutVersion = "WargusHD " + pi.versionName;
            // The unpacked content belongs to the installed package: its date is the install's.
            aboutDate = new java.text.SimpleDateFormat("yyyy-MM-dd", java.util.Locale.US)
                .format(new java.util.Date(pi.lastUpdateTime));
        } catch (Exception e) {
            // The name alone.
        }
        aboutFolder = GateActivity.rememberedFolder(c);
        builtInSource = store == null || store.getBoolean(KEY_BUILT_IN, true);
    }

    /** The Game Files tab: two facts of this installation and the mods-source override. */
    private static final int SOURCE_TAB = 5;
    private static final String KEY_BUILT_IN = "mods_source_built_in";
    private static final float VALUE_W = 1500f;
    /** The game folder (null when none is chosen) and the installed content's date. */
    private String aboutFolder, aboutDate = "";

    private String sourceFolder(SkinKit kit) {
        return (aboutFolder != null) ? aboutFolder : kit.own("Not chosen");
    }

    /** There is no record of the art's preparation yet; the unpacked content's date stands in. */
    private String sourceArt(SkinKit kit) {
        return aboutDate.isEmpty() ? kit.own("Prepared") : kit.own("Prepared") + " " + aboutDate;
    }

    private String aboutFiles(SkinKit kit) {
        return kit.own("Game files: {0}", aboutFolder != null ? aboutFolder : kit.own("not chosen"))
            + (aboutDate.isEmpty() ? "" : " - " + kit.own("content {0}", aboutDate));
    }
    private boolean builtInSource = true;
    private boolean folderAsked;
    private boolean licencesAsked;

    /** True once after a tap on the About tab's Licences button. */
    boolean takeLicencesAsked() {
        final boolean asked = licencesAsked;
        licencesAsked = false;
        return asked;
    }

    private float pageHeightOf(float screenHeight) { return screenHeight - 264f + 120f - PAGE_Y; }

    /** True once after a tap on the game folder's box: the folder is the gate's business. */
    boolean takeFolderAsked() {
        final boolean asked = folderAsked;
        folderAsked = false;
        return asked;
    }

    private void valueBox(Canvas c, SkinKit kit, float y, String value) {
        final float x = controlRight() - VALUE_W;
        kit.fill(c, x, y, VALUE_W, ROW_H, WELL, 14f);
        kit.outline(c, x, y, VALUE_W, ROW_H, LINE, 14f);
        kit.textMiddle(c, kit.fit(value, CAP_BODY, VALUE_W - 150f), x + 36f, y, ROW_H, CAP_BODY, INK, true);
        kit.textCentre(c, "v", x + VALUE_W - 96f, y, 60f, ROW_H, CAP_SMALL, GOLD, true);
    }

    private float sourceSwitchY() { return rowY(2) + 60f + 96f; }

    private void drawSource(Canvas c, SkinKit kit) {
        final float x = X0 + 60f, rx = controlRight();
        kit.textMiddle(c, kit.own("Game folder"), x, rowY(0), ROW_H, CAP_BODY, INK, true);
        valueBox(c, kit, rowY(0), sourceFolder(kit));
        kit.textMiddle(c, kit.own("Game art"), x, rowY(1), ROW_H, CAP_BODY, INK, true);
        valueBox(c, kit, rowY(1), sourceArt(kit));

        kit.textTop(c, kit.own("Mods source (advanced)"), x, rowY(2) + 60f, CAP_MID, GOLD, true);
        final float y = sourceSwitchY();
        kit.textMiddle(c, kit.own("Use the built-in source"), x, y, ROW_H, CAP_BODY, INK, true);
        final float sx = rx - SWITCH_W, sy = y + 6f;
        kit.fill(c, sx, sy, SWITCH_W, SWITCH_H, builtInSource ? SWITCH_ON : SWITCH_OFF, 48f);
        kit.outline(c, sx, sy, SWITCH_W, SWITCH_H, LINE, 48f);
        kit.fill(c, sx + (builtInSource ? 78f : 6f), sy + 6f, 84f, 84f, KNOB, 42f);
        if (builtInSource) {
            return;
        }
        // The override's fields: shown, not yet editable (the mod list comes with a later phase).
        kit.textMiddle(c, kit.own("Address"), x, y + ROW_PITCH, ROW_H, CAP_BODY, INK, true);
        valueBox(c, kit, y + ROW_PITCH, "");
        kit.textMiddle(c, kit.own("Key"), x, y + ROW_PITCH * 2f, ROW_H, CAP_BODY, INK, true);
        valueBox(c, kit, y + ROW_PITCH * 2f, "");
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
        }
        final float by = y + ROW_PITCH * 3f + 12f;
        kit.nine(c, buttonPlain, rx - 600f, by, 600f, 120f, 14);
        kit.textCentre(c, kit.own("Check Source"), rx - 600f, by, 600f, 120f, CAP_BODY, INK, true);
    }

    /** The About tab: kind ('h' heading, 't' text, 'm' small muted line) and the line. */
    private static final String[][] ABOUT = {
        { "h", null }, { "m", null },
        { "h", "The game" },
        { "t", "Warcraft II and Warcraft II: Remastered are the work of Blizzard Entertainment." },
        { "t", "This app contains no game files. You supply your own copy of the game." },
        { "h", "The engine and the scripts" },
        { "t", "The game runs on the Stratagus engine, the work of the Stratagus team and its contributors." },
        { "t", "Its game scripts grew out of Wargus, the work of the Wargus team and its contributors." },
        { "t", "Without all of this work, this app would not be possible." },
        { "m", "Stratagus and Wargus are free software under the GNU General Public License, version 2."
               + " Other libraries: see Licences." },
        { "m", "WargusHD is an independent fan project. It is not affiliated with, endorsed by or sponsored by" },
        { "m", "Blizzard Entertainment, the Stratagus team or the Wargus team." },
        { "m", "Warcraft and Blizzard Entertainment are trademarks or registered trademarks of Blizzard"
               + " Entertainment, Inc." },
    };
    private static final int ABOUT_TAB = 6;
    private static final float CAP_SMALL = 30f;
    private static final int INK_MUTED = 0xFFCEC6B6;

    private String aboutVersion = "WargusHD";
    private android.graphics.Bitmap buttonPlain;

    private void drawAbout(Canvas c, SkinKit kit) {
        final float x = X0 + 72f, w = RIGHT - X0 - 144f;
        float y = PAGE_Y + 66f;
        for (int i = 0; i < ABOUT.length; ++i) {
            final String kind = ABOUT[i][0];
            final String line = (i == 0) ? aboutVersion : (i == 1) ? aboutFiles(kit) : kit.own(ABOUT[i][1]);
            if (kind.equals("h")) {
                y += (i > 0) ? 42f : 0f;
                kit.textTop(c, kit.fit(line, CAP_BODY, w), x, y, CAP_BODY, GOLD, true);
                y += 90f;
            } else if (kind.equals("t")) {
                kit.textTop(c, kit.fit(line, CAP_MID, w), x, y, CAP_MID, INK, true);
                y += 72f;
            } else {
                if (i > 0 && ABOUT[i - 1][0].equals("t")) {
                    y += 48f;
                }
                kit.textTop(c, kit.fit(line, CAP_SMALL, w), x, y, CAP_SMALL, INK_MUTED, true);
                y += 60f;
            }
        }
        if (buttonPlain == null) {
            buttonPlain = kit.atlasFrame("Modern_Widgets_Orc", "large", 1008, 126);
        }
        final float bx = RIGHT - 72f - 600f, by = PAGE_Y + pageHeight(kit) - 48f - 120f;
        kit.nine(c, buttonPlain, bx, by, 600f, 120f, 14);
        kit.textCentre(c, kit.own("Licences"), bx, by, 600f, 120f, CAP_BODY, INK, true);
    }

    /** The settings as the block of a launch request: the game's preference names and values. */
    String requestBlock() {
        final StringBuilder b = new StringBuilder("  settings = {\n");
        for (Setting[] page : pages) {
            for (Setting s : page) {
                b.append("    ").append(s.pref).append(" = ");
                if (s.kind == SWITCH) {
                    b.append(s.value != 0);
                } else if (s.kind == CHOICE && s.firstIsTrue) {
                    b.append(s.value == 0);
                } else {
                    b.append(s.value);
                }
                b.append(",\n");
            }
        }
        b.append("    GameSpeedScale = 2,\n");
        return b.append("  },\n").toString();
    }

    private void keep(Setting s) {
        store.edit().putInt(s.pref, s.value).apply();
    }

    private float pageHeight(SkinKit kit) { return kit.height - 264f + 120f - PAGE_Y; }
    private float rowY(int i) { return PAGE_Y + 48f + ROW_PITCH * i; }
    private float controlRight() { return RIGHT - 60f; }

    private void parts(Canvas c, SkinKit kit, float x, float y, float w, float h, String[] words,
                       int selected) {
        kit.fill(c, x, y, w, h, WELL, 14f);
        kit.outline(c, x, y, w, h, LINE, 14f);
        final float pw = w / words.length;
        for (int i = 0; i < words.length; ++i) {
            final float px = x + pw * i + 6f;
            final boolean on = (i == selected);
            kit.fill(c, px, y + 6f, pw - 12f, h - 12f, on ? GOLD : PART_FAINT, 10f);
            kit.textCentre(c, kit.fit(words[i], CAP_MID, pw - 36f), px, y + 6f, pw - 12f, h - 12f, CAP_MID,
                           on ? INK_ON_GOLD : INK, !on);
        }
    }

    private String[] words(SkinKit kit, String[][] keyed) {
        final String[] w = new String[keyed.length];
        for (int i = 0; i < keyed.length; ++i) {
            w[i] = kit.string(keyed[i][0], keyed[i][1]);
        }
        return w;
    }

    /** The screen's height in units, as the last draw saw it (the About button hangs from it). */
    private float screenHeight = 2400f;

    void draw(Canvas c, SkinKit kit) {
        screenHeight = kit.height;
        kit.textTop(c, kit.string("options", "Options"), X0, 84f, CAP_TITLE, INK_TITLE, false);
        parts(c, kit, X0, TOP, RIGHT - X0, STRIP_H, words(kit, TABS), tab);

        kit.fill(c, X0, PAGE_Y, RIGHT - X0, pageHeight(kit), PANEL_FILL, 18f);
        kit.outline(c, X0, PAGE_Y, RIGHT - X0, pageHeight(kit), LINE, 18f);

        if (tab == ABOUT_TAB) {
            drawAbout(c, kit);
            return;
        }
        if (tab == SOURCE_TAB) {
            drawSource(c, kit);
            return;
        }
        if (tab == LANGUAGE_TAB) {
            drawLanguages(c, kit);
            return;
        }
        final Setting[] rows = pages[tab];
        final float rx = controlRight();
        for (int i = 0; i < rows.length; ++i) {
            final Setting s = rows[i];
            final float y = rowY(i);
            kit.textMiddle(c, kit.string(s.captionKey, s.caption), X0 + 60f, y, ROW_H, CAP_BODY, INK, true);
            if (s.kind == SLIDER) {
                final float x = rx - CONTROL_W, sy = y + 6f;
                final float part = (s.value - s.min) / (float) (s.max - s.min);
                kit.fill(c, x, sy + 42f, CONTROL_W, 12f, TRACK, 6f);
                if (part > 0f) {
                    kit.fill(c, x, sy + 42f, CONTROL_W * part, 12f, GOLD, 6f);
                }
                kit.fill(c, x + CONTROL_W * part - 36f, sy + 12f, 72f, 72f, KNOB, 36f);
            } else if (s.kind == CHOICE) {
                parts(c, kit, rx - CONTROL_W, y, CONTROL_W, ROW_H, words(kit, s.options), s.value);
            } else {
                final float x = rx - SWITCH_W, sy = y + 6f;
                final boolean on = (s.value != 0);
                kit.fill(c, x, sy, SWITCH_W, SWITCH_H, on ? SWITCH_ON : SWITCH_OFF, 48f);
                kit.outline(c, x, sy, SWITCH_W, SWITCH_H, LINE, 48f);
                kit.fill(c, x + (on ? 78f : 6f), sy + 6f, 84f, 84f, KNOB, 42f);
            }
        }
    }

    private static boolean in(float x, float y, float rx, float ry, float rw, float rh) {
        return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
    }

    private void slide(Setting s, float x) {
        final float part = Math.max(0f, Math.min(1f, (x - (controlRight() - CONTROL_W)) / CONTROL_W));
        s.value = s.min + Math.round(part * (s.max - s.min));
    }

    /** @return true when the page must be redrawn */
    boolean down(float x, float y) {
        dragged = null;
        final Setting[] rows = pages[tab];
        for (int i = 0; i < rows.length; ++i) {
            // The slider answers in its whole touch band, a knob's half beyond each end.
            if (rows[i].kind == SLIDER
                && in(x, y, controlRight() - CONTROL_W - 36f, rowY(i) + 6f, CONTROL_W + 72f, 96f)) {
                dragged = rows[i];
                slide(dragged, x);
                return true;
            }
        }
        return false;
    }

    boolean move(float x, float y) {
        if (dragged == null) {
            return false;
        }
        slide(dragged, x);
        return true;
    }

    boolean up(float x, float y) {
        if (dragged != null) {
            keep(dragged);
            dragged = null;
            return true;
        }
        if (in(x, y, X0, TOP, RIGHT - X0, STRIP_H)) {
            tab = Math.min(TABS.length - 1, (int) ((x - X0) / ((RIGHT - X0) / TABS.length)));
            return true;
        }
        final Setting[] rows = pages[tab];
        final float rx = controlRight();
        if (tab == SOURCE_TAB && in(x, y, rx - VALUE_W, rowY(0), VALUE_W, ROW_H)) {
            folderAsked = true;
            return true;
        }
        if (tab == ABOUT_TAB && in(x, y, RIGHT - 72f - 600f,
                                   PAGE_Y + pageHeightOf(screenHeight) - 48f - 120f, 600f, 120f)) {
            licencesAsked = true;
            return true;
        }
        if (tab == LANGUAGE_TAB && in(x, y, X0 + 60f, PAGE_Y + 48f, RIGHT - X0 - 120f,
                                      LANG_PITCH * SkinKit.LANGUAGES.length)) {
            final int row = (int) ((y - PAGE_Y - 48f) / LANG_PITCH);
            if (row >= 0 && row < SkinKit.LANGUAGES.length
                && y - PAGE_Y - 48f - row * LANG_PITCH < LANG_H
                && !SkinKit.LANGUAGES[row][0].equals(languageInUse)) {
                languageAsked = SkinKit.LANGUAGES[row][0];
                return true;
            }
            return false;
        }
        if (tab == SOURCE_TAB && store != null
            && in(x, y, rx - SWITCH_W - 24f, sourceSwitchY(), SWITCH_W + 48f, ROW_H)) {
            builtInSource = !builtInSource;
            store.edit().putBoolean(KEY_BUILT_IN, builtInSource).apply();
            return true;
        }
        for (int i = 0; i < rows.length; ++i) {
            final Setting s = rows[i];
            final float ry = rowY(i);
            if (s.kind == SWITCH && in(x, y, rx - SWITCH_W - 24f, ry, SWITCH_W + 48f, ROW_H)) {
                s.value = (s.value != 0) ? 0 : 1;
                keep(s);
                return true;
            }
            if (s.kind == CHOICE && in(x, y, rx - CONTROL_W, ry, CONTROL_W, ROW_H)) {
                s.value = Math.min(s.max, (int) ((x - (rx - CONTROL_W)) / (CONTROL_W / s.options.length)));
                keep(s);
                return true;
            }
        }
        return false;
    }
}
