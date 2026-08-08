package com.dingoopie.android;

import java.io.BufferedOutputStream;
import java.io.Closeable;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

final class StagedUploadFile implements Closeable {
    private final File file;

    private StagedUploadFile(File file) {
        this.file = file;
    }

    static StagedUploadFile receive(File directory, InputStream input, long length)
            throws IOException {
        File file = File.createTempFile("dingoopie-upload-", ".tmp", directory);
        boolean complete = false;
        try (BufferedOutputStream output = new BufferedOutputStream(
                new FileOutputStream(file))) {
            byte[] buffer = new byte[64 * 1024];
            long remaining = length;
            while (remaining > 0) {
                int read = input.read(buffer, 0,
                        (int)Math.min((long)buffer.length, remaining));
                if (read < 0) {
                    throw new IOException("Upload ended before Content-Length");
                }
                if (read == 0) {
                    int single = input.read();
                    if (single < 0) {
                        throw new IOException("Upload ended before Content-Length");
                    }
                    output.write(single);
                    remaining--;
                    continue;
                }
                output.write(buffer, 0, read);
                remaining -= read;
            }
            complete = true;
            return new StagedUploadFile(file);
        } finally {
            if (!complete && !file.delete() && file.exists()) {
                file.deleteOnExit();
            }
        }
    }

    InputStream openInput() throws IOException {
        return new FileInputStream(file);
    }

    @Override
    public void close() {
        if (!file.delete() && file.exists()) {
            file.deleteOnExit();
        }
    }
}
