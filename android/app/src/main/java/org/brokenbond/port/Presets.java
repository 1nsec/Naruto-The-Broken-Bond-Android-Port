package org.brokenbond.port;

/**
 * The graphics presets of the launcher and of Settings: Performance, Balanced
 * and Quality set the graphics options of naruto.toml at once (the other
 * settings stay as they are).
 */
final class Presets {
    static final int PERFORMANCE = 0, BALANCED = 1, QUALITY = 2;

    // Render resolution, anti-aliasing, edge smoothing, upscaling, texture filtering.
    private static final String[] KEYS = {"internal_resolution_scale", "msaa_samples", "transparency_antialiasing",
            "swap_post_effect", "upscaling", "anisotropic_override"};
    private static final String[][] VALUES = {
            {"0.75", "1", "false", "\"none\"", "\"bilinear\"", "0"},
            {"1.0", "1", "false", "\"none\"", "\"bilinear\"", "2"},
            {"1.5", "1", "false", "\"fxaa\"", "\"bilinear\"", "4"}};

    private Presets() {}

    static void apply(GameConfig config, int preset) {
        for (int i = 0; i < KEYS.length; ++i) config.setRaw(KEYS[i], VALUES[preset][i]);
        config.setBool("water_reflections", preset != PERFORMANCE);  // About a sixth of the open world's draws.
        config.setBool("character_shadows", preset != PERFORMANCE);
        // Draw distance: short in Performance, medium in Balanced, the game's own in Quality.
        config.setInt("draw_distance", preset == PERFORMANCE ? 60 : preset == BALANCED ? 120 : 0);
        config.setInt("frame_rate", 30);
        config.setBool("async_shaders", true);
    }

    /** The preset the settings match (render resolution and draw distance), or -1. */
    static int current(GameConfig config) {
        double scale = config.getDouble("internal_resolution_scale", 1.0);
        double distance = config.getDouble("draw_distance", 0);
        for (int preset = PERFORMANCE; preset <= QUALITY; ++preset) {
            double want = preset == PERFORMANCE ? 60 : preset == BALANCED ? 120 : 0;
            if (scale == Double.parseDouble(VALUES[preset][0]) && distance == want) return preset;
        }
        return -1;
    }
}
