#include "frontend/sdl_audio.h"
#include "frontend/audio_validation_capture.h"

#include <SDL2/SDL.h>
#include <stdint.h>
#include <deque>
#include <vector>
#include <stdlib.h>
#include <string.h>
#include <utility>

static const uint32_t kQueueBackpressureLogIntervalMs = 1000;
static const uint32_t kAudioQueueDropDisabledMs = 0;
static const uint32_t kAudioQueueDropMaxMs = 60000;
static const uint32_t kPendingAudioMaxBytes = 512 * 1024;
static const int kAudioEffectStateChannels = 8;
static const int kCcStableHostSampleRate = 48000;
static const Uint8 kCcStableHostChannels = 2;

static SDL_AudioDeviceID g_audioDevice = 0;
static SDL_AudioSpec g_audioSpec;
static SDL_AudioSpec g_guestAudioSpec;
static SDL_AudioStream* g_audioStream = NULL;
static SDL_mutex* g_audioMutex = NULL;
static uint32_t g_volume = 100;
static int g_masterVolumePercent = 100;
static int g_bufferSamples = 2048;
static AudioEffectMode g_audioEffect = AUDIO_EFFECT_OFF;
static int32_t g_audioEffectState[kAudioEffectStateChannels] = {};
static bool g_audioEffectStateValid[kAudioEffectStateChannels] = {};
static bool g_guestMuteRequested = false;
static bool g_frontendPauseRequested = false;
static bool g_audioOutputUnavailable = false;
static bool g_gameAudioResourcesActive = false;
static MixerRuntimeAudioProfile g_runtimeAudioProfile = MIXER_RUNTIME_AUDIO_NATIVE_GUEST;
static uint64_t g_lastQueueBackpressureLogTicks = 0;
static std::deque<std::vector<char> > g_pendingAudio;
static uint32_t g_pendingAudioBytes = 0;

enum AudioQueueWaitResult
{
    AUDIO_QUEUE_READY,
    AUDIO_QUEUE_OUTPUT_STOPPED,
    AUDIO_QUEUE_DROP_BUFFER
};

static uint32_t parseBoundedUintEnv(const char* name, uint32_t defaultValue, uint32_t maxValue)
{
    const char* value = getenv(name);
    if (!value || !value[0])
    {
        return defaultValue;
    }

    char* end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value)
    {
        return defaultValue;
    }
    if (parsed > (unsigned long)maxValue)
    {
        parsed = (unsigned long)maxValue;
    }
    return (uint32_t)parsed;
}

static uint32_t audioQueueDropAfterMs(void)
{
    static int initialized = 0;
    static uint32_t dropAfterMs = kAudioQueueDropDisabledMs;
    if (!initialized)
    {
        // Dropping saturated PCM buffers shortens the guest audio timeline.
        // Keep lossless backpressure by default; set the env var to a timeout
        // only when a sample needs bounded audio latency more than exact pacing.
        dropAfterMs = parseBoundedUintEnv(
            "DINGOO_PIE_AUDIO_QUEUE_DROP_MS",
            kAudioQueueDropDisabledMs,
            kAudioQueueDropMaxMs);
        initialized = 1;
    }
    return dropAfterMs;
}

static bool audioQueueTraceEnabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = getenv("DINGOO_PIE_AUDIO_QUEUE_TRACE");
        enabled = value && value[0] && value[0] != '0' ? 1 : 0;
    }
    return enabled != 0;
}

static void resetAudioBackpressureLog(void)
{
    g_lastQueueBackpressureLogTicks = 0;
}

static SDL_mutex* audioMutex(void)
{
    if (!g_audioMutex)
    {
        g_audioMutex = SDL_CreateMutex();
    }
    return g_audioMutex;
}

static void lockAudio(void)
{
    SDL_mutex* mutex = audioMutex();
    if (mutex)
    {
        SDL_LockMutex(mutex);
    }
}

static void unlockAudio(void)
{
    SDL_mutex* mutex = audioMutex();
    if (mutex)
    {
        SDL_UnlockMutex(mutex);
    }
}

static Uint16 convertFormat(uint16_t format)
{
    switch (format)
    {
    case AFMT_U8:
        return AUDIO_U8;
    case AFMT_S16_LE:
        return AUDIO_S16LSB;
    default:
        return AUDIO_S16LSB;
    }
}

