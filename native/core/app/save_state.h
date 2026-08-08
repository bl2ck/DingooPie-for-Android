#ifndef DINGOO_PIE_SAVE_STATE_H
#define DINGOO_PIE_SAVE_STATE_H

#include "emulator_core.h"

#include <stddef.h>
#include <string>
#include <vector>

struct CcRuntimeState;

static const int kSaveStateSlotCount = 15;

enum SaveStateGameFormat
{
    SAVE_STATE_FORMAT_APP,
    SAVE_STATE_FORMAT_CC
};

enum SaveStateProgressPhase
{
    SAVE_STATE_PROGRESS_COMPRESS,
    SAVE_STATE_PROGRESS_DECOMPRESS
};

struct SaveStateProgress
{
    SaveStateProgressPhase phase;
    uint32_t percent;
};

typedef void (*SaveStateProgressCallback)(const SaveStateProgress& progress, void* userData);

bool saveStateCompressPayload(const std::vector<uint8_t>& bytes,
    std::vector<uint8_t>* out, SaveStateProgressCallback progressCallback = 0,
    void* progressUserData = 0);
bool saveStateDecompressPayload(const std::vector<uint8_t>& bytes,
    size_t offset, size_t size, size_t expectedSize, std::vector<uint8_t>* out,
    SaveStateProgressCallback progressCallback = 0, void* progressUserData = 0);

struct SaveStateSlotInfo
{
    bool exists;
    std::string path;
    uint64_t modifiedTime;
    bool runtimeCountValid;
    // Includes the main runtime plus all captured task runtimes.
    uint32_t runtimeCount;
};

std::string saveStateAppIdForPath(const std::string& appPath);
SaveStateGameFormat saveStateFormatForPath(const std::string& appPath);
std::string saveStatePathForSlot(const std::string& appPath,
    SaveStateGameFormat format, int slot);
std::string saveStateThumbnailPathForSlot(const std::string& appPath,
    SaveStateGameFormat format, int slot);
bool saveStateSlotExists(const std::string& appPath,
    SaveStateGameFormat format, int slot);
SaveStateSlotInfo saveStateSlotInfo(const std::string& appPath,
    SaveStateGameFormat format, int slot);
bool saveStateWriteSlot(const std::string& appPath, SaveStateGameFormat format, int slot,
    const EmulatorRuntimeState& state, std::string* error,
    SaveStateProgressCallback progressCallback = 0, void* progressUserData = 0);
bool saveStateReadSlot(const std::string& appPath, SaveStateGameFormat format, int slot,
    EmulatorRuntimeState* state, std::string* error,
    SaveStateProgressCallback progressCallback = 0, void* progressUserData = 0);
bool saveStateWriteCcSlot(const std::string& appPath, int slot,
    const CcRuntimeState& state, std::string* error,
    SaveStateProgressCallback progressCallback = 0, void* progressUserData = 0);
bool saveStateReadCcSlot(const std::string& appPath, int slot,
    CcRuntimeState* state, std::string* error,
    SaveStateProgressCallback progressCallback = 0, void* progressUserData = 0);
bool saveStateWriteThumbnailRgb565(const std::string& appPath,
    SaveStateGameFormat format, int slot, const uint16_t* pixels,
    uint32_t width, uint32_t height);
bool saveStateReadThumbnail(const std::string& appPath,
    SaveStateGameFormat format, int slot, std::vector<uint8_t>* out);
bool saveStateDeleteSlot(const std::string& appPath,
    SaveStateGameFormat format, int slot);
bool saveStateRunRegressionTests(void);

#endif
