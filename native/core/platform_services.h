#ifndef DINGOO_PIE_PLATFORM_SERVICES_H
#define DINGOO_PIE_PLATFORM_SERVICES_H

#include <string>
#include <stdio.h>

bool platformFileExists(const std::string& path);
bool platformChangeToGameDirectory(const std::string& gamePath);
FILE* platformOpenGameFile(const std::string& path);
FILE* platformOpenGameSiblingFile(const std::string& gamePath, const std::string& fileName);
std::string platformAndroidConsumeCheatManagerAutomationGamePath(void);
std::string platformAndroidConsumeGameAutomationPath(void);
bool platformAndroidConsumeAudioValidationAutomationEnabled(void);
void platformAndroidRequestApplicationExit(void);
std::string platformAndroidGetSaveDirectory(const std::string& gamePath);
std::string platformAndroidGetCcSaveDirectory(const std::string& gamePath,
    const std::string& gameIdentity);
FILE* platformAndroidOpenSaveFile(const std::string& directoryUri, const std::string& fileName, const char* mode);
std::string platformWideToUtf8(const std::wstring& text);

#endif