static uint32_t audioBytesPerSample(Uint16 format)
{
    switch (format & 0xff)
    {
    case 8:
        return 1;
    case 16:
        return 2;
    case 32:
        return 4;
    default:
        return 2;
    }
}

static uint32_t audioBytesPerSecondLocked(void)
{
    uint32_t channels = g_audioSpec.channels ? g_audioSpec.channels : 1;
    uint32_t bytes = audioBytesPerSample(g_audioSpec.format);
    uint32_t freq = g_audioSpec.freq > 0 ? (uint32_t)g_audioSpec.freq : 16000;
    return freq * channels * bytes;
}

static uint32_t maxQueuedAudioBytesLocked(void)
{
    uint32_t quarterSecond = audioBytesPerSecondLocked() / 4;
    uint32_t deviceBuffer = g_audioSpec.size ? g_audioSpec.size : 4096;
    return quarterSecond > deviceBuffer ? quarterSecond : deviceBuffer;
}

static void clearPendingAudioLocked(void)
{
    g_pendingAudio.clear();
    g_pendingAudioBytes = 0;
}

static void clearAudioStreamLocked(void)
{
    if (g_audioStream)
    {
        SDL_AudioStreamClear(g_audioStream);
    }
}

static bool configureAudioStreamLocked(const waveout_args* args)
{
    if (g_audioStream)
    {
        SDL_FreeAudioStream(g_audioStream);
        g_audioStream = NULL;
    }

    SDL_zero(g_guestAudioSpec);
    g_guestAudioSpec.freq = args->sample_rate;
    g_guestAudioSpec.format = convertFormat(args->format);
    g_guestAudioSpec.channels = args->channel ? args->channel : 2;
    if (g_guestAudioSpec.freq == g_audioSpec.freq &&
        g_guestAudioSpec.format == g_audioSpec.format &&
        g_guestAudioSpec.channels == g_audioSpec.channels)
    {
        return true;
    }

    g_audioStream = SDL_NewAudioStream(
        g_guestAudioSpec.format, g_guestAudioSpec.channels, g_guestAudioSpec.freq,
        g_audioSpec.format, g_audioSpec.channels, g_audioSpec.freq);
    if (!g_audioStream)
    {
        SDL_Log("Couldn't create audio conversion stream: %s", SDL_GetError());
        return false;
    }
    SDL_Log("Audio conversion enabled guest=%dHz/0x%x/%uch host=%dHz/0x%x/%uch",
        g_guestAudioSpec.freq, g_guestAudioSpec.format,
        (unsigned int)g_guestAudioSpec.channels,
        g_audioSpec.freq, g_audioSpec.format,
        (unsigned int)g_audioSpec.channels);
    return true;
}

static bool convertAudioBufferLocked(const char* buffer, int count,
    std::vector<char>* output)
{
    output->clear();
    if (!g_audioStream)
    {
        output->assign(buffer, buffer + count);
        return true;
    }
    if (SDL_AudioStreamPut(g_audioStream, buffer, count) != 0)
    {
        SDL_Log("Audio conversion input failed: %s", SDL_GetError());
        return false;
    }
    int available = SDL_AudioStreamAvailable(g_audioStream);
    if (available <= 0)
    {
        return available == 0;
    }
    output->resize((size_t)available);
    int converted = SDL_AudioStreamGet(g_audioStream, output->data(), available);
    if (converted < 0)
    {
        SDL_Log("Audio conversion output failed: %s", SDL_GetError());
        output->clear();
        return false;
    }
    output->resize((size_t)converted);
    return true;
}

static void flushPendingAudioLocked(void)
{
    while (g_audioDevice && !g_pendingAudio.empty() &&
        SDL_GetQueuedAudioSize(g_audioDevice) < maxQueuedAudioBytesLocked())
    {
        std::vector<char>& pending = g_pendingAudio.front();
        if (SDL_QueueAudio(g_audioDevice, pending.data(), (Uint32)pending.size()) != 0)
        {
            break;
        }
        g_pendingAudioBytes -= (uint32_t)pending.size();
        g_pendingAudio.pop_front();
    }
}

static bool outputMutedLocked(void)
{
    return g_frontendPauseRequested || g_guestMuteRequested || g_volume == 0 || g_masterVolumePercent == 0;
}

