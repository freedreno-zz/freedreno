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

#include "util.h"
#include "msm_kgsl.h"
#include "freedreno.h"
#include "program.h"
#include "ring.h"
#include "ir.h"
#include "ws.h"
#include "bmp.h"


/* ************************************************************************* */

struct fd_param {
	const char *name;
	enum {
		FD_PARAM_ATTRIBUTE,
		FD_PARAM_ATTRIBUTE_VBO,
		FD_PARAM_TEXTURE,
		FD_PARAM_UNIFORM,
	} type;
	union {
		struct fd_bo *bo;         /* FD_PARAM_ATTRIBUTE_VBO */
		struct fd_surface *tex;   /* FD_PARAM_TEXTURE */
		struct { /* FD_PARAM_ATTRIBUTE and FD_PARAM_UNIFORM */
			/* user ptr and dimensions for passed in uniform/attribute:
			 *   elem_size - size of individual element in bytes
			 *   size      - number of elements per group
			 *   count     - number of groups
			 * so total size in bytes is elem_size * size * count
			 */
			const void *data;
			uint32_t elem_size, size, count;
		};
	};
};

#define MAX_PARAMS 32
struct fd_parameters {
	struct fd_param params[MAX_PARAMS];
	uint32_t nparams;
};

struct fd_state {

	struct fd_winsys *ws;

	/* device properties: */
	uint32_t gmemsize_bytes;
	uint32_t device_id;

	/* primary cmdstream buffer with render commands: */
	struct fd_ringbuffer *ring;
	struct fd_ringmarker *draw_start, *draw_end;

	/* if render target needs to be broken into tiles/bins, this cmd
	 * buffer contains the per-tile setup, and then IB calls to the
	 * primary cmdstream buffer for each tile.  The primary cmdstream
	 * buffer is executed for each tile.
	 */
	struct fd_ringbuffer *ring_tile;

	/* program used internally for blits/fills */
	struct fd_program *solid_program;

	/* buffer for passing vertices to solid program */
	struct fd_bo *solid_const;

	/* shader program: */
	struct fd_program *program;

	/* uniform related params: */
	struct {
		struct fd_parameters params;
	} uniforms;

	/* attribute related params: */
	struct {
		/* gpu buffer used for passing parameters by ptr to the gpu..
		 * it is used in a circular buffer fashion, wrapping around,
		 * so we don't immediately overwrite the parameters for the
		 * last draw cmd which the gpu may still be using:
		 */
		struct fd_bo *bo;
		uint32_t off;

		struct fd_parameters params;
	} attributes;

	/* texture related params: */
	struct {
		enum sq_tex_filter min_filter, mag_filter;
		enum sq_tex_clamp clamp_x, clamp_y;

		struct fd_parameters params;
	} textures;

	struct {
		/* render target: */
		struct fd_surface *surface;
		/* for now just keep things simple and split the
		 * render target into horizontal strips:
		 */
		uint16_t bin_h, nbins_y;
		uint16_t bin_w, nbins_x;
	} render_target;

	struct {
		uint32_t color;
		uint32_t stencil;
		float depth;
	} clear;

	/* have there been any render cmds since last flush? */
	bool dirty;

	struct {
		struct {
			float x, y, z;
		} scale, offset;
	} viewport;

	GLenum cull_mode;

	uint32_t pa_su_sc_mode_cntl;
	uint32_t rb_blendcontrol;
	uint32_t rb_colorcontrol;
	uint32_t rb_depthcontrol;
	uint32_t rb_stencilrefmask;
};

struct fd_shader_const {
	uint32_t offset, sz;
	struct fd_bo *bo;
	enum a2xx_colorformatx format;
};

/* ************************************************************************* */
/* color format info */

static int color2cpp[] = {
		[COLORX_8_8_8_8] = 4,
		[COLORX_32_32_32_32_FLOAT] = 16,
};

static enum a2xx_sq_surfaceformat color2fmt[] = {
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

/* some adreno register enum's map directly to GL enum's minus GL_NEVER: */
#define g2a(val) ((val) - GL_NEVER)

/* ************************************************************************* */

static void emit_mem_write(struct fd_state *state, struct fd_bo *bo,
		const void *data, uint32_t sizedwords)
{
	struct fd_ringbuffer *ring = state->ring;
	const uint32_t *dwords = data;
	OUT_PKT3(ring, CP_MEM_WRITE, sizedwords+1);
	OUT_RELOC(ring, bo, 0, 0);
	while (sizedwords--)
		OUT_RING(ring, *(dwords++));
}

static void emit_pa_state(struct fd_state *state)
{
	struct fd_ringbuffer *ring = state->ring;
	struct fd_surface *surface = state->render_target.surface;

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SC_AA_CONFIG));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SC_AA_MASK));
	OUT_RING(ring, 0x0000ffff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SC_LINE_CNTL));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SU_LINE_CNTL));
	OUT_RING(ring, A2XX_PA_SU_LINE_CNTL_WIDTH(1));

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SU_POINT_SIZE));
	OUT_RING(ring, A2XX_PA_SU_POINT_SIZE_HEIGHT(1) |
			A2XX_PA_SU_POINT_SIZE_WIDTH(1));

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SC_WINDOW_OFFSET));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 7);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_CL_VPORT_XSCALE));
	OUT_RING(ring, fui(state->viewport.scale.x));	/* PA_CL_VPORT_XSCALE */
	OUT_RING(ring, fui(state->viewport.offset.x));	/* PA_CL_VPORT_XOFFSET */
	OUT_RING(ring, fui(state->viewport.scale.y));	/* PA_CL_VPORT_YSCALE */
	OUT_RING(ring, fui(state->viewport.offset.y));	/* PA_CL_VPORT_YOFFSET */
	OUT_RING(ring, fui(state->viewport.scale.z));	/* PA_CL_VPORT_ZSCALE */
	OUT_RING(ring, fui(state->viewport.offset.z));	/* PA_CL_VPORT_ZOFFSET */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_CL_CLIP_CNTL));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_CL_VTE_CNTL));
	OUT_RING(ring, A2XX_PA_CL_VTE_CNTL_VTX_W0_FMT |
			A2XX_PA_CL_VTE_CNTL_VPORT_X_SCALE_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_X_OFFSET_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_Y_SCALE_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_Y_OFFSET_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_Z_SCALE_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_Z_OFFSET_ENA);

	OUT_PKT3(ring, CP_SET_CONSTANT, 5);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_CL_GB_VERT_CLIP_ADJ));
	OUT_RING(ring, fui(1.0));		/* PA_CL_GB_VERT_CLIP_ADJ */
	OUT_RING(ring, fui(1.0));		/* PA_CL_GB_VERT_DISC_ADJ */
	OUT_RING(ring, fui(1.0));		/* PA_CL_GB_HORZ_CLIP_ADJ */
	OUT_RING(ring, fui(1.0));		/* PA_CL_GB_HORZ_DISC_ADJ */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SU_VTX_CNTL));
	OUT_RING(ring, A2XX_PA_SU_VTX_CNTL_PIX_CENTER(PIXCENTER_OGL));

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SU_SC_MODE_CNTL));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SC_WINDOW_SCISSOR_TL));
	OUT_RING(ring, xy2d(0,0));			/* PA_SC_WINDOW_SCISSOR_TL */
	OUT_RING(ring, xy2d(surface->width, /* PA_SC_WINDOW_SCISSOR_BR */
			surface->height));

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SC_SCREEN_SCISSOR_TL));
	OUT_RING(ring, xy2d(0,0));			/* PA_SC_SCREEN_SCISSOR_TL */
	OUT_RING(ring, xy2d(surface->width, /* PA_SC_SCREEN_SCISSOR_BR */
			surface->height));
}

/* val - shader linkage (I think..) */
static void emit_shader_const(struct fd_ringbuffer *ring, uint32_t val,
		struct fd_shader_const *consts, uint32_t n)
{
	uint32_t i;

	OUT_PKT3(ring, CP_SET_CONSTANT, 1 + (2 * n));
	OUT_RING(ring, (0x1 << 16) | (val & 0xffff));
	for (i = 0; i < n; i++) {
		OUT_RELOC(ring, consts[i].bo, consts[i].offset, consts[i].format);
		OUT_RING (ring, consts[i].sz);
	}
}

