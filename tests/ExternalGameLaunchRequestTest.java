package com.dingoopie.android;

public final class ExternalGameLaunchRequestTest {
    private ExternalGameLaunchRequestTest() {
    }

    public static void main(String[] args) {
        require(ExternalGameLaunchRequest.isSupportedGameName("game.app"),
                "APP extension was rejected");
        require(ExternalGameLaunchRequest.isSupportedGameName("GAME.CC"),
                "CC extension was rejected");
        require(!ExternalGameLaunchRequest.isSupportedGameName("game.zip"),
                "unsupported extension was accepted");
        require("/storage/emulated/0/Games/demo.app".equals(
                        ExternalGameLaunchRequest.normalizeStringPath(
                                "file:///storage/emulated/0/Games/demo.app")),
                "file URI was not normalized");
        String quotedPath = (char) 34 +
                "/storage/emulated/0/Games/demo game.cc" + (char) 34;
        require("/storage/emulated/0/Games/demo game.cc".equals(
                        ExternalGameLaunchRequest.normalizeStringPath(quotedPath)),
                "quoted path was not normalized");
        require("content://games/demo.app".equals(
                        ExternalGameLaunchRequest.normalizeStringPath(
                                "content://games/demo.app")),
                "content URI was changed");
        System.out.println("External game launch request regression passed.");
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }
}
