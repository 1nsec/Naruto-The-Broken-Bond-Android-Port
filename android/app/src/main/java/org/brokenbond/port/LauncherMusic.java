package org.brokenbond.port;

import android.animation.ValueAnimator;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.AssetFileDescriptor;
import android.media.AudioAttributes;
import android.media.AudioManager;
import android.media.MediaPlayer;
import android.net.Uri;

/**
 * The launcher's music, looped quietly while the launcher is on screen: the
 * port's own theme (res/raw/launcher_theme, synthesized by
 * tools/android/make-launcher-music.py) or a song the player chose in
 * Settings. It fades in, pauses with the launcher, and does not start over
 * music another app is playing. Settings > Launcher music turns it off.
 */
final class LauncherMusic {
    private static final float VOLUME = 0.45f;

    private final Context context;
    private MediaPlayer player;
    private String source;  // What the player holds: "" for the theme, or the chosen song's URI.
    private boolean prepared, wanted;
    private ValueAnimator fade;

    LauncherMusic(Context context) {
        this.context = context;
    }

    /** Starts the music, or resumes it (the launcher's onResume). */
    void start() {
        SharedPreferences prefs = Storage.prefs(context);
        if (!prefs.getBoolean(Storage.KEY_LAUNCHER_MUSIC, true)) {
            release();
            return;
        }
        String chosen = prefs.getString(Storage.KEY_LAUNCHER_MUSIC_URI, "");
        if (player != null && !chosen.equals(source)) release();  // Another song was chosen in Settings.
        AudioManager audio = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        if (audio != null && audio.isMusicActive() && (player == null || !player.isPlaying())) return;
        wanted = true;
        if (player == null) open(chosen);
        else if (prepared) fadeIn();
    }

    /** Pauses it (the launcher's onPause). */
    void pause() {
        wanted = false;
        if (fade != null) fade.cancel();
        if (player != null && prepared && player.isPlaying()) player.pause();
    }

    /** Fades it out (Play: the game is starting). */
    void fadeOut() {
        if (player == null || !prepared || !player.isPlaying()) return;
        if (fade != null) fade.cancel();
        fade = ValueAnimator.ofFloat(VOLUME, 0f).setDuration(350);
        fade.addUpdateListener(a -> volume((float) a.getAnimatedValue()));
        fade.start();
    }

    void release() {
        wanted = false;
        if (fade != null) fade.cancel();
        fade = null;
        if (player != null) player.release();
        player = null;
        prepared = false;
        source = null;
    }

    private void open(String uri) {
        source = uri;
        prepared = false;
        player = new MediaPlayer();
        player.setAudioAttributes(new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build());
        try {
            if (uri.isEmpty()) {
                try (AssetFileDescriptor fd = context.getResources().openRawResourceFd(R.raw.launcher_theme)) {
                    player.setDataSource(fd.getFileDescriptor(), fd.getStartOffset(), fd.getLength());
                }
            } else {
                player.setDataSource(context, Uri.parse(uri));
            }
        } catch (Exception e) {
            player.release();
            player = null;
            if (!uri.isEmpty()) {
                // The chosen song is gone or can no longer be read: back to the theme.
                Storage.prefs(context).edit().remove(Storage.KEY_LAUNCHER_MUSIC_URI)
                        .remove(Storage.KEY_LAUNCHER_MUSIC_NAME).apply();
                open("");
            }
            return;
        }
        player.setLooping(true);
        player.setOnPreparedListener(p -> {
            prepared = true;
            if (wanted) fadeIn();
        });
        player.setOnErrorListener((p, what, extra) -> {
            release();
            return true;
        });
        player.prepareAsync();
    }

    private void fadeIn() {
        if (fade != null) fade.cancel();
        volume(0f);
        player.start();
        fade = ValueAnimator.ofFloat(0f, VOLUME).setDuration(1800);
        fade.addUpdateListener(a -> volume((float) a.getAnimatedValue()));
        fade.start();
    }

    private void volume(float value) {
        if (player != null) player.setVolume(value, value);
    }
}
