package org.brokenbond.port;

import android.app.AlertDialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.hardware.input.InputManager;
import android.net.Uri;
import android.os.Build;
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
import android.view.WindowInsets;
import android.view.WindowInsetsController;
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
    private static final int KEPT_PERFORMANCE_RUNS = 5;
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
        forceFullscreen();
        try {
            // The runtime's stand-in for the executable's folder, and a temp folder.
            Os.setenv("REX_ANDROID_DATA_DIR", data.getAbsolutePath(), true);
            Os.setenv("TMPDIR", getCacheDir().getAbsolutePath(), true);
            setUpGpuDriver();
            setUpShaderCaches();
            setUpPerformanceLog(data);
        } catch (ErrnoException e) {
            Log.e(TAG, "setenv failed", e);
        }
        openDisc();
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            // Edge to edge on phones with a camera cutout.
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
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
        // Settings > Turnip rendering: Turnip draws each render pass in tiles of
        // its on-chip memory (gmem) or straight to memory (sysmem); it picks per
        // pass unless told (TU_DEBUG, read only by Turnip).
        String mode = Storage.prefs(this).getString(Storage.KEY_TURNIP_MODE, "auto");
        if ("sysmem".equals(mode) || "gmem".equals(mode)) Os.setenv("TU_DEBUG", mode, true);
        else Os.unsetenv("TU_DEBUG");
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

    /**
     * The game always fills the screen on a phone: a windowed naruto.toml (the
     * PC's default, or PC Options > Display Mode) would leave the status and
     * navigation bars over a smaller window.
     */
    private void forceFullscreen() {
        GameConfig config = GameConfig.load(this);
        if (!config.getBool("fullscreen", false)) {
            config.setBool("fullscreen", true);
            config.save();
        }
    }

    /**
     * Compiled shaders kept between sessions, so an area stutters only the first
     * time it is played: the renderer's Vulkan pipeline cache and the list of the
     * pipelines drawn, which the next sessions prepare in the background from
     * the start (src/gpu/backend/vulkan/pipeline_cache.inc), and Mesa's own disk
     * cache for Turnip. All in the app's private storage, deleted with its data.
     */
    private void setUpShaderCaches() throws ErrnoException {
        File folder = new File(getFilesDir(), "shader-cache");
        File mesa = new File(folder, "mesa");
        //noinspection ResultOfMethodCallIgnored
        mesa.mkdirs();
        Os.setenv("BROKENBOND_PIPELINE_CACHE", new File(folder, "vulkan-pipelines.bin").getAbsolutePath(), true);
        Os.setenv("BROKENBOND_PIPELINE_LIST", new File(folder, "pipelines.list").getAbsolutePath(), true);
        Os.setenv("MESA_SHADER_CACHE_DIR", mesa.getAbsolutePath(), true);
        Os.setenv("MESA_SHADER_CACHE_MAX_SIZE", "1G", true);
    }

    /**
     * Settings > Performance log (on by default in the alphas): the game writes
     * one line per second of play to pipeline-runs/<time>/performance.csv
     * (src/gpu/diagnostics/perf_log.h), which Save logs includes. The newest
     * runs are kept.
     */
    private void setUpPerformanceLog(File data) throws ErrnoException {
        Os.unsetenv("REX_NATIVE_FRAME_DIR");
        Os.unsetenv("REX_NATIVE_PERF_LOG");
        if (!Storage.prefs(this).getBoolean(Storage.KEY_PERF_LOG, true)) return;
        File runs = new File(data, "pipeline-runs");
        File[] old = runs.listFiles(File::isDirectory);
        if (old != null && old.length >= KEPT_PERFORMANCE_RUNS) {
            java.util.Arrays.sort(old, java.util.Comparator.comparingLong(File::lastModified));
            for (int i = 0; i <= old.length - KEPT_PERFORMANCE_RUNS; ++i) deleteTree(old[i]);
        }
        File run = new File(runs, new java.text.SimpleDateFormat("yyyyMMdd-HHmmss", java.util.Locale.US)
                .format(new java.util.Date()));
        if (!run.mkdirs()) return;
        Os.setenv("REX_NATIVE_FRAME_DIR", run.getAbsolutePath(), true);
        Os.setenv("REX_NATIVE_PERF_LOG", "1", true);
        // The symbols of the game's libraries for its sampling profiler
        // (src/android/sampling_profiler.cpp), from the APK, once per APK.
        File symbols = new File(getFilesDir(), "symbols");
        File stamp = new File(symbols, "installed-from");
        String apk = getApplicationInfo().sourceDir + ":" + new File(getApplicationInfo().sourceDir).lastModified();
        try {
            boolean current = stamp.isFile() && apk.equals(new String(java.nio.file.Files.readAllBytes(stamp.toPath()),
                    java.nio.charset.StandardCharsets.UTF_8));
            if (!current) {
                String[] names = getAssets().list("symbols");
                //noinspection ResultOfMethodCallIgnored
                symbols.mkdirs();
                if (names != null) {
                    for (String name : names) {
                        try (java.io.InputStream in = getAssets().open("symbols/" + name);
                             java.io.FileOutputStream out = new java.io.FileOutputStream(new File(symbols, name))) {
                            byte[] buffer = new byte[256 * 1024];
                            int n;
                            while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
                        }
                    }
                }
                java.nio.file.Files.write(stamp.toPath(), apk.getBytes(java.nio.charset.StandardCharsets.UTF_8));
            }
            Os.setenv("BROKENBOND_SYMBOLS_DIR", symbols.getAbsolutePath(), true);
        } catch (java.io.IOException e) {
            Log.i(TAG, "No symbol files: " + e.getMessage());
            Os.unsetenv("BROKENBOND_SYMBOLS_DIR");
        }
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteTree(child);
        //noinspection ResultOfMethodCallIgnored
        file.delete();
    }

    /** Hides the status and navigation bars; a swipe from an edge shows them for a moment. */
    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
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
        if (!show) touch.detach();
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
