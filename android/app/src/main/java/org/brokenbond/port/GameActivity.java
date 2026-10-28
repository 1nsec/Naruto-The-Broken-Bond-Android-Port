package org.brokenbond.port;

import android.app.AlertDialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.hardware.input.InputManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;
import android.view.InputDevice;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.File;

/**
 * The game: SDL3's activity running the recompiled game (libmain.so), with the
 * on-screen controller over it. It runs in its own process (":game").
 */
public class GameActivity extends SDLActivity implements InputManager.InputDeviceListener {
    private static final String TAG = "narutobb";
    private static final String TURNIP_LIBRARY = "libvulkan_freedreno.so";
    // The disc image stays open for the life of the process: the runtime maps it.
    private static ParcelFileDescriptor isoDescriptor;
    private static String gameDataRoot;

    private TouchControllerView touch;
    private InputManager inputManager;
    private boolean touchWanted;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        File data = Storage.dataDir(this);
        GameConfig.ensureDefaults(this);
        try {
            // The runtime's stand-in for the executable's folder, and a temp folder.
            Os.setenv("REX_ANDROID_DATA_DIR", data.getAbsolutePath(), true);
            Os.setenv("TMPDIR", getCacheDir().getAbsolutePath(), true);
            setUpGpuDriver();
        } catch (ErrnoException e) {
            Log.e(TAG, "setenv failed", e);
        }
        openDisc();
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        super.onCreate(savedInstanceState);
        if (mLayout == null) return;  // SDL could not start (missing libraries); it shows its own error.

        SharedPreferences prefs = Storage.prefs(this);
        touchWanted = prefs.getBoolean(Storage.KEY_TOUCH, true);
        touch = new TouchControllerView(this, prefs.getFloat(Storage.KEY_TOUCH_OPACITY, 0.85f),
                prefs.getFloat(Storage.KEY_TOUCH_SCALE, 1.0f));
        mLayout.addView(touch, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
        if (inputManager != null) inputManager.registerInputDeviceListener(this, new Handler(Looper.getMainLooper()));
        updateTouchVisibility();
    }

    /**
     * The Vulkan driver for the game (src/android/gpu_driver.cpp): the choice of
     * Settings, and the Turnip driver bundled in the APK's assets, copied to the
     * app's private storage where it can be loaded (once per installed APK).
     */
    private void setUpGpuDriver() throws ErrnoException {
        Os.setenv("BROKENBOND_GPU_DRIVER", Storage.prefs(this).getString(Storage.KEY_GPU_DRIVER, "auto"), true);
        Os.setenv("BROKENBOND_HOOK_DIR", getApplicationInfo().nativeLibraryDir, true);
        File folder = new File(getFilesDir(), "gpu/turnip");
        File driver = new File(folder, TURNIP_LIBRARY);
        File stamp = new File(folder, "installed-from");
        String apk = getApplicationInfo().sourceDir + ":" + new File(getApplicationInfo().sourceDir).lastModified();
        try {
            boolean current = driver.isFile() && stamp.isFile()
                    && apk.equals(new String(java.nio.file.Files.readAllBytes(stamp.toPath()), java.nio.charset.StandardCharsets.UTF_8));
            if (!current) {
                //noinspection ResultOfMethodCallIgnored
                folder.mkdirs();
                try (java.io.InputStream in = getAssets().open("gpu/" + TURNIP_LIBRARY);
                     java.io.FileOutputStream out = new java.io.FileOutputStream(driver)) {
                    byte[] buffer = new byte[256 * 1024];
                    int n;
                    while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
                }
                java.nio.file.Files.write(stamp.toPath(), apk.getBytes(java.nio.charset.StandardCharsets.UTF_8));
            }
            Os.setenv("BROKENBOND_DRIVER_DIR", folder.getAbsolutePath() + "/", true);
            Os.setenv("BROKENBOND_DRIVER_NAME", TURNIP_LIBRARY, true);
        } catch (java.io.IOException e) {
            // Not in this APK (a build without Turnip): the system driver only.
            Log.i(TAG, "No bundled Vulkan driver: " + e.getMessage());
            Os.unsetenv("BROKENBOND_DRIVER_DIR");
        }
    }

    /** Opens the picked disc image and hands it to the runtime as /proc/self/fd/N. */
    private void openDisc() {
        if (gameDataRoot != null) return;
        String uri = Storage.prefs(this).getString(Storage.KEY_ISO_URI, null);
        if (uri == null) return;  // An ISO in the data folder's assets/ is found by the game itself.
        try {
            isoDescriptor = getContentResolver().openFileDescriptor(Uri.parse(uri), "r");
            if (isoDescriptor != null) gameDataRoot = "/proc/self/fd/" + isoDescriptor.getFd();
        } catch (Exception e) {
            Log.e(TAG, "Could not open the disc image " + uri, e);
        }
    }

    @Override
    protected String[] getLibraries() {
        // libmain.so (the game) needs the runtime and SDL; the last one is the main library.
        return new String[] {"c++_shared", "SDL3", "rexruntime", "main"};
    }

    @Override
    protected String[] getArguments() {
        return gameDataRoot != null ? new String[] {"--game_data_root=" + gameDataRoot} : new String[0];
    }

    // The on-screen controller hides while a physical controller is connected.
    private boolean physicalPadConnected() {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device == null || device.isVirtual()) continue;
            int sources = device.getSources();
            if ((sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                    || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) {
                return true;
            }
        }
        return false;
    }

    private void updateTouchVisibility() {
        if (touch == null) return;
        boolean show = touchWanted && !physicalPadConnected();
        if (!show) touch.releaseAll();
        touch.setVisibility(show ? View.VISIBLE : View.GONE);
    }

    @Override
    public void onInputDeviceAdded(int deviceId) {
        updateTouchVisibility();
    }

    @Override
    public void onInputDeviceRemoved(int deviceId) {
        updateTouchVisibility();
    }

    @Override
    public void onInputDeviceChanged(int deviceId) {
        updateTouchVisibility();
    }

    @Override
    protected void onPause() {
        if (touch != null) touch.releaseAll();
        super.onPause();
    }

    @Override
    public void onBackPressed() {
        // The back gesture would end the game: ask first.
        new AlertDialog.Builder(this)
                .setTitle(R.string.app_name)
                .setItems(new CharSequence[] {
                        getString(R.string.menu_continue),
                        getString(touchWanted ? R.string.menu_hide_touch : R.string.menu_show_touch),
                        getString(R.string.menu_exit)}, (dialog, which) -> {
                    if (which == 1) {
                        touchWanted = !touchWanted;
                        Storage.prefs(this).edit().putBoolean(Storage.KEY_TOUCH, touchWanted).apply();
                        updateTouchVisibility();
                    } else if (which == 2) {
                        finish();
                    }
                })
                .show();
    }

    @Override
    protected void onDestroy() {
        if (inputManager != null) inputManager.unregisterInputDeviceListener(this);
        super.onDestroy();
        // SDL and the runtime keep static state: the game process ends with the activity.
        System.exit(0);
    }
}
