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
#include <linux/fb.h>

#include "msm_kgsl.h"
#include "freedreno.h"
#include "program.h"
#include "kgsl.h"
#include "bmp.h"

/* ************************************************************************* */

struct fd_param {
	const char *name;
	/* user ptr and dimensions for passed in uniform/attribute:
	 *   elem_size - size of individual element in bytes
	 *   size      - number of elements per group
	 *   count     - number of groups
	 * so total size in bytes is elem_size * size * count
	 */
	const void *data;
	uint32_t elem_size, size, count;
};

struct fd_parameters {

	/* gpu buffer used for passing parameters by ptr to the gpu..
	 * it is used in a circular buffer fashion, wrapping around,
	 * so we don't immediately overwrite the parameters for the
	 * last draw cmd which the gpu may still be using:
	 */
	struct kgsl_bo *bo;
	uint32_t off;

	struct fd_param params[4];
	uint32_t nparams;
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

	/* primary cmdstream buffer with render commands: */
	struct kgsl_ringbuffer *ring;

	/* if render target needs to be broken into tiles/bins, this cmd
	 * buffer contains the per-tile setup, and then IB calls to the
	 * primary cmdstream buffer for each tile.  The primary cmdstream
	 * buffer is executed for each tile.
	 */
	struct kgsl_ringbuffer *ring_tile;

	/* program used internally for blits/fills */
	struct fd_program *solid_program;

	/* buffer for passing vertices to solid program */
	struct kgsl_bo *solid_const;

	/* shader program: */
	struct fd_program *program;

	struct fd_parameters attributes, uniforms;

	struct {
		/* render target: */
		struct fd_surface *surface;
		/* for now just keep things simple and split the
		 * render target into horizontal strips:
		 */
		uint16_t bin_h, nbins;
	} render_target;

	/* have there been any render cmds since last flush? */
	bool dirty;

	struct {
		struct {
			float x, y, z;
		} scale, offset;
	} viewport;

	GLenum cull_mode;

	uint32_t pa_su_sc_mode_cntl;
	uint32_t rb_colorcontrol;
	uint32_t rb_depthcontrol;

	/* framebuffer info, for presentation blit: */
	struct {
		struct fb_var_screeninfo var;
		struct fb_fix_screeninfo fix;
		struct fd_surface *surface;
		void *ptr;
	} fb;

	/* textures: */
	struct {
		struct fd_surface *textures[16];
		uint8_t min_filter, mag_filter;
		uint8_t clamp_x, clamp_y;
	} tex;
};

struct fd_surface {
	struct kgsl_bo *bo;
	uint32_t cpp;	/* bytes per pixel */
	uint32_t width, height, pitch;	/* width/height/pitch in pixels */
	enum COLORFORMATX color;
};

struct fd_shader_const {
	uint32_t gpuaddr, sz;
	enum COLORFORMATX format;
};

/* ************************************************************************* */
/* color format info */

static int color2cpp[] = {
		[COLORX_8_8_8_8] = 4,
		[COLORX_32_32_32_32_FLOAT] = 16,
};

static enum SURFACEFORMAT color2fmt[] = {
		[COLORX_4_4_4_4]           = FMT_4_4_4_4,
		[COLORX_1_5_5_5]           = FMT_1_5_5_5,
		[COLORX_5_6_5]             = FMT_5_6_5,
		[COLORX_8]                 = FMT_8,
		[COLORX_8_8]               = FMT_8_8,
		[COLORX_8_8_8_8]           = FMT_8_8_8_8,
		[COLORX_S8_8_8_8]          = FMT_8_8_8_8,
		[COLORX_16_FLOAT]          = FMT_16_FLOAT,
		[COLORX_16_16_FLOAT]       = FMT_16_16_FLOAT,
		[COLORX_16_16_16_16_FLOAT] = FMT_16_16_16_16_FLOAT,
		[COLORX_32_FLOAT]          = FMT_32_FLOAT,
		[COLORX_32_32_FLOAT]       = FMT_32_32_FLOAT,
		[COLORX_32_32_32_32_FLOAT] = FMT_32_32_32_32_FLOAT,
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
	OUT_RING(ring, f2d(1.0));		/* PA_CL_GB_VERT_CLIP_ADJ */ // XXX ???
	OUT_RING(ring, f2d(1.0));		/* PA_CL_GB_VERT_DISC_ADJ */
	OUT_RING(ring, f2d(1.0));		/* PA_CL_GB_HORZ_CLIP_ADJ */ // XXX ???
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
static void emit_shader_const(struct kgsl_ringbuffer *ring, uint32_t val,
		struct fd_shader_const *consts, uint32_t n)
{
	uint32_t i;

