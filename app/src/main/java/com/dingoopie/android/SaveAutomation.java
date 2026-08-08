package com.dingoopie.android;

import android.content.Intent;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

final class SaveAutomation {
    private static final String TAG = "DingooPie";
    private static final String EXTRA_SAVE_AUTOMATION = "dingoopie.save_automation";

    private SaveAutomation() {
    }

    static void schedule(DingooPieActivity activity, Intent intent) {
        boolean debuggable = (activity.getApplicationInfo().flags &
                ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        if (!debuggable || intent == null ||
                !intent.getBooleanExtra(EXTRA_SAVE_AUTOMATION, false)) {
            return;
        }
        activity.getWindow().getDecorView().postDelayed(() ->
                new Thread(() -> run(activity), "DingooPieSaveAutomation").start(), 800L);
    }

    private static void run(DingooPieActivity activity) {
        String appIdentity =
                "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
        String ccIdentity =
                "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        String appDirectory = activity.getGameSaveDirectory(
                "android-content://automation/sample.app",
                appIdentity);
        String ccDirectory = activity.getCcGameSaveDirectory(
                "android-content://automation/sample.cc",
                ccIdentity);
        File dataDirectory = new File(activity.getApplicationInfo().dataDir);
        File saveRoot = new File(dataDirectory, "saves");
        String expectedAppDirectory = Uri.fromFile(
                new File(saveRoot, appIdentity)).toString();
        String expectedCcDirectory = Uri.fromFile(
                new File(saveRoot, ccIdentity)).toString();
        String logDirectory = activity.getPrivateLogDirectory();
        String expectedLogDirectory = new File(dataDirectory, "logs").getAbsolutePath();
        byte[] initial = new byte[]{1, 2, 3, 4};
        byte[] appended = new byte[]{5, 6};
        byte[] replacement = new byte[]{9, 8};
        boolean privateLayout = expectedAppDirectory.equals(appDirectory) &&
                expectedCcDirectory.equals(ccDirectory) && !appDirectory.equals(ccDirectory);
        boolean directoryClassification =
                activity.isPrivateGameSaveDirectory(appDirectory) &&
                activity.isPrivateGameSaveDirectory(ccDirectory) &&
                !activity.isPrivateGameSaveDirectory(
                        "content://automation/tree/authorized-directory") &&
                !activity.isPrivateGameSaveDirectory(Uri.fromFile(
                        new File(dataDirectory, "authorized-directory")).toString());
        boolean logLayout = expectedLogDirectory.equals(logDirectory);
        boolean logWrite = verifyLogDirectory(logDirectory);
        boolean runtimeLogs = new File(logDirectory, "dingoopie-native.log").isFile() &&
                new File(logDirectory, "dingoopie-native.err").isFile();
        boolean releaseLogCleanup = verifyReleaseLogCleanup(activity, dataDirectory);
        boolean nestedWrite = write(activity,
                appDirectory, "profiles/sample.bin", "wb", initial);
        boolean append = write(activity,
                appDirectory, "profiles/sample.bin", "ab", appended);
        boolean appendRead = Arrays.equals(
                read(activity, appDirectory, "profiles/sample.bin"),
                new byte[]{1, 2, 3, 4, 5, 6});
        boolean overwrite = write(activity,
                appDirectory, "profiles/sample.bin", "wb", replacement) &&
                Arrays.equals(read(activity,
                        appDirectory, "profiles/sample.bin"), replacement);
        boolean ccWrite = write(activity,
                ccDirectory, "save/sample.dat", "wb", initial) &&
                Arrays.equals(read(activity, ccDirectory, "save/sample.dat"), initial);
        boolean nativeIo = DingooPieActivity.nativeRunSaveAutomation(
                appDirectory, ccDirectory);
        boolean emptyScanProtected = verifyEmptyScanProtection(activity);
        boolean traversalRejected = activity.openGameSaveFileDescriptor(
                appDirectory, "../escape.sav", "wb") < 0;
        boolean passed = privateLayout && directoryClassification && logLayout &&
                logWrite && runtimeLogs && releaseLogCleanup &&
                nestedWrite && append && appendRead && overwrite && ccWrite && nativeIo &&
                emptyScanProtected && traversalRejected;
        Log.i(TAG, "SAVE_AUTOMATION result=" + (passed ? "pass" : "fail") +
                " private_layout=" + privateLayout +
                " directory_classification=" + directoryClassification +
                " log_layout=" + logLayout +
                " log_write=" + logWrite +
                " runtime_logs=" + runtimeLogs +
                " release_log_cleanup=" + releaseLogCleanup +
                " nested_write=" + nestedWrite +
                " append=" + append +
                " append_read=" + appendRead +
                " overwrite=" + overwrite +
                " cc_write=" + ccWrite +
                " native_io=" + nativeIo +
                " empty_scan_protected=" + emptyScanProtected +
                " traversal_rejected=" + traversalRejected);
        deleteDirectory(appDirectory);
        deleteDirectory(ccDirectory);
    }

