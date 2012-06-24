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
#include "a2xx_reg.h"
#include "freedreno.h"
#include "kgsl.h"
#include "bmp.h"

/* ************************************************************************* */

struct fd_shader {
	uint32_t bin[256];
	uint32_t sizedwords;
};

struct fd_state {

	int fd;

	uint32_t drawctxt_id;

	/* device properties: */
	struct kgsl_version version;
	struct kgsl_devinfo devinfo;
	struct kgsl_shadowprop shadowprop;

	/* shadow buffer.. not sure if we need this, but blob drivers mmap it.. */
	struct kgsl_bo *shadow;

	/* not sure if we really need 2nd ring buffer, but just copying what
	 * libGLESv2_adreno.so does..
	 */
	struct kgsl_ringbuffer *ring, *ring2;

	/* unknown buffers.. */
	struct kgsl_bo *bo6601a000, *bo660c8000, *bo660ca000, *bo661a6000;

	/* shaders: */
	struct fd_shader vertex_shader, fragment_shader;

	/* vertex shader used internally for blits/fills */
	struct fd_shader solid_vertex_shader;

	struct {
		struct kgsl_bo *bo;
		uint32_t sz;
	} attributes;

	struct {
		struct kgsl_bo *bo;
		uint32_t sz;
	} uniforms;

	struct {
		/* render target: */
		struct fd_surface *surface;
		/* bin size: */
		uint16_t bin_w, bin_h;
	} render_target;

	struct {
		struct {
			float x, y, z;
		} scale, offset;
	} viewport;
};

struct fd_surface {
	struct kgsl_bo *bo;
	uint32_t cpp;	/* bytes per pixel */
	uint32_t width, height, pitch;	/* width/height/pitch in pixels */
};

struct fd_shader_const {
	uint32_t gpuaddr, sz;
	enum COLORFORMATX format;
};

/* ************************************************************************* */

static void emit_mem_write(struct fd_state *state, uint32_t gpuaddr,
		void *data, uint32_t sizedwords)
{
	struct kgsl_ringbuffer *ring = state->ring;
	uint32_t *dwords = data;
	OUT_PKT3(ring, CP_MEM_WRITE, sizedwords+1);
	OUT_RING(ring, gpuaddr);
	while (sizedwords--)
		OUT_RING(ring, *(dwords++));
}

static void emit_pa_state(struct fd_state *state)
{
	struct kgsl_ringbuffer *ring = state->ring;
	struct fd_surface *surface = state->render_target.surface;

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SC_AA_CONFIG));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SC_AA_MASK));
	OUT_RING(ring, 0x0000ffff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SC_LINE_CNTL));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SU_LINE_CNTL));
	OUT_RING(ring, 0x00000008);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SU_POINT_SIZE));
	OUT_RING(ring, 0x00080008);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SC_WINDOW_OFFSET));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 7);
	OUT_RING(ring, CP_REG(REG_PA_CL_VPORT_XSCALE));
	OUT_RING(ring, f2d(state->viewport.scale.x));	/* PA_CL_VPORT_XSCALE */
	OUT_RING(ring, f2d(state->viewport.offset.x));	/* PA_CL_VPORT_XOFFSET */
	OUT_RING(ring, f2d(state->viewport.scale.y));	/* PA_CL_VPORT_YSCALE */
	OUT_RING(ring, f2d(state->viewport.offset.y));	/* PA_CL_VPORT_YOFFSET */
	OUT_RING(ring, f2d(state->viewport.scale.z));	/* PA_CL_VPORT_ZSCALE */
	OUT_RING(ring, f2d(state->viewport.offset.z));	/* PA_CL_VPORT_ZOFFSET */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_CL_CLIP_CNTL));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_CL_VTE_CNTL));
	OUT_RING(ring, 0x0000043f);

	OUT_PKT3(ring, CP_SET_CONSTANT, 5);
	OUT_RING(ring, CP_REG(REG_PA_CL_GB_VERT_CLIP_ADJ));
	OUT_RING(ring, f2d(31.0));		/* PA_CL_GB_VERT_CLIP_ADJ */ // XXX ???
	OUT_RING(ring, f2d(1.0));		/* PA_CL_GB_VERT_DISC_ADJ */
	OUT_RING(ring, f2d(31.0));		/* PA_CL_GB_HORZ_CLIP_ADJ */ // XXX ???
	OUT_RING(ring, f2d(1.0));		/* PA_CL_GB_HORZ_DISC_ADJ */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SU_VTX_CNTL));
	OUT_RING(ring, 0x00000001);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SU_SC_MODE_CNTL));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_PA_SC_WINDOW_SCISSOR_TL));
	OUT_RING(ring, xy2d(0,0));			/* PA_SC_WINDOW_SCISSOR_TL */
	OUT_RING(ring, xy2d(surface->width, /* PA_SC_WINDOW_SCISSOR_BR */
			surface->height));

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_PA_SC_SCREEN_SCISSOR_TL));
	OUT_RING(ring, xy2d(0,0));			/* PA_SC_SCREEN_SCISSOR_TL */
	OUT_RING(ring, xy2d(surface->width, /* PA_SC_SCREEN_SCISSOR_BR */
			surface->height));
}

