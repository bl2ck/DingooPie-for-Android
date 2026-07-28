package com.dingoopie.android;

import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Arrays;

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
        String appDirectory = activity.getGameSaveDirectory(
                "android-content://automation/sample.app");
        String ccDirectory = activity.getCcGameSaveDirectory(
                "android-content://automation/sample.cc",
                "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
        byte[] initial = new byte[]{1, 2, 3, 4};
        byte[] appended = new byte[]{5, 6};
        byte[] replacement = new byte[]{9, 8};
        boolean isolated = appDirectory.contains("/app-saves/") &&
                ccDirectory.contains("/cc-saves/") && !appDirectory.equals(ccDirectory);
        boolean nestedWrite = write(activity,
                appDirectory, "profiles/slot1.sav", "wb", initial);
        boolean append = write(activity,
                appDirectory, "profiles/slot1.sav", "ab", appended);
        boolean appendRead = Arrays.equals(
                read(activity, appDirectory, "profiles/slot1.sav"),
                new byte[]{1, 2, 3, 4, 5, 6});
        boolean overwrite = write(activity,
                appDirectory, "profiles/slot1.sav", "wb", replacement) &&
                Arrays.equals(read(activity,
                        appDirectory, "profiles/slot1.sav"), replacement);
        boolean ccWrite = write(activity,
                ccDirectory, "save/slot1.dat", "wb", initial) &&
                Arrays.equals(read(activity, ccDirectory, "save/slot1.dat"), initial);
        boolean nativeIo = DingooPieActivity.nativeRunSaveAutomation(
                appDirectory, ccDirectory);
        boolean traversalRejected = activity.openGameSaveFileDescriptor(
                appDirectory, "../escape.sav", "wb") < 0;
        boolean passed = isolated && nestedWrite && append && appendRead && overwrite &&
                ccWrite && nativeIo && traversalRejected;
        Log.i(TAG, "SAVE_AUTOMATION result=" + (passed ? "pass" : "fail") +
                " isolated=" + isolated +
                " nested_write=" + nestedWrite +
                " append=" + append +
                " append_read=" + appendRead +
                " overwrite=" + overwrite +
                " cc_write=" + ccWrite +
                " native_io=" + nativeIo +
                " traversal_rejected=" + traversalRejected);
        deleteDirectory(appDirectory);
        deleteDirectory(ccDirectory);
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
