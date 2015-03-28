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
#include "ir-a3xx.h"
#include "ws.h"
#include "bmp.h"

static inline void
emit_marker(struct fd_ringbuffer *ring, int scratch_idx)
{
	static unsigned marker_cnt = 0;
	OUT_PKT0(ring, REG_AXXX_CP_SCRATCH_REG0 + scratch_idx, 1);
	OUT_RING(ring, ++marker_cnt);
}

struct fd_state {

	struct fd_winsys *ws;
	struct fd_device *dev;
	struct fd_pipe *pipe;

	/* device properties: */
	uint32_t gmemsize_bytes;
	uint32_t device_id;

	/* cmdstream buffer with render commands: */
	struct fd_ringbuffer *ring;
	struct fd_ringmarker *draw_start, *draw_end;

	struct {
		struct fd_bo *bo;
	} vsc_pipe[8];

	/* program used internally for blits/fills */
	struct fd_program *solid_program;

	/* buffer for passing vertices to solid program */
	struct fd_bo *solid_const;

	/* buffers for private memory for vert/frag shaders: */
	struct fd_bo *vs_pvt_mem, *fs_pvt_mem;

	/* shader program: */
	struct fd_program *program;

	/* uniform related params: */
	struct fd_parameters uniforms, solid_uniforms;

	/* attribute related params: */
	struct fd_parameters attributes, solid_attributes;

	/* texture related params: */
	struct {
		enum a3xx_tex_filter min_filter, mag_filter;
		enum a3xx_tex_clamp clamp_s, clamp_t;

		struct fd_parameters params;
	} textures;

	/* buffer related params: */
	struct fd_parameters bufs;

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
		float color[4];
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

	/* query related state: */
	struct {
		bool active;
		struct fd_bo *bo;
		uint32_t offset;
	} query;

	uint32_t pc_prim_vtx_cntl;
	uint32_t gras_su_mode_control;
	struct {
		uint32_t control;
		uint32_t blendcontrol;
	} rb_mrt[4];
	uint32_t rb_render_control;
	uint32_t rb_stencil_control;
	uint32_t rb_depth_control;
	uint32_t rb_stencilrefmask;
};

struct fd_shader_const {
	uint32_t offset, sz;
	struct fd_bo *bo;
	enum a3xx_color_fmt format;
};

/* ************************************************************************* */
/* color format info */

static int color2cpp[] = {
		[RB_R8G8B8_UNORM]       = 3,
		[RB_R8G8B8A8_UNORM]     = 4,
		[RB_Z16_UNORM]          = 2,
		[RB_A8_UNORM]           = 1,
		[RB_R16G16B16A16_FLOAT] = 8,
		[RB_R32G32B32A32_FLOAT] = 16,
		[RB_R32G32B32A32_UINT]  = 16,
};

static enum a3xx_tex_fmt color2fmt[] = {
		[RB_R8G8B8_UNORM]       = TFMT_NORM_UINT_8_8_8,
		[RB_R8G8B8A8_UNORM]     = TFMT_NORM_UINT_8_8_8_8,
//		[RB_Z16_UNORM]          = TFMT_FLOAT_16,  // ???
		[RB_A8_UNORM]           = TFMT_NORM_UINT_8,
		[RB_R16G16B16A16_FLOAT] = TFMT_FLOAT_16_16_16_16,
		[RB_R32G32B32A32_FLOAT] = TFMT_FLOAT_32_32_32_32,
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

const char *solid_vertex_shader_asm =
		"@attribute(r0.x)  aPosition                             \n"
		"(sy)(ss)end                                             \n"
		"nop                                                     \n"
		"nop                                                     \n"
		"nop                                                     \n";

const char *solid_fragment_shader_asm =
		"@uniform(hc0.x) uColor                                  \n"
		"@out(hr0.x)     gl_FragColor                            \n"
		"(sy)(ss)(rpt3)mov.f16f16 hr0.x, (r)hc0.x                \n"
		"end                                                     \n"
		"nop                                                     \n"
		"nop                                                     \n";

static const float init_shader_const[] = {
		-1.000000, +1.000000, +1.000000,
		+1.000000, -1.000000, +1.000000,
		+0.000000, +0.000000, +1.000000,
		+1.000000
};

/* ************************************************************************* */

struct fd_state * fd_init(void)
{
	struct fd_state *state;
	struct fd_param *p;
	uint64_t val;
	unsigned i;
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

	if (state->ws) {
		state->dev  = state->ws->dev;
		state->pipe = state->ws->pipe;
	} else {
		/* well, we can still do compute.. */
		int fd = drmOpen("msm", NULL);
		if (fd < 0) {
			ERROR_MSG("could not open msm device: %d (%s)",
					fd, strerror(errno));
			goto fail;
		}

		state->dev = fd_device_new(fd);
		state->pipe = fd_pipe_new(state->dev, FD_PIPE_3D);
	}

	fd_pipe_get_param(state->pipe, FD_GMEM_SIZE, &val);
	state->gmemsize_bytes = val;

	fd_pipe_get_param(state->pipe, FD_DEVICE_ID, &val);
	state->device_id = val;

	state->ring = fd_ringbuffer_new(state->pipe, 0x10000);
	state->draw_start = fd_ringmarker_new(state->ring);
	state->draw_end = fd_ringmarker_new(state->ring);

	state->solid_const = fd_bo_new(state->dev, 0x1000,
			DRM_FREEDRENO_GEM_TYPE_KMEM);

	state->vs_pvt_mem = fd_bo_new(state->dev, 0x2000,
			DRM_FREEDRENO_GEM_TYPE_KMEM);

	state->fs_pvt_mem = fd_bo_new(state->dev, 0x102000,
			DRM_FREEDRENO_GEM_TYPE_KMEM);

	state->program = fd_program_new(state);

	state->solid_program = fd_program_new(state);

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

	/* manually setup the solid-program attribute/uniform: */
	p = find_param(&state->solid_attributes, "aPosition");
	p->fmt = VFMT_FLOAT_32_32_32;
	p->bo  = state->solid_const;

	p = find_param(&state->solid_uniforms, "uColor");
	p->elem_size = 4;
	p->size  = 4;
	p->count = 1;
	p->data  = &state->clear.color[0];

	/* setup initial GL state: */
	state->cull_mode = GL_BACK;

	state->pc_prim_vtx_cntl =
			A3XX_PC_PRIM_VTX_CNTL_PROVOKING_VTX_LAST |
			A3XX_PC_PRIM_VTX_CNTL_POLYMODE_FRONT_PTYPE(PC_DRAW_TRIANGLES) |
			A3XX_PC_PRIM_VTX_CNTL_POLYMODE_BACK_PTYPE(PC_DRAW_TRIANGLES);
	state->gras_su_mode_control =
			A3XX_GRAS_SU_MODE_CONTROL_LINEHALFWIDTH(4);
	state->rb_render_control =
			A3XX_RB_RENDER_CONTROL_ALPHA_TEST_FUNC(g2a(GL_ALWAYS));
	state->rb_depth_control =
			A3XX_RB_DEPTH_CONTROL_Z_WRITE_ENABLE |
			A3XX_RB_DEPTH_CONTROL_EARLY_Z_DISABLE |
			A3XX_RB_DEPTH_CONTROL_ZFUNC(g2a(GL_LESS));
	state->rb_stencil_control =
			A3XX_RB_STENCIL_CONTROL_FUNC(g2a(GL_ALWAYS)) |
			A3XX_RB_STENCIL_CONTROL_FUNC_BF(g2a(GL_ALWAYS));
	state->rb_stencilrefmask = 0xff000000 |
			A3XX_RB_STENCILREFMASK_STENCILWRITEMASK(0xff);

	state->clear.depth = 1;
	state->clear.stencil = 0;

	for (i = 0; i < ARRAY_SIZE(state->rb_mrt); i++) {
		state->rb_mrt[i].blendcontrol =
				A3XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(FACTOR_ONE) |
				A3XX_RB_MRT_BLEND_CONTROL_RGB_BLEND_OPCODE(BLEND_DST_PLUS_SRC) |
				A3XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(FACTOR_ZERO) |
				A3XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(FACTOR_ONE) |
				A3XX_RB_MRT_BLEND_CONTROL_ALPHA_BLEND_OPCODE(BLEND_DST_PLUS_SRC) |
				A3XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(FACTOR_ZERO) |
				A3XX_RB_MRT_BLEND_CONTROL_CLAMP_ENABLE;
		state->rb_mrt[i].control =
				A3XX_RB_MRT_CONTROL_READ_DEST_ENABLE |
				A3XX_RB_MRT_CONTROL_ROP_CODE(ROP_COPY) |
				A3XX_RB_MRT_CONTROL_DITHER_MODE(DITHER_ALWAYS) |
				A3XX_RB_MRT_CONTROL_COMPONENT_ENABLE(0xf);
	}

