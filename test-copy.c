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

#define DEFAULT_BLIT_MASK (C2D_SOURCE_RECT_BIT | C2D_TARGET_RECT_BIT |	\
			   C2D_NO_PIXEL_ALPHA_BIT | C2D_NO_BILINEAR_BIT | \
			   C2D_NO_ANTIALIASING_BIT | C2D_ALPHA_BLEND_NONE)

void test_copy(uint32_t w, uint32_t h, uint32_t format)
{
	PixmapPtr src, dest;
	C2D_OBJECT blit = {};
	C2D_RECT rect;
	c2d_ts_handle curTimestamp;

	DEBUG_MSG("----------------------------------------------------------------");
	DEBUG_MSG("copy: %04dx%04d-%08x", w, h, format);
	RD_START("copy", "%dx%d format:%08x", w, h, format);

	dest = create_pixmap(w, h, format);
	src  = create_pixmap(13, 17, format);

	rect.x = 1;
	rect.y = 2;
	rect.width = w - 2;
	rect.height = h - 3;
	CHK(c2dFillSurface(dest->id, 0xff556677, &rect));
	CHK(c2dFlush(dest->id, &curTimestamp));
	CHK(c2dWaitTimestamp(curTimestamp));

	rect.x = 0;
	rect.y = 0;
	rect.width = 13;
	rect.height = 17;
	CHK(c2dFillSurface(src->id, 0xff223344, &rect));
	CHK(c2dFlush(src->id, &curTimestamp));
	CHK(c2dWaitTimestamp(curTimestamp));

	blit.surface_id = src->id;
	blit.config_mask = DEFAULT_BLIT_MASK;
	blit.next = NULL;

	blit.source_rect.x = FIXED(1);
	blit.source_rect.y = FIXED(2);
	blit.source_rect.width = FIXED(13-1);
	blit.source_rect.height = FIXED(17-2);

	blit.target_rect.x = FIXED((w - 13) / 2);
	blit.target_rect.y = FIXED((h - 17) / 2);
	blit.target_rect.width = FIXED(13);
	blit.target_rect.height = FIXED(17);
	CHK(c2dDraw(dest->id, 0, NULL, 0, 0, &blit, 1));
	CHK(c2dFlush(dest->id, &curTimestamp));
	CHK(c2dWaitTimestamp(curTimestamp));

	RD_END();

	dump_pixmap(dest, "copy-%04dx%04d-%08x.bmp", w, h, format);
}

int main(int argc, char **argv)
{
	/* create dummy pixmap to get initialization out of the way */
	c2d_ts_handle curTimestamp;
	PixmapPtr tmp = create_pixmap(64, 64, C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA);
	CHK(c2dFlush(tmp->id, &curTimestamp));
	CHK(c2dWaitTimestamp(curTimestamp));

	test_copy(63, 65, C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA);
	test_copy(127, 260, C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA);
	test_copy(62, 66, C2D_COLOR_FORMAT_8888_ARGB);
	test_copy(59, 69, C2D_COLOR_FORMAT_565_RGB);

	return 0;
}

void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}

