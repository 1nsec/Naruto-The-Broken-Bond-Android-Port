package org.brokenbond.port;

import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

import java.io.File;

/**
 * Where the port keeps its files, shared by the launcher and the game process.
 *
 * <p>The data folder is the app's external files folder
 * (Android/data/org.brokenbond.port/files). It plays the role of the folder of
 * the Windows executable: naruto.toml (settings), hdd/ (saves), logs/ and
 * caches live there, and an ISO copied into its assets/ folder is found
 * without picking it, as on PC.
 */
final class Storage {
    static final String PREFS = "port";
    static final String KEY_ISO_URI = "iso_uri";
    static final String KEY_ISO_NAME = "iso_name";
    static final String KEY_TOUCH = "touch_controls";
    static final String KEY_TOUCH_OPACITY = "touch_opacity";
    static final String KEY_TOUCH_SCALE = "touch_scale";
    static final String KEY_GPU_DRIVER = "gpu_driver";  // auto, system or turnip
    static final String KEY_PERF_LOG = "performance_log";  // On by default while the port is an alpha.
    static final String KEY_TURNIP_MODE = "turnip_mode";  // auto, sysmem or gmem (TU_DEBUG)

    private Storage() {}

    static File dataDir(Context context) {
        File dir = context.getExternalFilesDir(null);
        if (dir == null) dir = context.getFilesDir();  // No shared storage mounted.
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
        return dir;
    }

    static File assetsDir(Context context) {
        File dir = new File(dataDir(context), "assets");
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
        return dir;
    }

    static SharedPreferences prefs(Context context) {
        // Multi-process: the launcher writes, the game process reads at start.
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    /** An ISO copied into the data folder's assets/ (or the data folder itself), if any. */
    static File localIso(Context context) {
        for (File dir : new File[] {assetsDir(context), dataDir(context)}) {
            File[] files = dir.listFiles();
            if (files == null) continue;
            for (File f : files) {
                if (f.isFile() && f.getName().toLowerCase().endsWith(".iso")) return f;
            }
        }
        return null;
    }

    static String displayName(Context context, Uri uri) {
        try (Cursor c = context.getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME},
                null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Exception ignored) {
        }
        String path = uri.getLastPathSegment();
        return path != null ? path : uri.toString();
    }

    static long size(Context context, Uri uri) {
        try (Cursor c = context.getContentResolver().query(uri, new String[] {OpenableColumns.SIZE},
                null, null, null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0)) return c.getLong(0);
        } catch (Exception ignored) {
        }
        return -1;
    }
}