	return state;

fail:
	fd_fini(state);
	return NULL;
}

void fd_fini(struct fd_state *state)
{
	fd_surface_del(state, state->render_target.surface);
	fd_ringbuffer_del(state->ring);
	if (state->ws)
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

/* for VBO's */
struct fd_bo * fd_attribute_bo_new(struct fd_state *state,
		uint32_t size, const void *data)
{
	struct fd_bo *bo = fd_bo_new(state->dev, size,
			DRM_FREEDRENO_GEM_TYPE_KMEM);
	if (data)
		memcpy(fd_bo_map(bo), data, size);
	return bo;
}

int fd_attribute_bo(struct fd_state *state, const char *name,
		enum a3xx_vtx_fmt fmt, struct fd_bo * bo)
{
	struct fd_param *p = find_param(&state->attributes, name);
	if (!p)
		return -1;
	p->fmt  = fmt;
	p->bo   = bo;
	return 0;
}

int fd_attribute_pointer(struct fd_state *state, const char *name,
		enum a3xx_vtx_fmt fmt, uint32_t count, const void *data)
{
	uint32_t size = fmt2size(fmt) * count;
	struct fd_bo *bo = fd_bo_new(state->dev, size,
			DRM_FREEDRENO_GEM_TYPE_KMEM);
	memcpy(fd_bo_map(bo), data, size);
	return fd_attribute_bo(state, name, fmt, bo);
}

int fd_uniform_attach(struct fd_state *state, const char *name,
		uint32_t size, uint32_t count, const void *data)
{
	struct fd_param *p = find_param(&state->uniforms, name);
	if (!p)
		return -1;
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
	p->tex = tex;
	return 0;
}

int fd_set_buf(struct fd_state *state, const char *name, struct fd_bo *bo)
{
	struct fd_param *p = find_param(&state->bufs, name);
	if (!p)
		return -1;
	p->bo = bo;
	return 0;
}

static void emit_draw_indx(struct fd_ringbuffer *ring, enum pc_di_primtype primtype,
		enum pc_di_index_size index_size, uint32_t count,
		struct fd_bo *indx_bo, uint32_t idx_offset, uint32_t idx_size)
{
	enum pc_di_src_sel src_sel = indx_bo ? DI_SRC_SEL_DMA : DI_SRC_SEL_AUTO_INDEX;

#if 0
	/* NOTE: blob driver always inserts a dummy DI_PT_POINTLIST draw.. not
	 * sure if this is needed or is a workaround for some early hw, or??
	 */
	OUT_PKT3(ring, CP_DRAW_INDX, 3);
	OUT_RING(ring, 0x00000000);   /* viz query info. */
	OUT_RING(ring, DRAW(DI_PT_POINTLIST, DI_SRC_SEL_AUTO_INDEX,
			INDEX_SIZE_IGN, 1));
	OUT_RING(ring, 0);            /* NumIndices */

	OUT_PKT0(ring, REG_A3XX_UNKNOWN_0E00, 1);
	OUT_RING(ring, 0x00000000);   /* UNKNOWN_0E00 */
#endif

	OUT_PKT3(ring, CP_DRAW_INDX, indx_bo ? 5 : 3);
	OUT_RING(ring, 0x00000000);   /* viz query info. */
	OUT_RING(ring, DRAW(primtype, src_sel, index_size, IGNORE_VISIBILITY));
	OUT_RING(ring, count);        /* NumIndices */
	if (indx_bo) {
		OUT_RELOC(ring, indx_bo, idx_offset, 0);
		OUT_RING (ring, idx_size);
	}
}

static void emit_mrt(struct fd_state *state,
		struct fd_ringbuffer *ring, struct fd_surface *surface)
{
	int i;

	for (i = 0; i < 4; i++) {
		OUT_PKT0(ring, REG_A3XX_RB_MRT_CONTROL(i), 1);
		OUT_RING(ring, state->rb_mrt[i].control);
		OUT_PKT0(ring, REG_A3XX_RB_MRT_BLEND_CONTROL(i), 1);
		OUT_RING(ring, state->rb_mrt[i].blendcontrol);
	}
}

/* emit cmdstream to blit from GMEM back to the surface */
static void emit_gmem2mem(struct fd_state *state,
		struct fd_ringbuffer *ring, struct fd_surface *surface,
		uint32_t xoff, uint32_t yoff)
{
	fd_program_emit_state(state->solid_program, 0,
			NULL, &state->solid_attributes, NULL, ring);

	OUT_PKT0(ring, REG_A3XX_RB_DEPTH_CONTROL, 1);
	OUT_RING(ring, A3XX_RB_DEPTH_CONTROL_ZFUNC(FUNC_NEVER));

	OUT_PKT0(ring, REG_A3XX_RB_STENCIL_CONTROL, 1);
	OUT_RING(ring, A3XX_RB_STENCIL_CONTROL_FUNC(FUNC_NEVER) |
			A3XX_RB_STENCIL_CONTROL_FAIL(STENCIL_KEEP) |
			A3XX_RB_STENCIL_CONTROL_ZPASS(STENCIL_KEEP) |
			A3XX_RB_STENCIL_CONTROL_ZFAIL(STENCIL_KEEP) |
			A3XX_RB_STENCIL_CONTROL_FUNC_BF(FUNC_NEVER) |
			A3XX_RB_STENCIL_CONTROL_FAIL_BF(STENCIL_KEEP) |
			A3XX_RB_STENCIL_CONTROL_ZPASS_BF(STENCIL_KEEP) |
			A3XX_RB_STENCIL_CONTROL_ZFAIL_BF(STENCIL_KEEP));

	OUT_PKT0(ring, REG_A3XX_GRAS_SU_MODE_CONTROL, 1);
	OUT_RING(ring, A3XX_GRAS_SU_MODE_CONTROL_LINEHALFWIDTH(0));

	OUT_PKT0(ring, REG_A3XX_PC_PRIM_VTX_CNTL, 1);
	OUT_RING(ring, A3XX_PC_PRIM_VTX_CNTL_STRIDE_IN_VPC(0) |
			A3XX_PC_PRIM_VTX_CNTL_POLYMODE_FRONT_PTYPE(PC_DRAW_TRIANGLES) |
			A3XX_PC_PRIM_VTX_CNTL_POLYMODE_BACK_PTYPE(PC_DRAW_TRIANGLES) |
			A3XX_PC_PRIM_VTX_CNTL_PROVOKING_VTX_LAST);

	OUT_PKT0(ring, REG_A3XX_GRAS_CL_CLIP_CNTL, 1);
	OUT_RING(ring, 0x00000000);   /* GRAS_CL_CLIP_CNTL */

	OUT_PKT0(ring, REG_A3XX_RB_MODE_CONTROL, 1);
	OUT_RING(ring, A3XX_RB_MODE_CONTROL_RENDER_MODE(RB_RESOLVE_PASS) |
			A3XX_RB_MODE_CONTROL_MARB_CACHE_SPLIT_MODE);

	OUT_PKT3(ring, CP_REG_RMW, 3);
	OUT_RING(ring, REG_A3XX_RB_RENDER_CONTROL);
	OUT_RING(ring, A3XX_RB_RENDER_CONTROL_BIN_WIDTH__MASK);
	OUT_RING(ring, 0x2000 | /* XXX */
			A3XX_RB_RENDER_CONTROL_DISABLE_COLOR_PIPE |
			A3XX_RB_RENDER_CONTROL_ALPHA_TEST_FUNC(FUNC_NEVER));

	OUT_PKT0(ring, REG_A3XX_GRAS_SC_CONTROL, 1);
	OUT_RING(ring, A3XX_GRAS_SC_CONTROL_RENDER_MODE(RB_RESOLVE_PASS) |
			A3XX_GRAS_SC_CONTROL_MSAA_SAMPLES(MSAA_ONE) |
			A3XX_GRAS_SC_CONTROL_RASTER_MODE(1));

	OUT_PKT0(ring, REG_A3XX_RB_COPY_CONTROL, 4);
	OUT_RING(ring, A3XX_RB_COPY_CONTROL_MSAA_RESOLVE(MSAA_ONE) |
			A3XX_RB_COPY_CONTROL_MODE(RB_COPY_RESOLVE) |
			A3XX_RB_COPY_CONTROL_GMEM_BASE(0));
	OUT_RELOCS(ring, surface->bo, 0, 0, -1);     /* RB_COPY_DEST_BASE */
	OUT_RING(ring, A3XX_RB_COPY_DEST_PITCH_PITCH(surface->pitch * surface->cpp));
	OUT_RING(ring, A3XX_RB_COPY_DEST_INFO_TILE(LINEAR) |
			A3XX_RB_COPY_DEST_INFO_FORMAT(surface->color) |
			A3XX_RB_COPY_DEST_INFO_COMPONENT_ENABLE(0xf) |
			A3XX_RB_COPY_DEST_INFO_ENDIAN(ENDIAN_NONE));

	emit_draw_indx(ring, DI_PT_RECTLIST, INDEX_SIZE_IGN, 2, NULL, 0, 0);

	OUT_PKT0(ring, REG_A3XX_RB_MODE_CONTROL, 1);
	OUT_RING(ring, A3XX_RB_MODE_CONTROL_RENDER_MODE(RB_RENDERING_PASS) |
			A3XX_RB_MODE_CONTROL_MARB_CACHE_SPLIT_MODE);

	OUT_PKT0(ring, REG_A3XX_GRAS_SC_CONTROL, 1);
	OUT_RING(ring, A3XX_GRAS_SC_CONTROL_RENDER_MODE(RB_RENDERING_PASS) |
			A3XX_GRAS_SC_CONTROL_MSAA_SAMPLES(MSAA_ONE) |
			A3XX_GRAS_SC_CONTROL_RASTER_MODE(0));

	OUT_PKT0(ring, REG_A3XX_GRAS_SU_MODE_CONTROL, 1);
	OUT_RING(ring, state->gras_su_mode_control);

	emit_mrt(state, ring, surface);

	OUT_PKT0(ring, REG_A3XX_RB_STENCIL_CONTROL, 1);
	OUT_RING(ring, state->rb_stencil_control);

	OUT_PKT0(ring, REG_A3XX_RB_DEPTH_CONTROL, 1);
	OUT_RING(ring, state->rb_depth_control);

	OUT_PKT0(ring, REG_A3XX_RB_STENCILREFMASK, 2);
	OUT_RING(ring, state->rb_stencilrefmask);    /* RB_STENCILREFMASK */
	OUT_RING(ring, state->rb_stencilrefmask);    /* RB_STENCILREFMASK_BF */

	OUT_PKT0(ring, REG_A3XX_GRAS_CL_CLIP_CNTL, 1);
	OUT_RING(ring, A3XX_GRAS_CL_CLIP_CNTL_IJ_PERSP_CENTER);
}

/* color in RGBA */
void fd_clear_color(struct fd_state *state, float color[4])
{
	state->clear.color[0] = color[0];
	state->clear.color[1] = color[1];
	state->clear.color[2] = color[2];
	state->clear.color[3] = color[3];
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
	int i;

	state->dirty = true;

	OUT_PKT3(ring, CP_REG_RMW, 3);
	OUT_RING(ring, REG_A3XX_RB_RENDER_CONTROL);
	OUT_RING(ring, A3XX_RB_RENDER_CONTROL_BIN_WIDTH__MASK);
	OUT_RING(ring, 0x2000 | /* XXX */
			A3XX_RB_RENDER_CONTROL_ALPHA_TEST_FUNC(FUNC_NEVER));

	if (mask & GL_DEPTH_BUFFER_BIT) {
		OUT_PKT0(ring, REG_A3XX_RB_DEPTH_CONTROL, 1);
		OUT_RING(ring, A3XX_RB_DEPTH_CONTROL_Z_WRITE_ENABLE |
				A3XX_RB_DEPTH_CONTROL_Z_ENABLE |
				A3XX_RB_DEPTH_CONTROL_ZFUNC(FUNC_ALWAYS));

		OUT_PKT0(ring, REG_A3XX_GRAS_CL_VPORT_ZOFFSET, 2);
		OUT_RING(ring, A3XX_GRAS_CL_VPORT_ZOFFSET(0.0));
		OUT_RING(ring, A3XX_GRAS_CL_VPORT_ZSCALE(state->clear.depth));
	}

	if (mask & GL_STENCIL_BUFFER_BIT) {
		OUT_PKT0(ring, REG_A3XX_RB_STENCILREFMASK, 1);
		OUT_RING(ring, A3XX_RB_STENCILREFMASK_STENCILREF(state->clear.stencil) |
				A3XX_RB_STENCILREFMASK_STENCILMASK(state->clear.stencil) |
				A3XX_RB_STENCILREFMASK_STENCILWRITEMASK(0xff));

		OUT_PKT0(ring, REG_A3XX_RB_STENCIL_CONTROL, 1);
		OUT_RING(ring, A3XX_RB_STENCIL_CONTROL_STENCIL_ENABLE |
				A3XX_RB_STENCIL_CONTROL_FUNC(FUNC_ALWAYS) |
				A3XX_RB_STENCIL_CONTROL_FAIL(STENCIL_KEEP) |
				A3XX_RB_STENCIL_CONTROL_ZPASS(STENCIL_REPLACE) |
				A3XX_RB_STENCIL_CONTROL_ZFAIL(STENCIL_KEEP) |
				A3XX_RB_STENCIL_CONTROL_FUNC_BF(FUNC_NEVER) |
				A3XX_RB_STENCIL_CONTROL_FAIL_BF(STENCIL_KEEP) |
				A3XX_RB_STENCIL_CONTROL_ZPASS_BF(STENCIL_KEEP) |
				A3XX_RB_STENCIL_CONTROL_ZFAIL_BF(STENCIL_KEEP));
	}

	for (i = 0; i < 4; i++) {
		OUT_PKT0(ring, REG_A3XX_RB_MRT_CONTROL(i), 1);
		OUT_RING(ring, A3XX_RB_MRT_CONTROL_ROP_CODE(ROP_COPY) |
				A3XX_RB_MRT_CONTROL_DITHER_MODE(DITHER_ALWAYS) |
				A3XX_RB_MRT_CONTROL_COMPONENT_ENABLE(0xf));

		OUT_PKT0(ring, REG_A3XX_RB_MRT_BLEND_CONTROL(i), 1);
		OUT_RING(ring, A3XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(FACTOR_ONE) |
				A3XX_RB_MRT_BLEND_CONTROL_RGB_BLEND_OPCODE(BLEND_DST_PLUS_SRC) |
				A3XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(FACTOR_ZERO) |
				A3XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(FACTOR_ONE) |
				A3XX_RB_MRT_BLEND_CONTROL_ALPHA_BLEND_OPCODE(BLEND_DST_PLUS_SRC) |
				A3XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(FACTOR_ZERO) |
				A3XX_RB_MRT_BLEND_CONTROL_CLAMP_ENABLE);
	}

	fd_program_emit_state(state->solid_program, 0,
			&state->solid_uniforms, &state->solid_attributes,
			NULL, ring);

	emit_draw_indx(ring, DI_PT_RECTLIST, INDEX_SIZE_IGN, 2, NULL, 0, 0);

	return 0;
}

int fd_cull(struct fd_state *state, GLenum mode)
{
	state->cull_mode = mode;
	return 0;
}

int fd_depth_func(struct fd_state *state, GLenum depth_func)
{
	state->rb_depth_control &= ~A3XX_RB_DEPTH_CONTROL_ZFUNC__MASK;
	state->rb_depth_control |= A3XX_RB_DEPTH_CONTROL_ZFUNC(g2a(depth_func));
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
			state->gras_su_mode_control |= A3XX_GRAS_SU_MODE_CONTROL_CULL_FRONT;
		}
		if ((state->cull_mode == GL_BACK) ||
				(state->cull_mode == GL_FRONT_AND_BACK)) {
			state->gras_su_mode_control |= A3XX_GRAS_SU_MODE_CONTROL_CULL_BACK;
		}
		return 0;
	case GL_POLYGON_OFFSET_FILL:
		state->gras_su_mode_control |= A3XX_GRAS_SU_MODE_CONTROL_POLY_OFFSET;
		return 0;
	case GL_BLEND:
		state->rb_mrt[0].control |= (A3XX_RB_MRT_CONTROL_BLEND | A3XX_RB_MRT_CONTROL_BLEND2);
		return 0;
	case GL_DEPTH_TEST:
		state->rb_depth_control |= (A3XX_RB_DEPTH_CONTROL_Z_ENABLE |
				A3XX_RB_DEPTH_CONTROL_Z_TEST_ENABLE);
		return 0;
	case GL_STENCIL_TEST:
		state->rb_stencil_control |= (A3XX_RB_STENCIL_CONTROL_STENCIL_ENABLE |
				A3XX_RB_STENCIL_CONTROL_STENCIL_ENABLE_BF);
		return 0;
	case GL_DITHER:
		state->rb_mrt[0].control |= A3XX_RB_MRT_CONTROL_DITHER_MODE(DITHER_ALWAYS);
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
		state->gras_su_mode_control &=
			~(A3XX_GRAS_SU_MODE_CONTROL_CULL_FRONT | A3XX_GRAS_SU_MODE_CONTROL_CULL_BACK);
		return 0;
	case GL_POLYGON_OFFSET_FILL:
		state->gras_su_mode_control &= ~A3XX_GRAS_SU_MODE_CONTROL_POLY_OFFSET;
		return 0;
	case GL_BLEND:
		state->rb_mrt[0].control &= ~(A3XX_RB_MRT_CONTROL_BLEND | A3XX_RB_MRT_CONTROL_BLEND2);
		return 0;
	case GL_DEPTH_TEST:
		state->rb_depth_control &= ~(A3XX_RB_DEPTH_CONTROL_Z_ENABLE |
				A3XX_RB_DEPTH_CONTROL_Z_TEST_ENABLE);
		return 0;
	case GL_STENCIL_TEST:
		state->rb_stencil_control &= ~(A3XX_RB_STENCIL_CONTROL_STENCIL_ENABLE |
				A3XX_RB_STENCIL_CONTROL_STENCIL_ENABLE_BF);
		return 0;
	case GL_DITHER:
		state->rb_mrt[0].control &= ~A3XX_RB_MRT_CONTROL_DITHER_MODE(DITHER_ALWAYS);
		return 0;
	default:
		ERROR_MSG("unsupported cap: 0x%04x", cap);
		return -1;
	}
}

