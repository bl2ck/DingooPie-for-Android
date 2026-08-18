#ifndef DINGOO_PIE_FRONTEND_MENU_MENU_OVERLAY_INTERNAL_H
#define DINGOO_PIE_FRONTEND_MENU_MENU_OVERLAY_INTERNAL_H

#include "config/settings/emulator_settings.h"
#include "frontend/menu/menu_overlay.h"
#include "shared/save/save_slots.h"

#include <SDL2/SDL.h>
#include <stdint.h>
#include <string>
#include <vector>

extern SDL_Renderer* g_renderer;
extern EmulatorSettings* g_frontendSettings;
extern std::string g_frontendCurrentGamePath;
extern std::string g_androidCheatManagerGamePath;
extern std::vector<std::string> g_androidGamePaths;
extern AndroidMenuScreen g_androidMenuScreen;
extern int g_androidMenuScrollOffset;
extern int g_androidMenuSelectedRow;
extern bool g_androidMenuSelectionHighlightVisible;
extern bool g_androidSaveStateBusy;
extern SaveStateProgress g_androidSaveStateProgress;
extern int g_androidSaveStateSelectedSlot;
extern std::string g_androidSaveStateStatus;
extern SDL_Texture* g_androidSaveStateThumbnail;
extern bool g_androidSaveStateSlotExists[kSaveStateSlotCount];
extern uint64_t g_androidSaveStateSlotModifiedTime[kSaveStateSlotCount];
extern std::string g_androidSaveStateSlotCacheGamePath;
extern bool g_controllerMappingPending;
extern uint32_t g_controllerMappingTarget;

extern const int kAndroidMenuRowTop;
extern const int kAndroidMenuRowHeight;
extern const int kAndroidMenuRowGap;
extern const char* kZhBack;
extern const char* kZhExitApp;
extern const char* kZhGameMenu;
extern const char* kZhMenu;
extern const char* kZhSwitchGame;

std::string androidAppVersionName(void);
bool androidChineseUi(void);
SDL_Rect androidMenuButtonRect(int width);
bool androidMenuScreenHasSettingsList(void);
bool androidMenuScreenUsesOverlay(AndroidMenuScreen menuScreen);
int androidSettingsMenuRowCount(void);
void androidSetScreenOrientationMode(ScreenOrientationMode mode);
ScreenOrientationMode androidScreenOrientationMode(void);
int androidUiMetric(int value);
SDL_Rect androidPanelRowRect(const SDL_Rect& panel, int row);
void beginControllerCalibration(void);
void clearAndroidSystemTextTextures(void);
void clampAndroidMenuScroll(const SDL_Rect& panel, int rowCount);
std::string controllerCalibrationStatusText(void);
uint32_t controlMask(uint32_t controlBit);
bool drawFrame(uint16_t* pixels, int displayedFps);
void navigateBackAndroidMenu(void);
void openAndroidMenu(AndroidMenuScreen screen);
void requestAndroidGame(const std::string& path,
    AndroidMenuScreen screenAfterRestart = ANDROID_MENU_NONE);
bool saveAndroidSettings(void);
bool showAndroidConfirmationDialog(const std::string& title,
    const std::string& body, const std::string& positiveButton,
    const std::string& negativeButton);
void showAndroidMessageDialog(const std::string& title, const std::string& body);
bool portraitModeEnabled(void);
void resetControllerCalibration(void);
void resetControllerMapping(void);
void drawAndroidRect(const SDL_Rect& rect, SDL_Color color);
void drawAndroidOutline(const SDL_Rect& rect, SDL_Color color);
void drawAndroidSystemTextCentered(const char* text, const SDL_Rect& rect,
    int pixelSize, SDL_Color color);
void drawAndroidSystemTextLeftCentered(const char* text, const SDL_Rect& rect,
    int leftInset, int pixelSize, SDL_Color color);
void drawAndroidSystemTextRightCentered(const char* text, const SDL_Rect& rect,
    int rightInset, int pixelSize, SDL_Color color);
SDL_Rect androidPanelRect(int width, int height);
SDL_Rect androidMenuViewportRect(const SDL_Rect& panel);
SDL_Rect androidMenuRowRect(const SDL_Rect& panel, int row);
int androidMenuMaxScroll(const SDL_Rect& panel, int rowCount);
void syncAndroidScreenOrientation(void);
int virtualCompactButtonTextSize(const SDL_Rect& rect);
bool virtualControlsVisible(void);

#endif
