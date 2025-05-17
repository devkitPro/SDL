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
#include "SDL_internal.h"

#if SDL_AUDIO_DRIVER_SWITCH

#include <malloc.h>
#include <string.h>

#include "SDL_switchaudio.h"

// clang-format off
static const AudioRendererConfig arConfig = {
    .output_rate     = AudioRendererOutputRate_48kHz,
    .num_voices      = 24,
    .num_effects     = 0,
    .num_sinks       = 1,
    .num_mix_objs    = 1,
    .num_mix_buffers = 2,
};
// clang-format on

static bool SWITCHAUDIO_OpenDevice(SDL_AudioDevice *device)
{
    static const u8 sink_channels[] = { 0, 1 };
    bool supported_format = false;
    SDL_AudioFormat test_format;
    Result res;
    u32 size;
    int mpid;
    const SDL_AudioFormat *closefmts;

    device->hidden = (struct SDL_PrivateAudioData *)SDL_malloc(sizeof(*device->hidden));
    if (device->hidden == NULL) {
        return SDL_OutOfMemory();
    }
    SDL_zerop(device->hidden);

    res = audrenInitialize(&arConfig);
    if (R_FAILED(res)) {
        return SDL_SetError("audrenInitialize failed (0x%x)", res);
    }
    device->hidden->audr_device = true;

    res = audrvCreate(&device->hidden->driver, &arConfig, 2);
    if (R_FAILED(res)) {
        return SDL_SetError("audrvCreate failed (0x%x)", res);
    }
    device->hidden->audr_driver = true;

    closefmts = SDL_ClosestAudioFormats(device->spec.format);
    while ((test_format = *(closefmts++)) != 0) {
        if (test_format == SDL_AUDIO_S16) {
            supported_format = true;
            break;
        }
    }

    if (!supported_format) {
        return SDL_SetError("Unsupported audio format");
    }

    device->spec.format = test_format;

    SDL_UpdatedAudioDeviceFormat(device);

    if (device->buffer_size >= SDL_MAX_UINT32 / 2) {
        return SDL_SetError("Mixing buffer is too large.");
    }

    size = (u32)((device->buffer_size * 2) + 0xfff) & ~0xfff;
    device->hidden->pool = memalign(0x1000, size);
    for (int i = 0; i < 2; i++) {
        device->hidden->buffer[i].data_raw = device->hidden->pool;
        device->hidden->buffer[i].size = device->buffer_size * 2;
        device->hidden->buffer[i].start_sample_offset = i * device->sample_frames;
        device->hidden->buffer[i].end_sample_offset = device->hidden->buffer[i].start_sample_offset + device->sample_frames;
        device->hidden->buffer_tmp = SDL_malloc(device->buffer_size);
    }

    mpid = audrvMemPoolAdd(&device->hidden->driver, device->hidden->pool, size);
    audrvMemPoolAttach(&device->hidden->driver, mpid);

    audrvDeviceSinkAdd(&device->hidden->driver, AUDREN_DEFAULT_DEVICE_NAME, 2, sink_channels);

    res = audrenStartAudioRenderer();
    if (R_FAILED(res)) {
        return SDL_SetError("audrenStartAudioRenderer failed (0x%x)", res);
    }

    audrvVoiceInit(&device->hidden->driver, 0, device->spec.channels, PcmFormat_Int16, device->spec.freq);
    audrvVoiceSetDestinationMix(&device->hidden->driver, 0, AUDREN_FINAL_MIX_ID);
    if (device->spec.channels == 1) {
        audrvVoiceSetMixFactor(&device->hidden->driver, 0, 1.0f, 0, 0);
        audrvVoiceSetMixFactor(&device->hidden->driver, 0, 1.0f, 0, 1);
    } else {
        audrvVoiceSetMixFactor(&device->hidden->driver, 0, 1.0f, 0, 0);
        audrvVoiceSetMixFactor(&device->hidden->driver, 0, 0.0f, 0, 1);
        audrvVoiceSetMixFactor(&device->hidden->driver, 0, 0.0f, 1, 0);
        audrvVoiceSetMixFactor(&device->hidden->driver, 0, 1.0f, 1, 1);
    }

    audrvVoiceStart(&device->hidden->driver, 0);

    return true;
}

static bool SWITCHAUDIO_PlayDevice(SDL_AudioDevice *device, const Uint8 *buffer, int buflen)
{
    int current = -1;
    for (int i = 0; i < 2; i++) {
        if (device->hidden->buffer[i].state == AudioDriverWaveBufState_Free || device->hidden->buffer[i].state == AudioDriverWaveBufState_Done) {
            current = i;
            break;
        }
    }

    /* paranoia */
    SDL_assert(buffer == device->hidden->buffer_tmp);
    SDL_assert(buflen == device->buffer_size);

    if (current >= 0) {
        Uint8 *ptr = (Uint8 *)(device->hidden->pool + (current * device->buffer_size));
        memcpy(ptr, device->hidden->buffer_tmp, device->buffer_size);
        armDCacheFlush(ptr, device->buffer_size);
        audrvVoiceAddWaveBuf(&device->hidden->driver, 0, &device->hidden->buffer[current]);
    } else if (!audrvVoiceIsPlaying(&device->hidden->driver, 0)) {
        audrvVoiceStart(&device->hidden->driver, 0);
    }

    audrvUpdate(&device->hidden->driver);

    if (current >= 0) {
        while (device->hidden->buffer[current].state != AudioDriverWaveBufState_Playing) {
            audrvUpdate(&device->hidden->driver);
            audrenWaitFrame();
        }
    } else {
        current = -1;
        for (int i = 0; i < 2; i++) {
            if (device->hidden->buffer[i].state == AudioDriverWaveBufState_Playing) {
                current = i;
                break;
            }
        }
        while (device->hidden->buffer[current].state == AudioDriverWaveBufState_Playing) {
            audrvUpdate(&device->hidden->driver);
            audrenWaitFrame();
        }
    }

    return true;
}

static bool SWITCHAUDIO_WaitDevice(SDL_AudioDevice *device)
{
    return true;
}

static Uint8 *SWITCHAUDIO_GetDeviceBuf(SDL_AudioDevice *device, int *buffer_size)
{
    return device->hidden->buffer_tmp;
}

static void SWITCHAUDIO_CloseDevice(SDL_AudioDevice *device)
{
    if (device->hidden->audr_driver) {
        audrvClose(&device->hidden->driver);
    }

    if (device->hidden->audr_device) {
        audrenExit();
    }

    if (device->hidden->buffer_tmp) {
        free(device->hidden->buffer_tmp);
    }

    SDL_free(device->hidden);
}

static bool SWITCHAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = SWITCHAUDIO_OpenDevice;
    impl->PlayDevice = SWITCHAUDIO_PlayDevice;
    impl->WaitDevice = SWITCHAUDIO_WaitDevice;
    impl->GetDeviceBuf = SWITCHAUDIO_GetDeviceBuf;
    impl->CloseDevice = SWITCHAUDIO_CloseDevice;

    impl->OnlyHasDefaultPlaybackDevice = true;

    return 1;
}

// clang-format off
AudioBootStrap SWITCHAUDIO_bootstrap = {
    .name         = "switch",
    .desc         = "Nintendo Switch audio driver",
    .init         = SWITCHAUDIO_Init,
    .demand_only  = false,
    .is_preferred = true,
};
// clang-format on

#endif /* SDL_AUDIO_DRIVER_SWITCH */
