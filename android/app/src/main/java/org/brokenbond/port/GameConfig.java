package org.brokenbond.port;

import android.content.Context;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * naruto.toml, the game's settings file, in the data folder.
 *
 * <p>The file is a flat list of "key = value" lines with comments, as the PC
 * port writes it. This edits values in place and keeps everything else (the
 * game itself rewrites the file when PC Options changes something).
 */
final class GameConfig {
    static final String FILE = "naruto.toml";
    private static final Pattern LINE = Pattern.compile("^(\\s*)([A-Za-z0-9_]+)(\\s*=\\s*)(.*?)(\\s*)$");

    private final File file;
    private final List<String> lines = new ArrayList<>();

    private GameConfig(File file) {
        this.file = file;
    }

    /** Writes the Android defaults (assets/naruto.toml) when there is no settings file yet. */
    static void ensureDefaults(Context context) {
        File target = new File(Storage.dataDir(context), FILE);
        if (target.exists()) return;
        try (InputStream in = context.getAssets().open(FILE); FileOutputStream out = new FileOutputStream(target)) {
            byte[] buffer = new byte[8192];
            int n;
            while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
        } catch (IOException ignored) {
            // The game writes its own defaults when the file is missing.
        }
    }

    static GameConfig load(Context context) {
        ensureDefaults(context);
        GameConfig config = new GameConfig(new File(Storage.dataDir(context), FILE));
        try (FileInputStream in = new FileInputStream(config.file)) {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            int n;
            while ((n = in.read(buffer)) > 0) bytes.write(buffer, 0, n);
            String text = new String(bytes.toByteArray(), StandardCharsets.UTF_8).replace("\r\n", "\n");
            for (String line : text.split("\n", -1)) config.lines.add(line);
            if (!config.lines.isEmpty() && config.lines.get(config.lines.size() - 1).isEmpty()) {
                config.lines.remove(config.lines.size() - 1);
            }
        } catch (IOException ignored) {
        }
        return config;
    }

    /** The raw TOML value of key (quotes included for strings), or null. */
    String raw(String key) {
        for (String line : lines) {
            Matcher m = LINE.matcher(line);
            if (m.matches() && m.group(2).equals(key)) return m.group(4);
        }
        return null;
    }

    String getString(String key, String fallback) {
        String value = raw(key);
        if (value == null) return fallback;
        if (value.length() >= 2 && value.startsWith("\"") && value.endsWith("\"")) {
            return value.substring(1, value.length() - 1);
        }
        return value;
    }

    boolean getBool(String key, boolean fallback) {
        String value = raw(key);
        return value == null ? fallback : value.trim().equalsIgnoreCase("true");
    }

    double getDouble(String key, double fallback) {
        try {
            String value = raw(key);
            return value == null ? fallback : Double.parseDouble(value.replace("\"", "").trim());
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    void setRaw(String key, String value) {
        for (int i = 0; i < lines.size(); ++i) {
            Matcher m = LINE.matcher(lines.get(i));
            if (m.matches() && m.group(2).equals(key)) {
                lines.set(i, m.group(1) + key + m.group(3) + value);
                return;
            }
        }
        lines.add(key + " = " + value);
    }

    void setString(String key, String value) {
        setRaw(key, "\"" + value.replace("\\", "\\\\").replace("\"", "\\\"") + "\"");
    }

    void setBool(String key, boolean value) {
        setRaw(key, value ? "true" : "false");
    }

    /** A floating-point value, written as TOML floats are ("1.0", "1.5"). */
    void setDouble(String key, double value) {
        setRaw(key, Double.toString(value));
    }

    void setInt(String key, int value) {
        setRaw(key, Integer.toString(value));
    }

    boolean save() {
        StringBuilder text = new StringBuilder();
        for (String line : lines) text.append(line).append('\n');
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(text.toString().getBytes(StandardCharsets.UTF_8));
            return true;
        } catch (IOException e) {
            return false;
        }
    }
}