	OUT_PKT3(ring, CP_SET_CONSTANT, 1 + (2 * n));
	OUT_RING(ring, (0x1 << 16) | (val & 0xffff));
	for (i = 0; i < n; i++) {
		OUT_RING(ring, consts[i].gpuaddr | consts[i].format);
		OUT_RING(ring, consts[i].sz);
	}
}

const char *solid_vertex_shader_asm =
		"EXEC ADDR(0x3) CNT(0x1)                                 \n"
		"   (S)FETCH:	VERTEX	R1.xyz1 = R0.x                   \n"
		"        FMT_32_32_32_FLOAT UNSIGNED STRIDE(12) CONST(10)\n"
		"ALLOC COORD SIZE(0x0)                                   \n"
		"EXEC ADDR(0x4) CNT(0x1)                                 \n"
		"      ALU:	MAXv	export62 = R1, R1	; gl_Position    \n"
		"ALLOC PARAM/PIXEL SIZE(0x0)                             \n"
		"EXEC_END ADDR(0x5) CNT(0x0)                             \n"
		"NOP                                                     \n";

const char *solid_fragment_shader_asm =
		"ALLOC PARAM/PIXEL SIZE(0x0)                             \n"
		"EXEC_END ADDR(0x1) CNT(0x1)                             \n"
		"      ALU:	MAXv	export0 = C0, C0	; gl_FragColor   \n";

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
			state->shadowprop.gpuaddr, NULL,
			state->shadowprop.size);

	ret = ioctl(state->fd, IOCTL_KGSL_DRAWCTXT_CREATE, &req);
	if (ret) {
		ERROR_MSG("failed to allocate context: %d (%s)",
				ret, strerror(errno));
		goto fail;
	}

	state->drawctxt_id = req.drawctxt_id;

	state->ring = kgsl_ringbuffer_new(state->fd, 0x10000,
			state->drawctxt_id);

	state->ring_tile = kgsl_ringbuffer_new(state->fd, 0x10000,
			state->drawctxt_id);

	state->solid_const = kgsl_bo_new(state->fd, 0x1000, 0);

	/* allocate bo to pass vertices: */
	state->attributes.bo = kgsl_bo_new(state->fd, 0x20000, 0);

	/* allocate bo to pass uniforms: */
	state->uniforms.bo = kgsl_bo_new(state->fd, 0x20000, 0);

	state->program = fd_program_new();

	state->solid_program = fd_program_new();

	ret = fd_program_attach_asm(state->solid_program,
			FD_SHADER_VERTEX, solid_vertex_shader_asm);
	if (ret) {
		ERROR_MSG("failed to attach solid vertex shader: %d", ret);
		goto fail;
	}

	ret = fd_program_attach_asm(state->solid_program,
			FD_SHADER_FRAGMENT, solid_fragment_shader_asm);
	if (ret) {
		ERROR_MSG("failed to attach solid fragment shader: %d", ret);
		goto fail;
	}

	/* setup initial GL state: */
	state->cull_mode = GL_BACK;

	state->pa_su_sc_mode_cntl = PA_SU_SC_PROVOKING_VTX_LAST |
			PA_SU_SC_POLYMODE_FRONT_PTYPE(TRIANGLES) |
			PA_SU_SC_POLYMODE_BACK_PTYPE(TRIANGLES);
	state->rb_colorcontrol = 0x00000c07 |
			RB_COLORCONTROL_DITHER_ENABLE | RB_COLORCONTROL_BLEND_DISABLE;
	state->rb_depthcontrol = 0x0070078c | RB_DEPTH_CONTROL_FUNC(GL_LESS);

	state->tex.clamp_x = state->tex.clamp_y = SQ_TEX0_WRAP;
	state->tex.min_filter = state->tex.mag_filter = SQ_TEX3_XY_FILTER_POINT;

	/* open framebuffer for displaying results: */
	fd = open("/dev/fb0", O_RDWR);
	ret = ioctl(fd, FBIOGET_VSCREENINFO, &state->fb.var);
	if (ret) {
		ERROR_MSG("failed to get var: %d (%s)",
				ret, strerror(errno));
		goto fail;
	}
	ret = ioctl(fd, FBIOGET_FSCREENINFO, &state->fb.fix);
	if (ret) {
		ERROR_MSG("failed to get fix: %d (%s)",
				ret, strerror(errno));
		goto fail;
	}

	INFO_MSG("res %dx%d virtual %dx%d, line_len %d",
			state->fb.var.xres, state->fb.var.yres,
			state->fb.var.xres_virtual, state->fb.var.yres_virtual,
			state->fb.fix.line_length);

	state->fb.ptr = mmap(0,
			state->fb.var.yres_virtual * state->fb.fix.line_length,
			PROT_WRITE | PROT_READ,
			MAP_SHARED, fd, 0);
	if(state->fb.ptr == MAP_FAILED) {
		ERROR_MSG("mmap failed");
		goto fail;
	}

	return state;

