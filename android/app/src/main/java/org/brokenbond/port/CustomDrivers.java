package org.brokenbond.port;

import android.content.Context;
import android.net.Uri;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Vulkan drivers the player adds, next to the Turnip the app carries.
 *
 * <p>A driver is an adrenotools package, the .zip that Turnip builds for Android
 * emulators are published as: a meta.json ("name", "description",
 * "driverVersion", "libraryName") and the driver library it names. Installing
 * one keeps those two files in gpu/custom/&lt;id&gt;/ in the app's private storage,
 * where libadrenotools can load the library; GameActivity hands the folder and
 * the library's name to the game (src/android/gpu_driver.cpp) when Settings >
 * GPU driver names it ("custom:&lt;id&gt;").
 */
final class CustomDrivers {
    static final String PREFIX = "custom:";
    private static final long MAX_ENTRY_BYTES = 256L << 20;

    static final class Driver {
        final String id, name, version, library;
        final File folder;

        Driver(String id, String name, String version, String library, File folder) {
            this.id = id;
            this.name = name;
            this.version = version;
            this.library = library;
            this.folder = folder;
        }

        String label() {
            return version.isEmpty() ? name : name + " (" + version + ")";
        }
    }

    private CustomDrivers() {}

    static File root(Context context) {
        return new File(context.getFilesDir(), "gpu/custom");
    }

    /** The installed drivers, by name. */
    static List<Driver> list(Context context) {
        List<Driver> out = new ArrayList<>();
        File[] folders = root(context).listFiles(File::isDirectory);
        if (folders == null) return out;
        Arrays.sort(folders);
        for (File folder : folders) {
            Driver driver = read(folder);
            if (driver != null) out.add(driver);
        }
        return out;
    }

    /** The driver of a Settings value ("custom:<id>"), or null when it is not installed. */
    static Driver find(Context context, String value) {
        if (value == null || !value.startsWith(PREFIX)) return null;
        String id = value.substring(PREFIX.length());
        if (!safeName(id)) return null;
        return read(new File(root(context), id));
    }

    private static Driver read(File folder) {
        try {
            JSONObject meta = new JSONObject(new String(
                    java.nio.file.Files.readAllBytes(new File(folder, "meta.json").toPath()), StandardCharsets.UTF_8));
            String library = meta.optString("libraryName", "");
            if (!safeName(library) || !new File(folder, library).isFile()) return null;
            String name = meta.optString("name", folder.getName()).trim();
            return new Driver(folder.getName(), name.isEmpty() ? folder.getName() : name,
                    meta.optString("driverVersion", "").trim(), library, folder);
        } catch (IOException | JSONException e) {
            return null;
        }
    }

    /**
     * Installs the package at uri (replacing a driver of the same name) and
     * returns it. Only meta.json and the library it names are taken from the
     * zip, by their file names, so nothing is written outside the driver's folder.
     */
    static Driver install(Context context, Uri uri) throws IOException {
        byte[] meta = null;
        File staging = new File(root(context), ".installing");
        deleteTree(staging);
        if (!staging.mkdirs()) throw new IOException("cannot create " + staging);
        try {
            // The library may come before meta.json in the zip: every .so is kept until meta.json says which one.
            List<String> libraries = new ArrayList<>();
            try (InputStream raw = context.getContentResolver().openInputStream(uri);
                 ZipInputStream zip = new ZipInputStream(raw)) {
                ZipEntry entry;
                while ((entry = zip.getNextEntry()) != null) {
                    if (entry.isDirectory()) continue;
                    String name = new File(entry.getName()).getName();
                    if (name.equals("meta.json")) {
                        meta = readAll(zip);
                    } else if (name.endsWith(".so") && safeName(name)) {
                        copy(zip, new File(staging, name));
                        libraries.add(name);
                    }
                }
            }
            if (meta == null) throw new IOException("no meta.json (not an adrenotools driver package)");
            JSONObject json;
            try {
                json = new JSONObject(new String(meta, StandardCharsets.UTF_8));
            } catch (JSONException e) {
                throw new IOException("meta.json does not read: " + e.getMessage());
            }
            String library = json.optString("libraryName", "");
            if (!libraries.contains(library)) throw new IOException("the package has no " + library);
            for (String other : libraries) {
                //noinspection ResultOfMethodCallIgnored
                if (!other.equals(library)) new File(staging, other).delete();
            }
            try (FileOutputStream out = new FileOutputStream(new File(staging, "meta.json"))) {
                out.write(meta);
            }
            String name = json.optString("name", "").trim();
            String id = idOf(name.isEmpty() ? library : name);
            File folder = new File(root(context), id);
            deleteTree(folder);
            if (!staging.renameTo(folder)) throw new IOException("cannot move the driver to " + folder);
            Driver driver = read(folder);
            if (driver == null) throw new IOException("the installed driver does not read");
            return driver;
        } finally {
            deleteTree(staging);
        }
    }

    static void remove(Driver driver) {
        deleteTree(driver.folder);
    }

    private static String idOf(String name) {
        String id = name.toLowerCase(Locale.ROOT).replaceAll("[^a-z0-9._-]+", "-").replaceAll("^[-.]+|-+$", "");
        if (id.length() > 48) id = id.substring(0, 48);
        return id.isEmpty() ? "driver" : id;
    }

    private static boolean safeName(String name) {
        return name != null && !name.isEmpty() && name.length() <= 128 && !name.contains("/") && !name.contains("\\")
                && !name.equals(".") && !name.equals("..");
    }

    private static byte[] readAll(InputStream in) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[64 * 1024];
        int n;
        while ((n = in.read(buffer)) > 0) {
            out.write(buffer, 0, n);
            if (out.size() > 1 << 20) throw new IOException("meta.json is too large");
        }
        return out.toByteArray();
    }

    private static void copy(InputStream in, File file) throws IOException {
        long total = 0;
        try (FileOutputStream out = new FileOutputStream(file)) {
            byte[] buffer = new byte[256 * 1024];
            int n;
            while ((n = in.read(buffer)) > 0) {
                total += n;
                if (total > MAX_ENTRY_BYTES) throw new IOException(file.getName() + " is too large");
                out.write(buffer, 0, n);
            }
        }
    }

    static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteTree(child);
        //noinspection ResultOfMethodCallIgnored
        file.delete();
    }
}
