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

static int
SWITCHAUDIO_OpenDevice(_THIS, void *handle, const char *devname, int iscapture)
{
    Result res;
    PcmFormat fmt;
    int mixlen, aligned_size, i;

    this->hidden = (struct SDL_PrivateAudioData *) SDL_malloc(sizeof(*this->hidden));
    if (this->hidden == NULL) {
        return SDL_OutOfMemory();
    }
    SDL_zerop(this->hidden);

    res = audoutInitialize();
    if (R_FAILED(res)) {
        SDL_free(this->hidden);
        this->hidden = NULL;
        return SDL_SetError("audoutInitialize failed (0x%x)", res);
    }

    fmt = audoutGetPcmFormat();
    switch (fmt) {
    case PcmFormat_Invalid:
        SDL_free(this->hidden);
        this->hidden = NULL;
        return SDL_SetError("audoutGetPcmFormat returned invalid value (0x%x)", (int)fmt);
    case PcmFormat_Int8:
        this->spec.format = AUDIO_S8;
        break;
    case PcmFormat_Int16:
        this->spec.format = AUDIO_S16SYS;
        break;
    case PcmFormat_Int32:
        this->spec.format = AUDIO_S32SYS;
        break;
    case PcmFormat_Float:
        this->spec.format = AUDIO_F32SYS;
        break;
    case PcmFormat_Int24:
    case PcmFormat_Adpcm:
        SDL_free(this->hidden);
        this->hidden = NULL;
        return SDL_SetError("audoutGetPcmFormat returned unsupported sample format (0x%x)", (int)fmt);
        break;
    }

    this->spec.freq = (int)audoutGetSampleRate();
    this->spec.channels = audoutGetChannelCount();

    SDL_CalculateAudioSpec(&this->spec);

    aligned_size = (this->spec.size + 0xfff) & ~0xfff;
    mixlen = aligned_size * NUM_BUFFERS;

    this->hidden->rawbuf = memalign(0x1000, mixlen);
    if (this->hidden->rawbuf == NULL) {
        SDL_free(this->hidden);
        this->hidden = NULL;
        return SDL_SetError("Couldn't allocate mixing buffer");
    }

    SDL_memset(this->hidden->rawbuf, 0, mixlen);
    for (i = 0; i < NUM_BUFFERS; i++) {
        this->hidden->out_buffers[i] = &this->hidden->rawbuf[i * aligned_size];
        this->hidden->buffer[i].next = NULL;
        this->hidden->buffer[i].buffer = this->hidden->out_buffers[i];
        this->hidden->buffer[i].buffer_size = aligned_size;
        this->hidden->buffer[i].data_size = this->spec.size;
        this->hidden->buffer[i].data_offset = 0;
    }

    this->hidden->cur_buffer = this->hidden->next_buffer;
    this->hidden->next_buffer = (this->hidden->next_buffer + 1) % NUM_BUFFERS;

    res = audoutAppendAudioOutBuffer(&this->hidden->buffer[this->hidden->cur_buffer]);
    if (R_FAILED(res)) {
        free(this->hidden->rawbuf);
        this->hidden->rawbuf = NULL;
        SDL_free(this->hidden);
        this->hidden = NULL;
        return SDL_SetError("audoutAppendAudioOutBuffer failed (0x%x)", res);
    }

    res = audoutStartAudioOut();
    if (R_FAILED(res)) {
        free(this->hidden->rawbuf);
        this->hidden->rawbuf = NULL;
        SDL_free(this->hidden);
        this->hidden = NULL;
        return SDL_SetError("audoutStartAudioOut failed (0x%x)", res);
    }

    return 0;
}

static void
SWITCHAUDIO_PlayDevice(_THIS)
{
    this->hidden->cur_buffer = this->hidden->next_buffer;
    audoutAppendAudioOutBuffer(&this->hidden->buffer[this->hidden->cur_buffer]);
    this->hidden->next_buffer = (this->hidden->next_buffer + 1) % NUM_BUFFERS;
}

static void
SWITCHAUDIO_WaitDevice(_THIS)
{
    audoutWaitPlayFinish(&this->hidden->released_out_buffer, &this->hidden->released_out_count, UINT64_MAX);
}

static Uint8
*SWITCHAUDIO_GetDeviceBuf(_THIS)
{
    return this->hidden->out_buffers[this->hidden->next_buffer];
}

static void
SWITCHAUDIO_CloseDevice(_THIS)
{
    audoutStopAudioOut();
    audoutExit();

    if (this->hidden->rawbuf) {
        free(this->hidden->rawbuf);
        this->hidden->rawbuf = NULL;
    }

    SDL_free(this->hidden);
}

static void
SWITCHAUDIO_ThreadInit(_THIS)
{
    (void)this;
}

static int
SWITCHAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = SWITCHAUDIO_OpenDevice;
    impl->PlayDevice = SWITCHAUDIO_PlayDevice;
    impl->WaitDevice = SWITCHAUDIO_WaitDevice;
    impl->GetDeviceBuf = SWITCHAUDIO_GetDeviceBuf;
    impl->CloseDevice = SWITCHAUDIO_CloseDevice;
    impl->ThreadInit = SWITCHAUDIO_ThreadInit;

    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;

    return SDL_TRUE;
}

AudioBootStrap SWITCHAUDIOOUT_bootstrap = {
    "switchout", "Nintendo Switch audio out driver", SWITCHAUDIO_Init, SDL_FALSE
};

#endif /* SDL_AUDIO_DRIVER_SWITCH */