/* val - shader linkage (I think..) */
static void emit_shader_const(struct fd_state *state, uint32_t val,
		struct fd_shader_const *consts, uint32_t n)
{
	uint32_t i;
	struct kgsl_ringbuffer *ring = state->ring;

	OUT_PKT3(ring, CP_SET_CONSTANT, 1 + (2 * n));
	OUT_RING(ring, (0x1 << 16) | (val & 0xffff));
	for (i = 0; i < n; i++) {
		OUT_RING(ring, consts[i].gpuaddr | consts[i].format);
		OUT_RING(ring, consts[i].sz);
	}
}

#if 0
static void emit_texture_const(struct fd_state *state)
{
	struct kgsl_ringbuffer *ring = state->ring;

}
#endif

static void emit_shader(struct fd_state *state, struct fd_shader *shader)
{
	struct kgsl_ringbuffer *ring = state->ring;
	uint32_t type = (shader == &state->fragment_shader) ? 1 : 0;
	uint32_t i;

	OUT_PKT3(ring, CP_IM_LOAD_IMMEDIATE, 2 + shader->sizedwords);
	OUT_RING(ring, type);
	OUT_RING(ring, shader->sizedwords);
	for (i = 0; i < shader->sizedwords; i++)
		OUT_RING(ring, shader->bin[i]);
}

static int attach_shader(struct fd_shader *shader,
		const void *bin, uint32_t sz)
{
	memset(shader, 0, sizeof(*shader));
	memcpy(shader->bin, bin, sz);
	shader->sizedwords = ALIGN(sz, 4) / 4;
	return 0;
}
const uint32_t solid_vertex_shader_bin[] = {
		0x00031003, 0x00001000, 0xc2000000, 0x00001004,
		0x00001000, 0xc4000000, 0x00000005, 0x00002000,
		0x00000000, 0x19a81000, 0x00390a88, 0x0000000c,
		0x140f803e, 0x00000000, 0xe2010100,
};

static float init_shader_const[] = {
		-1.000000, +1.000000, +1.000000, +1.100000,
		+1.000000, +1.000000, -1.000000, -1.100000,
		+1.000000, +1.100000, -1.100000, +1.000000,
		-1.000000, +1.000000, +1.000000, +1.000000,
		+1.000000, +1.000000, -1.000000, -1.000000,
		+1.000000, +1.000000, -1.000000, +1.000000,
		+0.000000, +0.000000, +1.000000, +0.000000,
		+0.000000, +1.000000, +1.000000, +1.000000,
};

/* ************************************************************************* */

static int getprop(int fd, enum kgsl_property_type type,
		void *value, int sizebytes)
{
	struct kgsl_device_getproperty req = {
			.type = type,
			.value = value,
			.sizebytes = sizebytes,
	};
	return ioctl(fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &req);
}

