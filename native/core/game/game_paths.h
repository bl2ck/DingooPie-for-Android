#ifndef DINGOO_PIE_GAME_GAME_PATHS_H
#define DINGOO_PIE_GAME_GAME_PATHS_H

#include <string>
#include <vector>

enum GameFormat
{
    GAME_FORMAT_APP = 0,
    GAME_FORMAT_CC,
    GAME_FORMAT_UNKNOWN
};

GameFormat gameFormatFromPath(const std::string& path);
bool gamePathHasAppExtension(const std::string& path);
bool gamePathHasCcExtension(const std::string& path);
bool gamePathHasSupportedExtension(const std::string& path);
std::string gamePathNormalize(const char* gamePath);
std::string gameFileNameFromPath(const std::string& path);
std::string gameCheatFileNameFromPath(const std::string& path);
std::string gameLegacyCheatFileNameFromPath(const std::string& path);
std::vector<std::string> gameCheatFileNamesFromPath(const std::string& path);
std::string appGuestMainPathFromGamePath(const std::string& path);

#endif