fail:
	fd_fini(state);
	return NULL;
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

int fd_vertex_shader_attach_asm(struct fd_state *state, const char *src)
{
	return fd_program_attach_asm(state->program, FD_SHADER_VERTEX, src);
}

int fd_fragment_shader_attach_asm(struct fd_state *state, const char *src)
{
	return fd_program_attach_asm(state->program, FD_SHADER_FRAGMENT, src);
}

int fd_link(struct fd_state *state)
{
	ERROR_MSG("TODO");
	return 0;
}

static int attach_parameter(struct fd_parameters *parameter,
		const char *name, uint32_t size, uint32_t count, const void *data)
{
	struct fd_param *p = NULL;
	uint32_t i;

	/* if this param is already bound, just update it: */
	for (i = 0; i < parameter->nparams; i++) {
		p = &parameter->params[i];
		if (!strcmp(name, p->name)) {
			break;
		}
	}

	if (i == parameter->nparams) {
		if (parameter->nparams >= ARRAY_SIZE(parameter->params)) {
			ERROR_MSG("do many params, cannot bind %s", name);
			return -1;
		}
		p = &parameter->params[parameter->nparams++];
	}

	p->name  = name;
	p->elem_size = 4;  /* for now just 32bit types */
	p->size  = size;
	p->count = count;
	p->data  = data;

	return 0;
}

int fd_attribute_pointer(struct fd_state *state, const char *name,
		uint32_t size, uint32_t count, const void *data)
{
	return attach_parameter(&state->attributes, name, size, count, data);
}

int fd_uniform_attach(struct fd_state *state, const char *name,
		uint32_t size, uint32_t count, const void *data)
{
	return attach_parameter(&state->uniforms, name, size, count, data);
}

/* emit cmdstream to blit from GMEM back to the surface */
static void emit_gmem2mem(struct fd_state *state,
		struct kgsl_ringbuffer *ring,
		struct fd_surface *surface, uint32_t offset)
{
	emit_shader_const(ring, 0x9c, (struct fd_shader_const[]) {
			{ .format = COLORX_8, .gpuaddr = state->solid_const->gpuaddr, .sz = 48 },
		}, 1 );
	fd_program_emit_shader(state->solid_program, FD_SHADER_VERTEX, ring);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A220_PC_VERTEX_REUSE_BLOCK_CNTL));
	OUT_RING(ring, 0x0000028f);

	fd_program_emit_sq_program_cntl(state->solid_program, ring);

	fd_program_emit_shader(state->solid_program, FD_SHADER_FRAGMENT, ring);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_SQ_CONTEXT_MISC));
	OUT_RING(ring, 0x00000004);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLORCONTROL));
	OUT_RING(ring, state->rb_colorcontrol);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SC_AA_MASK));
	OUT_RING(ring, 0x0000ffff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_DEPTHCONTROL));
	OUT_RING(ring, 0x00000008);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SU_SC_MODE_CNTL));
	OUT_RING(ring, PA_SU_SC_PROVOKING_VTX_LAST |
			PA_SU_SC_POLYMODE_FRONT_PTYPE(TRIANGLES) |
			PA_SU_SC_POLYMODE_BACK_PTYPE(TRIANGLES));

	OUT_PKT3(ring, CP_SET_CONSTANT, 4);
	OUT_RING(ring, CP_REG(REG_PA_SC_WINDOW_OFFSET));
	OUT_RING(ring, 0x00000000);             /* PA_SC_WINDOW_OFFSET */
	OUT_RING(ring, xy2d(0,0) | 0x80000000); /* PA_SC_WINDOW_SCISSOR_TL | WINDOW_OFFSET_DISABLE */
	OUT_RING(ring, xy2d(surface->width,     /* PA_SC_WINDOW_SCISSOR_BR */
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
	OUT_RING(ring, 0x00000000);             /* RB_COPY_CONTROL */
	OUT_RING(ring, surface->bo->gpuaddr + offset); /* RB_COPY_DEST_BASE */
	OUT_RING(ring, surface->pitch >> 5);    /* RB_COPY_DEST_PITCH */
	OUT_RING(ring, 0x0003c108 | (surface->color << 4));	/* RB_COPY_DEST_FORMAT */ // XXX
	OUT_RING(ring, 0x00000000);             /* RB_COPY_DEST_OFFSET */

	OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
	OUT_RING(ring, 0x0000000);

	OUT_PKT3(ring, CP_DRAW_INDX, 3);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, DRAW(RECTLIST, AUTO_INDEX, IGN, IGNORE_VISIBILITY));
	OUT_RING(ring, 3);					/* NumIndices */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_MODECONTROL));
	OUT_RING(ring, 0x0000004);

	OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
	OUT_RING(ring, 0x0000000);
}

