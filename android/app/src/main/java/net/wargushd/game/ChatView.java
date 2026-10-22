package net.wargushd.game;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.text.InputType;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

import java.util.ArrayList;
import java.util.List;

/**
 * The chat control of a game with other people, above the engine's surface: a small envelope
 * in the top-right corner and, while a message is typed, the message line above the system
 * keyboard with the last lines and four quick messages over it.
 *
 * The view covers the whole surface but takes touches only on the envelope's touch target
 * while it is closed; everything else goes through to the game. While it is open every touch
 * and every key is the view's, none the game's. The engine's surface is never moved or
 * resized: the window does not adjust to the keyboard, the line is placed above it from the
 * window insets.
 *
 * It shows what the engine reports (the lines it has shown) and hands the engine the typed
 * line; it sends nothing itself.
 */
final class ChatView extends View {
    interface Listener {
        /** The player has finished a line. */
        void onSend(String text);

        /** The message line was closed: the game has the keys again. */
        void onClosed();
    }

    /** One line as the engine reported it. */
    static final class Line {
        /** 0xRRGGBB of the player, or -1 for a line without a player. */
        final int colour;
        final String name;
        final String text;

        Line(int colour, String name, String text) {
            this.colour = colour;
            this.name = name;
            this.text = text;
        }
    }

    /** The longest message, in characters; the engine has the same bound. */
    static final int MESSAGE_MAX = 120;

    // Boards ingame_chat, ingame_chat_unread, ingame_chat_open (layout tables 6.19, 6.19a, 6.19b),
    // in canvas units; the canvas is 3840 wide.
    private static final float HIT_X = 3696f, HIT_Y = 0f, HIT_W = 144f, HIT_H = 144f;
    private static final float PLATE_X = 3744f, PLATE_Y = 0f, PLATE_W = 96f, PLATE_H = 72f;
    private static final float GLYPH_X = 3762f, GLYPH_Y = 15f, GLYPH_W = 60f, GLYPH_H = 42f;
    private static final float DOT_X = 3750f, DOT_Y = 6f, DOT = 18f;
    private static final int PLATE_IDLE = 0x82100C08, PLATE_LIT = 0xEB100C08, DOT_INK = 0xFFFF9680;
    private static final int GLYPH_IDLE = 0x8CE8E2D4;
    /** The message line: its height, and the rows in it measured from its top. */
    private static final float LINE_H = 168f, LINE_FILL_X = 0f;
    private static final int LINE_FILL = 0xFA100C08, BOX_FILL = 0xA0000000;
    private static final float BOX_X = 816f, BOX_DY = 30f, BOX_W = 2040f, BOX_H = 108f, BOX_TEXT_X = 852f;
    private static final float SEND_X = 2904f, CANCEL_X = 3360f, BUTTON_DY = 24f, BUTTON_W = 432f, BUTTON_H = 120f;
    /** The quick messages: a row above the line. */
    private static final float QUICK_X = 48f, QUICK_PITCH = 492f, QUICK_W = 468f, QUICK_H = 108f, QUICK_UP = 132f;
    private static final String[] QUICK = { "Attack now", "Need help", "On my way", "Good game" };
    /** The last lines: a plate above the quick messages, as wide as their row (not on the boards). */
    private static final int RECENT = 6;
    private static final float RECENT_W = 3f * QUICK_PITCH + QUICK_W, RECENT_PAD = 24f, RECENT_GAP = 24f;
    /** The keyboard's height where the system does not tell it (board 6.19b). */
    private static final float KEYBOARD_ASSUMED = 810f;

    private final SkinKit kit;
    private final Listener listener;
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path flap = new Path();
    private final StringBuilder message = new StringBuilder();
    private List<Line> lines = new ArrayList<Line>();
    private int linesTotal;

    private boolean open;
    private boolean unread;
    private boolean pressed;
    /** True once the system keyboard was seen up for the line being typed. */
    private boolean keysSeen;
    /** The characters at the end of the message that are the keyboard's composing text. */
    private int composing;
    /** The keyboard's height in canvas units. */
    private float keyboard;
    /** Lines this device has just sent: their arrival is no news. */
    private int ownPending;
    private float loggedLineY = -1f;

