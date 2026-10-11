package net.wargushd.game;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.os.Bundle;

import java.io.File;

/**
 * Entry activity: decides what the app needs before the game can start, and hands over.
 *
 *   storage access, game folder or required files missing  ->  the gate
 *   unpacked content does not belong to this package       ->  the preparing screen
 *   otherwise                                              ->  the game
 *
 * The gate and the preparing screen come back here when they are done.
 */
public class SetupActivity extends Activity {
    /** Intent extra (debuggable builds): show the gate even when everything is in order. */
    public static final String EXTRA_SHOW_GATE = "net.wargushd.game.SHOW_GATE";
    /** Intent extra (debuggable builds): run the preparing screen and keep it on screen. */
    public static final String EXTRA_SHOW_PREP = "net.wargushd.game.SHOW_PREP";
    /** Intent extra (debuggable builds): open the launcher's shell. */
    public static final String EXTRA_SHOW_HOME = "net.wargushd.game.SHOW_HOME";

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        final boolean debuggable =
            (getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        final Intent intent = getIntent();
        final boolean satisfied = GateActivity.isSatisfied(this);

        if (!satisfied || (debuggable && intent.getBooleanExtra(EXTRA_SHOW_GATE, false))) {
            // Storage access, the folder and its verification are the gate's business.
            startActivity(new Intent(this, GateActivity.class));
        } else if (debuggable && intent.getBooleanExtra(EXTRA_SHOW_PREP, false)) {
            startActivity(new Intent(this, PrepActivity.class)
                .putExtra(PrepActivity.EXTRA_HOLD, true));
        } else if (Provision.needed(this)) {
            startActivity(new Intent(this, PrepActivity.class));
        } else if (debuggable && intent.getBooleanExtra(EXTRA_SHOW_HOME, false)) {
            startActivity(new Intent(this, HomeActivity.class));
        } else {
            startGame();
        }
        finish();
    }

    private void startGame() {
        // The launcher is the entry; the game's own menus are retired (design phase P9).
        startActivity(new Intent(this, HomeActivity.class));
    }
}
