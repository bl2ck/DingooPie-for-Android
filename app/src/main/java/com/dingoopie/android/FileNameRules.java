package com.dingoopie.android;

import java.io.IOException;

final class FileNameRules {
    private FileNameRules() {
    }

    static String normalizeRenameTarget(String currentName, String requestedName)
            throws IOException {
        if (requestedName != null && requestedName.equals(currentName)) {
            return null;
        }
        String name = normalize(requestedName);
        return name.equals(currentName) ? null : name;
    }

    static String normalize(String requestedName) throws IOException {
        if (requestedName == null) {
            throw new IOException("A name is required");
        }
        String name = requestedName.trim();
        if (name.isEmpty() || ".".equals(name) || "..".equals(name) ||
                name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 ||
                name.indexOf('\0') >= 0) {
            throw new IOException("Invalid file name");
        }
        return name;
    }
}