#define GETPROP(fd, prop, x) do { \
	if (getprop((fd), KGSL_PROP_##prop, &(x), sizeof(x))) {			\
		ERROR_MSG("failed to get property: " #prop);					\
		exit(-1);														\
	} } while (0)


struct fd_state * fd_init(void)
{
	struct kgsl_drawctxt_create req = {
			.flags = 0x2000, /* ??? */
	};
	struct fd_state *state;
	int fd, ret;

	fd = open("/dev/kgsl-3d0", O_RDWR);
	if (fd < 0) {
		ERROR_MSG("could not open kgsl-3d0 device: %d (%s)",
				fd, strerror(errno));
		return NULL;
	}

	state = calloc(1, sizeof(*state));
	assert(state);

	state->fd = fd;

	GETPROP(state->fd, VERSION,       state->version);
	GETPROP(state->fd, DEVICE_INFO,   state->devinfo);
	GETPROP(state->fd, DEVICE_SHADOW, state->shadowprop);

	INFO_MSG("Accel Info:");
	INFO_MSG(" Chip-id:         %d.%d.%d.%d",
			(state->devinfo.chip_id >> 24) & 0xff,
			(state->devinfo.chip_id >> 16) & 0xff,
			(state->devinfo.chip_id >>  8) & 0xff,
			(state->devinfo.chip_id >>  0) & 0xff);
	INFO_MSG(" Device-id:       %d", state->devinfo.device_id);
	INFO_MSG(" GPU-id:          %d", state->devinfo.gpu_id);
	INFO_MSG(" MMU enabled:     %d", state->devinfo.mmu_enabled);
	INFO_MSG(" GMEM Base addr:  0x%08x", state->devinfo.gmem_gpubaseaddr);
	INFO_MSG(" GMEM size:       0x%08x", state->devinfo.gmem_sizebytes);
	INFO_MSG( "Shadow:          %08x (size:%d, flags:%x)",
			state->shadowprop.gpuaddr, state->shadowprop.size,
			state->shadowprop.flags);
	INFO_MSG(" Driver version:  %d.%d",
			state->version.drv_major, state->version.drv_minor);
	INFO_MSG(" Device version:  %d.%d",
			state->version.dev_major, state->version.dev_minor);

	state->shadow = kgsl_bo_new_from_gpuaddr(state->fd,
			state->shadowprop.gpuaddr,
			state->shadowprop.size);

	//6601a000/00001000
	state->bo6601a000 = kgsl_bo_new(state->fd, 0x1000, 0);

	ret = ioctl(state->fd, IOCTL_KGSL_DRAWCTXT_CREATE, &req);
	if (ret) {
		ERROR_MSG("failed to allocate context: %d (%s)",
				ret, strerror(errno));
		fd_fini(state);
		return NULL;
	}

	state->drawctxt_id = req.drawctxt_id;

	//660a8000/00010000
	state->ring = kgsl_ringbuffer_new(state->fd, 0x10000,
			state->drawctxt_id);

	//660b8000/00010000
	state->ring2 = kgsl_ringbuffer_new(state->fd, 0x10000,
			state->drawctxt_id);

	//660c8000/00001000
	state->bo660c8000 = kgsl_bo_new(state->fd, 0x1000, 0);

	//660ca000/00040000
	state->bo660ca000 = kgsl_bo_new(state->fd, 0x40000, 0);

	//661a6000/00040000
	state->bo661a6000 = kgsl_bo_new(state->fd, 0x40000, 0);

	/* allocate bo to pass vertices: */
	state->attributes.bo = kgsl_bo_new(state->fd, 0x20000, 0);

	/* allocate bo to pass vertices: */
	// TODO currently seems like uniforms are passed in the
	// cmdstream, not by ptr.. but not sure if that is always
	// the case
	state->uniforms.bo = kgsl_bo_new(state->fd, 0x20000, 0);

	attach_shader(&state->solid_vertex_shader, solid_vertex_shader_bin,
			sizeof(solid_vertex_shader_bin));

	return state;
}

void fd_fini(struct fd_state *state)
{
	fd_surface_del(state, state->render_target.surface);
	kgsl_bo_del(state->shadow);
	kgsl_ringbuffer_del(state->ring);
	close(state->fd);
	free(state);
}

/* ************************************************************************* */

int fd_vertex_shader_attach_bin(struct fd_state *state,
		const void *bin, uint32_t sz)
{
	return attach_shader(&state->vertex_shader, bin, sz);
}

int fd_fragment_shader_attach_bin(struct fd_state *state,
		const void *bin, uint32_t sz)
{
	return attach_shader(&state->fragment_shader, bin, sz);
}

int fd_link(struct fd_state *state)
{
	ERROR_MSG("TODO");
	return 0;
}

int fd_attribute_pointer(struct fd_state *state, const char *name,
		uint32_t size, uint32_t count, const void *data)
{
	// XXX this is a bit hard coded.. need some samples with multiple
	// attribute pointers to figure out how to generalize this
	// NOTE for some reason the data seems to get rounded up to a
	// multiple of 8 32b words.
	uint32_t i;
	const uint32_t *d = data;
	uint32_t *a = state->attributes.bo->hostptr;

	state->attributes.sz = ALIGN(size * count, 8);

	for (i = 0; i < (size * count); i++)
		*(a++) = *(d++);

	/* zero pad up to multiple of 8 */
	for (; i < state->attributes.sz; i++)
		*(a++) = 0;

	return 0;
}

int fd_uniform_attach(struct fd_state *state, const char *name,
		uint32_t size, uint32_t count, const void *data)
{
	// XXX this is a bit hard coded.. need some samples with multiple
	// uniforms to figure out how to generalize this
	uint32_t i;
	const uint32_t *d = data;
	uint32_t *a = state->uniforms.bo->hostptr;

	state->uniforms.sz = size * count;

	for (i = 0; i < (size * count); i++)
		*(a++) = *(d++);

	return 0;
}

/* emit cmdstream to blit from GMEM back to the surface */
static void emit_gmem2mem(struct fd_state *state,
		struct fd_surface *surface)
{
	struct kgsl_ringbuffer *ring = state->ring;

	emit_shader_const(state, 0x9c, (struct fd_shader_const[]) {
			{ .format = COLORX_8, .gpuaddr = state->bo660c8000->gpuaddr, .sz = 48 },
		}, 1 );
	emit_shader(state, &state->solid_vertex_shader);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A220_PC_VERTEX_REUSE_BLOCK_CNTL));
	OUT_RING(ring, 0x0000028f);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_SQ_PROGRAM_CNTL));
	OUT_RING(ring, 0x10038001);

	emit_shader(state, &state->fragment_shader);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_2181));
	OUT_RING(ring, 0x00000004);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLORCONTROL));
	OUT_RING(ring, 0x00001c27);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SC_AA_MASK));
	OUT_RING(ring, 0x0000ffff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_DEPTHCONTROL));
	OUT_RING(ring, 0x00000008);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SU_SC_MODE_CNTL));
	OUT_RING(ring, 0x00080240);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_PA_SC_WINDOW_SCISSOR_TL));
	OUT_RING(ring, xy2d(0,0) | 0x80000000);	/* PA_SC_WINDOW_SCISSOR_TL | WINDOW_OFFSET_DISABLE */
	OUT_RING(ring, xy2d(surface->width, /* PA_SC_WINDOW_SCISSOR_BR */
			surface->height));

	OUT_PKT3(ring, CP_SET_CONSTANT, 5);
	OUT_RING(ring, CP_REG(REG_PA_CL_VPORT_XSCALE));
	OUT_RING(ring, f2d(state->viewport.scale.x));	/* PA_CL_VPORT_XSCALE */
	OUT_RING(ring, f2d(state->viewport.offset.x));	/* PA_CL_VPORT_XOFFSET */
	OUT_RING(ring, f2d(state->viewport.scale.y));	/* PA_CL_VPORT_YSCALE */
	OUT_RING(ring, f2d(state->viewport.offset.y));	/* PA_CL_VPORT_YOFFSET */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_CL_VTE_CNTL));
	OUT_RING(ring, 0x0000040f);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_CL_CLIP_CNTL));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_MODECONTROL));
	OUT_RING(ring, 0x0000006);

	OUT_PKT3(ring, CP_SET_CONSTANT, 6);
	OUT_RING(ring, CP_REG(REG_RB_COPY_CONTROL));
	OUT_RING(ring, 0x00000000);				/* RB_COPY_CONTROL */
	OUT_RING(ring, surface->bo->gpuaddr);	/* RB_COPY_DEST_BASE */
	OUT_RING(ring, surface->pitch >> 5);	/* RB_COPY_DEST_PITCH */
	OUT_RING(ring, 0x0003c108 | (COLORX_8_8_8_8 << 4));	/* RB_COPY_DEST_FORMAT */ // XXX
	OUT_RING(ring, 0x0000000);				/* RB_COPY_DEST_OFFSET */

	OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
	OUT_RING(ring, 0x0000000);

	OUT_PKT3(ring, CP_DRAW_INDX, 3);
	OUT_RING(ring, 0x00000000);		// XXX
	OUT_RING(ring, 0x00004088);		// XXX
	OUT_RING(ring, 0x00000003);		// XXX

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_MODECONTROL));
	OUT_RING(ring, 0x0000004);
}