static void logAudioBackpressure(uint64_t nowTicks, uint64_t waitBeginTicks, bool dropping)
{
    if (g_lastQueueBackpressureLogTicks &&
        nowTicks - g_lastQueueBackpressureLogTicks < kQueueBackpressureLogIntervalMs)
    {
        return;
    }

    SDL_Log(dropping ?
        "Audio queue saturated for %u ms; dropping guest buffer" :
        "Audio queue saturated for %u ms; waiting for playback",
        (unsigned int)(nowTicks - waitBeginTicks));
    g_lastQueueBackpressureLogTicks = nowTicks;
}

static AudioQueueWaitResult waitForAudioQueueSpaceLocked(uint32_t maxQueued)
{
    uint32_t dropAfterMs = audioQueueDropAfterMs();
    bool traceWaits = dropAfterMs == 0 && audioQueueTraceEnabled();
    uint64_t waitBeginTicks = SDL_GetTicks64();
    while (g_audioDevice && SDL_GetQueuedAudioSize(g_audioDevice) >= maxQueued)
    {
        unlockAudio();
        SDL_Delay(1);
        lockAudio();

        if (!g_audioDevice || !g_gameAudioResourcesActive || outputMutedLocked())
        {
            return AUDIO_QUEUE_OUTPUT_STOPPED;
        }

        uint64_t nowTicks = SDL_GetTicks64();
        if (dropAfterMs > 0 && nowTicks - waitBeginTicks >= dropAfterMs)
        {
            logAudioBackpressure(nowTicks, waitBeginTicks, true);
            audioValidationRecordDrop(SDL_GetQueuedAudioSize(g_audioDevice),
                g_pendingAudioBytes, nowTicks - waitBeginTicks);
            return AUDIO_QUEUE_DROP_BUFFER;
        }
        // Some games normally stream at the queue cap. Keep that path quiet
        // unless audio queue tracing is explicitly requested.
        if (traceWaits)
        {
            logAudioBackpressure(nowTicks, waitBeginTicks, false);
        }
    }

    uint64_t waitMs = SDL_GetTicks64() - waitBeginTicks;
    if (waitMs > 0)
    {
        audioValidationRecordWait(
            g_audioDevice ? SDL_GetQueuedAudioSize(g_audioDevice) : 0,
            g_pendingAudioBytes, waitMs);
    }

    return AUDIO_QUEUE_READY;
}

static int clampIntLocal(int value, int minValue, int maxValue)
{
    if (value < minValue)
    {
        return minValue;
    }
    if (value > maxValue)
    {
        return maxValue;
    }
    return value;
}

static int normalizeBufferSamples(int samples)
{
    switch (samples)
    {
    case 512:
    case 1024:
    case 2048:
    case 4096:
    case 8192:
        return samples;
    default:
        return 2048;
    }
}

static AudioEffectMode normalizeAudioEffect(AudioEffectMode effect)
{
    switch (effect)
    {
    case AUDIO_EFFECT_OFF:
    case AUDIO_EFFECT_SOFT:
    case AUDIO_EFFECT_CLEAR:
    case AUDIO_EFFECT_BASS_BOOST:
    case AUDIO_EFFECT_MONO:
        return effect;
    default:
        return AUDIO_EFFECT_OFF;
    }
}

static int effectiveVolumePercentLocked(void)
{
    uint32_t guestVolume = g_volume > 255 ? 255 : g_volume;
    // Dingoo samples commonly pass 0-100, while some SDK layers document 0-255.
    // Treat 0-100 as direct percent and only normalize larger values from 255.
    int guestPercent = guestVolume <= 100 ? (int)guestVolume : (int)((guestVolume * 100u + 127u) / 255u);
    int masterVolume = clampIntLocal(g_masterVolumePercent, 0, 150);
    return (guestPercent * masterVolume + 50) / 100;
}

static bool audioDisabledEnvEnabled(void)
{
    const char* audioDisabled = getenv("DINGOO_PIE_AUDIO_DISABLED");
    return audioDisabled && audioDisabled[0] && audioDisabled[0] != '0';
}

static int clampS16(int value)
{
    if (value < -32768)
    {
        return -32768;
    }
    if (value > 32767)
    {
        return 32767;
    }
    return value;
}

