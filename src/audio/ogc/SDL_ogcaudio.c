/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

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

#ifdef SDL_AUDIO_DRIVER_OGC

/* OGC Audio driver */

#include "../SDL_sysaudio.h"
#include "SDL_ogcaudio.h"

#include <malloc.h>

#define OGCAUDIO_DRIVER_NAME "ogc"

/**
 * Cleans up all allocated memory, safe to call with null pointers
 */
static void FreePrivateData(SDL_AudioDevice *this)
{
    if (!this->hidden) {
        return;
    }

    SDL_free(this->hidden);
    this->hidden = NULL;
}

static bool FindAudioFormat(SDL_AudioDevice *this)
{
    bool found_valid_format = false;
    const SDL_AudioFormat *closefmts;
    Uint16 test_format;

    closefmts = SDL_ClosestAudioFormats(this->spec.format);
    while (!found_valid_format && (test_format = *(closefmts++)) != 0) {
        this->spec.format = test_format;
        SDL_LogDebug(SDL_LOG_CATEGORY_AUDIO, "Trying format %x", test_format);
        switch (test_format) {
        case SDL_AUDIO_S8:
            this->hidden->format = VOICE_MONO8;
            this->hidden->bytes_per_sample = this->spec.channels;
            found_valid_format = true;
            break;
        case SDL_AUDIO_U8:
            this->hidden->format = VOICE_MONO8_UNSIGNED;
            this->hidden->bytes_per_sample = this->spec.channels;
            found_valid_format = true;
            break;
        case SDL_AUDIO_S16LE:
        case SDL_AUDIO_S16BE:
            this->hidden->format = VOICE_MONO16;
            this->hidden->bytes_per_sample = this->spec.channels * 2;
            this->spec.format = SDL_AUDIO_S16BE;
            found_valid_format = true;
            break;
        default:
            break;
        }
    }

    if (found_valid_format && this->spec.channels == 2) {
        this->hidden->format++;
    }

    return found_valid_format;
}

/* fully local functions related to the wavebufs / DSP, not the same as the SDL-wide mixer lock */
static SDL_INLINE void contextLock(SDL_AudioDevice *this)
{
    LWP_MutexLock(this->hidden->lock);
}

static SDL_INLINE void contextUnlock(SDL_AudioDevice *this)
{
    LWP_MutexUnlock(this->hidden->lock);
}

static void audio_frame_finished(AESNDPB *pb, u32 state, void *arg)
{
    SDL_AudioDevice *this = (SDL_AudioDevice *)arg;

    if (state == VOICE_STATE_STREAM) {
        const size_t buffer_size = DMA_BUFFER_SIZE;
        s8 playing_buffer;
        void *buffer;

        /* Immediately send the next buffer to the DSP. It's important that
         * AESND_SetVoiceBuffer() gets called before this callback returns, or
         * some audio gaps might be audible. */
        contextLock(this);
        playing_buffer = (this->hidden->playing_buffer + 1) % NUM_BUFFERS;
        buffer = this->hidden->dma_buffers[playing_buffer];
        AESND_SetVoiceBuffer(pb, buffer, buffer_size);
        this->hidden->playing_buffer = playing_buffer;
        contextUnlock(this);

        /* If a frame has finished playing, it means that the corresponding
         * buffer is no longer in use and can be filled up again. We signal
         * this event to the audio thread via a semaphore. */
        LWP_SemPost(this->hidden->available_buffers);
    } if (state == VOICE_STATE_STOPPED) {
        contextLock(this);
        this->hidden->playing_buffer = -1;
        contextUnlock(this);
    }
}