int fd_clear(struct fd_state *state, uint32_t color)
{
	struct kgsl_ringbuffer *ring = state->ring;
	struct fd_surface *surface = state->render_target.surface;

	state->dirty = true;

	emit_shader_const(ring, 0x9c, (struct fd_shader_const[]) {
			{ .format = COLORX_8, .gpuaddr = state->solid_const->gpuaddr, .sz = 48 },
		}, 1 );
	fd_program_emit_shader(state->solid_program, FD_SHADER_VERTEX, ring);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A220_PC_VERTEX_REUSE_BLOCK_CNTL));
	OUT_RING(ring, 0x0000028f);

	fd_program_emit_sq_program_cntl(state->solid_program, ring);

	fd_program_emit_shader(state->solid_program, FD_SHADER_FRAGMENT, ring);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_SQ_CONTEXT_MISC));
	OUT_RING(ring, 0x00000004);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLORCONTROL));
	OUT_RING(ring, state->rb_colorcontrol);

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
	OUT_RING(ring, state->pa_su_sc_mode_cntl);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLORCONTROL));
	OUT_RING(ring, state->rb_colorcontrol);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SC_AA_MASK));
	OUT_RING(ring, 0x0000ffff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_PA_SC_WINDOW_SCISSOR_TL));
	OUT_RING(ring, xy2d(0,0));	        /* PA_SC_WINDOW_SCISSOR_TL */
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
	OUT_RING(ring, 0x200 | surface->color);

	OUT_PKT3(ring, CP_DRAW_INDX, 3);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, DRAW(RECTLIST, AUTO_INDEX, IGN, IGNORE_VISIBILITY));
	OUT_RING(ring, 3);					/* NumIndices */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A220_RB_LRZ_VSC_CONTROL));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COPY_CONTROL));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_DEPTHCONTROL));
	OUT_RING(ring, state->rb_depthcontrol);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLOR_MASK));
	OUT_RING(ring, 0x0000000f);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SU_SC_MODE_CNTL));
	OUT_RING(ring, PA_SU_SC_PROVOKING_VTX_LAST);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLORCONTROL));
	OUT_RING(ring, state->rb_colorcontrol);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SC_AA_MASK));
	OUT_RING(ring, 0x0000ffff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_PA_SC_WINDOW_SCISSOR_TL));
	OUT_RING(ring, xy2d(0,0));          /* PA_SC_WINDOW_SCISSOR_TL */
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

	return 0;
}

int fd_cull(struct fd_state *state, GLenum mode)
{
	state->cull_mode = mode;
	return 0;
}

int fd_depth_func(struct fd_state *state, GLenum depth_func)
{
	state->rb_depthcontrol &= ~RB_DEPTHCONTROL_FUNC_MASK;
	state->rb_depthcontrol |= RB_DEPTH_CONTROL_FUNC(depth_func);
	return 0;
}

int fd_enable(struct fd_state *state, GLenum cap)
{
	/* note: some of this code makes assumptions that mode/func/etc is
	 * set before enabling, and that the previous state was disabled.
	 * But this isn't really intended to be a robust GL implementation,
	 * just a tool for figuring out the cmdstream..
	 */

	switch (cap) {
	case GL_CULL_FACE:
		if ((state->cull_mode == GL_FRONT) ||
				(state->cull_mode == GL_FRONT_AND_BACK)) {
			state->pa_su_sc_mode_cntl |= PA_SU_SC_CULL_FRONT;
		}
		if ((state->cull_mode == GL_BACK) ||
				(state->cull_mode == GL_FRONT_AND_BACK)) {
			state->pa_su_sc_mode_cntl |= PA_SU_SC_CULL_BACK;
		}
		return 0;
	case GL_POLYGON_OFFSET_FILL:
		state->pa_su_sc_mode_cntl |=
				(PA_SU_SC_POLY_OFFSET_FRONT | PA_SU_SC_POLY_OFFSET_BACK);
		return 0;
	case GL_BLEND:
		state->rb_colorcontrol &= ~RB_COLORCONTROL_BLEND_DISABLE;
		return 0;
	case GL_DEPTH_TEST:
		state->rb_depthcontrol |= RB_DEPTHCONTROL_ENABLE;
		return 0;
	case GL_DITHER:
		state->rb_colorcontrol |= RB_COLORCONTROL_DITHER_ENABLE;
		return 0;
	default:
		ERROR_MSG("unsupported cap: 0x%04x", cap);
		return -1;
	}
}