    ChatView(Context c, SkinKit kit, Listener listener) {
        super(c);
        this.kit = kit;
        this.listener = listener;
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeJoin(Paint.Join.ROUND);
    }

    /** The engine's lines have changed; `total` is how many it has added in all. */
    void setLines(List<Line> now, int total) {
        final boolean more = total > linesTotal;
        linesTotal = total;
        lines = now;
        if (more) {
            if (ownPending > 0) {
                --ownPending;
            } else if (!open) {
                unread = true;
            }
        }
        invalidate();
    }

    boolean isOpen() {
        return open;
    }

    /** Close the message line (the game has ended, or the player is done). */
    void close() {
        if (!open) {
            return;
        }
        open = false;
        keysSeen = false;
        message.setLength(0);
        ((InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE))
            .hideSoftInputFromWindow(getWindowToken(), 0);
        clearFocus();
        setFocusable(false);
        setFocusableInTouchMode(false);
        Log.i("WargusHD", "LAUNCH chat closed");
        listener.onClosed();
        invalidate();
    }

    private void open() {
        open = true;
        unread = false;
        ownPending = 0;
        keysSeen = false;
        keyboard = (android.os.Build.VERSION.SDK_INT >= 30) ? 0f : KEYBOARD_ASSUMED;
        message.setLength(0);
        composing = 0;
        setFocusable(true);
        setFocusableInTouchMode(true);
        requestFocus();
        final InputMethodManager keys =
            (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
        keys.restartInput(this);
        keys.showSoftInput(this, 0);
        Log.i("WargusHD", "LAUNCH chat open");
        invalidate();
    }

    private void type(CharSequence text) {
        for (int i = 0; i < text.length() && message.length() < MESSAGE_MAX; ++i) {
            final char ch = text.charAt(i);
            if (ch >= 0x20 && ch != 0x7F) {
                message.append(ch);
            }
        }
        invalidate();
    }

    private void erase(int count) {
        int length = Math.max(0, message.length() - Math.max(0, count));
        // Never half of a character made of two.
        if (length > 0 && Character.isHighSurrogate(message.charAt(length - 1))) {
            --length;
        }
        message.setLength(length);
        invalidate();
    }

    /** Send the typed line; the message line stays open for the next one. */
    private void send(String text) {
        final String line = text.trim();
        if (!line.isEmpty()) {
            ++ownPending;
            listener.onSend(line);
        }
        invalidate();
    }

    private void sendTyped() {
        final String text = message.toString();
        message.setLength(0);
        if (composing > 0) {
            // The word the keyboard was still composing went out with the line: the keyboard
            // is told to forget it, or it would type it again into the next one.
            composing = 0;
            ((InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE)).restartInput(this);
        }
        send(text);
    }

    // ----- the keyboard -----

    @Override
    public boolean onCheckIsTextEditor() {
        return open;
    }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo out) {
        if (!open) {
            return null;
        }
        out.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD;
        out.imeOptions = EditorInfo.IME_ACTION_SEND | EditorInfo.IME_FLAG_NO_EXTRACT_UI
            | EditorInfo.IME_FLAG_NO_FULLSCREEN;
        // The line is the keyboard's text, the cursor at its end. A keyboard that composes
        // (swipe, a non-Latin script, one that ignores the no-suggestions hint) gets its
        // composing word shown at the end of the line and replaced as it changes; without
        // this the base connection kept that word to itself and sent it on as one key event
        // the view does not take, so it was lost.
        return new BaseInputConnection(this, false) {
            private void replaceComposing(CharSequence text) {
                message.setLength(Math.max(0, message.length() - composing));
                final int had = message.length();
                type(text);
                composing = message.length() - had;
            }

            @Override
            public boolean commitText(CharSequence text, int newCursorPosition) {
                if (open) {
                    replaceComposing(text);
                    composing = 0;
                }
                return true;
            }

            @Override
            public boolean setComposingText(CharSequence text, int newCursorPosition) {
                if (open) {
                    replaceComposing(text);
                }
                return true;
            }

            @Override
            public boolean finishComposingText() {
                composing = 0;
                return true;
            }

            @Override
            public CharSequence getTextBeforeCursor(int n, int flags) {
                final String line = open ? message.toString() : "";
                return line.substring(Math.max(0, line.length() - Math.max(0, n)));
            }

            @Override
            public CharSequence getTextAfterCursor(int n, int flags) {
                return "";
            }

            @Override
            public boolean deleteSurroundingText(int before, int after) {
                if (open) {
                    erase(before);
                    composing = Math.max(0, composing - Math.max(0, before));
                }
                return true;
            }

            @Override
            public boolean performEditorAction(int action) {
                if (open) {
                    sendTyped();
                }
                return true;
            }
        };
    }