const char *solid_vertex_shader_asm =
		"EXEC ADDR(0x3) CNT(0x1)                                 \n"
		"   (S)FETCH:	VERTEX	R1.xyz1 = R0.x FMT_32_32_32_FLOAT\n"
		"                       UNSIGNED STRIDE(12) CONST(26, 0) \n"
		"ALLOC POSITION SIZE(0x0)                                \n"
		"EXEC ADDR(0x4) CNT(0x1)                                 \n"
		"      ALU:	MAXv	export62 = R1, R1	; gl_Position    \n"
		"ALLOC PARAM/PIXEL SIZE(0x0)                             \n"
		"EXEC_END ADDR(0x5) CNT(0x0)                             \n"
		"NOP                                                     \n";

const char *solid_fragment_shader_asm =
		"ALLOC PARAM/PIXEL SIZE(0x0)                             \n"
		"EXEC_END ADDR(0x1) CNT(0x1)                             \n"
		"      ALU:	MAXv	export0 = C0, C0	; gl_FragColor   \n";

static const float init_shader_const[] = {
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

struct fd_state * fd_init(void)
{
	struct fd_state *state;
	uint64_t val;
	int ret;

	state = calloc(1, sizeof(*state));
	assert(state);

#ifdef HAVE_X11
	state->ws = fd_winsys_dri2_open();
	if (!state->ws)
		ERROR_MSG("failed to open dri2, trying fbdev");
#endif
	if (!state->ws)
		state->ws = fd_winsys_fbdev_open();

	fd_pipe_get_param(state->ws->pipe, FD_GMEM_SIZE, &val);
	state->gmemsize_bytes = val;

	fd_pipe_get_param(state->ws->pipe, FD_DEVICE_ID, &val);
	state->device_id = val;

	state->ring = fd_ringbuffer_new(state->ws->pipe, 0x10000);
	state->draw_start = fd_ringmarker_new(state->ring);
	state->draw_end = fd_ringmarker_new(state->ring);
	state->ring_tile = fd_ringbuffer_new(state->ws->pipe, 0x10000);

	state->solid_const = fd_bo_new(state->ws->dev, 0x1000, 0);

	/* allocate bo to pass vertices: */
	state->attributes.bo = fd_bo_new(state->ws->dev, 0x20000, 0);

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

	state->pa_su_sc_mode_cntl =
			A2XX_PA_SU_SC_MODE_CNTL_PROVOKING_VTX_LAST |
			A2XX_PA_SU_SC_MODE_CNTL_FRONT_PTYPE(PC_DRAW_POINTS) |
			A2XX_PA_SU_SC_MODE_CNTL_BACK_PTYPE(PC_DRAW_POINTS);
	state->rb_colorcontrol =
			A2XX_RB_COLORCONTROL_ROP_CODE(12) |
			A2XX_RB_COLORCONTROL_ALPHA_FUNC(g2a(GL_ALWAYS)) |
			A2XX_RB_COLORCONTROL_DITHER_MODE(DITHER_ALWAYS) |
			A2XX_RB_COLORCONTROL_BLEND_DISABLE;
	state->rb_depthcontrol =
			A2XX_RB_DEPTHCONTROL_Z_WRITE_ENABLE |
			A2XX_RB_DEPTHCONTROL_EARLY_Z_ENABLE |
			A2XX_RB_DEPTHCONTROL_ZFUNC(g2a(GL_LESS)) |
			A2XX_RB_DEPTHCONTROL_STENCILFUNC(g2a(GL_ALWAYS)) |
			A2XX_RB_DEPTHCONTROL_BACKFACE_ENABLE |
			A2XX_RB_DEPTHCONTROL_STENCILFUNC_BF(g2a(GL_ALWAYS));
	state->rb_stencilrefmask = 0xff000000 |
			A2XX_RB_STENCILREFMASK_STENCILWRITEMASK(0xff);

	state->textures.clamp_x = SQ_TEX_WRAP;
	state->textures.clamp_y = SQ_TEX_WRAP;
	state->textures.min_filter = SQ_TEX_FILTER_POINT;
	state->textures.mag_filter = SQ_TEX_FILTER_POINT;

	state->clear.depth = 1;
	state->clear.stencil = 0;
	state->clear.color = 0x00000000;

	state->rb_blendcontrol =
			A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND(FACTOR_ONE) |
			A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND(FACTOR_ONE) |
			A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND(FACTOR_ZERO) |
			A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND(FACTOR_ZERO);


	return state;

fail:
	fd_fini(state);
	return NULL;
}

void fd_fini(struct fd_state *state)
{
	fd_surface_del(state, state->render_target.surface);
	fd_ringbuffer_del(state->ring);
	fd_ringbuffer_del(state->ring_tile);
	state->ws->destroy(state->ws);
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
	/* nothing really to do yet */
	return 0;
}

int fd_set_program(struct fd_state *state, struct fd_program *program)
{
	state->program = program;
	return fd_link(state);
}

static struct fd_param * find_param(struct fd_parameters *params,
		const char *name)
{
	uint32_t i;
	struct fd_param *p;

	/* if this param is already bound, just update it: */
	for (i = 0; i < params->nparams; i++) {
		p = &params->params[i];
		if (!strcmp(name, p->name)) {
			return p;
		}
	}

	if (i == ARRAY_SIZE(params->params)) {
		ERROR_MSG("too many params, cannot bind %s", name);
		return NULL;
	}

	p = &params->params[params->nparams++];
	p->name = name;

	return p;
}

/* for VBO's */
struct fd_bo * fd_attribute_bo_new(struct fd_state *state,
		uint32_t size, const void *data)
{
	struct fd_bo *bo = fd_bo_new(state->ws->dev, size, 0); /* TODO read-only? */
	memcpy(fd_bo_map(bo), data, size);
	return bo;
}

int fd_attribute_bo(struct fd_state *state, const char *name,
		struct fd_bo * bo)
{
	struct fd_param *p = find_param(&state->attributes.params, name);
	if (!p)
		return -1;
	p->type = FD_PARAM_ATTRIBUTE_VBO;
	p->bo   = bo;
	return 0;
}

int fd_attribute_pointer(struct fd_state *state, const char *name,
		uint32_t size, uint32_t count, const void *data)
{
	struct fd_param *p = find_param(&state->attributes.params, name);
	if (!p)
		return -1;
	p->type = FD_PARAM_ATTRIBUTE;
	p->elem_size = 4;  /* for now just 32bit types */
	p->size  = size;
	p->count = count;
	p->data  = data;
	return 0;
}

int fd_uniform_attach(struct fd_state *state, const char *name,
		uint32_t size, uint32_t count, const void *data)
{
	struct fd_param *p = find_param(&state->uniforms.params, name);
	if (!p)
		return -1;
	p->type = FD_PARAM_UNIFORM;
	p->elem_size = 4;  /* for now just 32bit types */
	p->size  = size;
	p->count = count;
	p->data  = data;
	return 0;
}

/* use tex=NULL to clear */
int fd_set_texture(struct fd_state *state, const char *name,
		struct fd_surface *tex)
{
	struct fd_param *p = find_param(&state->textures.params, name);
	if (!p)
		return -1;
	p->type = FD_PARAM_TEXTURE;
	p->tex = tex;
	return 0;
}

/* emit cmdstream to blit from GMEM back to the surface */
static void emit_gmem2mem(struct fd_state *state,
		struct fd_ringbuffer *ring, struct fd_surface *surface,
		uint32_t xoff, uint32_t yoff)
{
	emit_shader_const(ring, 0x9c, (struct fd_shader_const[]) {
			{ .format = COLORX_8, .bo = state->solid_const, .sz = 48 },
		}, 1 );
	fd_program_emit_shader(state->solid_program, FD_SHADER_VERTEX, ring);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_VGT_VERTEX_REUSE_BLOCK_CNTL));
	OUT_RING(ring, 0x0000028f);

	fd_program_emit_sq_program_cntl(state->solid_program, ring);

	fd_program_emit_shader(state->solid_program, FD_SHADER_FRAGMENT, ring);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_SQ_CONTEXT_MISC));
	OUT_RING(ring, A2XX_SQ_CONTEXT_MISC_SC_SAMPLE_CNTL(CENTERS_ONLY));

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_COLORCONTROL));
	OUT_RING(ring, state->rb_colorcontrol);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SC_AA_MASK));
	OUT_RING(ring, 0x0000ffff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_DEPTHCONTROL));
	OUT_RING(ring, A2XX_RB_DEPTHCONTROL_EARLY_Z_ENABLE);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SU_SC_MODE_CNTL));
	OUT_RING(ring, A2XX_PA_SU_SC_MODE_CNTL_PROVOKING_VTX_LAST |
			A2XX_PA_SU_SC_MODE_CNTL_FRONT_PTYPE(PC_DRAW_TRIANGLES) |
			A2XX_PA_SU_SC_MODE_CNTL_BACK_PTYPE(PC_DRAW_TRIANGLES));

	OUT_PKT3(ring, CP_SET_CONSTANT, 4);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SC_WINDOW_OFFSET));
	OUT_RING(ring, 0x00000000);             /* PA_SC_WINDOW_OFFSET */
	OUT_RING(ring, xy2d(0,0) | A2XX_PA_SC_WINDOW_OFFSET_DISABLE); /* PA_SC_WINDOW_SCISSOR_TL */
	OUT_RING(ring, xy2d(surface->width,     /* PA_SC_WINDOW_SCISSOR_BR */
			surface->height));

	OUT_PKT3(ring, CP_SET_CONSTANT, 5);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_CL_VPORT_XSCALE));
	OUT_RING(ring, fui(state->viewport.scale.x));	/* PA_CL_VPORT_XSCALE */
	OUT_RING(ring, fui(state->viewport.offset.x));	/* PA_CL_VPORT_XOFFSET */
	OUT_RING(ring, fui(state->viewport.scale.y));	/* PA_CL_VPORT_YSCALE */
	OUT_RING(ring, fui(state->viewport.offset.y));	/* PA_CL_VPORT_YOFFSET */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_CL_VTE_CNTL));
	OUT_RING(ring, A2XX_PA_CL_VTE_CNTL_VTX_W0_FMT |
			A2XX_PA_CL_VTE_CNTL_VPORT_X_SCALE_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_X_OFFSET_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_Y_SCALE_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_Y_OFFSET_ENA);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_CL_CLIP_CNTL));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_MODECONTROL));
	OUT_RING(ring, A2XX_RB_MODECONTROL_EDRAM_MODE(EDRAM_COPY));

	OUT_PKT3(ring, CP_SET_CONSTANT, 6);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_COPY_CONTROL));
	OUT_RING(ring, 0x00000000);             /* RB_COPY_CONTROL */
	OUT_RELOC(ring, surface->bo, 0, 0);     /* RB_COPY_DEST_BASE */
	OUT_RING(ring, surface->pitch >> 5);    /* RB_COPY_DEST_PITCH */
	OUT_RING(ring, A2XX_RB_COPY_DEST_INFO_FORMAT(surface->color) |
			A2XX_RB_COPY_DEST_INFO_LINEAR |      /* RB_COPY_DEST_INFO */
			A2XX_RB_COPY_DEST_INFO_SWAP(1) |
			A2XX_RB_COPY_DEST_INFO_WRITE_RED |
			A2XX_RB_COPY_DEST_INFO_WRITE_GREEN |
			A2XX_RB_COPY_DEST_INFO_WRITE_BLUE |
			A2XX_RB_COPY_DEST_INFO_WRITE_ALPHA);
	OUT_RING(ring, A2XX_RB_COPY_DEST_OFFSET_X(xoff) | /* RB_COPY_DEST_OFFSET */
			A2XX_RB_COPY_DEST_OFFSET_Y(yoff));

	OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
	OUT_RING(ring, 0x0000000);

	OUT_PKT3(ring, CP_DRAW_INDX, 3);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, DRAW(DI_PT_RECTLIST, DI_SRC_SEL_AUTO_INDEX,
			INDEX_SIZE_IGN, IGNORE_VISIBILITY));
	OUT_RING(ring, 3);					/* NumIndices */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_MODECONTROL));
	OUT_RING(ring, A2XX_RB_MODECONTROL_EDRAM_MODE(COLOR_DEPTH));

	OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
	OUT_RING(ring, 0x0000000);
}

