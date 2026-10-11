package net.wargushd.game;

import android.content.Context;
import android.graphics.Canvas;

import java.io.File;

/**
 * The host's Change... in a room: the map list of the Create segment and the five settings on a
 * sheet over the room. Apply gives the room the map and the settings; Cancel changes nothing.
 */
final class RoomMapSheet {
    static final int NONE = 0, CANCEL = 1, APPLY = 2;

    // Layout, logical units (design 38.10, L7).
    private static final float W = 3120f, H = 1560f, PAD = 96f;
    private static final float RIGHT_DX = 1584f, RIGHT_W = 1440f, NAME_DY = 120f, META_DY = 198f;
    private static final float SET_DY = 300f, SET_PITCH = 138f, SET_H = 108f, VALUE_DX = 2304f, VALUE_W = 720f;
    private static final float NOTE_DY = 1020f, NOTE2_DY = 1082f;
    private static final float BUTTON_W = 696f, BUTTON_H = 120f, BUTTON_DY = 1344f, APPLY_DX = 2328f;

    final MapList maps = new MapList();
    final LanSettings settings = new LanSettings();

    /** Open on the room's map and settings; the list shows the source that holds the map. */
    RoomMapSheet(Context c, String mapId, LanSettings now) {
        settings.resources = now.resources;
        settings.units = now.units;
        settings.tileset = now.tileset;
        settings.fog = now.fog;
        settings.reveal = now.reveal;
        maps.source = 0;
        maps.load(c);
        for (int i = 0; i < maps.maps.size(); ++i) {
            if (MapList.path(c, maps.maps.get(i)).equals(mapId)) {
                maps.selected = i;
            }
        }
    }

    File chosen() {
        return maps.chosen();
    }

    private float x() { return (Screen.CANVAS_W - W) / 2f; }
    private float y(SkinKit kit) { return kit.height / 2f - H / 2f; }
    private float listBottom(SkinKit kit) { return y(kit) + H - PAD; }
    private float setY(SkinKit kit, int i) { return y(kit) + SET_DY + SET_PITCH * i; }

    void draw(Canvas c, SkinKit kit) {
        final float x = x(), y = y(kit), rx = x + RIGHT_DX;
        LanKit.sheet(c, kit, x, y, W, H);
        maps.draw(c, kit, x + PAD, y + PAD, listBottom(kit));

        final File map = maps.chosen();
        if (map != null) {
            kit.textTop(c, kit.fit(MapList.name(map), LanKit.CAP_BODY, RIGHT_W), rx, y + NAME_DY,
                        LanKit.CAP_BODY, LanKit.GOLD, true);
            kit.textTop(c, MapList.facts(kit, maps.info.get(maps.selected), -1), rx, y + META_DY,
                        LanKit.CAP_MID, LanKit.INK_MUTED, true);
        }
        for (int i = 0; i < LanSettings.ROWS; ++i) {
            kit.textMiddle(c, LanSettings.label(kit, i), rx, setY(kit, i), SET_H, LanKit.CAP_BODY, LanKit.INK, true);
            LanKit.value(c, kit, settings.value(kit, i), x + VALUE_DX, setY(kit, i), VALUE_W, SET_H,
                         LanKit.CAP_BODY);
        }
        kit.textTop(c, kit.own("A new map clears every Ready."), rx, y + NOTE_DY, LanKit.CAP_MID,
                    LanKit.INK_MUTED, true);
        kit.textTop(c, kit.own("Players without the map receive it."), rx, y + NOTE2_DY, LanKit.CAP_MID,
                    LanKit.INK_MUTED, true);

        final float by = y + BUTTON_DY;
        LanKit.button(c, kit, kit.string("common_cancel", "Cancel"), rx, by, BUTTON_W, BUTTON_H,
                      LanKit.CAP_BODY, LanKit.PLAIN);
        LanKit.button(c, kit, kit.own("Apply"), x + APPLY_DX, by, BUTTON_W, BUTTON_H, LanKit.CAP_BODY,
                      map != null ? LanKit.PRIMARY : LanKit.DISABLED);
    }

    void down(float px, float py, SkinKit kit) {
        maps.down(px, py, x() + PAD, y(kit) + PAD, listBottom(kit));
    }

    boolean move(float py, SkinKit kit) {
        return maps.move(py, y(kit) + PAD, listBottom(kit));
    }

    /** A finger was lifted at (x, y), in logical units. */
    int up(Context c, float px, float py, SkinKit kit) {
        final float x = x(), y = y(kit), by = y + BUTTON_DY;
        if (maps.up(c, px, py, x + PAD, y + PAD, listBottom(kit))) {
            return NONE;
        }
        if (LanKit.in(px, py, x + RIGHT_DX, by, BUTTON_W, BUTTON_H)) {
            return CANCEL;
        }
        if (LanKit.in(px, py, x + APPLY_DX, by, BUTTON_W, BUTTON_H)) {
            return (maps.chosen() != null) ? APPLY : NONE;
        }
        for (int i = 0; i < LanSettings.ROWS; ++i) {
            if (LanKit.in(px, py, x + VALUE_DX, setY(kit, i), VALUE_W, SET_H)) {
                settings.next(i);
            }
        }
        return NONE;
    }
}
