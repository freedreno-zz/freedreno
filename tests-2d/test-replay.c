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

static void end(void);

#ifdef BIONIC
#include "test-util-2d.h"

static void init(void)
{
	/* create dummy pixmap to get initialization out of the way */
	c2d_ts_handle curTimestamp;
	PixmapPtr tmp = create_pixmap(64, 64, xRGB);
	CHK(c2dFlush(tmp->id, &curTimestamp));
	CHK(c2dWaitTimestamp(curTimestamp));
}

static void solid(PixmapPtr dest, int x1, int y1, int x2, int y2,
		uint32_t fill)
{
	C2D_RECT rect = {};

	rect.x = x1;
	rect.y = y1;
	rect.width = x2 - x1;
	rect.height = y2 - y1;

	CHK(c2dFillSurface(dest->id, fill, &rect));
}

static void copy(PixmapPtr dest, PixmapPtr src, int srcX, int srcY,
		int dstX, int dstY, int width, int height)
{
	C2D_OBJECT blit = {};

	blit.surface_id = src->id;
	blit.config_mask = C2D_SOURCE_RECT_BIT | C2D_TARGET_RECT_BIT |
			   C2D_NO_PIXEL_ALPHA_BIT | C2D_NO_BILINEAR_BIT |
			   C2D_NO_ANTIALIASING_BIT | C2D_ALPHA_BLEND_NONE;
	blit.next = NULL;

	blit.source_rect.x = FIXED(srcX);
	blit.source_rect.y = FIXED(srcY);
	blit.source_rect.width = FIXED(width);
	blit.source_rect.height = FIXED(height);

	blit.target_rect.x = FIXED(dstX);
	blit.target_rect.y = FIXED(dstY);
	blit.target_rect.width = FIXED(width);
	blit.target_rect.height = FIXED(height);

	CHK(c2dDraw(dest->id, 0, NULL, 0, 0, &blit, 1));
}

static void wait(uint32_t timestamp)
{
	CHK(c2dWaitTimestamp((c2d_ts_handle)timestamp));
}

static void flush(PixmapPtr dest, uint32_t *timestamp)
{
	c2d_ts_handle curTimestamp;
	CHK(c2dFlush(dest->id, &curTimestamp));
	*timestamp = (uint32_t)curTimestamp;
}
#else
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <freedreno_drmif.h>
#include <freedreno_ringbuffer.h>
#include "freedreno_z1xx.h"
#include "../fdre/util.h"
#include "../util/redump.h"

static struct fd_device *dev;
static struct fd_pipe *pipe;

static struct fd_ringbuffer *rings[4];
static struct fd_ringbuffer *ring;
static struct fd_bo *context_bos[3];
static int ring_idx;
static uint32_t input = 0;

/* matching buffer info:
                len:            00001000
                gpuaddr:        66142000

                len:            00009000
                gpuaddr:        66276000

                len:            00081000
                gpuaddr:        66280000
 */