void fd_clear_color(struct fd_state *state, uint32_t color)
{
	state->clear.color = color;
}

void fd_clear_stencil(struct fd_state *state, uint32_t s)
{
	state->clear.stencil = s;
}

void fd_clear_depth(struct fd_state *state, float depth)
{
	state->clear.depth = depth;
}

int fd_clear(struct fd_state *state, GLbitfield mask)
{
	struct fd_ringbuffer *ring = state->ring;
	struct fd_surface *surface = state->render_target.surface;
	uint32_t reg;

	state->dirty = true;

	emit_shader_const(ring, 0x9c, (struct fd_shader_const[]) {
			{ .format = COLORX_8, .bo = state->solid_const, .sz = 48 },
		}, 1 );
	fd_program_emit_shader(state->solid_program, FD_SHADER_VERTEX, ring);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_VGT_VERTEX_REUSE_BLOCK_CNTL));
	OUT_RING(ring, 0x0000028f);

	fd_program_emit_sq_program_cntl(state->solid_program, ring);

	fd_program_emit_shader(state->solid_program, FD_SHADER_FRAGMENT, ring);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_SQ_CONTEXT_MISC));
	OUT_RING(ring, A2XX_SQ_CONTEXT_MISC_SC_SAMPLE_CNTL(CENTERS_ONLY));

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_COLORCONTROL));
	OUT_RING(ring, state->rb_colorcontrol);

	OUT_PKT0(ring, REG_A2XX_TC_CNTL_STATUS, 1);
	OUT_RING(ring, A2XX_TC_CNTL_STATUS_L2_INVALIDATE);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_CLEAR_COLOR));
	OUT_RING(ring, state->clear.color);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_A220_RB_LRZ_VSC_CONTROL));
	OUT_RING(ring, 0x00000084);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_COPY_CONTROL));
	reg = 0;
	if (mask & GL_DEPTH_BUFFER_BIT) {
		reg |= A2XX_RB_COPY_CONTROL_CLEAR_MASK(0xf) |
				A2XX_RB_COPY_CONTROL_DEPTH_CLEAR_ENABLE;
	}
	OUT_RING(ring, reg);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_DEPTH_CLEAR));
	if (state->rb_depthcontrol & A2XX_RB_DEPTHCONTROL_STENCIL_ENABLE) {
		/* DEPTHX_24_8 */
		reg = (((uint32_t)(0xffffff * state->clear.depth)) << 8) |
				(state->clear.stencil & 0xff);
	} else if (state->rb_depthcontrol & A2XX_RB_DEPTHCONTROL_Z_ENABLE) {
		/* DEPTHX_16 */
		reg = (uint32_t)(0xffffffff * state->clear.depth);
	} else {
		reg = 0;
	}
	OUT_RING(ring, reg);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_DEPTHCONTROL));
	reg = 0;
	if (mask & GL_DEPTH_BUFFER_BIT) {
		reg |= A2XX_RB_DEPTHCONTROL_ZFUNC(g2a(GL_ALWAYS)) |
				A2XX_RB_DEPTHCONTROL_Z_ENABLE |
				A2XX_RB_DEPTHCONTROL_Z_WRITE_ENABLE |
				A2XX_RB_DEPTHCONTROL_EARLY_Z_ENABLE;
	}
	if (mask & GL_STENCIL_BUFFER_BIT) {
		reg |= A2XX_RB_DEPTHCONTROL_STENCILFUNC(g2a(GL_ALWAYS)) |
				A2XX_RB_DEPTHCONTROL_STENCIL_ENABLE |
				A2XX_RB_DEPTHCONTROL_STENCILZPASS(STENCIL_REPLACE);
	}
	OUT_RING(ring, reg);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_COLOR_MASK));
	OUT_RING(ring, A2XX_RB_COLOR_MASK_WRITE_RED |
			A2XX_RB_COLOR_MASK_WRITE_GREEN |
			A2XX_RB_COLOR_MASK_WRITE_BLUE |
			A2XX_RB_COLOR_MASK_WRITE_ALPHA);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SU_SC_MODE_CNTL));
	OUT_RING(ring, A2XX_PA_SU_SC_MODE_CNTL_PROVOKING_VTX_LAST |
			A2XX_PA_SU_SC_MODE_CNTL_FRONT_PTYPE(PC_DRAW_TRIANGLES) |
			A2XX_PA_SU_SC_MODE_CNTL_BACK_PTYPE(PC_DRAW_TRIANGLES));

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_COLORCONTROL));
	OUT_RING(ring, state->rb_colorcontrol);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SC_AA_MASK));
	OUT_RING(ring, 0x0000ffff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SC_WINDOW_SCISSOR_TL));
	OUT_RING(ring, xy2d(0,0));	        /* PA_SC_WINDOW_SCISSOR_TL */
	OUT_RING(ring, xy2d(surface->width, /* PA_SC_WINDOW_SCISSOR_BR */
			surface->height));

	OUT_PKT3(ring, CP_SET_CONSTANT, 5);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_CL_VPORT_XSCALE));
	OUT_RING(ring, fui(state->viewport.scale.x));	/* PA_CL_VPORT_XSCALE */
	OUT_RING(ring, fui(state->viewport.offset.x));	/* PA_CL_VPORT_XOFFSET */
	OUT_RING(ring, fui(state->viewport.scale.y));	/* PA_CL_VPORT_YSCALE */
	OUT_RING(ring, fui(state->viewport.offset.y));	/* PA_CL_VPORT_YOFFSET */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_CL_VTE_CNTL));
	OUT_RING(ring, A2XX_PA_CL_VTE_CNTL_VTX_W0_FMT |
			A2XX_PA_CL_VTE_CNTL_VPORT_X_SCALE_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_X_OFFSET_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_Y_SCALE_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_Y_OFFSET_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_Z_SCALE_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_Z_OFFSET_ENA);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_CL_CLIP_CNTL));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_COLOR_INFO));
	OUT_RING(ring, 0x200 | surface->color);

	OUT_PKT3(ring, CP_DRAW_INDX, 3);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, DRAW(DI_PT_RECTLIST, DI_SRC_SEL_AUTO_INDEX,
			INDEX_SIZE_IGN, IGNORE_VISIBILITY));
	OUT_RING(ring, 3);					/* NumIndices */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_A220_RB_LRZ_VSC_CONTROL));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_COPY_CONTROL));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_DEPTHCONTROL));
	OUT_RING(ring, state->rb_depthcontrol);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SU_SC_MODE_CNTL));
	OUT_RING(ring, A2XX_PA_SU_SC_MODE_CNTL_PROVOKING_VTX_LAST);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_COLORCONTROL));
	OUT_RING(ring, state->rb_colorcontrol);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SC_AA_MASK));
	OUT_RING(ring, 0x0000ffff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SC_WINDOW_SCISSOR_TL));
	OUT_RING(ring, xy2d(0,0));          /* PA_SC_WINDOW_SCISSOR_TL */
	OUT_RING(ring, xy2d(surface->width, /* PA_SC_WINDOW_SCISSOR_BR */
			surface->height));

	OUT_PKT3(ring, CP_SET_CONSTANT, 5);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_CL_VPORT_XSCALE));
	OUT_RING(ring, fui(state->viewport.scale.x));	/* PA_CL_VPORT_XSCALE */
	OUT_RING(ring, fui(state->viewport.offset.x));	/* PA_CL_VPORT_XOFFSET */
	OUT_RING(ring, fui(state->viewport.scale.y));	/* PA_CL_VPORT_YSCALE */
	OUT_RING(ring, fui(state->viewport.offset.y));	/* PA_CL_VPORT_YOFFSET */

	return 0;
}

