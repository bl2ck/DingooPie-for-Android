package com.dingoopie.android;

import java.io.IOException;

public final class FileNameRulesTest {
    private FileNameRulesTest() {
    }

    public static void main(String[] args) throws Exception {
        require(FileNameRules.normalizeRenameTarget("game.cc", "game.cc") == null,
                "same-name rename was not ignored");
        require(FileNameRules.normalizeRenameTarget("game.cc", "  game.cc  ") == null,
                "trimmed same-name rename was not ignored");
        require(FileNameRules.normalizeRenameTarget(" game.cc ", " game.cc ") == null,
                "exact whitespace name was not ignored");
        require("renamed.cc".equals(
                        FileNameRules.normalizeRenameTarget("game.cc", " renamed.cc ")),
                "new name was not normalized");
        requireInvalid(null);
        requireInvalid("   ");
        requireInvalid("../game.cc");
        System.out.println("File name rules regression passed.");
    }

    private static void requireInvalid(String name) throws Exception {
        try {
            FileNameRules.normalizeRenameTarget("game.cc", name);
            throw new AssertionError("invalid name was accepted: " + name);
        } catch (IOException expected) {
        }
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }
}
