#include "config/cheat_runtime.h"

#include "game/game_paths.h"
#include "config/cheat_engine.h"
#include "platform_services.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>


static std::mutex g_cheatMutex;
static CheatSet g_cheatSet;
static bool g_cheatLoaded = false;
static bool g_cheatEnabled = false;
static bool g_cheatRequestedEnabled = false;
static bool g_cheatShaMismatch = false;
struct CheatRuntimeBinding
{
    void* userData;
    CheatReadCallback readCallback;
    CheatWriteCallback writeCallback;
    CheatFlushCallback flushCallback;
};
static CheatRuntimeBinding g_cheatBinding = {};
static uint32_t g_lastFrameApplyCount = 0;
static uint32_t g_cheatRevision = 0;
static std::string g_currentGameSha256;
static bool g_manualApplyPending = false;

static bool runtimeReadCallback(void* userData, uint32_t address, void* out, size_t size)
{
    return nativeRuntimeReadRaw((NativeRuntime*)userData, address, out, size);
}

static bool runtimeWriteCallback(void* userData, uint32_t address, const void* in, size_t size)
{
    return nativeRuntimeWriteRaw((NativeRuntime*)userData, address, in, size);
}

static void runtimeFlushCallback(void* userData)
{
    nativeRuntimeFlushCodeCache((NativeRuntime*)userData);
}

static bool envEnabled(const char* name)
{
    const char* value = getenv(name);
    return value && value[0] && strcmp(value, "0") != 0;
}

static std::string normalizeSha(const char* appSha256)
{
    std::string value = appSha256 ? appSha256 : "";
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] >= 'a' && value[i] <= 'f')
        {
            value[i] = (char)(value[i] - 'a' + 'A');
        }
    }
    return value;
}