int fd_cull(struct fd_state *state, GLenum mode)
{
	state->cull_mode = mode;
	return 0;
}

int fd_depth_func(struct fd_state *state, GLenum depth_func)
{
	state->rb_depthcontrol &= ~A2XX_RB_DEPTHCONTROL_ZFUNC__MASK;
	state->rb_depthcontrol |= A2XX_RB_DEPTHCONTROL_ZFUNC(g2a(depth_func));
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
			state->pa_su_sc_mode_cntl |= A2XX_PA_SU_SC_MODE_CNTL_CULL_FRONT;
		}
		if ((state->cull_mode == GL_BACK) ||
				(state->cull_mode == GL_FRONT_AND_BACK)) {
			state->pa_su_sc_mode_cntl |= A2XX_PA_SU_SC_MODE_CNTL_CULL_BACK;
		}
		return 0;
	case GL_POLYGON_OFFSET_FILL:
		state->pa_su_sc_mode_cntl |=
				(A2XX_PA_SU_SC_MODE_CNTL_POLY_OFFSET_FRONT_ENABLE |
						A2XX_PA_SU_SC_MODE_CNTL_POLY_OFFSET_BACK_ENABLE);
		return 0;
	case GL_BLEND:
		state->rb_colorcontrol &= ~A2XX_RB_COLORCONTROL_BLEND_DISABLE;
		return 0;
	case GL_DEPTH_TEST:
		state->rb_depthcontrol |= A2XX_RB_DEPTHCONTROL_Z_ENABLE;
		return 0;
	case GL_STENCIL_TEST:
		state->rb_depthcontrol |= A2XX_RB_DEPTHCONTROL_STENCIL_ENABLE;
		return 0;
	case GL_DITHER:
		state->rb_colorcontrol |= A2XX_RB_COLORCONTROL_DITHER_MODE(DITHER_ALWAYS);
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
			~(A2XX_PA_SU_SC_MODE_CNTL_CULL_FRONT | A2XX_PA_SU_SC_MODE_CNTL_CULL_BACK);
		return 0;
	case GL_POLYGON_OFFSET_FILL:
		state->pa_su_sc_mode_cntl &=
			~(A2XX_PA_SU_SC_MODE_CNTL_POLY_OFFSET_FRONT_ENABLE |
					A2XX_PA_SU_SC_MODE_CNTL_POLY_OFFSET_BACK_ENABLE);
		return 0;
	case GL_BLEND:
		state->rb_colorcontrol |= A2XX_RB_COLORCONTROL_BLEND_DISABLE;
		return 0;
	case GL_DEPTH_TEST:
		state->rb_depthcontrol &= ~A2XX_RB_DEPTHCONTROL_Z_ENABLE;
		return 0;
	case GL_STENCIL_TEST:
		state->rb_depthcontrol &= ~A2XX_RB_DEPTHCONTROL_STENCIL_ENABLE;
		return 0;
	case GL_DITHER:
		state->rb_colorcontrol &= ~A2XX_RB_COLORCONTROL_DITHER_MODE(DITHER_ALWAYS);
		return 0;
	default:
		ERROR_MSG("unsupported cap: 0x%04x", cap);
		return -1;
	}
}

int fd_blend_func(struct fd_state *state, GLenum sfactor, GLenum dfactor)
{
	uint32_t bc = 0;

	/* sfactor					dfactor
	 * GL_ZERO					GL_ZERO						-> 0000 0000
	 * GL_ZERO					GL_ONE						-> 0100 0100
	 * GL_ONE					GL_ZERO						-> 0001 0001
	 * GL_DST_COLOR				GL_DST_COLOR	 			-> 0808 0808
	 * GL_ONE_MINUS_DST_COLOR	GL_ONE_MINUS_DST_COLOR		-> 0909 0909
	 * GL_DST_ALPHA				GL_ONE_MINUS_DST_ALPHA		-> 0b0a 0b0a
	 * GL_SRC_ALPHA_SATURATE	GL_CONSTANT_COLOR			-> 0c01 0c10  <-- are there some special cases?
	 * GL_CONSTANT_ALPHA		GL_ONE_MINUS_CONSTANT_ALPHA	-> 0f0e 0f0e
	 * GL_SRC_COLOR				GL_ONE_MINUS_SRC_COLOR		-> 0504 0504
	 * GL_SRC_ALPHA				GL_CONSTANT_COLOR			-> 0c06 0c06
	 * GL_ONE_MINUS_SRC_ALPHA	GL_ONE_MINUS_CONSTANT_ALPHA	-> 0f07 0f07
	 */

	switch (sfactor) {
	case GL_ZERO:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND(FACTOR_ZERO);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND(FACTOR_ZERO);
		break;
	case GL_ONE:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND(FACTOR_ONE);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND(FACTOR_ONE);
		break;
	case GL_SRC_COLOR:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND(FACTOR_SRC_COLOR);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND(FACTOR_SRC_COLOR);
		break;
	case GL_ONE_MINUS_SRC_COLOR:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND(FACTOR_ONE_MINUS_SRC_COLOR);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND(FACTOR_ONE_MINUS_SRC_COLOR);
		break;
	case GL_SRC_ALPHA:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND(FACTOR_SRC_ALPHA);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND(FACTOR_SRC_ALPHA);
		break;
	case GL_ONE_MINUS_SRC_ALPHA:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND(FACTOR_ONE_MINUS_SRC_ALPHA);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND(FACTOR_ONE_MINUS_SRC_ALPHA);
		break;
	case GL_DST_COLOR:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND(FACTOR_DST_COLOR);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND(FACTOR_DST_COLOR);
		break;
	case GL_ONE_MINUS_DST_COLOR:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND(FACTOR_ONE_MINUS_DST_COLOR);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND(FACTOR_ONE_MINUS_DST_COLOR);
		break;
	case GL_DST_ALPHA:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND(FACTOR_DST_ALPHA);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND(FACTOR_DST_ALPHA);
		break;
	case GL_ONE_MINUS_DST_ALPHA:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND(FACTOR_ONE_MINUS_DST_ALPHA);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND(FACTOR_ONE_MINUS_DST_ALPHA);
		break;
	default:
		ERROR_MSG("invalid sfactor: 0x%04x", sfactor);
		return -1;
	}

	switch (dfactor) {
	case GL_ZERO:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND(FACTOR_ZERO);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND(FACTOR_ZERO);
		break;
	case GL_ONE:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND(FACTOR_ONE);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND(FACTOR_ONE);
		break;
	case GL_SRC_COLOR:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND(FACTOR_SRC_COLOR);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND(FACTOR_SRC_COLOR);
		break;
	case GL_ONE_MINUS_SRC_COLOR:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND(FACTOR_ONE_MINUS_SRC_COLOR);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND(FACTOR_ONE_MINUS_SRC_COLOR);
		break;
	case GL_SRC_ALPHA:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND(FACTOR_SRC_ALPHA);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND(FACTOR_SRC_ALPHA);
		break;
	case GL_ONE_MINUS_SRC_ALPHA:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND(FACTOR_ONE_MINUS_SRC_ALPHA);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND(FACTOR_ONE_MINUS_SRC_ALPHA);
		break;
	case GL_DST_COLOR:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND(FACTOR_DST_COLOR);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND(FACTOR_DST_COLOR);
		break;
	case GL_ONE_MINUS_DST_COLOR:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND(FACTOR_ONE_MINUS_DST_COLOR);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND(FACTOR_ONE_MINUS_DST_COLOR);
		break;
	case GL_DST_ALPHA:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND(FACTOR_DST_ALPHA);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND(FACTOR_DST_ALPHA);
		break;
	case GL_ONE_MINUS_DST_ALPHA:
		bc |= A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND(FACTOR_ONE_MINUS_DST_ALPHA);
		bc |= A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND(FACTOR_ONE_MINUS_DST_ALPHA);
		break;
	default:
		ERROR_MSG("invalid dfactor: 0x%04x", sfactor);
		return -1;
	}

	state->rb_blendcontrol = bc;

	return 0;
}

