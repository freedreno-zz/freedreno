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

#include "test-util.h"

struct blend_mode {
	const char *name;
	uint32_t    mode;
};

/* mapping xorg composite op to c2d2 config_mask bits: */
static const struct blend_mode blend_modes[] = {
		{ "PictOpSrc",			C2D_ALPHA_BLEND_SRC },
		{ "PictOpIn",			C2D_ALPHA_BLEND_SRC_IN },
		{ "PictOpOut",			C2D_ALPHA_BLEND_SRC_OUT },
		{ "PictOpOver",			C2D_ALPHA_BLEND_SRC_OVER },
		{ "PictOpOutReverse",	C2D_ALPHA_BLEND_DST_OUT },
		{ "PictOpAdd",			C2D_ALPHA_BLEND_ADDITIVE },
		{ "PictOpOverReverse",	C2D_ALPHA_BLEND_DST_OVER },
		{ "PictOpInReverse",	C2D_ALPHA_BLEND_DST_IN },
		{ "PictOpAtop",			C2D_ALPHA_BLEND_SRC_ATOP },
		{ "PictOpAtopReverse",	C2D_ALPHA_BLEND_DST_ATOP },
		{ "PictOpXor",			C2D_ALPHA_BLEND_XOR },
};

struct format_mode {
	const char *name;
	uint32_t    format;
};

static const struct format_mode format_modes[] = {
		{ "xRGB", xRGB },
		{ "ARGB", ARGB },
		{ "A8",   A8 },
};

#define DEFAULT_BLEND_MASK	(C2D_TARGET_RECT_BIT | C2D_NO_BILINEAR_BIT | \
							C2D_NO_ANTIALIASING_BIT)

void test_composite(const char *name, const struct blend_mode *blend,
		const struct format_mode *dst_format,
		const struct format_mode *src_format, uint32_t src_repeat,
		const struct format_mode *mask_format, uint32_t mask_repeat)
{
	PixmapPtr src, dest;
	C2D_OBJECT blit = {};
	c2d_ts_handle curTimestamp;
	uint32_t sw = 17, sh = 19;

	DEBUG_MSG("----------------------------------------------------------------");
	DEBUG_MSG("%s: op:%s src:%s (repeat:%d) dst:%s",
			name, blend->name, src_format->name, src_repeat, dst_format->name);
	RD_START(name, "op:%s src:%s (repeat:%d) dst:%s",
			blend->name, src_format->name, src_repeat, dst_format->name);

	dest = create_pixmap(1033, 1077, dst_format->format);
	src  = src_repeat ? create_pixmap(1, 1, src_format->format) :
			create_pixmap(sw, sh, src_format->format);

	blit.surface_id = src->id;
	blit.config_mask = DEFAULT_BLEND_MASK | blend->mode;

	if (!src_repeat)
		blit.config_mask |= C2D_SOURCE_RECT_BIT;

	blit.next = NULL;

	blit.source_rect.x = FIXED(1);
	blit.source_rect.y = FIXED(2);
	blit.source_rect.width = FIXED(sw - blit.source_rect.x - 1);
	blit.source_rect.height = FIXED(sh - blit.source_rect.y - 2);

	blit.target_rect.x = FIXED((dest->width - sw) / 2);
	blit.target_rect.y = FIXED((dest->height - sh) / 2);
	blit.target_rect.width = blit.source_rect.width;
	blit.target_rect.height = blit.source_rect.height;
	CHK(c2dDraw(dest->id, 0, NULL, 0, 0, &blit, 1));
	CHK(c2dFlush(dest->id, &curTimestamp));
	CHK(c2dWaitTimestamp(curTimestamp));

	free_pixmap(src);
	free_pixmap(dest);

	RD_END();

//	dump_pixmap(dest, "copy-%04dx%04d-%08x.bmp", w, h, format);
}

int main(int argc, char **argv)
{
	uint32_t i, j;

	/* create dummy pixmap to get initialization out of the way */
	c2d_ts_handle curTimestamp;
	PixmapPtr tmp = create_pixmap(64, 64, xRGB);
	CHK(c2dFlush(tmp->id, &curTimestamp));
	CHK(c2dWaitTimestamp(curTimestamp));

	/* NOTE: my assumption here is that repeat, op, and color formats
	 * are rather independent, so we don't need to dump every possible
	 * combination (which would be a huge list)..  possibly need to
	 * sanity check this assumption, though..
	 */

	/* test composite ops: */
	for (i = 0; i < ARRAY_SIZE(blend_modes); i++) {
		test_composite("composite-op", &blend_modes[i],
				&format_modes[1],
				&format_modes[1], FALSE,
				NULL, FALSE);
	}

	/* test formats, by dst: */
	for (i = 0; i < ARRAY_SIZE(format_modes); i++) {
		char name[32];
		sprintf(name, "composite-dst-%s", format_modes[i].name);
		for (j = 0; j < ARRAY_SIZE(format_modes); j++) {
			// TODO add mask:
			test_composite(name, &blend_modes[4],
					&format_modes[i],
					&format_modes[j], FALSE,
					NULL, FALSE);
		}
	}

	/* test formats, by src: */
	for (i = 0; i < ARRAY_SIZE(format_modes); i++) {
		char name[32];
		sprintf(name, "composite-src-%s", format_modes[i].name);
		for (j = 0; j < ARRAY_SIZE(format_modes); j++) {
			// TODO add mask:
			test_composite(name, &blend_modes[4],
					&format_modes[j],
					&format_modes[i], FALSE,
					NULL, FALSE);
		}
	}

	/* test repeat: */
	// TODO add mask:
	test_composite("composite-repeat", &blend_modes[4],
			&format_modes[0],
			&format_modes[0], FALSE,
			NULL, FALSE);
	test_composite("composite-repeat", &blend_modes[4],
			&format_modes[0],
			&format_modes[0], TRUE,
			NULL, FALSE);

	return 0;
}

void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}

