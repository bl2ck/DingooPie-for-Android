package com.dingoopie.android;

import android.annotation.SuppressLint;
import android.app.AlertDialog;
import android.content.BroadcastReceiver;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.ParcelFileDescriptor;
import android.os.SystemClock;
import android.provider.OpenableColumns;
import android.provider.DocumentsContract;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructStat;
import android.util.Base64;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.TextView;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

public final class DingooPieActivity extends SDLActivity {
    private enum GameImportOption {
        FILE,
        DIRECTORY
    }

    private static final String TAG = "DingooPie";
    private static final int REQUEST_GAME_FILE = 4108;
    private static final int REQUEST_GAME_DIRECTORY = 4109;
    private static final int REQUEST_GAME_FILE_DIRECTORY = 4110;
    private static final String GAME_LIBRARY_PREFERENCES = "game_library";
    private static final String GAME_LIBRARY_PATHS = "paths";
    private static final String GAME_LAST_RUN_TIMES = "last_run_times";
    private static final String GAME_SAVE_DIRECTORIES = "save_directories";
    private static final String GAME_LIBRARY_EXCLUDED_IDENTITIES = "excluded_identities";
    private static final String GAME_LIBRARY_DIRECTORIES = "library_directories";
    private static final String GAME_PATH_PREFIX = "android-content://";
    private static final String PRIVATE_SAVE_ROOT = "saves";
    private static final String PRIVATE_LOG_ROOT = "logs";
    private static final String SCREEN_ORIENTATION_PREFERENCES = "screen_orientation";
    private static final String SCREEN_ORIENTATION_MODE = "mode";
    private static final String EXTRA_IME_AUTOMATION = "dingoopie.ime_automation";
    private static final String EXTRA_IME_DISABLED = "dingoopie.ime_disabled";
    private static final String EXTRA_CHEAT_MANAGER_AUTOMATION =
            "dingoopie.cheat_manager_automation";
    private static final String EXTRA_CHEAT_MANAGER_GAME_NAME =
            "dingoopie.cheat_manager_game_name";
    private static final String EXTRA_CHEAT_MANAGER_GAME_PATH =
            "dingoopie.cheat_manager_game_path";
    private static final String EXTRA_CHEAT_MANAGER_SECONDARY_GAME_PATH =
            "dingoopie.cheat_manager_secondary_game_path";
    private static final String EXTRA_GAME_AUTOMATION_PATH =
            "dingoopie.game_automation_path";
    private static final String EXTRA_AUDIO_VALIDATION_AUTOMATION =
            "dingoopie.audio_validation";
    private static final String STATE_PENDING_GAME_FILE_URI =
            "dingoopie.pending_game_file_uri";
    private static final String STATE_GAME_SELECTION_CHINESE =
            "dingoopie.game_selection_chinese";
    private static final long IME_AUTOMATION_START_DELAY_MS = 500;
    private static final long IME_AUTOMATION_RESULT_DELAY_MS = 1200;
    private static final long APPLICATION_EXIT_NATIVE_CLEANUP_DELAY_MS = 500;
    private static final long APPLICATION_EXIT_PROCESS_DELAY_MS = 750;
    public static final int SCREEN_ORIENTATION_AUTO = 0;
    public static final int SCREEN_ORIENTATION_LANDSCAPE = 1;
    public static final int SCREEN_ORIENTATION_PORTRAIT = 2;
    private String selectedGamePath;
    private final AtomicReference<String> pendingExternalGamePath = new AtomicReference<>();
    private Uri pendingGameFileUri;
    private boolean gameSelectionChinese = true;
    private boolean gameAutomationPathConsumed;
    private boolean audioValidationAutomationConsumed;
    private boolean cheatManagerAutomationGameConsumed;
    private boolean gameLibraryInitializationStarted;
    private HandlerThread backgroundSignalThread;
    private BroadcastReceiver systemDialogsReceiver;
    private volatile boolean activityDestroyed;
    private LanFileManagerServer lanFileManagerServer;
    private volatile boolean gameLibraryScanning;
    private volatile int gameLibraryScanProcessedEntries;
    private volatile int gameLibraryScanTotalEntries;
    private final AtomicInteger gameLibraryScanTaskCount = new AtomicInteger();
    private final ExecutorService gameLibraryScanExecutor =
            Executors.newSingleThreadExecutor(runnable ->
                    new Thread(runnable, "DingooPieGameLibraryScan"));
    private final Runnable initializeGameLibraryTask = this::initializeGameLibrary;
    private final Runnable restoreImmersiveModeTask = this::applyImmersiveMode;