int fd_stencil_func(struct fd_state *state, GLenum func,
		GLint ref, GLuint mask)
{
	state->rb_stencilrefmask &= ~(
			A2XX_RB_STENCILREFMASK_STENCILREF__MASK |
			A2XX_RB_STENCILREFMASK_STENCILMASK__MASK);
	state->rb_stencilrefmask |=
			A2XX_RB_STENCILREFMASK_STENCILREF(ref) |
			A2XX_RB_STENCILREFMASK_STENCILMASK(mask);
	state->rb_depthcontrol &= ~(
			A2XX_RB_DEPTHCONTROL_STENCILFUNC__MASK |
			A2XX_RB_DEPTHCONTROL_STENCILFUNC_BF__MASK);
	state->rb_depthcontrol |=
			A2XX_RB_DEPTHCONTROL_STENCILFUNC(g2a(func)) |
			A2XX_RB_DEPTHCONTROL_STENCILFUNC_BF(g2a(func));
	return 0;
}

static int set_stencil_op(enum adreno_stencil_op *rbop, GLenum glop)
{
	switch (glop) {
	case GL_KEEP:
		*rbop = STENCIL_KEEP;
		return 0;
	case GL_REPLACE:
		*rbop = STENCIL_REPLACE;
		return 0;
	case GL_INCR:
		*rbop = STENCIL_INCR_CLAMP;
		return 0;
	case GL_DECR:
		*rbop = STENCIL_DECR_CLAMP;
		return 0;
	case GL_INVERT:
		*rbop = STENCIL_INVERT;
		return 0;
	case GL_INCR_WRAP:
		*rbop = STENCIL_INCR_WRAP;
		return 0;
	case GL_DECR_WRAP:
		*rbop = STENCIL_DECR_WRAP;
		return 0;
	default:
		ERROR_MSG("unsupported stencil op: %04x", glop);
		return -1;
	}
}

int fd_stencil_op(struct fd_state *state, GLenum sfail,
		GLenum zfail, GLenum zpass)
{
	enum adreno_stencil_op rbsfail, rbzfail, rbzpass;

	if (set_stencil_op(&rbsfail, sfail) ||
			set_stencil_op(&rbzfail, zfail) ||
			set_stencil_op(&rbzpass, zpass))
		return -1;

	state->rb_depthcontrol &= ~(
			A2XX_RB_DEPTHCONTROL_STENCILFAIL__MASK |
			A2XX_RB_DEPTHCONTROL_STENCILZPASS__MASK |
			A2XX_RB_DEPTHCONTROL_STENCILZFAIL__MASK |
			A2XX_RB_DEPTHCONTROL_STENCILFAIL_BF__MASK |
			A2XX_RB_DEPTHCONTROL_STENCILZPASS_BF__MASK |
			A2XX_RB_DEPTHCONTROL_STENCILZFAIL_BF__MASK);

	state->rb_depthcontrol |=
			A2XX_RB_DEPTHCONTROL_STENCILFAIL(rbsfail) |
			A2XX_RB_DEPTHCONTROL_STENCILZPASS(rbzpass) |
			A2XX_RB_DEPTHCONTROL_STENCILZFAIL(rbzfail) |
			A2XX_RB_DEPTHCONTROL_STENCILFAIL_BF(rbsfail) |
			A2XX_RB_DEPTHCONTROL_STENCILZPASS_BF(rbzpass) |
			A2XX_RB_DEPTHCONTROL_STENCILZFAIL_BF(rbzfail);

	return 0;
}

int fd_stencil_mask(struct fd_state *state, GLuint mask)
{
	state->rb_stencilrefmask &= ~A2XX_RB_STENCILREFMASK_STENCILWRITEMASK__MASK;
	state->rb_stencilrefmask |= A2XX_RB_STENCILREFMASK_STENCILWRITEMASK(mask);
	return 0;
}

static int set_filter(enum sq_tex_filter *filter, GLint param)
{
	switch (param) {
	case GL_LINEAR:
		*filter = SQ_TEX_FILTER_BILINEAR;
		return 0;
	case GL_NEAREST:
		*filter = SQ_TEX_FILTER_POINT;
		return 0;
	default:
		ERROR_MSG("unsupported param: 0x%04x", param);
		return -1;
	}
}

static int set_clamp(enum sq_tex_clamp *clamp, GLint param)
{
	switch (param) {
	case GL_REPEAT:
		*clamp = SQ_TEX_WRAP;
		return 0;
	case GL_MIRRORED_REPEAT:
		*clamp = SQ_TEX_MIRROR;
		return 0;
	case GL_CLAMP_TO_EDGE:
		*clamp = SQ_TEX_CLAMP_LAST_TEXEL;
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
		return set_filter(&state->textures.mag_filter, param);
	case GL_TEXTURE_MIN_FILTER:
		return set_filter(&state->textures.min_filter, param);
	case GL_TEXTURE_WRAP_S:
		return set_clamp(&state->textures.clamp_x, param);
	case GL_TEXTURE_WRAP_T:
		return set_clamp(&state->textures.clamp_y, param);
	default:
		ERROR_MSG("unsupported name: 0x%04x", name);
		return -1;
	}
}

static int upload_attributes(struct fd_bo *bo, uint32_t off,
		struct fd_param *p, uint32_t start, uint32_t count)
{
	uint32_t group_size = p->elem_size * p->size;
	uint32_t total_size = group_size * count;
	uint32_t align_size = ALIGN(total_size, 32);
	uint32_t data_off   = group_size * start;

	if ((off + align_size) > fd_bo_size(bo))
		return -1;

	memcpy(fd_bo_map(bo) + off, p->data + data_off, total_size);

	/* zero pad up to multiple of 32 */
	memset(fd_bo_map(bo) + off + total_size, 0, align_size - total_size);

	return align_size;
}

