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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "msm_kgsl.h"
#include "kgsl.h"

/* ************************************************************************* */

struct kgsl_bo * kgsl_bo_new_from_gpuaddr(int fd,
		uint32_t gpuaddr, uint32_t size)
{
	struct kgsl_bo *bo = calloc(1, sizeof(*bo));
	assert(bo);
	bo->fd = fd;
	bo->gpuaddr = gpuaddr;
	bo->size = size;
	bo->hostptr = mmap(NULL, size, PROT_WRITE|PROT_READ, MAP_SHARED, fd, gpuaddr);
	assert(bo->hostptr);
	return bo;
}

/* flags can be KGSL_MEMFLAGS_GPUREADONLY */
struct kgsl_bo * kgsl_bo_new(int fd, uint32_t size, uint32_t flags)
{
	struct kgsl_bo *bo;
	struct kgsl_gpumem_alloc req = {
			.size = size,
			.flags = flags,
	};
	int ret;

	ret = ioctl(fd, IOCTL_KGSL_GPUMEM_ALLOC, &req);
	assert(!ret);

	bo = kgsl_bo_new_from_gpuaddr(fd, req.gpuaddr, size);
	assert(bo);

	bo->free_gpuaddr = true;

	return bo;
}

void kgsl_bo_del(struct kgsl_bo *bo)
{
	if (!bo)
		return;

	if (bo->hostptr)
		munmap(bo->hostptr, bo->size);

	if (bo->free_gpuaddr) {
		struct kgsl_sharedmem_free req = {
				.gpuaddr = bo->gpuaddr,
		};
		int ret;

		ret = ioctl(bo->fd, IOCTL_KGSL_SHAREDMEM_FREE, &req);
		assert(!ret);
	}

	free(bo);
}

uint32_t kgsl_bo_gpuaddr(struct kgsl_bo *bo, void *hostptr)
{
	assert(hostptr >= bo->hostptr);
	assert(hostptr < (bo->hostptr + bo->size));
	return bo->gpuaddr + (hostptr - bo->hostptr);
}

/* ************************************************************************* */

struct kgsl_ringbuffer * kgsl_ringbuffer_new(int fd, uint32_t size,
		uint32_t drawctxt_id)
{
	struct kgsl_ringbuffer *ring = calloc(1, sizeof(*ring));
	assert(ring);
	ring->drawctxt_id = drawctxt_id;
	ring->fd = fd;
	ring->size = size;
	ring->bo = kgsl_bo_new(ring->fd, size, KGSL_MEMFLAGS_GPUREADONLY);
	ring->start = ring->bo->hostptr;
	ring->end = &(ring->start[size/4]);
	kgsl_ringbuffer_reset(ring);
	return ring;
}

void kgsl_ringbuffer_del(struct kgsl_ringbuffer *ring)
{
	if (!ring)
		return;
	kgsl_bo_del(ring->bo);
	free(ring);
}

void kgsl_ringbuffer_reset(struct kgsl_ringbuffer *ring)
{
	ring->cur = ring->last_start = ring->start;
}

int kgsl_ringbuffer_wait(struct kgsl_ringbuffer *ring)
{
	struct kgsl_device_waittimestamp req = {
			.timestamp = ring->timestamp,
			.timeout   = 5000,
	};
	int ret;

	ret = ioctl(ring->fd, IOCTL_KGSL_DEVICE_WAITTIMESTAMP, &req);
	if (ret)
		ERROR_MSG("waittimestamp failed! %d (%s)", ret, strerror(errno));

	return ret;
}

int kgsl_ringbuffer_flush(struct kgsl_ringbuffer *ring)
{
	struct kgsl_ibdesc ibdesc = {
			.gpuaddr     = kgsl_bo_gpuaddr(ring->bo, ring->last_start),
			.hostptr     = ring->last_start,
			.sizedwords  = ring->cur - ring->last_start,
	};
	struct kgsl_ringbuffer_issueibcmds req = {
			.drawctxt_id = ring->drawctxt_id,
			.ibdesc_addr = (unsigned long)&ibdesc,
			.numibs      = 1,
			.flags       = KGSL_CONTEXT_SUBMIT_IB_LIST,
	};
	int ret;

	ret = ioctl(ring->fd, IOCTL_KGSL_RINGBUFFER_ISSUEIBCMDS, &req);
	if (ret)
		ERROR_MSG("issueibcmds failed!  %d (%s)", ret, strerror(errno));

	ring->timestamp  = req.timestamp;
	ring->last_start = ring->cur;

	return ret;
}

int kgsl_ringbuffer_begin(struct kgsl_ringbuffer *ring, int dwords)
{
	int ret;
	if ((ring->cur + dwords) >= ring->end) {
		/* this probably won't really work if we have multiple tiles..
		 * need to re-think how this should work.
		 */
		WARN_MSG("unexpected flush");
		ret = kgsl_ringbuffer_flush(ring);
		kgsl_ringbuffer_reset(ring);
		return ret;
	}
	return 0;
}

/* emit branch to dst ring: */
int kgsl_ringbuffer_emit_ib(struct kgsl_ringbuffer *ring,
		struct kgsl_ringbuffer *dst_ring)
{
	OUT_PKT3(ring, CP_INDIRECT_BUFFER_PFD, 2);
	OUT_RING(ring, kgsl_bo_gpuaddr(dst_ring->bo, dst_ring->last_start));
	OUT_RING(ring, dst_ring->cur - dst_ring->last_start);
	return 0;
}