static std::string trimAscii(const std::string& text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t'))
    {
        begin++;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t'))
    {
        end--;
    }
    return text.substr(begin, end - begin);
}

struct CheatFeatureName
{
    std::string key;
    std::string chinese;
    std::string english;
};

static size_t findFeatureSeparator(const std::string& entryName)
{
    size_t asciiColon = entryName.find(':');
    size_t fullColon = entryName.find(u8"\uff1a");
    if (asciiColon != std::string::npos && fullColon != std::string::npos)
    {
        return asciiColon < fullColon ? asciiColon : fullColon;
    }
    if (asciiColon != std::string::npos)
    {
        return asciiColon;
    }
    return fullColon;
}

static CheatFeatureName parseFeatureName(const std::string& entryName)
{
    // A .cht line maps to one low-level patch; the UI groups lines that share
    // the same prefix so players see feature names, not implementation details.
    size_t sep = findFeatureSeparator(entryName);
    std::string key = sep == std::string::npos ?
        trimAscii(entryName) : trimAscii(entryName.substr(0, sep));
    if (key.empty())
    {
        key = trimAscii(entryName);
    }

    CheatFeatureName name;
    name.key = key;
    name.chinese = key;
    name.english = key;

    size_t slash = key.find('/');
    if (slash != std::string::npos)
    {
        std::string first = trimAscii(key.substr(0, slash));
        std::string second = trimAscii(key.substr(slash + 1));
        if (!first.empty())
        {
            name.chinese = first;
        }
        if (!second.empty())
        {
            name.english = second;
        }
    }

    return name;
}

struct CheatFeatureGroup
{
    std::string name;
    std::string nameChinese;
    std::string nameEnglish;
    std::vector<size_t> entryIndices;
};

static std::vector<CheatFeatureGroup> buildFeatureGroupsLocked(void)
{
    std::vector<CheatFeatureGroup> groups;
    std::unordered_map<std::string, size_t> groupIndexByName;
    for (size_t i = 0; i < g_cheatSet.entries.size(); ++i)
    {
        CheatFeatureName name = parseFeatureName(g_cheatSet.entries[i].name);
        std::unordered_map<std::string, size_t>::iterator found = groupIndexByName.find(name.key);
        size_t groupIndex = 0;
        if (found == groupIndexByName.end())
        {
            groupIndex = groups.size();
            CheatFeatureGroup group;
            group.name = name.key;
            group.nameChinese = name.chinese;
            group.nameEnglish = name.english;
            groups.push_back(group);
            groupIndexByName[name.key] = groupIndex;
        }
        else
        {
            groupIndex = found->second;
        }
        groups[groupIndex].entryIndices.push_back(i);
    }
    return groups;
}

static bool groupEnabledLocked(const CheatFeatureGroup& group)
{
    if (group.entryIndices.empty())
    {
        return false;
    }
    for (size_t i = 0; i < group.entryIndices.size(); ++i)
    {
        size_t entryIndex = group.entryIndices[i];
        if (entryIndex >= g_cheatSet.entries.size() || !g_cheatSet.entries[entryIndex].enabled)
        {
            return false;
        }
    }
    return true;
}

static bool setFeatureGroupEnabledLocked(const CheatFeatureGroup& group, bool enabled)
{
    bool changed = false;
    for (size_t i = 0; i < group.entryIndices.size(); ++i)
    {
        size_t entryIndex = group.entryIndices[i];
        if (entryIndex >= g_cheatSet.entries.size())
        {
            continue;
        }
        CheatEntry& entry = g_cheatSet.entries[entryIndex];
        if (entry.enabled != enabled)
        {
            entry.enabled = enabled;
            changed = true;
        }
    }
    if (changed)
    {
        g_cheatRevision++;
    }
    return true;
}

static bool setEnabledFeatureKeysLocked(const std::vector<std::string>& featureKeys)
{
    // Parsed .cht status is treated as metadata only; user selections from the
    // INI decide which feature groups are active for this app launch.
    std::unordered_set<std::string> enabledKeys;
    for (size_t i = 0; i < featureKeys.size(); ++i)
    {
        if (!featureKeys[i].empty())
        {
            enabledKeys.insert(featureKeys[i]);
        }
    }

    bool changed = false;
    std::vector<CheatFeatureGroup> groups = buildFeatureGroupsLocked();
    for (size_t i = 0; i < groups.size(); ++i)
    {
        bool enabled = enabledKeys.find(groups[i].name) != enabledKeys.end();
        bool wasEnabled = groupEnabledLocked(groups[i]);
        setFeatureGroupEnabledLocked(groups[i], enabled);
        changed = changed || wasEnabled != enabled;
    }
    return changed;
}

static void resetEntryEnabledStateLocked(void)
{
    // Keep every feature unchecked by default, even if the .cht line says "on".
    // The menu persists explicit user choices separately.
    for (size_t i = 0; i < g_cheatSet.entries.size(); ++i)
    {
        g_cheatSet.entries[i].enabled = false;
        g_cheatSet.entries[i].appliedOnce = false;
    }
}

enum CheatCandidateLoadResult
{
    CHEAT_CANDIDATE_NOT_FOUND = 0,
    CHEAT_CANDIDATE_LOADED,
    CHEAT_CANDIDATE_INVALID
};

static CheatCandidateLoadResult loadCandidate(
    const std::string& gamePath, const std::string& cheatName,
    CheatSet* out)
{
    FILE* file = platformOpenGameSiblingFile(gamePath, cheatName);
    if (!file)
    {
        return CHEAT_CANDIDATE_NOT_FOUND;
    }

    std::string error;
    CheatSet loaded;
    bool loadedFile = cheatLoadStream(file, cheatName, &loaded, &error);
    fclose(file);
    if (!loadedFile)
    {
        printf("cheat: failed to load %s beside %s: %s\n",
            cheatName.c_str(), gamePath.c_str(), error.c_str());
        return CHEAT_CANDIDATE_INVALID;
    }

    *out = loaded;
    return CHEAT_CANDIDATE_LOADED;
}

static void logApplyStats(const char* reason, const CheatApplyStats& stats)
{
    if (stats.attempted == 0 && stats.skippedDisabled == 0)
    {
        return;
    }
    if (envEnabled("DINGOO_PIE_CHEAT_TRACE"))
    {
        printf("cheat: apply %s attempted=%u applied=%u disabled=%u once=%u compare=%u read_fail=%u write_fail=%u\n",
            reason ? reason : "runtime",
            stats.attempted,
            stats.applied,
            stats.skippedDisabled,
            stats.skippedOnce,
            stats.skippedCompare,
            stats.readFailures,
            stats.writeFailures);
    }
}

static bool cheatAvailableLocked(void)
{
    return g_cheatLoaded && !g_cheatShaMismatch && !g_cheatSet.entries.empty();
}

static bool cheatCanApplyLocked(void)
{
    return g_cheatBinding.userData && g_cheatBinding.readCallback &&
        g_cheatBinding.writeCallback && g_cheatEnabled && cheatAvailableLocked();
}

static void clearManualApplyLocked(void)
{
    g_manualApplyPending = false;
}

static void requestManualApplyLocked(void)
{
    g_manualApplyPending = true;
}

static bool consumeManualApplyLocked(void)
{
    bool pending = g_manualApplyPending;
    g_manualApplyPending = false;
    return pending;
}

static void refreshEffectiveEnabledLocked(void)
{
    g_cheatEnabled = g_cheatRequestedEnabled && cheatAvailableLocked();
}

static void finishApplyLocked(const CheatApplyStats& stats)
{
    if (stats.appliedOnce > 0)
    {
        g_cheatRevision++;
    }
    if (stats.applied > 0)
    {
        if (g_cheatBinding.flushCallback)
        {
            g_cheatBinding.flushCallback(g_cheatBinding.userData);
        }
    }
}

static CheatApplyStats applyLocked(CheatApplyPhase phase, const char* reason)
{
    CheatApplyStats stats = {};
    if (!cheatCanApplyLocked())
    {
        return stats;
    }

    stats = cheatApply(&g_cheatSet, g_cheatBinding.readCallback,
        g_cheatBinding.writeCallback, g_cheatBinding.userData, phase);
    logApplyStats(reason, stats);
    finishApplyLocked(stats);
    return stats;
}

void cheatRuntimeSetEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    bool oldEnabled = g_cheatEnabled;
    g_cheatRequestedEnabled = enabled;
    refreshEffectiveEnabledLocked();
    if (g_cheatEnabled != oldEnabled)
    {
        g_cheatRevision++;
    }
}