int fd_blend_func(struct fd_state *state, GLenum sfactor, GLenum dfactor)
{
	uint32_t bc = 0;

	switch (sfactor) {
	case GL_ZERO:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(FACTOR_ZERO);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(FACTOR_ZERO);
		break;
	case GL_ONE:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(FACTOR_ONE);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(FACTOR_ONE);
		break;
	case GL_SRC_COLOR:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(FACTOR_SRC_COLOR);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(FACTOR_SRC_COLOR);
		break;
	case GL_ONE_MINUS_SRC_COLOR:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(FACTOR_ONE_MINUS_SRC_COLOR);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(FACTOR_ONE_MINUS_SRC_COLOR);
		break;
	case GL_SRC_ALPHA:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(FACTOR_SRC_ALPHA);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(FACTOR_SRC_ALPHA);
		break;
	case GL_ONE_MINUS_SRC_ALPHA:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(FACTOR_ONE_MINUS_SRC_ALPHA);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(FACTOR_ONE_MINUS_SRC_ALPHA);
		break;
	case GL_DST_COLOR:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(FACTOR_DST_COLOR);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(FACTOR_DST_COLOR);
		break;
	case GL_ONE_MINUS_DST_COLOR:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(FACTOR_ONE_MINUS_DST_COLOR);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(FACTOR_ONE_MINUS_DST_COLOR);
		break;
	case GL_DST_ALPHA:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(FACTOR_DST_ALPHA);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(FACTOR_DST_ALPHA);
		break;
	case GL_ONE_MINUS_DST_ALPHA:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(FACTOR_ONE_MINUS_DST_ALPHA);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(FACTOR_ONE_MINUS_DST_ALPHA);
		break;
	default:
		ERROR_MSG("invalid sfactor: 0x%04x", sfactor);
		return -1;
	}

	switch (dfactor) {
	case GL_ZERO:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(FACTOR_ZERO);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(FACTOR_ZERO);
		break;
	case GL_ONE:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(FACTOR_ONE);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(FACTOR_ONE);
		break;
	case GL_SRC_COLOR:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(FACTOR_SRC_COLOR);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(FACTOR_SRC_COLOR);
		break;
	case GL_ONE_MINUS_SRC_COLOR:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(FACTOR_ONE_MINUS_SRC_COLOR);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(FACTOR_ONE_MINUS_SRC_COLOR);
		break;
	case GL_SRC_ALPHA:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(FACTOR_SRC_ALPHA);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(FACTOR_SRC_ALPHA);
		break;
	case GL_ONE_MINUS_SRC_ALPHA:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(FACTOR_ONE_MINUS_SRC_ALPHA);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(FACTOR_ONE_MINUS_SRC_ALPHA);
		break;
	case GL_DST_COLOR:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(FACTOR_DST_COLOR);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(FACTOR_DST_COLOR);
		break;
	case GL_ONE_MINUS_DST_COLOR:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(FACTOR_ONE_MINUS_DST_COLOR);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(FACTOR_ONE_MINUS_DST_COLOR);
		break;
	case GL_DST_ALPHA:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(FACTOR_DST_ALPHA);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(FACTOR_DST_ALPHA);
		break;
	case GL_ONE_MINUS_DST_ALPHA:
		bc |= A3XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(FACTOR_ONE_MINUS_DST_ALPHA);
		bc |= A3XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(FACTOR_ONE_MINUS_DST_ALPHA);
		break;
	default:
		ERROR_MSG("invalid dfactor: 0x%04x", sfactor);
		return -1;
	}

	state->rb_mrt[0].blendcontrol = bc;

	return 0;
}

