#ifndef DINGOO_PIE_FRONTEND_SDL_AUDIO_H
#define DINGOO_PIE_FRONTEND_SDL_AUDIO_H

#include <stdint.h>

#include "config/emulator_settings.h"
#include "guest/guest_audio.h"

uint32_t mixerOpen(waveout_args* args);
uint32_t mixerClose();
void mixerReleaseGameResources(void);
void mixerResetAfterRuntimeStop(void);
void mixerPrepareApplicationExit(void);
uint32_t mixerWriteBuffer(char* buffer, int count);
uint32_t mixerTryWriteBuffer(char* buffer, int count);
uint32_t mixerIsPlaying();
uint32_t mixerCanWriteNonBlocking();
bool mixerSkipsAudioOutput();
void mixerSetGuestVolume(uint32_t vol);
void mixerSetMuted(bool muted);
void mixerSetFrontendPaused(bool paused);
void mixerSetMasterVolumePercent(int percent);
void mixerSetBufferSamples(int samples);
void mixerSetAudioEffect(AudioEffectMode effect);
void mixerSetDigitalNoiseReduction(DigitalNoiseReductionLevel level);
void mixerRecordInput(uint32_t controlMask);
void mixerSetValidationCaptureEnabled(bool enabled);

#endif
