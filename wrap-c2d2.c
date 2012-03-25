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

/* wrapper code to intercept c2d2 library calls, so we can log interesting
 * parameters..
 */

#include "wrap.h"
#include "c2d2.h"

static void param(enum rd_param_type ptype, uint32_t val, uint32_t bitlen)
{
	uint32_t buf[3] = { ptype, val, bitlen };
	rd_write_section(RD_PARAM, buf, sizeof(buf));
}

/* just needed so we don't have to link directly against libC2D2.so, which
 * would mess up the ioctl wrapping..
 */
C2D_STATUS c2dQuerySurface(uint32_t surface_id, uint32_t *surface_bits,
		C2D_SURFACE_TYPE *surface_type, uint32_t *width, uint32_t *height,
		uint32_t *format)
{
	PROLOG(c2dQuerySurface);
	return orig_c2dQuerySurface(surface_id, surface_bits,
			surface_type, width, height, format);
}

C2D_STATUS c2dFlush(uint32 target_id, c2d_ts_handle *timestamp)
{
	C2D_STATUS ret;
	PROLOG(c2dFlush);
	ret = orig_c2dFlush(target_id, timestamp);
	// note: this has to come after c2dFlush call:
	// a size of 0 confuses redump, so just put in some bogus payloads
	rd_write_section(RD_FLUSH, &target_id, 1);
	return ret;
}


C2D_STATUS c2dFillSurface(uint32_t surface_id, uint32_t fill_color, C2D_RECT *rect)
{
	uint32_t surface_bits, width, height, format, color;
	C2D_SURFACE_TYPE surface_type;
	PROLOG(c2dFillSurface);

	/* note, from msm-exa.c:
	 *    /* Max blit extents that hw supports * /
	 *    pExa->maxX = 2048;
	 *    pExa->maxY = 2048;
	 *
	 * so figure that x/y/w/h are 11 bits..
	 */

	c2dQuerySurface(surface_id, &surface_bits, &surface_type, &width, &height, &format);

	switch(format & 0xff) {
	case C2D_COLOR_FORMAT_565_RGB:
		color  = ((fill_color << 3) & 0xf8) | ((fill_color >> 2) & 0x07) |
			((fill_color << 5) & 0xfc00)    | ((fill_color >> 1) & 0x300) |
			((fill_color << 8) & 0xf80000)  | ((fill_color << 3) & 0x70000) |
			0xff000000; /* implicitly DISABLE_ALPHA */
		break;
	case C2D_COLOR_FORMAT_8888_ARGB:
		color = fill_color;
		break;
	default:
		printf("error, unsupported format: %d\n", format & 0xff);
		exit(1);
	}

	if (format & C2D_FORMAT_DISABLE_ALPHA)
		format |= 0xff000000;

	param(RD_PARAM_COLOR,          color,        32);
	param(RD_PARAM_SURFACE_WIDTH,  width,        11);
	param(RD_PARAM_SURFACE_HEIGHT, height,       11);
	param(RD_PARAM_BLIT_X,         rect->x,      11);
	param(RD_PARAM_BLIT_Y,         rect->y,      11);
	param(RD_PARAM_BLIT_WIDTH,     rect->width,  11);
	param(RD_PARAM_BLIT_HEIGHT,    rect->height, 11);

	return orig_c2dFillSurface(surface_id, fill_color, rect);
}

C2D_STATUS c2dDraw(uint32_t target_id,  uint32_t target_config, C2D_RECT *target_scissor,
		uint32_t target_mask_id, uint32_t target_color_key,
		C2D_OBJECT *objects_list, uint32_t num_objects)
{
	uint32_t surface_bits, width, height, format, color;
	C2D_SURFACE_TYPE surface_type;
	C2D_RECT *rect;
	PROLOG(c2dDraw);

	c2dQuerySurface(target_id, &surface_bits, &surface_type, &width, &height, &format);

	param(RD_PARAM_SURFACE_WIDTH,  width,              11);
	param(RD_PARAM_SURFACE_HEIGHT, height,             11);
	rect = &objects_list->source_rect;
	param(RD_PARAM_BLIT_X,         rect->x >> 16,      11);
	param(RD_PARAM_BLIT_Y,         rect->y >> 16,      11);
	param(RD_PARAM_BLIT_WIDTH,     rect->width >> 16,  11);
	param(RD_PARAM_BLIT_HEIGHT,    rect->height >> 16, 11);
	rect = &objects_list->target_rect;
	param(RD_PARAM_BLIT_X,         rect->x >> 16,      11);
	param(RD_PARAM_BLIT_Y,         rect->y >> 16,      11);
	param(RD_PARAM_BLIT_WIDTH,     rect->width >> 16,  11);
	param(RD_PARAM_BLIT_HEIGHT,    rect->height >> 16, 11);

	return orig_c2dDraw(target_id, target_config, target_scissor, target_mask_id,
			target_color_key, objects_list, num_objects);
}