int fd_stencil_func(struct fd_state *state, GLenum func,
		GLint ref, GLuint mask)
{
	state->rb_stencilrefmask &= ~(
			A3XX_RB_STENCILREFMASK_STENCILREF__MASK |
			A3XX_RB_STENCILREFMASK_STENCILMASK__MASK);
	state->rb_stencilrefmask |=
			A3XX_RB_STENCILREFMASK_STENCILREF(ref) |
			A3XX_RB_STENCILREFMASK_STENCILMASK(mask);
	state->rb_stencil_control &= ~(
			A3XX_RB_STENCIL_CONTROL_FUNC__MASK |
			A3XX_RB_STENCIL_CONTROL_FUNC_BF__MASK );
	state->rb_stencil_control |=
			A3XX_RB_STENCIL_CONTROL_FUNC(g2a(func)) |
			A3XX_RB_STENCIL_CONTROL_FUNC_BF(g2a(func));
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

	state->rb_stencil_control &= ~(
			A3XX_RB_STENCIL_CONTROL_FAIL__MASK |
			A3XX_RB_STENCIL_CONTROL_ZPASS__MASK |
			A3XX_RB_STENCIL_CONTROL_ZFAIL__MASK |
			A3XX_RB_STENCIL_CONTROL_FAIL_BF__MASK |
			A3XX_RB_STENCIL_CONTROL_ZPASS_BF__MASK |
			A3XX_RB_STENCIL_CONTROL_ZFAIL_BF__MASK);

	state->rb_stencil_control |=
			A3XX_RB_STENCIL_CONTROL_FAIL(rbsfail) |
			A3XX_RB_STENCIL_CONTROL_ZPASS(rbzpass) |
			A3XX_RB_STENCIL_CONTROL_ZFAIL(rbzfail) |
			A3XX_RB_STENCIL_CONTROL_FAIL_BF(rbsfail) |
			A3XX_RB_STENCIL_CONTROL_ZPASS_BF(rbzpass) |
			A3XX_RB_STENCIL_CONTROL_ZFAIL_BF(rbzfail);
	return 0;
}

int fd_stencil_mask(struct fd_state *state, GLuint mask)
{
	state->rb_stencilrefmask &= ~A3XX_RB_STENCILREFMASK_STENCILWRITEMASK__MASK;
	state->rb_stencilrefmask |= A3XX_RB_STENCILREFMASK_STENCILWRITEMASK(mask);
	return 0;
}

static int set_filter(enum a3xx_tex_filter *filter, GLint param)
{
	switch (param) {
	case GL_LINEAR:
		*filter = A3XX_TEX_LINEAR;
		return 0;
	case GL_NEAREST:
		*filter = A3XX_TEX_NEAREST;
		return 0;
	default:
		ERROR_MSG("unsupported param: 0x%04x", param);
		return -1;
	}
}

static int set_clamp(enum a3xx_tex_clamp *clamp, GLint param)
{
	switch (param) {
	case GL_REPEAT:
		*clamp = A3XX_TEX_REPEAT;
		return 0;
	case GL_MIRRORED_REPEAT:
		*clamp = A3XX_TEX_MIRROR_REPEAT;
		return 0;
	case GL_CLAMP_TO_EDGE:
		*clamp = A3XX_TEX_CLAMP_TO_EDGE;
		return 0;
	default:
		ERROR_MSG("unsupported param: 0x%04x", param);
		return -1;
	}
}

int fd_tex_param(struct fd_state *state, GLenum name, GLint param)
{
	switch (name) {
	default:
	case GL_TEXTURE_MAG_FILTER:
		return set_filter(&state->textures.mag_filter, param);
	case GL_TEXTURE_MIN_FILTER:
		return set_filter(&state->textures.min_filter, param);
	case GL_TEXTURE_WRAP_S:
		return set_clamp(&state->textures.clamp_s, param);
	case GL_TEXTURE_WRAP_T:
		return set_clamp(&state->textures.clamp_t, param);
		ERROR_MSG("unsupported name: 0x%04x", name);
		return -1;
	}
}

static void emit_textures(struct fd_state *state)
{
	struct fd_ringbuffer *ring = state->ring;
	struct ir3_sampler **samplers;
	int n, samplers_count;

	/* this dst_off should align w/ values in TPL1_TP_FS_TEX_OFFSET:
	 */
	int dst_off = 16;

	samplers = fd_program_samplers(state->program,
			FD_SHADER_FRAGMENT, &samplers_count);

	if (!samplers_count)
		return;

	/* emit sampler state: */
	OUT_PKT3(ring, CP_LOAD_STATE, 2 + (2 * samplers_count));
	OUT_RING(ring, CP_LOAD_STATE_0_DST_OFF(dst_off) |
			CP_LOAD_STATE_0_STATE_SRC(SS_DIRECT) |
			CP_LOAD_STATE_0_STATE_BLOCK(SB_FRAG_TEX) |
			CP_LOAD_STATE_0_NUM_UNIT(samplers_count));
	OUT_RING(ring, CP_LOAD_STATE_1_STATE_TYPE(ST_SHADER) |
			CP_LOAD_STATE_1_EXT_SRC_ADDR(0));
	for (n = 0; n < samplers_count; n++) {
		OUT_RING(ring, A3XX_TEX_SAMP_0_XY_MAG(state->textures.mag_filter) |
				A3XX_TEX_SAMP_0_XY_MIN(state->textures.min_filter) |
				A3XX_TEX_SAMP_0_WRAP_S(state->textures.clamp_s) |
				A3XX_TEX_SAMP_0_WRAP_T(state->textures.clamp_t) |
				A3XX_TEX_SAMP_0_WRAP_R(A3XX_TEX_REPEAT));
		OUT_RING(ring, 0x00000000);
	}

	/* emit texture state: */
	OUT_PKT3(ring, CP_LOAD_STATE, 2 + (4 * samplers_count));
	OUT_RING(ring, CP_LOAD_STATE_0_DST_OFF(dst_off) |
			CP_LOAD_STATE_0_STATE_SRC(SS_DIRECT) |
			CP_LOAD_STATE_0_STATE_BLOCK(SB_FRAG_TEX) |
			CP_LOAD_STATE_0_NUM_UNIT(samplers_count));
	OUT_RING(ring, CP_LOAD_STATE_1_STATE_TYPE(ST_CONSTANTS) |
			CP_LOAD_STATE_1_EXT_SRC_ADDR(0));
	for (n = 0; n < samplers_count; n++) {
		struct fd_param *p = find_param(&state->textures.params,
				samplers[n]->name);
		struct fd_surface *tex = p->tex;
		OUT_RING(ring, 0x00c00000 | // XXX
				A3XX_TEX_CONST_0_SWIZ_X(A3XX_TEX_X) |
				A3XX_TEX_CONST_0_SWIZ_Y(A3XX_TEX_Y) |
				A3XX_TEX_CONST_0_SWIZ_Z(A3XX_TEX_Z) |
				A3XX_TEX_CONST_0_SWIZ_W(A3XX_TEX_W) |
				A3XX_TEX_CONST_0_FMT(color2fmt[tex->color]));
		OUT_RING(ring, 0x30000000 | // XXX
				A3XX_TEX_CONST_1_WIDTH(tex->width) |
				A3XX_TEX_CONST_1_HEIGHT(tex->height));
		OUT_RING(ring, A3XX_TEX_CONST_2_INDX(14 * n) |
				A3XX_TEX_CONST_2_PITCH(tex->pitch * tex->cpp));
		OUT_RING(ring, 0x00000000);
	}

	/* emit mipaddrs: */
	OUT_PKT3(ring, CP_LOAD_STATE, 2 + (14 * samplers_count));
	OUT_RING(ring, CP_LOAD_STATE_0_DST_OFF(14 * dst_off) |
			CP_LOAD_STATE_0_STATE_SRC(SS_DIRECT) |
			CP_LOAD_STATE_0_STATE_BLOCK(SB_FRAG_MIPADDR) |
			CP_LOAD_STATE_0_NUM_UNIT(14 * samplers_count));
	OUT_RING(ring, CP_LOAD_STATE_1_STATE_TYPE(ST_CONSTANTS) |
			CP_LOAD_STATE_1_EXT_SRC_ADDR(0));
	for (n = 0; n < samplers_count; n++) {
		struct fd_param *p = find_param(&state->textures.params,
				samplers[n]->name);
		OUT_RELOC(ring, p->tex->bo, 0, 0);
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
	}
}

static enum pc_di_primtype mode2prim(GLenum mode)
{
	switch (mode) {
	case GL_TRIANGLE_STRIP: return DI_PT_TRISTRIP;
	case GL_TRIANGLE_FAN:   return DI_PT_TRIFAN;
	case GL_TRIANGLES:      return DI_PT_TRILIST;
	default:
		ERROR_MSG("unsupported mode: %d", mode);
		return -1;
	}
}


/* TODO: support for >1 tile (ie. so each tile doesn't overwrite the
 * same part of the results buffer) we need to set the scratch reg from
 * the tiling cmds.  Probably the simplest way is to have one buffer
 * per tile in the batch, and then just increment the offset at each
 * query.
 */
