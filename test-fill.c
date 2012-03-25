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

#include "test-util.h"

void test_fill(uint32_t w, uint32_t h, uint32_t format)
{
	PixmapPtr dest;
	C2D_RECT rect;
	c2d_ts_handle curTimestamp;

	DEBUG_MSG("----------------------------------------------------------------");
	DEBUG_MSG("fill: %04dx%04d-%08x", w, h, format);
	RD_START("fill", "%04dx%04d-%08x", w, h, format);

	dest = create_pixmap(w, h, format);

	rect.x = 0;
	rect.y = 0;
	rect.width = w;
	rect.height = h;

	// note: look for pattern 0xff556677 in memory to find cmdstream:
	CHK(c2dFillSurface(dest->id, 0xff556677, &rect));

	// second blit.. fill a sub-rect in center of surface:
	rect.x = (w - 10) / 2;
	rect.y = (h - 16) / 2;
	rect.width = 10;
	rect.height = 16;
	CHK(c2dFillSurface(dest->id, 0xff223344, &rect));

	CHK(c2dFlush(dest->id, &curTimestamp));
	CHK(c2dWaitTimestamp(curTimestamp));

	RD_END();

	dump_pixmap(dest, "fill-%04dx%04d-%08x.bmp", w, h, format);
}

int main(int argc, char **argv)
{
	/* create dummy pixmap to get initialization out of the way */
	c2d_ts_handle curTimestamp;
	PixmapPtr tmp = create_pixmap(64, 64, C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA);
	CHK(c2dFlush(tmp->id, &curTimestamp));
	CHK(c2dWaitTimestamp(curTimestamp));

	test_fill(64, 64, C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA);
	test_fill(128, 256, C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA);
	test_fill(64, 64, C2D_COLOR_FORMAT_8888_ARGB);
	test_fill(64, 64, C2D_COLOR_FORMAT_565_RGB);

	return 0;
}

void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}

