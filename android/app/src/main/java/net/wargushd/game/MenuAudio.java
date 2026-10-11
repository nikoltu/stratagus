package net.wargushd.game;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.SoundPool;
import android.util.Log;

import java.io.File;

/**
 * The launcher's own sounds: the game's interface sounds, read in place from the game folder
 * and played through a sound pool. They follow the same switch as the menu music. Without a
 * game folder, or without a file, nothing is played.
 */
final class MenuAudio {
    static final int BUTTON = 0, MENU = 1, ERROR = 2;
    private static final String[] FILES = { "Data/Sfx/Button.wav", "Data/Sfx/Menu.wav", "Data/Sfx/Error.wav" };
    private static final String[] NAMES = { "button", "menu", "error" };

    private final SoundPool pool;
    private final int[] ids = new int[FILES.length];
    private final boolean[] ready = new boolean[FILES.length];
    private boolean enabled = true;

    MenuAudio(Context c) {
        pool = new SoundPool.Builder().setMaxStreams(3).setAudioAttributes(
            new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build()).build();
        pool.setOnLoadCompleteListener(new SoundPool.OnLoadCompleteListener() {
            @Override
            public void onLoadComplete(SoundPool p, int id, int status) {
                for (int i = 0; i < ids.length; ++i) {
                    if (ids[i] == id) {
                        ready[i] = (status == 0);
                    }
                }
            }
        });
        final String folder = GateActivity.rememberedFolder(c);
        for (int i = 0; i < FILES.length; ++i) {
            final File f = (folder != null) ? new File(folder, FILES[i]) : null;
            if (f != null && f.isFile()) {
                try {
                    ids[i] = pool.load(f.getPath(), 1);
                } catch (RuntimeException e) {
                    ids[i] = 0;
                }
            }
        }
    }

    void setEnabled(boolean on) {
        enabled = on;
    }

    /** How many sounds were asked for so far (whether or not they could be played). */
    int asked;

    void play(int which) {
        ++asked;
        if (!enabled || which < 0 || which >= ids.length || !ready[which]) {
            return;
        }
        pool.play(ids[which], 1f, 1f, 1, 0, 1f);
        Log.i("WargusHD", "LAUNCHER sound " + NAMES[which]);
    }

    void release() {
        pool.release();
    }
}