int fd_disable(struct fd_state *state, GLenum cap)
{
	switch (cap) {
	case GL_CULL_FACE:
		state->pa_su_sc_mode_cntl &=
				~(PA_SU_SC_CULL_FRONT | PA_SU_SC_CULL_BACK);
		return 0;
	case GL_POLYGON_OFFSET_FILL:
		state->pa_su_sc_mode_cntl &=
				~(PA_SU_SC_POLY_OFFSET_FRONT | PA_SU_SC_POLY_OFFSET_BACK);
		return 0;
	case GL_BLEND:
		state->rb_colorcontrol |= RB_COLORCONTROL_BLEND_DISABLE;
		return 0;
	case GL_DEPTH_TEST:
		state->rb_depthcontrol &= ~RB_DEPTHCONTROL_ENABLE;
		return 0;
	case GL_DITHER:
		state->rb_colorcontrol &= ~RB_COLORCONTROL_DITHER_ENABLE;
		return 0;
	default:
		ERROR_MSG("unsupported cap: 0x%04x", cap);
		return -1;
	}
}

static int set_filter(uint8_t *filter, GLint param)
{
	switch (param) {
	case GL_LINEAR:
		*filter = SQ_TEX3_XY_FILTER_BILINEAR;
		return 0;
	case GL_NEAREST:
		*filter = SQ_TEX3_XY_FILTER_POINT;
		return 0;
	default:
		ERROR_MSG("unsupported param: 0x%04x", param);
		return -1;
	}
}

static int set_clamp(uint8_t *clamp, GLint param)
{
	switch (param) {
	case GL_REPEAT:
		*clamp = SQ_TEX0_WRAP;
		return 0;
	case GL_MIRRORED_REPEAT:
		*clamp = SQ_TEX0_MIRROR;
		return 0;
	case GL_CLAMP_TO_EDGE:
		*clamp = SQ_TEX0_CLAMP_LAST_TEXEL;
		return 0;
	default:
		ERROR_MSG("unsupported param: 0x%04x", param);
		return -1;
	}
}

int fd_tex_param(struct fd_state *state, GLenum name, GLint param)
{
	switch (name) {
	case GL_TEXTURE_MAG_FILTER:
		return set_filter(&state->tex.mag_filter, param);
	case GL_TEXTURE_MIN_FILTER:
		return set_filter(&state->tex.min_filter, param);
	case GL_TEXTURE_WRAP_S:
		return set_clamp(&state->tex.clamp_x, param);
	case GL_TEXTURE_WRAP_T:
		return set_clamp(&state->tex.clamp_y, param);
	default:
		ERROR_MSG("unsupported name: 0x%04x", name);
		return -1;
	}
}

static void emit_attributes(struct fd_state *state,
		uint32_t start, uint32_t count)
{
	struct kgsl_bo *bo = state->attributes.bo;
	struct fd_shader_const shader_const[ARRAY_SIZE(state->attributes.params)];
	uint32_t n;

retry:
	for (n = 0; n < state->attributes.nparams; n++) {
		struct fd_param *p = &state->attributes.params[n];
		uint32_t group_size = p->elem_size * p->size;
		uint32_t total_size = group_size * count;
		uint32_t align_size = ALIGN(total_size, 32);
		uint32_t bo_off     = state->attributes.off;
		uint32_t data_off   = group_size * start;

		/* if we reach end of bo, then wrap-around: */
		if ((bo_off + align_size) > bo->size) {
			state->attributes.off = 0;
			n = 0;
			goto retry;
		}

		memcpy(bo->hostptr + bo_off, p->data + data_off, total_size);

		/* zero pad up to multiple of 32 */
		memset(bo->hostptr + bo_off + total_size, 0, align_size - total_size);

		state->attributes.off += align_size;

		shader_const[n].format  = COLORX_8;
		shader_const[n].gpuaddr = bo->gpuaddr + bo_off;
		shader_const[n].sz      = align_size;
	}

	if (n > 0) {
		emit_shader_const(state->ring, 0x78, shader_const, n);
	}
}

static void emit_uniforms(struct fd_state *state)
{
	struct kgsl_ringbuffer *ring = state->ring;
	uint32_t n, size = 0;

	for (n = 0; n < state->uniforms.nparams; n++) {
		struct fd_param *p = &state->uniforms.params[n];
		// NOTE: a 3x3 matrix seems to get rounded up to four
		// entries per row.. probably to meet some alignment
		// constraint:
		size += ALIGN(p->size, 4) * p->count;
	}

	if (size == 0)
		return;

	/* emit uniforms: */
	OUT_PKT3(ring, CP_SET_CONSTANT, 1 + size);

	// XXX hack, I guess I'll understand this value better once
	// I start working on the shaders.. note, the last byte appears
	// to be an offset..
	OUT_RING(ring, (state->uniforms.nparams == 1) ? 0x00000480 : 0x00000080);

	for (n = 0; n < state->uniforms.nparams; n++) {
		struct fd_param *p = &state->uniforms.params[n];
		const uint32_t *data = p->data;
		uint32_t i, j;
		for (i = 0; i < p->count; i++) {
			for (j = 0; j < p->size; j++) {
				OUT_RING(ring, *(data++));
			}
			/* zero pad, if needed: */
			for (; j < ALIGN(p->size, 4); j++) {
				OUT_RING(ring, 0);
			}
		}
	}
}

