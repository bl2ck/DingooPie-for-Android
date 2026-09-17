#ifndef DINGOO_PIE_SHARED_GAME_GAME_RUNTIME_TYPES_H
#define DINGOO_PIE_SHARED_GAME_GAME_RUNTIME_TYPES_H

#include <stdint.h>

struct AppRuntimeRegisterSnapshot
{
    bool running;
    uint32_t gpr[32];
    float fpr[32];
    float vfpu[128];
    uint32_t vfpuCtrl[16];
    uint32_t pc;
    uint32_t hi;
    uint32_t lo;
    uint32_t fcr31;
    uint32_t fpcond;
};

#endif
