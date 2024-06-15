/*
	SDL - Simple DirectMedia Layer
	Copyright (C) 1997-2006 Sam Lantinga

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public
	License along with this library; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

	Tantric, 2009
*/
#include "SDL_config.h"

// Standard includes.
#include <math.h>

// SDL internal includes.
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "SDL_timer.h"
#include "SDL_thread.h"

// SDL ogc specifics.
#include <gccore.h>
#include <ogcsys.h>
#include <opengx.h>
#include <malloc.h>
#include <wiiuse/wpad.h>
#include "SDL_ogc_video.h"

#ifdef __wii__
#include "../wii/SDL_wiievents_c.h"
#endif

#ifdef __gamecube__
#include "../gamecube/SDL_gamecube_events_c.h"
#endif


#include <ogc/machine/processor.h>

static const char	OGCVID_DRIVER_NAME[] = "ogc-video";
static lwp_t videothread = LWP_THREAD_NULL;
static SDL_mutex *videomutex = NULL;
static SDL_cond *videocond = NULL;
static ogcVideo *current = NULL;

int vresx=0, vresy=0;

/*** SDL ***/
static SDL_Rect mode_320, mode_640;
#ifdef __wii__
static SDL_Rect mode_848;
#endif

static SDL_Rect* modes_descending[] =
{
#ifdef __wii__
	&mode_848,
#endif
	&mode_640,
	&mode_320,
	NULL
};


/*** 2D Video ***/
#define HASPECT 			320
#define VASPECT 			240

unsigned char *xfb = NULL;
GXRModeObj* vmode = 0;
static int quit_flip_thread = 0;
static GXTexObj texobj_a, texobj_b;
static GXTlutObj texpalette_a, texpalette_b;

/*** GX ***/
#define DEFAULT_FIFO_SIZE 256 * 1024
static unsigned char gp_fifo[DEFAULT_FIFO_SIZE] __attribute__((aligned(32)));

/* New texture based scaler */
typedef struct tagcamera
{
	guVector pos;
	guVector up;
	guVector view;
}
camera;

/*** Square Matrix
     This structure controls the size of the image on the screen.
	 Think of the output as a -80 x 80 by -60 x 60 graph.
***/
static s16 square[] ATTRIBUTE_ALIGN (32) =
{
  /*
   * X,   Y,  Z
   * Values set are for roughly 4:3 aspect
   */
	-HASPECT,  VASPECT, 0,	// 0
	 HASPECT,  VASPECT, 0,	// 1
	 HASPECT, -VASPECT, 0,	// 2
	-HASPECT, -VASPECT, 0	// 3
};

static const f32 tex_pos[] ATTRIBUTE_ALIGN(32) = {
	0.0, 0.0,
	1.0, 0.0,
	1.0, 1.0,
	0.0, 1.0,
};

static camera cam = {
	{0.0F, 0.0F, 0.0F},
	{0.0F, 0.5F, 0.0F},
	{0.0F, 0.0F, -0.5F}
};

/****************************************************************************
 * Scaler Support Functions
 ***************************************************************************/
static int currentwidth;
static int currentheight;
static int currentbpp;

