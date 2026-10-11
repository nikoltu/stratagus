package net.wargushd.game;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.util.Arrays;

/**
 * Developer screen for starting the game from request files.
 *
 * It lists the request files found in files/data/launch, starts the game with the chosen
 * one, and shows the record the engine left when it comes back. It is a tool for exercising
 * the start and return path, not a user interface.
 */
public class LaunchActivity extends Activity {
    /** Intent extra (debuggable builds): name of a request in files/data/launch to start at once. */
    public static final String EXTRA_REQUEST_FILE = "net.wargushd.game.REQUEST_FILE";
    /** Intent extra (debuggable builds): id of the profile to start with; absent = default. */
    public static final String EXTRA_PROFILE = "net.wargushd.game.PROFILE";
    /** Intent extra (debuggable builds): start the game activity with a token nobody issued. */
    public static final String EXTRA_STALE = "net.wargushd.game.STALE";

    private static final String STATE_TOKEN = "token";
    private static final String STATE_PROFILE = "profile";

    /** Profiles offered by the switch; the first entry is the default user directory. */
    private static final String[] PROFILES = { null, "p1", "p2" };

    private TextView status;
    /** Token of the game this screen started and is waiting for; null when none. */
    private String pendingToken;
    /** Profile used for the next start and for reading its result; null = default. */
    private String profileId;

    private File requestsDir() {
        return new File(getFilesDir(), "data/launch");
    }

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        if (saved != null) {
            pendingToken = saved.getString(STATE_TOKEN);
            profileId = saved.getString(STATE_PROFILE);
        }

        final LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        final int pad = (int) (16 * getResources().getDisplayMetrics().density);
        col.setPadding(pad, pad, pad, pad);

        status = new TextView(this);
        status.setTextIsSelectable(true);
        col.addView(status);

        final Button profile = new Button(this);
        profile.setAllCaps(false);
        profile.setText(profileLabel());
        profile.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                int at = 0;
                for (int k = 0; k < PROFILES.length; ++k) {
                    if (PROFILES[k] == null ? profileId == null : PROFILES[k].equals(profileId)) {
                        at = k;
                    }
                }
                profileId = PROFILES[(at + 1) % PROFILES.length];
                profile.setText(profileLabel());
            }
        });
        col.addView(profile);

        final String[] names = requestsDir().list();
        if (names != null) {
            Arrays.sort(names);
            for (final String name : names) {
                if (!name.endsWith(".lua")) {
                    continue;
                }
                final Button b = new Button(this);
                b.setAllCaps(false);
                b.setText(name);
                b.setOnClickListener(new View.OnClickListener() {
                    @Override
                    public void onClick(View v) {
                        startRequest(name);
                    }
                });
                col.addView(b);
            }
        }

        final ScrollView scroll = new ScrollView(this);
        scroll.addView(col);
        setContentView(scroll);

        if (saved == null) {
            handleStartExtras(getIntent());
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        handleStartExtras(intent);
    }

    @Override
    protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        out.putString(STATE_TOKEN, pendingToken);
        out.putString(STATE_PROFILE, profileId);
    }

    @Override
    protected void onResume() {
        super.onResume();
        collectResult();
    }

    private String profileLabel() {
        return "Profile: " + (profileId == null ? "default" : profileId);
    }

    private File profileDir() {
        return GameLaunch.profileDir(this, profileId);
    }

    /** Starts asked for on the command line; honoured in debuggable builds only. */
    private void handleStartExtras(Intent intent) {
        final boolean debuggable =
            (getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        if (!debuggable || intent == null) {
            return;
        }
        if (intent.getBooleanExtra(EXTRA_STALE, false)) {
            intent.removeExtra(EXTRA_STALE);
            final Intent i = new Intent(this, WargusHDActivity.class);
            i.putExtra(GameLaunch.EXTRA_TOKEN, GameLaunch.newToken());
            startActivity(i);
            status.setText("Started the game with a token that was never issued.");
            return;
        }
        final String name = intent.getStringExtra(EXTRA_REQUEST_FILE);
        if (name != null) {
            profileId = intent.getStringExtra(EXTRA_PROFILE);
            intent.removeExtra(EXTRA_PROFILE);
            intent.removeExtra(EXTRA_REQUEST_FILE);
            startRequest(name);
        }
    }

    /** Copy the named request, give it a fresh token, and start the game with it. */
    private void startRequest(String name) {
        final String text = GameLaunch.readText(new File(requestsDir(), new File(name).getName()));
        if (text == null) {
            status.setText("No such request: " + name);
            return;
        }
        final String token = GameLaunch.newToken();
        final File request = GameLaunch.requestFile(this);
        final String body = text + "\nLaunchRequest.token = \"" + token + "\"\n";
        GameLaunch.resultFile(this, profileDir()).delete();
        if (!GameLaunch.writeText(request, body)
            || !GameLaunch.start(this, token, request, profileDir(), 0L)) {
            status.setText("Could not write the request.");
            return;
        }
        pendingToken = token;
        status.setText("Started " + name + " (" + profileLabel() + ")");
    }

    /** Read the record of the game this screen started, show it, and remove it. */
    private void collectResult() {
        if (pendingToken == null) {
            return;
        }
        final File f = GameLaunch.resultFile(this, profileDir());
        final String text = GameLaunch.readText(f);
        if (text == null) {
            // Still running, or it ended without leaving a record; the next resume tells.
            status.setText("No result yet for the game that was started.");
            return;
        }
        f.delete();
        final boolean mine = text.contains("\"token\":\"" + pendingToken + "\"");
        pendingToken = null;
        status.setText((mine ? "Result:\n" : "A result with a foreign token was discarded:\n") + text);
    }
}
