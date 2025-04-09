/*
  Simple DirectMedia Layer
  Copyright (C) 2018-2018 Ash Logan <ash@heyquark.com>

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

#if SDL_AUDIO_DRIVER_WIIU

#include <stdio.h>
#include <malloc.h>

#include "SDL_audio.h"
#include "SDL_error.h"
#include "SDL_timer.h"
#include "../SDL_audio_c.h"
#include "../SDL_audiodev_c.h"
#include "../SDL_sysaudio.h"
#include "SDL_wiiuaudio.h"
#include "SDL_wiiuaudio_mix.h"

#include <sndcore2/core.h>
#include <sndcore2/voice.h>
#include <sndcore2/drcvs.h>
#include <coreinit/core.h>
#include <coreinit/cache.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/memorymap.h>
#include <coreinit/debug.h>

#define WIIUAUDIO_DRIVER_NAME "wiiu"

#define AX_MAIN_AFFINITY OS_THREAD_ATTRIB_AFFINITY_CPU1

#define WII_U_TV 0
#define WII_U_GAMEPAD 1
#define WII_U_BOTH 2

static void _WIIUAUDIO_framecallback();
static SDL_AudioDevice* wiiuDevices[3];
//static int deviceType;
static int deviceCount = 0;
static SDL_bool AXIsFrameCallbackRegistered = SDL_FALSE;
//#define cb_hidden cb_this->hidden

/*  Some helpers for AX-related math */
/*  Absolute address to an AXVoiceOffsets offset */
#define calc_ax_offset(offs, addr) (((void*)addr - offs.data) \
    / sizeof_sample(offs))

#define sizeof_sample(offs) (offs.dataType == AX_VOICE_FORMAT_LPCM8 ? 1 : 2)

/*  +1, but never goes above NUM_BUFFERS */
#define next_id(id) (id + 1) % NUM_BUFFERS

typedef struct {
    SDL_AudioDevice *device;
    int deviceType;
} OpenThreadArgs;