static void
draw_init(void *palette, void *tex)
{
	Mtx m, mv, view;

	GX_ClearVtxDesc ();
	GX_SetVtxDesc (GX_VA_POS, GX_INDEX8);
	GX_SetVtxDesc (GX_VA_TEX0, GX_INDEX8);

	GX_SetVtxAttrFmt (GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S16, 0);
	GX_SetVtxAttrFmt (GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	GX_SetArray (GX_VA_POS, square, 3 * sizeof (s16));
	GX_SetArray (GX_VA_TEX0, (void*)tex_pos, 2 * sizeof (f32));
	GX_SetNumTexGens (1);
	GX_SetNumChans (0);

	GX_SetTexCoordGen (GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

	GX_SetTevOp (GX_TEVSTAGE0, GX_REPLACE);
	GX_SetTevOrder (GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);

	memset(&view, 0, sizeof (Mtx));
	guLookAt(view, &cam.pos, &cam.up, &cam.view);
	guMtxIdentity(m);
	guMtxTransApply(m, m, 0, 0, -100);
	guMtxConcat(view, m, mv);
	GX_LoadPosMtxImm(mv, GX_PNMTX0);

	GX_InvVtxCache ();	// update vertex cache

	if (currentbpp == 8) {
		GX_InitTlutObj(&texpalette_a, palette, GX_TL_IA8, 256);
		GX_InitTlutObj(&texpalette_b, (Uint16*)palette+256, GX_TL_IA8, 256);
		DCStoreRange(palette, sizeof(512*sizeof(Uint16)));
		GX_LoadTlut(&texpalette_a, GX_TLUT0);
		GX_LoadTlut(&texpalette_b, GX_TLUT1);

		GX_InitTexObjCI(&texobj_a, tex, currentwidth, currentheight, GX_TF_CI8, GX_CLAMP, GX_CLAMP, 0, GX_TLUT0);
		GX_InitTexObjCI(&texobj_b, tex, currentwidth, currentheight, GX_TF_CI8, GX_CLAMP, GX_CLAMP, 0, GX_TLUT1);
		GX_LoadTexObj(&texobj_b, GX_TEXMAP1);

		// Setup TEV to combine Red+Green and Blue paletted images
		GX_SetTevColor(GX_TEVREG0, (GXColor){255, 255, 0, 0});
		GX_SetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_RED, GX_CH_ALPHA, GX_CH_BLUE, GX_CH_ALPHA);
		GX_SetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_ALPHA, GX_CH_ALPHA, GX_CH_BLUE, GX_CH_ALPHA);
		// first stage = red and green
		GX_SetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP1);
		GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C0, GX_CC_ZERO);
		// second stage = add blue (and opaque alpha)
		GX_SetTevOp(GX_TEVSTAGE1, GX_BLEND);
		GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP1, GX_COLORNULL);
		GX_SetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP0, GX_TEV_SWAP2);
		GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
		GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);

		GX_SetNumTevStages(2);
	}
	else if (currentbpp == 16)
		GX_InitTexObj(&texobj_a, tex, currentwidth, currentheight, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
	else
		GX_InitTexObj(&texobj_a, tex, currentwidth, currentheight, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);

	GX_LoadTexObj(&texobj_a, GX_TEXMAP0);	// load texture object so its ready to use
}

static inline void
draw_vert (u8 index)
{
	GX_Position1x8 (index);
	GX_TexCoord1x8 (index);
}

static inline void
draw_square ()
{
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	draw_vert(0);
	draw_vert(1);
	draw_vert(2);
	draw_vert(3);
	GX_End();
}

static void * flip_thread (void *arg)
{
	u32 *tex = (u32*)arg;

	GX_SetCurrentGXThread();

	// clear EFB
	GX_CopyDisp(xfb, GX_TRUE);

	SDL_mutexP(videomutex);

	while(!quit_flip_thread)
	{
		// update texture
		DCStoreRange((void*)tex[0], tex[1]);
		// clear texture objects
		GX_InvalidateTexAll();
		draw_square(); // render textured quad

		VIDEO_WaitVSync();
		GX_CopyDisp(xfb, GX_FALSE);

		GX_DrawDone();

		SDL_CondWait(videocond, videomutex);
	}
	SDL_mutexV(videomutex);

	return NULL;
}

