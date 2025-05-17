/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2018 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#if SDL_AUDIO_DRIVER_SWITCH

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>

#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "../SDL_audiodev_c.h"

#include "SDL_switchaudio.h"

static const AudioRendererConfig arConfig =
    {
        .output_rate     = AudioRendererOutputRate_48kHz,
        .num_voices      = 24,
        .num_effects     = 0,
        .num_sinks       = 1,
        .num_mix_objs    = 1,
        .num_mix_buffers = 2,
    };

static int
SWITCHAUDIO_OpenDevice(_THIS, const char *devname)
{
    static const u8 sink_channels[] = {0, 1};
    SDL_bool supported_format = SDL_FALSE;
    SDL_AudioFormat test_format;
    Result res;
    u32 size;
    int mpid;

    this->hidden = (struct SDL_PrivateAudioData *) SDL_malloc(sizeof(*this->hidden));
    if (this->hidden == NULL) {
        return SDL_OutOfMemory();
    }
    SDL_zerop(this->hidden);

    res = audrenInitialize(&arConfig);
    if (R_FAILED(res)) {
        return SDL_SetError("audrenInitialize failed (0x%x)", res);
    }
    this->hidden->audr_device = true;

    res = audrvCreate(&this->hidden->driver, &arConfig, 2);
    if (R_FAILED(res)) {
        return SDL_SetError("audrvCreate failed (0x%x)", res);
    }
    this->hidden->audr_driver = true;

    test_format = SDL_FirstAudioFormat(this->spec.format);
    while ((!supported_format) && (test_format)) {
        if (test_format == AUDIO_S16SYS) {
            supported_format = SDL_TRUE;
        }
        else {
            test_format = SDL_NextAudioFormat();
        }
    }
    if (!supported_format) {
        return SDL_SetError("Unsupported audio format");
    }

    this->spec.format = test_format;
    SDL_CalculateAudioSpec(&this->spec);

    size = (u32) ((this->spec.size * 2) + 0xfff) & ~0xfff;
    this->hidden->pool = memalign(0x1000, size);
    for (int i = 0; i < 2; i++) {
        this->hidden->buffer[i].data_raw = this->hidden->pool;
        this->hidden->buffer[i].size = this->spec.size * 2;
        this->hidden->buffer[i].start_sample_offset = i * this->spec.samples;
        this->hidden->buffer[i].end_sample_offset = this->hidden->buffer[i].start_sample_offset + this->spec.samples;
        this->hidden->buffer_tmp = malloc(this->spec.size);
    }

    mpid = audrvMemPoolAdd(&this->hidden->driver, this->hidden->pool, size);
    audrvMemPoolAttach(&this->hidden->driver, mpid);

    audrvDeviceSinkAdd(&this->hidden->driver, AUDREN_DEFAULT_DEVICE_NAME, 2, sink_channels);

    res = audrenStartAudioRenderer();
    if (R_FAILED(res)) {
        return SDL_SetError("audrenStartAudioRenderer failed (0x%x)", res);
    }

    audrvVoiceInit(&this->hidden->driver, 0, this->spec.channels, PcmFormat_Int16, this->spec.freq);
    audrvVoiceSetDestinationMix(&this->hidden->driver, 0, AUDREN_FINAL_MIX_ID);
    if (this->spec.channels == 1) {
        audrvVoiceSetMixFactor(&this->hidden->driver, 0, 1.0f, 0, 0);
        audrvVoiceSetMixFactor(&this->hidden->driver, 0, 1.0f, 0, 1);
    }
    else {
        audrvVoiceSetMixFactor(&this->hidden->driver, 0, 1.0f, 0, 0);
        audrvVoiceSetMixFactor(&this->hidden->driver, 0, 0.0f, 0, 1);
        audrvVoiceSetMixFactor(&this->hidden->driver, 0, 0.0f, 1, 0);
        audrvVoiceSetMixFactor(&this->hidden->driver, 0, 1.0f, 1, 1);
    }

    audrvVoiceStart(&this->hidden->driver, 0);

    return 0;
}

static void
SWITCHAUDIO_PlayDevice(_THIS)
{
    int current = -1;
    for (int i = 0; i < 2; i++) {
        if (this->hidden->buffer[i].state == AudioDriverWaveBufState_Free
            || this->hidden->buffer[i].state == AudioDriverWaveBufState_Done) {
            current = i;
            break;
        }
    }

    if (current >= 0) {
        Uint8 *ptr = (Uint8 *) (this->hidden->pool + (current * this->spec.size));
        memcpy(ptr, this->hidden->buffer_tmp, this->spec.size);
        armDCacheFlush(ptr, this->spec.size);
        audrvVoiceAddWaveBuf(&this->hidden->driver, 0, &this->hidden->buffer[current]);
    }
    else if (!audrvVoiceIsPlaying(&this->hidden->driver, 0)) {
        audrvVoiceStart(&this->hidden->driver, 0);
    }

    audrvUpdate(&this->hidden->driver);

    if (current >= 0) {
        while (this->hidden->buffer[current].state != AudioDriverWaveBufState_Playing) {
            audrvUpdate(&this->hidden->driver);
            audrenWaitFrame();
        }
    }
    else {
        current = -1;
        for (int i = 0; i < 2; i++) {
            if (this->hidden->buffer[i].state == AudioDriverWaveBufState_Playing) {
                current = i;
                break;
            }
        }
        while (this->hidden->buffer[current].state == AudioDriverWaveBufState_Playing) {
            audrvUpdate(&this->hidden->driver);
            audrenWaitFrame();
        }
    }
}

static void
SWITCHAUDIO_WaitDevice(_THIS)
{
}

static Uint8
*SWITCHAUDIO_GetDeviceBuf(_THIS)
{
    return this->hidden->buffer_tmp;
}

static void
SWITCHAUDIO_CloseDevice(_THIS)
{
    if (this->hidden->audr_driver) {
        audrvClose(&this->hidden->driver);
    }

    if (this->hidden->audr_device) {
        audrenExit();
    }

    if (this->hidden->buffer_tmp) {
        free(this->hidden->buffer_tmp);
    }

    SDL_free(this->hidden);
}

static void
SWITCHAUDIO_ThreadInit(_THIS)
{

}

static SDL_bool
SWITCHAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = SWITCHAUDIO_OpenDevice;
    impl->PlayDevice = SWITCHAUDIO_PlayDevice;
    impl->WaitDevice = SWITCHAUDIO_WaitDevice;
    impl->GetDeviceBuf = SWITCHAUDIO_GetDeviceBuf;
    impl->CloseDevice = SWITCHAUDIO_CloseDevice;
    impl->ThreadInit = SWITCHAUDIO_ThreadInit;

    impl->OnlyHasDefaultOutputDevice = 1;

    return 1;
}

AudioBootStrap SWITCHAUDIO_bootstrap = {
    "switch", "Nintendo Switch audio driver", SWITCHAUDIO_Init, 0
};

#endif /* SDL_AUDIO_DRIVER_SWITCH */
