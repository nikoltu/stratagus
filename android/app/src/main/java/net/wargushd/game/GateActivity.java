package net.wargushd.game;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.text.TextUtils;
import android.view.MotionEvent;
import android.view.View;

import java.io.File;
import java.util.List;

/**
 * First-run gate: obtain storage access and the game folder, and verify the folder.
 *
 * The app contains no game files, so this screen is drawn with flat fills and the system
 * typeface only. It is laid out in logical units on a canvas 3840 wide (2400 high on a
 * 16:10 screen); one unit is screen width / 3840 pixels, and the panel is centred
 * vertically.
 */
public class GateActivity extends Activity {
    private static final int REQ_PICK = 1001;
    /** Opened from the shell's Options: Continue and Exit lead back there, nothing is started. */
    static final String EXTRA_RETURN = "net.wargushd.game.GATE_RETURN";
    private static final int REQ_READ = 1002;

    // Layout, logical units. Vertical positions are relative to the panel's top.
    private static final float PANEL_X = 720f, PANEL_W = 2400f, PANEL_H = 1560f;
    private static final float TEXT_X = 840f, TEXT_W = 2160f;
    private static final float TITLE_Y = 100f, SUB_Y = 220f, HINT_Y = 960f;
    private static final float STEP_Y = 400f, STEP_PITCH = 123.75f;
    private static final float BTN_Y = 1320f, BTN_W = 672f, BTN_H = 120f, BTN_PITCH = 744f;
    private static final float CAP_TITLE = 60f, CAP_BODY = 44f, CAP_SMALL = 36f;

    private static final int INK = 0xFFE3EDF2, INK_DIM = 0xFFA0AEB8, INK_BAD = 0xFFFF9682;
    private static final int INK_OFF = 0xFF96A0A8;
    private static final int PANEL_FILL = 0xFF141B20, PANEL_LINE = 0xFF3A4A55;
    private static final int BTN_FILL = 0xFF1F2A31, BTN_FILL_OFF = 0xFF1A2126, BTN_LINE = 0xFF55707F;

    private GateView view;

    // State shown by the screen; refreshed on every resume.
    private boolean accessOk;
    private String folder;            // remembered path, or null
    private boolean folderExists;
    private GameFiles.Required required;
    private String optionalLine;

    static File sourceMarker(Context c) {
        return new File(c.getFilesDir(), "wc2_source.txt");
    }

    /** True when access is granted and the remembered folder passes the required-files check. */
    static boolean isSatisfied(Context c) {
        if (!hasAccess(c)) {
            return false;
        }
        final String path = rememberedFolder(c);
        return path != null && GameFiles.checkRequired(new File(path)).ok;
    }

    static String rememberedFolder(Context c) {
        final String text = GameLaunch.readText(sourceMarker(c));
        if (text == null) {
            return null;
        }
        final String path = text.trim();
        return path.isEmpty() ? null : path;
    }

