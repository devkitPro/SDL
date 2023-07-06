/*
  Copyright (C) 1997-2017 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/

#include <stdlib.h>
#include <stdio.h>

#include <switch.h>
#include "SDL2/SDL.h"

static SDL_DisplayMode modes[5];

static int mode_count = 0, current_mode = 0;

void print_info(SDL_Window *window, SDL_Renderer *renderer)
{
    int w, h;
    SDL_DisplayMode mode;

    SDL_GetWindowSize(window, &w, &h);
    SDL_Log("window size: %i x %i\n", w, h);
    SDL_GetRendererOutputSize(renderer, &w, &h);
    SDL_Log("renderer size: %i x %i\n", w, h);

    SDL_GetCurrentDisplayMode(0, &mode);
    SDL_Log("display mode: %i x %i @ %i bpp (%s)",
            mode.w, mode.h,
            SDL_BITSPERPIXEL(mode.format),
            SDL_GetPixelFormatName(mode.format));
}

void change_mode(SDL_Window *window)
{
    current_mode++;
    if (current_mode == mode_count) {
        current_mode = 0;
    }

    SDL_SetWindowDisplayMode(window, &modes[current_mode]);
}

void draw_rects(SDL_Renderer *renderer, int x, int y)
{
    // R
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_Rect r = {x, y, 64, 64};
    SDL_RenderFillRect(renderer, &r);

    // G
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    SDL_Rect g = {x + 64, y, 64, 64};
    SDL_RenderFillRect(renderer, &g);

    // B
    SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
    SDL_Rect b = {x + 128, y, 64, 64};
    SDL_RenderFillRect(renderer, &b);
}

int main(int argc, char *argv[])
{
    SDL_Event event;
    SDL_Window *window;
    SDL_Renderer *renderer;
    int done = 0, x = 0, w = 0, h = 0;

    // mandatory at least on switch, else gfx is not properly closed
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        SDL_Log("SDL_Init: %s\n", SDL_GetError());
        return -1;
    }

    /// create a window (OpenGL always enabled)
    /// available switch SDL2 video modes :
    /// 1920 x 1080 @ 32 bpp (SDL_PIXELFORMAT_RGBA8888)
    /// 1280 x 720 @ 32 bpp (SDL_PIXELFORMAT_RGBA8888)
    ///
    /// SDL_SetWindowSize to change window size when SDL_WINDOW_FULLSCREEN is NOT used (preferably)
    /// SDL_SetDisplayMode to change display size after SDL_CreateWindow called with SDL_WINDOW_FULLSCREEN
    /// (this means window size won't change, you'll need to handle that, as any SDL2 app)
    window = SDL_CreateWindow("sdl2_gles2", 0, 0, 1280, 720, 0);
    if (!window) {
        SDL_Log("SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    // create a renderer (OpenGL ES2)
    renderer = SDL_CreateRenderer(window, 0, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    // pint some info about display/window/renderer
    print_info(window, renderer);

    // list available display modes
    mode_count = SDL_GetNumDisplayModes(0);
    for (int i = 0; i < mode_count; i++) {
        SDL_DisplayMode mode;
        SDL_GetDisplayMode(0, i, &mode);
        modes[i] = mode;
        SDL_Log("found display mode: %i x %i @ %i bpp (%s)",
                mode.w, mode.h,
                SDL_BITSPERPIXEL(mode.format),
                SDL_GetPixelFormatName(mode.format));
    }

    // open CONTROLLER_PLAYER_1 and CONTROLLER_PLAYER_2
    // when railed, both joycons are mapped to joystick #0,
    // else joycons are individually mapped to joystick #0, joystick #1, ...
    // https://github.com/devkitPro/SDL/blob/switch-sdl2/src/joystick/switch/SDL_sysjoystick.c#L45
    for (int i = 0; i < 2; i++) {
        if (SDL_JoystickOpen(i) == NULL) {
            SDL_Log("SDL_JoystickOpen: %s\n", SDL_GetError());
            SDL_Quit();
            return -1;
        }
    }

    while (!done) {

        while (SDL_PollEvent(&event)) {

            switch (event.type) {

                case SDL_JOYAXISMOTION:
                    SDL_Log("Joystick %d axis %d value: %d\n",
                            event.jaxis.which,
                            event.jaxis.axis, event.jaxis.value);
                    break;

                case SDL_JOYBUTTONDOWN:
                    SDL_Log("Joystick %d button %d down\n",
                            event.jbutton.which, event.jbutton.button);
                    // https://github.com/devkitPro/SDL/blob/switch-sdl2/src/joystick/switch/SDL_sysjoystick.c#L52
                    if (event.jbutton.which == 0) {
                        if (event.jbutton.button == 0) {
                            // joystick #0 down (A)
                            change_mode(window);
                            print_info(window, renderer);
                        }
                        else if (event.jbutton.button == 2) {
                            // joystick #0 down (X)
                            if (w == 1920) {
                                SDL_SetWindowSize(window, 1280, 720);
                            }
                            else {
                                SDL_SetWindowSize(window, 1920, 1080);
                            }
                            print_info(window, renderer);
                        }
                    }
                    // joystick #0 down (B)
                    if (event.jbutton.which == 0 && event.jbutton.button == 1) {
                        done = 1;
                    }
                    break;

                default:
                    break;
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        // Fill renderer bounds
        SDL_SetRenderDrawColor(renderer, 111, 111, 111, 255);
        SDL_GetWindowSize(window, &w, &h);
        SDL_Rect f = {0, 0, w, h};
        SDL_RenderFillRect(renderer, &f);

        draw_rects(renderer, x, 0);
        draw_rects(renderer, x, h - 64);

        SDL_RenderPresent(renderer);

        x++;
        if (x > w - 192) {
            x = 0;
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

//-----------------------------------------------------------------------------
// nxlink support
//-----------------------------------------------------------------------------

#include <unistd.h>

static int s_nxlinkSock = -1;

static void initNxLink()
{
    if (R_FAILED(socketInitializeDefault()))
        return;

    s_nxlinkSock = nxlinkStdio();
    if (s_nxlinkSock >= 0)
        printf("printf output now goes to nxlink server\n");
    else
        socketExit();
}

static void deinitNxLink()
{
    if (s_nxlinkSock >= 0) {
        close(s_nxlinkSock);
        socketExit();
        s_nxlinkSock = -1;
    }
}

void userAppInit()
{
    initNxLink();
}

void userAppExit()
{
    deinitNxLink();
}
