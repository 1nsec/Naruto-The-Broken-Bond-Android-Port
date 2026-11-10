package org.brokenbond.port;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/**
 * The common settings of naruto.toml, plus the on-screen controller's. The
 * game's PC Options menu changes every setting in game; this screen covers
 * the ones worth setting before the first start on a phone.
 */
public class SettingsActivity extends Activity {
    private static final int PICK_DRIVER = 1;
    private GameConfig config;
    private LinearLayout list;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        config = GameConfig.load(this);
        ScrollView scroll = new ScrollView(this);
        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(16);
        list.setPadding(pad, pad, pad, pad);
        scroll.addView(list);
        setContentView(scroll);
        build();
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (!config.save()) Toast.makeText(this, R.string.settings_save_failed, Toast.LENGTH_LONG).show();
    }

    private int dp(int value) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, getResources().getDisplayMetrics()));
    }

    private void header(int text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        view.setTextColor(0xFFF28C28);
        view.setPadding(0, dp(18), 0, dp(6));
        list.addView(view);
    }

    private LinearLayout row(int label) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(6), 0, dp(6));
        TextView text = new TextView(this);
        text.setText(label);
        text.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        row.addView(text, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        list.addView(row);
        return row;
    }

    /** A choice among labels, stored as the matching raw TOML value. */
    private void choice(int label, String key, String[] labels, String[] values, String fallback) {
        LinearLayout row = row(label);
        Spinner spinner = new Spinner(this);
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, labels);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter);
        String current = config.raw(key);
        if (current == null) current = fallback;
        int selected = 0;
        for (int i = 0; i < values.length; ++i) {
            if (sameValue(values[i], current)) selected = i;
        }
        spinner.setSelection(selected);
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                config.setRaw(key, values[position]);
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {}
        });
        row.addView(spinner, new LinearLayout.LayoutParams(dp(220), ViewGroup.LayoutParams.WRAP_CONTENT));
    }

    /**
     * Performance, balanced and quality presets: the graphics settings of this
     * screen and PC Options at once (the screen is built again to show them).
     * The other settings stay as they are.
     */
    private void presets() {
        LinearLayout row = row(R.string.setting_preset);
        int[] labels = {R.string.preset_performance, R.string.preset_balanced, R.string.preset_quality};
        for (int i = 0; i < labels.length; ++i) {
            final int preset = i;
            Button button = new Button(this);
            button.setText(labels[i]);
            button.setAllCaps(false);
            button.setOnClickListener(v -> applyPreset(preset));
            row.addView(button);
        }
        TextView note = new TextView(this);
        note.setText(R.string.preset_note);
        note.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        list.addView(note);
    }

    private void applyPreset(int preset) {
        // Render resolution, anti-aliasing, edge smoothing, upscaling, texture filtering.
        String[][] values = {
                {"0.75", "1", "false", "\"none\"", "\"bilinear\"", "0"},
                {"1.0", "1", "false", "\"none\"", "\"bilinear\"", "2"},
                {"1.5", "1", "false", "\"fxaa\"", "\"bilinear\"", "4"}};
        String[] keys = {"internal_resolution_scale", "msaa_samples", "transparency_antialiasing", "swap_post_effect",
                "upscaling", "anisotropic_override"};
        for (int i = 0; i < keys.length; ++i) config.setRaw(keys[i], values[preset][i]);
        config.setBool("water_reflections", preset != 0);  // Off in Performance: about a sixth of the open world's draws.
        config.setBool("character_shadows", preset != 0);
        // Draw distance: short in Performance, medium in Balanced, the game's own in Quality.
        config.setInt("draw_distance", preset == 0 ? 60 : preset == 1 ? 120 : 0);
        config.setInt("frame_rate", 30);
        config.setBool("async_shaders", true);
        if (!config.save()) {
            Toast.makeText(this, R.string.settings_save_failed, Toast.LENGTH_LONG).show();
            return;
        }
        Toast.makeText(this, R.string.preset_applied, Toast.LENGTH_SHORT).show();
        recreate();
    }

    private static boolean sameValue(String a, String b) {
        String x = a.replace("\"", "").trim(), y = b.replace("\"", "").trim();
        try {
            return Double.parseDouble(x) == Double.parseDouble(y);
        } catch (NumberFormatException e) {
            return x.equalsIgnoreCase(y);
        }
    }

    private void toggle(int label, String key, boolean fallback) {
        LinearLayout row = row(label);
        Switch view = new Switch(this);
        view.setChecked(config.getBool(key, fallback));
        view.setOnCheckedChangeListener((button, checked) -> config.setBool(key, checked));
        row.addView(view);
    }

    private void prefToggle(int label, String key, boolean fallback) {
        LinearLayout row = row(label);
        Switch view = new Switch(this);
        SharedPreferences prefs = Storage.prefs(this);
        view.setChecked(prefs.getBoolean(key, fallback));
        view.setOnCheckedChangeListener((button, checked) -> prefs.edit().putBoolean(key, checked).apply());
        row.addView(view);
    }

    /** A choice among labels, stored as the matching string preference. */
    private void prefChoice(int label, String key, String[] labels, String[] values, String fallback) {
        LinearLayout row = row(label);
        Spinner spinner = new Spinner(this);
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, labels);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter);
        SharedPreferences prefs = Storage.prefs(this);
        String current = prefs.getString(key, fallback);
        for (int i = 0; i < values.length; ++i) {
            if (values[i].equals(current)) spinner.setSelection(i);
        }
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                prefs.edit().putString(key, values[position]).apply();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {}
        });
        row.addView(spinner, new LinearLayout.LayoutParams(dp(220), ViewGroup.LayoutParams.WRAP_CONTENT));
    }

    /** A float preference from min to max. */
    private void prefSlider(int label, String key, float fallback, float min, float max) {
        LinearLayout row = row(label);
        SeekBar bar = new SeekBar(this);
        SharedPreferences prefs = Storage.prefs(this);
        bar.setMax(100);
        bar.setProgress(Math.round((prefs.getFloat(key, fallback) - min) / (max - min) * 100));
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (fromUser) prefs.edit().putFloat(key, min + (max - min) * progress / 100f).apply();
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {}

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        row.addView(bar, new LinearLayout.LayoutParams(dp(220), ViewGroup.LayoutParams.WRAP_CONTENT));
    }

    /**
     * Settings > GPU driver: automatic, the system's, the bundled Turnip, or a
     * driver the player added (CustomDrivers: an adrenotools .zip), with the
     * buttons to add one and to remove the one chosen.
     */
    private void gpuDrivers() {
        List<String> labels = new ArrayList<>(), values = new ArrayList<>();
        labels.add(getString(R.string.gpu_driver_auto));
        values.add("auto");
        labels.add(getString(R.string.gpu_driver_system));
        values.add("system");
        labels.add(getString(R.string.gpu_driver_turnip));
        values.add("turnip");
        for (CustomDrivers.Driver driver : CustomDrivers.list(this)) {
            labels.add(getString(R.string.gpu_driver_custom, driver.label()));
            values.add(CustomDrivers.PREFIX + driver.id);
        }
        prefChoice(R.string.setting_gpu_driver, Storage.KEY_GPU_DRIVER, labels.toArray(new String[0]),
                values.toArray(new String[0]), "auto");
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        Button add = new Button(this);
        add.setText(R.string.gpu_driver_add);
        add.setAllCaps(false);
        add.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"application/zip", "application/x-zip-compressed",
                    "application/octet-stream"});
            try {
                startActivityForResult(intent, PICK_DRIVER);
            } catch (Exception e) {
                Toast.makeText(this, R.string.no_file_picker, Toast.LENGTH_LONG).show();
            }
        });
        row.addView(add);
        Button remove = new Button(this);
        remove.setText(R.string.gpu_driver_remove);
        remove.setAllCaps(false);
        remove.setOnClickListener(v -> {
            String chosen = Storage.prefs(this).getString(Storage.KEY_GPU_DRIVER, "auto");
            CustomDrivers.Driver driver = CustomDrivers.find(this, chosen);
            if (driver == null) {
                Toast.makeText(this, R.string.gpu_driver_remove_none, Toast.LENGTH_LONG).show();
                return;
            }
            new AlertDialog.Builder(this)
                    .setMessage(getString(R.string.gpu_driver_remove_confirm, driver.label()))
                    .setPositiveButton(android.R.string.ok, (d, w) -> {
                        CustomDrivers.remove(driver);
                        Storage.prefs(this).edit().putString(Storage.KEY_GPU_DRIVER, "auto").apply();
                        rebuild();
                    })
                    .setNegativeButton(android.R.string.cancel, null)
                    .show();
        });
        row.addView(remove);
        list.addView(row);
        TextView note = new TextView(this);
        note.setText(R.string.gpu_driver_note);
        note.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        list.addView(note);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_DRIVER || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        Toast.makeText(this, R.string.gpu_driver_installing, Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            String message;
            try {
                CustomDrivers.Driver driver = CustomDrivers.install(this, uri);
                Storage.prefs(this).edit().putString(Storage.KEY_GPU_DRIVER, CustomDrivers.PREFIX + driver.id).apply();
                message = getString(R.string.gpu_driver_installed, driver.label());
            } catch (Exception e) {
                message = getString(R.string.gpu_driver_install_failed, e.getMessage());
            }
            final String text = message;
            runOnUiThread(() -> {
                Toast.makeText(this, text, Toast.LENGTH_LONG).show();
                rebuild();
            });
        }, "driver-install").start();
    }

    /** The screen built again with what is saved (a driver added or removed). */
    private void rebuild() {
        if (!config.save()) Toast.makeText(this, R.string.settings_save_failed, Toast.LENGTH_LONG).show();
        config = GameConfig.load(this);
        list.removeAllViews();
        build();
    }

    private void build() {
        TextView note = new TextView(this);
        note.setText(R.string.settings_note);
        list.addView(note);

        header(R.string.settings_graphics);
        presets();
        choice(R.string.setting_resolution, "internal_resolution_scale",
                new String[] {"360p (0.5x)", "450p (0.625x)", "540p (0.75x)", "720p (1x)", "900p (1.25x)",
                        "1080p (1.5x)"},
                new String[] {"0.5", "0.625", "0.75", "1.0", "1.25", "1.5"}, "1.0");
        choice(R.string.setting_frame_rate, "frame_rate",
                new String[] {"30", "60"}, new String[] {"30", "60"}, "30");
        choice(R.string.setting_msaa, "msaa_samples",
                new String[] {getString(R.string.off), "2x", "4x"}, new String[] {"1", "2", "4"}, "1");
        choice(R.string.setting_fxaa, "swap_post_effect",
                new String[] {getString(R.string.off), "FXAA", "FXAA (extreme)"},
                new String[] {"\"none\"", "\"fxaa\"", "\"fxaa_extreme\""}, "\"none\"");
        choice(R.string.setting_upscaling, "upscaling",
                new String[] {"Bilinear", "AMD CAS", "AMD FSR"},
                new String[] {"\"bilinear\"", "\"cas\"", "\"fsr\""}, "\"bilinear\"");
        // Full screen: the game draws a wider view (ultrawide.cpp, aspect_ratio auto) that fills the phone's screen.
        choice(R.string.setting_aspect, "aspect_ratio",
                new String[] {getString(R.string.aspect_original), getString(R.string.aspect_full)},
                new String[] {"\"16:9\"", "\"auto\""}, "\"16:9\"");
        toggle(R.string.setting_vsync, "vsync", true);
        toggle(R.string.setting_transparency_aa, "transparency_antialiasing", false);
        toggle(R.string.setting_water_reflections, "water_reflections", true);
        toggle(R.string.setting_character_shadows, "character_shadows", true);
        choice(R.string.setting_draw_distance, "draw_distance",
                new String[] {getString(R.string.draw_distance_full), getString(R.string.draw_distance_medium),
                        getString(R.string.draw_distance_short)},
                new String[] {"0", "120", "60"}, "0");
        toggle(R.string.setting_async_shaders, "async_shaders", true);
        toggle(R.string.setting_show_fps, "show_fps", false);
        prefToggle(R.string.setting_performance_log, Storage.KEY_PERF_LOG, true);
        gpuDrivers();
        prefChoice(R.string.setting_turnip_mode, Storage.KEY_TURNIP_MODE,
                new String[] {getString(R.string.turnip_mode_auto), getString(R.string.turnip_mode_sysmem),
                        getString(R.string.turnip_mode_gmem)},
                new String[] {"auto", "sysmem", "gmem"}, "auto");

        header(R.string.settings_game);
        choice(R.string.setting_language, "user_language",
                new String[] {"English", "Deutsch", "Français", "Español", "Italiano"},
                new String[] {"1", "3", "4", "5", "6"}, "1");
        toggle(R.string.setting_skip_intros, "skip_intros", false);
        toggle(R.string.setting_title_screen, "show_title_screen", true);
        toggle(R.string.setting_mute, "audio_mute", false);

        header(R.string.settings_touch);
        prefToggle(R.string.setting_touch, Storage.KEY_TOUCH, true);
        prefSlider(R.string.setting_touch_opacity, Storage.KEY_TOUCH_OPACITY, 0.85f, 0.15f, 1.0f);
        prefSlider(R.string.setting_touch_scale, Storage.KEY_TOUCH_SCALE, 1.0f, 0.7f, 1.4f);
        choice(R.string.setting_touch_camera, "touch_camera_sensitivity",
                new String[] {getString(R.string.sensitivity_low), getString(R.string.sensitivity_normal),
                        getString(R.string.sensitivity_high), getString(R.string.sensitivity_very_high)},
                new String[] {"0.5", "1.0", "1.5", "2.0"}, "1.0");

        header(R.string.settings_debug);
        toggle(R.string.setting_log_to_file, "log_to_file", true);

        Button reset = new Button(this);
        reset.setText(R.string.settings_reset);
        reset.setOnClickListener(v -> new AlertDialog.Builder(this)
                .setMessage(R.string.settings_reset_confirm)
                .setPositiveButton(android.R.string.ok, (d, w) -> {
                    //noinspection ResultOfMethodCallIgnored
                    new File(Storage.dataDir(this), GameConfig.FILE).delete();
                    config = GameConfig.load(this);
                    list.removeAllViews();
                    build();
                })
                .setNegativeButton(android.R.string.cancel, null)
                .show());
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(24);
        list.addView(reset, params);
    }
}
