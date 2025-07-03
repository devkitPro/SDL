/*
  Simple DirectMedia Layer
  Copyright (C) 2022 GaryOderNichts <garyodernichts@gmail.com>

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

#if SDL_VIDEO_RENDER_WIIU

#include "../SDL_sysrender.h"
#include "SDL_hints.h"
#include "SDL_render_wiiu.h"

#include <gx2/texture.h>
#include <gx2/draw.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/state.h>
#include <gx2/clear.h>
#include <gx2/mem.h>
#include <gx2/event.h>
#include <gx2r/surface.h>
#include <gx2r/buffer.h>
#include <gx2r/draw.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int WIIU_SDL_QueueSetViewport(SDL_Renderer * renderer, SDL_RenderCommand * cmd)
{
    return 0;
}

int WIIU_SDL_QueueSetDrawColor(SDL_Renderer * renderer, SDL_RenderCommand * cmd)
{
    return 0;
}

int WIIU_SDL_QueueDrawPoints(SDL_Renderer * renderer, SDL_RenderCommand * cmd, const SDL_FPoint * points, int count)
{
    WIIU_RenderData *data = (WIIU_RenderData *) renderer->driverdata;
    SDL_VertexSolid *vertices;
    GX2RBuffer *vertexBuffer;
    SDL_Color color;

    // discard empty draws or GX2 will crash
    if (count < 1) {
        return -1;
    }

    color.r = cmd->data.draw.r;
    color.g = cmd->data.draw.g;
    color.b = cmd->data.draw.b;
    color.a = cmd->data.draw.a;

    vertexBuffer = WIIU_AllocRenderData(data, (GX2RBuffer) {
        .flags =
            GX2R_RESOURCE_BIND_VERTEX_BUFFER |
            GX2R_RESOURCE_USAGE_CPU_WRITE,
        .elemSize = sizeof(SDL_VertexSolid),
        .elemCount = count,
    });

    if (!vertexBuffer) {
        return -1;
    }

    cmd->data.draw.first = (size_t)vertexBuffer;
    cmd->data.draw.count = count;

    vertices = GX2RLockBufferEx(vertexBuffer, 0);

    for (int i = 0; i < count; ++i) {
        vertices[i].position.x = points[i].x;
        vertices[i].position.y = points[i].y;
        vertices[i].color = color;
    }

    GX2RUnlockBufferEx(vertexBuffer, 0);

    return 0;
}

int WIIU_SDL_QueueDrawLines(SDL_Renderer * renderer, SDL_RenderCommand * cmd, const SDL_FPoint * points, int count)
{
    WIIU_RenderData *data = (WIIU_RenderData *) renderer->driverdata;
    SDL_VertexSolid *vertices;
    GX2RBuffer *vertexBuffer;
    SDL_Color color;

    // discard empty draws or GX2 will crash
    if (count < 2) {
        return -1;
    }

    color.r = cmd->data.draw.r;
    color.g = cmd->data.draw.g;
    color.b = cmd->data.draw.b;
    color.a = cmd->data.draw.a;

    vertexBuffer = WIIU_AllocRenderData(data, (GX2RBuffer) {
        .flags =
            GX2R_RESOURCE_BIND_VERTEX_BUFFER |
            GX2R_RESOURCE_USAGE_CPU_WRITE,
        .elemSize = sizeof(SDL_VertexSolid),
        .elemCount = (count - 1) * 2,
    });

    if (!vertexBuffer) {
        return -1;
    }

    cmd->data.draw.first = (size_t)vertexBuffer;
    cmd->data.draw.count = (count - 1) * 2;

    vertices = GX2RLockBufferEx(vertexBuffer, 0);

    for (int i = 0; i < count - 1; ++i) {
        vertices[i * 2].position.x = points[i].x;
        vertices[i * 2].position.y = points[i].y;
        vertices[i * 2].color = color;

        vertices[i * 2 + 1].position.x = points[i + 1].x;
        vertices[i * 2 + 1].position.y = points[i + 1].y;
        vertices[i * 2 + 1].color = color;
    }

    GX2RUnlockBufferEx(vertexBuffer, 0);

    return 0;
}

int WIIU_SDL_QueueGeometry(SDL_Renderer * renderer, SDL_RenderCommand * cmd, SDL_Texture * texture,
        const float * xy, int xy_stride, const SDL_Color * color, int color_stride, const float * uv, int uv_stride,
        int num_vertices, const void * indices, int num_indices, int size_indices,
        float scale_x, float scale_y)
{
    WIIU_RenderData *data = (WIIU_RenderData *) renderer->driverdata;
    GX2RBuffer *vertexBuffer;

    int count = indices ? num_indices : num_vertices;
    // discard empty draws or GX2 will crash
    if (count < 1) {
        return -1;
    }

    cmd->data.draw.count = count;

    size_indices = indices ? size_indices : 0;

    if (texture) {
        SDL_Vertex *vertices;
        vertexBuffer = WIIU_AllocRenderData(data, (GX2RBuffer) {
            .flags =
                GX2R_RESOURCE_BIND_VERTEX_BUFFER |
                GX2R_RESOURCE_USAGE_CPU_WRITE,
            .elemSize = sizeof(SDL_Vertex),
            .elemCount = count,
        });

        if (!vertexBuffer) {
            return -1;
        }

        vertices = GX2RLockBufferEx(vertexBuffer, 0);

        for (int i = 0; i < count; i++) {
            int j;
            float *xy_;
            float *uv_;
            SDL_Color col_;
            if (size_indices == 4) {
                j = ((const Uint32 *)indices)[i];
            } else if (size_indices == 2) {
                j = ((const Uint16 *)indices)[i];
            } else if (size_indices == 1) {
                j = ((const Uint8 *)indices)[i];
            } else {
                j = i;
            }

            xy_ = (float *)((char*)xy + j * xy_stride);
            col_ = *(SDL_Color *)((char*)color + j * color_stride);
            uv_ = (float *)((char*)uv + j * uv_stride);

            vertices[i].position.x = xy_[0] * scale_x;
            vertices[i].position.y = xy_[1] * scale_y;
            vertices[i].tex_coord.x = uv_[0];
            vertices[i].tex_coord.y = uv_[1];
            vertices[i].color = col_;
        }

        GX2RUnlockBufferEx(vertexBuffer, 0);

        cmd->data.draw.first = (size_t)vertexBuffer;
    } else {
        SDL_VertexSolid *vertices;
        vertexBuffer = WIIU_AllocRenderData(data, (GX2RBuffer) {
            .flags =
                GX2R_RESOURCE_BIND_VERTEX_BUFFER |
                GX2R_RESOURCE_USAGE_CPU_WRITE,
            .elemSize = sizeof(SDL_VertexSolid),
            .elemCount = count,
        });

        if (!vertexBuffer) {
            return -1;
        }

        vertices = GX2RLockBufferEx(vertexBuffer, 0);

        for (int i = 0; i < count; i++) {
            int j;
            float *xy_;
            SDL_Color col_;
            if (size_indices == 4) {
                j = ((const Uint32 *)indices)[i];
            } else if (size_indices == 2) {
                j = ((const Uint16 *)indices)[i];
            } else if (size_indices == 1) {
                j = ((const Uint8 *)indices)[i];
            } else {
                j = i;
            }

            xy_ = (float *)((char*)xy + j * xy_stride);
            col_ = *(SDL_Color *)((char*)color + j * color_stride);

            vertices[i].position.x = xy_[0] * scale_x;
            vertices[i].position.y = xy_[1] * scale_y;
            vertices[i].color = col_;
        }

        GX2RUnlockBufferEx(vertexBuffer, 0);

        cmd->data.draw.first = (size_t)vertexBuffer;
    }

    return 0;
}

static int WIIU_SDL_RenderClear(SDL_Renderer * renderer, SDL_RenderCommand * cmd)
{
    WIIU_RenderData *data = (WIIU_RenderData*) renderer->driverdata;
    SDL_Texture *target = WIIU_GetRenderTarget(renderer);
    WIIU_TextureData *tdata = (WIIU_TextureData*) target->driverdata;

    GX2ClearColor(&tdata->cbuf,
                  (float) cmd->data.color.r / 255.0f,
                  (float) cmd->data.color.g / 255.0f,
                  (float) cmd->data.color.b / 255.0f,
                  (float) cmd->data.color.a / 255.0f);

    /* Restore SDL context state */
    GX2SetContextState(data->ctx);

    return 0;
}