    static native boolean nativeRunSaveAutomation(
            String appDirectory, String ccDirectory);
    private static native void nativeSetAppBackgrounded(boolean backgrounded);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (savedInstanceState != null) {
            String pendingUriText = savedInstanceState.getString(STATE_PENDING_GAME_FILE_URI, "");
            if (!pendingUriText.isEmpty()) {
                pendingGameFileUri = Uri.parse(pendingUriText);
            }
            gameSelectionChinese = savedInstanceState.getBoolean(
                    STATE_GAME_SELECTION_CHINESE, true);
        }
        registerBackgroundSignalReceiver();
        applySavedScreenOrientation();
        getWindow().getDecorView().postDelayed(initializeGameLibraryTask, 800);
        scheduleImmersiveMode();
        scheduleImeAutomation(getIntent());
        SaveAutomation.schedule(this, getIntent());
        handleExternalGameLaunchIntent(getIntent());
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        if (pendingGameFileUri != null) {
            outState.putString(STATE_PENDING_GAME_FILE_URI, pendingGameFileUri.toString());
        }
        outState.putBoolean(STATE_GAME_SELECTION_CHINESE, gameSelectionChinese);
        super.onSaveInstanceState(outState);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        scheduleImeAutomation(intent);
        SaveAutomation.schedule(this, intent);
        handleExternalGameLaunchIntent(intent);
    }

    @Override
    protected void onDestroy() {
        activityDestroyed = true;
        if (lanFileManagerServer != null) {
            lanFileManagerServer.close();
            lanFileManagerServer = null;
        }
        unregisterBackgroundSignalReceiver();
        getWindow().getDecorView().removeCallbacks(initializeGameLibraryTask);
        gameLibraryScanExecutor.shutdownNow();
        gameLibraryScanTaskCount.set(0);
        gameLibraryScanning = false;
        super.onDestroy();
    }

    @SuppressLint("UnspecifiedRegisterReceiverFlag")
    private void registerBackgroundSignalReceiver() {
        backgroundSignalThread = new HandlerThread("DingooPieBackgroundSignal");
        backgroundSignalThread.start();
        Handler handler = new Handler(backgroundSignalThread.getLooper());
        systemDialogsReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                String reason = intent.getStringExtra("reason");
                if ("homekey".equals(reason) || "recentapps".equals(reason)) {
                    nativeSetAppBackgrounded(true);
                }
            }
        };
        IntentFilter filter = new IntentFilter(Intent.ACTION_CLOSE_SYSTEM_DIALOGS);
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(systemDialogsReceiver, filter, null, handler,
                    Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(systemDialogsReceiver, filter, null, handler);
        }
    }

    private void unregisterBackgroundSignalReceiver() {
        if (systemDialogsReceiver != null) {
            unregisterReceiver(systemDialogsReceiver);
            systemDialogsReceiver = null;
        }
        if (backgroundSignalThread != null) {
            backgroundSignalThread.quitSafely();
            backgroundSignalThread = null;
        }
    }

    @Override
    public void onBackPressed() {
        SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_BACK);
        SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_BACK);
    }

    public void requestApplicationExitFromNative() {
        runOnUiThread(() -> {
            android.os.Handler handler = new android.os.Handler(getMainLooper());
            handler.postDelayed(() -> {
                Log.i(TAG, "Removing application task after native shutdown");
                finishAndRemoveTask();
                handler.postDelayed(() -> {
                    Log.i(TAG, "Ending process after explicit application exit");
                    android.os.Process.killProcess(android.os.Process.myPid());
                }, APPLICATION_EXIT_PROCESS_DELAY_MS);
            }, APPLICATION_EXIT_NATIVE_CLEANUP_DELAY_MS);
        });
    }

    private void scheduleImeAutomation(Intent intent) {
        boolean debuggable = (getApplicationInfo().flags &
                android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        if (!debuggable || intent == null ||
                !intent.getBooleanExtra(EXTRA_IME_AUTOMATION, false)) {
            return;
        }
        final boolean disabled = intent.getBooleanExtra(EXTRA_IME_DISABLED, true);
        getWindow().getDecorView().postDelayed(() -> {
            setSystemImeDisabledFromNative(disabled);
            boolean accepted = requestSystemImeForAutomation();
            getWindow().getDecorView().postDelayed(() -> Log.i(TAG,
                    "IME_AUTOMATION disabled=" + disabled +
                            " request_accepted=" + accepted +
                            " keyboard_shown=" + isSystemImeShownForAutomation()),
                    IME_AUTOMATION_RESULT_DELAY_MS);
        }, IME_AUTOMATION_START_DELAY_MS);
    }

    public int getScreenOrientationMode() {
        return getSharedPreferences(SCREEN_ORIENTATION_PREFERENCES, MODE_PRIVATE)
                .getInt(SCREEN_ORIENTATION_MODE, SCREEN_ORIENTATION_LANDSCAPE);
    }

    public boolean isCurrentScreenPortrait() {
        return getResources().getConfiguration().orientation ==
                android.content.res.Configuration.ORIENTATION_PORTRAIT;
    }

    public void setSystemImeDisabledFromNative(boolean disabled) {
        SDLActivity.setSystemImeDisabled(disabled);
        Log.i(TAG, "System IME disabled=" + disabled);
    }

    boolean requestSystemImeForAutomation() {
        return SDLActivity.showTextInput(0, 0, 1, 1);
    }

    boolean isSystemImeShownForAutomation() {
        return SDLActivity.isScreenKeyboardShown();
    }

    public void setScreenOrientationMode(int mode) {
        if (mode < SCREEN_ORIENTATION_AUTO || mode > SCREEN_ORIENTATION_PORTRAIT) {
            mode = SCREEN_ORIENTATION_LANDSCAPE;
        }
        final int selectedMode = mode;
        getSharedPreferences(SCREEN_ORIENTATION_PREFERENCES, MODE_PRIVATE)
                .edit().putInt(SCREEN_ORIENTATION_MODE, selectedMode).apply();
        runOnUiThread(() -> applyScreenOrientation(selectedMode));
    }

    private void applySavedScreenOrientation() {
        applyScreenOrientation(getScreenOrientationMode());
    }

    private void applyScreenOrientation(int mode) {
        int requestedOrientation;
        if (mode == SCREEN_ORIENTATION_LANDSCAPE) {
            requestedOrientation = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;
        } else if (mode == SCREEN_ORIENTATION_PORTRAIT) {
            requestedOrientation = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT;
        } else {
            requestedOrientation = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR;
        }
        setRequestedOrientation(requestedOrientation);
    }
    @Override
    public void setOrientationBis(int width, int height, boolean resizable, String hint) {
        int mode = getScreenOrientationMode();
        if (mode == SCREEN_ORIENTATION_LANDSCAPE) {
            setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        } else if (mode == SCREEN_ORIENTATION_PORTRAIT) {
            setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
        } else {
            super.setOrientationBis(width, height, resizable, hint);
        }
    }
    @Override
    protected void onResume() {
        super.onResume();
        scheduleImmersiveMode();
    }

    @Override
    protected void onNativeStateChanged(NativeState state) {
        nativeSetAppBackgrounded(state != NativeState.RESUMED);
    }

    @Override
    protected void onPause() {
        nativeSetAppBackgrounded(true);
        super.onPause();
    }

    @Override
    protected void onStop() {
        nativeSetAppBackgrounded(true);
        super.onStop();
    }

    @Override
    protected void onUserLeaveHint() {
        nativeSetAppBackgrounded(true);
        super.onUserLeaveHint();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            SDLActivity.enforceSystemImePolicy();
            scheduleImmersiveMode();
        }
    }

    @Override
    public void onSystemUiVisibilityChange(int visibility) {
        super.onSystemUiVisibilityChange(visibility);
        int hiddenFlags = View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION;
        if ((visibility & hiddenFlags) != hiddenFlags) {
            scheduleImmersiveMode();
        }
    }

    private void scheduleImmersiveMode() {
        View decorView = getWindow().getDecorView();
        decorView.removeCallbacks(restoreImmersiveModeTask);
        decorView.post(restoreImmersiveModeTask);
        decorView.postDelayed(restoreImmersiveModeTask, 300);
    }

    private void applyImmersiveMode() {
        Window window = getWindow();
        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        window.clearFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN);

        View decorView = window.getDecorView();
        int flags = View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE;
        decorView.setSystemUiVisibility(flags);

        if (Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected String[] getArguments() {
        return new String[0];
    }

    public int[] renderSystemText(byte[] utf8Text, float textSizePx, int color) {
        return renderSystemTextInternal(utf8Text, textSizePx, color, false);
    }

    public int[] renderSystemTextBold(byte[] utf8Text, float textSizePx, int color) {
        return renderSystemTextInternal(utf8Text, textSizePx, color, true);
    }

    private int[] renderSystemTextInternal(byte[] utf8Text, float textSizePx, int color,
            boolean bold) {
        String text = new String(utf8Text, StandardCharsets.UTF_8);
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
        paint.setTypeface(bold ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
        paint.setElegantTextHeight(true);
        paint.setTextSize(textSizePx);
        paint.setColor(color);
        paint.setTextAlign(Paint.Align.LEFT);

        Paint.FontMetrics metrics = paint.getFontMetrics();
        int padding = Math.max(2, Math.round(textSizePx * 0.12f));
        int width = Math.max(1, (int)Math.ceil(paint.measureText(text)) + padding * 2);
        int height = Math.max(1, (int)Math.ceil(metrics.descent - metrics.ascent) + padding * 2);
        Bitmap bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        canvas.drawColor(Color.TRANSPARENT);
        canvas.drawText(text, padding, padding - metrics.ascent, paint);

        int[] result = new int[width * height + 2];
        result[0] = width;
        result[1] = height;
        bitmap.getPixels(result, 2, width, 0, 0, width, height);
        bitmap.recycle();
        return result;
    }

    public boolean requestGameSelection(boolean chinese) {
        if (gameLibraryScanning) {
            return false;
        }
        gameSelectionChinese = chinese;
        runOnUiThread(() -> {
            if (gameLibraryScanning || isFinishing()) {
                completeGameSelection("");
                return;
            }
            new AlertDialog.Builder(this)
                    .setTitle(gameSelectionText("\u6DFB\u52A0\u6E38\u620F", "Add Game"))
                    .setItems(new String[]{
                            gameSelectionText("\u6DFB\u52A0\u6587\u4EF6", "Add File"),
                            gameSelectionText("\u6DFB\u52A0\u6587\u4EF6\u5939", "Add Folder")
                    }, (dialog, which) -> {
                        GameImportOption option = GameImportOption.values()[which];
                        if (option == GameImportOption.FILE) {
                            requestGameFile();
                        } else {
                            requestGameDirectory();
                        }
                    })
                    .setOnCancelListener(dialog -> completeGameSelection(""))
                    .show();
        });
        return true;
    }

    public String getAppVersionName() {
        try {
            String versionName = getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
            return versionName == null ? "unknown" : versionName;
        } catch (PackageManager.NameNotFoundException exception) {
            return "unknown";
        }
    }

    public void showMessageDialog(String title, String body, String positiveButton) {
        runOnUiThread(() -> new AlertDialog.Builder(this)
                .setTitle(title)
                .setMessage(body)
                .setPositiveButton(positiveButton, null)
                .show());
    }

    public void showLanFileManager(boolean chinese) {
        runOnUiThread(() -> {
            if (activityDestroyed || isFinishing()) {
                return;
            }
            try {
                if (lanFileManagerServer == null) {
                    lanFileManagerServer = new LanFileManagerServer(this, chinese);
                    lanFileManagerServer.start();
                }
                List<String> urls = lanFileManagerServer.getAccessUrls();
                StringBuilder message = new StringBuilder();
                message.append(chinese ? "\u8bf7\u5728\u540c\u4e00\u5185\u7f51\u8bbe\u5907\u7684\u6d4f\u89c8\u5668\u4e2d\u6253\u5f00\uff1a\n\n" :
                        "Open one of these addresses in a browser on the same network:\n\n");
                if (urls.isEmpty()) {
                    message.append(chinese ?
                            "\u672a\u627e\u5230\u53ef\u7528\u7684\u5185\u7f51 IPv4 \u5730\u5740\u3002\n\u8bf7\u786e\u4fdd\u8bbe\u5907\u4e0e\u8bbf\u95ee\u7aef\u8fde\u63a5\u540c\u4e00\u7f51\u7edc\u3002" :
                            "No LAN IPv4 address was found.\nConnect the device and client to the same network.");
                } else {
                    for (String url : urls) {
                        message.append(url).append('\n');
                    }
                }
                message.append('\n').append(chinese ?
                        "\u5173\u95ed\u7a97\u53e3\u540e\uff0c\u6587\u4ef6\u670d\u52a1\u5c06\u7ee7\u7eed\u5728\u540e\u53f0\u8fd0\u884c\u3002" :
                        "Closing this dialog keeps the file service running in the background.");
                TextView textView = new TextView(this);
                textView.setText(message.toString());
                textView.setTextIsSelectable(true);
                int padding = (int)(24 * getResources().getDisplayMetrics().density + 0.5f);
                textView.setPadding(padding, padding / 2, padding, padding / 2);
                new AlertDialog.Builder(this)
                        .setTitle(chinese ? "\u6587\u4ef6\u7ba1\u7406\u670d\u52a1" : "File Manager Service")
                        .setView(textView)
                        .setPositiveButton(chinese ? "\u590d\u5236\u5730\u5740" : "Copy Address", (dialog, which) -> {
                            if (!urls.isEmpty()) {
                                ClipboardManager clipboard = (ClipboardManager)getSystemService(
                                        CLIPBOARD_SERVICE);
                                clipboard.setPrimaryClip(ClipData.newPlainText("DingooPie", urls.get(0)));
                            }
                        })
                        .setNeutralButton(chinese ? "\u505c\u6b62\u670d\u52a1" : "Stop Service",
                                (dialog, which) -> stopLanFileManager())
                        .setNegativeButton(chinese ? "\u540e\u53f0\u8fd0\u884c" : "Keep Running", null)
                        .show();
            } catch (IOException exception) {
                Log.e(TAG, "Unable to start LAN file manager", exception);
                if (lanFileManagerServer != null) {
                    lanFileManagerServer.close();
                    lanFileManagerServer = null;
                }
                showMessageDialog(chinese ? "\u65e0\u6cd5\u542f\u52a8\u6587\u4ef6\u670d\u52a1" :
                                "Cannot Start File Service",
                        exception.getMessage() == null ? exception.toString() : exception.getMessage(),
                        chinese ? "\u786e\u5b9a" : "OK");
            }
        });
    }

    private void stopLanFileManager() {
        if (lanFileManagerServer != null) {
            lanFileManagerServer.close();
            lanFileManagerServer = null;
        }
    }

    public boolean showConfirmationDialog(
            String title, String body, String positiveButton, String negativeButton) {
        if (isFinishing() || activityDestroyed) {
            return false;
        }
        CountDownLatch completed = new CountDownLatch(1);
        AtomicBoolean confirmed = new AtomicBoolean(false);
        AtomicBoolean abandoned = new AtomicBoolean(false);
        AtomicReference<AlertDialog> activeDialog = new AtomicReference<>();
        runOnUiThread(() -> {
            if (isFinishing() || activityDestroyed || abandoned.get()) {
                completed.countDown();
                return;
            }
            try {
                AlertDialog dialog = new AlertDialog.Builder(this)
                        .setTitle(title)
                        .setMessage(body)
                        .setPositiveButton(positiveButton,
                                (unusedDialog, unusedWhich) -> confirmed.set(true))
                        .setNegativeButton(negativeButton, null)
                        .create();
                activeDialog.set(dialog);
                dialog.setOnDismissListener(unusedDialog -> {
                    activeDialog.compareAndSet(dialog, null);
                    completed.countDown();
                });
                if (abandoned.get()) {
                    activeDialog.compareAndSet(dialog, null);
                    completed.countDown();
                    return;
                }
                dialog.show();
            } catch (RuntimeException exception) {
                Log.e(TAG, "Unable to show confirmation dialog", exception);
                completed.countDown();
            }
        });
        try {
            if (!completed.await(30, TimeUnit.SECONDS)) {
                abandoned.set(true);
                Log.w(TAG, "Confirmation dialog timed out");
                runOnUiThread(() -> {
                    AlertDialog dialog = activeDialog.get();
                    if (dialog != null && dialog.isShowing()) {
                        dialog.dismiss();
                    }
                });
                return false;
            }
        } catch (InterruptedException exception) {
            abandoned.set(true);
            runOnUiThread(() -> {
                AlertDialog dialog = activeDialog.get();
                if (dialog != null && dialog.isShowing()) {
                    dialog.dismiss();
                }
            });
            Thread.currentThread().interrupt();
            return false;
        }
        return confirmed.get();
    }

    public boolean isGameLibraryScanning() {
        return gameLibraryScanning;
    }

    public int getGameLibraryScanProcessedEntries() {
        return gameLibraryScanProcessedEntries;
    }

    public int getGameLibraryScanTotalEntries() {
        return gameLibraryScanTotalEntries;
    }

    public synchronized String consumeSelectedGamePath() {
        String path = selectedGamePath;
        selectedGamePath = null;
        return path;
    }

    public synchronized String consumeCheatManagerAutomationGamePath() {
        Intent intent = getIntent();
        boolean debuggable = (getApplicationInfo().flags &
                android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        if (!debuggable || cheatManagerAutomationGameConsumed || intent == null ||
                !intent.getBooleanExtra(EXTRA_CHEAT_MANAGER_AUTOMATION, false)) {
            return "";
        }
        cheatManagerAutomationGameConsumed = true;
        String requestedPath = intent.getStringExtra(EXTRA_CHEAT_MANAGER_GAME_PATH);
        if (requestedPath != null && !requestedPath.isEmpty()) {
            Set<String> paths = new HashSet<>(getSharedPreferences(
                    GAME_LIBRARY_PREFERENCES, MODE_PRIVATE).getStringSet(
                    GAME_LIBRARY_PATHS, Collections.emptySet()));
            paths.add(requestedPath);
            String secondaryPath = intent.getStringExtra(
                    EXTRA_CHEAT_MANAGER_SECONDARY_GAME_PATH);
            if (secondaryPath != null && !secondaryPath.isEmpty()) {
                paths.add(secondaryPath);
            }
            if (getSharedPreferences(GAME_LIBRARY_PREFERENCES, MODE_PRIVATE).edit()
                    .putStringSet(GAME_LIBRARY_PATHS, paths).commit()) {
                Log.i(TAG, "CHEAT_MANAGER_AUTOMATION selected_path=" + requestedPath);
                return requestedPath;
            }
            Log.e(TAG, "CHEAT_MANAGER_AUTOMATION path_registration_failed=" +
                    requestedPath);
            return "";
        }
        String requestedName = intent.getStringExtra(EXTRA_CHEAT_MANAGER_GAME_NAME);
        if (requestedName == null || requestedName.isEmpty()) {
            return "";
        }
        if (!requestedName.toLowerCase(Locale.ROOT).endsWith(".app")) {
            requestedName += ".app";
        }
        Set<String> paths = getSharedPreferences(GAME_LIBRARY_PREFERENCES, MODE_PRIVATE)
                .getStringSet(GAME_LIBRARY_PATHS, Collections.emptySet());
        for (String path : paths) {
            if (requestedName.equalsIgnoreCase(gameDisplayNameFromPath(path))) {
                Log.i(TAG, "CHEAT_MANAGER_AUTOMATION selected_game=" + requestedName);
                return path;
            }
        }
        Log.e(TAG, "CHEAT_MANAGER_AUTOMATION game_not_found=" + requestedName);
        return "";
    }

    private synchronized void completeGameSelection(String path) {
        selectedGamePath = path;
    }

    public String consumeExternalGameLaunchPath() {
        String path = pendingExternalGamePath.getAndSet(null);
        return path == null ? "" : path;
    }

    private void queueExternalGameLaunch(String path) {
        pendingExternalGamePath.set(path);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_GAME_FILE) {
            if (resultCode != RESULT_OK || data == null || data.getData() == null) {
                completeGameSelection("");
                return;
            }
            Uri fileUri = data.getData();
            if ((data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION) == 0) {
                completeGameSelection("");
                return;
            }
            try {
                getContentResolver().takePersistableUriPermission(
                        fileUri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (SecurityException exception) {
                Log.e(TAG, "Unable to persist selected game permission", exception);
                completeGameSelection("");
                return;
            }
            String displayName = queryDisplayName(fileUri);
            if (!isSupportedGameFileName(displayName)) {
                releasePersistedUriPermission(fileUri);
                showMessageDialog(
                        gameSelectionText("\u65E0\u6CD5\u6DFB\u52A0\u6E38\u620F",
                                "Cannot Add Game"),
                        gameSelectionText(
                                "\u8BF7\u9009\u62E9 .app \u6216 .cc \u6E38\u620F\u6587\u4EF6\u3002",
                                "Select an .app or .cc game file."),
                        gameSelectionText("\u786E\u5B9A", "OK"));
                completeGameSelection("");
                return;
            }
            pendingGameFileUri = fileUri;
            requestGameFileDirectoryPermission(fileUri);
            return;
        }
        if (requestCode == REQUEST_GAME_FILE_DIRECTORY) {
            Uri fileUri = pendingGameFileUri;
            if (fileUri == null || resultCode != RESULT_OK || data == null ||
                    data.getData() == null) {
                cancelPendingGameFileSelection();
                return;
            }
            Uri directoryUri = data.getData();
            if ((data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION) == 0 ||
                    (data.getFlags() & Intent.FLAG_GRANT_WRITE_URI_PERMISSION) == 0) {
                Log.e(TAG, "Selected single-game directory did not grant read/write access");
                cancelPendingGameFileSelection();
                return;
            }
            if (!directoryContainsDocument(directoryUri, fileUri)) {
                showGameFileDirectoryMismatchDialog(fileUri);
                return;
            }
            try {
                getContentResolver().takePersistableUriPermission(
                        directoryUri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION |
                                Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            } catch (SecurityException exception) {
                Log.e(TAG, "Unable to persist single-game directory permission", exception);
                cancelPendingGameFileSelection();
                return;
            }
            pendingGameFileUri = null;
            importGameFileAsync(fileUri, directoryUri);
            return;
        }
        if (requestCode != REQUEST_GAME_DIRECTORY) {
            return;
        }
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            completeGameSelection("");
            return;
        }
        Uri directoryUri = data.getData();
        if ((data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION) == 0 ||
                (data.getFlags() & Intent.FLAG_GRANT_WRITE_URI_PERMISSION) == 0) {
            Log.e(TAG, "Selected game directory did not grant read/write access");
            completeGameSelection("");
            return;
        }
        try {
            getContentResolver().takePersistableUriPermission(
                    directoryUri,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION |
                            Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        } catch (SecurityException exception) {
            Log.e(TAG, "Unable to persist game directory permission", exception);
            completeGameSelection("");
            return;
        }
        scanGameDirectoryAsync(directoryUri);
    }

    private void initializeGameLibrary() {
        if (activityDestroyed || gameLibraryInitializationStarted) {
            return;
        }
        gameLibraryInitializationStarted = true;
        SharedPreferences preferences =
                getSharedPreferences(GAME_LIBRARY_PREFERENCES, MODE_PRIVATE);
        Set<String> directoryTexts = new HashSet<>(preferences.getStringSet(
                GAME_LIBRARY_DIRECTORIES, Collections.emptySet()));
        if (directoryTexts.isEmpty()) {
            return;
        }
        List<Uri> directoryUris = new ArrayList<>();
        for (String directoryText : directoryTexts) {
            try {
                directoryUris.add(Uri.parse(directoryText));
            } catch (Exception exception) {
                Log.e(TAG, "Unable to restore game library directory", exception);
            }
        }
        if (!directoryUris.isEmpty()) {
            scanGameDirectoriesAsync(directoryUris, false);
        }
    }

    private void scanGameDirectoryAsync(Uri directoryUri) {
        scanGameDirectoriesAsync(Collections.singletonList(directoryUri), true);
    }

    private boolean submitGameLibraryScanTask(Runnable task) {
        if (activityDestroyed || gameLibraryScanExecutor.isShutdown()) {
            return false;
        }
        gameLibraryScanTaskCount.incrementAndGet();
        gameLibraryScanning = true;
        try {
            gameLibraryScanExecutor.execute(task);
            return true;
        } catch (RejectedExecutionException exception) {
            finishGameLibraryScanTask();
            Log.w(TAG, "Game library scan task was rejected", exception);
            return false;
        }
    }

    private void finishGameLibraryScanTask() {
        int remainingTasks = gameLibraryScanTaskCount.decrementAndGet();
        if (remainingTasks <= 0) {
            gameLibraryScanTaskCount.set(0);
            gameLibraryScanning = false;
        }
    }

    private void scanGameDirectoriesAsync(List<Uri> directoryUris, boolean notifySelection) {
        List<Uri> scanUris = new ArrayList<>(directoryUris);
        boolean submitted = submitGameLibraryScanTask(() -> {
            gameLibraryScanProcessedEntries = 0;
            gameLibraryScanTotalEntries = 0;
            long scanStarted = SystemClock.uptimeMillis();
            String completion = "";
            try {
                List<Integer> expectedEntryCounts = new ArrayList<>(scanUris.size());
                long totalEntries = 0;
                boolean hasUnknownEntryCount = false;
                for (Uri directoryUri : scanUris) {
                    if (activityDestroyed || Thread.currentThread().isInterrupted()) {
                        break;
                    }
                    int entryCount = countGameDirectoryEntries(directoryUri);
                    expectedEntryCounts.add(entryCount);
                    if (entryCount < 0) {
                        hasUnknownEntryCount = true;
                    } else {
                        totalEntries = Math.min(Integer.MAX_VALUE,
                                totalEntries + entryCount);
                    }
                }
                gameLibraryScanTotalEntries = hasUnknownEntryCount ? 0 : (int) totalEntries;
                for (int index = 0; index < scanUris.size(); index++) {
                    if (activityDestroyed || Thread.currentThread().isInterrupted()) {
                        break;
                    }
                    Uri directoryUri = scanUris.get(index);
                    List<String> paths = scanGameDirectory(
                            directoryUri, expectedEntryCounts.get(index),
                            !hasUnknownEntryCount);
                    if (paths == null) {
                        continue;
                    }
                    if (activityDestroyed || Thread.currentThread().isInterrupted()) {
                        break;
                    }
                    if (shouldIgnoreEmptyAutomaticScan(
                            directoryUri, paths, notifySelection)) {
                        Log.w(TAG, "Ignoring empty automatic scan for existing game directory: " +
                                directoryUri);
                        continue;
                    }
                    mergeGameLibrary(directoryUri, paths, notifySelection);
                    if (completion.isEmpty()) {
                        completion = paths.isEmpty() ? "directory" : paths.get(0);
                    }
                }
            } catch (Exception exception) {
                Log.e(TAG, "Unable to complete game library scan", exception);
            } finally {
                long remainingDisplayTime = 400L - (SystemClock.uptimeMillis() - scanStarted);
                if (remainingDisplayTime > 0L && !activityDestroyed) {
                    SystemClock.sleep(remainingDisplayTime);
                }
                finishGameLibraryScanTask();
            }
            if (notifySelection && !activityDestroyed) {
                completeGameSelection(completion);
            }
        });
        if (!submitted && notifySelection && !activityDestroyed) {
            completeGameSelection("");
        }
    }

    private void importGameFileAsync(Uri fileUri, Uri saveDirectoryUri) {
        boolean submitted = submitGameLibraryScanTask(() -> {
            gameLibraryScanProcessedEntries = 0;
            gameLibraryScanTotalEntries = 1;
            long scanStarted = SystemClock.uptimeMillis();
            String completion = "";
            try {
                String displayName = queryDisplayName(fileUri);
                if (isSupportedGameFileName(displayName)) {
                    displayName = displayName.replaceAll("[^\\p{L}\\p{N}._ -]", "_");
                    try (ParcelFileDescriptor descriptor = getContentResolver().openFileDescriptor(fileUri, "r")) {
                        if (descriptor != null) {
                            String gamePath = buildGamePath(fileUri, displayName);
                            mergeGameLibrary(null, Collections.singletonList(gamePath), true);
                            rememberGameSaveDirectory(gamePath, saveDirectoryUri);
                            completion = gamePath;
                        }
                    }
                }
            } catch (Exception exception) {
                Log.e(TAG, "Unable to add selected game", exception);
            } finally {
                gameLibraryScanProcessedEntries = 1;
                long remainingDisplayTime = 400L - (SystemClock.uptimeMillis() - scanStarted);
                if (remainingDisplayTime > 0L && !activityDestroyed) {
                    SystemClock.sleep(remainingDisplayTime);
                }
                finishGameLibraryScanTask();
            }
            if (!activityDestroyed) {
                completeGameSelection(completion);
            }
        });
        if (!submitted && !activityDestroyed) {
            completeGameSelection("");
        }
    }

    private int countGameDirectoryEntries(Uri directoryUri) {
        if (directoryUri == null) {
            return 0;
        }
        try {
            String treeDocumentId = DocumentsContract.getTreeDocumentId(directoryUri);
            Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(
                    directoryUri, treeDocumentId);
            try (Cursor cursor = getContentResolver().query(childrenUri,
                    new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID},
                    null, null, null)) {
                return cursor != null ? cursor.getCount() : -1;
            }
        } catch (Exception exception) {
            Log.w(TAG, "Unable to count game library directory entries", exception);
            return -1;
        }
    }

    private void reconcileGameLibraryScanTotalEntries(int expectedEntries, int actualEntries) {
        long adjustedTotal = (long) gameLibraryScanTotalEntries -
                Math.max(0, expectedEntries) + Math.max(0, actualEntries);
        adjustedTotal = Math.max(gameLibraryScanProcessedEntries, adjustedTotal);
        gameLibraryScanTotalEntries = (int) Math.min(Integer.MAX_VALUE, adjustedTotal);
    }

    private void incrementGameLibraryScanProcessedEntries() {
        if (gameLibraryScanProcessedEntries < Integer.MAX_VALUE) {
            gameLibraryScanProcessedEntries++;
        }
    }

    private List<String> scanGameDirectory(Uri directoryUri, int expectedEntryCount,
            boolean reconcileProgressTotal) {
        if (directoryUri == null) {
            if (reconcileProgressTotal) {
                reconcileGameLibraryScanTotalEntries(expectedEntryCount, 0);
            }
            return null;
        }
        int accountedEntryCount = Math.max(0, expectedEntryCount);
        int processedEntryCount = 0;
        try {
            String treeDocumentId = DocumentsContract.getTreeDocumentId(directoryUri);
            Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(
                    directoryUri, treeDocumentId);
            List<String> paths = new ArrayList<>();
            try (Cursor cursor = getContentResolver().query(childrenUri,
                    new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                            DocumentsContract.Document.COLUMN_DISPLAY_NAME},
                    null, null, null)) {
                if (cursor == null) {
                    if (reconcileProgressTotal) {
                        reconcileGameLibraryScanTotalEntries(accountedEntryCount, 0);
                    }
                    return null;
                }
                int actualEntryCount = cursor.getCount();
                if (reconcileProgressTotal) {
                    reconcileGameLibraryScanTotalEntries(
                            accountedEntryCount, actualEntryCount);
                }
                accountedEntryCount = actualEntryCount;
                int idIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DOCUMENT_ID);
                int nameIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DISPLAY_NAME);
                while (cursor.moveToNext()) {
                    if (activityDestroyed || Thread.currentThread().isInterrupted()) {
                        return null;
                    }
                    incrementGameLibraryScanProcessedEntries();
                    processedEntryCount++;
                    if (idIndex < 0 || nameIndex < 0) {
                        continue;
                    }
                    String displayName = cursor.getString(nameIndex);
                    if (!isSupportedGameFileName(displayName)) {
                        continue;
                    }
                    displayName = displayName.replaceAll("[^\\p{L}\\p{N}._ -]", "_");
                    Uri fileUri = DocumentsContract.buildDocumentUriUsingTree(
                            directoryUri, cursor.getString(idIndex));
                    paths.add(buildGamePath(fileUri, displayName));
                }
            }
            Collections.sort(paths, (left, right) ->
                    gameDisplayNameFromPath(left).compareToIgnoreCase(gameDisplayNameFromPath(right)));
            return paths;
        } catch (Exception exception) {
            if (reconcileProgressTotal) {
                reconcileGameLibraryScanTotalEntries(
                        accountedEntryCount, processedEntryCount);
            }
            Log.e(TAG, "Unable to scan game library directory", exception);
            return null;
        }
    }

    private static final class GameLibraryEntry {
        String path = "";
        long lastRunTime;
        String saveDirectory = "";
        final Set<String> sourcePaths = new HashSet<>();
    }

    private String gameDocumentIdentity(String gamePath) {
        Uri uri = gameUriFromPath(gamePath);
        if (uri == null) {
            return gamePath;
        }
        try {
            String documentId = DocumentsContract.getDocumentId(uri);
            if (documentId != null && !documentId.isEmpty()) {
                return uri.getAuthority() + "\n" + documentId;
            }
        } catch (IllegalArgumentException ignored) {
        }
        return uri.normalizeScheme().toString();
    }

    private boolean preferGameLibraryPath(
            String candidatePath,
            long candidateLastRunTime,
            String candidateSaveDirectory,
            GameLibraryEntry current) {
        boolean candidateHasSaveDirectory = !candidateSaveDirectory.isEmpty();
        boolean currentHasSaveDirectory = !current.saveDirectory.isEmpty();
        if (candidateHasSaveDirectory != currentHasSaveDirectory) {
            return candidateHasSaveDirectory;
        }
        if (candidateLastRunTime != current.lastRunTime) {
            return candidateLastRunTime > current.lastRunTime;
        }
        return candidatePath.compareTo(current.path) < 0;
    }

    private synchronized Set<String> mergeGameLibraryPaths(
            SharedPreferences preferences,
            Uri directoryUri,
            List<String> importedPaths,
            boolean restoreExcludedPaths) {
        Map<String, GameLibraryEntry> entries = new HashMap<>();
        String importedSaveDirectory = directoryUri == null ? "" : directoryUri.toString();
        Set<String> excludedIdentities = new HashSet<>(preferences.getStringSet(
                GAME_LIBRARY_EXCLUDED_IDENTITIES, Collections.emptySet()));
        List<String> eligibleImportedPaths = new ArrayList<>();
        boolean exclusionsChanged = false;
        for (String path : importedPaths) {
            String identity = gameDocumentIdentity(path);
            if (excludedIdentities.contains(identity) && !restoreExcludedPaths) {
                continue;
            }
            if (excludedIdentities.remove(identity)) {
                exclusionsChanged = true;
            }
            eligibleImportedPaths.add(path);
        }
        Set<String> importedIdentities = new HashSet<>();
        for (String path : eligibleImportedPaths) {
            importedIdentities.add(gameDocumentIdentity(path));
        }
        Set<String> obsoleteSourcePaths = new HashSet<>();
        Set<String> storedPaths = preferences.getStringSet(
                GAME_LIBRARY_PATHS, Collections.emptySet());
        for (String path : storedPaths) {
            String identity = gameDocumentIdentity(path);
            long lastRunTime = preferences.getLong(GAME_LAST_RUN_TIMES + ":" + path, 0L);
            String saveDirectory = preferences.getString(
                    GAME_SAVE_DIRECTORIES + ":" + path, "");
            if (!importedSaveDirectory.isEmpty() &&
                    importedSaveDirectory.equals(saveDirectory) &&
                    !importedIdentities.contains(identity)) {
                obsoleteSourcePaths.add(path);
                continue;
            }
            GameLibraryEntry entry = entries.get(identity);
            if (entry == null) {
                entry = new GameLibraryEntry();
                entry.path = path;
                entry.lastRunTime = lastRunTime;
                entry.saveDirectory = saveDirectory;
                entries.put(identity, entry);
            } else {
                if (preferGameLibraryPath(path, lastRunTime, saveDirectory, entry)) {
                    entry.path = path;
                    entry.saveDirectory = saveDirectory;
                }
                entry.lastRunTime = Math.max(entry.lastRunTime, lastRunTime);
            }
            entry.sourcePaths.add(path);
        }

        for (String path : eligibleImportedPaths) {
            String identity = gameDocumentIdentity(path);
            GameLibraryEntry entry = entries.get(identity);
            if (entry == null) {
                entry = new GameLibraryEntry();
                entries.put(identity, entry);
            }
            entry.sourcePaths.add(path);
            entry.path = path;
            entry.lastRunTime = Math.max(entry.lastRunTime,
                    preferences.getLong(GAME_LAST_RUN_TIMES + ":" + path, 0L));
            if (!importedSaveDirectory.isEmpty()) {
                entry.saveDirectory = importedSaveDirectory;
            }
        }

        Set<String> mergedPaths = new HashSet<>();
        SharedPreferences.Editor editor = preferences.edit();
        if (exclusionsChanged) {
            editor.putStringSet(GAME_LIBRARY_EXCLUDED_IDENTITIES, excludedIdentities);
        }
        for (String obsoletePath : obsoleteSourcePaths) {
            editor.remove(GAME_LAST_RUN_TIMES + ":" + obsoletePath)
                    .remove(GAME_SAVE_DIRECTORIES + ":" + obsoletePath);
        }
        for (GameLibraryEntry entry : entries.values()) {
            mergedPaths.add(entry.path);
            for (String sourcePath : entry.sourcePaths) {
                if (!sourcePath.equals(entry.path)) {
                    editor.remove(GAME_LAST_RUN_TIMES + ":" + sourcePath)
                            .remove(GAME_SAVE_DIRECTORIES + ":" + sourcePath);
                }
            }
            if (entry.lastRunTime > 0L) {
                editor.putLong(GAME_LAST_RUN_TIMES + ":" + entry.path, entry.lastRunTime);
            }
            if (!entry.saveDirectory.isEmpty()) {
                editor.putString(GAME_SAVE_DIRECTORIES + ":" + entry.path,
                        entry.saveDirectory);
            }
        }
        editor.putStringSet(GAME_LIBRARY_PATHS, mergedPaths).apply();
        return mergedPaths;
    }

    private synchronized void mergeGameLibrary(
            Uri directoryUri, List<String> paths, boolean restoreExcludedPaths) {
        if (paths == null) {
            return;
        }
        SharedPreferences preferences = getSharedPreferences(
                GAME_LIBRARY_PREFERENCES, MODE_PRIVATE);
        mergeGameLibraryPaths(preferences, directoryUri, paths, restoreExcludedPaths);
        if (directoryUri != null) {
            Set<String> directories = new HashSet<>(preferences.getStringSet(
                    GAME_LIBRARY_DIRECTORIES, Collections.emptySet()));
            directories.add(directoryUri.toString());
            preferences.edit()
                    .putStringSet(GAME_LIBRARY_DIRECTORIES, directories)
                    .apply();
        }
    }
    private void requestGameFile() {
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                    Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(intent, REQUEST_GAME_FILE);
        });
    }

    private void requestGameFileDirectoryPermission(Uri fileUri) {
        runOnUiThread(() -> new AlertDialog.Builder(this)
                .setTitle(gameSelectionText("\u9009\u62E9\u6E38\u620F\u6587\u4EF6\u5939",
                        "Select Game Folder"))
                .setMessage(gameSelectionText(
                        "\u9009\u62E9\u8BE5\u6E38\u620F\u6240\u5728\u7684\u6587\u4EF6\u5939\uFF0C\u4EE5\u4FBF\u4FDD\u5B58\u5B58\u6863\u548C\u65E5\u5FD7\u3002",
                        "Select the folder containing this game so saves and logs can be stored."))
                .setPositiveButton(gameSelectionText("\u9009\u62E9\u6587\u4EF6\u5939",
                                "Select Folder"),
                        (dialog, which) -> launchGameFileDirectoryPicker(fileUri))
                .setNegativeButton(gameSelectionText("\u53D6\u6D88", "Cancel"),
                        (dialog, which) -> cancelPendingGameFileSelection())
                .setOnCancelListener(dialog -> cancelPendingGameFileSelection())
                .show());
    }

    private void showGameFileDirectoryMismatchDialog(Uri fileUri) {
        runOnUiThread(() -> new AlertDialog.Builder(this)
                .setTitle(gameSelectionText("\u6587\u4EF6\u5939\u4E0D\u5339\u914D",
                        "Folder Mismatch"))
                .setMessage(gameSelectionText(
                        "\u6240\u9009\u6587\u4EF6\u5939\u4E2D\u672A\u627E\u5230\u8BE5\u6E38\u620F\uFF0C\u8BF7\u9009\u62E9\u6B63\u786E\u7684\u6587\u4EF6\u5939\u3002",
                        "The selected folder does not contain this game. Choose the correct folder."))
                .setPositiveButton(gameSelectionText("\u91CD\u65B0\u9009\u62E9",
                                "Choose Again"),
                        (dialog, which) -> launchGameFileDirectoryPicker(fileUri))
                .setNegativeButton(gameSelectionText("\u53D6\u6D88", "Cancel"),
                        (dialog, which) -> cancelPendingGameFileSelection())
                .setOnCancelListener(dialog -> cancelPendingGameFileSelection())
                .show());
    }

    private String gameSelectionText(String chineseText, String englishText) {
        return gameSelectionChinese ? chineseText : englishText;
    }

    private void cancelPendingGameFileSelection() {
        Uri fileUri = pendingGameFileUri;
        pendingGameFileUri = null;
        if (fileUri != null) {
            releasePersistedUriPermission(fileUri);
        }
        completeGameSelection("");
    }

    private void launchGameFileDirectoryPicker(Uri fileUri) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                Intent.FLAG_GRANT_WRITE_URI_PERMISSION |
                Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Uri parentUri = getParentDocumentUri(fileUri);
            if (parentUri != null) {
                intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, parentUri);
            }
        }
        startActivityForResult(intent, REQUEST_GAME_FILE_DIRECTORY);
    }

    private Uri getParentDocumentUri(Uri fileUri) {
        if (fileUri == null || !DocumentsContract.isDocumentUri(this, fileUri)) {
            return null;
        }
        try {
            String documentId = DocumentsContract.getDocumentId(fileUri);
            int separator = documentId.lastIndexOf('/');
            if (separator < 0) {
                separator = documentId.lastIndexOf(':');
            }
            if (separator < 0) {
                return null;
            }
            int parentEnd = documentId.charAt(separator) == ':' ? separator + 1 : separator;
            String parentId = documentId.substring(0, parentEnd);
            return DocumentsContract.buildDocumentUri(fileUri.getAuthority(), parentId);
        } catch (IllegalArgumentException exception) {
            Log.w(TAG, "Unable to determine selected game parent directory", exception);
            return null;
        }
    }

    private boolean directoryContainsDocument(Uri directoryUri, Uri fileUri) {
        if (directoryUri == null || fileUri == null) {
            return false;
        }
        try {
            String fileDocumentId = DocumentsContract.getDocumentId(fileUri);
            String directoryDocumentId = DocumentsContract.getTreeDocumentId(directoryUri);
            String fileDisplayName = queryDisplayName(fileUri);
            long fileSize = queryDocumentSize(fileUri);
            Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(
                    directoryUri, directoryDocumentId);
            try (Cursor cursor = getContentResolver().query(childrenUri,
                    new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                            DocumentsContract.Document.COLUMN_SIZE},
                    null, null, null)) {
                if (cursor == null) {
                    return false;
                }
                int documentIdIndex = cursor.getColumnIndex(
                        DocumentsContract.Document.COLUMN_DOCUMENT_ID);
                int displayNameIndex = cursor.getColumnIndex(
                        DocumentsContract.Document.COLUMN_DISPLAY_NAME);
                int sizeIndex = cursor.getColumnIndex(
                        DocumentsContract.Document.COLUMN_SIZE);
                while (cursor.moveToNext()) {
                    if (documentIdIndex < 0) {
                        continue;
                    }
                    String childDocumentId = cursor.getString(documentIdIndex);
                    if (java.util.Objects.equals(directoryUri.getAuthority(),
                            fileUri.getAuthority()) &&
                            fileDocumentId.equals(childDocumentId)) {
                        return true;
                    }
                    String childDisplayName = displayNameIndex >= 0
                            ? cursor.getString(displayNameIndex) : null;
                    if (fileDisplayName == null ||
                            !fileDisplayName.equals(childDisplayName)) {
                        continue;
                    }
                    Uri childUri = DocumentsContract.buildDocumentUriUsingTree(
                            directoryUri, childDocumentId);
                    if (documentsReferToSameFile(fileUri, childUri)) {
                        return true;
                    }
                    long childSize = sizeIndex >= 0 && !cursor.isNull(sizeIndex)
                            ? cursor.getLong(sizeIndex) : -1L;
                    if (fileSize >= 0L && fileSize == childSize) {
                        return true;
                    }
                }
            }
        } catch (Exception exception) {
            Log.e(TAG, "Unable to verify selected game directory", exception);
        }
        return false;
    }

    private boolean documentsReferToSameFile(Uri firstUri, Uri secondUri) {
        try (ParcelFileDescriptor firstDescriptor =
                     getContentResolver().openFileDescriptor(firstUri, "r");
             ParcelFileDescriptor secondDescriptor =
                     getContentResolver().openFileDescriptor(secondUri, "r")) {
            if (firstDescriptor == null || secondDescriptor == null) {
                return false;
            }
            StructStat firstStat = Os.fstat(firstDescriptor.getFileDescriptor());
            StructStat secondStat = Os.fstat(secondDescriptor.getFileDescriptor());
            return firstStat.st_ino != 0L && firstStat.st_dev == secondStat.st_dev &&
                    firstStat.st_ino == secondStat.st_ino;
        } catch (Exception exception) {
            return false;
        }
    }

    private long queryDocumentSize(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri,
                new String[]{OpenableColumns.SIZE}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int index = cursor.getColumnIndex(OpenableColumns.SIZE);
                if (index >= 0 && !cursor.isNull(index)) {
                    return cursor.getLong(index);
                }
            }
        } catch (Exception exception) {
            return -1L;
        }
        return -1L;
    }

    private void requestGameDirectory() {
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                    Intent.FLAG_GRANT_WRITE_URI_PERMISSION |
                    Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(intent, REQUEST_GAME_DIRECTORY);
        });
    }

    public String[] listImportedGamePaths() {
        SharedPreferences preferences = getSharedPreferences(GAME_LIBRARY_PREFERENCES, MODE_PRIVATE);
        Set<String> stored = mergeGameLibraryPaths(
                preferences, null, Collections.emptyList(), false);
        List<String> paths = new ArrayList<>(stored);
        Collections.sort(paths, (left, right) -> {
            long leftRunTime = preferences.getLong(
                    GAME_LAST_RUN_TIMES + ":" + left, 0L);
            long rightRunTime = preferences.getLong(
                    GAME_LAST_RUN_TIMES + ":" + right, 0L);
            int timeOrder = Long.compare(rightRunTime, leftRunTime);
            if (timeOrder != 0) {
                return timeOrder;
            }
            return gameDisplayNameFromPath(left).compareToIgnoreCase(
                    gameDisplayNameFromPath(right));
        });
        return paths.toArray(new String[0]);
    }

    public void rememberGameRun(String gamePath) {
        if (gamePath == null || gamePath.isEmpty()) {
            return;
        }
        getSharedPreferences(GAME_LIBRARY_PREFERENCES, MODE_PRIVATE).edit()
                .putLong(GAME_LAST_RUN_TIMES + ":" + gamePath,
                        System.currentTimeMillis())
                .apply();
    }

    public String getGameSaveDirectory(String gamePath, String gameIdentity) {
        String selectedDirectory = getSelectedGameSaveDirectory(gamePath);
        if (canWriteSaveDirectory(selectedDirectory)) {
            return selectedDirectory;
        }
        return getPrivateGameSaveDirectory(gameIdentity);
    }

    public String getCcGameSaveDirectory(String gamePath, String gameIdentity) {
        String selectedDirectory = getSelectedGameSaveDirectory(gamePath);
        if (canWriteSaveDirectory(selectedDirectory)) {
            return selectedDirectory;
        }
        return getPrivateGameSaveDirectory(gameIdentity);
    }

    public String getPrivateLogDirectory() {
        File dataDirectory = new File(getApplicationInfo().dataDir);
        boolean debuggable =
                (getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        return preparePrivateLogDirectory(dataDirectory, getFilesDir(), debuggable);
    }

    String preparePrivateLogDirectory(
            File dataDirectory, File filesDirectory, boolean debuggable) {
        File directory = new File(dataDirectory, PRIVATE_LOG_ROOT);
        if (!debuggable) {
            deletePrivateDebugFiles(dataDirectory, filesDirectory, directory);
            Log.i(TAG, "Private debug file logging is disabled");
            return "";
        }
        if (!directory.exists() && !directory.mkdirs()) {
            Log.e(TAG, "Unable to create private log directory: " + directory);
            return "";
        }
        return directory.getAbsolutePath();
    }

    private void deletePrivateDebugFiles(
            File dataDirectory, File filesDirectory, File logDirectory) {
        deletePrivateLogTree(logDirectory);
        File[] files = filesDirectory.listFiles();
        if (files != null) {
            for (File file : files) {
                String name = file.getName();
                if ("dingoopie-native.log".equals(name) ||
                        "dingoopie-native.err".equals(name) ||
                        "dingoopie-audio-validation.wav".equals(name) ||
                        "dingoopie-audio-validation.csv".equals(name) ||
                        (name.startsWith("DingooPie-debug-") && name.endsWith(".log"))) {
                    deletePrivateLogTree(file);
                }
            }
        }
        File saveRoot = new File(dataDirectory, PRIVATE_SAVE_ROOT);
        File[] gameDirectories = saveRoot.listFiles();
        if (gameDirectories != null) {
            for (File gameDirectory : gameDirectories) {
                File[] gameFiles = gameDirectory.listFiles();
                if (gameFiles == null) {
                    continue;
                }
                for (File gameFile : gameFiles) {
                    String name = gameFile.getName();
                    if (name.startsWith("DingooPie-crash-") && name.endsWith(".log")) {
                        deletePrivateLogTree(gameFile);
                    }
                }
            }
        }
    }

    private void deletePrivateLogTree(File file) {
        if (file == null || !file.exists()) {
            return;
        }
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deletePrivateLogTree(child);
            }
        }
        if (!file.delete()) {
            Log.w(TAG, "Unable to remove private debug log path: " + file);
        }
    }

    public boolean isPrivateGameSaveDirectory(String directoryText) {
        if (directoryText == null || directoryText.isEmpty()) {
            return false;
        }
        try {
            Uri directoryUri = Uri.parse(directoryText);
            if (!"file".equalsIgnoreCase(directoryUri.getScheme())) {
                return false;
            }
            File saveRoot = new File(getApplicationInfo().dataDir, PRIVATE_SAVE_ROOT);
            File directory = new File(directoryUri.getPath());
            String rootPath = saveRoot.getCanonicalPath();
            String directoryPath = directory.getCanonicalPath();
            return !directoryPath.equals(rootPath) &&
                    directoryPath.startsWith(rootPath + File.separator);
        } catch (Exception exception) {
            Log.e(TAG, "Unable to classify game save directory: " + directoryText, exception);
            return false;
        }
    }

    private String getSelectedGameSaveDirectory(String gamePath) {
        return getSharedPreferences(GAME_LIBRARY_PREFERENCES, MODE_PRIVATE)
                .getString(GAME_SAVE_DIRECTORIES + ":" + gamePath, "");
    }

    private boolean canWriteSaveDirectory(String directoryText) {
        if (directoryText == null || directoryText.isEmpty()) {
            return false;
        }
        Uri directoryUri = Uri.parse(directoryText);
        if ("file".equalsIgnoreCase(directoryUri.getScheme())) {
            return true;
        }
        if (!"content".equalsIgnoreCase(directoryUri.getScheme())) {
            return false;
        }
        for (UriPermission permission : getContentResolver().getPersistedUriPermissions()) {
            if (permission.isWritePermission() && directoryUri.equals(permission.getUri())) {
                return true;
            }
        }
        Log.w(TAG, "Save directory write permission is unavailable: " + directoryUri);
        return false;
    }

    private String getPrivateGameSaveDirectory(String gameIdentity) {
        if (gameIdentity == null || gameIdentity.isEmpty()) {
            Log.e(TAG, "Missing game identity for private save directory");
            return "";
        }
        String saveKey = gameIdentity.toLowerCase(Locale.ROOT);
        File saveRoot = new File(getApplicationInfo().dataDir, PRIVATE_SAVE_ROOT);
        if (!saveRoot.exists() && !saveRoot.mkdirs()) {
            Log.e(TAG, "Unable to create private save root: " + saveRoot);
            return "";
        }
        File directory = new File(saveRoot, saveKey);
        if (!directory.exists() && !directory.mkdirs()) {
            Log.e(TAG, "Unable to create private save directory: " + directory);
            return "";
        }
        return Uri.fromFile(directory).toString();
    }

    public int openGameSaveFileDescriptor(String directoryUriText, String fileName, String mode) {
        if (directoryUriText == null || fileName == null || fileName.isEmpty()) {
            return -1;
        }
        try {
            List<String> pathSegments = normalizeSavePath(fileName);
            if (pathSegments == null || pathSegments.isEmpty()) {
                return -1;
            }
            Uri directoryUri = Uri.parse(directoryUriText);
            if ("file".equalsIgnoreCase(directoryUri.getScheme())) {
                File directory = new File(directoryUri.getPath());
                File file = directory;
                for (String pathSegment : pathSegments) {
                    file = new File(file, pathSegment);
                }
                String directoryPath = directory.getCanonicalPath();
                String filePath = file.getCanonicalPath();
                if (!filePath.equals(directoryPath) &&
                        !filePath.startsWith(directoryPath + File.separator)) {
                    return -1;
                }
                boolean writing = mode != null && (mode.contains("w") || mode.contains("a") || mode.contains("+"));
                if (writing) {
                    File parent = file.getParentFile();
                    if (parent == null || (!parent.exists() && !parent.mkdirs())) {
                        return -1;
                    }
                } else if (!file.isFile()) {
                    return -1;
                }
                try (java.io.RandomAccessFile random = new java.io.RandomAccessFile(
                        file, writing ? "rw" : "r")) {
                    if (mode != null && mode.contains("w") && !mode.contains("+") && !mode.contains("a")) {
                        random.setLength(0);
                    } else if (mode != null && mode.contains("a")) {
                        random.seek(random.length());
                    }
                    ParcelFileDescriptor descriptor = ParcelFileDescriptor.dup(
                        random.getFD());
                    return descriptor.detachFd();
                }
            }
            boolean writing = mode != null && (mode.contains("w") || mode.contains("a") || mode.contains("+"));
            Uri parentUri = asDocumentDirectoryUri(directoryUri);
            if (parentUri == null) {
                return -1;
            }
            for (int index = 0; index + 1 < pathSegments.size(); index++) {
                String pathSegment = pathSegments.get(index);
                Uri childUri = findChildDocument(parentUri, pathSegment);
                if (childUri == null && writing) {
                    childUri = DocumentsContract.createDocument(getContentResolver(), parentUri,
                            DocumentsContract.Document.MIME_TYPE_DIR, pathSegment);
                }
                if (childUri == null) {
                    return -1;
                }
                parentUri = childUri;
            }
            String leafName = pathSegments.get(pathSegments.size() - 1);
            Uri fileUri = findChildDocument(parentUri, leafName);
            if (fileUri == null && writing) {
                fileUri = DocumentsContract.createDocument(getContentResolver(), parentUri,
                        "application/octet-stream", leafName);
            }
            if (fileUri == null) {
                return -1;
            }
            ParcelFileDescriptor descriptor = openSaveDocument(fileUri, mode);
            return descriptor == null ? -1 : descriptor.detachFd();
        } catch (Exception exception) {
            Log.e(TAG, "Unable to open game save file: " + fileName, exception);
            return -1;
        }
    }

    public long getGameSaveFileModifiedTime(String directoryUriText, String fileName) {
        if (directoryUriText == null || fileName == null || fileName.isEmpty()) {
            return 0L;
        }
        try {
            List<String> pathSegments = normalizeSavePath(fileName);
            if (pathSegments == null || pathSegments.isEmpty()) {
                return 0L;
            }
            Uri directoryUri = Uri.parse(directoryUriText);
            if ("file".equalsIgnoreCase(directoryUri.getScheme())) {
                File directory = new File(directoryUri.getPath());
                File file = directory;
                for (String pathSegment : pathSegments) {
                    file = new File(file, pathSegment);
                }
                String directoryPath = directory.getCanonicalPath();
                String filePath = file.getCanonicalPath();
                if (!filePath.equals(directoryPath) &&
                        !filePath.startsWith(directoryPath + File.separator)) {
                    return 0L;
                }
                return file.isFile() ? file.lastModified() : 0L;
            }
            Uri parentUri = asDocumentDirectoryUri(directoryUri);
            if (parentUri == null) {
                return 0L;
            }
            for (int index = 0; index + 1 < pathSegments.size(); index++) {
                parentUri = findChildDocument(parentUri, pathSegments.get(index));
                if (parentUri == null) {
                    return 0L;
                }
            }
            Uri fileUri = findChildDocument(parentUri,
                    pathSegments.get(pathSegments.size() - 1));
            if (fileUri == null) {
                return 0L;
            }
            try (android.database.Cursor cursor = getContentResolver().query(
                    fileUri,
                    new String[]{DocumentsContract.Document.COLUMN_LAST_MODIFIED},
                    null, null, null)) {
                if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) {
                    return cursor.getLong(0);
                }
            }
        } catch (Exception exception) {
            Log.e(TAG, "Unable to query game save file time: " + fileName, exception);
        }
        return 0L;
    }

    boolean shouldIgnoreEmptyAutomaticScan(
            Uri directoryUri, List<String> paths, boolean notifySelection) {
        return paths != null && paths.isEmpty() && !notifySelection &&
                hasStoredGamesForDirectory(directoryUri);
    }

    private boolean hasStoredGamesForDirectory(Uri directoryUri) {
        if (directoryUri == null) {
            return false;
        }
        SharedPreferences preferences = getSharedPreferences(
                GAME_LIBRARY_PREFERENCES, MODE_PRIVATE);
        String directoryText = directoryUri.toString();
        for (String path : preferences.getStringSet(
                GAME_LIBRARY_PATHS, Collections.emptySet())) {
            if (directoryText.equals(preferences.getString(
                    GAME_SAVE_DIRECTORIES + ":" + path, ""))) {
                return true;
            }
        }
        return false;
    }

    public boolean deleteGameSaveFile(String directoryUriText, String fileName) {
        if (directoryUriText == null || fileName == null || fileName.isEmpty()) {
            return false;
        }
        try {
            List<String> pathSegments = normalizeSavePath(fileName);
            if (pathSegments == null || pathSegments.isEmpty()) {
                return false;
            }
            Uri directoryUri = Uri.parse(directoryUriText);
            if ("file".equalsIgnoreCase(directoryUri.getScheme())) {
                File directory = new File(directoryUri.getPath());
                File file = directory;
                for (String pathSegment : pathSegments) {
                    file = new File(file, pathSegment);
                }
                String directoryPath = directory.getCanonicalPath();
                String filePath = file.getCanonicalPath();
                if (!filePath.equals(directoryPath) &&
                        !filePath.startsWith(directoryPath + File.separator)) {
                    return false;
                }
                return !file.exists() || file.delete();
            }
            Uri parentUri = asDocumentDirectoryUri(directoryUri);
            if (parentUri == null) {
                return false;
            }
            for (int index = 0; index + 1 < pathSegments.size(); index++) {
                parentUri = findChildDocument(parentUri, pathSegments.get(index));
                if (parentUri == null) {
                    return true;
                }
            }
            Uri fileUri = findChildDocument(parentUri,
                    pathSegments.get(pathSegments.size() - 1));
            return fileUri == null || DocumentsContract.deleteDocument(
                    getContentResolver(), fileUri);
        } catch (Exception exception) {
            Log.e(TAG, "Unable to delete game save file: " + fileName, exception);
            return false;
        }
    }

    private List<String> normalizeSavePath(String fileName) {
        String normalized = fileName.replace('\\', '/');
        if (normalized.startsWith("/") || normalized.indexOf('\0') >= 0) {
            return null;
        }
        List<String> result = new ArrayList<>();
        for (String pathSegment : normalized.split("/")) {
            if (pathSegment.isEmpty() || ".".equals(pathSegment)) {
                continue;
            }
            if ("..".equals(pathSegment) || pathSegment.indexOf(':') >= 0) {
                return null;
            }
            result.add(pathSegment);
        }
        return result;
    }

    private Uri asDocumentDirectoryUri(Uri directoryUri) {
        if (directoryUri == null || !"content".equalsIgnoreCase(directoryUri.getScheme())) {
            return null;
        }
        try {
            String documentId = DocumentsContract.getDocumentId(directoryUri);
            if (documentId != null && !documentId.isEmpty()) {
                return directoryUri;
            }
        } catch (IllegalArgumentException ignored) {
        }
        try {
            String treeDocumentId = DocumentsContract.getTreeDocumentId(directoryUri);
            return DocumentsContract.buildDocumentUriUsingTree(directoryUri, treeDocumentId);
        } catch (IllegalArgumentException exception) {
            Log.e(TAG, "Invalid save directory URI: " + directoryUri, exception);
            return null;
        }
    }

    private ParcelFileDescriptor openSaveDocument(Uri fileUri, String mode) throws IOException {
        boolean writing = mode != null &&
                (mode.contains("w") || mode.contains("a") || mode.contains("+"));
        if (!writing) {
            return getContentResolver().openFileDescriptor(fileUri, "r");
        }
        boolean truncate = mode.contains("w") && !mode.contains("a");
        boolean append = mode.contains("a");
        String providerMode = append ? "wa" : truncate ? "rwt" : "rw";
        ParcelFileDescriptor descriptor;
        try {
            descriptor = getContentResolver().openFileDescriptor(fileUri, providerMode);
        } catch (IOException | IllegalArgumentException exception) {
            descriptor = getContentResolver().openFileDescriptor(fileUri, "rw");
            providerMode = "rw";
        }
        if (descriptor == null) {
            return null;
        }
        try {
            if (truncate && !"rwt".equals(providerMode)) {
                Os.ftruncate(descriptor.getFileDescriptor(), 0L);
            } else if (append && !"wa".equals(providerMode)) {
                Os.lseek(descriptor.getFileDescriptor(), 0L, OsConstants.SEEK_END);
            }
        } catch (ErrnoException exception) {
            descriptor.close();
            throw new IOException("Unable to position save file", exception);
        }
        return descriptor;
    }

    private Uri findChildDocument(Uri directoryUri, String fileName) {
        String documentId;
        try {
            documentId = DocumentsContract.getDocumentId(directoryUri);
        } catch (IllegalArgumentException exception) {
            documentId = DocumentsContract.getTreeDocumentId(directoryUri);
        }
        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(directoryUri, documentId);
        try (Cursor cursor = getContentResolver().query(childrenUri,
                new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                        DocumentsContract.Document.COLUMN_DISPLAY_NAME},
                null, null, null)) {
            if (cursor == null) {
                return null;
            }
            int idIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DOCUMENT_ID);
            int nameIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DISPLAY_NAME);
            while (cursor.moveToNext()) {
                if (idIndex >= 0 && nameIndex >= 0 && fileName.equals(cursor.getString(nameIndex))) {
                    return DocumentsContract.buildDocumentUriUsingTree(directoryUri,
                            cursor.getString(idIndex));
                }
            }
        }
        return null;
    }

    private void rememberGameSaveDirectory(String gamePath, Uri directoryUri) {
        getSharedPreferences(GAME_LIBRARY_PREFERENCES, MODE_PRIVATE).edit()
                .putString(GAME_SAVE_DIRECTORIES + ":" + gamePath, directoryUri.toString())
                .apply();
    }

    public int openGameFileDescriptor(String gamePath) {
        Uri uri = gameUriFromPath(gamePath);
        if (uri == null) {
            return -1;
        }
        try {
            ParcelFileDescriptor descriptor = getContentResolver().openFileDescriptor(uri, "r");
            return descriptor == null ? -1 : descriptor.detachFd();
        } catch (IOException | SecurityException exception) {
            Log.e(TAG, "Unable to open game URI", exception);
            return -1;
        }
    }

    public int openSiblingGameFileDescriptor(String gamePath, String fileName) {
        if (gamePath == null || fileName == null || fileName.isEmpty()) {
            return -1;
        }
        try {
            Uri fileUri = findSiblingDocument(gameUriFromPath(gamePath), fileName);
            if (fileUri == null) {
                return -1;
            }
            ParcelFileDescriptor descriptor = getContentResolver().openFileDescriptor(fileUri, "r");
            return descriptor == null ? -1 : descriptor.detachFd();
        } catch (Exception exception) {
            Log.e(TAG, "Unable to open sibling game file", exception);
            return -1;
        }
    }

    private Uri findSiblingDocument(Uri gameUri, String fileName) {
        if (gameUri == null || !DocumentsContract.isDocumentUri(this, gameUri)) {
            return null;
        }
        String documentId = DocumentsContract.getDocumentId(gameUri);
        int separator = documentId.lastIndexOf('/');
        if (separator < 0) {
            separator = documentId.lastIndexOf(':');
        }
        if (separator < 0) {
            return null;
        }
        String parentId = documentId.substring(0, separator +
                (documentId.charAt(separator) == ':' ? 1 : 0));
        Uri parentUri = DocumentsContract.buildDocumentUriUsingTree(gameUri, parentId);
        return findChildDocument(parentUri, fileName);
    }

    public synchronized boolean removeGamePath(String gamePath) {
        SharedPreferences preferences = getSharedPreferences(
                GAME_LIBRARY_PREFERENCES, MODE_PRIVATE);
        Set<String> paths = new HashSet<>(preferences.getStringSet(
                GAME_LIBRARY_PATHS, Collections.emptySet()));
        if (!paths.remove(gamePath)) {
            return false;
        }
        String removedSaveDirectory = preferences.getString(
                GAME_SAVE_DIRECTORIES + ":" + gamePath, "");
        Set<String> excludedIdentities = new HashSet<>(preferences.getStringSet(
                GAME_LIBRARY_EXCLUDED_IDENTITIES, Collections.emptySet()));
        excludedIdentities.add(gameDocumentIdentity(gamePath));
        boolean saved = preferences.edit().putStringSet(GAME_LIBRARY_PATHS, paths)
                .putStringSet(GAME_LIBRARY_EXCLUDED_IDENTITIES, excludedIdentities)
                .remove(GAME_SAVE_DIRECTORIES + ":" + gamePath)
                .remove(GAME_LAST_RUN_TIMES + ":" + gamePath)
                .commit();
        if (!saved) {
            return false;
        }
        releaseUnusedGameFilePermission(gamePath, paths);
        releaseUnusedSaveDirectoryPermission(removedSaveDirectory, paths, preferences);
        synchronized (this) {
            if (gamePath.equals(selectedGamePath)) {
                selectedGamePath = null;
            }
        }
        return true;
    }

    private void releaseUnusedGameFilePermission(String removedPath,
            Set<String> remainingPaths) {
        Uri removedUri = gameUriFromPath(removedPath);
        if (removedUri == null) {
            return;
        }
        for (String path : remainingPaths) {
            if (removedUri.equals(gameUriFromPath(path))) {
                return;
            }
        }
        releasePersistedUriPermission(removedUri);
    }

    private void releaseUnusedSaveDirectoryPermission(String directoryText,
            Set<String> remainingPaths, SharedPreferences preferences) {
        if (directoryText == null || directoryText.isEmpty() ||
                preferences.getStringSet(GAME_LIBRARY_DIRECTORIES,
                        Collections.emptySet()).contains(directoryText)) {
            return;
        }
        for (String path : remainingPaths) {
            if (directoryText.equals(preferences.getString(
                    GAME_SAVE_DIRECTORIES + ":" + path, ""))) {
                return;
            }
        }
        Uri directoryUri = Uri.parse(directoryText);
        if (!"content".equalsIgnoreCase(directoryUri.getScheme())) {
            return;
        }
        releasePersistedUriPermission(directoryUri);
    }

    private void releasePersistedUriPermission(Uri uri) {
        int permissionFlags = 0;
        for (UriPermission permission : getContentResolver().getPersistedUriPermissions()) {
            if (!uri.equals(permission.getUri())) {
                continue;
            }
            if (permission.isReadPermission()) {
                permissionFlags |= Intent.FLAG_GRANT_READ_URI_PERMISSION;
            }
            if (permission.isWritePermission()) {
                permissionFlags |= Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
            }
            break;
        }
        if (permissionFlags == 0) {
            return;
        }
        try {
            getContentResolver().releasePersistableUriPermission(
                    uri, permissionFlags);
        } catch (SecurityException exception) {
            Log.w(TAG, "Unable to release unused game URI permission", exception);
        }
    }

    private String buildGamePath(Uri uri, String displayName) {
        String encodedUri = Base64.encodeToString(uri.toString().getBytes(StandardCharsets.UTF_8),
                Base64.URL_SAFE | Base64.NO_WRAP | Base64.NO_PADDING);
        return GAME_PATH_PREFIX + encodedUri + "/" + displayName;
    }

    private void handleExternalGameLaunchIntent(Intent intent) {
        String path = resolveExternalGameLaunchPath(intent);
        if (path.isEmpty()) {
            return;
        }
        Log.i(TAG, "External frontend requested game=" + path);
        queueExternalGameLaunch(path);
    }

    private String resolveExternalGameLaunchPath(Intent intent) {
        if (intent == null) {
            return "";
        }
        Uri dataUri = intent.getData();
        if (dataUri != null && "dingoopie".equalsIgnoreCase(dataUri.getScheme())) {
            for (String key : ExternalGameLaunchRequest.URI_QUERY_KEYS) {
                String path = resolveExternalGameCandidate(dataUri.getQueryParameter(key), intent);
                if (!path.isEmpty()) {
                    return path;
                }
            }
        } else {
            String path = resolveExternalGameUri(dataUri, intent);
            if (!path.isEmpty()) {
                return path;
            }
        }

        Object stream = intent.getParcelableExtra(Intent.EXTRA_STREAM);
        if (stream instanceof Uri) {
            String path = resolveExternalGameUri((Uri) stream, intent);
            if (!path.isEmpty()) {
                return path;
            }
        }
        ClipData clipData = intent.getClipData();
        if (clipData != null && clipData.getItemCount() > 0) {
            String path = resolveExternalGameUri(clipData.getItemAt(0).getUri(), intent);
            if (!path.isEmpty()) {
                return path;
            }
        }

        Bundle extras = intent.getExtras();
        if (extras == null) {
            return "";
        }
        for (String key : ExternalGameLaunchRequest.PATH_EXTRA_KEYS) {
            Object value = extras.get(key);
            String path = value instanceof Uri ?
                    resolveExternalGameUri((Uri) value, intent) :
                    resolveExternalGameCandidate(value instanceof String ? (String) value : null,
                            intent);
            if (!path.isEmpty()) {
                return path;
            }
        }
        return "";
    }

    private String resolveExternalGameCandidate(String candidate, Intent intent) {
        String path = ExternalGameLaunchRequest.normalizeStringPath(candidate);
        if (path.isEmpty()) {
            return "";
        }
        if (path.startsWith(GAME_PATH_PREFIX)) {
            return ExternalGameLaunchRequest.isSupportedGameName(path) ? path : "";
        }
        Uri uri = Uri.parse(path);
        if (uri.getScheme() != null) {
            return resolveExternalGameUri(uri, intent);
        }
        return ExternalGameLaunchRequest.isSupportedGameName(path) ? path : "";
    }

    private String resolveExternalGameUri(Uri uri, Intent intent) {
        if (uri == null) {
            return "";
        }
        String scheme = uri.getScheme();
        if (scheme == null || scheme.isEmpty()) {
            return resolveExternalGameCandidate(uri.toString(), intent);
        }
        if ("file".equalsIgnoreCase(scheme)) {
            String path = ExternalGameLaunchRequest.normalizeStringPath(uri.toString());
            return ExternalGameLaunchRequest.isSupportedGameName(path) ? path : "";
        }
        if (!"content".equalsIgnoreCase(scheme)) {
            return "";
        }

        String displayName = queryDisplayName(uri);
        if (!ExternalGameLaunchRequest.isSupportedGameName(displayName)) {
            return "";
        }
        try (ParcelFileDescriptor descriptor = getContentResolver().openFileDescriptor(uri, "r")) {
            if (descriptor == null) {
                return "";
            }
        } catch (IOException | SecurityException exception) {
            Log.e(TAG, "Unable to open externally requested game URI", exception);
            return "";
        }
        if ((intent.getFlags() & Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION) != 0 &&
                (intent.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION) != 0) {
            try {
                getContentResolver().takePersistableUriPermission(
                        uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (SecurityException exception) {
                Log.w(TAG, "External game URI permission is temporary", exception);
            }
        }
        displayName = displayName.replaceAll("[^\\p{L}\\p{N}._ -]", "_");
        return buildGamePath(uri, displayName);
    }

    public synchronized String consumeGameAutomationPath() {
        Intent intent = getIntent();
        boolean debuggable = (getApplicationInfo().flags &
                android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        if (!debuggable || gameAutomationPathConsumed || intent == null) {
            return "";
        }
        gameAutomationPathConsumed = true;
        String path = intent.getStringExtra(EXTRA_GAME_AUTOMATION_PATH);
        if (path == null || !isSupportedGameFileName(path)) {
            return "";
        }
        return path;
    }

    public synchronized boolean consumeAudioValidationAutomationEnabled() {
        Intent intent = getIntent();
        boolean debuggable = (getApplicationInfo().flags &
                android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        if (!debuggable || audioValidationAutomationConsumed || intent == null) {
            return false;
        }
        audioValidationAutomationConsumed = true;
        return intent.getBooleanExtra(EXTRA_AUDIO_VALIDATION_AUTOMATION, false);
    }

    private boolean isSupportedGameFileName(String displayName) {
        if (displayName == null) {
            return false;
        }
        String lowerName = displayName.toLowerCase(Locale.ROOT);
        return lowerName.endsWith(".app") || lowerName.endsWith(".cc");
    }

    private Uri gameUriFromPath(String gamePath) {
        if (gamePath == null || !gamePath.startsWith(GAME_PATH_PREFIX)) {
            return null;
        }
        int separator = gamePath.indexOf('/', GAME_PATH_PREFIX.length());
        if (separator <= GAME_PATH_PREFIX.length()) {
            return null;
        }
        try {
            String encodedUri = gamePath.substring(GAME_PATH_PREFIX.length(), separator);
            byte[] bytes = Base64.decode(encodedUri,
                    Base64.URL_SAFE | Base64.NO_WRAP | Base64.NO_PADDING);
            return Uri.parse(new String(bytes, StandardCharsets.UTF_8));
        } catch (IllegalArgumentException exception) {
            Log.e(TAG, "Invalid persisted game path", exception);
            return null;
        }
    }

    private String gameDisplayNameFromPath(String gamePath) {
        return gamePath.substring(gamePath.lastIndexOf('/') + 1);
    }

    private String queryDisplayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri,
            new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (index >= 0) {
                    return cursor.getString(index);
                }
            }
        }
        return uri.getLastPathSegment();
    }
}
