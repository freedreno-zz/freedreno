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

#include "a2xx_reg.h"
#include "freedreno_a2xx_reg.h"
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
		uint32_t gpuaddr, void *hostptr, uint32_t size);
struct kgsl_bo * kgsl_bo_new(int fd, uint32_t size, uint32_t flags);
struct kgsl_bo * kgsl_bo_new_hostptr(int fd, void *hostptr, uint32_t size);
void kgsl_bo_del(struct kgsl_bo *bo);

/* ************************************************************************* */

struct kgsl_ringbuffer {
	uint32_t drawctxt_id;
	int fd;
	int size;
	struct kgsl_bo *bo;
	uint32_t *cur, *end, *start, *last_start;
	unsigned int timestamp;
};

struct kgsl_ringbuffer * kgsl_ringbuffer_new(int fd, uint32_t size,
		uint32_t drawctxt_id);
void kgsl_ringbuffer_del(struct kgsl_ringbuffer *ring);

void kgsl_ringbuffer_reset(struct kgsl_ringbuffer *ring);
int kgsl_ringbuffer_wait(struct kgsl_ringbuffer *ring);

int kgsl_ringbuffer_flush(struct kgsl_ringbuffer *ring);
int kgsl_ringbuffer_begin(struct kgsl_ringbuffer *ring, int dwords);

int kgsl_ringbuffer_emit_ib(struct kgsl_ringbuffer *ring,
		struct kgsl_ringbuffer *dst_ring);

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
	kgsl_ringbuffer_begin(ring, cnt+1);
	OUT_RING(ring, CP_TYPE0_PKT | ((cnt-1) << 16) | (regindx & 0x7FFF));
}

static inline void
OUT_PKT3(struct kgsl_ringbuffer *ring, uint8_t opcode, uint16_t cnt)
{
	kgsl_ringbuffer_begin(ring, cnt+1);
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

#endif /* KGSL_H_ */