static void emit_query(struct fd_state *state, bool flush)
{
	struct fd_ringbuffer *ring = state->ring;

	if (flush) {
		OUT_PKT0(ring, REG_A3XX_RBBM_PERFCTR_LOAD_VALUE_LO, 1);
		OUT_RING(ring, 0x00000000);

		OUT_PKT0(ring, REG_A3XX_RBBM_PERFCTR_LOAD_VALUE_HI, 1);
		OUT_RING(ring, 0x00000000);

		OUT_PKT3(ring, CP_NOP, 7);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
	} else {
		OUT_PKT3(ring, CP_NOP, 2);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
	}

	OUT_PKT3(ring, CP_NOP, 1);
	OUT_RING(ring, 0x00000000);

	if (!state->query.bo) {
		state->query.bo = fd_bo_new(state->dev, 0x1000,
				DRM_FREEDRENO_GEM_TYPE_KMEM);
	}

// TODO: just set directly for now until we add tiling support..
	OUT_PKT0(ring, REG_A3XX_RB_SAMPLE_COUNT_ADDR, 1);
	OUT_RELOC(ring, state->query.bo, state->query.offset, 0);
	state->query.offset += sizeof(struct fd_perfctrs);
//t3			opcode: CP_SET_CONSTANT (2d) (4 dwords)
//				RB_SAMPLE_COUNT_ADDR: 0x101b2040
//1017c6b0:			c0022d00 80040111 0000057e 101b2040

	OUT_PKT0(ring, REG_A3XX_RB_SAMPLE_COUNT_CONTROL, 1);
	OUT_RING(ring, A3XX_RB_SAMPLE_COUNT_CONTROL_COPY);

	emit_draw_indx(ring, DI_PT_POINTLIST_A2XX, INDEX_SIZE_IGN, 0, NULL, 0, 0);

	OUT_PKT3(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, ZPASS_DONE);

	if (flush) {
		OUT_PKT0(ring, REG_A3XX_RBBM_PERFCTR_CTL, 1);
		OUT_RING(ring, A3XX_RBBM_PERFCTR_CTL_ENABLE);

		OUT_PKT0(ring, REG_A3XX_VBIF_PERF_CNT_EN, 1);
		OUT_RING(ring, A3XX_VBIF_PERF_CNT_EN_CNT0 |
				A3XX_VBIF_PERF_CNT_EN_CNT1 |
				A3XX_VBIF_PERF_CNT_EN_PWRCNT0 |
				A3XX_VBIF_PERF_CNT_EN_PWRCNT1 |
				A3XX_VBIF_PERF_CNT_EN_PWRCNT2);
	}
}

static int draw_impl(struct fd_state *state, GLenum mode,
		GLint first, GLsizei count, GLenum type, const GLvoid *indices)
{
	struct fd_ringbuffer *ring = state->ring;
	enum pc_di_index_size idx_type = INDEX_SIZE_IGN;
	struct fd_bo *indx_bo = NULL;
	uint32_t idx_size, stride_in_vpc;

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

		indx_bo = fd_bo_new(state->dev, idx_size,
				DRM_FREEDRENO_GEM_TYPE_KMEM);
		memcpy(fd_bo_map(indx_bo), indices, idx_size);

	} else {
		idx_type = INDEX_SIZE_IGN;
		idx_size = 0;
	}

	state->dirty = true;

	fd_program_emit_state(state->program, first, &state->uniforms,
			&state->attributes, &state->bufs, ring);

	/*
	 * +----------- max outloc
	 * |    +------ next outloc (max outloc + size of that varying.. ie,
	 * |    |       the outloc of next varying if there was one more)
	 * |    |   +-- stride_in_vpc
	 * |    |   |
	 * v    v   v
	 *
	 * 8	9	2
	 * 9	10	2
	 * 10	11	2
	 * 11	12	2
	 * 12	13	2
	 * 13	14	2
	 * 14	15	2
	 * 15	16	2
	 *
	 * 16	17	3
	 * 17	18	3
	 * 18	19	3
	 * 19	20	3
	 *
	 * 20	21	4
	 * 21	22	4
	 * 22	23	4
	 * 23	24	4
	 *
	 * 24	25	5
	 * 25	26	5
	 * 26	27	5
	 * 27	28	5
	 *
	 * 28	29	6
	 * 29	30	6
	 * 30	31	6
	 * 30	32	6
	 *
	 * 31	33	7
	 *
	 * STRIDE_IN_VPC seems to be, ALIGN(next_outloc - 8, 4) / 4, but blob
	 * driver never uses value of 1, so possibly 0 (no varying), or minimum
	 * of 2..
	 */
	stride_in_vpc = ALIGN(fd_program_outloc(state->program) - 8, 4) / 4;
	if (stride_in_vpc > 0)
		stride_in_vpc = max(stride_in_vpc, 2);
	OUT_PKT0(ring, REG_A3XX_PC_PRIM_VTX_CNTL, 1);
	OUT_RING(ring, A3XX_PC_PRIM_VTX_CNTL_STRIDE_IN_VPC(stride_in_vpc) |
			state->pc_prim_vtx_cntl);

	OUT_PKT0(ring, REG_A3XX_GRAS_SU_MODE_CONTROL, 1);
	OUT_RING(ring, state->gras_su_mode_control);

	OUT_PKT0(ring, REG_A3XX_RB_DEPTH_CONTROL, 1);
	OUT_RING(ring, state->rb_depth_control);

	OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_REG_RMW, 3);
	OUT_RING(ring, REG_A3XX_RB_RENDER_CONTROL);
	OUT_RING(ring, A3XX_RB_RENDER_CONTROL_BIN_WIDTH__MASK);
	OUT_RING(ring, A3XX_RB_RENDER_CONTROL_ENABLE_GMEM |
			A3XX_RB_RENDER_CONTROL_FACENESS |
			A3XX_RB_RENDER_CONTROL_XCOORD |
			A3XX_RB_RENDER_CONTROL_YCOORD |
			A3XX_RB_RENDER_CONTROL_ZCOORD |
			A3XX_RB_RENDER_CONTROL_WCOORD |
			state->rb_render_control);

	OUT_PKT0(ring, REG_A3XX_GRAS_CL_CLIP_CNTL, 1);
	OUT_RING(ring, A3XX_GRAS_CL_CLIP_CNTL_IJ_PERSP_CENTER |
			A3XX_GRAS_CL_CLIP_CNTL_ZCOORD |
			A3XX_GRAS_CL_CLIP_CNTL_WCOORD);

	OUT_PKT0(ring, REG_A3XX_GRAS_CL_VPORT_XOFFSET, 6);
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_XOFFSET(state->viewport.offset.x));
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_XSCALE(state->viewport.scale.x));
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_YOFFSET(state->viewport.offset.y));
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_YSCALE(state->viewport.scale.y));
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_ZOFFSET(state->viewport.offset.z));
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_ZSCALE(state->viewport.scale.z));

	OUT_PKT0(ring, REG_A3XX_RB_STENCILREFMASK, 2);
	OUT_RING(ring, state->rb_stencilrefmask);    /* RB_STENCILREFMASK */
	OUT_RING(ring, state->rb_stencilrefmask);    /* RB_STENCILREFMASK_BF */

	OUT_PKT0(ring, REG_A3XX_RB_STENCIL_CONTROL, 1);
	OUT_RING(ring, state->rb_stencil_control);

	emit_textures(state);

	emit_mrt(state, ring, state->render_target.surface);

	emit_draw_indx(ring, mode2prim(mode), idx_type, count,
			indx_bo, 0, idx_size);
	if (state->query.active)
		emit_query(state, false);

	if (indx_bo)
		fd_bo_del(indx_bo);

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