#define STATE_SIZE  0x140
static const uint32_t initial_state[] = {
		 0x7c000275, 0x00000000, 0x00050005, 0x7c000129,
		 0x00000000, 0x7c00012a, 0x00000000, 0x7c00012b,
		 0x00000000, 0x7c00010f, 0x00000000, 0x7c000108,
		 0x00000000, 0x7c000109, 0x00000000, 0x7c000100,
		 0x00000000, 0x7c000101, 0x00000000, 0x7c000110,
		 0x00000000, 0x7c0001d0, 0x00000000, 0x7c0001d4,
		 0x00000000, 0x7c00010c, 0x00000000, 0x7c00010e,
		 0x00000000, 0x7c00010d, 0x00000000, 0x7c00010b,
		 0x00000000, 0x7c00010a, 0x00000000, 0x7c000111,
		 0x00000000, 0x7c000114, 0x00000000, 0x7c000115,
		 0x00000000, 0x7c000116, 0x00000000, 0x7c000117,
		 0x00000000, 0x7c000118, 0x00000000, 0x7c000119,
		 0x00000000, 0x7c00011a, 0x00000000, 0x7c00011b,
		 0x00000000, 0x7c00011c, 0x00000000, 0x7c00011d,
		 0x00000000, 0x7c00011e, 0x00000000, 0x7c00011f,
		 0x00000000, 0x7c000124, 0x00000000, 0x7c000125,
		 0x00000000, 0x7c000127, 0x00000000, 0x7c000128,
		 0x00000000, 0x7b00015e, 0x00000000, 0x7b000161,
		 0x00000000, 0x7b000165, 0x00000000, 0x7b000166,
		 0x00000000, 0x7b00016e, 0x00000000, 0x7c00016f,
		 0x00000000, 0x7b000165, 0x00000000, 0x7b000154,
		 0x00000000, 0x7b000155, 0x00000000, 0x7b000153,
		 0x00000000, 0x7b000168, 0x00000000, 0x7b000160,
		 0x00000000, 0x7b000150, 0x00000000, 0x7b000156,
		 0x00000000, 0x7b000157, 0x00000000, 0x7b000158,
		 0x00000000, 0x7b000159, 0x00000000, 0x7b000152,
		 0x00000000, 0x7b000151, 0x00000000, 0x7b000156,
		 0x00000000, 0x7c00017f, 0x00000000, 0x7c00017f,
		 0x00000000, 0x7c00017f, 0x00000000, 0x7c00017f,
		 0x00000000, 0x7f000000, 0x7f000000, 0x7c000129,
/**/	 0x66142000, 0x7c00012a, 0x66276000, 0x7c00012b,
/**/	 0x66280000, 0x7c0001e2, 0x00000000, 0x7c0001e3,
		 0x00000000, 0x7c0001e4, 0x00000000, 0x7c0001e5,
		 0x00000000, 0x7c0001e6, 0x00000000, 0x7c0001e7,
		 0x00000000, 0x7c0001c0, 0x00000000, 0x7c0001c1,
		 0x00000000, 0x7c0001c2, 0x00000000, 0x7c0001c3,
		 0x00000000, 0x7c0001c4, 0x00000000, 0x7c0001c5,
		 0x00000000, 0x7c0001c6, 0x00000000, 0x7c0001c7,
		 0x00000000, 0x7c0001c8, 0x00000000, 0x7c0001c9,
		 0x00000000, 0x7c0001ca, 0x00000000, 0x7c0001d1,
		 0x00000000, 0x7c0001d2, 0x00000000, 0x7c0001d4,
		 0x00000000, 0x7c0001d3, 0x00000000, 0x7c0001d5,
		 0x00000000, 0x7c0001d0, 0x00000000, 0x7c0001e0,
		 0x00000000, 0x7c0001e1, 0x00000000, 0x7c0001e2,
		 0x00000000, 0x7c0001e3, 0x00000000, 0x7c0001e4,
		 0x00000000, 0x7c0001e5, 0x00000000, 0x7c0001e6,
		 0x00000000, 0x7c0001e7, 0x00000000, 0x7c0001c0,
		 0x00000000, 0x7c0001c1, 0x00000000, 0x7c0001c2,
		 0x00000000, 0x7c0001c3, 0x00000000, 0x7c0001c4,
		 0x00000000, 0x7c0001c5, 0x00000000, 0x7c0001c6,
		 0x00000000, 0x7c0001c7, 0x00000000, 0x7c0001c8,
		 0x00000000, 0x7c0001c9, 0x00000000, 0x7c0001ca,
		 0x00000000, 0x7c0001d1, 0x00000000, 0x7c0001d2,
		 0x00000000, 0x7c0001d4, 0x00000000, 0x7c0001d3,
		 0x00000000, 0x7c0001d5, 0x00000000, 0x7c0001d0,
		 0x00000000, 0x7c0001e0, 0x00000000, 0x7c0001e1,
		 0x00000000, 0x7c0001e2, 0x00000000, 0x7c0001e3,
		 0x00000000, 0x7c0001e4, 0x00000000, 0x7c0001e5,
		 0x00000000, 0x7c0001e6, 0x00000000, 0x7c0001e7,
		 0x00000000, 0x7c0001c0, 0x00000000, 0x7c0001c1,
		 0x00000000, 0x7c0001c2, 0x00000000, 0x7c0001c3,
		 0x00000000, 0x7c0001c4, 0x00000000, 0x7c0001c5,
		 0x00000000, 0x7c0001c6, 0x00000000, 0x7c0001c7,
		 0x00000000, 0x7c0001c8, 0x00000000, 0x7c0001c9,
		 0x00000000, 0x7c0001ca, 0x00000000, 0x7c0001d1,
		 0x00000000, 0x7c0001d2, 0x00000000, 0x7c0001d4,
		 0x00000000, 0x7c0001d3, 0x00000000, 0x7c0001d5,
		 0x00000000, 0x7c0001d0, 0x00000000, 0x7c0001e0,
		 0x00000000, 0x7c0001e1, 0x00000000, 0x7c0001e2,
		 0x00000000, 0x7c0001e3, 0x00000000, 0x7c0001e4,
		 0x00000000, 0x7c0001e5, 0x00000000, 0x7c0001e6,
		 0x00000000, 0x7c0001e7, 0x00000000, 0x7c0001c0,
		 0x00000000, 0x7c0001c1, 0x00000000, 0x7c0001c2,
		 0x00000000, 0x7c0001c3, 0x00000000, 0x7c0001c4,
		 0x00000000, 0x7c0001c5, 0x00000000, 0x7c0001c6,
		 0x00000000, 0x7c0001c7, 0x00000000, 0x7c0001c8,
		 0x00000000, 0x7c0001c9, 0x00000000, 0x7c0001ca,
		 0x00000000, 0x7c0001d1, 0x00000000, 0x7c0001d2,
		 0x00000000, 0x7c0001d4, 0x00000000, 0x7c0001d3,
		 0x00000000, 0x7c0001d5, 0x00000000, 0x7f000000,
};

