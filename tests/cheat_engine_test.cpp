#include "config/cheats/cheat_engine.h"
#include "shared/game/game_paths.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>

struct TestMemory
{
    uint32_t value;
};

static bool readMemory(void* userData, uint32_t address, void* out, size_t size)
{
    if (!userData || address != 0x1000 || size != sizeof(uint32_t))
    {
        return false;
    }
    memcpy(out, &((TestMemory*)userData)->value, size);
    return true;
}

static bool writeMemory(void* userData, uint32_t address, const void* in, size_t size)
{
    if (!userData || address != 0x1000 || size != sizeof(uint32_t))
    {
        return false;
    }
    memcpy(&((TestMemory*)userData)->value, in, size);
    return true;
}

int main(int argc, char** argv)
{
    std::vector<std::string> appCandidates =
        gameCheatFileNamesFromPath("games/SameName.app");
    std::vector<std::string> ccCandidates =
        gameCheatFileNamesFromPath("games/SameName.cc");
    if (appCandidates.size() != 2 || appCandidates[0] != "SameName.app.cht" ||
        appCandidates[1] != "SameName.cht" || ccCandidates.size() != 2 ||
        ccCandidates[0] != "SameName.cc.cht" || ccCandidates[1] != "SameName.cht" ||
        gameCheatFileNameFromPath("games/Upper.APP") != "Upper.app.cht" ||
        gameCheatFileNameFromPath("games/Upper.CC") != "Upper.cc.cht")
    {
        fprintf(stderr, "format-aware cheat filename regression failed\n");
        return 1;
    }

    if (argc != 3)
    {
        fprintf(stderr, "usage: cheat_engine_test <file.cht> <app-sha256>\n");
        return 2;
    }

    CheatSet fileSet;
    std::string error;
    if (!cheatLoadFile(argv[1], &fileSet, &error))
    {
        fprintf(stderr, "load_failed=%s\n", error.c_str());
        return 3;
    }
    bool shaMatch = cheatSetMatchesApp(fileSet, argv[2]);

    CheatSet synthetic;
    const std::string text =
        "on|Write Test|u32|0x1000|0x12345678|0x11111111\n";
    if (!cheatParseText(text, "synthetic.cht", &synthetic, &error))
    {
        fprintf(stderr, "parse_failed=%s\n", error.c_str());
        return 4;
    }
    TestMemory memory = { 0x11111111u };
    CheatApplyStats stats = cheatApply(&synthetic, readMemory, writeMemory,
        &memory, CHEAT_APPLY_FRAME);

    printf("entries=%u parse_errors=%u sha_match=%u attempted=%u applied=%u value=0x%08X\n",
        (unsigned int)fileSet.entries.size(), fileSet.parseErrors,
        shaMatch ? 1u : 0u, stats.attempted, stats.applied, memory.value);
    return !fileSet.entries.empty() && fileSet.parseErrors == 0 && shaMatch &&
        stats.attempted == 1 && stats.applied == 1 &&
        memory.value == 0x12345678u ? 0 : 5;
}
