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

/* this is mainly just for reproducing specific blits, to compare cmdstream
 * to what EXA driver is generating
 */

#define DEFAULT_BLEND_MASK	(C2D_TARGET_RECT_BIT | C2D_NO_BILINEAR_BIT | \
							C2D_NO_ANTIALIASING_BIT)

void test_composite(uint32_t blend_mode,
		uint32_t dst_format, uint32_t dst_width, uint32_t dst_height,
		uint32_t src_format, uint32_t src_width, uint32_t src_height,
		uint32_t mask_format, uint32_t mask_width, uint32_t mask_height,
		uint32_t src_x, uint32_t src_y, uint32_t mask_x, uint32_t mask_y,
		uint32_t dst_x, uint32_t dst_y, uint32_t w, uint32_t h)
{
	PixmapPtr src, dest, mask = NULL;
	C2D_OBJECT blit = {};
	c2d_ts_handle curTimestamp;

	DEBUG_MSG("composite2: blend_mode:%08x, dst_format:%08x, dst_width:%x, dst_height=%x, "
			"src_format:%08x, src_width:%x, src_height:%x, "
			"mask_format:%08x, mask_width:%x, mask_height:%x, "
			"src_x:%x, src_y:%x, mask_x:%x, mask_y:%x, dst_x:%x, dst_y:%x, w:%x, h:%x",
			blend_mode, dst_format, dst_width, dst_height,
			src_format, src_width, src_height,
			mask_format, mask_width, mask_height,
			src_x, src_y, mask_x, mask_y, dst_x, dst_y, w, h);
	RD_START("composite2","blend_mode:%08x, dst_format:%08x, dst_width:%x, dst_height=%x, "
			"src_format:%08x, src_width:%x, src_height:%x, "
			"mask_format:%08x, mask_width:%x, mask_height:%x, "
			"src_x:%x, src_y:%x, mask_x:%x, mask_y:%x, dst_x:%x, dst_y:%x, w:%x, h:%x",
			blend_mode, dst_format, dst_width, dst_height,
			src_format, src_width, src_height,
			mask_format, mask_width, mask_height,
			src_x, src_y, mask_x, mask_y, dst_x, dst_y, w, h);

	blit.config_mask = DEFAULT_BLEND_MASK | blend_mode;

	dest = create_pixmap(dst_width, dst_height, dst_format);
	src  = create_pixmap(src_width, src_height, src_format);

	blit.config_mask |= C2D_SOURCE_RECT_BIT;
	blit.surface_id = src->id;

	if (mask_format) {
		/* TODO not clear if mask repeat is really supported.. msm-exa-c2d2.c
		 * seems to reject it but C2D_MASK_TILE_BIT??
		 *
		 * Also, for src format, msm-exa-c2d2.c seems to encode fgcolor (like
		 * a solid fill) for repeats.. not really clear if TILE_BIT does what
		 * we expect or not??
		 *
		 * Seems like libC2D2 doesn't actually give any way to specify the
		 * maskX/maskY!!!  The previous c2d API does, so I'd have to assume
		 * this is actually supported by the hardware and this is just C2D2
		 * retardation
		 */
		mask = create_pixmap(mask_width, mask_height, mask_format);

		blit.config_mask |= C2D_MASK_SURFACE_BIT;
		blit.mask_surface_id = mask->id;
	}

	blit.next = NULL;

	blit.source_rect.x = FIXED(src_x);
	blit.source_rect.y = FIXED(src_y);
	blit.source_rect.width = FIXED(w);
	blit.source_rect.height = FIXED(h);

	blit.target_rect.x = FIXED(dst_x);
	blit.target_rect.y = FIXED(dst_y);
	blit.target_rect.width = FIXED(w);
	blit.target_rect.height = FIXED(h);
	CHK(c2dDraw(dest->id, 0, NULL, 0, 0, &blit, 1));
	CHK(c2dFlush(dest->id, &curTimestamp));
	CHK(c2dWaitTimestamp(curTimestamp));

	free_pixmap(src);
	free_pixmap(dest);
	if (mask)
		free_pixmap(mask);

	RD_END();

//	dump_pixmap(dest, "copy-%04dx%04d-%08x.bmp", w, h, format);
}

int main(int argc, char **argv)
{
/*
[  1340.257] (II) freedreno(0): MSMCheckComposite:590 op:12: 0x2eb268 {20028888, 0} <- 0x2eb3a8 {08018000, 0} ((nil) {00000000, 0})
[  1340.257] (II) freedreno(0): MSMPrepareComposite:694 0x2eb178 {35x8,256} <- 0x2eb2c8 {1024x320,1024} ((nil) {0x0,0})
[  1340.258] (II) freedreno(0): MSMComposite:766 srcX=0 srcY=0  maskX=0 maskY=0 dstX=0  dstY=2  width=7 height=6
 */

	test_composite(C2D_ALPHA_BLEND_ADDITIVE,
			ARGB, 35,   8,
			A8,   1024, 320,
			0,    0,    0,
			0, 0, 0, 0, 0, 2, 7, 6);

	return 0;
}

#ifdef BIONIC
void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}
#endif
