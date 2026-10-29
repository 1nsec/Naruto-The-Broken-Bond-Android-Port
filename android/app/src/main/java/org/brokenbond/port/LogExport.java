package org.brokenbond.port;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.pm.FeatureInfo;
import android.net.Uri;
import android.os.Build;
import android.provider.MediaStore;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Comparator;
import java.util.Date;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * Saves the logs of the game to Downloads as a zip, for problem reports.
 *
 * <p>The data folder (Android/data/...) cannot be opened by file managers on
 * Android 11 and newer; Downloads can. The zip holds the device's details,
 * naruto.toml, the newest files of logs/ (game logs and crash reports), the
 * performance logs of the newest sessions (Settings > Performance log:
 * pipeline-runs/<time>/performance.csv) and the app's own system log
 * (logcat), which also has what happened before the game's log started.
 */
final class LogExport {
    private static final int MAX_LOG_FILES = 15;
    private static final int MAX_PERFORMANCE_RUNS = 3;

    private LogExport() {}

    /** Writes the zip and returns its file name in Downloads. */
    static String export(Context context) throws IOException {
        String name = "NarutoBrokenBond-logs-"
                + new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date()) + ".zip";
        ContentResolver resolver = context.getContentResolver();
        ContentValues values = new ContentValues();
        values.put(MediaStore.Downloads.DISPLAY_NAME, name);
        values.put(MediaStore.Downloads.MIME_TYPE, "application/zip");
        values.put(MediaStore.Downloads.IS_PENDING, 1);
        Uri uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
        if (uri == null) throw new IOException("Downloads is not available");
        try (OutputStream out = resolver.openOutputStream(uri)) {
            if (out == null) throw new IOException("Downloads is not writable");
            try (ZipOutputStream zip = new ZipOutputStream(out)) {
                addText(zip, "device.txt", deviceInfo(context));
                File data = Storage.dataDir(context);
                addFile(zip, new File(data, GameConfig.FILE), GameConfig.FILE);
                File[] logs = new File(data, "logs").listFiles(File::isFile);
                if (logs != null) {
                    Arrays.sort(logs, Comparator.<File>comparingLong(File::lastModified).reversed());
                    for (int i = 0; i < Math.min(logs.length, MAX_LOG_FILES); ++i) {
                        addFile(zip, logs[i], "logs/" + logs[i].getName());
                    }
                }
                File[] runs = new File(data, "pipeline-runs").listFiles(File::isDirectory);
                if (runs != null) {
                    Arrays.sort(runs, Comparator.<File>comparingLong(File::lastModified).reversed());
                    boolean images = false;
                    for (int i = 0, added = 0; i < runs.length && added < MAX_PERFORMANCE_RUNS; ++i) {
                        File csv = new File(runs[i], "performance.csv");
                        if (!csv.isFile()) continue;
                        addFile(zip, csv, "performance/" + runs[i].getName() + ".csv");
                        ++added;
                        // The surface images of the newest session that has them.
                        File[] surfaces = new File(runs[i], "surfaces").listFiles(File::isFile);
                        if (!images && surfaces != null && surfaces.length > 0) {
                            for (File image : surfaces) {
                                addFile(zip, image, "performance/" + runs[i].getName() + "-surfaces/" + image.getName());
                            }
                            images = true;
                        }
                    }
                }
                addText(zip, "logcat.txt", logcat());
            }
        } catch (IOException | RuntimeException e) {
            resolver.delete(uri, null, null);
            throw e;
        }
        values.clear();
        values.put(MediaStore.Downloads.IS_PENDING, 0);
        resolver.update(uri, values, null, null);
        return name;
    }

    private static String deviceInfo(Context context) {
        StringBuilder s = new StringBuilder();
        s.append("Device: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
                .append(" (").append(Build.DEVICE).append(")\n");
        s.append("Android: ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT).append(")\n");
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            s.append("SoC: ").append(Build.SOC_MANUFACTURER).append(' ').append(Build.SOC_MODEL).append('\n');
        }
        s.append("Hardware: ").append(Build.HARDWARE).append(", ABIs: ").append(String.join(",", Build.SUPPORTED_ABIS)).append('\n');
        for (FeatureInfo feature : context.getPackageManager().getSystemAvailableFeatures()) {
            if (feature.name != null && feature.name.startsWith("android.hardware.vulkan")) {
                s.append(feature.name).append(": 0x").append(Integer.toHexString(feature.version)).append('\n');
            }
        }
        try {
            s.append("App: ").append(context.getPackageManager().getPackageInfo(context.getPackageName(), 0).versionName)
                    .append('\n');
        } catch (Exception ignored) {
        }
        return s.toString();
    }

    /** The app's own log lines (both of its processes; apps only see their own). */
    private static String logcat() {
        try {
            Process process = new ProcessBuilder("logcat", "-d", "-v", "threadtime", "-t", "20000")
                    .redirectErrorStream(true).start();
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            try (InputStream in = process.getInputStream()) {
                copy(in, bytes);
            }
            process.waitFor();
            return bytes.toString(StandardCharsets.UTF_8.name());
        } catch (Exception e) {
            return "logcat failed: " + e;
        }
    }

    // InputStream.transferTo and readAllBytes need Android 13; this runs from Android 10.
    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buffer = new byte[64 * 1024];
        int n;
        while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
    }

    private static void addText(ZipOutputStream zip, String name, String text) throws IOException {
        zip.putNextEntry(new ZipEntry(name));
        zip.write(text.getBytes(StandardCharsets.UTF_8));
        zip.closeEntry();
    }

    private static void addFile(ZipOutputStream zip, File file, String name) throws IOException {
        if (!file.isFile()) return;
        zip.putNextEntry(new ZipEntry(name));
        try (InputStream in = new FileInputStream(file)) {
            copy(in, zip);
        }
        zip.closeEntry();
    }
}