static bool OGCAUDIO_OpenDevice(SDL_AudioDevice *this)
{
    struct SDL_PrivateAudioData *hidden =
        memalign(32, sizeof(struct SDL_PrivateAudioData));
    if (!hidden) {
        return SDL_OutOfMemory();
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_AUDIO,
                 "OGCAUDIO_OpenDevice, freq=%d, channels=%d\n",
                 this->spec.freq, this->spec.channels);

    memset(hidden, 0, sizeof(*hidden));
    hidden->playing_buffer = -1;
    this->hidden = hidden;

    AESND_Init();
    AESND_Pause(1);

    /* Initialise internal state */
    LWP_MutexInit(&hidden->lock, false);
    /* We set the initial number of available buffers to NUM_BUFFERS - 1, since
     * SDL first calls GetDeviceBuf() and starts filling it without first
     * calling WaitDevice(). So we consider the first buffer to be busy already
     * at start. */
    LWP_SemInit(&hidden->available_buffers, NUM_BUFFERS - 1, NUM_BUFFERS);

    if (this->spec.freq <= 0 || this->spec.freq > 144000)
        this->spec.freq = DSP_DEFAULT_FREQ;

    if (this->spec.channels > 2) {
        this->spec.channels = 2;
    }

    /* Should not happen but better be safe. */
    if (!FindAudioFormat(this)) {
        return SDL_SetError("No supported audio format found.");
    }

    this->sample_frames = DMA_BUFFER_SIZE / this->hidden->bytes_per_sample;

    /* Update the fragment size as size in bytes */
    SDL_UpdatedAudioDeviceFormat(this);

    hidden->voice = AESND_AllocateVoiceWithArg(audio_frame_finished, this);
    if (hidden->voice == NULL)
        return false;

    // start audio
    AESND_SetVoiceFormat(hidden->voice, hidden->format);
    AESND_SetVoiceFrequency(hidden->voice, this->spec.freq);
    AESND_SetVoiceBuffer(hidden->voice, hidden->dma_buffers[0], DMA_BUFFER_SIZE);
    AESND_SetVoiceStream(hidden->voice, true);
    AESND_SetVoiceStop(hidden->voice, 0);
    AESND_Pause(0);

    return true;
}

static bool OGCAUDIO_PlayDevice(SDL_AudioDevice *this,
                                const Uint8 *buffer_unused, int buflen_unused)
{
    void *buffer;

    contextLock(this);
    if (this->hidden->playing_buffer < 0) {
        buffer = this->hidden->dma_buffers[++this->hidden->playing_buffer];
        AESND_SetVoiceBuffer(this->hidden->voice, buffer, DMA_BUFFER_SIZE);
    }
    contextUnlock(this);
    return true;
}

static bool OGCAUDIO_WaitDevice(SDL_AudioDevice *this)
{
    s8 nextbuf;

    /* This will block until at least one buffer is available for writing. */
    LWP_SemWait(this->hidden->available_buffers);

    nextbuf = this->hidden->nextbuf;
    this->hidden->nextbuf = (nextbuf + 1) % NUM_BUFFERS;
    return true;
}

static Uint8 *OGCAUDIO_GetDeviceBuf(SDL_AudioDevice *this, int *buffer_size)
{
    return this->hidden->dma_buffers[this->hidden->nextbuf];
}

static void OGCAUDIO_CloseDevice(SDL_AudioDevice *this)
{
    struct SDL_PrivateAudioData *hidden = this->hidden;

    LWP_SemDestroy(hidden->available_buffers);
    if (hidden->voice) {
        AESND_SetVoiceStop(hidden->voice, true);
        AESND_FreeVoice(hidden->voice);
        hidden->voice = NULL;
    }

    AESND_Pause(1);
    FreePrivateData(this);
}

static void OGCAUDIO_ThreadInit(SDL_AudioDevice *this)
{
    LWP_SetThreadPriority(LWP_THREAD_NULL, 80);
}

static bool OGCAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    /* Set the function pointers */
    impl->OpenDevice = OGCAUDIO_OpenDevice;
    impl->PlayDevice = OGCAUDIO_PlayDevice;
    impl->WaitDevice = OGCAUDIO_WaitDevice;
    impl->GetDeviceBuf = OGCAUDIO_GetDeviceBuf;
    impl->CloseDevice = OGCAUDIO_CloseDevice;
    impl->ThreadInit = OGCAUDIO_ThreadInit;
    impl->OnlyHasDefaultPlaybackDevice = true;

    return true; /* this audio target is available. */
}

AudioBootStrap OGCAUDIO_bootstrap = {
    OGCAUDIO_DRIVER_NAME,
    "SDL OGC audio driver",
    OGCAUDIO_Init,
    false, false
};

#endif /* SDL_AUDIO_DRIVER_OGC */