int fd_clear(struct fd_state *state, uint32_t color)
{
	struct kgsl_ringbuffer *ring = state->ring;
	struct fd_surface *surface = state->render_target.surface;

	emit_shader_const(state, 0x9c, (struct fd_shader_const[]) {
			{ .format = COLORX_8, .gpuaddr = state->bo660c8000->gpuaddr, .sz = 48 },
		}, 1 );
	emit_shader(state, &state->solid_vertex_shader);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A220_PC_VERTEX_REUSE_BLOCK_CNTL));
	OUT_RING(ring, 0x0000028f);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_SQ_PROGRAM_CNTL));
	OUT_RING(ring, 0x10038001);

	emit_shader(state, &state->fragment_shader);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_2181));
	OUT_RING(ring, 0x00000004);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLORCONTROL));
	OUT_RING(ring, 0x00001c27);

	OUT_PKT0(ring, REG_TC_CNTL_STATUS, 1);
	OUT_RING(ring, 0x00000001);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_CLEAR_COLOR));
	OUT_RING(ring, color);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A220_RB_LRZ_VSC_CONTROL));
	OUT_RING(ring, 0x00000084);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COPY_CONTROL));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_DEPTH_CLEAR));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_DEPTHCONTROL));
	OUT_RING(ring, 0x00000008);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLOR_MASK));
	OUT_RING(ring, 0x0000000f);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SU_SC_MODE_CNTL));
	OUT_RING(ring, 0x00080240);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLORCONTROL));
	OUT_RING(ring, 0x00001c27);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SC_AA_MASK));
	OUT_RING(ring, 0x0000ffff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_PA_SC_WINDOW_SCISSOR_TL));
	OUT_RING(ring, xy2d(0,0) | 0x80000000);	/* PA_SC_WINDOW_SCISSOR_TL | WINDOW_OFFSET_DISABLE */
	OUT_RING(ring, xy2d(surface->width, /* PA_SC_WINDOW_SCISSOR_BR */
			surface->height));

	OUT_PKT3(ring, CP_SET_CONSTANT, 5);
	OUT_RING(ring, CP_REG(REG_PA_CL_VPORT_XSCALE));
	OUT_RING(ring, f2d(state->viewport.scale.x));	/* PA_CL_VPORT_XSCALE */
	OUT_RING(ring, f2d(state->viewport.offset.x));	/* PA_CL_VPORT_XOFFSET */
	OUT_RING(ring, f2d(state->viewport.scale.y));	/* PA_CL_VPORT_YSCALE */
	OUT_RING(ring, f2d(state->viewport.offset.y));	/* PA_CL_VPORT_YOFFSET */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_CL_VTE_CNTL));
	OUT_RING(ring, 0x0000043f);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_CL_CLIP_CNTL));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLOR_INFO));
	OUT_RING(ring, 0x200 | COLORX_8_8_8_8);

	OUT_PKT3(ring, CP_DRAW_INDX, 3);
	OUT_RING(ring, 0x00000000);		// XXX
	OUT_RING(ring, 0x00004088);		// XXX
	OUT_RING(ring, 0x00000003);		// XXX

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A220_RB_LRZ_VSC_CONTROL));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COPY_CONTROL));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_DEPTHCONTROL));
	OUT_RING(ring, 0x0070079c);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLOR_MASK));
	OUT_RING(ring, 0x0000000f);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SU_SC_MODE_CNTL));
	OUT_RING(ring, 0x00080000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLORCONTROL));
	OUT_RING(ring, 0x00001c27);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SC_AA_MASK));
	OUT_RING(ring, 0x0000ffff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_PA_SC_WINDOW_SCISSOR_TL));
	OUT_RING(ring, xy2d(0,0) | 0x80000000);	/* PA_SC_WINDOW_SCISSOR_TL | WINDOW_OFFSET_DISABLE */
	OUT_RING(ring, xy2d(surface->width, /* PA_SC_WINDOW_SCISSOR_BR */
			surface->height));

	OUT_PKT3(ring, CP_SET_CONSTANT, 5);
	OUT_RING(ring, CP_REG(REG_PA_CL_VPORT_XSCALE));
	OUT_RING(ring, f2d(state->viewport.scale.x));	/* PA_CL_VPORT_XSCALE */
	OUT_RING(ring, f2d(state->viewport.offset.x));	/* PA_CL_VPORT_XOFFSET */
	OUT_RING(ring, f2d(state->viewport.scale.y));	/* PA_CL_VPORT_YSCALE */
	OUT_RING(ring, f2d(state->viewport.offset.y));	/* PA_CL_VPORT_YOFFSET */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_CL_VTE_CNTL));
	OUT_RING(ring, 0x0000043f);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_CL_CLIP_CNTL));
	OUT_RING(ring, 0x00000000);

	emit_gmem2mem(state, surface);

	return kgsl_ringbuffer_flush(ring);
}