static int WIIU_SDL_SetDrawState(WIIU_RenderData * data, const SDL_RenderCommand * cmd)
{
    SDL_bool matrixUpdated = SDL_FALSE;
    SDL_bool shaderUpdated = SDL_FALSE;
    const SDL_BlendMode blendMode = cmd->data.draw.blend;
    WIIU_ShaderType shader = cmd->data.draw.texture ? SHADER_TEXTURE : SHADER_COLOR;
    WiiU_ShaderGroup* shaderGroup = WIIU_SDL_GetShaderGroup(shader);
    SDL_Texture *texture = cmd->data.draw.texture;

    if (data->drawState.viewportDirty) {
        const SDL_Rect *viewport = &data->drawState.viewport;

        GX2SetViewport(viewport->x, viewport->y, viewport->w, viewport->h, 0.0f, 1.0f);

        if (viewport->w && viewport->h) {
            data->drawState.projectionMatrix[0][0] = 2.0f / viewport->w;
            data->drawState.projectionMatrix[1][1] = -2.0f / viewport->h;
            matrixUpdated = SDL_TRUE;
        }

        data->drawState.viewportDirty = SDL_FALSE;
    }

    if (data->drawState.cliprectDirty) {
        SDL_Rect scissor;
        const SDL_Rect *viewport = &data->drawState.viewport;
        const SDL_Rect *rect = &data->drawState.cliprect;

        /* Make sure scissor is never larger than the colorbuffer to prevent memory corruption.
           In this case just clamp it to the viewport. */
        scissor.x = SDL_clamp(viewport->x + rect->x, viewport->x, viewport->x + viewport->w);
        scissor.y = SDL_clamp(viewport->y + rect->y, viewport->y, viewport->y + viewport->h);
        scissor.w = SDL_clamp(rect->w, 0, (viewport->x + viewport->w) - scissor.x);
        scissor.h = SDL_clamp(rect->h, 0, (viewport->y + viewport->h) - scissor.y);

        GX2SetScissor(scissor.x, scissor.y, scissor.w, scissor.h);

        data->drawState.cliprectDirty = SDL_FALSE;
    }

    if (blendMode != data->drawState.blendMode) {
        if (blendMode == SDL_BLENDMODE_NONE) {
            GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x00, FALSE, TRUE);
        } else {
            GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);
            GX2SetBlendControl(GX2_RENDER_TARGET_0,
                WIIU_SDL_GetBlendMode(SDL_GetBlendModeSrcColorFactor(blendMode)),
                WIIU_SDL_GetBlendMode(SDL_GetBlendModeDstColorFactor(blendMode)),
                WIIU_SDL_GetBlendCombineMode(SDL_GetBlendModeColorOperation(blendMode)),
                TRUE,
                WIIU_SDL_GetBlendMode(SDL_GetBlendModeSrcAlphaFactor(blendMode)),
                WIIU_SDL_GetBlendMode(SDL_GetBlendModeDstAlphaFactor(blendMode)),
                WIIU_SDL_GetBlendCombineMode(SDL_GetBlendModeAlphaOperation(blendMode)));
        }

        data->drawState.blendMode = blendMode;
    }

    if (shader != data->drawState.shader) {
        WIIU_SDL_SelectShader(shader);
        shaderUpdated = SDL_TRUE;
        data->drawState.shader = shader;
    }

    if (shaderUpdated || matrixUpdated) {
        GX2SetVertexUniformReg(shaderGroup->vertexShader->uniformVars[0].offset, 16, data->drawState.projectionMatrix);
    }

    if (texture != data->drawState.texture) {
        if (texture) {
            WIIU_TextureData *tdata = (WIIU_TextureData*) texture->driverdata;
            uint32_t location = shaderGroup->pixelShader->samplerVars[0].location;
            GX2SetPixelTexture(&tdata->texture, location);
            GX2SetPixelSampler(&tdata->sampler, location);
        }

        data->drawState.texture = texture;
    }

    if (texture) {
        WIIU_TextureData *tdata = (WIIU_TextureData*) texture->driverdata;
        WIIU_TextureMarkUsed(data, tdata);
    }

    return 0;
}

