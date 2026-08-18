#ifndef DINGOO_PIE_FRONTEND_VIDEO_FRAMEBUFFER_H
#define DINGOO_PIE_FRONTEND_VIDEO_FRAMEBUFFER_H

#include <stddef.h>
#include <stdint.h>
#include "shared/config/runtime_constants.h"
#include "shared/services/guest_package.h"

// 320x240 RGB565 framebuffer, rounded up to a 4 KB page boundary.
#define VM_LCD_FB_SIZE  0x00026000

void framebufferReset(void);
size_t framebufferGuestAliasCount(void);
uint32_t framebufferGuestAlias(size_t index);

uint32_t framebufferGuestAddress(void);
bool framebufferHostPointer(uint32_t addr, void** out);
bool framebufferVmPointer(void* ptr, uint32_t* out);

void* framebufferPixels(void);
void* framebufferPresentedPixels(void);
void framebufferCopyPresented(void* dst, uint32_t size);

void framebufferRequestUpdate(void);
void framebufferSetTransientPartialProtectionEnabled(bool enabled);
void framebufferPresentRestoredFrame(void);
int framebufferConsumeUpdateRequest(void);
uint64_t consumeFramebufferSubmittedCount(void);
uint64_t consumeFramebufferCopyMicros(void);
void consumeFramebufferTimingStats(uint64_t* totalIntervalMicros, uint64_t* maxIntervalMicros,
    uint64_t* over25msCount, uint64_t* over33msCount);
void trackFramebufferWrite(uint32_t address, uint32_t size);
bool framebufferAddressOverlaps(uint32_t address, uint32_t size);
uint64_t consumeFramebufferWriteCount(void);
uint64_t consumeFramebufferWriteBytes(void);
void framebufferSetProfileEnabled(bool enabled);

#endif
