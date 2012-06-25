/*
 * Copyright (c) 2012 Rob Clark <robdclark@gmail.com>
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

#ifndef KGSL_H_
#define KGSL_H_

#include <stdint.h>

#include "adreno_pm4types.h"

#include "util.h"

#define LOG_DWORDS 0


/* ************************************************************************* */

struct kgsl_bo {
	int fd;
	uint32_t size;
	uint32_t gpuaddr;
	void *hostptr;
	bool free_gpuaddr;
};

struct kgsl_bo * kgsl_bo_new_from_gpuaddr(int fd,
		uint32_t gpuaddr, uint32_t size);
struct kgsl_bo * kgsl_bo_new(int fd, uint32_t size, uint32_t flags);
void kgsl_bo_del(struct kgsl_bo *bo);

/* ************************************************************************* */

struct kgsl_ringbuffer {
	uint32_t drawctxt_id;
	int fd;
	int size;
	struct kgsl_bo *bo;
	uint32_t *cur, *end, *start, *last_start;
};

struct kgsl_ringbuffer * kgsl_ringbuffer_new(int fd, uint32_t size,
		uint32_t drawctxt_id);
void kgsl_ringbuffer_del(struct kgsl_ringbuffer *ring);

int kgsl_ringbuffer_flush(struct kgsl_ringbuffer *ring);

static inline void
OUT_RING(struct kgsl_ringbuffer *ring, uint32_t data)
{
	if (LOG_DWORDS) {
		DEBUG_MSG("ring[%d]: OUT_RING   %04x:  %08x\n", ring->fd,
				(uint32_t)(ring->cur - ring->last_start), data);
	}
	*(ring->cur++) = data;
}

static inline void
OUT_PKT0(struct kgsl_ringbuffer *ring, uint16_t regindx, uint16_t cnt)
{
	OUT_RING(ring, CP_TYPE0_PKT | ((cnt-1) << 16) | (regindx & 0x7FFF));
}

static inline void
OUT_PKT3(struct kgsl_ringbuffer *ring, uint8_t opcode, uint16_t cnt)
{
	OUT_RING(ring, CP_TYPE3_PKT | ((cnt-1) << 16) | ((opcode & 0xFF) << 8));
}

/* convert float to dword */
static inline uint32_t f2d(float f)
{
	union {
		float f;
		uint32_t d;
	} u = {
		.f = f,
	};
	return u.d;
}

/* convert x,y to dword */
static inline uint32_t xy2d(uint16_t x, uint16_t y)
{
	return ((y & 0x3fff) << 16) | (x & 0x3fff);
}

/* registers that we have figured out but are not in kernel: */
#define REG_CLEAR_COLOR			0x220b
#define REG_PA_CL_VPORT_XOFFSET	0x2110
#define REG_PA_CL_VPORT_YSCALE		0x2111
#define REG_PA_CL_VPORT_YOFFSET	0x2112
#define REG_RB_COPY_DEST_BASE		0x2319
#define REG_RB_COPY_DEST_PITCH		0x231a
#define REG_RB_COPY_DEST_FORMAT	0x231b
#define REG_RB_COPY_DEST_OFFSET	0x231c  /* ?? */
#define REG_RB_COLOR_INFO			0x2001

#define REG_PA_SU_VTX_CNTL			0x2302
#define REG_PA_CL_GB_VERT_CLIP_ADJ	0x2303
#define REG_PA_CL_GB_VERT_DISC_ADJ	0x2304
#define REG_PA_CL_GB_HORZ_CLIP_ADJ	0x2305
#define REG_PA_CL_GB_HORZ_DISC_ADJ	0x2306

#define REG_RB_BLEND_COLOR			0x2105
#define REG_RB_ALPHA_REF			0x210e
#define REG_RB_BLEND_CONTROL		0x2201

#define REG_SQ_CONTEXT_MISC		0x2181

/* unnamed registers: */
#define REG_2307		0x2307
#define REG_2281		0x2281
#define REG_0c02		0x0c02
#define REG_0c04		0x0c04
#define REG_0c06		0x0c06
#define REG_210d		0x210d
#define REG_2010		0x2010

/*
 * Format for 2nd dword in CP_DRAW_INDX and friends:
 */

/* see VGT_PRIMITIVE_TYPE.PRIM_TYPE? */
#define PC_DI_PT_POINTLIST 1
#define PC_DI_PT_LINELIST  2
#define PC_DI_PT_LINESTRIP 3
#define PC_DI_PT_TRILIST   4
#define PC_DI_PT_TRIFAN    5
#define PC_DI_PT_TRISTRIP  6
#define PC_DI_PT_RECTLIST  8

/* see VGT:VGT_DRAW_INITIATOR.SOURCE_SELECT? */
#define PC_DI_SRC_SEL_IMMEDIATE 1
#define PC_DI_SRC_SEL_AUTO_INDEX 2

/* see VGT_DMA_INDEX_TYPE.INDEX_TYPE? */
#define PC_DI_INDEX_SIZE_IGN    0
#define PC_DI_INDEX_SIZE_16_BIT 0
#define PC_DI_INDEX_SIZE_32_BIT 1

#define PC_DI_IGNORE_VISIBILITY 0

#define DRAW(prim_type, source_select, index_size, vis_cull_mode) \
	(((PC_DI_PT_         ## prim_type)       <<  0) | \
	 ((PC_DI_SRC_SEL_    ## source_select)   <<  6) | \
	 ((PC_DI_INDEX_SIZE_ ## index_size & 1)  << 11) | \
	 ((PC_DI_INDEX_SIZE_ ## index_size >> 1) << 13) | \
	 ((PC_DI_            ## vis_cull_mode)   <<  9) | \
	 (1                                      << 14))

#endif /* KGSL_H_ */
