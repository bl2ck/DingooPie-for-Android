#ifndef DINGOO_PIE_FRONTEND_SDL_AUDIO_H
#define DINGOO_PIE_FRONTEND_SDL_AUDIO_H

#include <stdint.h>

#include "config/emulator_settings.h"
#include "guest/guest_audio.h"

enum MixerRuntimeAudioProfile
{
    MIXER_RUNTIME_AUDIO_NATIVE_GUEST,
    MIXER_RUNTIME_AUDIO_CC_STABLE_HOST
};

uint32_t MixerOpen(waveout_args* args);
uint32_t MixerClose();
void MixerReleaseGameResources(void);
void MixerResetAfterRuntimeStop(void);
void MixerPrepareApplicationExit(void);
uint32_t MixerWriteBuff(char* buffer, int count);
uint32_t MixerTryWriteBuff(char* buffer, int count);
uint32_t MixerPlaying();
uint32_t MixerCanWriteNonBlocking();
bool MixerSkipsAudioOutput();
void MixerSetVolume(uint32_t vol);
void MixerSetMuted(bool muted);
void MixerSetFrontendPaused(bool paused);
void MixerSetMasterVolumePercent(int percent);
void MixerSetBufferSamples(int samples);
void MixerSetAudioEffect(AudioEffectMode effect);
void MixerSetRuntimeAudioProfile(MixerRuntimeAudioProfile profile);
void MixerSetValidationCaptureEnabled(bool enabled);

#endif
