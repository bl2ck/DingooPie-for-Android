#include "guest/guest_audio.h"
#include "frontend/sdl_audio.h"
#include <stdlib.h>

uint32_t waveout_open(waveout_args* args)
{
	printf("audio: waveout_open channel=%d format=%d sample_rate=%d volume=%d\n",
		args->channel, args->format, args->sample_rate, args->volume);

    uint32_t ret = mixerOpen(args);
    free(args);
	return ret;
}

uint32_t waveout_write(uint32_t inst, char* buffer, int count)
{
    return mixerWriteBuffer(buffer, count);
}

uint32_t waveout_try_write(uint32_t inst, char* buffer, int count)
{
    return mixerTryWriteBuffer(buffer, count);
}

uint32_t waveout_can_write()
{
    return mixerIsPlaying();
}

uint32_t waveout_can_write_nonblocking()
{
    return mixerCanWriteNonBlocking();
}

bool waveout_skips_audio_output()
{
    return mixerSkipsAudioOutput();
}

uint32_t waveout_set_volume(uint32_t vol)
{
    mixerSetGuestVolume(vol);
    return 1;
}

uint32_t waveout_close(uint32_t inst)
{
    (void)inst;
    return mixerClose();
}

uint32_t waveout_mute(uint32_t muted)
{
    mixerSetMuted(muted != 0);
    return 1;
}