/* input fields seem to be enabled/disabled in a certain order: */
static uint32_t iena(uint32_t enable)
{
	input |= enable;
	return input;
}

static uint32_t idis(uint32_t enable)
{
	input &= ~enable;
	return input;
}

static inline void
OUT_RING(struct fd_ringbuffer *ring, unsigned data)
{
	fd_ringbuffer_emit(ring, data);
}

static inline void
OUT_RELOC(struct fd_ringbuffer *ring, struct fd_bo *bo)
{
	fd_ringbuffer_emit_reloc(ring, bo, 0);
}

static inline void
BEGIN_RING(int size)
{
}

static inline void
END_RING(void)
{
}

static void
ring_pre(struct fd_ringbuffer *ring)
{
	/* each packet seems to carry the address/size of next (w/ 0x00000000
	 * meaning no branch, next packet follows).  Each cmd packet is preceded
	 * by a dummy packet to give the size of the next..
	 */
	OUT_RING (ring, REGM(VGV3_NEXTADDR, 2));
	OUT_RING (ring, 0x00000000);	/* VGV3_NEXTADDR */
	OUT_RING (ring, 0x00000000);	/* VGV3_NEXTCMD, fixed up on flush */
	OUT_RING (ring, 0x7c000134);
	OUT_RING (ring, 0x00000000);

	OUT_RING (ring, REGM(VGV3_NEXTADDR, 2));
	OUT_RING (ring, 0x00000000);	/* fixed up by kernel */
	OUT_RING (ring, 0x00000000);	/* fixed up by kernel */
}

static void
ring_post(struct fd_ringbuffer *ring)
{
	/* This appears to be common end of packet: */
	OUT_RING(ring, REG(G2D_IDLE) | G2D_IDLE_IRQ | G2D_IDLE_BCFLUSH);
	OUT_RING(ring, REG(VGV3_LAST) | 0x0);
	OUT_RING(ring, REG(VGV3_LAST) | 0x0);
}

void
next_ring(void)
{
	int idx = ring_idx++ % ARRAY_SIZE(rings);

	if (rings[idx]) {
		ring = rings[idx];
		fd_ringbuffer_reset(ring);
		return;
	}

	ring = rings[idx] = fd_ringbuffer_new(pipe, 0x5000);

	memcpy(ring->start, initial_state, STATE_SIZE * sizeof(uint32_t));
	ring->cur = &ring->start[120];
	OUT_RELOC (ring, context_bos[0]);
	ring->cur = &ring->start[122];
	OUT_RELOC (ring, context_bos[1]);
	ring->cur = &ring->start[124];
	OUT_RELOC (ring, context_bos[2]);

	fd_ringbuffer_reset(ring);
}

