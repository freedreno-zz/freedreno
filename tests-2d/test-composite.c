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
		uint32_t bw, uint32_t bh,
		const struct format_mode *src_format,
		uint32_t src_repeat, uint32_t sw, uint32_t sh,
		const struct format_mode *mask_format,
		uint32_t mask_repeat, uint32_t mw, uint32_t mh)
{
	PixmapPtr src, dest, mask = NULL;
	C2D_OBJECT blit = {};
	c2d_ts_handle curTimestamp;

	DEBUG_MSG("%s: op:%s src:%s (repeat:%d) mask=%s (repeat:%d) dst:%s",
			name, blend->name, src_format->name, src_repeat,
			mask_format ? mask_format->name : "none", mask_repeat,
			dst_format->name);
	RD_START(name, "op:%s src:%s (repeat:%d) mask=%s (repeat:%d) dst:%s",
			blend->name, src_format->name, src_repeat,
			mask_format ? mask_format->name : "none", mask_repeat,
			dst_format->name);

	blit.config_mask = DEFAULT_BLEND_MASK | blend->mode;

	dest = create_pixmap(1033, 1077, dst_format->format);

	if (src_repeat) {
		src  = create_pixmap(1, 1, src_format->format);
		blit.config_mask |= C2D_SOURCE_TILE_BIT;
	} else {
		src = create_pixmap(sw, sh, src_format->format);
	}

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
		if (mask_repeat) {
			mask = create_pixmap(1, 1, mask_format->format);
			blit.config_mask |= C2D_MASK_TILE_BIT;
		} else {
			mask = create_pixmap(mw, mh, mask_format->format);
		}

		blit.config_mask |= C2D_MASK_SURFACE_BIT;
		blit.mask_surface_id = mask->id;
	} else {
		// TODO make redump not confused when one column has extra rows
		mask = create_pixmap(1, 1, ARGB);
	}

	blit.next = NULL;

	blit.source_rect.x = FIXED(1);
	blit.source_rect.y = FIXED(2);
	blit.source_rect.width = FIXED(bw - blit.source_rect.x - 1);
	blit.source_rect.height = FIXED(bh - blit.source_rect.y - 2);

	blit.target_rect.x = FIXED((dest->width - sw) / 2);
	blit.target_rect.y = FIXED((dest->height - sh) / 2);
	blit.target_rect.width = blit.source_rect.width;
	blit.target_rect.height = blit.source_rect.height;
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
	uint32_t i, j, k, l;

	/* NOTE: my assumption here is that repeat, op, and color formats
	 * are rather independent, so we don't need to dump every possible
	 * combination (which would be a huge list)..  possibly need to
	 * sanity check this assumption, though..
	 */

	/* test composite ops: */
	for (i = 0; i < ARRAY_SIZE(blend_modes); i++) {
		test_composite("composite-op", &blend_modes[i],
				&format_modes[2], 17, 19,
				&format_modes[0], FALSE, 17, 19,
				NULL, FALSE, 17, 19);
	}

	/* test formats, by dst: */
	for (i = 0; i < ARRAY_SIZE(format_modes); i++) {
		char name[32];
		sprintf(name, "composite-dst-%s", format_modes[i].name);
		for (j = 0; j < ARRAY_SIZE(format_modes); j++) {
			// TODO add mask:
			test_composite(name, &blend_modes[4],
					&format_modes[i], 17, 19,
					&format_modes[j], FALSE, 17, 19,
					NULL, FALSE, 17, 19);
		}
	}

	/* test formats, by src: */
	for (i = 0; i < ARRAY_SIZE(format_modes); i++) {
		char name[32];
		sprintf(name, "composite-src-%s", format_modes[i].name);
		for (j = 0; j < ARRAY_SIZE(format_modes); j++) {
			// TODO add mask:
			test_composite(name, &blend_modes[4],
					&format_modes[j], 17, 19,
					&format_modes[i], FALSE, 17, 19,
					NULL, FALSE, 17, 19);
		}
	}

	/* test all combinations of src/dst/mask/op: */
	for (l = 0; l < ARRAY_SIZE(blend_modes); l++) {
		for (i = 0; i < ARRAY_SIZE(format_modes); i++) {
			for (j = 0; j < ARRAY_SIZE(format_modes); j++) {
				for (k = 0; k < ARRAY_SIZE(format_modes); k++) {
					test_composite("composite-all", &blend_modes[l],
							&format_modes[i], 17, 19,
							&format_modes[j], FALSE, 17, 19,
							&format_modes[k], FALSE, 17, 19);
				}
				test_composite("composite-all", &blend_modes[l],
						&format_modes[i], 17, 19,
						&format_modes[j], FALSE, 17, 19,
						NULL, FALSE, 17, 19);
			}
		}
	}

	/* test with/without mask: */
	test_composite("composite-mask", &blend_modes[3],
			&format_modes[0], 17, 19,
			&format_modes[0], FALSE, 17, 19,
			NULL, FALSE, 17, 19);
	for (i = 0; i < ARRAY_SIZE(format_modes); i++) {
		test_composite("composite-mask", &blend_modes[3],
				&format_modes[0], 17, 19,
				&format_modes[0], FALSE, 17, 19,
				&format_modes[i], FALSE, 17, 19);
	}

	/* test repeat: */
	test_composite("composite-repeat", &blend_modes[4],
			&format_modes[0], 17, 19,
			&format_modes[0], FALSE, 17, 19,
			NULL, FALSE, 17, 19);
	test_composite("composite-repeat", &blend_modes[4],
			&format_modes[0], 17, 19,
			&format_modes[0], TRUE, 5, 5,
			NULL, FALSE, 0, 0);
	test_composite("composite-repeat", &blend_modes[4],
			&format_modes[0], 17, 19,
			&format_modes[0], TRUE, 6, 6,
			NULL, FALSE, 0, 0);
	test_composite("composite-repeat", &blend_modes[4],
			&format_modes[0], 17, 19,
			&format_modes[0], TRUE, 7, 7,
			&format_modes[2], TRUE, 5, 5);
	test_composite("composite-repeat", &blend_modes[4],
			&format_modes[0], 17, 19,
			&format_modes[0], TRUE, 8, 8,
			&format_modes[2], TRUE, 1, 1);

	return 0;
}

#ifdef BIONIC
void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}
#endif