int fd_draw_arrays(struct fd_state *state, GLenum mode,
		uint32_t start, uint32_t count)
{
	struct kgsl_ringbuffer *ring = state->ring;
	struct fd_surface *surface = state->render_target.surface;
	uint32_t i;

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SC_AA_MASK));
	OUT_RING(ring, 0x0000ffff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_DEPTHCONTROL));
	OUT_RING(ring, 0x0070079c);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SU_SC_MODE_CNTL));
	OUT_RING(ring, 0x00080000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_PA_SC_WINDOW_SCISSOR_TL));
	OUT_RING(ring, xy2d(0,0) | 0x80000000);	/* PA_SC_WINDOW_SCISSOR_TL | WINDOW_OFFSET_DISABLE */
	OUT_RING(ring, xy2d(surface->width, /* PA_SC_WINDOW_SCISSOR_BR */
			surface->height));

	OUT_PKT3(ring, CP_SET_CONSTANT, 5);
	OUT_RING(ring, CP_REG(REG_PA_CL_VPORT_XSCALE));
	OUT_RING(ring, f2d(state->viewport.scale.x));	/* PA_CL_VPORT_XSCALE */
	OUT_RING(ring, f2d(state->viewport.offset.x));	/* PA_CL_VPORT_XOFFSET */
	OUT_RING(ring, f2d(state->viewport.scale.y));	/* PA_CL_VPORT_YSCALE */
	OUT_RING(ring, f2d(state->viewport.offset.y));	/* PA_CL_VPORT_YOFFSET */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_CL_VTE_CNTL));
	OUT_RING(ring, 0x0000043f);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_CL_CLIP_CNTL));
	OUT_RING(ring, 0x00000000);

	emit_shader_const(state, 0x78, (struct fd_shader_const[]) {
			{ .format = COLORX_8, .gpuaddr = state->attributes.bo->gpuaddr,
				.sz = state->attributes.sz * 4 },
		}, 1 );
	emit_shader(state, &state->vertex_shader);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A220_PC_VERTEX_REUSE_BLOCK_CNTL));
	OUT_RING(ring, 0x0000028f);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_SQ_PROGRAM_CNTL));
	OUT_RING(ring, 0x10038001);

	emit_shader(state, &state->fragment_shader);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_2181));
	OUT_RING(ring, 0x00000004);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLORCONTROL));
	OUT_RING(ring, 0x00001c27);

	OUT_PKT3(ring, CP_SET_CONSTANT, 1 + state->uniforms.sz);
	OUT_RING(ring, 0x00000480);		// XXX
	for (i = 0; i < state->uniforms.sz; i++)
		OUT_RING(ring, ((uint32_t *)(state->uniforms.bo->hostptr))[i]);

	OUT_PKT0(ring, REG_TC_CNTL_STATUS, 1);
	OUT_RING(ring, 0x00000001);

	OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
	OUT_RING(ring, 0x0000000);

	OUT_PKT3(ring, CP_DRAW_INDX, 3);
	OUT_RING(ring, 0x00000000);		// XXX
	OUT_RING(ring, 0x00004086);		// XXX
	OUT_RING(ring, 0x00000004);		// XXX

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_2010));
	OUT_RING(ring, 0x00000000);

	for (i = 0; i < 12; i++) {
		OUT_PKT3(ring, CP_EVENT_WRITE, 1);
		OUT_RING(ring, CACHE_FLUSH);
	}

	emit_gmem2mem(state, surface);

	return kgsl_ringbuffer_flush(ring);
}

