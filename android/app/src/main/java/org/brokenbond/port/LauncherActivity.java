package org.brokenbond.port;

import android.animation.ObjectAnimator;
import android.animation.PropertyValuesHolder;
import android.animation.ValueAnimator;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;

/**
 * The launcher: the game's disc image, the settings, and Play.
 *
 * <p>The ISO is picked with the system file picker (the app keeps a read
 * permission to it, no storage permission needed), or copied into the data
 * folder's assets/ folder, where the game finds it on its own as on PC.
 */
public class LauncherActivity extends Activity {
    private static final int PICK_ISO = 1, PICK_DLC = 2;
    private static final long MIN_ISO_BYTES = 1L << 30;  // The game's disc image is about 7 GB.
    // The project's page: new versions (Releases) and problem reports (Issues).
    private static final String GITHUB_URL = "https://github.com/1nsec/Naruto-The-Broken-Bond-Android-Port";
    // Who made the Android port (the credit line under the logo).
    private static final String AUTHOR_URL = "https://github.com/1nsec";
    private static final String YOUTUBE_URL = "https://www.youtube.com/@1nsec";
    private static final String INSTAGRAM_URL = "https://www.instagram.com/1nsecore";

    private TextView disc;
    private Button play;
    private boolean hasDisc;
    private GameConfig config;
    // The quick settings of the right panel (naruto.toml, as Settings writes them).
    private SegmentedControl preset, resolution, screen, frameRate;
    private static final String[] RESOLUTIONS = {"0.5", "0.75", "1.0", "1.5"};
    // The launcher's motion besides the background: the logo floats, Play breathes and shines.
    private final java.util.List<ValueAnimator> loops = new java.util.ArrayList<>();
    private LauncherSounds sounds;
    private LauncherMusic music;
    private EffectsView effects;
    private boolean starting;  // Play was tapped: the game starts once the kunai land.
    private final Runnable launch = this::startGame;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_launcher);
        sounds = new LauncherSounds(this);
        music = new LauncherMusic(this);
        setVolumeControlStream(android.media.AudioManager.STREAM_MUSIC);
        effects = findViewById(R.id.effects);
        disc = findViewById(R.id.pick_iso);
        play = findViewById(R.id.play);
        disc.setOnClickListener(v -> {
            sounds.play(LauncherSounds.CLACK);
            pickIso();
        });
        play.setOnClickListener(v -> {
            if (starting) return;
            if (!hasDisc) {
                sounds.play(LauncherSounds.CLACK);
                pickIso();
                return;
            }
            // The call and the drum, kunai and shuriken thrown at Play; the game once they land.
            starting = true;
            sounds.play(LauncherSounds.PLAY);
            effects.volley(play);
            music.fadeOut();
            play.postDelayed(launch, 900);
        });
        findViewById(R.id.credit).setOnClickListener(v -> open(AUTHOR_URL));
        findViewById(R.id.youtube).setOnClickListener(v -> {
            sounds.play(LauncherSounds.CLACK);
            open(YOUTUBE_URL);
        });
        findViewById(R.id.instagram).setOnClickListener(v -> {
            sounds.play(LauncherSounds.CLACK);
            open(INSTAGRAM_URL);
        });
        ((TextView) findViewById(R.id.version)).setText(getString(R.string.version_line, versionName()));
        GameConfig.ensureDefaults(this);
        config = GameConfig.load(this);
        buildPanel();
        if (savedInstanceState == null) animateIn();
        if (!getPackageManager().hasSystemFeature(PackageManager.FEATURE_VULKAN_HARDWARE_VERSION, 0x401000)) {
            new AlertDialog.Builder(this)
                    .setTitle(R.string.no_vulkan_title)
                    .setMessage(R.string.no_vulkan_message)
                    .setPositiveButton(android.R.string.ok, null)
                    .show();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
        config = GameConfig.load(this);  // Settings may have changed it.
        refresh();
        showChoices();
        startLoops();
        starting = false;
        music.start();
    }

    @Override
    protected void onDestroy() {
        sounds.release();
        music.release();
        super.onDestroy();
    }

    @Override
    protected void onPause() {
        for (ValueAnimator loop : loops) loop.cancel();
        loops.clear();
        music.pause();
        play.removeCallbacks(launch);  // Left before the kunai landed: no game in the background.
        starting = false;
        super.onPause();
    }

    /** The left column slides in from the left, the cards of the panel one after another from the right. */
    private void animateIn() {
        sounds.play(LauncherSounds.SCROLL);  // The panel unrolls.
        View left = findViewById(R.id.left);
        left.setAlpha(0f);
        left.setTranslationX(-dp(40));
        left.animate().alpha(1f).translationX(0f).setDuration(500).setStartDelay(80)
                .setInterpolator(new android.view.animation.DecelerateInterpolator(1.6f)).start();
        LinearLayout panel = findViewById(R.id.panel);
        for (int i = 0; i < panel.getChildCount(); ++i) {
            View child = panel.getChildAt(i);
            child.setAlpha(0f);
            child.setTranslationX(dp(48));
            child.animate().alpha(1f).translationX(0f).setDuration(450).setStartDelay(160 + 70L * i)
                    .setInterpolator(new android.view.animation.DecelerateInterpolator(1.6f)).start();
        }
    }

    /** The loops of the logo and of Play, while the launcher is on screen. */
    private void startLoops() {
        for (ValueAnimator loop : loops) loop.cancel();
        loops.clear();
        View logo = findViewById(R.id.logo);
        ObjectAnimator floating = ObjectAnimator.ofFloat(logo, View.TRANSLATION_Y, -dp(4), dp(4));
        floating.setDuration(2600);
        floating.setRepeatCount(ValueAnimator.INFINITE);
        floating.setRepeatMode(ValueAnimator.REVERSE);
        floating.setInterpolator(new android.view.animation.AccelerateDecelerateInterpolator());
        loops.add(floating);
        ObjectAnimator breathing = ObjectAnimator.ofPropertyValuesHolder(play,
                PropertyValuesHolder.ofFloat(View.SCALE_X, 1f, 1.035f), PropertyValuesHolder.ofFloat(View.SCALE_Y, 1f, 1.035f));
        breathing.setDuration(1300);
        breathing.setRepeatCount(ValueAnimator.INFINITE);
        breathing.setRepeatMode(ValueAnimator.REVERSE);
        breathing.setInterpolator(new android.view.animation.AccelerateDecelerateInterpolator());
        loops.add(breathing);
        // A band of light crossing Play every few seconds.
        ShineDrawable shine = new ShineDrawable();
        play.setForeground(shine);
        ValueAnimator sweep = ValueAnimator.ofFloat(-0.4f, 1.8f);
        sweep.setDuration(3200);
        sweep.setRepeatCount(ValueAnimator.INFINITE);
        sweep.addUpdateListener(a -> shine.setPosition((float) a.getAnimatedValue()));
        loops.add(sweep);
        for (ValueAnimator loop : loops) loop.start();
    }

    /**
     * The right panel: the graphics preset, the render resolution, the screen
     * and the frame rate in cards, and the shortcut tiles (Settings, DLC, Help,
     * logs, GitHub). A choice is saved to naruto.toml at once.
     */
    private void buildPanel() {
        LinearLayout panel = findViewById(R.id.panel);
        preset = new SegmentedControl(this, new String[] {getString(R.string.preset_performance),
                getString(R.string.preset_balanced), getString(R.string.preset_quality)}, null, (index, option) -> {
            poof(option);
            Presets.apply(config, index);
            save();
            showChoices();
        });
        panel.addView(card(R.string.setting_preset, preset), cardParams());
        resolution = new SegmentedControl(this, new String[] {"360p", "540p", "720p", "1080p"},
                new String[] {getString(R.string.res_fast), getString(R.string.res_light),
                        getString(R.string.res_original), getString(R.string.res_sharp)}, (index, option) -> {
            poof(option);
            config.setRaw("internal_resolution_scale", RESOLUTIONS[index]);
            save();
            showChoices();
        });
        panel.addView(card(R.string.setting_resolution, resolution), cardParams());
        screen = new SegmentedControl(this, new String[] {getString(R.string.screen_169), getString(R.string.screen_full)},
                null, (index, option) -> {
            poof(option);
            config.setRaw("aspect_ratio", index == 0 ? "\"16:9\"" : "\"auto\"");
            save();
        });
        frameRate = new SegmentedControl(this, new String[] {"30 FPS", "60 FPS"}, null, (index, option) -> {
            poof(option);
            config.setInt("frame_rate", index == 0 ? 30 : 60);
            save();
        });
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams left = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        left.setMarginEnd(dp(8));
        row.addView(card(R.string.setting_aspect, screen), left);
        row.addView(card(R.string.setting_frame_rate, frameRate),
                new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        panel.addView(row, cardParams());
        LinearLayout tiles = new LinearLayout(this);
        tiles.setOrientation(LinearLayout.HORIZONTAL);
        // The tiles that open a screen go up in smoke first.
        tile(tiles, R.drawable.ic_touch_options, R.string.tile_settings, true,
                v -> startActivity(new Intent(this, SettingsActivity.class)));
        tile(tiles, R.drawable.ic_dlc, R.string.tile_dlc, true, v -> showDlc());
        tile(tiles, R.drawable.ic_help, R.string.tile_help, true, v -> showHelp());
        tile(tiles, R.drawable.ic_logs, R.string.tile_logs, false, v -> exportLogs());
        tile(tiles, R.drawable.ic_code, R.string.tile_github, false, v -> open(GITHUB_URL));
        panel.addView(tiles, cardParams());
    }

    /** The quick settings as naruto.toml has them. */
    private void showChoices() {
        if (preset == null) return;
        preset.setChosen(Presets.current(config));
        double scale = config.getDouble("internal_resolution_scale", 1.0);
        int chosen = -1;
        for (int i = 0; i < RESOLUTIONS.length; ++i) if (scale == Double.parseDouble(RESOLUTIONS[i])) chosen = i;
        resolution.setChosen(chosen);
        screen.setChosen("auto".equals(config.getString("aspect_ratio", "16:9")) ? 1 : 0);
        frameRate.setChosen(config.getDouble("frame_rate", 30) >= 60 ? 1 : 0);
    }

    /** A puff of smoke over what was tapped, and its sound. */
    private void poof(View around) {
        sounds.play(LauncherSounds.POOF);
        effects.puff(around);
    }

    private void save() {
        if (!config.save()) Toast.makeText(this, R.string.settings_save_failed, Toast.LENGTH_LONG).show();
    }

    private LinearLayout card(int title, View content) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackgroundResource(R.drawable.bg_card);
        card.setPadding(dp(12), dp(6), dp(12), dp(8));
        TextView label = new TextView(this);
        label.setText(title);
        label.setTextColor(0xFF8E1B10);  // Red ink on the parchment.
        label.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        label.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        label.setPadding(dp(2), 0, 0, dp(4));
        card.addView(label);
        card.addView(content);
        return card;
    }

    private LinearLayout.LayoutParams cardParams() {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(6);
        return params;
    }

    private void tile(LinearLayout row, int icon, int label, boolean smoke, View.OnClickListener action) {
        LinearLayout tile = new LinearLayout(this);
        tile.setOrientation(LinearLayout.VERTICAL);
        tile.setGravity(Gravity.CENTER);
        tile.setBackgroundResource(R.drawable.bg_tile);
        tile.setPadding(dp(4), dp(7), dp(4), dp(6));
        tile.setClickable(true);
        tile.setFocusable(true);
        tile.setOnClickListener(v -> {
            if (!smoke) {
                sounds.play(LauncherSounds.CLACK);
                action.onClick(v);
                return;
            }
            poof(v);
            v.postDelayed(() -> action.onClick(v), 220);  // Long enough to see the smoke.
        });
        tile.setOnTouchListener((v, event) -> {
            if (event.getActionMasked() == android.view.MotionEvent.ACTION_DOWN)
                v.animate().scaleX(0.94f).scaleY(0.94f).setDuration(90).start();
            else if (event.getActionMasked() == android.view.MotionEvent.ACTION_UP
                    || event.getActionMasked() == android.view.MotionEvent.ACTION_CANCEL)
                v.animate().scaleX(1f).scaleY(1f).setDuration(140).start();
            return false;
        });
        ImageView image = new ImageView(this);
        image.setImageResource(icon);
        image.setColorFilter(0xFF4A3420);
        tile.addView(image, new LinearLayout.LayoutParams(dp(26), dp(26)));
        TextView text = new TextView(this);
        text.setText(label);
        text.setTextColor(0xFF4A3420);
        text.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        text.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        text.setSingleLine(true);
        text.setPadding(0, dp(4), 0, 0);
        tile.addView(text);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        if (row.getChildCount() > 0) params.setMarginStart(dp(6));
        row.addView(tile, params);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private String versionName() {
        try {
            return getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
        } catch (PackageManager.NameNotFoundException e) {
            return "?";
        }
    }

    private void refresh() {
        SharedPreferences prefs = Storage.prefs(this);
        File local = Storage.localIso(this);
        String uri = prefs.getString(Storage.KEY_ISO_URI, null);
        if (uri != null && !hasPermission(Uri.parse(uri))) {
            prefs.edit().remove(Storage.KEY_ISO_URI).remove(Storage.KEY_ISO_NAME).apply();
            uri = null;
        }
        hasDisc = uri != null || local != null;
        if (uri != null) {
            disc.setText(getString(R.string.disc_selected, prefs.getString(Storage.KEY_ISO_NAME, "?")));
        } else if (local != null) {
            disc.setText(getString(R.string.disc_found, local.getName()));
        } else {
            disc.setText(R.string.disc_missing);
        }
        play.setText(hasDisc ? R.string.play : R.string.play_pick);
    }

    private boolean hasPermission(Uri uri) {
        for (android.content.UriPermission p : getContentResolver().getPersistedUriPermissions()) {
            if (p.getUri().equals(uri) && p.isReadPermission()) return true;
        }
        return false;
    }

    private void pickIso() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
                "application/x-iso9660-image", "application/octet-stream", "application/x-cd-image", "*/*"});
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(intent, PICK_ISO);
        } catch (Exception e) {
            Toast.makeText(this, R.string.no_file_picker, Toast.LENGTH_LONG).show();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == PICK_DLC && resultCode == RESULT_OK && data != null) {
            addDlc(data);
            return;
        }
        if (requestCode != PICK_ISO || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException e) {
            Toast.makeText(this, R.string.iso_no_permission, Toast.LENGTH_LONG).show();
            return;
        }
        String name = Storage.displayName(this, uri);
        long size = Storage.size(this, uri);
        Storage.prefs(this).edit()
                .putString(Storage.KEY_ISO_URI, uri.toString())
                .putString(Storage.KEY_ISO_NAME, name)
                .apply();
        if (!name.toLowerCase().endsWith(".iso") || (size >= 0 && size < MIN_ISO_BYTES)) {
            new AlertDialog.Builder(this)
                    .setTitle(R.string.iso_check_title)
                    .setMessage(getString(R.string.iso_check_message, name))
                    .setPositiveButton(android.R.string.ok, null)
                    .show();
        }
        refresh();
    }

    private void startGame() {
        // Saved settings and the chosen disc go to the game process through the
        // shared preferences and the data folder.
        startActivity(new Intent(this, GameActivity.class));
    }

    /** Saves the logs to Downloads (problem reports), off the UI thread. */
    private void exportLogs() {
        Toast.makeText(this, R.string.export_logs_working, Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            String message;
            try {
                message = getString(R.string.export_logs_done, LogExport.export(this));
            } catch (Exception e) {
                message = getString(R.string.export_logs_failed, String.valueOf(e.getMessage()));
            }
            final String text = message;
            runOnUiThread(() -> new AlertDialog.Builder(this)
                    .setTitle(R.string.export_logs)
                    .setMessage(text)
                    .setPositiveButton(android.R.string.ok, null)
                    .show());
        }, "export-logs").start();
    }

    private void showHelp() {
        new AlertDialog.Builder(this)
                .setTitle(R.string.help_title)
                .setMessage(getString(R.string.help_message, Storage.dataDir(this).getAbsolutePath()))
                .setPositiveButton(android.R.string.ok, null)
                .show();
    }

    private void hideSystemBars() {
        View decor = getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = decor.getWindowInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        }
    }

    /** A web page in the browser (the project's GitHub page, its author's). */
    private void open(String url) {
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
        } catch (android.content.ActivityNotFoundException e) {
            Toast.makeText(this, url, Toast.LENGTH_LONG).show();
        }
    }

    /** The downloadable content: what is installed or waits for the next start, add, remove. */
    private void showDlc() {
        StringBuilder text = new StringBuilder(getString(R.string.dlc_message));
        java.util.List<DlcPackages.Package> packages = DlcPackages.list(this);
        text.append("\n\n");
        if (packages.isEmpty()) text.append(getString(R.string.dlc_none));
        for (DlcPackages.Package p : packages)
            text.append("• ").append(p.name).append(p.installed ? "" : " " + getString(R.string.dlc_pending)).append('\n');
        AlertDialog.Builder dialog = new AlertDialog.Builder(this)
                .setTitle(R.string.dlc)
                .setMessage(text.toString().trim())
                .setPositiveButton(R.string.dlc_add, (d, w) -> {
                    Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                    intent.addCategory(Intent.CATEGORY_OPENABLE);
                    intent.setType("*/*");
                    intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
                    try {
                        startActivityForResult(intent, PICK_DLC);
                    } catch (Exception e) {
                        Toast.makeText(this, R.string.no_file_picker, Toast.LENGTH_LONG).show();
                    }
                })
                .setNegativeButton(android.R.string.cancel, null);
        if (!packages.isEmpty()) {
            dialog.setNeutralButton(R.string.dlc_remove_all, (d, w) -> new AlertDialog.Builder(this)
                    .setMessage(R.string.dlc_remove_confirm)
                    .setPositiveButton(android.R.string.ok, (d2, w2) -> DlcPackages.removeAll(this))
                    .setNegativeButton(android.R.string.cancel, null)
                    .show());
        }
        dialog.show();
    }

    /** Copies the picked packages into dlc/, off the UI thread (they can be large). */
    private void addDlc(Intent data) {
        java.util.List<Uri> uris = new java.util.ArrayList<>();
        if (data.getClipData() != null) {
            for (int i = 0; i < data.getClipData().getItemCount(); ++i) uris.add(data.getClipData().getItemAt(i).getUri());
        } else if (data.getData() != null) {
            uris.add(data.getData());
        }
        if (uris.isEmpty()) return;
        Toast.makeText(this, R.string.dlc_copying, Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            StringBuilder report = new StringBuilder();
            for (Uri uri : uris) {
                String file = Storage.displayName(this, uri);
                try {
                    String name = DlcPackages.add(this, uri);
                    report.append(getString(R.string.dlc_added, name)).append('\n');
                } catch (Exception e) {
                    report.append(getString(R.string.dlc_add_failed, file, e.getMessage())).append('\n');
                }
            }
            runOnUiThread(() -> new AlertDialog.Builder(this)
                    .setTitle(R.string.dlc)
                    .setMessage(report.toString().trim())
                    .setPositiveButton(android.R.string.ok, null)
                    .show());
        }, "dlc-copy").start();
    }
}