int fd_run_compute(struct fd_state *state, uint32_t workdim,
		uint32_t *globaloff, uint32_t *globalsize, uint32_t *localsize)
{
	struct fd_ringbuffer *ring = state->ring;
	uint32_t local[3] = {1, 1, 1};
	uint32_t global[3] = {1, 1, 1};
	uint32_t off[3] = {0, 0, 0};
	uint32_t i;

	for (i = 0; i < workdim; i++) {
		if (globaloff)
			off[i] = globaloff[i];
		global[i] = globalsize[i];
		local[i] = localsize[i];
	}

	OUT_PKT3(ring, CP_NOP, 2);
	OUT_RING(ring, 0xdeec0ded);
	OUT_RING(ring, 0x00000001);

	emit_marker(ring, 6);

	OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
	OUT_RING(ring, 0x00000000);

	emit_marker(ring, 6);

	OUT_PKT0(ring, REG_A3XX_UNKNOWN_0EE0, 1);
	OUT_RING(ring, 0x00000000);

	OUT_PKT0(ring, REG_A3XX_UNKNOWN_0F03, 1);
	OUT_RING(ring, 0x00000002);

	OUT_PKT3(ring, CP_REG_RMW, 3);
	OUT_RING(ring, REG_A3XX_RBBM_CLOCK_CTL);
	OUT_RING(ring, 0xfffcffff);
	OUT_RING(ring, 0x00010000);

	OUT_PKT0(ring, REG_A3XX_RB_MODE_CONTROL, 1);
	OUT_RING(ring, A3XX_RB_MODE_CONTROL_RENDER_MODE(RB_COMPUTE_PASS) |
			A3XX_RB_MODE_CONTROL_GMEM_BYPASS);

	OUT_PKT0(ring, REG_A3XX_RB_RENDER_CONTROL, 1);
	OUT_RING(ring, A3XX_RB_RENDER_CONTROL_BIN_WIDTH(0) |
			A3XX_RB_RENDER_CONTROL_ALPHA_TEST_FUNC(FUNC_NEVER));

	OUT_PKT0(ring, REG_A3XX_HLSQ_CL_NDRANGE_0_REG, 9);
	OUT_RING(ring, A3XX_HLSQ_CL_NDRANGE_0_REG_WORKDIM(workdim) |
			A3XX_HLSQ_CL_NDRANGE_0_REG_LOCALSIZE0(local[0]) |
			A3XX_HLSQ_CL_NDRANGE_0_REG_LOCALSIZE1(local[1]) |
			A3XX_HLSQ_CL_NDRANGE_0_REG_LOCALSIZE2(local[2]));
	OUT_RING(ring, global[0]);    /* HLSQ_CL_GLOBAL_WORK[0].SIZE */
	OUT_RING(ring, off[0]);       /* HLSQ_CL_GLOBAL_WORK[0].OFFSET */
	OUT_RING(ring, global[1]);    /* HLSQ_CL_GLOBAL_WORK[1].SIZE */
	OUT_RING(ring, off[1]);       /* HLSQ_CL_GLOBAL_WORK[1].OFFSET */
	OUT_RING(ring, global[2]);    /* HLSQ_CL_GLOBAL_WORK[2].SIZE */
	OUT_RING(ring, off[2]);       /* HLSQ_CL_GLOBAL_WORK[2].OFFSET */
	OUT_RING(ring, 0x0001200c);   /* HLSQ_CL_CONTROL_0_REG */
	OUT_RING(ring, 0x0000f000);   /* HLSQ_CL_CONTROL_1_REG */

	OUT_PKT0(ring, REG_A3XX_HLSQ_CL_KERNEL_CONST_REG, 4);
	OUT_RING(ring, 0x00003006);   /* HLSQ_CL_KERNEL_CONST_REG */
	OUT_RING(ring, global[0] / local[0]);  /* HLSQ_CL_KERNEL_GROUP[0].RATIO */
	OUT_RING(ring, global[1] / local[1]);  /* HLSQ_CL_KERNEL_GROUP[1].RATIO */
	OUT_RING(ring, global[2] / local[2]);  /* HLSQ_CL_KERNEL_GROUP[2].RATIO */

	OUT_PKT0(ring, REG_A3XX_HLSQ_CL_WG_OFFSET_REG, 1);
	OUT_RING(ring, 0x00000009);

	for (i = 0; i < 4; i++) {
		enum a3xx_color_fmt format = 0x11; // XXX

		OUT_PKT0(ring, REG_A3XX_RB_MRT(i), 4);
		OUT_RING(ring, A3XX_RB_MRT_CONTROL_ROP_CODE(ROP_CLEAR) |
				A3XX_RB_MRT_CONTROL_DITHER_MODE(DITHER_DISABLE) |
				A3XX_RB_MRT_CONTROL_COMPONENT_ENABLE(0));
		OUT_RING(ring, A3XX_RB_MRT_BUF_INFO_COLOR_FORMAT(format) |
				A3XX_RB_MRT_BUF_INFO_COLOR_TILE_MODE(LINEAR) |
				A3XX_RB_MRT_BUF_INFO_COLOR_BUF_PITCH(0));
		OUT_RING(ring, A3XX_RB_MRT_BUF_BASE_COLOR_BUF_BASE(0));
		OUT_RING(ring, A3XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(FACTOR_ZERO) |
				A3XX_RB_MRT_BLEND_CONTROL_RGB_BLEND_OPCODE(BLEND_DST_PLUS_SRC) |
				A3XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(FACTOR_ZERO) |
				A3XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(FACTOR_ZERO) |
				A3XX_RB_MRT_BLEND_CONTROL_ALPHA_BLEND_OPCODE(BLEND_DST_PLUS_SRC) |
				A3XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(FACTOR_ZERO));

		OUT_PKT0(ring, REG_A3XX_SP_FS_IMAGE_OUTPUT_REG(i), 1);
		OUT_RING(ring, A3XX_SP_FS_IMAGE_OUTPUT_REG_MRTFORMAT(format));
	}

	OUT_PKT0(ring, REG_A3XX_SP_FS_PVT_MEM_PARAM_REG, 2);
	OUT_RING(ring, 0x10400010);                  /* SP_FS_PVT_MEM_PARAM_REG */
	OUT_RELOC(ring, state->fs_pvt_mem, 0, 0);    /* SP_FS_PVT_MEM_ADDR_REG */

	OUT_PKT0(ring, REG_A3XX_SP_FS_PVT_MEM_SIZE_REG, 1);
	OUT_RING(ring, 0x00000102);

	OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
	OUT_RING(ring, 0x00000000);

	OUT_PKT0(ring, REG_A3XX_TPL1_TP_FS_TEX_OFFSET, 2);
	OUT_RING(ring, A3XX_TPL1_TP_FS_TEX_OFFSET_SAMPLEROFFSET(0) |
			A3XX_TPL1_TP_FS_TEX_OFFSET_MEMOBJOFFSET(0) |
			A3XX_TPL1_TP_FS_TEX_OFFSET_BASETABLEPTR(0));
	OUT_RING(ring, 0x00000000);        /* TPL1_TP_FS_BORDER_COLOR_BASE_ADDR */

	fd_program_emit_compute_state(state->program, &state->uniforms,
			&state->attributes, &state->bufs, ring);

	emit_marker(ring, 6);

	/* kick the compute: */
	OUT_PKT3(ring, CP_RUN_OPENCL, 1);
	OUT_RING(ring, 0x00000000);

	emit_marker(ring, 6);

	OUT_PKT3(ring, CP_NOP, 2);
	OUT_RING(ring, 0xdeec0ded);
	OUT_RING(ring, 0x00000002);

	emit_marker(ring, 6);

	OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
	OUT_RING(ring, 0x00000000);

	emit_marker(ring, 6);

	OUT_PKT3(ring, CP_REG_RMW, 3);
	OUT_RING(ring, REG_A3XX_RBBM_CLOCK_CTL);
	OUT_RING(ring, 0xfffcffff);
	OUT_RING(ring, 0x00000000);

	fd_ringbuffer_flush(ring);

	// TODO maybe return fence/timestamp and let app explicitly wait
	// for timestamp, so it could better pipeline things?  Probably
	// need to be a bit more sophisticated with ringbuffer rollover/
	// reset..

	fd_pipe_wait(state->pipe, fd_ringbuffer_timestamp(ring));
	fd_ringbuffer_reset(state->ring);

	fd_ringmarker_mark(state->draw_start);

	return 0;
}

int fd_swap_buffers(struct fd_state *state)
{
	fd_flush(state);

	state->ws->post_surface(state->ws,
			state->render_target.surface);

	return 0;
}

static void flush_setup(struct fd_state *state, struct fd_ringbuffer *ring)
{
	struct fd_surface *surface = state->render_target.surface;
	uint32_t bin_w = state->render_target.bin_w;
	int i;

	OUT_PKT3(ring, CP_REG_RMW, 3);
	OUT_RING(ring, REG_A3XX_RB_RENDER_CONTROL);
	OUT_RING(ring, ~A3XX_RB_RENDER_CONTROL_BIN_WIDTH__MASK);
	OUT_RING(ring, A3XX_RB_RENDER_CONTROL_BIN_WIDTH(bin_w));

	for (i = 0; i < 4; i++) {
		enum a3xx_color_fmt format = (i == 0) ? surface->color : 0;
		uint32_t pitch = (i == 0) ? (bin_w * surface->cpp) : 0;

		OUT_PKT0(ring, REG_A3XX_RB_MRT_BUF_INFO(i), 2);
		OUT_RING(ring, A3XX_RB_MRT_BUF_INFO_COLOR_FORMAT(format) |
				A3XX_RB_MRT_BUF_INFO_COLOR_TILE_MODE(TILE_32X32) |
				A3XX_RB_MRT_BUF_INFO_COLOR_BUF_PITCH(pitch));
		OUT_RING(ring, A3XX_RB_MRT_BUF_BASE_COLOR_BUF_BASE(0));

		OUT_PKT0(ring, REG_A3XX_SP_FS_IMAGE_OUTPUT_REG(i), 1);
		OUT_RING(ring, A3XX_SP_FS_IMAGE_OUTPUT_REG_MRTFORMAT(format));
	}
}

int fd_flush(struct fd_state *state)
{
	struct fd_surface *surface = state->render_target.surface;
	struct fd_ringbuffer *ring = state->ring;
	uint32_t i, yoff = 0;

	if (!state->dirty)
		return 0;

	if (state->query.bo) {
		/* TODO support for > 1 tile: */
		assert(state->render_target.nbins_x == 1);
		assert(state->render_target.nbins_y == 1);
	}

	fd_ringmarker_mark(state->draw_end);

	flush_setup(state, ring);

	for (i = 0; i < state->render_target.nbins_y; i++) {
		uint32_t j, xoff = 0;
		uint32_t bin_h = state->render_target.bin_h;

		/* clip bin height: */
		bin_h = min(bin_h, surface->height - yoff);

		for (j = 0; j < state->render_target.nbins_x; j++) {
			uint32_t bin_w = state->render_target.bin_w;
			uint32_t x1, y1, x2, y2;

			/* clip bin width: */
			bin_w = min(bin_w, surface->width - xoff);

			x1 = xoff;
			y1 = yoff;
			x2 = xoff + bin_w - 1;
			y2 = yoff + bin_h - 1;

			DEBUG_MSG("bin_h=%d, yoff=%d, bin_w=%d, xoff=%d",
					bin_h, yoff, bin_w, xoff);

			OUT_PKT3(ring, CP_SET_BIN, 3);
			OUT_RING(ring, 0x00000000);
			OUT_RING(ring, CP_SET_BIN_1_X1(x1) | CP_SET_BIN_1_Y1(y1));
			OUT_RING(ring, CP_SET_BIN_2_X2(x2) | CP_SET_BIN_2_Y2(y2));

			/* setup scissor/offset for current tile: */
			OUT_PKT0(ring, REG_A3XX_RB_WINDOW_OFFSET, 1);
			OUT_RING(ring, A3XX_RB_WINDOW_OFFSET_X(xoff) |
					A3XX_RB_WINDOW_OFFSET_Y(yoff));

			OUT_PKT0(ring, REG_A3XX_GRAS_SC_WINDOW_SCISSOR_TL, 2);
			OUT_RING(ring, A3XX_GRAS_SC_WINDOW_SCISSOR_TL_X(0) |
					A3XX_GRAS_SC_WINDOW_SCISSOR_TL_Y(0));
			OUT_RING(ring, A3XX_GRAS_SC_WINDOW_SCISSOR_BR_X(surface->width - 1) |
					A3XX_GRAS_SC_WINDOW_SCISSOR_BR_Y(surface->height - 1));

			OUT_PKT0(ring, REG_A3XX_GRAS_SC_SCREEN_SCISSOR_TL, 2);
			OUT_RING(ring, A3XX_GRAS_SC_SCREEN_SCISSOR_TL_X(x1) |
					A3XX_GRAS_SC_SCREEN_SCISSOR_TL_Y(y1));
			OUT_RING(ring, A3XX_GRAS_SC_SCREEN_SCISSOR_BR_X(x2) |
					A3XX_GRAS_SC_SCREEN_SCISSOR_BR_Y(y2));

			/* emit IB to drawcmds: */
			OUT_IB  (ring, state->draw_start, state->draw_end);

			/* emit gmem2mem to transfer tile back to system memory: */
			emit_gmem2mem(state, ring, surface, xoff, yoff);

			OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
			OUT_RING(ring, 0x00000000);

			xoff += bin_w;
		}

		yoff += bin_h;
	}

	fd_ringmarker_flush(state->draw_end);
	fd_ringbuffer_flush(ring);
	fd_pipe_wait(state->pipe, fd_ringbuffer_timestamp(ring));
	fd_ringbuffer_reset(state->ring);

	fd_ringmarker_mark(state->draw_start);

	state->dirty = false;

	return 0;
}