int fd_flush(struct fd_state *state)
{
	ERROR_MSG("TODO");
	sleep(1);
	return 0;
}

/* ************************************************************************* */

struct fd_surface * fd_surface_new(struct fd_state *state,
		uint32_t width, uint32_t height)
{
	struct fd_surface *surface = calloc(1, sizeof(*surface));
	assert(surface);
	surface->width  = width;
	surface->height = height;
	surface->pitch  = ALIGN(width, 32);
	surface->cpp    = 4;

	// 64x64 -> 0x4000/0x4000 -> 0x0
	//       then eglMakeCurrent() -> 0x2000
	// 400x240 -> 0x61800/0x68000 -> 0x61800
	//       then eglMakeCurrent() -> 0x34000,0x40000,0x4000,0x10000
	// 500x240 -> 0x78000/0x80000 -> 0x8000
	// 400x340 -> 0x8A200/0x8f000 -> 0x4E00
	// 800x600 -> 0x1D4C00/0x1db000 -> 0x6400

	surface->bo = kgsl_bo_new(state->fd,
			surface->pitch * surface->height * surface->cpp, 0);
	return surface;
}

void fd_surface_del(struct fd_state *state, struct fd_surface *surface)
{
	if (!surface)
		return;
	if (state->render_target.surface == surface)
		state->render_target.surface = NULL;
	kgsl_bo_del(surface->bo);
	free(surface);
}

