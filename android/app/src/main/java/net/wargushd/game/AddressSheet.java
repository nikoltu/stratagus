package net.wargushd.game;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;

import java.util.ArrayList;
import java.util.List;

/**
 * Join by address: a sheet with the address field, the addresses used before and its own
 * twelve-key pad. No system keyboard is opened; the port is fixed and never typed.
 */
final class AddressSheet {
    static final int NONE = 0, CANCEL = 1, JOIN = 2;

    // Layout, logical units (design 38.10, L5).
    private static final float W = 1800f, H = 1332f, PAD = 96f;
    private static final float FIELD_DY = 204f, FIELD_H = 120f, HINT_DY = 342f;
    private static final float LIST_DY = 420f, LIST_W = 780f, ROW_DY = 480f, ROW_H = 132f, ROW_PITCH = 138f;
    private static final float KEY_DX = 948f, KEY_W = 236f, KEY_H = 132f, KEY_PITCH_X = 260f, KEY_PITCH_Y = 156f;
    private static final float BUTTON_W = 720f, BUTTON_H = 120f, BUTTON_DY = 1116f;

    private static final String[] KEYS = { "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "del" };
    private static final int USED_MAX = 3, ADDRESS_MAX = 15;
    private static final String PREFS = "launcher", PREF_USED = "lanAddresses";

    private final StringBuilder address = new StringBuilder();
    /** Addresses joined before, the last one first: { address, host name }. */
    private final List<String[]> used;
    /** True after Join Lobby was asked for a text that is not an address. */
    private boolean wrong;

    AddressSheet(Context c) {
        used = used(c);
    }

    String address() {
        return address.toString();
    }

    /** Four numbers of 0 to 255 with dots between them. */
    static boolean valid(String s) {
        final String[] part = s.split("\\.", -1);
        if (part.length != 4) {
            return false;
        }
        for (String p : part) {
            if (p.isEmpty() || p.length() > 3 || Integer.parseInt(p) > 255) {
                return false;
            }
        }
        return true;
    }

    private static List<String[]> used(Context c) {
        final List<String[]> l = new ArrayList<String[]>();
        final String kept = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(PREF_USED, "");
        for (String line : kept.split("\n")) {
            final int cut = line.indexOf('|');
            if (cut > 0 && l.size() < USED_MAX) {
                l.add(new String[] { line.substring(0, cut), line.substring(cut + 1) });
            }
        }
        return l;
    }

    /** Keep an address a room was joined at, with its host's name, in front of the earlier ones. */
    static void remember(Context c, String address, String host) {
        final StringBuilder s = new StringBuilder(address).append('|').append(host.replace('\n', ' '));
        int kept = 1;
        for (String[] u : used(c)) {
            if (!u[0].equals(address) && kept < USED_MAX) {
                s.append('\n').append(u[0]).append('|').append(u[1]);
                ++kept;
            }
        }
        final SharedPreferences p = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        p.edit().putString(PREF_USED, s.toString()).apply();
    }

    private float x() { return (Screen.CANVAS_W - W) / 2f; }
    private float y(SkinKit kit) { return kit.height / 2f - H / 2f; }
    private float keyX(int i) { return x() + KEY_DX + KEY_PITCH_X * (i % 3); }
    private float keyY(SkinKit kit, int i) { return y(kit) + LIST_DY + KEY_PITCH_Y * (i / 3); }

    void draw(Canvas c, SkinKit kit) {
        final float x = x(), y = y(kit), px = x + PAD, pw = W - PAD * 2f;
        LanKit.sheet(c, kit, x, y, W, H);
        kit.textTop(c, kit.own("Join by Address"), px, y + PAD, LanKit.CAP_BODY, LanKit.GOLD, true);

        final float fy = y + FIELD_DY;
        kit.fill(c, px, fy, pw, FIELD_H, LanKit.FIELD, 14f);
        kit.outline(c, px, fy, pw, FIELD_H, LanKit.GOLD, 14f);
        final String shown = address.toString();
        kit.textMiddle(c, shown, px + 36f, fy, FIELD_H, LanKit.CAP_BODY, LanKit.INK, true);
        // The caret stands after the last digit.
        final float caret = px + 36f + (shown.isEmpty() ? 0f : kit.width(shown, LanKit.CAP_BODY) + 8f);
        kit.fill(c, caret, fy + 30f, 4f, FIELD_H - 60f, LanKit.GOLD, 0f);
        kit.textTop(c, kit.own("An address has four numbers, for example 192.168.1.20"), px, y + HINT_DY,
                    LanKit.CAP_SMALL, wrong ? LanKit.GOLD : LanKit.INK_MUTED, true);

        kit.textTop(c, kit.own("Used before"), px, y + LIST_DY, LanKit.CAP_SMALL, LanKit.INK_MUTED, true);
        for (int i = 0; i < used.size(); ++i) {
            final float ry = y + ROW_DY + ROW_PITCH * i;
            kit.fill(c, px, ry, LIST_W, ROW_H, LanKit.ROW_FAINT, LanKit.ROW_RADIUS);
            kit.textMiddle(c, used.get(i)[0], px + 36f, ry, ROW_H, LanKit.CAP_BODY, LanKit.INK, true);
            // The name takes what the address leaves, with a gap between the two.
            final float nameW = Math.max(0f, LIST_W - 72f - kit.width(used.get(i)[0], LanKit.CAP_BODY) - 36f);
            kit.textMiddleRight(c, kit.fit(used.get(i)[1], LanKit.CAP_MID, nameW), px + LIST_W - 36f, ry, ROW_H,
                                LanKit.CAP_MID, LanKit.INK_MUTED, true);
        }
        for (int i = 0; i < KEYS.length; ++i) {
            LanKit.well(c, kit, keyX(i), keyY(kit, i), KEY_W, KEY_H);
            kit.textCentre(c, i == 11 ? kit.own(KEYS[i]) : KEYS[i], keyX(i), keyY(kit, i), KEY_W, KEY_H,
                           i == 11 ? LanKit.CAP_MID : LanKit.CAP_BODY, LanKit.INK, true);
        }

        final float by = y + BUTTON_DY;
        LanKit.button(c, kit, kit.string("common_cancel", "Cancel"), px, by, BUTTON_W, BUTTON_H,
                      LanKit.CAP_BODY, LanKit.PLAIN);
        LanKit.button(c, kit, kit.own("Join Lobby"), x + W - PAD - BUTTON_W, by, BUTTON_W, BUTTON_H,
                      LanKit.CAP_BODY, address.length() > 0 ? LanKit.PRIMARY : LanKit.DISABLED);
    }

    /** A finger was lifted at (x, y), in logical units. */
    int up(float px, float py, SkinKit kit) {
        final float x = x(), y = y(kit), by = y + BUTTON_DY;
        if (LanKit.in(px, py, x + PAD, by, BUTTON_W, BUTTON_H)) {
            return CANCEL;
        }
        if (LanKit.in(px, py, x + W - PAD - BUTTON_W, by, BUTTON_W, BUTTON_H)) {
            wrong = !valid(address.toString());
            return wrong ? NONE : JOIN;
        }
        for (int i = 0; i < used.size(); ++i) {
            if (LanKit.in(px, py, x + PAD, y + ROW_DY + ROW_PITCH * i, LIST_W, ROW_H)) {
                address.setLength(0);
                address.append(used.get(i)[0]);
                wrong = false;
            }
        }
        for (int i = 0; i < KEYS.length; ++i) {
            if (LanKit.in(px, py, keyX(i), keyY(kit, i), KEY_W, KEY_H)) {
                if (i == 11) {
                    address.setLength(Math.max(0, address.length() - 1));
                } else if (address.length() < ADDRESS_MAX) {
                    address.append(KEYS[i]);
                }
                wrong = false;
            }
        }
        return NONE;
    }
}
