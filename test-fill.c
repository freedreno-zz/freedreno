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
//void free(void *ptr);
//void *realloc(void *ptr, size_t size);

/*****************************************************************************/

/* split some of the utils into common file.. */

#define DEBUG_MSG(fmt, ...) \
		do { printf("%s:%d: " fmt "\n", \
				__FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)
#define ERROR_MSG(fmt, ...) \
		do { printf("ERROR: %s:%d: " fmt "\n", \
				__FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)

typedef enum {
	TRUE = 1,
	FALSE = 0,
} Bool;
#define NULL (void *)0

typedef struct {
	int width, height, pitch;
	void *ptr;
} Pixmap, *PixmapPtr;

C2D_STATUS
c2d2_create_rgb_surface(PixmapPtr pixmap, unsigned int *id)
{
	C2D_RGB_SURFACE_DEF def = {
			.format = C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA,
			.width = pixmap->width,
			.height = pixmap->height,
			.buffer = pixmap->ptr,
			.phys = NULL,
			.stride = pixmap->pitch,
	};

	return c2dCreateSurface(id, C2D_SOURCE | C2D_TARGET,
			C2D_SURFACE_RGB_HOST,
			&def);
}

PixmapPtr create_pixmap(int width, int height)
{
	PixmapPtr pixmap = calloc(1, sizeof *pixmap);
	pixmap->width  = width;
	pixmap->height = height;
	pixmap->pitch  = width * 4; // XXX probably need to round up??
	pixmap->ptr    = malloc(pixmap->pitch * pixmap->height); // XXX ???
	return pixmap;
}

void dump_pixmap(PixmapPtr pixmap, char *filename)
{
	wrap_bmp_dump(pixmap->ptr, pixmap->pitch * pixmap->height,
			pixmap->width, pixmap->height, filename);
}

/*****************************************************************************/

#define CHK(x) do { \
		C2D_STATUS status = x; \
		if (status) { \
			ERROR_MSG("%s failed: %d", #x, status); \
			return -1; \
		} \
		DEBUG_MSG("%s succeeded", #x); \
	} while (0)


int main(int argc, char **argv)
{
	PixmapPtr dest = create_pixmap(64, 64);
	int dest_id;
	C2D_RECT rect;
	c2d_ts_handle curTimestamp;

	dump_pixmap(dest, "before.bmp");

	CHK(c2d2_create_rgb_surface(dest, &dest_id));

	rect.x = 0;
	rect.y = 0;
	rect.width = 64;
	rect.height = 64;

	CHK(c2dFillSurface(dest_id, 0xff556677, &rect));
	CHK(c2dFlush(dest_id, &curTimestamp));
	CHK(c2dWaitTimestamp(curTimestamp));

	dump_pixmap(dest, "after.bmp");

	return 0;
}

void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}

