/*
 * Copyright © 2012 Rob Clark
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

#include "c2d2.h"
#include "bmp.h"

typedef long unsigned int size_t;
void exit(int status);
int printf(const char *,...);
void *calloc(size_t nmemb, size_t size);
void *malloc(size_t size);

/*****************************************************************************/

/* split some of the utils into common file.. */

#define DEBUG_MSG(fmt, ...) \
		do { printf(fmt " (%s:%d)\n", \
				##__VA_ARGS__, __FUNCTION__, __LINE__); } while (0)
#define ERROR_MSG(fmt, ...) \
		do { printf("ERROR: " fmt " (%s:%d)\n", \
				##__VA_ARGS__, __FUNCTION__, __LINE__); } while (0)

#define CHK(x) do { \
		C2D_STATUS status; \
		DEBUG_MSG(">>> %s", #x); \
		status = x; \
		if (status) { \
			ERROR_MSG("<<< %s: failed: %d", #x, status); \
			exit(-1); \
		} \
		DEBUG_MSG("<<< %s: succeeded", #x); \
	} while (0)

typedef enum {
	TRUE = 1,
	FALSE = 0,
} Bool;
#define NULL (void *)0

typedef struct {
	C2D_RGB_SURFACE_DEF def;
	int width, height, pitch, id;
	void *ptr;
} Pixmap, *PixmapPtr;

PixmapPtr create_pixmap(int width, int height)
{
	int pitch = width * 4;  // XXX probably need to round up??
	C2D_RGB_SURFACE_DEF def = {
			.format = C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA,
			.width  = width,
			.height = height,
			.buffer = malloc(pitch * height), // XXX ???,
			.phys = NULL,
			.stride = pitch,
	};
	PixmapPtr pixmap = calloc(1, sizeof *pixmap);
	pixmap->width  = width;
	pixmap->height = height;
	pixmap->pitch  = pitch;
	pixmap->ptr    = def.buffer;
	pixmap->def    = def;
	CHK(c2dCreateSurface(&pixmap->id, C2D_SOURCE | C2D_TARGET,
			C2D_SURFACE_RGB_HOST, &def));
	DEBUG_MSG("created pixmap: %p %d", pixmap, pixmap->id);
	return pixmap;
}

void dump_pixmap(PixmapPtr pixmap, char *filename)
{
	CHK(c2dReadSurface(pixmap->id, C2D_SURFACE_RGB_HOST, &pixmap->def, 0, 0));
	wrap_bmp_dump(pixmap->ptr, pixmap->pitch * pixmap->height,
			pixmap->width, pixmap->height, filename);
}

/*****************************************************************************/


void test_fill(int w, int h)
{
	PixmapPtr dest = create_pixmap(w, h);
	C2D_RECT rect;
	c2d_ts_handle curTimestamp;

	dump_pixmap(dest, "before.bmp");

	rect.x = 0;
	rect.y = 0;
	rect.width = w;
	rect.height = h;

	// note: look for pattern 0xff556677 in memory to find cmdstream:
	CHK(c2dFillSurface(dest->id, 0xff556677, &rect));
	CHK(c2dFlush(dest->id, &curTimestamp));
	CHK(c2dWaitTimestamp(curTimestamp));

	dump_pixmap(dest, "after.bmp");
}

int main(int argc, char **argv)
{
	DEBUG_MSG("Test fill 64, 64");
	test_fill(64, 64);

	// for now, two same sized blits.. once I think I know where
	// the cmdstream is then I'll try varying dimenions (and color?)
	DEBUG_MSG("Test fill 64, 64");
	test_fill(64, 64);
}

void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}

