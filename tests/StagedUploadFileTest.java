package com.dingoopie.android;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.file.Files;
import java.util.Arrays;

public final class StagedUploadFileTest {
    public static void main(String[] args) throws Exception {
        File directory = Files.createTempDirectory("dingoopie-upload-test").toFile();
        File target = new File(directory, "target.bin");
        byte[] original = new byte[]{1, 2, 3, 4};
        byte[] partial = new byte[]{9, 8};
        Files.write(target.toPath(), original);

        try (FileOutputStream output = new FileOutputStream(target, false)) {
            output.write(partial);
        }
        boolean baselineCorrupted = !Arrays.equals(Files.readAllBytes(target.toPath()), original);
        Files.write(target.toPath(), original);

        boolean rejected = false;
        try {
            StagedUploadFile.receive(directory, new ByteArrayInputStream(partial), 4).close();
        } catch (Exception exception) {
            rejected = true;
        }
        boolean fixedPreserved = Arrays.equals(Files.readAllBytes(target.toPath()), original);

        byte[] replacement = new byte[]{7, 6, 5, 4, 3};
        try (StagedUploadFile staged = StagedUploadFile.receive(
                directory, new ByteArrayInputStream(replacement), replacement.length);
             java.io.InputStream input = staged.openInput();
             FileOutputStream output = new FileOutputStream(target, false)) {
            byte[] buffer = new byte[16];
            int count;
            while ((count = input.read(buffer)) >= 0) {
                output.write(buffer, 0, count);
            }
        }
        boolean committed = Arrays.equals(Files.readAllBytes(target.toPath()), replacement);
        Files.deleteIfExists(target.toPath());
        Files.deleteIfExists(directory.toPath());
        if (!baselineCorrupted || !rejected || !fixedPreserved || !committed) {
            throw new AssertionError("Staged upload behavior mismatch");
        }
        System.out.println("staged_upload passed baseline_corrupted=1 fixed_preserved=1 " +
                "committed=1");
    }
}