static uint32_t emit_attributes(struct fd_state *state,
		uint32_t start, uint32_t count,
		uint32_t idx_size, const void *indices)
{
	struct fd_bo *bo = state->attributes.bo;
	struct fd_shader_const shader_const[MAX_PARAMS];
	struct ir_attribute **attributes;
	int n, attributes_count;
	uint32_t idx_offset = 0;

	attributes = fd_program_attributes(state->program,
			FD_SHADER_VERTEX, &attributes_count);
retry:
	for (n = 0; n < attributes_count; n++) {
		struct fd_param *p = find_param(&state->attributes.params,
				attributes[n]->name);

		if (p->type == FD_PARAM_ATTRIBUTE_VBO) {
			shader_const[n].offset = 0;
			shader_const[n].bo = p->bo;
			shader_const[n].sz = fd_bo_size(p->bo);
		} else {
			int align_size = upload_attributes(bo, state->attributes.off,
					p, start, indices ? p->count : count);

			/* if we reach end of bo, then wrap-around: */
			if (align_size < 0) {
				state->attributes.off = 0;
				n = 0;
				goto retry;
			}

			shader_const[n].offset = state->attributes.off;
			shader_const[n].bo = bo;
			shader_const[n].sz = align_size;

			state->attributes.off += align_size;
		}

		shader_const[n].format  = COLORX_8;
	}

	if (n > 0) {
		emit_shader_const(state->ring, 0x78, shader_const, n);
		if (indices) {
			uint32_t align_size = ALIGN(idx_size, 32);

			if ((state->attributes.off + align_size) > fd_bo_size(bo)) {
				state->attributes.off = 0;
				n = 0;
				goto retry;
			}

			memcpy(fd_bo_map(bo) + state->attributes.off, indices, idx_size);
			idx_offset = state->attributes.off;
			state->attributes.off += align_size;
		}
	}

	return idx_offset;
}

/* in the cmdstream, uniforms and conts are the same */
static void emit_uniconst(struct fd_state *state, enum fd_shader_type type,
		const void *data, uint32_t size, uint32_t count,
		uint32_t cstart, uint32_t num)
{
	struct fd_ringbuffer *ring = state->ring;
	uint32_t base = (type == FD_SHADER_VERTEX) ? 0x00000080 : 0x00000480;
	const uint32_t *dwords = data;
	uint32_t i, j;

	/* make sure it fits in the # of registers that shader is expecting */
	assert((ALIGN(size, 4) * count) == (num * 4));

	// NOTE: a 3x3 matrix seems to get rounded up to four
	// entries per row.. having one vec4 register per row
	// makes matrix multiplication sane

	OUT_PKT3(ring, CP_SET_CONSTANT, 1 + ALIGN(size, 4) * count);
	OUT_RING(ring, base + (cstart * 4));

	for (i = 0; i < count; i++) {
		for (j = 0; j < size; j++) {
			OUT_RING(ring, *(dwords++));
		}
		/* zero pad, if needed: */
		for (; j < ALIGN(size, 4); j++) {
			OUT_RING(ring, 0);
		}
	}
}

static void emit_uniforms(struct fd_state *state, enum fd_shader_type type)
{
	struct ir_uniform **uniforms;
	int n, uniforms_count;

	uniforms = fd_program_uniforms(state->program, type, &uniforms_count);

	for (n = 0; n < uniforms_count; n++) {
		struct fd_param *p = find_param(&state->uniforms.params,
				uniforms[n]->name);
		emit_uniconst(state, type, p->data, p->size, p->count,
				uniforms[n]->cstart, uniforms[n]->num);
	}
}

static void emit_constants(struct fd_state *state, enum fd_shader_type type)
{
	struct ir_const **consts;
	int n, consts_count;

	consts = fd_program_consts(state->program, type, &consts_count);

	for (n = 0; n < consts_count; n++) {
		emit_uniconst(state, type, consts[n]->val, 4, 1, consts[n]->cstart, 1);
	}
}

static void emit_textures(struct fd_state *state)
{
	struct fd_ringbuffer *ring = state->ring;
	struct ir_sampler **samplers;
	int n, samplers_count;

	samplers = fd_program_samplers(state->program,
			FD_SHADER_FRAGMENT, &samplers_count);

	for (n = 0; n < samplers_count; n++) {
		struct fd_param *p = find_param(&state->textures.params,
				samplers[n]->name);

		OUT_PKT3(ring, CP_SET_CONSTANT, 7);
		OUT_RING(ring, 0x00010000 + (0x6 * n));

		OUT_RING(ring,
				A2XX_SQ_TEX_0_PITCH(p->tex->pitch) |
				A2XX_SQ_TEX_0_CLAMP_X(state->textures.clamp_x) |
				A2XX_SQ_TEX_0_CLAMP_Y(state->textures.clamp_y));

		OUT_RELOC(ring, p->tex->bo, 0, color2fmt[p->tex->color]);

		OUT_RING(ring,
				A2XX_SQ_TEX_2_HEIGHT(p->tex->height - 1) |
				A2XX_SQ_TEX_2_WIDTH(p->tex->width - 1));

		/* NumFormat=0:RF, DstSelXYZW=XYZW, ExpAdj=0, MagFilt=MinFilt=0:Point,
		 * Mip=2:BaseMap
		 */
		OUT_RING(ring,
				A2XX_SQ_TEX_3_SWIZ_X(SQ_TEX_X) |
				A2XX_SQ_TEX_3_SWIZ_Y(SQ_TEX_Y) |
				A2XX_SQ_TEX_3_SWIZ_Z(SQ_TEX_Z) |
				A2XX_SQ_TEX_3_SWIZ_W(SQ_TEX_W) |
				A2XX_SQ_TEX_3_XY_MAG_FILTER(state->textures.mag_filter) |
				A2XX_SQ_TEX_3_XY_MIN_FILTER(state->textures.min_filter));

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
	struct fd_ringbuffer *ring = state->ring;
	uint32_t i;

	for (i = 0; i < 12; i++) {
		OUT_PKT3(ring, CP_EVENT_WRITE, 1);
		OUT_RING(ring, CACHE_FLUSH);
	}
}

static int draw_impl(struct fd_state *state, GLenum mode,
		GLint first, GLsizei count, GLenum type, const GLvoid *indices)
{
	struct fd_ringbuffer *ring = state->ring;
	struct fd_surface *surface = state->render_target.surface;
	enum pc_di_index_size idx_type = INDEX_SIZE_IGN;
	enum pc_di_src_sel src_sel;
	uint32_t idx_offset, idx_size;

	if (indices) {
		switch (type) {
		case GL_UNSIGNED_BYTE:
			idx_type = INDEX_SIZE_8_BIT;
			idx_size = count;
			break;
		case GL_UNSIGNED_SHORT:
			idx_type = INDEX_SIZE_16_BIT;
			idx_size = 2 * count;
			break;
		case GL_UNSIGNED_INT:
			idx_type = INDEX_SIZE_32_BIT;
			idx_size = 4 * count;
			break;
		default:
			ERROR_MSG("invalid type");
			return -1;
		}
		src_sel = DI_SRC_SEL_DMA;
	} else {
		idx_type = INDEX_SIZE_IGN;
		idx_size = 0;
		src_sel = DI_SRC_SEL_AUTO_INDEX;
	}

	/*
	 * vertex shader consts start at 0x80 <-> C0
	 * fragment shader consts start at 0x480?  Or is this controlled by some reg?
	 *
	 */

	state->dirty = true;

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SC_AA_MASK));
	OUT_RING(ring, 0x0000ffff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_DEPTHCONTROL));
	OUT_RING(ring, state->rb_depthcontrol);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SU_SC_MODE_CNTL));
	OUT_RING(ring, state->pa_su_sc_mode_cntl);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SC_WINDOW_SCISSOR_TL));
	OUT_RING(ring, xy2d(0,0));          /* PA_SC_WINDOW_SCISSOR_TL */
	OUT_RING(ring, xy2d(surface->width, /* PA_SC_WINDOW_SCISSOR_BR */
			surface->height));

	OUT_PKT3(ring, CP_SET_CONSTANT, 5);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_CL_VPORT_XSCALE));
	OUT_RING(ring, fui(state->viewport.scale.x));	/* PA_CL_VPORT_XSCALE */
	OUT_RING(ring, fui(state->viewport.offset.x));	/* PA_CL_VPORT_XOFFSET */
	OUT_RING(ring, fui(state->viewport.scale.y));	/* PA_CL_VPORT_YSCALE */
	OUT_RING(ring, fui(state->viewport.offset.y));	/* PA_CL_VPORT_YOFFSET */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_CL_VTE_CNTL));
	OUT_RING(ring, A2XX_PA_CL_VTE_CNTL_VTX_W0_FMT |
			A2XX_PA_CL_VTE_CNTL_VPORT_X_SCALE_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_X_OFFSET_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_Y_SCALE_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_Y_OFFSET_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_Z_SCALE_ENA |
			A2XX_PA_CL_VTE_CNTL_VPORT_Z_OFFSET_ENA);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_CL_CLIP_CNTL));
	OUT_RING(ring, 0x00000000);

	if (state->rb_depthcontrol & A2XX_RB_DEPTHCONTROL_STENCIL_ENABLE) {
		OUT_PKT3(ring, CP_SET_CONSTANT, 3);
		OUT_RING(ring, CP_REG(REG_A2XX_RB_STENCILREFMASK_BF));
		OUT_RING(ring, state->rb_stencilrefmask);     /* RB_STENCILREFMASK_BF */
		OUT_RING(ring, state->rb_stencilrefmask);     /* RB_STENCILREFMASK */
	}

	emit_constants(state, FD_SHADER_VERTEX);
	emit_constants(state, FD_SHADER_FRAGMENT);

	idx_offset = emit_attributes(state, first, count, idx_size, indices);

	fd_program_emit_shader(state->program, FD_SHADER_VERTEX, ring);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_VGT_VERTEX_REUSE_BLOCK_CNTL));
	OUT_RING(ring, 0x0000003b);

	fd_program_emit_sq_program_cntl(state->program, ring);

	fd_program_emit_shader(state->program, FD_SHADER_FRAGMENT, ring);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_SQ_CONTEXT_MISC));
	OUT_RING(ring, A2XX_SQ_CONTEXT_MISC_SC_SAMPLE_CNTL(CENTERS_ONLY));

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_COLORCONTROL));
	OUT_RING(ring, state->rb_colorcontrol);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_BLEND_CONTROL));
	OUT_RING(ring, state->rb_blendcontrol);

	emit_uniforms(state, FD_SHADER_VERTEX);
	emit_uniforms(state, FD_SHADER_FRAGMENT);

	emit_textures(state);

	OUT_PKT0(ring, REG_A2XX_TC_CNTL_STATUS, 1);
	OUT_RING(ring, A2XX_TC_CNTL_STATUS_L2_INVALIDATE);

	OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
	OUT_RING(ring, 0x0000000);

	OUT_PKT3(ring, CP_DRAW_INDX, indices ? 5 : 3);
	OUT_RING(ring, 0x00000000);		/* viz query info. */
	switch (mode) {
	case GL_TRIANGLE_STRIP:
		OUT_RING(ring, DRAW(DI_PT_TRISTRIP, src_sel, idx_type, IGNORE_VISIBILITY));
		break;
	case GL_TRIANGLE_FAN:
		OUT_RING(ring, DRAW(DI_PT_TRIFAN, src_sel, idx_type, IGNORE_VISIBILITY));
		break;
	case GL_TRIANGLES:
		OUT_RING(ring, DRAW(DI_PT_TRILIST, src_sel, idx_type, IGNORE_VISIBILITY));
		break;
	default:
		ERROR_MSG("unsupported mode: %d", mode);
		return -1;
	}
	OUT_RING(ring, count);				/* NumIndices */
	if (indices) {
		OUT_RELOC(ring, state->attributes.bo, idx_offset, 0);
		OUT_RING (ring, idx_size);
	}

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_UNKNOWN_2010));
	OUT_RING(ring, 0x00000000);

	emit_cacheflush(state);

	return 0;
}