/* ************************************************************************* */

struct fd_surface * fd_surface_new_fmt(struct fd_state *state,
		uint32_t width, uint32_t height, enum a3xx_color_fmt color_format)
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

	surface->bo = fd_bo_new(state->dev,
			surface->pitch * surface->height * surface->cpp,
			DRM_FREEDRENO_GEM_TYPE_KMEM);
	return surface;
}

struct fd_surface * fd_surface_new(struct fd_state *state,
		uint32_t width, uint32_t height)
{
	return fd_surface_new_fmt(state, width, height, RB_R8G8B8A8_UNORM);
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
	uint32_t max_width = 256;

	if ((state->rb_depth_control & A3XX_RB_DEPTH_CONTROL_Z_ENABLE) |
			(state->rb_stencil_control & A3XX_RB_STENCIL_CONTROL_STENCIL_ENABLE)) {
		gmem_size /= 2;
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
	uint32_t bw, bh;
	int i;

	attach_render_target(state, surface);
	set_viewport(state, 0, 0, surface->width, surface->height);

	bw = state->render_target.bin_w;
	bh = state->render_target.bin_h;

	emit_mem_write(state, state->solid_const,
			init_shader_const, ARRAY_SIZE(init_shader_const));

	OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_REG_RMW, 3);
	OUT_RING(ring, REG_A3XX_RBBM_CLOCK_CTL);
	OUT_RING(ring, 0xfffcffff);
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_INVALIDATE_STATE, 1);
	OUT_RING(ring, 0x00007fff);

	OUT_PKT0(ring, REG_A3XX_VFD_INDEX_MIN, 4);
	OUT_RING(ring, 0x00000000);        /* VFD_INDEX_MIN */
	OUT_RING(ring, 0xffffffff);        /* VFD_INDEX_MAX */
	OUT_RING(ring, 0x00000000);        /* VFD_INSTANCEID_OFFSET */
	OUT_RING(ring, 0x00000000);        /* VFD_INDEX_OFFSET */

	OUT_PKT0(ring, REG_A3XX_SP_VS_PVT_MEM_PARAM_REG, 2);
	OUT_RING(ring, 0x08000001);                  /* SP_VS_PVT_MEM_PARAM_REG */
	OUT_RELOC(ring, state->vs_pvt_mem, 0, 0);    /* SP_VS_PVT_MEM_ADDR_REG */

	OUT_PKT0(ring, REG_A3XX_SP_FS_PVT_MEM_PARAM_REG, 2);
	OUT_RING(ring, 0x08000001);                  /* SP_FS_PVT_MEM_PARAM_REG */
	OUT_RELOC(ring, state->fs_pvt_mem, 0, 0);    /* SP_FS_PVT_MEM_ADDR_REG */

	OUT_PKT0(ring, REG_A3XX_PC_VERTEX_REUSE_BLOCK_CNTL, 1);
	OUT_RING(ring, 0x0000000b);                  /* PC_VERTEX_REUSE_BLOCK_CNTL */

	OUT_PKT0(ring, REG_A3XX_GRAS_CL_CLIP_CNTL, 1);
	OUT_RING(ring, 0x00000000);                  /* GRAS_CL_CLIP_CNTL */

	OUT_PKT0(ring, REG_A3XX_RB_MODE_CONTROL, 1);
	OUT_RING(ring, A3XX_RB_MODE_CONTROL_RENDER_MODE(RB_RENDERING_PASS) |
			A3XX_RB_MODE_CONTROL_MARB_CACHE_SPLIT_MODE);

	emit_mrt(state, ring, surface);

	OUT_PKT0(ring, REG_A3XX_GRAS_SC_CONTROL, 1);
	OUT_RING(ring, A3XX_GRAS_SC_CONTROL_RENDER_MODE(RB_RENDERING_PASS) |
			A3XX_GRAS_SC_CONTROL_MSAA_SAMPLES(MSAA_ONE) |
			A3XX_GRAS_SC_CONTROL_RASTER_MODE(0));

	OUT_PKT0(ring, REG_A3XX_RB_MSAA_CONTROL, 2);
	OUT_RING(ring, A3XX_RB_MSAA_CONTROL_DISABLE |
			A3XX_RB_MSAA_CONTROL_SAMPLES(MSAA_ONE) |
			A3XX_RB_MSAA_CONTROL_SAMPLE_MASK(0xffff));
	OUT_RING(ring, 0x00000000);        /* UNKNOWN_20C3 */

	OUT_PKT0(ring, REG_A3XX_GRAS_CL_GB_CLIP_ADJ, 1);
	OUT_RING(ring, A3XX_GRAS_CL_GB_CLIP_ADJ_HORZ(0) |
			A3XX_GRAS_CL_GB_CLIP_ADJ_VERT(0));

	OUT_PKT0(ring, REG_A3XX_GRAS_TSE_DEBUG_ECO, 1);
	OUT_RING(ring, 0x00000001);        /* GRAS_TSE_DEBUG_ECO */

	OUT_PKT0(ring, REG_A3XX_GRAS_SU_MODE_CONTROL, 1);
	OUT_RING(ring, state->gras_su_mode_control);

	OUT_PKT0(ring, REG_A3XX_GRAS_SU_POINT_MINMAX, 2);
	OUT_RING(ring, 0xffc00010);        /* GRAS_SU_POINT_MINMAX */
	OUT_RING(ring, 0x00000000);        /* GRAS_SU_POINT_SIZE */

	OUT_PKT0(ring, REG_A3XX_TPL1_TP_VS_TEX_OFFSET, 1);
	OUT_RING(ring, A3XX_TPL1_TP_VS_TEX_OFFSET_SAMPLEROFFSET(0) |
			A3XX_TPL1_TP_VS_TEX_OFFSET_MEMOBJOFFSET(0) |
			A3XX_TPL1_TP_VS_TEX_OFFSET_BASETABLEPTR(0));

	OUT_PKT0(ring, REG_A3XX_TPL1_TP_FS_TEX_OFFSET, 1);
	OUT_RING(ring, A3XX_TPL1_TP_FS_TEX_OFFSET_SAMPLEROFFSET(16) |
			A3XX_TPL1_TP_FS_TEX_OFFSET_MEMOBJOFFSET(16) |
			A3XX_TPL1_TP_FS_TEX_OFFSET_BASETABLEPTR(224));

	OUT_PKT0(ring, REG_A3XX_GRAS_SC_WINDOW_SCISSOR_TL, 2);
	OUT_RING(ring, A3XX_GRAS_SC_WINDOW_SCISSOR_TL_X(0) |
			A3XX_GRAS_SC_WINDOW_SCISSOR_TL_Y(0));
	OUT_RING(ring, A3XX_GRAS_SC_WINDOW_SCISSOR_BR_X(surface->width - 1) |
			A3XX_GRAS_SC_WINDOW_SCISSOR_BR_Y(surface->height - 1));

	OUT_PKT0(ring, REG_A3XX_GRAS_SC_SCREEN_SCISSOR_TL, 2);
	OUT_RING(ring, A3XX_GRAS_SC_SCREEN_SCISSOR_TL_X(0) |
			A3XX_GRAS_SC_SCREEN_SCISSOR_TL_Y(0));
	OUT_RING(ring, A3XX_GRAS_SC_SCREEN_SCISSOR_BR_X(bw - 1) |
			A3XX_GRAS_SC_SCREEN_SCISSOR_BR_Y(bh - 1));

	OUT_PKT0(ring, REG_A3XX_VPC_VARY_CYLWRAP_ENABLE_0, 2);
	OUT_RING(ring, 0x00000000);        /* VPC_VARY_CYLWRAP_ENABLE_0 */
	OUT_RING(ring, 0x00000000);        /* VPC_VARY_CYLWRAP_ENABLE_1 */

	OUT_PKT0(ring, REG_A3XX_UNKNOWN_0E43, 1);
	OUT_RING(ring, 0x00000001);        /* UNKNOWN_0E43 */

	OUT_PKT0(ring, REG_A3XX_UNKNOWN_0F03, 1);
	OUT_RING(ring, 0x00000001);        /* UNKNOWN_0f03 */

	OUT_PKT0(ring, REG_A3XX_UNKNOWN_0EE0, 1);
	OUT_RING(ring, 0x00000003);        /* UNKNOWN_0EE0 */

	OUT_PKT0(ring, REG_A3XX_UNKNOWN_0C3D, 1);
	OUT_RING(ring, 0x00000001);        /* UNKNOWN_0C3D */

	OUT_PKT0(ring, REG_A3XX_PC_RESTART_INDEX, 1);
	OUT_RING(ring, 0xffffffff);        /* PC_RESTART_INDEX */

	OUT_PKT0(ring, REG_A3XX_HLSQ_CONST_VSPRESV_RANGE_REG, 2);
	OUT_RING(ring, A3XX_HLSQ_CONST_VSPRESV_RANGE_REG_STARTENTRY(0) |
			A3XX_HLSQ_CONST_VSPRESV_RANGE_REG_ENDENTRY(0));
	OUT_RING(ring, A3XX_HLSQ_CONST_FSPRESV_RANGE_REG_STARTENTRY(0) |
			A3XX_HLSQ_CONST_FSPRESV_RANGE_REG_ENDENTRY(0));

	OUT_PKT0(ring, REG_A3XX_UCHE_CACHE_MODE_CONTROL_REG, 1);
	OUT_RING(ring, 0x00000001);        /* UCHE_CACHE_MODE_CONTROL_REG */

	OUT_PKT0(ring, REG_A3XX_VSC_BIN_SIZE, 2);
	OUT_RING(ring, A3XX_VSC_BIN_SIZE_WIDTH(bw) |
			A3XX_VSC_BIN_SIZE_HEIGHT(bh));
	OUT_RELOC(ring, state->solid_const, /* VSC_SIZE_ADDRESS */
			sizeof(init_shader_const), 0);

	/* since we aren't using binning, just try to assign all bins
	 * to same pipe for now:
	 */
	for (i = 0; i < 1; i++) {
		struct fd_bo *bo = state->vsc_pipe[i].bo;

		if (!bo) {
			bo = fd_bo_new(state->dev, 0x40000,
					DRM_FREEDRENO_GEM_TYPE_KMEM);
			state->vsc_pipe[i].bo = bo;
		}

		OUT_PKT0(ring, REG_A3XX_VSC_PIPE(0), 3);
		OUT_RING(ring, A3XX_VSC_PIPE_CONFIG_X(0) |
				A3XX_VSC_PIPE_CONFIG_Y(0) |
				A3XX_VSC_PIPE_CONFIG_W(state->render_target.nbins_x) |
				A3XX_VSC_PIPE_CONFIG_H(state->render_target.nbins_y));
		OUT_RELOC(ring, bo, 0, 0);               /* VSC_PIPE[0].DATA_ADDRESS */
		OUT_RING(ring, fd_bo_size(bo) - 32);     /* VSC_PIPE[0].DATA_LENGTH */
	}

	OUT_PKT0(ring, REG_A3XX_RB_DEPTH_INFO, 2);
	if (state->rb_stencil_control & A3XX_RB_STENCIL_CONTROL_STENCIL_ENABLE) {
		OUT_RING(ring, A3XX_RB_DEPTH_INFO_DEPTH_FORMAT(DEPTHX_24_8) |
				A3XX_RB_DEPTH_INFO_DEPTH_BASE(bw * bh));
		OUT_RING(ring, A3XX_RB_DEPTH_PITCH(bw * 4));
	} else {
		OUT_RING(ring, A3XX_RB_DEPTH_INFO_DEPTH_FORMAT(DEPTHX_16) |
				A3XX_RB_DEPTH_INFO_DEPTH_BASE(bw * bh));
		OUT_RING(ring, A3XX_RB_DEPTH_PITCH(bw * 2));
	}

	OUT_PKT0(ring, REG_A3XX_RB_WINDOW_OFFSET, 1);
	OUT_RING(ring, A3XX_RB_WINDOW_OFFSET_X(0) |
			A3XX_RB_WINDOW_OFFSET_Y(0));

	OUT_PKT0(ring, REG_A3XX_RB_DEPTH_CONTROL, 1);
	OUT_RING(ring, state->rb_depth_control);

	OUT_PKT0(ring, REG_A3XX_RB_STENCILREFMASK, 2);
	OUT_RING(ring, state->rb_stencilrefmask);    /* RB_STENCILREFMASK */
	OUT_RING(ring, state->rb_stencilrefmask);    /* RB_STENCILREFMASK_BF */

	OUT_PKT0(ring, REG_A3XX_RB_BLEND_RED, 4);
	OUT_RING(ring, 0x00000000);        /* RB_BLEND_RED */
	OUT_RING(ring, 0x00000000);        /* RB_BLEND_GREEN */
	OUT_RING(ring, 0x00000000);        /* RB_BLEND_BLUE */
	OUT_RING(ring, 0x3c0000ff);        /* RB_BLEND_ALPHA */

	OUT_PKT0(ring, REG_A3XX_RB_STENCIL_CONTROL, 1);
	OUT_RING(ring, state->rb_stencil_control);

	OUT_PKT3(ring, CP_WAIT_FOR_IDLE, 1);
	OUT_RING(ring, 0x00000000);

	OUT_PKT3(ring, CP_REG_RMW, 3);
	OUT_RING(ring, REG_A3XX_RB_RENDER_CONTROL);
	OUT_RING(ring, A3XX_RB_RENDER_CONTROL_BIN_WIDTH__MASK);
	OUT_RING(ring, 0x2000 | /* XXX */
			state->rb_render_control);

	OUT_PKT0(ring, REG_A3XX_GRAS_CL_VPORT_XOFFSET, 6);
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_XOFFSET(state->viewport.offset.x));
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_XSCALE(state->viewport.scale.x));
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_YOFFSET(state->viewport.offset.y));
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_YSCALE(state->viewport.scale.y));
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_ZOFFSET(state->viewport.offset.z));
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_ZSCALE(state->viewport.scale.z));

	OUT_PKT0(ring, REG_A3XX_RB_FRAME_BUFFER_DIMENSION, 1);
	OUT_RING(ring, A3XX_RB_FRAME_BUFFER_DIMENSION_WIDTH(surface->width) |
			A3XX_RB_FRAME_BUFFER_DIMENSION_HEIGHT(surface->height));

	OUT_PKT0(ring, REG_A3XX_GRAS_CL_CLIP_CNTL, 1);
	OUT_RING(ring, A3XX_GRAS_CL_CLIP_CNTL_IJ_PERSP_CENTER);

	fd_ringbuffer_flush(ring);

	fd_ringmarker_mark(state->draw_start);
}

