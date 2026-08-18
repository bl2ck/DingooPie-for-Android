package com.dingoopie.android;

import java.net.URI;
import java.util.Locale;

final class ExternalGameLaunchRequest {
    static final String[] URI_QUERY_KEYS = { "path", "gamePath", "rom" };
    static final String[] PATH_EXTRA_KEYS = {
            "gamePath", "path", "rom", "ROM", "game", "GAME",
            "file", "FILE", "filename", "fullPath"
    };

    private ExternalGameLaunchRequest() {
    }

    static boolean isSupportedGameName(String name) {
        if (name == null) {
            return false;
        }
        String lowerName = name.toLowerCase(Locale.ROOT);
        return lowerName.endsWith(".app") || lowerName.endsWith(".cc");
    }

    static String normalizeStringPath(String candidate) {
        if (candidate == null) {
            return "";
        }
        String path = candidate.trim();
        if (path.length() >= 2) {
            char first = path.charAt(0);
            char last = path.charAt(path.length() - 1);
            if ((first == 34 && last == 34) || (first == 39 && last == 39)) {
                path = path.substring(1, path.length() - 1).trim();
            }
        }
        if (path.regionMatches(true, 0, "file:", 0, 5)) {
            try {
                URI uri = URI.create(path);
                String filePath = uri.getPath();
                return filePath == null ? "" : filePath;
            } catch (IllegalArgumentException ignored) {
                return "";
            }
        }
        return path;
    }
}
