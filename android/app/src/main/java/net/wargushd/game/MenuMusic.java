package net.wargushd.game;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioManager;
import android.media.MediaPlayer;
import android.os.Handler;
import android.os.Looper;

import java.io.File;

/**
 * The launcher's menu music: the game's own menu track, played in a loop straight from the
 * game folder. Nothing is copied or converted.
 *
 * It plays while a launcher screen is in front and the music switch is on, steps aside when
 * another app takes the audio, and fades out when a game is started so that the engine
 * begins in silence.
 */
final class MenuMusic implements AudioManager.OnAudioFocusChangeListener {
    private static final String TRACK = "Data/Music/OWARROOM_r.wav";
    private static final long FADE_IN_MS = 500L, FADE_OUT_MS = 400L, STEP_MS = 25L;
    private static final float DUCKED = 0.3f;

    private final Context context;
    private final AudioManager audio;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private MediaPlayer player;
    /** The screen is in front and wants music. */
    private boolean wanted;
    /** The switch in the rail's footer. */
    private boolean enabled = true;
    /** Another app holds the audio for a moment. */
    private boolean interrupted;
    private float volume, ceiling = 1f;

    MenuMusic(Context c) {
        context = c.getApplicationContext();
        audio = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
    }

    /** The music switch. Takes effect at once. */
    void setEnabled(boolean on) {
        enabled = on;
        apply();
    }

    /** Call from onResume: the screen is in front. */
    void resume() {
        wanted = true;
        apply();
    }

    /** Call from onPause: the app is going to the background. */
    void pause() {
        wanted = false;
        handler.removeCallbacksAndMessages(null);
        stopNow();
    }

    /** A game is starting: fade out and let go of the player. */
    void fadeOutForGame() {
        wanted = false;
        fadeTo(0f, FADE_OUT_MS, true);
    }

    private void apply() {
        if (wanted && enabled && !interrupted) {
            start();
        } else if (!wanted || !enabled) {
            handler.removeCallbacksAndMessages(null);
            stopNow();
        } else if (player != null && player.isPlaying()) {
            player.pause();
        }
    }

    private void start() {
        if (player == null) {
            final String folder = GateActivity.rememberedFolder(context);
            final File track = (folder == null) ? null : new File(folder, TRACK);
            if (track == null || !track.isFile()) {
                return;   // no game folder, or the game has no music: silence
            }
            try {
                final MediaPlayer p = new MediaPlayer();
                p.setAudioAttributes(new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build());
                p.setDataSource(track.getPath());
                p.setLooping(true);
                p.prepare();
                player = p;
                volume = 0f;
                p.setVolume(0f, 0f);
            } catch (Exception e) {
                player = null;
                return;
            }
        }
        if (audio.requestAudioFocus(this, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN)
            != AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
            return;
        }
        if (!player.isPlaying()) {
            player.start();
        }
        fadeTo(ceiling, FADE_IN_MS, false);
    }

    private void stopNow() {
        if (player != null) {
            try {
                player.release();
            } catch (RuntimeException ignored) {
            }
            player = null;
            audio.abandonAudioFocus(this);
        }
        volume = 0f;
    }

    /** Move the volume to a target in small steps; optionally let go of the player at the end. */
    private void fadeTo(final float target, long millis, final boolean thenStop) {
        handler.removeCallbacksAndMessages(null);
        if (player == null) {
            return;
        }
        final float step = (target - volume) / Math.max(1L, millis / STEP_MS);
        handler.post(new Runnable() {
            @Override
            public void run() {
                if (player == null) {
                    return;
                }
                volume += step;
                final boolean done = (step >= 0f) ? volume >= target : volume <= target;
                if (done) {
                    volume = target;
                }
                try {
                    player.setVolume(volume, volume);
                } catch (RuntimeException e) {
                    return;
                }
                if (!done) {
                    handler.postDelayed(this, STEP_MS);
                } else if (thenStop) {
                    stopNow();
                }
            }
        });
    }

    @Override
    public void onAudioFocusChange(int change) {
        switch (change) {
            case AudioManager.AUDIOFOCUS_GAIN:
                interrupted = false;
                ceiling = 1f;
                apply();
                break;
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK:
                ceiling = DUCKED;
                fadeTo(ceiling, FADE_OUT_MS, false);
                break;
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT:
                interrupted = true;
                apply();
                break;
            default:   // permanent loss
                interrupted = true;
                handler.removeCallbacksAndMessages(null);
                stopNow();
                break;
        }
    }
}