static void init(void)
{
	int fd = drmOpen("kgsl", NULL);

	drmSetMaster(fd);

	dev = fd_device_new(fd);
	pipe = fd_pipe_new(dev, FD_PIPE_2D);

	context_bos[0] = fd_bo_new(dev, 0x1000, DRM_FREEDRENO_GEM_TYPE_KMEM);
	context_bos[1] = fd_bo_new(dev, 0x9000, DRM_FREEDRENO_GEM_TYPE_KMEM);
	context_bos[2] = fd_bo_new(dev, 0x81000, DRM_FREEDRENO_GEM_TYPE_KMEM);

	next_ring();

	ring_pre(ring);

	BEGIN_RING(8);
	OUT_RING  (ring, REGM(VGV1_DIRTYBASE, 3));
	OUT_RELOC (ring, context_bos[0]); /* VGV1_DIRTYBASE */
	OUT_RELOC (ring, context_bos[1]); /* VGV1_CBASE1 */
	OUT_RELOC (ring, context_bos[2]); /* VGV1_UBASE2 */
	OUT_RING  (ring, 0x11000000);
	OUT_RING  (ring, 0x10fff000);
	OUT_RING  (ring, 0x10ffffff);
	OUT_RING  (ring, 0x0d000404);
	END_RING  ();
}

#define xRGB 24
#define ARGB 32
#define A8   8
#define U1   1

typedef struct {
	int width, height, pitch, depth;
	struct fd_bo *bo;
} Pixmap, *PixmapPtr;

static PixmapPtr create_pixmap(uint32_t w, uint32_t h, uint32_t format)
{
	PixmapPtr pix = calloc(1, sizeof(*pix));
	int bpp;
	if ((format == xRGB) || (format == ARGB)) {
		bpp = 32;
	} else if (format == A8) {
		bpp = 8;
	} else {
		bpp = 1;
	}
	pix->width = w;
	pix->height = h;
	pix->pitch = ALIGN((w * bpp) / 8, 128);
	pix->depth = format;
	pix->bo = fd_bo_new(dev, pix->pitch * pix->height,
			DRM_FREEDRENO_GEM_TYPE_KMEM);

	return pix;
}

static inline void
out_dstpix(struct fd_ringbuffer *ring, PixmapPtr pix)
{
	struct fd_bo *bo = pix->bo;
	uint32_t w, h, p;

	w = pix->width;
	h = pix->height;

	/* pitch specified in units of 32 bytes, it appears.. not quite sure
	 * max size yet, but I think 11 or 12 bits..
	 */
	p = (pix->pitch / 32) & 0xfff;

	/* not quite sure if these first three dwords belong here, but all
	 * blits seem to start with these immediately before the dst surf
	 * parameters, so I'm putting them here for now
	 *
	 * Note that there are some similar dwords preceding src surf state
	 * although it varies slightly for composite (some extra bits set
	 * for src surface and no 0x11000000 like dword for mask surface..
	 * so this may need some shuffling around when I start playing with
	 * emitting dst/src/mask surf state in the corresponding Prepare
	 * fxns rather than for every blit..
	 */
	OUT_RING (ring, REG(G2D_ALPHABLEND) | 0x0);
	OUT_RING (ring, REG(G2D_BLENDERCFG) | 0x0);
	OUT_RING (ring, REG(G2D_GRADIENT) | 0x030000);
	OUT_RING (ring, REG(GRADW_TEXSIZE) | ((h & 0xfff) << 13) | (w & 0xfff));
	OUT_RING (ring, REG(G2D_CFG0) | p |
			((pix->depth == 8) ? 0xe000 : 0x7000));
	OUT_RING (ring, REGM(G2D_BASE0, 1));
	OUT_RELOC(ring, bo);
	OUT_RING (ring, REGM(GRADW_TEXBASE, 1));
	OUT_RELOC(ring, bo);
	OUT_RING (ring, REGM(GRADW_TEXCFG, 1));
	OUT_RING (ring, 0x40000000 | p |
			((pix->depth == 8) ? 0xe000 : 0x7000));
	OUT_RING (ring, 0xd5000000);
	OUT_RING (ring, REG(G2D_ALPHABLEND) | 0x0);
	OUT_RING (ring, REG(G2D_SCISSORX) | (w & 0xfff) << 12);
	OUT_RING (ring, REG(G2D_SCISSORY) | (h & 0xfff) << 12);
}