static void resetAudioEffectStateLocked(void)
{
    memset(g_audioEffectState, 0, sizeof(g_audioEffectState));
    memset(g_audioEffectStateValid, 0, sizeof(g_audioEffectStateValid));
}

static int audioFrameChannelsLocked(void)
{
    int channels = g_audioSpec.channels > 0 ? (int)g_audioSpec.channels : 1;
    return channels > 0 ? channels : 1;
}

static int16_t applyAudioEffectSampleLocked(int16_t sample, int channel)
{
    const int stateChannel = channel % kAudioEffectStateChannels;
    if (!g_audioEffectStateValid[stateChannel])
    {
        g_audioEffectState[stateChannel] = sample;
        g_audioEffectStateValid[stateChannel] = true;
    }

    const int32_t previous = g_audioEffectState[stateChannel];
    int32_t output = sample;
    switch (g_audioEffect)
    {
    case AUDIO_EFFECT_SOFT:
        output = (previous * 3 + sample) / 4;
        g_audioEffectState[stateChannel] = output;
        break;
    case AUDIO_EFFECT_CLEAR:
    {
        const int32_t low = (previous * 3 + sample) / 4;
        output = sample + (sample - low) / 2;
        g_audioEffectState[stateChannel] = low;
        break;
    }
    case AUDIO_EFFECT_BASS_BOOST:
    {
        const int32_t low = (previous * 15 + sample) / 16;
        output = sample + low / 4;
        g_audioEffectState[stateChannel] = low;
        break;
    }
    default:
        break;
    }
    return (int16_t)clampS16((int)output);
}

static void applyMonoEffectS16Locked(int16_t* samples, int sampleCount, int channels)
{
    if (!samples || sampleCount <= 0 || channels < 2)
    {
        return;
    }

    for (int frame = 0; frame + channels <= sampleCount; frame += channels)
    {
        int32_t sum = 0;
        for (int channel = 0; channel < channels; ++channel)
        {
            sum += samples[frame + channel];
        }
        const int16_t mixed = (int16_t)clampS16((int)(sum / channels));
        for (int channel = 0; channel < channels; ++channel)
        {
            samples[frame + channel] = mixed;
        }
    }
}

static void applyMonoEffectU8Locked(uint8_t* samples, int sampleCount, int channels)
{
    if (!samples || sampleCount <= 0 || channels < 2)
    {
        return;
    }

    for (int frame = 0; frame + channels <= sampleCount; frame += channels)
    {
        int32_t sum = 0;
        for (int channel = 0; channel < channels; ++channel)
        {
            sum += (int)samples[frame + channel] - 128;
        }
        const int mixed = clampIntLocal(128 + (int)(sum / channels), 0, 255);
        for (int channel = 0; channel < channels; ++channel)
        {
            samples[frame + channel] = (uint8_t)mixed;
        }
    }
}

static void applyAudioEffectInPlaceLocked(char* buffer, int count)
{
    if (!buffer || count <= 0 || g_audioEffect == AUDIO_EFFECT_OFF)
    {
        return;
    }

    const int channels = audioFrameChannelsLocked();
    switch (g_audioSpec.format)
    {
    case AUDIO_U8:
    {
        uint8_t* samples = (uint8_t*)buffer;
        const int sampleCount = count;
        if (g_audioEffect == AUDIO_EFFECT_MONO)
        {
            applyMonoEffectU8Locked(samples, sampleCount, channels);
            return;
        }

        for (int i = 0; i < sampleCount; ++i)
        {
            const int16_t centered = (int16_t)(((int)samples[i] - 128) << 8);
            const int16_t processed = applyAudioEffectSampleLocked(centered, i % channels);
            samples[i] = (uint8_t)clampIntLocal(128 + ((int)processed >> 8), 0, 255);
        }
        return;
    }
    case AUDIO_S16LSB:
    {
        const int sampleCount = count / 2;
        int16_t* samples = (int16_t*)buffer;
        if (g_audioEffect == AUDIO_EFFECT_MONO)
        {
            applyMonoEffectS16Locked(samples, sampleCount, channels);
            return;
        }

        for (int i = 0; i < sampleCount; ++i)
        {
            samples[i] = applyAudioEffectSampleLocked(samples[i], i % channels);
        }
        break;
    }
    default:
        break;
    }
}