static void emit_textures(struct fd_state *state)
{
	struct kgsl_ringbuffer *ring = state->ring;
	uint32_t n;

	/* this isn't too user-friendly.. you cannot unbind texture
	 * N without unbinding N+1.  But I think we'd have to patch
	 * up shader otherwise.  For now the fdre tests just need to
	 * know to do the right thing.
	 */
	for (n = 0; n < ARRAY_SIZE(state->tex.textures) && state->tex.textures[n]; n++) {
		struct fd_surface *tex = state->tex.textures[n];

		OUT_PKT3(ring, CP_SET_CONSTANT, 7);
		OUT_RING(ring, 0x00010000);

		OUT_RING(ring, SQ_TEX0_PITCH(tex->pitch) |
				SQ_TEX0_CLAMP_X(state->tex.clamp_x) |
				SQ_TEX0_CLAMP_Y(state->tex.clamp_y));

		OUT_RING(ring, color2fmt[tex->color] | tex->bo->gpuaddr);

		OUT_RING(ring, SQ_TEX2_HEIGHT(tex->height) |
				SQ_TEX2_WIDTH(tex->width));

		/* NumFormat=0:RF, DstSelXYZW=XYZW, ExpAdj=0, MagFilt=MinFilt=0:Point,
		 * Mip=2:BaseMap
		 */
		OUT_RING(ring, 0x01001910 |  // XXX
				SQ_TEX3_XY_MAG_FILTER(state->tex.mag_filter) |
				SQ_TEX3_XY_MIN_FILTER(state->tex.min_filter));

		/* VolMag=VolMin=0:Point, MinMipLvl=0, MaxMipLvl=1, LodBiasH=V=0,
		 * Dim3d=0
		 */
		OUT_RING(ring, 0x00000000);  // XXX

		/* BorderColor=0:ABGRBlack, ForceBC=0:diable, TriJuice=0, Aniso=0,
		 * Dim=1:2d, MipPacking=0
		 */
		OUT_RING(ring, 0x00000200);  // XXX
	}
}

static void emit_cacheflush(struct fd_state *state)
{
	struct kgsl_ringbuffer *ring = state->ring;
	uint32_t i;

	for (i = 0; i < 12; i++) {
		OUT_PKT3(ring, CP_EVENT_WRITE, 1);
		OUT_RING(ring, CACHE_FLUSH);
	}
}

int fd_draw_arrays(struct fd_state *state, GLenum mode,
		uint32_t start, uint32_t count)
{
	struct kgsl_ringbuffer *ring = state->ring;
	struct fd_surface *surface = state->render_target.surface;

	state->dirty = true;

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SC_AA_MASK));
	OUT_RING(ring, 0x0000ffff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_DEPTHCONTROL));
	OUT_RING(ring, state->rb_depthcontrol);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_PA_SU_SC_MODE_CNTL));
	OUT_RING(ring, state->pa_su_sc_mode_cntl);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_PA_SC_WINDOW_SCISSOR_TL));
	OUT_RING(ring, xy2d(0,0));          /* PA_SC_WINDOW_SCISSOR_TL */
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

	emit_attributes(state, start, count);

	fd_program_emit_shader(state->program, FD_SHADER_VERTEX, ring);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A220_PC_VERTEX_REUSE_BLOCK_CNTL));
	// XXX this is not understood:
	switch (state->attributes.nparams) {
	case 3:
		OUT_RING(ring, 0x0000003b);
		break;
	default:
		OUT_RING(ring, 0x0000028f);
		break;
	}

	fd_program_emit_sq_program_cntl(state->program, ring);

	fd_program_emit_shader(state->program, FD_SHADER_FRAGMENT, ring);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_SQ_CONTEXT_MISC));
	OUT_RING(ring, 0x00000004);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLORCONTROL));
	OUT_RING(ring, state->rb_colorcontrol);

	emit_uniforms(state);

	emit_textures(state);

	OUT_PKT0(ring, REG_TC_CNTL_STATUS, 1);
	OUT_RING(ring, 0x00000001);

	OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
	OUT_RING(ring, 0x0000000);

	OUT_PKT3(ring, CP_DRAW_INDX, 3);
	OUT_RING(ring, 0x00000000);		/* viz query info. */
	switch (mode) {
	case GL_TRIANGLE_STRIP:
		OUT_RING(ring, DRAW(TRISTRIP, AUTO_INDEX, IGN, IGNORE_VISIBILITY));
		break;
	case GL_TRIANGLE_FAN:
		OUT_RING(ring, DRAW(TRIFAN, AUTO_INDEX, IGN, IGNORE_VISIBILITY));
		break;
	case GL_TRIANGLES:
		OUT_RING(ring, DRAW(TRILIST, AUTO_INDEX, IGN, IGNORE_VISIBILITY));
		break;
	default:
		ERROR_MSG("unsupported mode: %d", mode);
		break;
	}
	OUT_RING(ring, count);				/* NumIndices */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_2010));
	OUT_RING(ring, 0x00000000);

	emit_cacheflush(state);

	return 0;
}