static inline void
out_srcpix(struct fd_ringbuffer *ring, PixmapPtr pix)
{
	struct fd_bo *bo = pix->bo;
	uint32_t w, h, p;

	w = pix->width;
	h = pix->height;

	/* pitch specified in units of 32 bytes, it appears.. not quite sure
	 * max size yet, but I think 11 or 12 bits..
	 */
	p = (pix->pitch / 32) & 0xfff;

	OUT_RING (ring, REGM(GRADW_TEXCFG, 3));
	OUT_RING (ring, 0x40000000 | p |   /* GRADW_TEXCFG */
			((pix->depth == 8) ? 0xe000 : 0x7000));
	// TODO check if 13 bit
	OUT_RING (ring, ((h & 0xfff) << 13) | (w & 0xfff)); /* GRADW_TEXSIZE */
	OUT_RELOC(ring, bo);               /* GRADW_TEXBASE */
}

static void solid(PixmapPtr dest, int x1, int y1, int x2, int y2,
		uint32_t fill)
{
	BEGIN_RING(23);
	out_dstpix(ring, dest);
	OUT_RING  (ring, REG(G2D_INPUT) | idis(G2D_INPUT_SCOORD1));
	OUT_RING  (ring, REG(G2D_INPUT) | iena(0x0));
	OUT_RING  (ring, REG(G2D_INPUT) | iena(G2D_INPUT_COLOR));
	OUT_RING  (ring, REG(G2D_CONFIG) | 0x0);
	OUT_RING  (ring, REGM(G2D_XY, 2));
	OUT_RING  (ring, ((x1 & 0xffff) << 16) | (y1 & 0xffff));   /* G2D_XY */
	OUT_RING  (ring, (((x2 - x1) & 0xffff) << 16) | ((y2 - y1) & 0xffff)); /* G2D_WIDTHHEIGHT */
	OUT_RING  (ring, REGM(G2D_COLOR, 1));
	OUT_RING  (ring, fill);
	END_RING  ();
}

static void copy(PixmapPtr dest, PixmapPtr src, int srcX, int srcY,
		int dstX, int dstY, int width, int height)
{
	BEGIN_RING(45);
	out_dstpix(ring, dest);
	OUT_RING  (ring, REGM(G2D_FOREGROUND, 2));
	OUT_RING  (ring, 0xff000000);      /* G2D_FOREGROUND */
	OUT_RING  (ring, 0xff000000);      /* G2D_BACKGROUND */
	OUT_RING  (ring, REG(G2D_BLENDERCFG) | 0x0);
	OUT_RING  (ring, 0xd0000000);
	out_srcpix(ring, src);
	OUT_RING  (ring, 0xd5000000);
	OUT_RING  (ring, 0xd0000000);
	OUT_RING  (ring, REG(G2D_INPUT) | iena(G2D_INPUT_SCOORD1));
	OUT_RING  (ring, REG(G2D_INPUT) | iena(0));
	OUT_RING  (ring, REG(G2D_INPUT) | idis(G2D_INPUT_COLOR));
	OUT_RING  (ring, REG(G2D_INPUT) | iena(G2D_INPUT_COPYCOORD));
	OUT_RING  (ring, 0xd0000000);
	OUT_RING  (ring, REG(G2D_INPUT) | iena(0));
	OUT_RING  (ring, REG(G2D_INPUT) | iena(0));
	OUT_RING  (ring, REG(G2D_INPUT) | iena(0));
	OUT_RING  (ring, REG(G2D_CONFIG) | G2D_CONFIG_SRC1); /* we don't read from dst */
	OUT_RING  (ring, REGM(G2D_XY, 3));
	OUT_RING  (ring, (dstX & 0xffff) << 16 | (dstY & 0xffff));    /* G2D_XY */
	OUT_RING  (ring, (width & 0xfff) << 16 | (height & 0xffff));  /* G2D_WIDTHHEIGHT */
	OUT_RING  (ring, (srcX & 0xffff) << 16 | (srcY & 0xffff));    /* G2D_SXY */
	OUT_RING  (ring, 0xd0000000);
	OUT_RING  (ring, 0xd0000000);
	OUT_RING  (ring, 0xd0000000);
	OUT_RING  (ring, 0xd0000000);
	OUT_RING  (ring, 0xd0000000);
	OUT_RING  (ring, 0xd0000000);
	END_RING  ();
}

static void wait(uint32_t timestamp)
{
	fd_pipe_wait(pipe, timestamp);
}

