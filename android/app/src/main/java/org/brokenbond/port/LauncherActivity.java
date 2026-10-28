package org.brokenbond.port;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.Button;
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
    private static final int PICK_ISO = 1;
    private static final long MIN_ISO_BYTES = 1L << 30;  // The game's disc image is about 7 GB.

    private TextView status;
    private Button play;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_launcher);
        status = findViewById(R.id.status);
        play = findViewById(R.id.play);
        findViewById(R.id.pick_iso).setOnClickListener(v -> pickIso());
        play.setOnClickListener(v -> startGame());
        findViewById(R.id.settings).setOnClickListener(v -> startActivity(new Intent(this, SettingsActivity.class)));
        findViewById(R.id.help).setOnClickListener(v -> showHelp());
        findViewById(R.id.export_logs).setOnClickListener(v -> exportLogs());
        ((TextView) findViewById(R.id.version)).setText(getString(R.string.version_line, versionName()));
        GameConfig.ensureDefaults(this);
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
        refresh();
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
        if (uri != null) {
            status.setText(getString(R.string.iso_selected, prefs.getString(Storage.KEY_ISO_NAME, "?")));
            play.setEnabled(true);
        } else if (local != null) {
            status.setText(getString(R.string.iso_found, local.getName()));
            play.setEnabled(true);
        } else {
            status.setText(getString(R.string.iso_missing, Storage.assetsDir(this).getAbsolutePath()));
            play.setEnabled(false);
        }
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
}
