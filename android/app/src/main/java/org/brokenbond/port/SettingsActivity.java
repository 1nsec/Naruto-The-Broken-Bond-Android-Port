package org.brokenbond.port;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.SharedPreferences;
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

/**
 * The common settings of naruto.toml, plus the on-screen controller's. The
 * game's PC Options menu changes every setting in game; this screen covers
 * the ones worth setting before the first start on a phone.
 */
public class SettingsActivity extends Activity {
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
        toggle(R.string.setting_vsync, "vsync", true);
        toggle(R.string.setting_transparency_aa, "transparency_antialiasing", false);
        toggle(R.string.setting_async_shaders, "async_shaders", true);
        toggle(R.string.setting_show_fps, "show_fps", false);
        prefToggle(R.string.setting_performance_log, Storage.KEY_PERF_LOG, true);
        prefChoice(R.string.setting_gpu_driver, Storage.KEY_GPU_DRIVER,
                new String[] {getString(R.string.gpu_driver_auto), getString(R.string.gpu_driver_system),
                        getString(R.string.gpu_driver_turnip)},
                new String[] {"auto", "system", "turnip"}, "auto");
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