static void flush(PixmapPtr dest, uint32_t *timestamp)
{
	ring_post(ring);
	fd_ringbuffer_flush(ring);
	next_ring();
	fd_pipe_wait(pipe, fd_ringbuffer_timestamp(ring));
	ring_pre(ring);
}
#endif

static char * readline(int fd)
{
	static char buf[256];
	char *line = buf;
	while(1) {
		int ret = read(fd, line, 1);
		if (ret != 1) {
			ERROR_MSG("invalid read");
			end();
		}
		if (*line == '\n')
			break;
		line++;
	}
	*(++line) = '\0';
	return buf + 13;  // skip timestamp
}

static PixmapPtr get_pixmap(int fd)
{
	static struct {
		uint32_t ptr;
		PixmapPtr pix;
	} pixmaps[256];
	static int npixmaps = 0;

	char *line = readline(fd);
	if ((line == strstr(line, "EXA: SRC:")) ||
			(line == strstr(line, "EXA: DST:"))) {
		uint32_t ptr;
		int w, h, p, d, i;

		DEBUG_MSG("line: %s", line);

		line += 10;

		sscanf(line, "0x%x, %dx%d,%d,%d", &ptr, &w, &h, &p, &d);
		DEBUG_MSG("GOT: 0x%x, %dx%d,%d,%d", ptr, w, h, p, d);

		/* see if we can find the pixmap: */
		for (i = 0; i < npixmaps; i++)
			if (ptr == pixmaps[i].ptr)
				return pixmaps[i].pix;

		/* ok, then we need to create a new one: */
		DEBUG_MSG("creating pixmap: %dx%d (0x%x)", w, h, ptr);
		pixmaps[npixmaps].ptr = ptr;
		pixmaps[npixmaps].pix = create_pixmap(w, h, xRGB);

		return pixmaps[npixmaps++].pix;
	}

	ERROR_MSG("unexpected line: %s", line);
	end();

	return NULL;
}

int main(int argc, char **argv)
{
	uint32_t timestamp;
	PixmapPtr dest = NULL;
	int fd, ret;

	fd = open("replay.txt", 0);
	if (fd < 0) {
		ERROR_MSG("could not open");
		return -1;
	}

	DEBUG_MSG("----------------------------------------------------------------");
	RD_START("replay", "replay");

	init();

	while(1) {
		char *line = readline(fd);

		DEBUG_MSG("line: %s", line);

		if (strstr(line, "EXA: SOLID:") == line) {
			int x1, y1, x2, y2;
			uint32_t fill;

			sscanf(line, "EXA: SOLID: x1=%d\ty1=%d\tx2=%d\ty2=%d\tfill=%08x",
					&x1, &y1, &x2, &y2, &fill);

			DEBUG_MSG("GOT: SOLID: x1=%d\ty1=%d\tx2=%d\ty2=%d\tfill=%08x",
					x1, y1, x2, y2, fill);

			dest = get_pixmap(fd);

			solid(dest, x1, y1, x2, y2, fill);

		} else if (strstr(line, "EXA: COPY:") == line) {
			int srcX, srcY, dstX, dstY, width, height;
			PixmapPtr src;

			sscanf(line, "EXA: COPY: srcX=%d\tsrcY=%d\tdstX=%d\tdstY=%d\twidth=%d\theight=%d",
					&srcX, &srcY, &dstX, &dstY, &width, &height);

			DEBUG_MSG("GOT: COPY: srcX=%d\tsrcY=%d\tdstX=%d\tdstY=%d\twidth=%d\theight=%d",
					srcX, srcY, dstX, dstY, width, height);

			dest = get_pixmap(fd);
			src = get_pixmap(fd);

			copy(dest, src, srcX, srcY, dstX, dstY, width, height);

		} else if (strstr(line, "EXA: COMPOSITE:") == line) {
			ERROR_MSG("TODO");
			end();
		} else if (strstr(line, "EXA: WAIT:") == line) {
			DEBUG_MSG("wait, timestamp=%u", timestamp);
			wait(timestamp);
		} else if (strstr(line, "FLUSH:") == line) {
			flush(dest, &timestamp);
			DEBUG_MSG("flush, timestamp=%u", timestamp);
		} else {
			ERROR_MSG("unexpected line: %s", line);
			end();
		}
	}

	return 0;
}

static void end(void)
{
	RD_END();
	exit(-1);
}

#ifdef BIONIC
void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}
#endif
