#include "game/game_history.h"

#include "config/emulator_settings.h"
#include "game/game_paths.h"

#include <stdio.h>

static bool gamePathsMatch(const std::string& first, const std::string& second)
{
    return gamePathNormalize(first.c_str()) == gamePathNormalize(second.c_str());
}

void gameHistoryClearRecentIfCurrent(
    const std::string& gamePath,
    bool enabled,
    const char* reason)
{
    if (!enabled || gamePath.empty())
    {
        return;
    }

    EmulatorSettings settings = emulatorLoadSettings();
    if (settings.lastGamePath.empty() || !gamePathsMatch(settings.lastGamePath, gamePath))
    {
        printf("game-history: recent game already changed; not clearing after %s\n", reason);
        return;
    }

    emulatorRemoveRecentGame(&settings, gamePath);
    if (emulatorSaveSettings(settings))
    {
        printf("game-history: cleared recent game after %s: %s\n", reason, gamePath.c_str());
    }
    else
    {
        printf("game-history: failed to clear recent game after %s: %s\n", reason, gamePath.c_str());
    }
}

void gameHistorySaveRecent(const std::string& gamePath, const char* reason)
{
    if (gamePath.empty())
    {
        return;
    }

    EmulatorSettings settings = emulatorLoadSettings();
    if (!emulatorRememberRecentGame(&settings, gamePath))
    {
        printf("game-history: recent game already current after %s: %s\n", reason, gamePath.c_str());
        return;
    }

    if (emulatorSaveSettings(settings))
    {
        printf("game-history: saved recent game after %s: %s\n", reason, gamePath.c_str());
    }
    else
    {
        printf("game-history: failed to save recent game after %s: %s\n", reason, gamePath.c_str());
    }
}