bool cheatRuntimeEnabled(void)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    return g_cheatEnabled;
}

uint32_t cheatRuntimeRevision(void)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    return g_cheatRevision;
}

static void loadCheatRuntimeForGame(
    const char* appSha256,
    const char* gamePath,
    const std::vector<std::string>& enabledFeatureKeys,
    bool verifyAppSha)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    cheatClearSet(&g_cheatSet);
    g_cheatLoaded = false;
    g_cheatShaMismatch = false;
    g_lastFrameApplyCount = 0;
    clearManualApplyLocked();
    g_currentGameSha256 = normalizeSha(appSha256);
    g_cheatRevision++;

    CheatSet loaded;

    if (gamePath && gamePath[0])
    {
        std::vector<std::string> cheatNames = gameCheatFileNamesFromPath(gamePath);
        for (size_t i = 0; i < cheatNames.size(); ++i)
        {
            CheatCandidateLoadResult result = loadCandidate(
                gamePath, cheatNames[i], &loaded);
            if (result == CHEAT_CANDIDATE_NOT_FOUND)
            {
                continue;
            }
            if (result == CHEAT_CANDIDATE_LOADED)
            {
                g_cheatSet = loaded;
                resetEntryEnabledStateLocked();
                g_cheatLoaded = true;
            }
            break;
        }
    }

    if (g_cheatLoaded)
    {
        g_cheatShaMismatch = verifyAppSha &&
            !cheatSetMatchesApp(g_cheatSet, appSha256);
        if (!g_cheatShaMismatch)
        {
            setEnabledFeatureKeysLocked(enabledFeatureKeys);
        }
    }
    if (!cheatAvailableLocked())
    {
        // A persisted global request must not keep cheats armed for a game
        // that has no matching, valid code file.
        g_cheatRequestedEnabled = false;
    }
    refreshEffectiveEnabledLocked();

    if (g_cheatLoaded)
    {
        printf("cheat: loaded %u code(s), parse_errors=%u, enabled=%u, sha_mismatch=%u, source=%s\n",
            (unsigned int)g_cheatSet.entries.size(),
            g_cheatSet.parseErrors,
            g_cheatEnabled ? 1u : 0u,
            g_cheatShaMismatch ? 1u : 0u,
            g_cheatSet.sourcePath.c_str());
        if (g_cheatShaMismatch)
        {
            printf("cheat: app_sha256 mismatch, cheats disabled for this game: cheat_sha256=%s current_sha256=%s\n",
                g_cheatSet.appSha256.empty() ? "(none)" : g_cheatSet.appSha256.c_str(),
                g_currentGameSha256.empty() ? "(none)" : g_currentGameSha256.c_str());
        }
    }
    else if (envEnabled("DINGOO_PIE_CHEAT_TRACE"))
    {
        printf("cheat: no matching same-directory cheat file for game=%s\n",
            gamePath && gamePath[0] ? gamePath : "(none)");
    }
}

