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

#ifndef _SDL_ogcaudio_h_
#define _SDL_ogcaudio_h_

#include <aesndlib.h>
#include <ogcsys.h>

#include <ogc/mutex.h>
#include <ogc/semaphore.h>

#define NUM_BUFFERS            4 /* -- Minimum 2! */
#define SAMPLES_PER_DMA_BUFFER (DSP_STREAMBUFFER_SIZE)
#define DMA_BUFFER_SIZE        (SAMPLES_PER_DMA_BUFFER * 2 * sizeof(short))

struct SDL_PrivateAudioData
{
    /* these go first so they will be aligned */
    Uint8 dma_buffers[NUM_BUFFERS][DMA_BUFFER_SIZE];
    AESNDPB *voice;

    /* Speaker data */
    Uint32 format;
    Uint8 bytes_per_sample;
    s8 nextbuf;
    s8 playing_buffer;
    mutex_t lock;
    sem_t available_buffers;
};

#endif /* _SDL_ogcaudio_h_ */