int WIIU_SDL_RunCommandQueue(SDL_Renderer * renderer, SDL_RenderCommand *cmd, void *vertices, size_t vertsize)
{
    WIIU_RenderData* data = (WIIU_RenderData*) renderer->driverdata;
    WIIU_VideoData *videodata = (WIIU_VideoData *) SDL_GetVideoDevice()->driverdata;

    /* The command queue is still ran, even with DONT_DRAW_WHILE_HIDDEN
       So check manually for foreground here */
    if (!videodata->hasForeground) {
        return 0;
    }

    /* make sure we're using the correct renderer ctx */
    GX2SetContextState(data->ctx);

    data->drawState.target = renderer->target;
    if (!data->drawState.target) {
        int w, h;
        SDL_GL_GetDrawableSize(renderer->window, &w, &h);
        if ((w != data->drawState.drawableWidth) || (h != data->drawState.drawableHeight)) {
            /* if the window dimensions changed, invalidate the current viewport, etc. */
            data->drawState.viewportDirty = SDL_TRUE;
            data->drawState.cliprectDirty = SDL_TRUE;
            data->drawState.drawableWidth = w;
            data->drawState.drawableHeight = h;
        }
    }

    while (cmd) {
        switch (cmd->command) {

            case SDL_RENDERCMD_SETVIEWPORT: {
                SDL_Rect *viewport = &data->drawState.viewport;
                if (SDL_memcmp(viewport, &cmd->data.viewport.rect, sizeof (SDL_Rect)) != 0) {
                    SDL_memcpy(viewport, &cmd->data.viewport.rect, sizeof (SDL_Rect));
                    data->drawState.viewportDirty = SDL_TRUE;

                    if (!data->drawState.cliprectEnabled) {
                        /* If the clip rect is disabled, then the scissor rect should be the whole viewport */
                        data->drawState.cliprect.x = 0;
                        data->drawState.cliprect.y = 0;
                        data->drawState.cliprect.w = viewport->w;
                        data->drawState.cliprect.h = viewport->h;
                        data->drawState.cliprectDirty = SDL_TRUE;
                    }
                }
                break;
            }

            case SDL_RENDERCMD_SETCLIPRECT: {
                SDL_Rect noclip;
                const SDL_Rect *viewport = &data->drawState.viewport;
                const SDL_Rect *rect = &cmd->data.cliprect.rect;
                if (data->drawState.cliprectEnabled != cmd->data.cliprect.enabled) {
                    data->drawState.cliprectEnabled = cmd->data.cliprect.enabled;
                    data->drawState.cliprectDirty = SDL_TRUE;
                }

                if (!data->drawState.cliprectEnabled) {
                    /* If the clip rect is disabled, then the scissor rect should be the whole viewport */
                    noclip.x = 0;
                    noclip.y = 0;
                    noclip.w = viewport->w;
                    noclip.h = viewport->h;
                    rect = &noclip;
                }

                if (SDL_memcmp(&data->drawState.cliprect, rect, sizeof (SDL_Rect)) != 0) {
                    SDL_memcpy(&data->drawState.cliprect, rect, sizeof (SDL_Rect));
                    data->drawState.cliprectDirty = SDL_TRUE;
                }
                break;
            }

            case SDL_RENDERCMD_SETDRAWCOLOR:
                break;

            case SDL_RENDERCMD_CLEAR: {
                WIIU_SDL_RenderClear(renderer, cmd);
                break;
            }

            case SDL_RENDERCMD_DRAW_POINTS: {
                GX2RBuffer *vertexBuffer = (GX2RBuffer *) cmd->data.draw.first;
                if (WIIU_SDL_SetDrawState(data, cmd) == 0) {
                    GX2RSetAttributeBuffer(vertexBuffer, 0, vertexBuffer->elemSize, 0);
                    GX2DrawEx(GX2_PRIMITIVE_MODE_POINTS, cmd->data.draw.count, 0, 1);
                }
                break;
            }

            case SDL_RENDERCMD_DRAW_LINES: {
                GX2RBuffer *vertexBuffer = (GX2RBuffer *) cmd->data.draw.first;
                if (WIIU_SDL_SetDrawState(data, cmd) == 0) {
                    GX2RSetAttributeBuffer(vertexBuffer, 0, vertexBuffer->elemSize, 0);
                    GX2DrawEx(GX2_PRIMITIVE_MODE_LINE_STRIP, cmd->data.draw.count, 0, 1);
                }
                break;
            }

            /* unused (will be handled by geometry) */
            case SDL_RENDERCMD_FILL_RECTS:
            case SDL_RENDERCMD_COPY:
            case SDL_RENDERCMD_COPY_EX:
                break;

            case SDL_RENDERCMD_GEOMETRY: {
                GX2RBuffer *vertexBuffer = (GX2RBuffer *) cmd->data.draw.first;
                if (WIIU_SDL_SetDrawState(data, cmd) == 0) {
                    GX2RSetAttributeBuffer(vertexBuffer, 0, vertexBuffer->elemSize, 0);
                    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, cmd->data.draw.count, 0, 1);
                }
            }

            case SDL_RENDERCMD_NO_OP:
                break;
        }

        cmd = cmd->next;
    }

    return 0;
}

#endif /* SDL_VIDEO_RENDER_WIIU */
