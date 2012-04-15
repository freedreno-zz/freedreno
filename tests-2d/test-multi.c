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

#define DEFAULT_BLIT_MASK (C2D_SOURCE_RECT_BIT | C2D_TARGET_RECT_BIT |	\
			   C2D_NO_PIXEL_ALPHA_BIT | C2D_NO_BILINEAR_BIT | \
			   C2D_NO_ANTIALIASING_BIT | C2D_ALPHA_BLEND_NONE)

/* simple test w/ multiple ops before flush to see how cmdstream is built for
 * multiple blits
 */

void test_multi(void)
{
	PixmapPtr src, dest;
	C2D_OBJECT blit = {};
	C2D_RECT rect;
	c2d_ts_handle curTimestamp;
	uint32_t w = 1920, h = 1080, format = xRGB;
	int i;

	DEBUG_MSG("----------------------------------------------------------------");
	DEBUG_MSG("multi: %04dx%04d-%08x", w, h, format);
	RD_START("multi", "%dx%d format:%08x", w, h, format);

	dest = create_pixmap(w, h, format);
	src  = create_pixmap(w, h, format);

	for (i = 0; i < 200; i++ ) {

		rect.x = 1;
		rect.y = 2;
		rect.width = w - 2;
		rect.height = h - 3;
		CHK(c2dFillSurface(dest->id, 0xff556677, &rect));
		CHK(c2dFillSurface(dest->id, 0xff556677, &rect));
		CHK(c2dFillSurface(dest->id, 0xff556677, &rect));
		CHK(c2dFillSurface(dest->id, 0xff556677, &rect));
		CHK(c2dFillSurface(dest->id, 0xff556677, &rect));
		CHK(c2dFillSurface(dest->id, 0xff556677, &rect));
		CHK(c2dFillSurface(dest->id, 0xff556677, &rect));
		CHK(c2dFillSurface(dest->id, 0xff556677, &rect));

		rect.x = 0;
		rect.y = 0;
		rect.width = 13;
		rect.height = 17;
		CHK(c2dFillSurface(src->id, 0xff223344, &rect));
		CHK(c2dFillSurface(src->id, 0xff223344, &rect));
		CHK(c2dFillSurface(src->id, 0xff223344, &rect));
		CHK(c2dFillSurface(src->id, 0xff223344, &rect));
		CHK(c2dFillSurface(src->id, 0xff223344, &rect));
		CHK(c2dFillSurface(src->id, 0xff223344, &rect));
		CHK(c2dFillSurface(src->id, 0xff223344, &rect));
		CHK(c2dFillSurface(src->id, 0xff223344, &rect));

		blit.surface_id = src->id;
		blit.config_mask = DEFAULT_BLIT_MASK;
		blit.next = NULL;

		blit.source_rect.x = FIXED(1);
		blit.source_rect.y = FIXED(2);
		blit.source_rect.width = FIXED(13-2);
		blit.source_rect.height = FIXED(17-4);

		blit.target_rect.x = FIXED((w - 13) / 2);
		blit.target_rect.y = FIXED((h - 17) / 2);
		blit.target_rect.width = blit.source_rect.width;
		blit.target_rect.height = blit.source_rect.height;
		CHK(c2dDraw(dest->id, 0, NULL, 0, 0, &blit, 1));
		// well, identical copy twice is fine, and I'm lazy:
		CHK(c2dDraw(dest->id, 0, NULL, 0, 0, &blit, 1));
		CHK(c2dDraw(dest->id, 0, NULL, 0, 0, &blit, 1));
		CHK(c2dDraw(dest->id, 0, NULL, 0, 0, &blit, 1));
		CHK(c2dDraw(dest->id, 0, NULL, 0, 0, &blit, 1));
		CHK(c2dDraw(dest->id, 0, NULL, 0, 0, &blit, 1));
		CHK(c2dDraw(dest->id, 0, NULL, 0, 0, &blit, 1));
		CHK(c2dDraw(dest->id, 0, NULL, 0, 0, &blit, 1));
		CHK(c2dDraw(dest->id, 0, NULL, 0, 0, &blit, 1));

		CHK(c2dFlush(dest->id, &curTimestamp));

		if (!(i % 16))
		CHK(c2dWaitTimestamp(curTimestamp));

	}

	free_pixmap(src);
	free_pixmap(dest);

	RD_END();
}

int main(int argc, char **argv)
{
	test_multi();

	return 0;
}

void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}