static void applyVolumeInPlaceLocked(char* buffer, int count)
{
    int volumePercent = effectiveVolumePercentLocked();
    if (!buffer || count <= 0 || volumePercent == 100)
    {
        return;
    }

    if (volumePercent <= 0)
    {
        memset(buffer, 0, (size_t)count);
        return;
    }

    if (g_audioSpec.format == AUDIO_U8)
    {
        uint8_t* samples = (uint8_t*)buffer;
        for (int i = 0; i < count; ++i)
        {
            int centered = (int)samples[i] - 128;
            int scaled = 128 + (centered * volumePercent) / 100;
            samples[i] = (uint8_t)clampIntLocal(scaled, 0, 255);
        }
        return;
    }

    if (g_audioSpec.format == AUDIO_S16LSB)
    {
        int sampleCount = count / 2;
        int16_t* samples = (int16_t*)buffer;
        for (int i = 0; i < sampleCount; ++i)
        {
            samples[i] = (int16_t)clampS16(((int)samples[i] * volumePercent) / 100);
        }
    }
}

uint32_t MixerOpen(waveout_args* args)
{
    if (!args)
    {
        return 0;
    }

    lockAudio();
    if (g_audioDevice)
    {
        g_volume = args->volume;
        g_audioOutputUnavailable = false;
        g_gameAudioResourcesActive = true;
        resetAudioEffectStateLocked();
        resetAudioBackpressureLog();
        clearPendingAudioLocked();
        SDL_ClearQueuedAudio(g_audioDevice);
        if (!configureAudioStreamLocked(args))
        {
            g_audioOutputUnavailable = true;
            unlockAudio();
            return 1;
        }
        audioValidationBegin(g_audioSpec);
        SDL_PauseAudioDevice(g_audioDevice, outputMutedLocked() ? 1 : 0);
        SDL_Log("Reusing audio device for new guest runtime");
        unlockAudio();
        return 1;
    }

    g_volume = args->volume;
    g_audioOutputUnavailable = false;
    g_gameAudioResourcesActive = true;
    resetAudioEffectStateLocked();
    resetAudioBackpressureLog();
    int bufferSamples = normalizeBufferSamples(g_bufferSamples);
    SDL_Log(
        "Audio waveout open requested sample_rate=%u format=%u channels=%u "
        "buffer_samples=%d guest_volume=%u master_volume=%d%% effective_volume=%d%%",
        (unsigned int)args->sample_rate,
        (unsigned int)args->format,
        (unsigned int)args->channel,
        bufferSamples,
        g_volume,
        g_masterVolumePercent,
        effectiveVolumePercentLocked());

    SDL_AudioSpec want;
    SDL_zero(want);
    const bool stableCcOutput =
        g_runtimeAudioProfile == MIXER_RUNTIME_AUDIO_CC_STABLE_HOST;
    want.freq = stableCcOutput ? kCcStableHostSampleRate : (int)args->sample_rate;
    want.format = stableCcOutput ? AUDIO_S16LSB : convertFormat(args->format);
    want.channels = stableCcOutput ? kCcStableHostChannels :
        (args->channel ? args->channel : 2);
    want.samples = (Uint16)bufferSamples;
    want.callback = NULL;

    SDL_Log("Audio runtime profile=%s output_request=%dHz/0x%x/%uch",
        stableCcOutput ? "cc_stable_host" : "native_guest",
        want.freq, want.format, (unsigned int)want.channels);

    const int allowedChanges = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
        SDL_AUDIO_ALLOW_FORMAT_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE;
    g_audioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &g_audioSpec, allowedChanges);
    if (!g_audioDevice)
    {
        SDL_Log("Guest audio device open failed: %s; retrying standard host format",
            SDL_GetError());
        SDL_AudioSpec hostWant;
        SDL_zero(hostWant);
        hostWant.freq = 48000;
        hostWant.format = AUDIO_S16LSB;
        hostWant.channels = 2;
        hostWant.samples = (Uint16)bufferSamples;
        hostWant.callback = NULL;
        g_audioDevice = SDL_OpenAudioDevice(
            NULL, 0, &hostWant, &g_audioSpec, allowedChanges);
    }
    if (!g_audioDevice)
    {
        SDL_Log("Couldn't open audio: %s", SDL_GetError());
        SDL_Log("Audio output disabled; guest audio buffers will be dropped");
        g_audioOutputUnavailable = true;
        unlockAudio();
        return 1;
    }

    if (!configureAudioStreamLocked(args))
    {
        SDL_CloseAudioDevice(g_audioDevice);
        g_audioDevice = 0;
        g_audioOutputUnavailable = true;
        unlockAudio();
        return 1;
    }

    audioValidationBegin(g_audioSpec);
    SDL_Log("Opened audio at %d Hz, format 0x%x, channels %d, samples %d, guest_volume=%u master_volume=%d%% effective_volume=%d%%",
        g_audioSpec.freq, g_audioSpec.format, g_audioSpec.channels, g_audioSpec.samples, g_volume,
        g_masterVolumePercent, effectiveVolumePercentLocked());
    SDL_PauseAudioDevice(g_audioDevice, outputMutedLocked() ? 1 : 0);
    unlockAudio();
    return 1;
}

