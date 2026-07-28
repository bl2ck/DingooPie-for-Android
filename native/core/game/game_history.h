#ifndef DINGOO_PIE_GAME_GAME_HISTORY_H
#define DINGOO_PIE_GAME_GAME_HISTORY_H

#include <string>

void gameHistoryClearRecentIfCurrent(
    const std::string& gamePath,
    bool enabled,
    const char* reason);
void gameHistorySaveRecent(const std::string& gamePath, const char* reason);

#endif
