/*
 * Copyright © 2012 Rob Clark <robclark@freedesktop.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef TEST_UTIL_2D_H_
#define TEST_UTIL_2D_H_

#include "test-util-common.h"

#include <c2d2.h>

/*****************************************************************************/

#define CHK(x) do { \
		C2D_STATUS status; \
		DEBUG_MSG(">>> %s", #x); \
		RD_WRITE_SECTION(RD_CMD, #x, strlen(#x)); \
		status = x; \
		if (status) { \
			ERROR_MSG("<<< %s: failed: %d", #x, status); \
			exit(-1); \
		} \
		DEBUG_MSG("<<< %s: succeeded", #x); \
	} while (0)

#ifndef TRUE
#  define TRUE 1
#endif
#ifndef FALSE
#  define FALSE 0
#endif
typedef int Bool;

typedef struct {
	C2D_RGB_SURFACE_DEF def;
	int width, height, pitch, id;
	void *ptr;
} Pixmap, *PixmapPtr;

static int fmt_bpp[] = {
		[C2D_COLOR_FORMAT_1] = 1,
		[C2D_COLOR_FORMAT_2_PALETTE] = 2,
		[C2D_COLOR_FORMAT_4_PALETTE] = 4,
		[C2D_COLOR_FORMAT_8_PALETTE] = 8,
		[C2D_COLOR_FORMAT_2_L] = 2,
		[C2D_COLOR_FORMAT_4_L] = 4,
		[C2D_COLOR_FORMAT_8_L] = 8,
		[C2D_COLOR_FORMAT_2_A] = 2,
		[C2D_COLOR_FORMAT_4_A] = 4,
		[C2D_COLOR_FORMAT_8_A] = 8,
		[C2D_COLOR_FORMAT_444_RGB] = 12,
		[C2D_COLOR_FORMAT_565_RGB] = 16,
		[C2D_COLOR_FORMAT_888_RGB] = 24,
		[C2D_COLOR_FORMAT_1555_ARGB] = 16,
		[C2D_COLOR_FORMAT_4444_ARGB] = 16,
		[C2D_COLOR_FORMAT_8565_ARGB] = 24,
		[C2D_COLOR_FORMAT_8888_ARGB] = 32,
		[C2D_COLOR_FORMAT_5551_RGBA] = 16,
		[C2D_COLOR_FORMAT_4444_RGBA] = 16,
		[C2D_COLOR_FORMAT_5658_RGBA] = 24,
		[C2D_COLOR_FORMAT_8888_RGBA] = 32,
};

#define xRGB (C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA)
#define ARGB C2D_COLOR_FORMAT_8888_ARGB
#define RGBx (C2D_COLOR_FORMAT_8888_RGBA | C2D_FORMAT_DISABLE_ALPHA)
#define RGBA C2D_COLOR_FORMAT_8888_RGBA
#define BGRx (C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA | C2D_FORMAT_SWAP_ENDIANNESS)
#define BGRA (C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_SWAP_ENDIANNESS)
#define xBGR (C2D_COLOR_FORMAT_8888_RGBA | C2D_FORMAT_DISABLE_ALPHA | C2D_FORMAT_SWAP_ENDIANNESS)
#define ABGR (C2D_COLOR_FORMAT_8888_RGBA | C2D_FORMAT_SWAP_ENDIANNESS)
#define A8   C2D_COLOR_FORMAT_8_A
#define U1   C2D_COLOR_FORMAT_1

static PixmapPtr create_pixmap(uint32_t w, uint32_t h, uint32_t format)
{
	int pitch = ALIGN((w * fmt_bpp[format & 0xff]) / 8, 128);
	C2D_RGB_SURFACE_DEF def = {
			.format = format,
			.width  = w,
			.height = h,
			.buffer = malloc(pitch * h),
			.phys   = NULL,
			.stride = pitch,
	};
	PixmapPtr pixmap = calloc(1, sizeof *pixmap);
	pixmap->width  = w;
	pixmap->height = h;
	pixmap->pitch  = pitch;
	pixmap->ptr    = def.buffer;
	pixmap->def    = def;
	CHK(c2dCreateSurface(&pixmap->id, C2D_SOURCE | C2D_TARGET,
			C2D_SURFACE_RGB_HOST, &def));
	DEBUG_MSG("created pixmap: %p %d", pixmap, pixmap->id);
	return pixmap;
}

static PixmapPtr create_pixmap_phys(uint32_t w, uint32_t h, uint32_t format,
		unsigned long phys)
{
	int pitch = ALIGN((w * fmt_bpp[format & 0xff]) / 8, 128);
	C2D_RGB_SURFACE_DEF def = {
			.format = format,
			.width  = w,
			.height = h,
			.buffer = (void *)1,
			.phys   = (void *)phys,
			.stride = pitch,
	};
	uint32_t sect[2] = {
			phys, pitch * h,
	};
	PixmapPtr pixmap = calloc(1, sizeof *pixmap);
	pixmap->width  = w;
	pixmap->height = h;
	pixmap->pitch  = pitch;
	pixmap->ptr    = def.buffer;
	pixmap->def    = def;
	CHK(c2dCreateSurface(&pixmap->id, C2D_SOURCE | C2D_TARGET,
			C2D_SURFACE_RGB_HOST | C2D_SURFACE_WITH_PHYS, &def));
	DEBUG_MSG("created pixmap: %p %d", pixmap, pixmap->id);
	rd_write_section(RD_GPUADDR, sect, sizeof(sect));
	return pixmap;
}


static void free_pixmap(PixmapPtr pix)
{
	CHK(c2dDestroySurface(pix->id));
	if (pix->ptr != (void *)1)
		free(pix->ptr);
	free(pix);
}

static void dump_pixmap(PixmapPtr pixmap, char *fmt, ...)
{
	char filename[256];

	va_list args;

	va_start(args, fmt);
	vsprintf(filename, fmt, args);
	va_end(args);

	CHK(c2dReadSurface(pixmap->id, C2D_SURFACE_RGB_HOST, &pixmap->def, 0, 0));
	wrap_bmp_dump(pixmap->ptr, pixmap->width, pixmap->height, pixmap->pitch, filename);
}


/* ************************************************************************* */
/* helper macros for tests, to let test-runner select test to run via
 * TESTNUM env variable.  Note env variable used because passing args
 * when tests are compiled for bionic doesn't really work.
 */

static inline int __gettest(void)
{
	const char *testnum = getenv("TESTNUM");
	if (testnum)
		return strtol(testnum, NULL, 0);
	return -1;
}

#define TEST_START() \
	int __n = 0, __test = __gettest()

#define TEST(t) do { \
		if ((__test == __n++) || (__test == -1)) { \
			t; \
		} \
	} while (0)

#define TEST_END() do { \
		if (__test >= __n++) \
			exit(42); \
	} while (0)

#endif /* TEST_UTIL_2D_H_ */