uint32_t MixerClose()
{
    MixerReleaseGameResources();
    return 1;
}

void MixerReleaseGameResources(void)
{
    lockAudio();
    audioValidationClose();
    g_gameAudioResourcesActive = false;
    if (g_audioDevice)
    {
        SDL_PauseAudioDevice(g_audioDevice, 1);
        SDL_ClearQueuedAudio(g_audioDevice);
    }
    clearPendingAudioLocked();
    clearAudioStreamLocked();
    resetAudioEffectStateLocked();
    resetAudioBackpressureLog();
    SDL_Log("Released game audio resources");
    unlockAudio();
}

void MixerResetAfterRuntimeStop(void)
{
    SDL_mutex* previousMutex = g_audioMutex;
    // Keep the SDL device alive across guest restarts; closing it blocks on MuMu.
    // Only reset guest-owned state and replace a mutex left locked by forced stop.
    g_audioOutputUnavailable = false;
    g_gameAudioResourcesActive = false;
    g_frontendPauseRequested = false;
    g_guestMuteRequested = false;
    resetAudioEffectStateLocked();
    resetAudioBackpressureLog();
    audioValidationClose();
    clearPendingAudioLocked();
    clearAudioStreamLocked();
    g_audioMutex = SDL_CreateMutex();

    if (previousMutex)
    {
        if (SDL_TryLockMutex(previousMutex) == 0)
        {
            SDL_UnlockMutex(previousMutex);
            SDL_DestroyMutex(previousMutex);
        }
    }
}

void MixerPrepareApplicationExit(void)
{
    SDL_AudioDeviceID audioDevice = 0;
    SDL_AudioStream* audioStream = NULL;

    lockAudio();
    audioValidationClose();
    audioDevice = g_audioDevice;
    audioStream = g_audioStream;
    if (audioDevice)
    {
        SDL_PauseAudioDevice(audioDevice, 1);
        SDL_ClearQueuedAudio(audioDevice);
    }
    g_audioDevice = 0;
    g_audioStream = NULL;
    SDL_zero(g_audioSpec);
    SDL_zero(g_guestAudioSpec);
    g_audioOutputUnavailable = false;
    g_gameAudioResourcesActive = false;
    g_frontendPauseRequested = false;
    g_guestMuteRequested = false;
    clearPendingAudioLocked();
    resetAudioEffectStateLocked();
    resetAudioBackpressureLog();
    unlockAudio();

    if (audioStream)
    {
        SDL_FreeAudioStream(audioStream);
    }
    if (audioDevice)
    {
        SDL_CloseAudioDevice(audioDevice);
    }
    SDL_Log("Application audio state released and device closed");
}