int fd_swap_buffers(struct fd_state *state)
{
	struct fd_surface *surface = state->render_target.surface;
	char *dstptr = state->fb.ptr;
	char *srcptr = surface->bo->hostptr;
	uint32_t len = surface->pitch * surface->cpp;
	uint32_t i;

	if (len > state->fb.fix.line_length)
		len = state->fb.fix.line_length;

	fd_flush(state);

	/* if we are rendering to front-buffer, we can skip this */
	if (surface != state->fb.surface) {
		for (i = 0; i < surface->height; i++) {
			memcpy(dstptr, srcptr, len);
			dstptr += state->fb.fix.line_length;
			srcptr += len;
		}
	}

	return 0;
}

int fd_flush(struct fd_state *state)
{
	struct fd_surface *surface = state->render_target.surface;
	struct kgsl_ringbuffer *ring;

	if (!state->dirty)
		return 0;

	if (state->render_target.nbins == 1) {
		/* no binning needed, just emit the primary ringbuffer: */
		ring = state->ring;
		emit_gmem2mem(state, ring, surface, 0);
	} else {
		/* binning required, build cmds to setup for each tile in
		 * the tile ringbuffer, w/ IB's to the primary ringbuffer:
		 */
		uint32_t i, yoff = 0;
		ring = state->ring_tile;

		for (i = 0; i < state->render_target.nbins; i++) {
			uint32_t bin_w = surface->width;
			uint32_t bin_h = state->render_target.bin_h;
			uint32_t offset = surface->pitch * surface->cpp * yoff;

			/* clip bin dimensions: */
			bin_h = min(bin_h, surface->height - yoff);

			DEBUG_MSG("bin_h=%d, yoff=%d, offset=0x%08x, cpp=%d, pitch=%d",
					bin_h, yoff, offset, surface->cpp, surface->pitch);

			/* setup scissor/offset for current tile: */
			OUT_PKT3(ring, CP_SET_CONSTANT, 4);
			OUT_RING(ring, CP_REG(REG_PA_SC_WINDOW_OFFSET));
			OUT_RING(ring, (-yoff) << 16);      /* PA_SC_WINDOW_OFFSET */
			OUT_RING(ring, xy2d(0,0));	        /* PA_SC_WINDOW_SCISSOR_TL */
			OUT_RING(ring, xy2d(surface->width, /* PA_SC_WINDOW_SCISSOR_BR */
					surface->height));

			OUT_PKT3(ring, CP_SET_CONSTANT, 3);
			OUT_RING(ring, CP_REG(REG_PA_SC_SCREEN_SCISSOR_TL));
			OUT_RING(ring, xy2d(0,0));			/* PA_SC_SCREEN_SCISSOR_TL */
			OUT_RING(ring, xy2d(bin_w, bin_h)); /* PA_SC_SCREEN_SCISSOR_BR */

			/* emit IB to drawcmds: */
			kgsl_ringbuffer_emit_ib(ring, state->ring);

			/* emit gmem2mem to transfer tile back to system memory: */
			emit_gmem2mem(state, ring, surface, offset);

			yoff += bin_h;
		}
	}

	kgsl_ringbuffer_flush(ring);
	kgsl_ringbuffer_wait(ring);
	kgsl_ringbuffer_reset(state->ring);
	kgsl_ringbuffer_reset(state->ring_tile);

	state->dirty = false;

	return 0;
}

/* ************************************************************************* */

struct fd_surface * fd_surface_new_fmt(struct fd_state *state,
		uint32_t width, uint32_t height, enum COLORFORMATX color_format)
{
	struct fd_surface *surface;
	int cpp = color2cpp[color_format];

	if (!cpp) {
		ERROR_MSG("invalid color format: %d", color_format);
		return NULL;
	}

	surface = calloc(1, sizeof(*surface));
	assert(surface);
	surface->color  = color_format;
	surface->width  = width;
	surface->height = height;
	surface->pitch  = ALIGN(width, 32);
	surface->cpp    = cpp;

	surface->bo = kgsl_bo_new(state->fd,
			surface->pitch * surface->height * surface->cpp, 0);
	return surface;
}

