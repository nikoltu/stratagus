package net.wargushd.game;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;

/**
 * The licence texts of everything the app is built from, one after another on one sheet that is
 * dragged to read. The texts are the components' own files, packed as assets; a component whose
 * file is not packed is named and said to be missing.
 */
final class LicencesSheet {
    // Layout, logical units: the sheet, a well for the texts, one button.
    private static final float X = 720f, Y = 180f, W = 2400f, PAD = 72f;
    private static final float WELL_DX = 48f, WELL_DY = 180f, WELL_FOOT = 216f;
    private static final float BUTTON_W = 600f, BUTTON_H = 120f;
    private static final float CAP_TITLE = 44f, CAP_HEAD = 36f;
    private static final float HEAD_PITCH = 96f, LINE_PITCH = 54f, PART_GAP = 60f, TEXT_SIZE = 40f;

    private static final int SHADE = 0xAA000000, PANEL_FILL = 0xF5140F0A, LINE = 0xFF92743E;
    private static final int WELL = 0x78000000, GOLD = 0xFFF0D250, INK_MUTED = 0xFFCEC6B6;
    private static final int MARK = 0xC8F0D250;

    /** One line of the sheet: a component's name or a line of its text. */
    private static final class Row {
        final String text;
        final boolean head;
        final float y;

        Row(String text, boolean head, float y) {
            this.text = text;
            this.head = head;
            this.y = y;
        }
    }

    private final List<Row> rows = new ArrayList<Row>();
    private float contentHeight;
    private final Paint mono = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
    private Bitmap buttonMarked;
    private float scroll, downY, downScroll;
    private boolean dragging, inWell;

    LicencesSheet(Context c) {
        mono.setTypeface(Typeface.MONOSPACE);
        mono.setColor(INK_MUTED);
        float y = 36f;
        for (String entry : read(c, "licences/index.txt")) {
            final String[] part = entry.split(" \\| ");
            if (part.length < 2) {
                continue;
            }
            rows.add(new Row(part[1], true, y));
            y += HEAD_PITCH;
            if (part[0].equals("-")) {
                rows.add(new Row("The licence text is not packed with this build.", false, y));
                y += LINE_PITCH;
            } else {
                for (String line : read(c, "licences/" + part[0])) {
                    rows.add(new Row(line.replace("\t", "    "), false, y));
                    y += LINE_PITCH;
                }
            }
            y += PART_GAP;
        }
        if (rows.isEmpty()) {
            rows.add(new Row("No licence texts are packed with this build.", false, y));
            y += LINE_PITCH;
        }
        contentHeight = y;
    }

    private static List<String> read(Context c, String asset) {
        final List<String> lines = new ArrayList<String>();
        try (BufferedReader in = new BufferedReader(
                 new InputStreamReader(c.getAssets().open(asset), "UTF-8"))) {
            String line;
            while ((line = in.readLine()) != null) {
                lines.add(line);
            }
        } catch (IOException e) {
            // Not packed.
        }
        return lines;
    }

    private float h(SkinKit kit) { return kit.height - Y * 2f; }
    private float wellX() { return X + WELL_DX; }
    private float wellY() { return Y + WELL_DY; }
    private float wellW() { return W - WELL_DX * 2f; }
    private float wellH(SkinKit kit) { return h(kit) - WELL_DY - WELL_FOOT; }
    private float buttonX() { return X + W - PAD - BUTTON_W; }
    private float buttonY(SkinKit kit) { return Y + h(kit) - 168f; }

    private float maxScroll(SkinKit kit) {
        return Math.max(0f, contentHeight - wellH(kit));
    }

    void draw(Canvas c, SkinKit kit) {
        if (buttonMarked == null) {
            buttonMarked = kit.atlasFrame("Modern_Widgets_Orc", "large_active", 1008, 126);
        }
        kit.fill(c, 0f, 0f, Screen.CANVAS_W, kit.height, SHADE, 0f);
        kit.fill(c, X, Y, W, h(kit), PANEL_FILL, 18f);
        kit.outline(c, X, Y, W, h(kit), LINE, 18f);
        kit.textTop(c, kit.own("Licences"), X + PAD, Y + PAD, CAP_TITLE, GOLD, true);

        final float wx = wellX(), wy = wellY(), ww = wellW(), wh = wellH(kit);
        kit.fill(c, wx, wy, ww, wh, WELL, 14f);
        kit.outline(c, wx, wy, ww, wh, LINE, 14f);
        c.save();
        c.clipRect(kit.rect(wx + 6f, wy + 6f, ww - 12f, wh - 12f));
        mono.setTextSize(TEXT_SIZE * kit.unit);
        for (Row r : rows) {
            final float top = wy + r.y - scroll;
            if (top + HEAD_PITCH < wy || top > wy + wh) {
                continue;
            }
            if (r.head) {
                kit.textTop(c, kit.fit(r.text, CAP_HEAD, ww - 120f), wx + 36f, top, CAP_HEAD, GOLD, true);
            } else {
                c.drawText(r.text, (wx + 36f) * kit.unit, (top + 36f) * kit.unit, mono);
            }
        }
        c.restore();
        // Where the reader is in the whole: a mark at the well's right edge.
        final float most = maxScroll(kit);
        if (most > 0f) {
            final float markH = Math.max(120f, (wh - 48f) * wh / contentHeight);
            final float markY = wy + 24f + (wh - 48f - markH) * (scroll / most);
            kit.fill(c, wx + ww - 30f, markY, 12f, markH, MARK, 6f);
        }

        final float bx = buttonX(), by = buttonY(kit);
        kit.nine(c, buttonMarked, bx, by, BUTTON_W, BUTTON_H, 14);
        kit.textCentre(c, kit.own("Close"), bx, by, BUTTON_W, BUTTON_H, CAP_TITLE, GOLD, true);
    }

    void down(float x, float y, SkinKit kit) {
        downY = y;
        downScroll = scroll;
        dragging = false;
        inWell = x >= wellX() && x < wellX() + wellW() && y >= wellY() && y < wellY() + wellH(kit);
    }

    /** @return true when the sheet must be drawn again */
    boolean move(float x, float y, SkinKit kit) {
        if (!inWell) {
            return false;
        }
        if (Math.abs(y - downY) > 16f) {
            dragging = true;
        }
        if (dragging) {
            scroll = Math.max(0f, Math.min(maxScroll(kit), downScroll - (y - downY)));
        }
        return dragging;
    }

    /** @return true when Close was tapped */
    boolean up(float x, float y, SkinKit kit) {
        final boolean wasDrag = dragging;
        dragging = false;
        return !wasDrag && x >= buttonX() && x < buttonX() + BUTTON_W
            && y >= buttonY(kit) && y < buttonY(kit) + BUTTON_H;
    }
}