    static boolean hasAccess(Context c) {
        if (Build.VERSION.SDK_INT >= 30) {
            return Environment.isExternalStorageManager();
        }
        if (Build.VERSION.SDK_INT >= 23) {
            return c.checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                == PackageManager.PERMISSION_GRANTED;
        }
        return true;
    }

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        view = new GateView(this);
        setContentView(view);
        Screen.useWhole(this);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            Screen.useWhole(this);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
    }

    /** Re-read everything the checklist shows. */
    private void refresh() {
        accessOk = hasAccess(this);
        folder = rememberedFolder(this);
        folderExists = folder != null && new File(folder).isDirectory();
        required = null;
        optionalLine = null;
        if (accessOk && folderExists) {
            final File dir = new File(folder);
            required = GameFiles.checkRequired(dir);
            final List<String> found = GameFiles.newList();
            final List<String> absent = GameFiles.newList();
            GameFiles.checkOptional(dir, found, absent);
            if (absent.isEmpty()) {
                optionalLine = GameFiles.join(found) + " found";
            } else if (found.isEmpty()) {
                optionalLine = "none found (" + GameFiles.join(absent) + ")";
            } else {
                optionalLine = GameFiles.join(found) + " found; absent: " + GameFiles.join(absent);
            }
        }
        view.invalidate();
    }

    private boolean canContinue() {
        return accessOk && required != null && required.ok;
    }

    private void askAccess() {
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName())));
            } catch (RuntimeException e) {
                try {
                    startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                } catch (RuntimeException ignored) {
                }
            }
        } else if (Build.VERSION.SDK_INT >= 23) {
            requestPermissions(new String[] { Manifest.permission.READ_EXTERNAL_STORAGE }, REQ_READ);
        }
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(code, permissions, results);
        refresh();
    }

    private void chooseFolder() {
        final Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
            | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(i, REQ_PICK);
        } catch (RuntimeException ignored) {
        }
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_PICK || res != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        final Uri tree = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(
                tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (RuntimeException ignored) {
        }
        final String path = resolveTreePath(tree);
        if (path != null) {
            GameLaunch.writeText(sourceMarker(this), path);
        }
        // onResume follows and re-reads the state.
    }

    /** The file-system path of a picked folder (primary storage; other volumes by their id). */
    private static String resolveTreePath(Uri tree) {
        try {
            final String[] parts = DocumentsContract.getTreeDocumentId(tree).split(":", 2);
            final String rel = parts.length > 1 ? parts[1] : "";
            if ("primary".equalsIgnoreCase(parts[0])) {
                return new File(Environment.getExternalStorageDirectory(), rel).getAbsolutePath();
            }
            return "/storage/" + parts[0] + "/" + rel;
        } catch (RuntimeException e) {
            return null;
        }
    }

    private void proceed() {
        setResult(RESULT_OK);
        if (getIntent().getBooleanExtra(EXTRA_RETURN, false)) {
            finish();
            return;
        }
        startActivity(new Intent(this, SetupActivity.class));
        finish();
    }

    /** The whole screen: one panel, eight lines of text, three buttons. */
    private final class GateView extends View {
        private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
        private final Rect bounds = new Rect();
        private final RectF box = new RectF();
        /** Text size that gives a capital letter a height of one pixel. */
        private final float sizePerCap;

        private float unit;      // pixels per logical unit
        private float panelTop;  // pixels

        GateView(Context c) {
            super(c);
            text.setTypeface(Typeface.DEFAULT);
            line.setStyle(Paint.Style.STROKE);
            text.setTextSize(1000f);
            text.getTextBounds("H", 0, 1, bounds);
            sizePerCap = 1000f / bounds.height();
        }

        private RectF rect(float x, float y, float w, float h) {
            box.set(x * unit, panelTop + y * unit, (x + w) * unit, panelTop + (y + h) * unit);
            return box;
        }

        /** Stroke a rounded outline that stays inside the rect last set in `box`. */
        private void outline(Canvas c, RectF r, float radius, int color) {
            final float width = Math.max(1f, 2f * unit);
            final float half = width / 2f;
            line.setColor(color);
            line.setStrokeWidth(width);
            c.drawRoundRect(r.left + half, r.top + half, r.right - half, r.bottom - half,
                            radius, radius, line);
        }

        private RectF button(int index) {
            return rect(TEXT_X + index * BTN_PITCH, BTN_Y, BTN_W, BTN_H);
        }

        private RectF step(int index) {
            return rect(TEXT_X, STEP_Y + index * STEP_PITCH - 30f, TEXT_W, CAP_BODY + 60f);
        }

        /** A line whose capitals start at the region's top, cut with an ellipsis if too long. */
        private void drawLine(Canvas c, String s, float y, float cap, int color) {
            text.setTextSize(cap * unit * sizePerCap);
            text.setColor(color);
            text.setTextAlign(Paint.Align.LEFT);
            final CharSequence shown = TextUtils.ellipsize(
                s, new android.text.TextPaint(text), TEXT_W * unit, TextUtils.TruncateAt.END);
            text.getTextBounds(shown.toString(), 0, Math.min(1, shown.length()), bounds);
            c.drawText(shown, 0, shown.length(), TEXT_X * unit - bounds.left,
                       panelTop + (y + cap) * unit, text);
        }

        private void drawButton(Canvas c, int index, String label, boolean enabled) {
            final RectF r = button(index);
            final float radius = 14f * unit;
            fill.setColor(enabled ? BTN_FILL : BTN_FILL_OFF);
            c.drawRoundRect(r, radius, radius, fill);
            outline(c, r, radius, BTN_LINE);
            // Centred by the ink width; the capitals' box is centred vertically.
            text.setTextSize(CAP_BODY * unit * sizePerCap);
            text.setColor(enabled ? INK : INK_OFF);
            text.setTextAlign(Paint.Align.LEFT);
            text.getTextBounds(label, 0, label.length(), bounds);
            final float x = r.centerX() - bounds.width() / 2f - bounds.left;
            final float baseline = r.centerY() + CAP_BODY * unit / 2f;
            c.drawText(label, x, baseline, text);
        }

        @Override
        protected void onDraw(Canvas c) {
            unit = getWidth() / Screen.CANVAS_W;
            panelTop = (getHeight() - PANEL_H * unit) / 2f;
            c.drawColor(0xFF000000);

            final RectF panel = rect(PANEL_X, 0f, PANEL_W, PANEL_H);
            final float radius = 18f * unit;
            fill.setColor(PANEL_FILL);
            c.drawRoundRect(panel, radius, radius, fill);
            outline(c, panel, radius, PANEL_LINE);

            drawLine(c, "Choose your Warcraft II: Remastered folder", TITLE_Y, CAP_TITLE, INK);
            drawLine(c, "This app contains no game files and downloads none.", SUB_Y, CAP_SMALL, INK_DIM);

            drawLine(c, accessOk ? "1  Storage access - granted"
                                 : "1  Storage access - not granted. Tap here to allow it.",
                     STEP_Y, CAP_BODY, accessOk ? INK : INK_BAD);

            final String s2;
            if (folder == null) {
                s2 = "2  Folder - not chosen";
            } else if (accessOk && !folderExists) {
                s2 = "2  Folder - not found: " + folder;
            } else {
                s2 = "2  Folder - " + folder;
            }
            drawLine(c, s2, STEP_Y + STEP_PITCH, CAP_BODY,
                     (folder != null && (folderExists || !accessOk)) ? INK : INK_BAD);

            final String s3;
            final boolean bad3;
            if (required == null) {
                s3 = "3  Required files - not checked yet";
                bad3 = false;
            } else if (required.ok) {
                s3 = "3  Required files - all present";
                bad3 = false;
            } else {
                s3 = "3  Required files - " + (required.unreadable ? "damaged: " : "missing: ")
                    + required.path;
                bad3 = true;
            }
            drawLine(c, s3, STEP_Y + 2 * STEP_PITCH, CAP_BODY,
                     bad3 ? INK_BAD : (required == null ? INK_DIM : INK));

            drawLine(c, "4  Optional content - "
                        + (optionalLine == null ? "not checked yet" : optionalLine),
                     STEP_Y + 3 * STEP_PITCH, CAP_BODY, optionalLine == null ? INK_DIM : INK);

            drawLine(c, "Until step 3 passes the launcher is not shown and no game art is used.",
                     HINT_Y, CAP_SMALL, INK_DIM);

            drawButton(c, 0, "Exit", true);
            drawButton(c, 1, "Choose Folder", true);
            drawButton(c, 2, "Continue", canContinue());
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            if (e.getAction() != MotionEvent.ACTION_UP) {
                return true;
            }
            final float x = e.getX(), y = e.getY();
            if (button(0).contains(x, y)) {
                if (getIntent().getBooleanExtra(EXTRA_RETURN, false)) {
                    finish();
                } else {
                    finishAffinity();
                }
            } else if (button(1).contains(x, y)) {
                chooseFolder();
            } else if (button(2).contains(x, y)) {
                if (canContinue()) {
                    proceed();
                }
            } else if (!accessOk && step(0).contains(x, y)) {
                askAccess();
            }
            return true;
        }
    }
}