static void
SetupGX()
{
	Mtx44 p;
	int df = 1; // deflicker on/off

	GX_SetCurrentGXThread();
	GX_SetViewport (0, 0, vmode->fbWidth, vmode->efbHeight, 0, 1);
	GX_SetDispCopyYScale ((f32) vmode->xfbHeight / (f32) vmode->efbHeight);
	GX_SetScissor (0, 0, vmode->fbWidth, vmode->efbHeight);

	GX_SetDispCopySrc(0, 0, vmode->fbWidth, vmode->efbHeight);
	GX_SetDispCopyDst(vmode->fbWidth, vmode->xfbHeight);
	GX_SetCopyFilter (vmode->aa, vmode->sample_pattern, (df == 1) ? GX_TRUE : GX_FALSE, vmode->vfilter);

	GX_SetFieldMode (vmode->field_rendering, ((vmode->viHeight == 2 * vmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));
	GX_SetPixelFmt (GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetDispCopyGamma (GX_GM_1_0);
	GX_SetCullMode (GX_CULL_NONE);
	GX_SetBlendMode(GX_BM_NONE,GX_BL_DSTALPHA,GX_BL_INVSRCALPHA,GX_LO_CLEAR);

	GX_SetZMode (GX_FALSE, GX_LEQUAL, GX_TRUE);
	GX_SetColorUpdate (GX_TRUE);
	GX_SetAlphaUpdate(GX_FALSE);

	guOrtho(p, VASPECT, -VASPECT, -HASPECT, HASPECT, 100, 1000); // matrix, t, b, l, r, n, f
	GX_LoadProjectionMtx (p, GX_ORTHOGRAPHIC);
	GX_Flush();
}

static void
StartVideoThread(void *args)
{
	if(videothread == LWP_THREAD_NULL)
	{
		quit_flip_thread = 0;
		LWP_CreateThread(&videothread, flip_thread, args, NULL, 0, 68);
	}
}

void OGC_VideoStart(ogcVideo *private, Uint32 flags)
{
	if (private==NULL) {
		if (current==NULL)
			return;
		private = current;
	}

	SetupGX();
	draw_init(private->palette, private->texturemem);
	if (!(flags & SDL_OPENGL)) {
		StartVideoThread(&private->texturemem);
	}
#ifdef __wii__
	WPAD_SetVRes(WPAD_CHAN_0, vresx+vresx/4, vresy+vresy/4);
#endif
	current = private;
}

void OGC_VideoStop()
{
	if(videothread == LWP_THREAD_NULL)
		return;

	SDL_LockMutex(videomutex);
	quit_flip_thread = 1;
	SDL_CondSignal(videocond);
	SDL_UnlockMutex(videomutex);

	LWP_JoinThread(videothread, NULL);
	videothread = LWP_THREAD_NULL;
}

static int OGC_VideoInit(_THIS, SDL_PixelFormat *vformat)
{
	// Set up the modes.
#ifdef __wii__
	mode_848.w = 848;
	mode_848.h = 480;
#endif
	mode_640.w = 640;
	mode_640.h = 480;
	mode_320.w = 320;
	mode_320.h = 240;

	// Set the current format.
	vformat->BitsPerPixel	= 16;
	vformat->BytesPerPixel	= 2;

	this->hidden->buffer = NULL;
	this->hidden->texturemem = NULL;
	this->hidden->width = 0;
	this->hidden->height = 0;
	this->hidden->pitch = 0;

	this->gl_config.driver_loaded = 1;
	this->gl_config.driver_path[0] = '\0';

	/* We're done! */
	return 0;
}

static SDL_Rect **OGC_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags)
{
	return modes_descending;
}

static SDL_Surface *OGC_SetVideoMode(_THIS, SDL_Surface *current,
								   int width, int height, int bpp, Uint32 flags)
{
	SDL_Rect* 		mode;
	size_t			bytes_per_pixel;
	Uint32			r_mask = 0;
	Uint32			b_mask = 0;
	Uint32			g_mask = 0;

	// Find a mode big enough to store the requested resolution
	mode = modes_descending[0];
	while (mode)
	{
		if (mode->w == width && mode->h == height)
			break;
		else
			++mode;
	}

	// Didn't find a mode?
	if (!mode)
	{
		SDL_SetError("Display mode (%dx%d) is unsupported.",
			width, height);
		return NULL;
	}

	if(bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
	{
		SDL_SetError("Resolution (%d bpp) is unsupported (8/16/24/32 bpp only).",
			bpp);
		return NULL;
	}

	bytes_per_pixel = bpp / 8;

	OGC_VideoStop();

	free(this->hidden->buffer);
	free(this->hidden->texturemem);

	// Allocate the new buffer.
	this->hidden->buffer = memalign(32, width * height * bytes_per_pixel);
	if (!this->hidden->buffer )
	{
		this->hidden->texturemem = NULL;
		SDL_SetError("Couldn't allocate buffer for requested mode");
		return(NULL);
	}

	// Allocate texture memory
	if (bytes_per_pixel > 2)
		this->hidden->texturemem_size = width * height * 4;
	else
		this->hidden->texturemem_size = width * height * bytes_per_pixel;

	this->hidden->texturemem = memalign(32, this->hidden->texturemem_size);
	if (this->hidden->texturemem == NULL)
	{
		free(this->hidden->buffer);
		this->hidden->buffer = NULL;
		SDL_SetError("Couldn't allocate memory for texture");
		return NULL;
	}

	// Allocate the new pixel format for the screen
	if (!SDL_ReallocFormat(current, bpp, r_mask, g_mask, b_mask, 0))
	{
		free(this->hidden->buffer);
		this->hidden->buffer = NULL;
		free(this->hidden->texturemem);
		this->hidden->texturemem = NULL;

		SDL_UnlockMutex(videomutex);
		SDL_SetError("Couldn't allocate new pixel format for requested mode");
		return NULL;
	}

	// Clear the buffer
	SDL_memset(this->hidden->buffer, 0, width * height * bytes_per_pixel);
	SDL_memset(this->hidden->texturemem, 0, this->hidden->texturemem_size);

	// Set up the new mode framebuffer
	current->flags = flags & (SDL_FULLSCREEN | SDL_HWPALETTE | SDL_NOFRAME | SDL_OPENGL);
	// Our surface is always double buffered
	current->flags |= SDL_PREALLOC | SDL_DOUBLEBUF;
	current->w = width;
	current->h = height;
	current->pitch = current->w * bytes_per_pixel;
	current->pixels = this->hidden->buffer;

	/* Set the hidden data */
	this->hidden->width = current->w;
	this->hidden->height = current->h;
	this->hidden->pitch = current->w * (bytes_per_pixel > 2 ? 4 : bytes_per_pixel);

	currentwidth = current->w;
	currentheight = current->h;
	currentbpp = bpp;
	vresx = currentwidth;
	vresy = currentheight;

	OGC_VideoStart(this->hidden, flags);

	return current;
}

/* We don't actually allow hardware surfaces other than the main one */
static int OGC_AllocHWSurface(_THIS, SDL_Surface *surface)
{
	return(-1);
}

static void OGC_FreeHWSurface(_THIS, SDL_Surface *surface)
{
	return;
}

static int OGC_LockHWSurface(_THIS, SDL_Surface *surface)
{
	return(0);
}

static void OGC_UnlockHWSurface(_THIS, SDL_Surface *surface)
{
	return;
}

static inline void Set_RGBAPixel(_THIS, int x, int y, u32 color)
{
	u8 *truc = this->hidden->texturemem;
	int width = this->hidden->width;
	u32 offset;

	offset = (((y >> 2) << 4) * width) + ((x >> 2) << 6) + ((((y & 3) << 2) + (x & 3)) << 1);

	*(truc + offset) = color;
	*(truc + offset + 1) = color >> 24;
	*(truc + offset + 32) = color >> 16;
	*(truc + offset + 33) = color >> 8;
}

static inline void Set_RGB565Pixel(_THIS, int x, int y, u16 color)
{
	u8 *truc = this->hidden->texturemem;
	int width = this->hidden->width;
	u32 offset;

	offset = (((y >> 2) << 3) * width) + ((x >> 2) << 5) + ((((y & 3) << 2) + (x & 3)) << 1);

	*(truc + offset) = color >> 8;
	*(truc + offset + 1) = color;
}

static inline void Set_PalPixel(_THIS, int x, int y, u8 color)
{
	u8 *truc = this->hidden->texturemem;
	int width = this->hidden->pitch;
	u32 offset;

	offset = ((y & ~3) * width) + ((x & ~7) << 2) + ((y & 3) << 3) + (x & 7);

	truc[offset] = color;
}

static void UpdateRect_8(_THIS, SDL_Rect *rect)
{
	u8 *src;
	u8 color;
	int i, j;

	for (i = 0; i < rect->h; i++)
	{
		src = (this->hidden->buffer + (this->hidden->width * (i + rect->y)) + rect->x);
		for (j = 0; j < rect->w; j++)
		{
			color = src[j];
			Set_PalPixel(this, rect->x + j, rect->y + i, color);
		}
	}
}

static void UpdateRect_16(_THIS, SDL_Rect *rect)
{
	u8 *src;
	u8 *ptr;
	u16 color;
	int i, j;
	for (i = 0; i < rect->h; i++)
	{
		src = (this->hidden->buffer + (this->hidden->width * 2 * (i + rect->y)) + (rect->x * 2));
		for (j = 0; j < rect->w; j++)
		{
			ptr = src + (j * 2);
			color = (ptr[0] << 8) | ptr[1];
			Set_RGB565Pixel(this, rect->x + j, rect->y + i, color);
		}
	}
}

static void UpdateRect_24(_THIS, SDL_Rect *rect)
{
	u8 *src;
	u8 *ptr;
	u32 color;
	int i, j;
	for (i = 0; i < rect->h; i++)
	{
		src = (this->hidden->buffer + (this->hidden->width * 3 * (i + rect->y)) + (rect->x * 3));
		for (j = 0; j < rect->w; j++)
		{
			ptr = src + (j * 3);
			color = (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | 0xff;
			Set_RGBAPixel(this, rect->x + j, rect->y + i, color);
		}
	}
}

static void UpdateRect_32(_THIS, SDL_Rect *rect)
{
	u8 *src;
	u8 *ptr;
	u32 color;
	int i, j;
	for (i = 0; i < rect->h; i++)
	{
		src = (this->hidden->buffer + (this->hidden->width * 4 * (i + rect->y)) + (rect->x * 4));
		for (j = 0; j < rect->w; j++)
		{
			ptr = src + (j * 4);
			color = (ptr[1] << 24) | (ptr[2] << 16) | (ptr[3] << 8) | ptr[0];
			Set_RGBAPixel(this, rect->x + j, rect->y + i, color);
		}
	}
}

static void flipHWSurface_16_16(_THIS, const SDL_Surface* const surface)
{
	int h, w;
	long long int *dst = (long long int *) this->hidden->texturemem;
	long long int *src1 = (long long int *) this->hidden->buffer;
	long long int *src2 = (long long int *) (this->hidden->buffer + (this->hidden->pitch * 1));
	long long int *src3 = (long long int *) (this->hidden->buffer + (this->hidden->pitch * 2));
	long long int *src4 = (long long int *) (this->hidden->buffer + (this->hidden->pitch * 3));
	int rowpitch = (this->hidden->pitch >> 3) * 3;

	SDL_mutexP(videomutex);
	for (h = 0; h < this->hidden->height; h += 4)
	{
		for (w = 0; w < this->hidden->pitch; w += 8)
		{
			*dst++ = *src1++;
			*dst++ = *src2++;
			*dst++ = *src3++;
			*dst++ = *src4++;
		}

		src1 = src4;
		src2 += rowpitch;
		src3 += rowpitch;
		src4 += rowpitch;
	}
	SDL_CondSignal(videocond);
	SDL_mutexV(videomutex);
}

static void OGC_UpdateRect(_THIS, SDL_Rect *rect)
{
	const SDL_Surface* const screen = this->screen;

	switch(screen->format->BytesPerPixel) {
	case 1:
		UpdateRect_8(this, rect);
		break;
	case 2:
		UpdateRect_16(this, rect);
		break;
	case 3:
		UpdateRect_24(this, rect);
		break;
	case 4:
		UpdateRect_32(this, rect);
		break;
	default:
		fprintf(stderr, "Invalid BPP %d\n", screen->format->BytesPerPixel);
		break;
	}
}

static void OGC_UpdateRects(_THIS, int numrects, SDL_Rect *rects)
{
	int i;

	// note that this function doesn't lock - we don't care if this isn't
	// rendered now, that's what Flip is for

	for (i = 0; i < numrects; i++)
	{
		OGC_UpdateRect(this, rects+i);
	}

	SDL_CondSignal(videocond);
}

static void flipHWSurface_24_16(_THIS, SDL_Surface *surface)
{
	SDL_Rect screen_rect = {0, 0, this->hidden->width, this->hidden->height};
	OGC_UpdateRects(this, 1, &screen_rect);
}

static void flipHWSurface_32_16(_THIS, SDL_Surface *surface)
{
	SDL_Rect screen_rect = {0, 0, this->hidden->width, this->hidden->height};
	OGC_UpdateRects(this, 1, &screen_rect);
}

static int OGC_FlipHWSurface(_THIS, SDL_Surface *surface)
{
	switch(surface->format->BytesPerPixel)
	{
		case 1:
		case 2:
			// 8 and 16 bit use the same tile format
			flipHWSurface_16_16(this, surface);
			break;
		case 3:
			flipHWSurface_24_16(this, surface);
			break;
		case 4:
			flipHWSurface_32_16(this, surface);
			break;
		default:
			return -1;
	}
	return 0;
}

static int OGC_SetColors(_THIS, int first_color, int color_count, SDL_Color *colors)
{
	const int last_color = first_color + color_count;
	Uint16* const palette = this->hidden->palette;
	int     component;

	SDL_LockMutex(videomutex);

	/* Build the RGB24 palette. */
	for (component = first_color; component != last_color; ++component, ++colors)
	{
		palette[component] = (colors->g << 8) | colors->r;
		palette[component+256] = colors->b;
	}

	DCStoreRangeNoSync(palette+first_color, color_count*sizeof(Uint16));
	DCStoreRange(palette+first_color+256, color_count*sizeof(Uint16));
	GX_LoadTlut(&texpalette_a, GX_TLUT0);
	GX_LoadTlut(&texpalette_b, GX_TLUT1);
	GX_LoadTexObj(&texobj_a, GX_TEXMAP0);
	GX_LoadTexObj(&texobj_b, GX_TEXMAP1);

	SDL_UnlockMutex(videomutex);

	return(1);
}

static void OGC_VideoQuit(_THIS)
{
	OGC_VideoStop();
	GX_AbortFrame();
	GX_Flush();

	current = NULL;

	VIDEO_SetBlack(TRUE);
	VIDEO_Flush();

	free(this->hidden->buffer);
	this->hidden->buffer = NULL;
	free(this->hidden->texturemem);
	this->hidden->texturemem = NULL;
}

static void OGC_DeleteDevice(SDL_VideoDevice *device)
{
	free(device->hidden);
	SDL_free(device);

	SDL_DestroyCond(videocond);
	videocond = 0;
	SDL_DestroyMutex(videomutex);
	videomutex=0;
}

int SDL_OGC_GL_LoadLibrary(_THIS, const char *path)
{
	return 0;
}

void *SDL_OGC_GL_GetProcAddress(_THIS, const char *proc)
{
	void *ptr = ogx_get_proc_address(proc);
	if (!ptr) {
		fprintf(stderr, "GetProcAddress %s\n", proc);
	}
	return ptr;
}

int SDL_OGC_GL_MakeCurrent(_THIS)
{
	ogx_initialize();
	return 0;
}

void SDL_OGC_GL_SwapBuffers(_THIS)
{
	VIDEO_WaitVSync();
	GX_CopyDisp(xfb, GX_FALSE);

	GX_DrawDone();
}

static SDL_VideoDevice *OGC_CreateDevice(int devindex)
{
	SDL_VideoDevice *device;

	/* Initialize all variables that we clean on shutdown */
	device = (SDL_VideoDevice *)SDL_malloc(sizeof(SDL_VideoDevice));
	if ( device ) {
		SDL_memset(device, 0, (sizeof *device));
		device->hidden = (struct SDL_PrivateVideoData *)
			memalign(32, sizeof(struct SDL_PrivateVideoData));
	}
	if ( (device == NULL) || (device->hidden == NULL) ) {
		SDL_OutOfMemory();
		if ( device ) {
			SDL_free(device);
		}
		return(0);
	}
	SDL_memset(device->hidden, 0, (sizeof *device->hidden));

	videomutex = SDL_CreateMutex();
	videocond = SDL_CreateCond();

	/* Set the function pointers */
	device->VideoInit = OGC_VideoInit;
	device->ListModes = OGC_ListModes;
	device->SetVideoMode = OGC_SetVideoMode;
	device->SetColors = OGC_SetColors;
	device->UpdateRects = OGC_UpdateRects;
	device->VideoQuit = OGC_VideoQuit;
	device->AllocHWSurface = OGC_AllocHWSurface;
	device->LockHWSurface = OGC_LockHWSurface;
	device->UnlockHWSurface = OGC_UnlockHWSurface;
	device->FlipHWSurface = OGC_FlipHWSurface;
	device->FreeHWSurface = OGC_FreeHWSurface;
#ifdef __wii__
	device->InitOSKeymap = WII_InitOSKeymap;
	device->PumpEvents = WII_PumpEvents;
#endif
#ifdef __gamecube__
	device->InitOSKeymap = GAMECUBE_InitOSKeymap;
	device->PumpEvents = GAMECUBE_PumpEvents;
#endif
	device->input_grab = SDL_GRAB_ON;

	device->GL_LoadLibrary = SDL_OGC_GL_LoadLibrary;
	device->GL_GetProcAddress = SDL_OGC_GL_GetProcAddress;
	device->GL_MakeCurrent = SDL_OGC_GL_MakeCurrent;
	device->GL_SwapBuffers = SDL_OGC_GL_SwapBuffers;

	device->free = OGC_DeleteDevice;

	OGC_InitVideoSystem();
	return device;
}

static int OGC_Available(void)
{
	return(1);
}

VideoBootStrap OGC_bootstrap = {
	OGCVID_DRIVER_NAME, "ogc video driver",
	OGC_Available, OGC_CreateDevice
};

void
OGC_InitVideoSystem()
{
	/* Initialise the video system */
	VIDEO_Init();
	vmode = VIDEO_GetPreferredMode(NULL);

	/* Set up the video system with the chosen mode */
	if (vmode == &TVPal528IntDf)
		vmode = &TVPal576IntDfScale;

	VIDEO_Configure(vmode);

	// Allocate the video buffer
	if (xfb) free(MEM_K1_TO_K0(xfb));
	xfb = (unsigned char*) MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));

	VIDEO_ClearFrameBuffer(vmode, xfb, COLOR_BLACK);
	VIDEO_SetNextFramebuffer(xfb);

	// Show the screen.
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync(); VIDEO_WaitVSync();

	//CON_Init(xfb,20,20,vmode->fbWidth,vmode->xfbHeight,vmode->fbWidth*VI_DISPLAY_PIX_SZ);

	/*** Clear out FIFO area ***/
	memset(&gp_fifo, 0, DEFAULT_FIFO_SIZE);

	/*** Initialise GX ***/
	GX_Init(&gp_fifo, DEFAULT_FIFO_SIZE);

	GXColor background = { 0, 0, 0, 0xff };
	GX_SetCopyClear (background, GX_MAX_Z24);

	SetupGX();
}

void OGC_SetWidescreen(int wide)
{
	int width;
	if(wide) {
		width = 678;
	}
	else
		width = 640;

	vmode->viWidth = width;
	vmode->viXOrigin = (VI_MAX_WIDTH_NTSC - width) / 2;

	VIDEO_Configure (vmode);

	if (xfb)
		VIDEO_ClearFrameBuffer(vmode, xfb, COLOR_BLACK);

	VIDEO_Flush();

	VIDEO_WaitVSync(); VIDEO_WaitVSync();
}

void OGC_ChangeSquare(int xscale, int yscale, int xshift, int yshift)
{
	square[6] = square[3]  =  xscale + xshift;
	square[0] = square[9]  = -xscale + xshift;
	square[4] = square[1]  =  yscale - yshift;
	square[7] = square[10] = -yscale - yshift;
	DCFlushRange (square, 32); // update memory BEFORE the GPU accesses it!
	GX_InvVtxCache();
}
