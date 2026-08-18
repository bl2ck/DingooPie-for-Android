#include "cc/runtime/cc_timing.h"
#include "cc/hle/cc_input_mapping.h"

#include <stdio.h>

int main()
{
    if (ccScaleElapsedMicros(10000000u, 0.65) != 6500000u ||
        ccScaleElapsedMicros(10000000u, 1.0) != 10000000u)
    {
        fprintf(stderr, "CC runtime elapsed-time scaling failed.\n");
        return 1;
    }
    if (ccTaskSchedulerElapsedMicros(10000000u, 0.65, false) != 6500000u ||
        ccTaskSchedulerElapsedMicros(10000000u, 0.65, true) != 10000000u)
    {
        fprintf(stderr, "CC audio scheduler clock isolation failed.\n");
        return 7;
    }
    if (ccHmsmToOsTicks(0u, 0u, 1u, 500u, 100u) != 150u ||
        ccMillisecondsToOsTicks(1u, 100u) != 1u)
    {
        fprintf(stderr, "CC runtime delay conversion failed.\n");
        return 2;
    }
    if (ccScaleDelayTicks(100u, 0.65) != 65u ||
        ccScaleDelayTicks(1u, 0.20) != 1u)
    {
        fprintf(stderr, "CC runtime delay scaling failed.\n");
        return 3;
    }
    if (ccCpuClockToTargetIps(200000000u, 336000000u, 15000000u) != 8928571u ||
        ccCpuClockToTargetIps(360000000u, 336000000u, 15000000u) != 16071428u ||
        ccCpuClockToTargetIps(430000000u, 336000000u, 15000000u) != 19196428u ||
        ccCpuClockToTargetIps(0u, 336000000u, 15000000u) != 0u)
    {
        fprintf(stderr, "CC CPU clock mapping regression failed.\n");
        return 4;
    }
    struct InputMapping
    {
        uint32_t source;
        uint32_t retailLayout;
        uint32_t homebrewLayout;
    };
    static const InputMapping mappings[] = {
        { CC_INPUT_SOURCE_A, 0x80000000u, 0x80000000u },
        { CC_INPUT_SOURCE_B, 0x00001000u, 0x00001000u },
        { CC_INPUT_SOURCE_X, 0x00010000u, 0x20000000u },
        { CC_INPUT_SOURCE_Y, 0x20000000u, 0x00010000u },
        { CC_INPUT_SOURCE_START, 0x00008000u, 0x00000080u },
        { CC_INPUT_SOURCE_SELECT, 0x00800000u, 0x00004000u },
        { CC_INPUT_SOURCE_L, 0x00020000u, 0x00020000u },
        { CC_INPUT_SOURCE_R, 0x40000000u, 0x40000000u },
        { CC_INPUT_SOURCE_UP, 0x00100000u, 0x00100000u },
        { CC_INPUT_SOURCE_DOWN, 0x08000000u, 0x08000000u },
        { CC_INPUT_SOURCE_LEFT, 0x10000000u, 0x10000000u },
        { CC_INPUT_SOURCE_RIGHT, 0x00040000u, 0x00040000u },
        { CC_INPUT_SOURCE_POWER, 0x00000080u, 0x00000001u },
    };
    for (size_t i = 0; i < sizeof(mappings) / sizeof(mappings[0]); ++i)
    {
        if (ccMapInputToRetailLayout(mappings[i].source) !=
                mappings[i].retailLayout ||
            ccMapInputToHomebrewLayout(mappings[i].source) !=
                mappings[i].homebrewLayout)
        {
            fprintf(stderr, "CC input mapping regression failed at index %u.\n",
                (unsigned)i);
            return 5;
        }
    }
    if (!ccUsesRetailInputMapping(kCcRetailProgramOrigin) ||
        ccUsesRetailInputMapping(kCcHomebrewProgramOrigin) ||
        ccPackageUsesRetailLayout(0x13000000u) ||
        ccPackageUsesHomebrewLayout(0x13000000u) ||
        ccMapInputToRetailLayout(CC_INPUT_SOURCE_Y | CC_INPUT_SOURCE_R) !=
            0x60000000u ||
        ccMapInputToRetailLayout(CC_INPUT_SOURCE_X) != 0x00010000u)
    {
        fprintf(stderr, "CC input layout mapping regression failed.\n");
        return 6;
    }
    printf("CC runtime timing regression passed.\n");
    return 0;
}