struct fd_surface * fd_surface_new(struct fd_state *state,
		uint32_t width, uint32_t height)
{
	return fd_surface_new_fmt(state, width, height, COLORX_8_8_8_8);
}

/* get framebuffer surface, return width/height */
struct fd_surface * fd_surface_screen(struct fd_state *state,
		uint32_t *width, uint32_t *height)
{
	struct fd_surface *surface;

	if (!state->fb.surface) {
		surface = calloc(1, sizeof(*surface));
		assert(surface);

		/* TODO don't hardcode: */
		surface->color  = COLORX_8_8_8_8;
		surface->cpp    = color2cpp[surface->color];
		surface->width  = state->fb.var.xres;
		surface->height = state->fb.var.yres;
		surface->pitch  = state->fb.fix.line_length / surface->cpp;

		surface->bo = kgsl_bo_new_hostptr(state->fd, state->fb.ptr,
				state->fb.var.yres_virtual * state->fb.fix.line_length);

		state->fb.surface = surface;
	} else {
		surface = state->fb.surface;
	}

	if (width)
		*width = surface->width;

	if (height)
		*height = surface->height;

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

void fd_surface_upload(struct fd_surface *surface, const void *data)
{
	memcpy(surface->bo->hostptr, data, surface->bo->size);
}

/* use tex=NULL to clear */
void fd_set_texture(struct fd_state *state, uint32_t id,
		struct fd_surface *tex)
{
	assert(id < ARRAY_SIZE(state->tex.textures));
	state->tex.textures[id] = tex;
}

static void attach_render_target(struct fd_state *state,
		struct fd_surface *surface)
{
	uint32_t nbins = 1;
	uint32_t bin_w, bin_h;
	uint32_t cpp = color2cpp[surface->color];

	state->render_target.surface = surface;

	bin_w = ALIGN(surface->width, 32);
	bin_h = ALIGN(surface->height, 32);

	while (true) {
		if ((bin_w * bin_h * cpp) < state->devinfo.gmem_sizebytes)
			break;

		nbins++;
		bin_h = ALIGN(surface->height / nbins, 32);
	}

	if (nbins > 1) {
		state->pa_su_sc_mode_cntl |= PA_SU_SC_VTX_WINDOW_OFF_ENABLE;
	} else {
		state->pa_su_sc_mode_cntl &= ~PA_SU_SC_VTX_WINDOW_OFF_ENABLE;
	}

	INFO_MSG("using %d bins of size %dx%d", nbins, bin_w, bin_h);

//if we use hw binning, tile sizes (in multiple of 32) need to
//fit in 5 bits.. for now don't care because we aren't using
//that:
//	assert(!(bin_h/32 & ~0x1f));
//	assert(!(bin_w/32 & ~0x1f));

	state->render_target.nbins = nbins;
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

	emit_mem_write(state, state->solid_const->gpuaddr,
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
	OUT_RING(ring, CP_REG(REG_SQ_CONTEXT_MISC));
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
	OUT_RING(ring, state->rb_depthcontrol);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_RB_STENCILREFMASK_BF));
	OUT_RING(ring, 0x00000000);		/* RB_STENCILREFMASK_BF */
	OUT_RING(ring, 0x00000000);		/* REG_210d */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_COLORCONTROL));
	OUT_RING(ring, state->rb_colorcontrol);

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
	OUT_RING(ring, surface->color);	/* RB_COLOR_INFO */
	OUT_RING(ring, state->render_target.bin_w << 10);	/* RB_DEPTH_INFO */ // XXX ???
#else
	OUT_RING(ring, surface->width);	/* RB_SURFACE_INFO */
	OUT_RING(ring, 0x200 | surface->color);	/* RB_COLOR_INFO */
	OUT_RING(ring, surface->width << 10);	/* RB_DEPTH_INFO */ // XXX ???
#endif

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_RB_SAMPLE_POS));
	OUT_RING(ring, 0x88888888);

	kgsl_ringbuffer_flush(ring);
}

int fd_dump_hex(struct fd_surface *surface)
{
	uint32_t *dbuf = surface->bo->hostptr;
	float   *fbuf = surface->bo->hostptr;
	uint32_t i;

	for (i = 0; i < surface->bo->size / 4; i+=4) {
		printf("\t\t\t%08X:   %08x %08x %08x %08x\t\t %8.8f %8.8f %8.8f %8.8f\n",
				(unsigned int) i*4,
				dbuf[i], dbuf[i+1], dbuf[i+2], dbuf[i+3],
				fbuf[i], fbuf[i+1], fbuf[i+2], fbuf[i+3]);
	}

	return 0;
}

int fd_dump_bmp(struct fd_surface *surface, const char *filename)
{
	return bmp_dump(surface->bo->hostptr,
			surface->width, surface->height,
			surface->pitch * surface->cpp,
			filename);
}