static int open_device_thread(void *arg) {
    OpenThreadArgs *threadArgs = (OpenThreadArgs*)arg;
    SDL_AudioDevice *thisDevice = threadArgs->device;
    int deviceType = threadArgs->deviceType;
    
    AXVoiceOffsets offs;
    AXVoiceVeData vol = {
        .volume = 0x8000,
    };
    float srcratio;
    Uint8* mixbuf = NULL;
    uint32_t mixbuf_allocation_count = 0;
    Uint8* mixbuf_allocations[32];

    thisDevice->hidden = (struct SDL_PrivateAudioData*)SDL_malloc(sizeof(*thisDevice->hidden));
    if (thisDevice->hidden == NULL) {
        return SDL_OutOfMemory();
    }

    SDL_zerop(thisDevice->hidden);
    free(threadArgs);

    OSReport("%d\n", deviceType);

/*  Take a quick aside to init the wiiu audio */
    if (!AXIsInit()) {
    /*  Init the AX audio engine */
        AXInitParams initparams = {
            .renderer = AX_INIT_RENDERER_48KHZ,
            .pipeline = AX_INIT_PIPELINE_SINGLE,
        };
        AXInitWithParams(&initparams);
    } else printf("DEBUG: AX already up?\n");

    if (thisDevice->spec.channels < 1) thisDevice->spec.channels = 1;
    if (thisDevice->spec.channels > WIIU_MAX_VALID_CHANNELS)
        thisDevice->spec.channels = WIIU_MAX_VALID_CHANNELS;

/*  Force wiiu-compatible audio formats.
    TODO verify - unsigned or signed? */
    switch (SDL_AUDIO_BITSIZE(thisDevice->spec.format)) {
        case 8:
        /*  TODO 8-bit audio sounds broken */
            /*this->spec.format = AUDIO_S8;
            break;*/
        case 16:
        default:
            thisDevice->spec.format = AUDIO_S16MSB;
            break;
    }

    //TODO maybe round this->spec.samples up even when >?
    //maybe even force at least 2* so we get more frame callbacks to think
    if (thisDevice->spec.samples < AXGetInputSamplesPerFrame()) {
        thisDevice->spec.samples = AXGetInputSamplesPerFrame();
    }

/*  We changed channels and samples, so recalculate the spec */
    SDL_CalculateAudioSpec(&thisDevice->spec);

/*  Allocate buffers for double-buffering and samples.
    Make sure the entire mixbuf is in a 512MiB block for the DSP to be accessible. */
    for (int i = 0; i < 32; i++) {
        Uint32 physStart, physEnd;
        mixbuf = memalign(0x40, thisDevice->spec.size * NUM_BUFFERS);
        if (!mixbuf) {
            break;
        }

        physStart = OSEffectiveToPhysical((uint32_t) mixbuf) & 0x1fffffff;
        physEnd = physStart + thisDevice->spec.size * NUM_BUFFERS;
        if ((physEnd & 0xe0000000) == 0) {
            break;
        }

        mixbuf_allocations[mixbuf_allocation_count] = mixbuf;
        mixbuf_allocation_count++;
        mixbuf = NULL;
    }

/*  Free the failed attempts */
    while (mixbuf_allocation_count--) {
        free(mixbuf_allocations[mixbuf_allocation_count]);
    }

    if (!mixbuf) {
        printf("Couldn't allocate mix buffer\n");
        return SDL_OutOfMemory();
    }

    memset(mixbuf, 0, thisDevice->spec.size * NUM_BUFFERS);
    DCStoreRange(mixbuf, thisDevice->spec.size * NUM_BUFFERS);

    for (int i = 0; i < NUM_BUFFERS; i++) {
        thisDevice->hidden->mixbufs[i] = mixbuf + thisDevice->spec.size * i;
    }

/*  Allocate a scratch buffer for deinterleaving operations */
    thisDevice->hidden->deintvbuf = SDL_malloc(thisDevice->spec.size);
    if (thisDevice->hidden->deintvbuf == NULL) {
        AXQuit();
        printf("DEBUG: Couldn't allocate deinterleave buffer");
        return SDL_SetError("Couldn't allocate deinterleave buffer");
    }


    for (int i = 0; i < thisDevice->spec.channels; i++) {
    /*  Get a voice, top priority */
        thisDevice->hidden->voice[i] = AXAcquireVoice(31, NULL, NULL);
        if (!thisDevice->hidden->voice[i]) {
            AXQuit();
            printf("DEBUG: couldn't get voice\n");
            return SDL_OutOfMemory();
        }

    /*  Start messing with it */
        AXVoiceBegin(thisDevice->hidden->voice[i]);
        AXSetVoiceType(thisDevice->hidden->voice[i], 0);

    /*  Set the voice's volume. */
        AXSetVoiceVe(thisDevice->hidden->voice[i], &vol);

        switch (thisDevice->spec.channels) {
            case 1: /* mono */ {
                if (deviceType == WII_U_BOTH) {
                    AXSetVoiceDeviceMix(thisDevice->hidden->voice[i],
                        AX_DEVICE_TYPE_DRC, 0, mono_mix[i]);
                    AXSetVoiceDeviceMix(thisDevice->hidden->voice[i],
                        AX_DEVICE_TYPE_TV, 0, mono_mix[i]);
                }
                else if (deviceType == WII_U_TV) {
                    AXSetVoiceDeviceMix(thisDevice->hidden->voice[i],
                        AX_DEVICE_TYPE_TV, 0, mono_mix[i]);
                }
                else if (deviceType == WII_U_GAMEPAD) {
                    AXSetVoiceDeviceMix(thisDevice->hidden->voice[i],
                        AX_DEVICE_TYPE_DRC, 0, mono_mix[i]);
                }
            } break;
            case 2: /* stereo */ {
                if (deviceType == WII_U_BOTH) {
                    AXSetVoiceDeviceMix(thisDevice->hidden->voice[i],
                        AX_DEVICE_TYPE_DRC, 0, stereo_mix[i]);
                    AXSetVoiceDeviceMix(thisDevice->hidden->voice[i],
                        AX_DEVICE_TYPE_TV, 0, stereo_mix[i]);
                }
                else if (deviceType == WII_U_TV) {
                    AXSetVoiceDeviceMix(thisDevice->hidden->voice[i],
                        AX_DEVICE_TYPE_TV, 0, stereo_mix[i]);
                }
                else if (deviceType == WII_U_GAMEPAD) {
                    AXSetVoiceDeviceMix(thisDevice->hidden->voice[i],
                        AX_DEVICE_TYPE_DRC, 0, stereo_mix[i]);
                }
            } break;
        }

    /*  Set the samplerate conversion ratio
        <source sample rate> / <target sample rate> */
        srcratio = (float)thisDevice->spec.freq / (float)AXGetInputSamplesPerSec();
        AXSetVoiceSrcRatio(thisDevice->hidden->voice[i], srcratio);
        AXSetVoiceSrcType(thisDevice->hidden->voice[i], AX_VOICE_SRC_TYPE_LINEAR);

    /*  Set up the offsets for the first mixbuf */
        switch (SDL_AUDIO_BITSIZE(thisDevice->spec.format)) {
            case 8:
                offs.dataType = AX_VOICE_FORMAT_LPCM8;
                offs.endOffset = thisDevice->spec.samples;
                break;
            case 16:
            default:
                offs.dataType = AX_VOICE_FORMAT_LPCM16;
                offs.endOffset = thisDevice->spec.samples;
                break;
        }
        offs.loopingEnabled = AX_VOICE_LOOP_ENABLED;
        offs.loopOffset = 0;
        offs.currentOffset = 0;

        if (offs.dataType == AX_VOICE_FORMAT_LPCM8) {
            offs.data = thisDevice->hidden->mixbufs[0]
                + thisDevice->spec.samples * i * sizeof(Uint8);
        } else if (offs.dataType == AX_VOICE_FORMAT_LPCM16) {
            offs.data = thisDevice->hidden->mixbufs[0]
                + thisDevice->spec.samples * i * sizeof(Uint16);
        }
        AXSetVoiceOffsets(thisDevice->hidden->voice[i], &offs);

    /*  Set the last good loopcount */
        thisDevice->hidden->last_loopcount = AXGetVoiceLoopCount(thisDevice->hidden->voice[i]);

    /*  Offsets are set for playing the first mixbuf, so we should render the second */
        thisDevice->hidden->playingid = 0;
        thisDevice->hidden->renderingid = 1;

    /*  Start playing. */
        AXSetVoiceState(thisDevice->hidden->voice[i], AX_VOICE_STATE_PLAYING);

    /*  Okay, we're good */
        AXVoiceEnd(thisDevice->hidden->voice[i]);
    }

    deviceCount++;
    OSReport("Device Num: %d\n", deviceCount);
    if (deviceCount > 3) {
        deviceCount = 3;
    }

    //this->hidden->wiiudeviceNum = deviceCount;
    
    wiiuDevices[deviceCount] = thisDevice;

    OSReport("Hello?\n");
    
    //AXRegisterAppFrameCallback(_WIIUAUDIO_framecallback);

    OSReport("Hello?\n");

    return 0;
}

