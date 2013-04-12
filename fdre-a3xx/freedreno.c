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

	/* buffers for private memory for vert/frag shaders: */
	struct fd_bo *vs_pvt_mem, *fs_pvt_mem;

	/* shader program: */
	struct fd_program *program;

	/* uniform related params: */
	struct fd_parameters uniforms, solid_uniforms;

	/* attribute related params: */
	struct fd_parameters attributes, solid_attributes;

	/* texture related params: */
	struct fd_parameters textures;

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
	enum a3xx_color_format format;
};

/* ************************************************************************* */
/* color format info */

static int color2cpp[] = {
		[RB_R8G8B8A8_UNORM] = 4,
};

static enum a3xx_fmt color2fmt[] = {
		[RB_R8G8B8A8_UNORM]        = FMT_UBYTE_8_8_8_8,
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
		"(sy)(ss)(rpt3)mov.f16f16 hr0.x, (r)hc0.x                \n"
		"end                                                     \n"
		"nop                                                     \n"
		"nop                                                     \n";

static const float init_shader_const[] = {
		-1.000000, +1.000000, +1.000000, +1.000000,
		-1.000000, +1.000000, +0.000000, +0.000000,
		+1.000000, +1.000000
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

	fd_pipe_get_param(state->ws->pipe, FD_GMEM_SIZE, &val);
	state->gmemsize_bytes = val;

	fd_pipe_get_param(state->ws->pipe, FD_DEVICE_ID, &val);
	state->device_id = val;

	state->ring = fd_ringbuffer_new(state->ws->pipe, 0x10000);
	state->draw_start = fd_ringmarker_new(state->ring);
	state->draw_end = fd_ringmarker_new(state->ring);
	state->ring_tile = fd_ringbuffer_new(state->ws->pipe, 0x10000);

	state->solid_const = fd_bo_new(state->ws->dev, 0x1000, 0);

	state->vs_pvt_mem = fd_bo_new(state->ws->dev, 0x2000, 0);
	state->fs_pvt_mem = fd_bo_new(state->ws->dev, 0x2000, 0);

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

	/* manually setup the solid-program attribute/uniform: */
	p = find_param(&state->solid_attributes, "aPosition");
	p->fmt = FMT_FLOAT_32_32_32;
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
			A3XX_PC_PRIM_VTX_CNTL_POLYMODE_FRONT_PTYPE(PC_DRAW_POINTS) |
			A3XX_PC_PRIM_VTX_CNTL_POLYMODE_FRONT_PTYPE(PC_DRAW_POINTS);
	state->gras_su_mode_control =
			A3XX_GRAS_SU_MODE_CONTROL_LINEHALFWIDTH(4);
	state->rb_render_control =
			A3XX_RB_RENDER_CONTROL_ALPHA_TEST_FUNC(g2a(GL_ALWAYS));
	state->rb_depth_control =
			A3XX_RB_DEPTH_CONTROL_Z_WRITE_ENABLE |
			A3XX_RB_DEPTH_CONTROL_EARLY_Z_ENABLE |
			A3XX_RB_DEPTH_CONTROL_ZFUNC(g2a(GL_LESS)) |
			A3XX_RB_DEPTH_CONTROL_BF_ENABLE;
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
				A3XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(FACTOR_ONE) |
				A3XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(FACTOR_ZERO) |
				A3XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(FACTOR_ZERO);
		state->rb_mrt[i].control =
				A3XX_RB_MRT_CONTROL_ROP_CODE(12) |
				A3XX_RB_MRT_CONTROL_DITHER_MODE(DITHER_ALWAYS);
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

/* for VBO's */
struct fd_bo * fd_attribute_bo_new(struct fd_state *state,
		uint32_t size, const void *data)
{
	struct fd_bo *bo = fd_bo_new(state->ws->dev, size, 0);
	memcpy(fd_bo_map(bo), data, size);
	return bo;
}

int fd_attribute_bo(struct fd_state *state, const char *name,
		enum a3xx_fmt fmt, struct fd_bo * bo)
{
	struct fd_param *p = find_param(&state->attributes, name);
	if (!p)
		return -1;
	p->fmt  = fmt;
	p->bo   = bo;
	return 0;
}

int fd_attribute_pointer(struct fd_state *state, const char *name,
		enum a3xx_fmt fmt, uint32_t count, const void *data)
{
	uint32_t size = fmt2size(fmt) * count;
	struct fd_bo *bo = fd_bo_new(state->ws->dev, size, 0);
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
	struct fd_param *p = find_param(&state->textures, name);
	if (!p)
		return -1;
	p->tex = tex;
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

/* emit cmdstream to blit from GMEM back to the surface */
static void emit_gmem2mem(struct fd_state *state,
		struct fd_ringbuffer *ring, struct fd_surface *surface,
		uint32_t xoff, uint32_t yoff)
{
	fd_program_emit_state(state->solid_program,
			NULL, &state->solid_attributes, ring);

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

	OUT_PKT0(ring, REG_A3XX_RB_MODE_CONTROL, 2);
	OUT_RING(ring, A3XX_RB_MODE_CONTROL_RENDER_MODE(RB_RESOLVE_PASS) |
			A3XX_RB_MODE_CONTROL_MARB_CACHE_SPLIT_MODE);
	OUT_RING(ring, A3XX_RB_RENDER_CONTROL_BIN_WIDTH(state->render_target.bin_w) |
			A3XX_RB_RENDER_CONTROL_DISABLE_COLOR_PIPE |
			A3XX_RB_RENDER_CONTROL_ALPHA_TEST_FUNC(FUNC_NEVER) |
			0x2000 /* XXX */);

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
			A3XX_RB_COPY_DEST_INFO_FORMAT(RB_R8G8B8A8_UNORM) |
			A3XX_RB_COPY_DEST_INFO_COMPONENT_ENABLE(0xf) |
			A3XX_RB_COPY_DEST_INFO_ENDIAN(ENDIAN_NONE));
/*
opcode: EVENT_WRITE (46) (2 dwords)
event PERFCOUNTER_STOP
        c0004600 00000018
 */

	emit_draw_indx(ring, DI_PT_RECTLIST, INDEX_SIZE_IGN, 2, NULL, 0, 0);
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

	state->dirty = true;

	// TODO set state according to whether we clear color/depth/stencil..

	fd_program_emit_state(state->solid_program,
			&state->solid_uniforms, &state->solid_attributes,
			ring);

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
		state->rb_depth_control |= A3XX_RB_DEPTH_CONTROL_Z_ENABLE;
		return 0;
	case GL_STENCIL_TEST:
		state->rb_stencil_control |= (A3XX_RB_STENCIL_CONTROL_STENCIL_ENABLE |
				A3XX_RB_STENCIL_CONTROL_STENCIL_ENABLE2);
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
		state->rb_depth_control &= ~A3XX_RB_DEPTH_CONTROL_Z_ENABLE;
		return 0;
	case GL_STENCIL_TEST:
		state->rb_stencil_control &= ~(A3XX_RB_STENCIL_CONTROL_STENCIL_ENABLE |
				A3XX_RB_STENCIL_CONTROL_STENCIL_ENABLE2);
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

int fd_tex_param(struct fd_state *state, GLenum name, GLint param)
{
	switch (name) {
	default:
		ERROR_MSG("unsupported name: 0x%04x", name);
		return -1;
	}
}

static int draw_impl(struct fd_state *state, GLenum mode,
		GLint first, GLsizei count, GLenum type, const GLvoid *indices)
{
	// XXX TODO
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
				OUT_PKT3(ring, CP_SET_CONSTANT, 2);
				OUT_RING(ring, CP_REG(REG_A3XX_PA_SC_WINDOW_OFFSET));
				OUT_RING(ring, A3XX_PA_SC_WINDOW_OFFSET_X(-xoff) |
						A3XX_PA_SC_WINDOW_OFFSET_Y(-yoff));/* PA_SC_WINDOW_OFFSET */

				OUT_PKT3(ring, CP_SET_CONSTANT, 3);
				OUT_RING(ring, CP_REG(REG_A3XX_GRAS_SC_WINDOW_SCISSOR_TL));
				OUT_RING(ring, xy2d(0,0));            /* GRAS_SC_WINDOW_SCISSOR_TL */
				OUT_RING(ring, xy2d(surface->width,   /* GRAS_SC_WINDOW_SCISSOR_BR */
						surface->height));

				OUT_PKT3(ring, CP_SET_CONSTANT, 3);
				OUT_RING(ring, CP_REG(REG_A3XX_GRAS_SC_SCREEN_SCISSOR_TL));
				OUT_RING(ring, xy2d(0,0));            /* GRAS_SC_SCREEN_SCISSOR_TL */
				OUT_RING(ring, xy2d(bin_w, bin_h));   /* GRAS_SC_SCREEN_SCISSOR_BR */

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
		uint32_t width, uint32_t height, enum a3xx_color_format color_format)
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
	uint32_t max_width = 992;

	if ((state->rb_depth_control & A3XX_RB_DEPTH_CONTROL_Z_ENABLE) |
			(state->rb_stencil_control & A3XX_RB_STENCIL_CONTROL_STENCIL_ENABLE)) {
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
	uint32_t base, bw, bh;
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

	OUT_PKT0(ring, REG_A3XX_SP_VS_PVT_MEM_CTRL_REG, 2);
	OUT_RING(ring, 0x08000001);                  /* SP_VS_PVT_MEM_CTRL_REG */
	OUT_RELOC(ring, state->vs_pvt_mem, 0, 0);    /* SP_VS_PVT_MEM_ADDR_REG */

	OUT_PKT0(ring, REG_A3XX_SP_FS_PVT_MEM_CTRL_REG, 2);
	OUT_RING(ring, 0x08000001);                  /* SP_FS_PVT_MEM_CTRL_REG */
	OUT_RELOC(ring, state->fs_pvt_mem, 0, 0);    /* SP_FS_PVT_MEM_ADDR_REG */

	OUT_PKT0(ring, REG_A3XX_PC_VERTEX_REUSE_BLOCK_CNTL, 1);
	OUT_RING(ring, 0x0000000b);                  /* PC_VERTEX_REUSE_BLOCK_CNTL */

	OUT_PKT0(ring, REG_A3XX_GRAS_CL_CLIP_CNTL, 1);
	OUT_RING(ring, 0x00000000);                  /* GRAS_CL_CLIP_CNTL */

	OUT_PKT0(ring, REG_A3XX_RB_MODE_CONTROL, 1);
	OUT_RING(ring, A3XX_RB_MODE_CONTROL_RENDER_MODE(RB_RENDERING_PASS) |
			A3XX_RB_MODE_CONTROL_MARB_CACHE_SPLIT_MODE);

	for (i = 0; i < 4; i++) {
		uint32_t pitch = (i == 0) ? (bw * surface->cpp) : 0;
		enum a3xx_color_format format = (i == 0) ? surface->color : 0;

		OUT_PKT0(ring, REG_A3XX_RB_MRT_CONTROL(i), 4);
		OUT_RING(ring, A3XX_RB_MRT_CONTROL_READ_DEST_ENABLE |
				A3XX_RB_MRT_CONTROL_ROP_CODE(12) |
				A3XX_RB_MRT_CONTROL_DITHER_MODE(DITHER_ALWAYS) |
				A3XX_RB_MRT_CONTROL_COMPONENT_ENABLE(0xf));
		OUT_RING(ring, A3XX_RB_MRT_BUF_INFO_COLOR_FORMAT(format) |
				A3XX_RB_MRT_BUF_INFO_COLOR_TILE_MODE(TILE_32X32) |
				A3XX_RB_MRT_BUF_INFO_COLOR_BUF_PITCH(pitch));
		OUT_RING(ring, A3XX_RB_MRT_BUF_BASE_COLOR_BUF_BASE(0));
		OUT_RING(ring, A3XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(FACTOR_ONE) |
				A3XX_RB_MRT_BLEND_CONTROL_RGB_BLEND_OPCODE(BLEND_DST_PLUS_SRC) |
				A3XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(FACTOR_ZERO) |
				A3XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(FACTOR_ONE) |
				A3XX_RB_MRT_BLEND_CONTROL_ALPHA_BLEND_OPCODE(BLEND_DST_PLUS_SRC) |
				A3XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(FACTOR_ZERO) |
				A3XX_RB_MRT_BLEND_CONTROL_CLAMP_ENABLE);

		OUT_PKT0(ring, REG_A3XX_SP_FS_IMAGE_OUTPUT_REG(i), 1);
		OUT_RING(ring, A3XX_SP_FS_IMAGE_OUTPUT_REG_MRTFORMAT(format));
	}

	OUT_PKT0(ring, REG_A3XX_GRAS_SC_CONTROL, 1);
	OUT_RING(ring, A3XX_GRAS_SC_CONTROL_RENDER_MODE(0) |
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

	OUT_PKT0(ring, REG_A3XX_UNKNOWN_0C81, 1);
	OUT_RING(ring, 0x00000001);        /* UNKNOWN_0C81 */

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

	OUT_PKT0(ring, REG_A3XX_RB_DEPTH_INFO, 2);
	OUT_RING(ring, A3XX_RB_DEPTH_INFO_DEPTH_FORMAT(DEPTHX_16) |
			A3XX_RB_DEPTH_INFO_DEPTH_BASE(bw * bh));
	OUT_RING(ring, A3XX_RB_DEPTH_PITCH(bw * surface->cpp));

	OUT_PKT0(ring, REG_A3XX_PA_SC_WINDOW_OFFSET, 1);
	OUT_RING(ring, A3XX_PA_SC_WINDOW_OFFSET_X(0) |
			A3XX_PA_SC_WINDOW_OFFSET_Y(0));

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

	OUT_PKT0(ring, REG_A3XX_RB_RENDER_CONTROL, 1);
	OUT_RING(ring, 0x2000 | /* XXX */
			state->rb_render_control |
			A3XX_RB_RENDER_CONTROL_BIN_WIDTH(bw));

	OUT_PKT0(ring, REG_A3XX_GRAS_CL_VPORT_XOFFSET, 6);
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_XOFFSET(state->viewport.offset.x));
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_XSCALE(state->viewport.scale.x));
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_YOFFSET(state->viewport.offset.y));
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_YSCALE(state->viewport.scale.y));
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_ZOFFSET(state->viewport.offset.z));
	OUT_RING(ring, A3XX_GRAS_CL_VPORT_ZSCALE(state->viewport.scale.z));

	OUT_PKT0(ring, REG_A3XX_RB_WINDOW_SIZE, 1);
	OUT_RING(ring, A3XX_RB_WINDOW_SIZE_WIDTH(surface->width) |
			A3XX_RB_WINDOW_SIZE_HEIGHT(surface->height));

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