    private static boolean verifyReleaseLogCleanup(
            DingooPieActivity activity, File dataDirectory) {
        File testRoot = new File(dataDirectory, "release-log-automation");
        File filesDirectory = new File(testRoot, "files");
        File logDirectory = new File(testRoot, "logs");
        File gameDirectory = new File(new File(testRoot, "saves"), "testhash");
        deleteTree(testRoot);
        boolean prepared = filesDirectory.mkdirs() && logDirectory.mkdirs() &&
                gameDirectory.mkdirs() &&
                touch(new File(logDirectory, "stale-debug.log")) &&
                touch(new File(filesDirectory, "dingoopie-native.log")) &&
                touch(new File(filesDirectory, "dingoopie-native.err")) &&
                touch(new File(filesDirectory, "DingooPie-debug-old.log")) &&
                touch(new File(filesDirectory, "dingoopie-audio-validation.wav")) &&
                touch(new File(filesDirectory, "dingoopie-audio-validation.csv")) &&
                touch(new File(filesDirectory, "DingooPie.ini")) &&
                touch(new File(gameDirectory, "DingooPie-crash-old.log")) &&
                touch(new File(gameDirectory, "save.dat"));
        String result = prepared ? activity.preparePrivateLogDirectory(
                testRoot, filesDirectory, false) : "invalid";
        boolean cleaned = result.isEmpty() && !logDirectory.exists() &&
                !new File(filesDirectory, "dingoopie-native.log").exists() &&
                !new File(filesDirectory, "dingoopie-native.err").exists() &&
                !new File(filesDirectory, "DingooPie-debug-old.log").exists() &&
                !new File(filesDirectory, "dingoopie-audio-validation.wav").exists() &&
                !new File(filesDirectory, "dingoopie-audio-validation.csv").exists() &&
                !new File(gameDirectory, "DingooPie-crash-old.log").exists() &&
                new File(filesDirectory, "DingooPie.ini").isFile() &&
                new File(gameDirectory, "save.dat").isFile();
        deleteTree(testRoot);
        return cleaned;
    }

    private static boolean touch(File file) {
        try (FileOutputStream output = new FileOutputStream(file)) {
            output.write(1);
            return true;
        } catch (IOException exception) {
            return false;
        }
    }

    private static boolean verifyLogDirectory(String directory) {
        if (directory == null || directory.isEmpty()) {
            return false;
        }
        File probe = new File(directory, "path-automation.tmp");
        try (FileOutputStream output = new FileOutputStream(probe)) {
            output.write(new byte[]{1, 2, 3, 4});
            output.flush();
            return probe.isFile() && probe.length() == 4L;
        } catch (IOException exception) {
            Log.e(TAG, "Unable to verify private log directory", exception);
            return false;
        } finally {
            if (probe.exists() && !probe.delete()) {
                Log.w(TAG, "Unable to remove private log probe: " + probe);
            }
        }
    }

    private static boolean verifyEmptyScanProtection(DingooPieActivity activity) {
        SharedPreferences preferences = activity.getSharedPreferences(
                "game_library", Context.MODE_PRIVATE);
        Set<String> originalPaths = new HashSet<>(preferences.getStringSet(
                "paths", Collections.emptySet()));
        String directoryText = "content://automation/tree/empty-scan";
        String gamePath = "android-content://automation-empty-scan/sample.app";
        Set<String> paths = new HashSet<>(originalPaths);
        paths.add(gamePath);
        boolean prepared = preferences.edit()
                .putStringSet("paths", paths)
                .putString("save_directories:" + gamePath, directoryText)
                .commit();
        boolean automaticProtected = prepared && activity.shouldIgnoreEmptyAutomaticScan(
                Uri.parse(directoryText), Collections.emptyList(), false);
        boolean manualAuthoritative = prepared && !activity.shouldIgnoreEmptyAutomaticScan(
                Uri.parse(directoryText), Collections.emptyList(), true);
        preferences.edit()
                .putStringSet("paths", originalPaths)
                .remove("save_directories:" + gamePath)
                .commit();
        return automaticProtected && manualAuthoritative;
    }

    private static boolean write(DingooPieActivity activity,
            String directory, String fileName, String mode, byte[] data) {
        int descriptor = activity.openGameSaveFileDescriptor(directory, fileName, mode);
        if (descriptor < 0) {
            return false;
        }
        try (OutputStream output = new ParcelFileDescriptor.AutoCloseOutputStream(
                ParcelFileDescriptor.adoptFd(descriptor))) {
            output.write(data);
            output.flush();
            return true;
        } catch (IOException exception) {
            Log.e(TAG, "Save automation write failed", exception);
            return false;
        }
    }

    private static byte[] read(DingooPieActivity activity,
            String directory, String fileName) {
        int descriptor = activity.openGameSaveFileDescriptor(directory, fileName, "rb");
        if (descriptor < 0) {
            return new byte[0];
        }
        try (InputStream input = new ParcelFileDescriptor.AutoCloseInputStream(
                ParcelFileDescriptor.adoptFd(descriptor));
             java.io.ByteArrayOutputStream output = new java.io.ByteArrayOutputStream()) {
            byte[] buffer = new byte[256];
            int count;
            while ((count = input.read(buffer)) >= 0) {
                output.write(buffer, 0, count);
            }
            return output.toByteArray();
        } catch (IOException exception) {
            Log.e(TAG, "Save automation read failed", exception);
            return new byte[0];
        }
    }

    private static void deleteDirectory(String directoryText) {
        Uri uri = Uri.parse(directoryText);
        if ("file".equalsIgnoreCase(uri.getScheme())) {
            deleteTree(new File(uri.getPath()));
        }
    }

    private static void deleteTree(File file) {
        if (file == null || !file.exists()) {
            return;
        }
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        if (!file.delete()) {
            Log.w(TAG, "Unable to delete save automation file: " + file);
        }
    }
}
