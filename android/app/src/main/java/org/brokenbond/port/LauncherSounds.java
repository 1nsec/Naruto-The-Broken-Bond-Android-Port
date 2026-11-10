package org.brokenbond.port;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.SoundPool;

/**
 * The launcher's sounds (res/raw, synthesized by tools/android/make-launcher-sounds.py):
 * a scroll unrolling when the launcher opens and for the screens it opens, a
 * paper flick for a choice, a wooden clack for the other taps and a kunai for
 * Play. Settings > Launcher sounds turns them off.
 */
final class LauncherSounds {
    static final int SCROLL = 0, FLICK = 1, CLACK = 2, KUNAI = 3;
    private static final int[] RESOURCES = {R.raw.ui_scroll, R.raw.ui_flick, R.raw.ui_clack, R.raw.ui_kunai};

    private final Context context;
    private final SoundPool pool;
    private final int[] ids = new int[RESOURCES.length];
    private final boolean[] loaded = new boolean[RESOURCES.length];
    private int pending = -1;  // A sound asked for before it loaded (the opening scroll).

    LauncherSounds(Context context) {
        this.context = context;
        pool = new SoundPool.Builder()
                .setMaxStreams(4)
                .setAudioAttributes(new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_GAME)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                        .build())
                .build();
        pool.setOnLoadCompleteListener((p, id, status) -> {
            for (int i = 0; i < ids.length; ++i) {
                if (ids[i] != id || status != 0) continue;
                loaded[i] = true;
                if (pending == i) {
                    pending = -1;
                    play(i);
                }
            }
        });
        for (int i = 0; i < RESOURCES.length; ++i) ids[i] = pool.load(context, RESOURCES[i], 1);
    }

    void play(int sound) {
        if (!Storage.prefs(context).getBoolean(Storage.KEY_LAUNCHER_SOUNDS, true)) return;
        if (!loaded[sound]) {
            pending = sound;
            return;
        }
        float volume = sound == KUNAI ? 0.9f : 0.7f;
        pool.play(ids[sound], volume, volume, 1, 0, 1f);
    }

    void release() {
        pool.release();
    }
}