static void thread_deallocator(OSThread* thread, void* stack) {
   free(thread);
   free(stack);
}

static void thread_cleanup(OSThread* thread, void* stack) {
}

static void WIIUAUDIO_DetectDevices(void) {
    void *drcHandle;
    void *tvHandle;
    void *bothHandle;
    
    /* This gets reset later anyways */
    SDL_AudioSpec spec;

    spec.channels = WIIU_MAX_VALID_CHANNELS;
    spec.format = AUDIO_S16MSB;
    spec.samples = 4096;

    SDL_CalculateAudioSpec(&spec);

    SDL_AddAudioDevice(SDL_FALSE, "Wii U TV", &spec, &tvHandle);
    SDL_AddAudioDevice(SDL_FALSE, "Wii U Gamepad", &spec, &drcHandle);
    SDL_AddAudioDevice(SDL_FALSE, "Wii U Both", &spec, &bothHandle);
}

static int WIIUAUDIO_OpenDevice(_THIS, const char* devname) {
    int result;
    /* AX functions need to run from the same core.
    Since we cannot easily change the affinity of the currently running thread
    we simply create a new one which only runs on CPU1 (AX_MAIN_AFFINITY), then join it */
    OSThread *thread = (OSThread *)memalign(16, sizeof(OSThread));
    uint32_t stackSize = 32 * 1024;
    uint8_t *stack = memalign(16, stackSize);
    int32_t priority = OSGetThreadPriority(OSGetCurrentThread());

    OpenThreadArgs *threadArgs = malloc(sizeof(OpenThreadArgs));
    threadArgs->device = this;
    
    threadArgs->deviceType = WII_U_BOTH;  
    if (strcmp(devname, "Wii U TV") == 0) {
        threadArgs->deviceType = WII_U_TV;
    }
    else if (strcmp(devname, "Wii U Gamepad") == 0) {
        threadArgs->deviceType = WII_U_GAMEPAD;     
    }

    OSReport("Before thread\n");

    if (!OSCreateThread(thread,
                        (OSThreadEntryPointFn)open_device_thread,
                        (int32_t)threadArgs,
                        NULL,
                        stack + stackSize,
                        stackSize,
                        priority,
                        AX_MAIN_AFFINITY))
    {
        return SDL_SetError("OSCreateThread() failed");
    }

    OSReport("After thread\n");

    OSSetThreadDeallocator(thread, &thread_deallocator);
    OSReport("Ur mom\n");
    OSSetThreadCleanupCallback(thread, &thread_cleanup);
    OSReport("Ur mom1\n");
    OSResumeThread(thread);
    OSReport("Ur mom2\n");

    if (!OSJoinThread(thread, &result)) {
        return SDL_SetError("OSJoinThread() failed");
    }

    if (!AXIsFrameCallbackRegistered) {
        AXRegisterAppFrameCallback(_WIIUAUDIO_framecallback);
        AXIsFrameCallbackRegistered = SDL_TRUE;
    }

    OSReport("After thread 2\n");

    return result;
}