void cheatRuntimeLoadForGame(
    const char* appSha256,
    const char* gamePath,
    const std::vector<std::string>& enabledFeatureKeys)
{
    loadCheatRuntimeForGame(appSha256, gamePath, enabledFeatureKeys, true);
}

void cheatRuntimeLoadForConfiguration(
    const char* gamePath,
    const std::vector<std::string>& enabledFeatureKeys)
{
    loadCheatRuntimeForGame(NULL, gamePath, enabledFeatureKeys, false);
}

CheatRuntimeStatus cheatRuntimeGetStatus(void)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    CheatRuntimeStatus status;
    status.enabled = g_cheatEnabled;
    status.available = cheatAvailableLocked();
    status.loaded = g_cheatLoaded;
    status.shaMismatch = g_cheatShaMismatch;
    status.revision = g_cheatRevision;
    status.sourcePath = g_cheatSet.sourcePath;
    status.appSha256 = g_cheatSet.appSha256;
    status.currentGameSha256 = g_currentGameSha256;
    std::vector<CheatFeatureGroup> groups = buildFeatureGroupsLocked();
    status.entries.reserve(groups.size());
    for (size_t i = 0; i < groups.size(); ++i)
    {
        CheatRuntimeEntryView view;
        view.enabled = groupEnabledLocked(groups[i]);
        view.name = groups[i].name;
        view.nameChinese = groups[i].nameChinese;
        view.nameEnglish = groups[i].nameEnglish;
        status.entries.push_back(view);
    }
    return status;
}

bool cheatRuntimeSetEntryEnabled(size_t index, bool enabled)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    if (!cheatAvailableLocked())
    {
        return false;
    }
    std::vector<CheatFeatureGroup> groups = buildFeatureGroupsLocked();
    if (index >= groups.size())
    {
        return false;
    }

    return setFeatureGroupEnabledLocked(groups[index], enabled);
}

void cheatRuntimeBind(NativeRuntime* runtime)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    g_cheatBinding.userData = runtime;
    g_cheatBinding.readCallback = runtimeReadCallback;
    g_cheatBinding.writeCallback = runtimeWriteCallback;
    g_cheatBinding.flushCallback = runtimeFlushCallback;
}

void cheatRuntimeUnbind(NativeRuntime* runtime)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    if (!runtime || g_cheatBinding.userData == runtime)
    {
        g_cheatBinding = {};
    }
}

void cheatRuntimeBindMemory(void* userData, CheatReadCallback readCallback,
    CheatWriteCallback writeCallback, CheatFlushCallback flushCallback)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    g_cheatBinding.userData = userData;
    g_cheatBinding.readCallback = readCallback;
    g_cheatBinding.writeCallback = writeCallback;
    g_cheatBinding.flushCallback = flushCallback;
}

void cheatRuntimeUnbindMemory(void* userData)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    if (!userData || g_cheatBinding.userData == userData)
    {
        g_cheatBinding = {};
    }
}

void cheatRuntimeApplyStartup(NativeRuntime* runtime)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    if (g_cheatBinding.userData == runtime)
    {
        applyLocked(CHEAT_APPLY_STARTUP, "startup");
    }
}

uint32_t cheatRuntimeApplyStartupBound(void)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    return applyLocked(CHEAT_APPLY_STARTUP, "startup").applied;
}

void cheatRuntimeApplyNow(void)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    if (!cheatCanApplyLocked())
    {
        return;
    }

    // Menu commands run on the frontend thread while the IR JIT may still own
    // its block cache. Defer the actual memory writes to the runtime frame
    // boundary, which is where periodic cheat application already runs.
    requestManualApplyLocked();
}

void cheatRuntimeApplyFrame(void)
{
    std::lock_guard<std::mutex> lock(g_cheatMutex);
    if (!cheatCanApplyLocked())
    {
        return;
    }

    bool manualApply = consumeManualApplyLocked();
    CheatApplyStats stats = cheatApply(&g_cheatSet, g_cheatBinding.readCallback,
        g_cheatBinding.writeCallback, g_cheatBinding.userData, CHEAT_APPLY_FRAME);
    g_lastFrameApplyCount += stats.applied;
    finishApplyLocked(stats);
    if (manualApply)
    {
        logApplyStats("menu", stats);
    }
    else if (envEnabled("DINGOO_PIE_CHEAT_TRACE") && g_lastFrameApplyCount >= 60)
    {
        logApplyStats("frame", stats);
        g_lastFrameApplyCount = 0;
    }
}