static void attach_render_target(struct fd_state *state,
		struct fd_surface *surface)
{
	uint32_t nx = 1, ny = 1;
	uint32_t bin_w, bin_h;
	uint32_t cpp = 4;  /* TODO don't assume 32bpp */

	state->render_target.surface = surface;

	bin_w = ALIGN(surface->width, 32);
	bin_h = ALIGN(surface->height, 32);

	while (true) {
		if ((bin_w * bin_h * cpp) < state->devinfo.gmem_sizebytes)
			break;

		ny++;
		bin_h = ALIGN(surface->height / ny, 32);

		if ((bin_w * bin_h * cpp) < state->devinfo.gmem_sizebytes)
			break;

		nx++;
		bin_w = ALIGN(surface->width / nx, 32);
	}

	DEBUG_MSG("using %d,%d bins of size %dx%d", nx, ny, bin_w, bin_h);

	assert(!(bin_h & ~(0x1f*32)));
	assert(!(bin_w & ~(0x1f*32)));

	state->render_target.bin_w = bin_w;
	state->render_target.bin_h = bin_h;
}

static void set_viewport(struct fd_state *state, uint32_t x, uint32_t y,
		uint32_t width, uint32_t height)
{
	float half_width = width * 0.5f;
	float half_height = height * 0.5f;

	state->viewport.scale.x = half_width;
	state->viewport.scale.y = -half_height;
	state->viewport.scale.z = 0.5;
	state->viewport.offset.x = half_width + x;
	state->viewport.offset.y = half_height + y;
	state->viewport.offset.z = 0.5;
}

void fd_make_current(struct fd_state *state,
		struct fd_surface *surface)
{
	struct kgsl_ringbuffer *ring = state->ring;

	attach_render_target(state, surface);
	set_viewport(state, 0, 0, surface->width, surface->height);

	emit_mem_write(state, state->bo660c8000->gpuaddr,
			init_shader_const, ARRAY_SIZE(init_shader_const));

	OUT_PKT0(ring, REG_TP0_CHICKEN, 1);
	OUT_RING(ring, 0x00000002);

	OUT_PKT3(ring, CP_INVALIDATE_STATE, 1);
	OUT_RING(ring, 0x00007fff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_2307));
	OUT_RING(ring, 0x00100020);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_SQ_PS_CONST));
	OUT_RING(ring, 0x000e0120);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_A220_PC_MAX_VTX_INDX));
	OUT_RING(ring, 0xffffffff);	/* A220_PC_MAX_VTX_INDX */
	OUT_RING(ring, 0x00000000);	/* VGT_MIN_VTX_INDX */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A220_PC_INDX_OFFSET));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A220_PC_VERTEX_REUSE_BLOCK_CNTL));
	OUT_RING(ring, 0x0000003b);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_2181));
	OUT_RING(ring, 0x00000004);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_SQ_INTERPOLATOR_CNTL));
	OUT_RING(ring, 0xffffffff);

	emit_pa_state(state);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_MODECONTROL));
	OUT_RING(ring, 0x00000004);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_BLEND_CONTROL));
	OUT_RING(ring, 0x00010001);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLORCONTROL));
	OUT_RING(ring, 0x00000020);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_SAMPLE_POS));
	OUT_RING(ring, 0x88888888);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLOR_DEST_MASK));
	OUT_RING(ring, 0xffffffff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COPY_DEST_FORMAT));
	OUT_RING(ring, 0x0003c000 | (COLORX_4_4_4_4 << 4));

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_SQ_WRAPPING_0));
	OUT_RING(ring, 0x00000000);	/* SQ_WRAPPING_0 */
	OUT_RING(ring, 0x00000000);	/* SQ_WRAPPING_1 */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_2281));
	OUT_RING(ring, 0x04000008);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_ALPHA_REF));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_DRAW_INIT_FLAGS, 1);
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_WAIT_REG_EQ, 4);
	OUT_RING(ring, 0x000005d0);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x5f601000);
	OUT_RING(ring, 0x00000001);

	OUT_PKT0(ring, REG_SQ_INST_STORE_MANAGMENT, 1);
	OUT_RING(ring, 0x00000180);

	OUT_PKT3(ring, CP_INVALIDATE_STATE, 1);
	OUT_RING(ring, 0x00000300);

	OUT_PKT3(ring, CP_SET_SHADER_BASES, 1);
	OUT_RING(ring, 0x80000180);

	/* not sure what this form of CP_SET_CONSTANT is.. */
	OUT_PKT3(ring, CP_SET_CONSTANT, 13);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x469c4000);
	OUT_RING(ring, 0x3f800000);
	OUT_RING(ring, 0x3f000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x40000000);
	OUT_RING(ring, 0x3f400000);
	OUT_RING(ring, 0x3ec00000);
	OUT_RING(ring, 0x3e800000);