/*  Called every 3ms before a frame of audio is rendered. Keep it fast! */
static void _WIIUAUDIO_framecallback() {
    for (int deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex) {
        SDL_AudioDevice *dev = wiiuDevices[deviceIndex];

        int playing_buffer = -1;
        AXVoiceOffsets offs[6];
        void* endaddr;

        if (!dev || !dev->hidden) {
            OSReport("FrameCallback: device %d is null or missing hidden!\n", deviceIndex);
            continue;
        }

        if (!dev->hidden->mixbufs[0]) {
            OSReport("FrameCallback: device %d has NULL mixbufs[0]!\n", deviceIndex);
            continue;
        }

        for (int i = 0; i < dev->spec.channels; i++) {
            AXGetVoiceOffsets(dev->hidden->voice[i], &offs[i]);
        }

        for (int i = 0; i < NUM_BUFFERS; i++) {
            void* buf = dev->hidden->mixbufs[i];
            uint32_t startOffset = calc_ax_offset(offs[0], buf);
            uint32_t endOffset = startOffset + dev->spec.samples;

            if (offs[0].currentOffset >= startOffset &&
                offs[0].currentOffset <= endOffset) {
                playing_buffer = i;
                break;
            }
        }

        if (playing_buffer < 0 || playing_buffer >= NUM_BUFFERS) {
            printf("DEBUG: Playing an invalid buffer? This is not a good sign.\n");
            playing_buffer = 0;
        }

        dev->hidden->playingid = playing_buffer;

        for (int i = 0; i < dev->spec.channels; i++) {
            void* loopaddr;
            
            endaddr = dev->hidden->mixbufs[dev->hidden->playingid] +
                      (dev->spec.samples * sizeof_sample(offs[i]) * (i + 1));
            endaddr -= 2;

            AXSetVoiceEndOffset(dev->hidden->voice[i], calc_ax_offset(offs[i], endaddr));

            if (dev->hidden->renderingid != next_id(dev->hidden->playingid)) {
                loopaddr = dev->hidden->mixbufs[next_id(dev->hidden->playingid)] +
                           (dev->spec.samples * sizeof_sample(offs[i]) * i);
            } else {
                loopaddr = dev->hidden->mixbufs[dev->hidden->playingid] +
                           (dev->spec.samples * sizeof_sample(offs[i]) * i);
            }

            AXSetVoiceLoopOffset(dev->hidden->voice[i], calc_ax_offset(offs[i], loopaddr));
        }
    }
}