    @Override
    public boolean onKeyDown(int code, KeyEvent e) {
        if (!open) {
            return super.onKeyDown(code, e);
        }
        if (code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_ESCAPE) {
            close();
        } else if (code == KeyEvent.KEYCODE_DEL) {
            erase(1);
        } else if (code == KeyEvent.KEYCODE_ENTER || code == KeyEvent.KEYCODE_NUMPAD_ENTER) {
            sendTyped();
        } else if (e.getUnicodeChar() != 0) {
            type(new String(Character.toChars(e.getUnicodeChar())));
        }
        // While a line is typed no key is the game's.
        return true;
    }

    @Override
    public boolean onKeyUp(int code, KeyEvent e) {
        return open || super.onKeyUp(code, e);
    }

    // ----- touch -----

    private float lineY() {
        return kit.height - keyboard - LINE_H;
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        final float unit = getWidth() / Screen.CANVAS_W;
        if (unit <= 0f) {
            return false;
        }
        final float x = e.getX() / unit, y = e.getY() / unit;
        final boolean onControl = LanKit.in(x, y, HIT_X, HIT_Y, HIT_W, HIT_H);
        final int action = e.getActionMasked();
        if (!open) {
            // Only the envelope is the view's; everything else is the game's.
            if (action == MotionEvent.ACTION_DOWN) {
                if (!onControl) {
                    return false;
                }
                pressed = true;
                invalidate();
                return true;
            }
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                final boolean was = pressed;
                pressed = false;
                if (was && onControl && action == MotionEvent.ACTION_UP) {
                    open();
                }
                invalidate();
            }
            return true;
        }
        if (action != MotionEvent.ACTION_UP) {
            return true;
        }
        final float ly = lineY();
        if (onControl || LanKit.in(x, y, CANCEL_X, ly + BUTTON_DY, BUTTON_W, BUTTON_H)) {
            close();
        } else if (LanKit.in(x, y, SEND_X, ly + BUTTON_DY, BUTTON_W, BUTTON_H)) {
            sendTyped();
        } else {
            for (int i = 0; i < QUICK.length; ++i) {
                if (LanKit.in(x, y, QUICK_X + i * QUICK_PITCH, ly - QUICK_UP, QUICK_W, QUICK_H)) {
                    send(kit.own(QUICK[i]));
                }
            }
        }
        return true;
    }

    // ----- drawing -----

    @Override
    protected void onDraw(Canvas c) {
        kit.begin(getWidth(), getHeight());
        if (open && android.os.Build.VERSION.SDK_INT >= 30 && getRootWindowInsets() != null) {
            final android.view.WindowInsets insets = getRootWindowInsets();
            if (insets.isVisible(android.view.WindowInsets.Type.ime())) {
                keysSeen = true;
                keyboard = insets.getInsets(android.view.WindowInsets.Type.ime()).bottom / kit.unit;
            } else if (keysSeen) {
                // The keyboard was closed from outside (Back, its own hide key): the line goes
                // with it; what was typed is dropped, as with Cancel.
                close();
            }
        }
        drawControl(c);
        if (!open) {
            return;
        }
        final float y = lineY();
        if (y != loggedLineY) {
            loggedLineY = y;
            Log.i("WargusHD", "LAUNCH chat line y " + Math.round(y) + " of " + Math.round(kit.height));
        }
        drawRecent(c, y - QUICK_UP - RECENT_GAP);
        for (int i = 0; i < QUICK.length; ++i) {
            final float qx = QUICK_X + i * QUICK_PITCH, qy = y - QUICK_UP;
            kit.fill(c, qx, qy, QUICK_W, QUICK_H, PLATE_LIT, 0f);
            kit.textCentre(c, kit.own(QUICK[i]), qx, qy, QUICK_W, QUICK_H, LanKit.CAP_MID, LanKit.INK, true);
        }
        kit.fill(c, LINE_FILL_X, y, Screen.CANVAS_W, LINE_H, LINE_FILL, 0f);
        kit.fill(c, BOX_X, y + BOX_DY, BOX_W, BOX_H, BOX_FILL, 0f);
        // A message longer than the box shows its end.
        String shown = message.toString();
        while (shown.length() > 1 && kit.width(shown, LanKit.CAP_BODY) > BOX_W - 96f) {
            shown = shown.substring(1);
        }
        kit.textMiddle(c, shown, BOX_TEXT_X, y + BOX_DY, BOX_H, LanKit.CAP_BODY, LanKit.INK, true);
        final float caret = BOX_TEXT_X + (shown.isEmpty() ? 0f : kit.width(shown, LanKit.CAP_BODY) + 8f);
        kit.fill(c, caret, y + BOX_DY + 30f, 4f, BOX_H - 60f, LanKit.GOLD, 0f);
        LanKit.button(c, kit, kit.string("mp_chat_send", "Send"), SEND_X, y + BUTTON_DY, BUTTON_W, BUTTON_H,
                      LanKit.CAP_BODY, LanKit.PRIMARY);
        LanKit.button(c, kit, kit.string("common_cancel", "Cancel"), CANCEL_X, y + BUTTON_DY, BUTTON_W,
                      BUTTON_H, LanKit.CAP_BODY, LanKit.PLAIN);
        // The insets may still change while the keyboard comes up.
        if (!keysSeen && android.os.Build.VERSION.SDK_INT >= 30) {
            postInvalidateOnAnimation();
        }
    }

    /** The envelope in its corner: subdued when idle, lit with a dot when a line is unread, gold when pressed or open. */
    private void drawControl(Canvas c) {
        final boolean lit = open || pressed || unread;
        kit.fill(c, PLATE_X, PLATE_Y, PLATE_W, PLATE_H, lit ? PLATE_LIT : PLATE_IDLE, 0f);
        final float u = kit.unit;
        stroke.setStrokeWidth(Math.max(1f, 4f * u));
        stroke.setColor(open || pressed ? LanKit.GOLD : unread ? LanKit.INK : GLYPH_IDLE);
        final float l = GLYPH_X * u, t = GLYPH_Y * u, r = (GLYPH_X + GLYPH_W) * u, b = (GLYPH_Y + GLYPH_H) * u;
        c.drawRoundRect(l, t, r, b, 4f * u, 4f * u, stroke);
        flap.rewind();
        flap.moveTo(l, t);
        flap.lineTo((l + r) / 2f, t + (b - t) * 0.58f);
        flap.lineTo(r, t);
        c.drawPath(flap, stroke);
        if (unread && !open) {
            kit.fill(c, DOT_X, DOT_Y, DOT, DOT, DOT_INK, DOT / 2f);
        }
    }

    /** The last lines on a plate whose foot is at `foot`. */
    private void drawRecent(Canvas c, float foot) {
        final int count = Math.min(RECENT, lines.size());
        if (count == 0) {
            return;
        }
        final float h = count * LanKit.LINE_PITCH + RECENT_PAD;
        final float top = foot - h;
        kit.fill(c, QUICK_X, top, RECENT_W, h, PLATE_LIT, 0f);
        final float x = QUICK_X + 36f, w = RECENT_W - 72f;
        for (int i = 0; i < count; ++i) {
            final Line line = lines.get(lines.size() - count + i);
            final float ly = top + RECENT_PAD + i * LanKit.LINE_PITCH;
            if (line.colour < 0) {
                kit.textTop(c, kit.fit(line.text, LanKit.CAP_MID, w), x, ly, LanKit.CAP_MID, LanKit.INK_MUTED, true);
            } else {
                kit.fill(c, x, ly, 36f, 36f, 0xFF000000 | line.colour, 4.5f);
                kit.outline(c, x, ly, 36f, 36f, LanKit.LINE, 4.5f);
                kit.textTop(c, kit.fit(line.name + ": " + line.text, LanKit.CAP_MID, w - 54f), x + 54f, ly,
                            LanKit.CAP_MID, LanKit.INK, true);
            }
        }
    }
}