int fd_draw_elements(struct fd_state *state, GLenum mode, GLsizei count,
		GLenum type, const GLvoid* indices)
{
	return draw_impl(state, mode, 0, count, type, indices);
}

int fd_draw_arrays(struct fd_state *state, GLenum mode,
		GLint first, GLsizei count)
{
	return draw_impl(state, mode, first, count, 0, NULL);
}

int fd_swap_buffers(struct fd_state *state)
{
	fd_flush(state);

	state->ws->post_surface(state->ws,
			state->render_target.surface);

	return 0;
}

int fd_flush(struct fd_state *state)
{
	struct fd_surface *surface = state->render_target.surface;
	struct fd_ringbuffer *ring;

	if (!state->dirty)
		return 0;

	fd_ringmarker_mark(state->draw_end);

	if ((state->render_target.nbins_x == 1) &&
			(state->render_target.nbins_y == 1)) {
		/* no binning needed, just emit the primary ringbuffer: */
		ring = state->ring;
		emit_gmem2mem(state, ring, surface, 0, 0);
	} else {
		/* binning required, build cmds to setup for each tile in
		 * the tile ringbuffer, w/ IB's to the primary ringbuffer:
		 */
		uint32_t i, yoff = 0;
		ring = state->ring_tile;

		for (i = 0; i < state->render_target.nbins_y; i++) {
			uint32_t j, xoff = 0;
			uint32_t bin_h = state->render_target.bin_h;

			/* clip bin height: */
			bin_h = min(bin_h, surface->height - yoff);

			for (j = 0; j < state->render_target.nbins_x; j++) {
				uint32_t bin_w = state->render_target.bin_w;

				/* clip bin width: */
				bin_w = min(bin_w, surface->width - xoff);

				DEBUG_MSG("bin_h=%d, yoff=%d, bin_w=%d, xoff=%d",
						bin_h, yoff, bin_w, xoff);

				/* setup scissor/offset for current tile: */
				OUT_PKT3(ring, CP_SET_CONSTANT, 4);
				OUT_RING(ring, CP_REG(REG_A2XX_PA_SC_WINDOW_OFFSET));
				OUT_RING(ring, A2XX_PA_SC_WINDOW_OFFSET_X(-xoff) |
						A2XX_PA_SC_WINDOW_OFFSET_Y(-yoff));/* PA_SC_WINDOW_OFFSET */
				OUT_RING(ring, xy2d(0,0));            /* PA_SC_WINDOW_SCISSOR_TL */
				OUT_RING(ring, xy2d(surface->width,   /* PA_SC_WINDOW_SCISSOR_BR */
						surface->height));

				OUT_PKT3(ring, CP_SET_CONSTANT, 3);
				OUT_RING(ring, CP_REG(REG_A2XX_PA_SC_SCREEN_SCISSOR_TL));
				OUT_RING(ring, xy2d(0,0));            /* PA_SC_SCREEN_SCISSOR_TL */
				OUT_RING(ring, xy2d(bin_w, bin_h));   /* PA_SC_SCREEN_SCISSOR_BR */

				/* emit IB to drawcmds: */
				OUT_IB  (ring, state->draw_start, state->draw_end);

				/* emit gmem2mem to transfer tile back to system memory: */
				emit_gmem2mem(state, ring, surface, xoff, yoff);

				xoff += bin_w;
			}

			yoff += bin_h;
		}
	}

	fd_ringbuffer_flush(ring);
	fd_pipe_wait(state->ws->pipe, fd_ringbuffer_timestamp(ring));
	fd_ringbuffer_reset(state->ring);
	fd_ringbuffer_reset(state->ring_tile);

	fd_ringmarker_mark(state->draw_start);

	state->dirty = false;

	return 0;
}

/* ************************************************************************* */

struct fd_surface * fd_surface_new_fmt(struct fd_state *state,
		uint32_t width, uint32_t height, enum a2xx_colorformatx color_format)
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

	surface->bo = fd_bo_new(state->ws->dev,
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
	return state->ws->get_surface(state->ws, width, height);
}

void fd_surface_del(struct fd_state *state, struct fd_surface *surface)
{
	if (!surface)
		return;
	if (state->render_target.surface == surface)
		state->render_target.surface = NULL;
	fd_bo_del(surface->bo);
	free(surface);
}

void fd_surface_upload(struct fd_surface *surface, const void *data)
{
	uint32_t i;
	uint8_t *surfp = fd_bo_map(surface->bo);
	const uint8_t *datap = data;

	for (i = 0; i < surface->height; i++) {
		memcpy(surfp, datap, surface->width * surface->cpp);
		surfp += surface->pitch * surface->cpp;
		datap += surface->width * surface->cpp;
	}
}

static void attach_render_target(struct fd_state *state,
		struct fd_surface *surface)
{
	uint32_t nbins_x = 1, nbins_y = 1;
	uint32_t bin_w, bin_h;
	uint32_t cpp = color2cpp[surface->color];
	uint32_t gmem_size = state->gmemsize_bytes;
	uint32_t max_width = 992;

	if (state->rb_depthcontrol & (A2XX_RB_DEPTHCONTROL_Z_ENABLE |
			A2XX_RB_DEPTHCONTROL_STENCIL_ENABLE)) {
		gmem_size /= 2;
		max_width = 256;
	}