static void WIIUAUDIO_PlayDevice(_THIS) {
/*  Deinterleave stereo audio */
    if (!this || !this->hidden) {
        OSReport("WIIUAUDIO_PlayDevice: null this or hidden!\n");
        return;
    }
    if (!this->hidden->deintvbuf) {
        OSReport("WIIUAUDIO_PlayDevice: deintvbuf is NULL!\n");
        return;
    }
    if (!this->hidden->mixbufs[this->hidden->renderingid]) {
        OSReport("WIIUAUDIO_PlayDevice: mixbufs[renderingid] is NULL!\n");
        return;
    }    

    switch (SDL_AUDIO_BITSIZE(this->spec.format)) {
        case 8: {
            Uint8* samples = (Uint8*)this->hidden->mixbufs[this->hidden->renderingid];
            Uint8* deintv = (Uint8*)this->hidden->deintvbuf;

            /* Store the samples in a separate deinterleaved buffer */
            for (int ch = 0; ch < this->spec.channels; ch++) {
                for (int i = 0; i < this->spec.samples; i++) {
                    deintv[this->spec.samples * ch + i] = samples[i * this->spec.channels + ch];
                }
            }
        } break;
        case 16: {
            Uint16* samples = (Uint16*)this->hidden->mixbufs[this->hidden->renderingid];
            Uint16* deintv = (Uint16*)this->hidden->deintvbuf;

            /* Store the samples in a separate deinterleaved buffer */
            for (int ch = 0; ch < this->spec.channels; ch++) {
                for (int i = 0; i < this->spec.samples; i++) {
                    deintv[this->spec.samples * ch + i] = samples[i * this->spec.channels + ch];
                }
            }
        } break;
        default: {} break;
    }

/*  Copy the deinterleaved buffer to the mixing buffer */
    memcpy(
        this->hidden->mixbufs[this->hidden->renderingid],
        this->hidden->deintvbuf,
        this->spec.size
    );
/*  Comment this out for broken-record mode ;3 */
    DCStoreRange(this->hidden->mixbufs[this->hidden->renderingid], this->spec.size);
/*  Signal we're no longer rendering this buffer, AX callback will notice later */
    this->hidden->renderingid = next_id(this->hidden->renderingid);
}

static void WIIUAUDIO_WaitDevice(_THIS) {
/*  TODO use real thread sync stuff */
    while (SDL_AtomicGet(&this->enabled) && this->hidden->renderingid == this->hidden->playingid) {
        OSSleepTicks(OSMillisecondsToTicks(3));
    }
}

static Uint8* WIIUAUDIO_GetDeviceBuf(_THIS) {
/*  SDL will write audio samples into this buffer */
    return this->hidden->mixbufs[this->hidden->renderingid];
}

static void WIIUAUDIO_CloseDevice(_THIS) {
    if (AXIsInit()) {
        AXDeregisterAppFrameCallback(_WIIUAUDIO_framecallback);
        for (int i = 0; i < SIZEOF_ARR(this->hidden->voice); i++) {
            if (this->hidden->voice[i]) {
                AXFreeVoice(this->hidden->voice[i]);
                this->hidden->voice[i] = NULL;
            }
        }
        AXQuit();
    }
    if (this->hidden->mixbufs[0]) free(this->hidden->mixbufs[0]);
    if (this->hidden->deintvbuf) SDL_free(this->hidden->deintvbuf);
    SDL_free(this->hidden);
}

static void WIIUAUDIO_ThreadInit(_THIS) {
/*  Bump our thread's priority a bit */
    OSThread* currentThread = OSGetCurrentThread();
    int32_t priority = OSGetThreadPriority(currentThread);
    priority -= 1;
    OSSetThreadPriority(currentThread, priority);
}

static SDL_bool WIIUAUDIO_Init(SDL_AudioDriverImpl* impl) {
    impl->DetectDevices = WIIUAUDIO_DetectDevices;
    impl->OpenDevice = WIIUAUDIO_OpenDevice;
    impl->PlayDevice = WIIUAUDIO_PlayDevice;
    impl->WaitDevice = WIIUAUDIO_WaitDevice;
    impl->GetDeviceBuf = WIIUAUDIO_GetDeviceBuf;
    impl->CloseDevice = WIIUAUDIO_CloseDevice;
    impl->ThreadInit = WIIUAUDIO_ThreadInit;

    impl->OnlyHasDefaultOutputDevice = SDL_FALSE;

    return SDL_TRUE;
}

AudioBootStrap WIIUAUDIO_bootstrap = {
    WIIUAUDIO_DRIVER_NAME, "Wii U AX Audio Driver", WIIUAUDIO_Init, 0,
};

#endif //SDL_AUDIO_DRIVER_WIIU