#ifndef DINGOO_PIE_FRONTEND_MENU_MENU_OVERLAY_H
#define DINGOO_PIE_FRONTEND_MENU_MENU_OVERLAY_H

#include "frontend/menu/menu_model.h"
#include "frontend/menu/menu_strings.h"

#include <SDL2/SDL.h>
#include <stddef.h>
#include <string>

std::string androidMenuString(AndroidMenuTextId id);
SDL_Rect androidSaveStateSlotRect(const SDL_Rect& panel, int slot);
SDL_Rect androidSaveStateActionRect(const SDL_Rect& panel, int index);
void deleteAndroidSaveState(void);
void drawAndroidMenuOverlay(void);
void handleAndroidMainMenuSelection(int row);
void handleAndroidAboutMenuSelection(int row);
void handleAndroidOptionsSelection(int row);
void handleAndroidDetailMenuSelection(AndroidMenuScreen screen, int row);
void invalidateAndroidSaveStateThumbnail(void);
void performAndroidSaveStateAction(bool saving);
void prepareAndroidCheatManagerGamePath(void);
void refreshAndroidSaveStateSlotInfo(int slot);
void refreshAndroidSaveStateSlots(void);
void refreshAndroidSaveStateThumbnail(void);
void requestAndroidSwitchGame(void);
void requestAndroidRestartGame(void);
void requestAndroidExitApplication(void);
bool selectNextAndroidCheatManagerGamePath(void);
bool setAndroidCheatManagerGlobalEnabled(bool enabled);
bool setAndroidCheatManagerFeatureEnabled(size_t index, bool enabled);
bool setAllAndroidCheatManagerFeaturesEnabled(bool enabled);

#endif
