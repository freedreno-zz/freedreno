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

#include "test-util-2d.h"

void test_fill(uint32_t w, uint32_t h, uint32_t format,
		uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2)
{
	PixmapPtr dest;
	C2D_RECT rect;
	c2d_ts_handle curTimestamp;

	DEBUG_MSG("----------------------------------------------------------------");
	DEBUG_MSG("fill2: %04dx%04d-%08x-%d,%d,%d,%d", w, h, format, x1, y1, x2, y2);
	RD_START("fill2", "%dx%d-%08x-%d-%d-%d-%d", w, h, format, x1, y1, x2, y2);

	dest = create_pixmap(w, h, format);

	rect.x = x1;
	rect.y = y1;
	rect.width = x2 - x1;
	rect.height = y2 - y1;

	CHK(c2dFillSurface(dest->id, 0x00585a5d, &rect));
	CHK(c2dFlush(dest->id, &curTimestamp));
	CHK(c2dWaitTimestamp(curTimestamp));

	RD_END();

	dump_pixmap(dest, "fill-%04dx%04d-%08x.bmp", w, h, format);
}

int main(int argc, char **argv)
{
	/* create dummy pixmap to get initialization out of the way */
	c2d_ts_handle curTimestamp;
	PixmapPtr tmp = create_pixmap(64, 64, xRGB);
	CHK(c2dFlush(tmp->id, &curTimestamp));
	CHK(c2dWaitTimestamp(curTimestamp));

	test_fill(1920, 1080, xRGB, 0, 0, 1920, 1080);
	test_fill(1920, 1080, xRGB, 717, 395, 718, 685);

	/* test limits of x, y */
	test_fill(1920, 1080, xRGB, 0x100, 0, 0x101, 0);
	test_fill(1920, 1080, xRGB, 0, 0x100, 0, 0x101);

	/* test limits of width, height */
	test_fill(1920, 1080, xRGB, 0, 0, 0x100, 0);
	test_fill(1920, 1080, xRGB, 0, 0, 0, 0x100);

	test_fill(1, 1, xRGB, 0, 0, 1, 1);

	return 0;
}

#ifdef BIONIC
void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}
#endif