static int dump_hex(void *buf, uint32_t w, uint32_t h, uint32_t p, bool flt)
{
	uint32_t *dbuf = buf;
	float   *fbuf = buf;
	uint32_t i, j;

	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			uint32_t off = (i * p) + j;
			off *= 4;  /* convert to vec4 (f32f32f32f32) */
			if (flt) {
				printf("\t\t\t%08X:   %08x %08x %08x %08x\t\t %8.8f %8.8f %8.8f %8.8f\n",
						(unsigned int)(off * 4),
						dbuf[off+0], dbuf[off+1], dbuf[off+2], dbuf[off+3],
						fbuf[off+0], fbuf[off+1], fbuf[off+2], fbuf[off+3]);
			} else {
				printf("\t\t\t%08X:   %08x %08x %08x %08x\n",
						(unsigned int)(off * 4),
						dbuf[off+0], dbuf[off+1], dbuf[off+2], dbuf[off+3]);
			}
		}
		printf("\t\t\t********\n");
	}

	return 0;

}

/* really just for float32 buffers.. */
int fd_dump_hex(struct fd_surface *surface)
{
	return dump_hex(fd_bo_map(surface->bo), surface->width,
			surface->height, surface->pitch, true);
}

int fd_dump_hex_bo(struct fd_bo *bo, bool flt)
{
	uint32_t sizedwords = fd_bo_size(bo) / 4;
	return dump_hex(fd_bo_map(bo), sizedwords / 4, 1, sizedwords, flt);
}

int fd_dump_bmp(struct fd_surface *surface, const char *filename)
{
	return bmp_dump(fd_bo_map(surface->bo),
			surface->width, surface->height,
			surface->pitch * surface->cpp,
			filename);
}

int fd_query_start(struct fd_state *state)
{
	if (state->query.active)
		return -1;

	state->query.active = true;
	emit_query(state, true);

	return 0;
}

int fd_query_end(struct fd_state *state)
{
	if (!state->query.active)
		return -1;
	state->query.active = false;
	return 0;
}

int fd_query_read(struct fd_state *state, struct fd_perfctrs *ctrs)
{
	struct fd_perfctrs last_ctrs;
	struct fd_bo *bo = state->query.bo;
	uint32_t offset = 0;
	uint8_t *ptr;

	if (state->query.active)
		return -1;

	if (!bo)
		return -1;

	memset(ctrs, 0, sizeof(*ctrs));

	fd_bo_cpu_prep(bo, state->pipe, DRM_FREEDRENO_PREP_READ);

	ptr = fd_bo_map(bo);

	while (offset < state->query.offset) {
		struct fd_perfctrs *cur = (void *)(ptr + offset);
		if (offset > 0) {
			int i;
			for (i = 0; i < ARRAY_SIZE(ctrs->ctr); i++) {
				ctrs->ctr[i] += cur->ctr[i] - last_ctrs.ctr[i];
			}
		}
		last_ctrs = *cur;
		offset += sizeof(struct fd_perfctrs);
	}

	fd_bo_cpu_fini(bo);

	fd_bo_del(state->query.bo);
	memset(&state->query, 0, sizeof(state->query));

	return 0;
}

void fd_query_dump(struct fd_perfctrs *ctrs)
{
#define dump_ctr(n) do { \
	printf("%s:\t%016llu\n", #n, ctrs->n); \
} while (0)

	dump_ctr(ctr0);
	dump_ctr(ctr1);
	dump_ctr(ctr2);
	dump_ctr(ctr3);
	dump_ctr(ctr4);
	dump_ctr(ctr5);
	dump_ctr(ctr6);
	dump_ctr(ctr7);
	dump_ctr(ctr8);
	dump_ctr(ctr9);
	dump_ctr(ctrA);
	dump_ctr(ctrB);
	dump_ctr(ctrC);
	dump_ctr(ctrD);
	dump_ctr(ctrE);
	dump_ctr(ctrF);
}
