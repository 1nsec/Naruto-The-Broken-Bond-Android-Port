package org.brokenbond.port;

import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/**
 * The game's downloadable content (Xbox Live Marketplace packages).
 *
 * <p>The launcher copies a package the player picks into dlc/ in the data
 * folder after checking its header: an STFS package (magic CON, LIVE or PIRS)
 * of content type 0x00000002 for this game's title. At the next start the game
 * installs what waits there (src/pc/dlc_packages.cpp, the SDK's content
 * manager) into hdd/0000000000000000/55530825/00000002/&lt;file name&gt;/ and
 * deletes the copy. The display names of the packages are kept in the
 * preferences, by file name, for the list.
 */
final class DlcPackages {
    static final int TITLE_ID = 0x55530825;
    private static final int MARKETPLACE_CONTENT = 0x00000002;
    private static final String NAME_PREFIX = "dlc_name_";

    /** A package of the list: installed, or copied and waiting for the next start. */
    static final class Package {
        final String file, name;
        final boolean installed;
        final File path;

        Package(String file, String name, boolean installed, File path) {
            this.file = file;
            this.name = name;
            this.installed = installed;
            this.path = path;
        }
    }

    private DlcPackages() {}

    static File pending(Context context) {
        return new File(Storage.dataDir(context), "dlc");
    }

    static File installed(Context context) {
        return new File(Storage.dataDir(context),
                String.format(Locale.ROOT, "hdd/0000000000000000/%08X/%08X", TITLE_ID, MARKETPLACE_CONTENT));
    }

    static List<Package> list(Context context) {
        SharedPreferences prefs = Storage.prefs(context);
        List<Package> out = new ArrayList<>();
        File[] done = installed(context).listFiles(File::isDirectory);
        if (done != null) {
            Arrays.sort(done);
            for (File folder : done)
                out.add(new Package(folder.getName(), prefs.getString(NAME_PREFIX + folder.getName(), folder.getName()),
                        true, folder));
        }
        File[] waiting = pending(context).listFiles(f -> f.isFile() && !f.getName().startsWith("."));
        if (waiting != null) {
            Arrays.sort(waiting);
            for (File file : waiting)
                out.add(new Package(file.getName(), prefs.getString(NAME_PREFIX + file.getName(), file.getName()),
                        false, file));
        }
        return out;
    }

    /**
     * Copies the package at uri into dlc/ and returns its display name. Throws
     * with a message for the player when it is not this game's downloadable
     * content.
     */
    static String add(Context context, Uri uri) throws IOException {
        byte[] header = new byte[0x511];
        try (InputStream in = context.getContentResolver().openInputStream(uri)) {
            if (in == null) throw new IOException("cannot open");
            int read = 0;
            while (read < header.length) {
                int n = in.read(header, read, header.length - read);
                if (n < 0) break;
                read += n;
            }
            if (read < header.length) throw new IOException(context.getString(R.string.dlc_not_package));
        }
        String magic = new String(header, 0, 4, StandardCharsets.US_ASCII);
        if (!magic.equals("CON ") && !magic.equals("LIVE") && !magic.equals("PIRS"))
            throw new IOException(context.getString(R.string.dlc_not_package));
        if (be32(header, 0x344) != MARKETPLACE_CONTENT || be32(header, 0x360) != TITLE_ID)
            throw new IOException(context.getString(R.string.dlc_other_game));
        // The English display name: UTF-16 big endian, 128 characters, at 0x411.
        String name = new String(header, 0x411, 0x100, StandardCharsets.UTF_16BE);
        int end = name.indexOf('\0');
        if (end >= 0) name = name.substring(0, end);
        name = name.trim();
        // The installed folder takes the file's name (at most 40 characters for the game's
        // content data): the package's own name when it fits, otherwise one from its header.
        String file = Storage.displayName(context, uri);
        if (file == null || !file.matches("[A-Za-z0-9_.-]{1,40}")) {
            file = String.format(Locale.ROOT, "DLC_%08X", Arrays.hashCode(Arrays.copyOfRange(header, 0x344, 0x511)));
        }
        File folder = pending(context);
        if (!folder.isDirectory() && !folder.mkdirs()) throw new IOException("cannot create " + folder);
        File target = new File(folder, file), partial = new File(folder, "." + file + ".part");
        try (InputStream in = context.getContentResolver().openInputStream(uri);
             FileOutputStream out = new FileOutputStream(partial)) {
            if (in == null) throw new IOException("cannot open");
            byte[] buffer = new byte[1 << 20];
            int n;
            while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
        } catch (IOException e) {
            //noinspection ResultOfMethodCallIgnored
            partial.delete();
            throw e;
        }
        //noinspection ResultOfMethodCallIgnored
        target.delete();
        if (!partial.renameTo(target)) throw new IOException("cannot move the package into " + folder);
        Storage.prefs(context).edit().putString(NAME_PREFIX + file, name.isEmpty() ? file : name).apply();
        return name.isEmpty() ? file : name;
    }

    /** Removes every package, installed or waiting. */
    static void removeAll(Context context) {
        for (Package p : list(context)) CustomDrivers.deleteTree(p.path);
    }

    private static int be32(byte[] b, int at) {
        return (b[at] & 0xFF) << 24 | (b[at + 1] & 0xFF) << 16 | (b[at + 2] & 0xFF) << 8 | (b[at + 3] & 0xFF);
    }
}