	state->render_target.surface = surface;

	bin_w = ALIGN(surface->width, 32);
	bin_h = ALIGN(surface->height, 32);

	/* first, find a bin width that satisfies the maximum width
	 * restrictions:
	 */
	while (bin_w > max_width) {
		nbins_x++;
		bin_w = ALIGN(surface->width / nbins_x, 32);
	}

	/* then find a bin height that satisfies the memory constraints:
	 */
	while ((bin_w * bin_h * cpp) > gmem_size) {
		nbins_y++;
		bin_h = ALIGN(surface->height / nbins_y, 32);
	}

	if ((nbins_x > 1) || (nbins_y > 1)) {
		state->pa_su_sc_mode_cntl |= A2XX_PA_SU_SC_MODE_CNTL_VTX_WINDOW_OFFSET_ENABLE;
	} else {
		state->pa_su_sc_mode_cntl &= ~A2XX_PA_SU_SC_MODE_CNTL_VTX_WINDOW_OFFSET_ENABLE;
	}

	INFO_MSG("using %d bins of size %dx%d", nbins_x*nbins_y, bin_w, bin_h);

//if we use hw binning, tile sizes (in multiple of 32) need to
//fit in 5 bits.. for now don't care because we aren't using
//that:
//	assert(!(bin_h/32 & ~0x1f));
//	assert(!(bin_w/32 & ~0x1f));

	state->render_target.nbins_x = nbins_x;
	state->render_target.nbins_y = nbins_y;
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
	struct fd_ringbuffer *ring = state->ring;
	uint32_t base;

	attach_render_target(state, surface);
	set_viewport(state, 0, 0, surface->width, surface->height);

	emit_mem_write(state, state->solid_const,
			init_shader_const, ARRAY_SIZE(init_shader_const));

	OUT_PKT0(ring, REG_A2XX_TP0_CHICKEN, 1);
	OUT_RING(ring, 0x00000002);

	OUT_PKT3(ring, CP_INVALIDATE_STATE, 1);
	OUT_RING(ring, 0x00007fff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_SQ_VS_CONST));
	OUT_RING(ring, 0x00100020);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_SQ_PS_CONST));
	OUT_RING(ring, 0x000e0120);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_A2XX_VGT_MAX_VTX_INDX));
	OUT_RING(ring, 0xffffffff);	/* VGT_MAX_VTX_INDX */
	OUT_RING(ring, 0x00000000);	/* VGT_MIN_VTX_INDX */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_VGT_INDX_OFFSET));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_VGT_VERTEX_REUSE_BLOCK_CNTL));
	OUT_RING(ring, 0x0000003b);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_SQ_CONTEXT_MISC));
	OUT_RING(ring, A2XX_SQ_CONTEXT_MISC_SC_SAMPLE_CNTL(CENTERS_ONLY));

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_SQ_INTERPOLATOR_CNTL));
	OUT_RING(ring, 0xffffffff);

	emit_pa_state(state);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_MODECONTROL));
	OUT_RING(ring, A2XX_RB_MODECONTROL_EDRAM_MODE(COLOR_DEPTH));

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_BLEND_CONTROL));
	OUT_RING(ring, state->rb_blendcontrol);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_COLORCONTROL));
	OUT_RING(ring, A2XX_RB_COLORCONTROL_BLEND_DISABLE);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_SAMPLE_POS));
	OUT_RING(ring, 0x88888888);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_COLOR_DEST_MASK));
	OUT_RING(ring, 0xffffffff);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_COPY_DEST_INFO));
	OUT_RING(ring, A2XX_RB_COPY_DEST_INFO_FORMAT(COLORX_4_4_4_4) |
			A2XX_RB_COPY_DEST_INFO_WRITE_RED |
			A2XX_RB_COPY_DEST_INFO_WRITE_GREEN |
			A2XX_RB_COPY_DEST_INFO_WRITE_BLUE |
			A2XX_RB_COPY_DEST_INFO_WRITE_ALPHA);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_A2XX_SQ_WRAPPING_0));
	OUT_RING(ring, 0x00000000);	/* SQ_WRAPPING_0 */
	OUT_RING(ring, 0x00000000);	/* SQ_WRAPPING_1 */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SU_POINT_MINMAX));
	OUT_RING(ring, 0x04000008);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_ALPHA_REF));
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_SET_DRAW_INIT_FLAGS, 1);
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_WAIT_REG_EQ, 4);
	OUT_RING(ring, 0x000005d0);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x5f601000);
	OUT_RING(ring, 0x00000001);

	OUT_PKT0(ring, REG_A2XX_SQ_INST_STORE_MANAGMENT, 1);
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

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_A2XX_PA_SC_SCREEN_SCISSOR_TL));
	OUT_RING(ring, xy2d(0,0));			/* PA_SC_SCREEN_SCISSOR_TL */
	OUT_RING(ring, xy2d(surface->width, /* PA_SC_SCREEN_SCISSOR_BR */
			surface->height));

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_COLOR_MASK));
	OUT_RING(ring, A2XX_RB_COLOR_MASK_WRITE_RED |
			A2XX_RB_COLOR_MASK_WRITE_GREEN |
			A2XX_RB_COLOR_MASK_WRITE_BLUE |
			A2XX_RB_COLOR_MASK_WRITE_ALPHA);

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_DEPTHCONTROL));
	OUT_RING(ring, state->rb_depthcontrol);

	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_STENCILREFMASK_BF));
	OUT_RING(ring, 0x00000000);		/* RB_STENCILREFMASK_BF */
	OUT_RING(ring, 0x00000000);		/* REG_RB_STENCILREFMASK */

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_COLORCONTROL));
	OUT_RING(ring, state->rb_colorcontrol);

	OUT_PKT3(ring, CP_SET_CONSTANT, 5);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_BLEND_RED));
	OUT_RING(ring, 0x00000000);		/* REG_RB_BLEND_RED */
	OUT_RING(ring, 0x00000000);		/* REG_RB_BLEND_GREEN */
	OUT_RING(ring, 0x00000000);		/* REG_RB_BLEND_BLUE */
	OUT_RING(ring, 0x000000ff);		/* REG_RB_BLEND_ALPHA */

	OUT_PKT3(ring, CP_SET_CONSTANT, 4);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_SURFACE_INFO));
	OUT_RING(ring, state->render_target.bin_w);  /* RB_SURFACE_INFO */
	OUT_RING(ring, 0x200 | surface->color);      /* RB_COLOR_INFO */
	base = state->render_target.bin_w * state->render_target.bin_h;
	if (state->rb_depthcontrol & A2XX_RB_DEPTHCONTROL_STENCIL_ENABLE) {
		OUT_RING(ring,                           /* RB_DEPTH_INFO */
				A2XX_RB_DEPTH_INFO_DEPTH_FORMAT(DEPTHX_24_8) |
				A2XX_RB_DEPTH_INFO_DEPTH_BASE(base));
	} else if (state->rb_depthcontrol & A2XX_RB_DEPTHCONTROL_Z_ENABLE) {
		OUT_RING(ring,                           /* RB_DEPTH_INFO */
				A2XX_RB_DEPTH_INFO_DEPTH_FORMAT(DEPTHX_16) |
				A2XX_RB_DEPTH_INFO_DEPTH_BASE(base));
	} else {
		OUT_RING(ring,                           /* RB_DEPTH_INFO */
				A2XX_RB_DEPTH_INFO_DEPTH_BASE(base));
	}

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_RB_SAMPLE_POS));
	OUT_RING(ring, 0x88888888);

	fd_ringbuffer_flush(ring);

	fd_ringmarker_mark(state->draw_start);
}

int fd_dump_hex(struct fd_surface *surface)
{
	uint32_t *dbuf = fd_bo_map(surface->bo);
	float   *fbuf = fd_bo_map(surface->bo);
	uint32_t i;

	for (i = 0; i < fd_bo_size(surface->bo) / 4; i+=4) {
		printf("\t\t\t%08X:   %08x %08x %08x %08x\t\t %8.8f %8.8f %8.8f %8.8f\n",
				(unsigned int) i*4,
				dbuf[i], dbuf[i+1], dbuf[i+2], dbuf[i+3],
				fbuf[i], fbuf[i+1], fbuf[i+2], fbuf[i+3]);
	}

	return 0;
}

int fd_dump_bmp(struct fd_surface *surface, const char *filename)
{
	return bmp_dump(fd_bo_map(surface->bo),
			surface->width, surface->height,
			surface->pitch * surface->cpp,
			filename);
}