#if 0 /* binning */
	OUT_PKT0(ring, REG_A220_VSC_BIN_SIZE, 1);
	OUT_RING(ring, (state->render_target.bin_h/32 << 5) |
			state->render_target.bin_w/32);

	OUT_PKT0(ring, REG_0c02, 1);
	OUT_RING(ring, state->bo660c8000->gpuaddr + 0x80);

	OUT_PKT0(ring, REG_0c04, 1);
	OUT_RING(ring, 0x00000000);

	OUT_PKT0(ring, REG_0c06, 24);
	OUT_RING(ring, 0x01100000);
	OUT_RING(ring, state->bo660ca000->gpuaddr);
	OUT_RING(ring, 0x00040000);
	OUT_RING(ring, 0x01100001);
	OUT_RING(ring, state->bo661a6000->gpuaddr);
	OUT_RING(ring, 0x00040000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
#else
	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_PA_SC_SCREEN_SCISSOR_TL));
	OUT_RING(ring, xy2d(0,0));			/* PA_SC_SCREEN_SCISSOR_TL */
	OUT_RING(ring, xy2d(surface->width, /* PA_SC_SCREEN_SCISSOR_BR */
			surface->height));
#endif

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLOR_MASK));
	OUT_RING(ring, 0x0000000f);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_DEPTHCONTROL));
	OUT_RING(ring, 0x0070079c);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_RB_STENCILREFMASK_BF));
	OUT_RING(ring, 0x00000000);		/* RB_STENCILREFMASK_BF */
	OUT_RING(ring, 0x00000000);		/* REG_210d */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLORCONTROL));
	OUT_RING(ring, 0x00001c27);

	OUT_PKT3(ring, CP_SET_CONSTANT, 5);
	OUT_RING(ring, CP_REG(REG_RB_BLEND_COLOR));
	OUT_RING(ring, 0x00000000);		/* RB_BLEND_COLOR */
	OUT_RING(ring, 0x00000000);		/* REG_2106 */
	OUT_RING(ring, 0x00000000);		/* REG_2107 */
	OUT_RING(ring, 0x000000ff);		/* REG_2108 */

	OUT_PKT3(ring, CP_SET_CONSTANT, 4);
	OUT_RING(ring, CP_REG(REG_RB_SURFACE_INFO));
#if 0 /* binning */
	OUT_RING(ring, state->render_target.bin_w);	/* RB_SURFACE_INFO */
	OUT_RING(ring, COLORX_8_8_8_8);	/* RB_COLOR_INFO */
	OUT_RING(ring, state->render_target.bin_w << 10);	/* RB_DEPTH_INFO */ // XXX ???
#else
	OUT_RING(ring, surface->width);	/* RB_SURFACE_INFO */
	OUT_RING(ring, 0x200 | COLORX_8_8_8_8);	/* RB_COLOR_INFO */
	OUT_RING(ring, surface->width << 10);	/* RB_DEPTH_INFO */ // XXX ???
#endif

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_SAMPLE_POS));
	OUT_RING(ring, 0x88888888);

	kgsl_ringbuffer_flush(ring);
}

int fd_dump_bmp(struct fd_surface *surface, const char *filename)
{
	return bmp_dump(surface->bo->hostptr,
			surface->width, surface->height,
			surface->pitch * surface->cpp,
			filename);
}