uint32_t MixerWriteBuff(char* buffer, int count)
{
    if (!buffer || count <= 0)
    {
        free(buffer);
        return 0;
    }

    if (audioDisabledEnvEnabled() || g_audioOutputUnavailable || !g_audioDevice)
    {
        free(buffer);
        return 1;
    }

    lockAudio();
    if (!g_audioDevice || !g_gameAudioResourcesActive || outputMutedLocked())
    {
        unlockAudio();
        free(buffer);
        return 1;
    }

    std::vector<char> converted;
    bool convertedOk = convertAudioBufferLocked(buffer, count, &converted);
    free(buffer);
    if (!convertedOk)
    {
        unlockAudio();
        return 0;
    }
    if (converted.empty())
    {
        unlockAudio();
        return 1;
    }

    AudioQueueWaitResult waitResult = waitForAudioQueueSpaceLocked(maxQueuedAudioBytesLocked());
    if (waitResult != AUDIO_QUEUE_READY)
    {
        unlockAudio();
        return 1;
    }

    applyAudioEffectInPlaceLocked(converted.data(), (int)converted.size());
    applyVolumeInPlaceLocked(converted.data(), (int)converted.size());
    uint32_t queuedBytes = SDL_GetQueuedAudioSize(g_audioDevice);
    audioValidationRecordAudio(converted.data(), (uint32_t)converted.size(),
        "queue", queuedBytes, g_pendingAudioBytes);
    int queued = SDL_QueueAudio(g_audioDevice, converted.data(), (Uint32)converted.size());
    if (queued != 0)
    {
        audioValidationRecordEvent("queue_error", (uint32_t)converted.size(),
            queuedBytes, g_pendingAudioBytes, 0);
    }
    unlockAudio();
    return queued == 0 ? 1 : 0;
}

uint32_t MixerTryWriteBuff(char* buffer, int count)
{
    if (!buffer || count <= 0)
    {
        free(buffer);
        return 0;
    }

    if (audioDisabledEnvEnabled() || g_audioOutputUnavailable || !g_audioDevice)
    {
        free(buffer);
        return 1;
    }

    lockAudio();
    if (!g_audioDevice || !g_gameAudioResourcesActive || outputMutedLocked())
    {
        unlockAudio();
        free(buffer);
        return 1;
    }
    flushPendingAudioLocked();
    std::vector<char> converted;
    bool convertedOk = convertAudioBufferLocked(buffer, count, &converted);
    free(buffer);
    if (!convertedOk)
    {
        unlockAudio();
        return 0;
    }
    if (converted.empty())
    {
        unlockAudio();
        return 1;
    }
    if (SDL_GetQueuedAudioSize(g_audioDevice) >= maxQueuedAudioBytesLocked())
    {
        applyAudioEffectInPlaceLocked(converted.data(), (int)converted.size());
        applyVolumeInPlaceLocked(converted.data(), (int)converted.size());
        audioValidationRecordAudio(converted.data(), (uint32_t)converted.size(),
            "pending", SDL_GetQueuedAudioSize(g_audioDevice),
            g_pendingAudioBytes);
        if (converted.size() <= kPendingAudioMaxBytes - g_pendingAudioBytes)
        {
            g_pendingAudio.push_back(std::move(converted));
            g_pendingAudioBytes += (uint32_t)g_pendingAudio.back().size();
            unlockAudio();
            return 1;
        }
        unlockAudio();
        return 0;
    }

    applyAudioEffectInPlaceLocked(converted.data(), (int)converted.size());
    applyVolumeInPlaceLocked(converted.data(), (int)converted.size());
    uint32_t queuedBytes = SDL_GetQueuedAudioSize(g_audioDevice);
    audioValidationRecordAudio(converted.data(), (uint32_t)converted.size(),
        "queue", queuedBytes, g_pendingAudioBytes);
    int queued = SDL_QueueAudio(g_audioDevice, converted.data(), (Uint32)converted.size());
    if (queued != 0)
    {
        audioValidationRecordEvent("queue_error", (uint32_t)converted.size(),
            queuedBytes, g_pendingAudioBytes, 0);
    }
    unlockAudio();
    return queued == 0 ? 1 : 0;
}

uint32_t MixerPlaying()
{
    uint32_t canWrite = MixerCanWriteNonBlocking();
    if (!canWrite)
    {
        SDL_Delay(1);
    }
    return canWrite;
}

uint32_t MixerCanWriteNonBlocking()
{
    lockAudio();
    flushPendingAudioLocked();
    uint32_t canWrite = 1;
    if (audioDisabledEnvEnabled() || g_audioOutputUnavailable)
    {
        canWrite = 1;
    }
    else if (g_audioDevice && g_gameAudioResourcesActive && !outputMutedLocked())
    {
        canWrite = SDL_GetQueuedAudioSize(g_audioDevice) < maxQueuedAudioBytesLocked() &&
            g_pendingAudio.empty() ? 1 : 0;
    }
    unlockAudio();
    return canWrite;
}

bool MixerSkipsAudioOutput()
{
    if (audioDisabledEnvEnabled())
    {
        return true;
    }

    lockAudio();
    bool skipsAudioOutput = g_audioOutputUnavailable || outputMutedLocked() || !g_audioDevice;
    unlockAudio();
    return skipsAudioOutput;
}

void MixerSetVolume(uint32_t vol)
{
    lockAudio();
    g_volume = vol > 255 ? 255 : vol;
    if (g_audioDevice)
    {
        bool muted = outputMutedLocked();
        SDL_PauseAudioDevice(g_audioDevice, muted ? 1 : 0);
        if (muted)
        {
            SDL_ClearQueuedAudio(g_audioDevice);
            clearPendingAudioLocked();
            clearAudioStreamLocked();
        }
    }
    SDL_Log("Audio guest volume set to %u, master=%d%%, effective=%d%%%s",
        g_volume, g_masterVolumePercent, effectiveVolumePercentLocked(), outputMutedLocked() ? " muted" : "");
    unlockAudio();
}

void MixerSetMuted(bool muted)
{
    lockAudio();
    g_guestMuteRequested = muted;
    if (g_audioDevice)
    {
        bool outputMuted = outputMutedLocked();
        SDL_PauseAudioDevice(g_audioDevice, outputMuted ? 1 : 0);
        if (outputMuted)
        {
            SDL_ClearQueuedAudio(g_audioDevice);
            clearPendingAudioLocked();
            clearAudioStreamLocked();
        }
    }
    SDL_Log("Audio mute %s", g_guestMuteRequested ? "on" : "off");
    unlockAudio();
}

void MixerSetFrontendPaused(bool paused)
{
    lockAudio();
    g_frontendPauseRequested = paused;
    if (g_audioDevice)
    {
        bool outputMuted = outputMutedLocked();
        SDL_PauseAudioDevice(g_audioDevice, outputMuted ? 1 : 0);
        if (paused)
        {
            // Avoid replaying stale guest audio when gameplay resumes.
            SDL_ClearQueuedAudio(g_audioDevice);
            clearPendingAudioLocked();
            clearAudioStreamLocked();
            resetAudioEffectStateLocked();
        }
    }
    SDL_Log("Audio frontend pause %s", g_frontendPauseRequested ? "on" : "off");
    unlockAudio();
}

void MixerSetMasterVolumePercent(int percent)
{
    lockAudio();
    g_masterVolumePercent = clampIntLocal(percent, 0, 150);
    if (g_audioDevice)
    {
        SDL_PauseAudioDevice(g_audioDevice, outputMutedLocked() ? 1 : 0);
        SDL_ClearQueuedAudio(g_audioDevice);
        clearPendingAudioLocked();
        clearAudioStreamLocked();
    }
    SDL_Log("Audio master volume set to %d%%, guest=%u, effective=%d%%%s",
        g_masterVolumePercent, g_volume, effectiveVolumePercentLocked(), outputMutedLocked() ? " muted" : "");
    unlockAudio();
}

void MixerSetBufferSamples(int samples)
{
    lockAudio();
    g_bufferSamples = normalizeBufferSamples(samples);
    SDL_Log("Audio buffer samples set to %d", g_bufferSamples);
    unlockAudio();
}

void MixerSetAudioEffect(AudioEffectMode effect)
{
    lockAudio();
    effect = normalizeAudioEffect(effect);
    if (g_audioEffect != effect)
    {
        g_audioEffect = effect;
        resetAudioEffectStateLocked();
        if (g_audioDevice)
        {
            SDL_ClearQueuedAudio(g_audioDevice);
            clearPendingAudioLocked();
            clearAudioStreamLocked();
        }
    }
    SDL_Log("Audio effect set to %s", emulatorAudioEffectName(g_audioEffect));
    unlockAudio();
}

void MixerSetRuntimeAudioProfile(MixerRuntimeAudioProfile profile)
{
    lockAudio();
    g_runtimeAudioProfile = profile;
    SDL_Log("Audio runtime profile selected: %s",
        profile == MIXER_RUNTIME_AUDIO_CC_STABLE_HOST ?
        "cc_stable_host" : "native_guest");
    unlockAudio();
}

void MixerSetValidationCaptureEnabled(bool enabled)
{
    lockAudio();
    audioValidationSetEnabled(enabled);
    SDL_Log("Audio validation capture %s", enabled ? "enabled" : "disabled");
    unlockAudio();
}
