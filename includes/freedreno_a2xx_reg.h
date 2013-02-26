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

#ifndef FREEDRENO_A2XX_REG_H_
#define FREEDRENO_A2XX_REG_H_

#include <GLES2/gl2.h>

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

/* convert float to 12.4 fixed point */
static inline uint32_t f2d12_4(float f)
{
	return (uint32_t)(f * 8.0);
}

/* convert x,y to dword */
static inline uint32_t xy2d(uint16_t x, uint16_t y)
{
	return ((y & 0x3fff) << 16) | (x & 0x3fff);
}

/*
 * Registers that we have figured out but are not in kernel:
 */

#define REG_CLEAR_COLOR			0x220b
#define REG_PA_CL_VPORT_XOFFSET	0x2110
#define REG_PA_CL_VPORT_YSCALE		0x2111
#define REG_PA_CL_VPORT_YOFFSET	0x2112
#define REG_RB_COPY_DEST_BASE		0x2319
#define REG_RB_COPY_DEST_PITCH		0x231a
#define REG_RB_COPY_DEST_INFO		0x231b
#define REG_RB_COPY_DEST_OFFSET	0x231c  /* ?? */
#define REG_RB_COLOR_INFO			0x2001

#define REG_PA_SU_VTX_CNTL			0x2302
#define REG_PA_CL_GB_VERT_CLIP_ADJ	0x2303
#define REG_PA_CL_GB_VERT_DISC_ADJ	0x2304
#define REG_PA_CL_GB_HORZ_CLIP_ADJ	0x2305
#define REG_PA_CL_GB_HORZ_DISC_ADJ	0x2306

#define REG_RB_BLEND_RED			0x2105
#define REG_RB_BLEND_GREEN			0x2106
#define REG_RB_BLEND_BLUE			0x2107
#define REG_RB_BLEND_ALPHA			0x2108
#define REG_RB_ALPHA_REF			0x210e
#define REG_RB_BLEND_CONTROL		0x2201

#define REG_SQ_CONTEXT_MISC		0x2181
#define REG_SQ_VS_CONST			0x2307
#define REG_PA_SU_POINT_MINMAX		0x2281
#define REG_RB_STENCILREFMASK		0x210d
#define REG_RB_BC_CONTROL			0x0f01
#define REG_VGT_OUT_DEALLOC_CNTL	0x2317
#define REG_VGT_CURRENT_BIN_ID_MAX	0x2203
#define REG_VGT_CURRENT_BIN_ID_MIN	0x2207

/* added in a220: */
#define REG_VSC_PIPE_CONFIG_0       0x0c06
#define REG_VSC_PIPE_DATA_ADDRESS_0 0x0c07
#define REG_VSC_PIPE_DATA_LENGTH_0  0x0c08
#define REG_VSC_PIPE_CONFIG_1       0x0c09
#define REG_VSC_PIPE_DATA_ADDRESS_1 0x0c0a
#define REG_VSC_PIPE_DATA_LENGTH_1  0x0c0b
#define REG_VSC_PIPE_CONFIG_2       0x0c0c
#define REG_VSC_PIPE_DATA_ADDRESS_2 0x0c0d
#define REG_VSC_PIPE_DATA_LENGTH_2  0x0c0e
#define REG_VSC_PIPE_CONFIG_3       0x0c0f
#define REG_VSC_PIPE_DATA_ADDRESS_3 0x0c10
#define REG_VSC_PIPE_DATA_LENGTH_3  0x0c11
#define REG_VSC_PIPE_CONFIG_4       0x0c12
#define REG_VSC_PIPE_DATA_ADDRESS_4 0x0c13
#define REG_VSC_PIPE_DATA_LENGTH_4  0x0c14
#define REG_VSC_PIPE_CONFIG_5       0x0c15
#define REG_VSC_PIPE_DATA_ADDRESS_5 0x0c16
#define REG_VSC_PIPE_DATA_LENGTH_5  0x0c17
#define REG_VSC_PIPE_CONFIG_6       0x0c18
#define REG_VSC_PIPE_DATA_ADDRESS_6 0x0c19
#define REG_VSC_PIPE_DATA_LENGTH_6  0x0c1a
#define REG_VSC_PIPE_CONFIG_7       0x0c1b
#define REG_VSC_PIPE_DATA_ADDRESS_7 0x0c1c
#define REG_VSC_PIPE_DATA_LENGTH_7  0x0c1d

/* unnamed registers: */
#define REG_0c02		0x0c02
#define REG_0c04		0x0c04
#define REG_0c06		0x0c06
#define REG_2010		0x2010


/*
 * Format for 2nd dword in CP_DRAW_INDX and friends:
 */

/* see VGT_PRIMITIVE_TYPE.PRIM_TYPE? */
enum pc_di_primtype {
	DI_PT_NONE = 0,
	DI_PT_POINTLIST = 1,
	DI_PT_LINELIST = 2,
	DI_PT_LINESTRIP = 3,
	DI_PT_TRILIST = 4,
	DI_PT_TRIFAN = 5,
	DI_PT_TRISTRIP = 6,
	DI_PT_RECTLIST = 8,
	DI_PT_QUADLIST = 13,
	DI_PT_QUADSTRIP = 14,
	DI_PT_POLYGON = 15,
	DI_PT_2D_COPY_RECT_LIST_V0 = 16,
	DI_PT_2D_COPY_RECT_LIST_V1 = 17,
	DI_PT_2D_COPY_RECT_LIST_V2 = 18,
	DI_PT_2D_COPY_RECT_LIST_V3 = 19,
	DI_PT_2D_FILL_RECT_LIST = 20,
	DI_PT_2D_LINE_STRIP = 21,
	DI_PT_2D_TRI_STRIP = 22,
};

/* see VGT:VGT_DRAW_INITIATOR.SOURCE_SELECT? */
enum pc_di_src_sel {
	DI_SRC_SEL_DMA = 0,
	DI_SRC_SEL_IMMEDIATE = 1,
	DI_SRC_SEL_AUTO_INDEX = 2,
	DI_SRC_SEL_RESERVED = 3,
};

/* see VGT_DMA_INDEX_TYPE.INDEX_TYPE? */
enum pc_di_index_size {
	INDEX_SIZE_IGN    = 0,
	INDEX_SIZE_16_BIT = 0,
	INDEX_SIZE_32_BIT = 1,
	INDEX_SIZE_8_BIT  = 2,
	INDEX_SIZE_INVALID
};

enum pc_di_vis_cull_mode {
	IGNORE_VISIBILITY = 0,
};

static inline uint32_t DRAW(enum pc_di_primtype prim_type,
		enum pc_di_src_sel source_select, enum pc_di_index_size index_size,
		enum pc_di_vis_cull_mode vis_cull_mode)
{
	return (prim_type         << 0) |
			(source_select     << 6) |
			((index_size & 1)  << 11) |
			((index_size >> 1) << 13) |
			(vis_cull_mode     << 9) |
			(1                 << 14);
}


/*
 * Bits for VGT_CURRENT_BIN_ID_MIN/MAX:
 */

#define VGT_CURRENT_BIN_ID_MIN_COLUMN(val)                (((val) & 0x7) << 0)
#define VGT_CURRENT_BIN_ID_MIN_ROW(val)                   (((val) & 0x7) << 3)
#define VGT_CURRENT_BIN_ID_MIN_GUARD_BAND(val)            (((val) & 0x7) << 6)


/*
 * Bits for PA_CL_VTE_CNTL:
 */

#define PA_CL_VTE_CNTL_VPORT_X_SCALE_ENA         0x00000001
#define PA_CL_VTE_CNTL_VPORT_X_OFFSET_ENA        0x00000002
#define PA_CL_VTE_CNTL_VPORT_Y_SCALE_ENA         0x00000004
#define PA_CL_VTE_CNTL_VPORT_Y_OFFSET_ENA        0x00000008
#define PA_CL_VTE_CNTL_VPORT_Z_SCALE_ENA         0x00000010
#define PA_CL_VTE_CNTL_VPORT_Z_OFFSET_ENA        0x00000020
#define PA_CL_VTE_CNTL_VTX_XY_FMT                0x00000100
#define PA_CL_VTE_CNTL_VTX_Z_FMT                 0x00000200
#define PA_CL_VTE_CNTL_VTX_W0_FMT                0x00000400
#define PA_CL_VTE_CNTL_PERFCOUNTER_REF           0x00000800


/*
 * Bits for PA_CL_CLIP_CNTL:
 */

#define PA_CL_CLIP_CNTL_CLIP_DISABLE             0x00010000
#define PA_CL_CLIP_CNTL_BOUNDARY_EDGE_FLAG_ENA   0x00040000
enum dx_clip_space {
	DXCLIP_OPENGL = 0,
	DXCLIP_DIRECTX = 1,
};
static inline uint32_t PA_CL_CLIP_CNTL_DX_CLIP_SPACE_DEF(enum dx_clip_space val)
{
	return val << 19;
}
#define PA_CL_CLIP_CNTL_DIS_CLIP_ERR_DETECT      0x00100000
#define PA_CL_CLIP_CNTL_VTX_KILL_OR              0x00200000
#define PA_CL_CLIP_CNTL_XY_NAN_RETAIN            0x00400000
#define PA_CL_CLIP_CNTL_Z_NAN_RETAIN             0x00800000
#define PA_CL_CLIP_CNTL_W_NAN_RETAIN             0x01000000


/*
 * Bits for PA_SU_SC_MODE_CNTL:
 */

#define PA_SU_SC_MODE_CNTL_CULL_FRONT                      0x00000001
#define PA_SU_SC_MODE_CNTL_CULL_BACK                       0x00000002
#define PA_SU_SC_MODE_CNTL_FACE                            0x00000004
enum pa_su_sc_polymode {
	POLY_DISABLED     = 0,
	POLY_DUALMODE     = 1,
};
static inline uint32_t PA_SU_SC_MODE_CNTL_POLYMODE(enum pa_su_sc_polymode val)
{
	return val << 3;
}
enum pa_su_sc_draw {
	DRAW_POINTS       = 0,
	DRAW_LINES        = 1,
	DRAW_TRIANGLES    = 2,
};
static inline uint32_t PA_SU_SC_MODE_CNTL_POLYMODE_FRONT_PTYPE(enum pa_su_sc_draw val)
{
	return val << 5;
}
static inline uint32_t PA_SU_SC_MODE_CNTL_POLYMODE_BACK_PTYPE(enum pa_su_sc_draw val)
{
	return val << 8;
}
#define PA_SU_SC_MODE_CNTL_POLY_OFFSET_FRONT_ENABLE        0x00000800
#define PA_SU_SC_MODE_CNTL_POLY_OFFSET_BACK_ENABLE         0x00001000
#define PA_SU_SC_MODE_CNTL_POLY_OFFSET_PARA_ENABLE         0x00002000
#define PA_SU_SC_MODE_CNTL_MSAA_ENABLE                     0x00008000
#define PA_SU_SC_MODE_CNTL_VTX_WINDOW_OFFSET_ENABLE        0x00010000
#define PA_SU_SC_MODE_CNTL_LINE_STIPPLE_ENABLE             0x00040000
#define PA_SU_SC_MODE_CNTL_PROVOKING_VTX_LAST              0x00080000
#define PA_SU_SC_MODE_CNTL_PERSP_CORR_DIS                  0x00100000
#define PA_SU_SC_MODE_CNTL_MULTI_PRIM_IB_ENA               0x00200000
#define PA_SU_SC_MODE_CNTL_QUAD_ORDER_ENABLE               0x00800000
#define PA_SU_SC_MODE_CNTL_WAIT_RB_IDLE_ALL_TRI            0x02000000
#define PA_SU_SC_MODE_CNTL_WAIT_RB_IDLE_FIRST_TRI_NEW_STATE 0x04000000
#define PA_SU_SC_MODE_CNTL_CLAMPED_FACENESS                0x10000000
#define PA_SU_SC_MODE_CNTL_ZERO_AREA_FACENESS              0x20000000
#define PA_SU_SC_MODE_CNTL_FACE_KILL_ENABLE                0x40000000
#define PA_SU_SC_MODE_CNTL_FACE_WRITE_ENABLE               0x80000000


/*
 * Bits for PA_SC_LINE_STIPPLE:
 */

#define PA_SC_LINE_STIPPLE_LINE_PATTERN(val)     ((val) & 0x0000ffff)
#define PA_SC_LINE_STIPPLE_REPEAT_COUNT(val)     (((val) << 16) & 0x00ff0000)
enum pa_sc_pattern_bit_order {
	PATTERN_BIT_ORDER_LITTLE = 0,
	PATTERN_BIT_ORDER_BIG    = 1,
};
static inline uint32_t PA_SC_LINE_STIPPLE_PATTERN_BIT_ORDER(enum pa_sc_pattern_bit_order val)
{
	return val << 28;
}
enum pa_sc_auto_reset_cntl {
	AUTO_RESET_NEVER          = 0,
	AUTO_RESET_EACH_PRIMITIVE = 1,
	AUTO_RESET_EACH_PACKET    = 2,
};
static inline uint32_t PA_SC_LINE_STIPPLE_AUTO_RESET_CNTL(enum pa_sc_auto_reset_cntl val)
{
	return val << 29;
}


/*
 * Bits for PA_SC_LINE_CNTL:
 */

#define PA_SC_LINE_CNTL_BRES_CNTL_MASK(val)      ((val) & 0x000000ff)
#define PA_SC_LINE_CNTL_USE_BRES_CNTL            0x00000100
#define PA_SC_LINE_CNTL_EXPAND_LINE_WIDTH        0x00000200
#define PA_SC_LINE_CNTL_LAST_PIXEL               0x00000400


/*
 * Bits for PA_SU_VTX_CNTL:
 */

enum pa_pixcenter {
	PIXCENTER_D3D = 0,
	PIXCENTER_OGL = 1,
};
static inline uint32_t PA_SU_VTX_CNTL_PIX_CENTER(enum pa_pixcenter val)
{
	return val;
}

enum pa_roundmode {
	TRUNCATE = 0,
	ROUND = 1,
	ROUNDTOEVEN = 2,
	ROUNDTOODD = 3,
};
static inline uint32_t PA_SU_VTX_CNTL_ROUND_MODE_MASK(enum pa_roundmode val)
{
	return val << 1;
}

enum pa_quantmode {
	ONE_SIXTEENTH = 0,
	ONE_EIGHTH = 1,
	ONE_QUARTER = 2,
	ONE_HALF = 3,
	ONE = 4,
};
static inline uint32_t PA_SU_VTX_CNTL_QUANT_MODE(enum pa_quantmode val)
{
	return val << 3;
}


/*
 * Bits for PA_SU_POINT_SIZE:
 */

#define PA_SU_POINT_SIZE_HEIGHT(val)   (f2d12_4(val) & 0xffff)
#define PA_SU_POINT_SIZE_WIDTH(val)    ((f2d12_4(val) << 16) & 0xffff)


/*
 * Bits for PA_SU_POINT_MINMAX:
 */

#define PA_SU_POINT_MINMAX_MIN_SIZE(val)    (f2d12_4(val) & 0xffff)
#define PA_SU_POINT_MINMAX_MAX_SIZE(val)    ((f2d12_4(val) << 16) & 0xffff)


/*
 * Bits for PA_SU_LINE_CNTL:
 */

#define PA_SU_LINE_CNTL_WIDTH(val)     (f2d12_4(val) & 0xffff)


/*
 * Bits for PA_SC_WINDOW_OFFSET:
 * (seems to be same as r600)
 */
#define PA_SC_WINDOW_OFFSET_X(val)     ((val) & 0x7fff)
#define PA_SC_WINDOW_OFFSET_Y(val)     (((val) & 0x7fff) << 16)

#define PA_SC_WINDOW_OFFSET_DISABLE    0x80000000


/*
 * Bits for SQ_CONTEXT_MISC:
 */

#define SQ_CONTEXT_MISC_INST_PRED_OPTIMIZE  0x00000001
#define SQ_CONTEXT_MISC_SC_OUTPUT_SCREEN_XY 0x00000002
enum sq_sample_cntl {
	CENTROIDS_ONLY = 0,
	CENTERS_ONLY = 1,
	CENTROIDS_AND_CENTERS = 2,
};
static inline uint32_t SQ_CONTEXT_MISC_SC_SAMPLE_CNTL(enum sq_sample_cntl val)
{
	return (val & 0x3) << 2;
}
#define SQ_CONTEXT_MISC_PARAM_GEN_POS(val)  (((val) & 0xff) << 8)
#define SQ_CONTEXT_MISC_PERFCOUNTER_REF     0x00010000
#define SQ_CONTEXT_MISC_YEILD_OPTIMIZE      0x00020000
#define SQ_CONTEXT_MISC_TX_CACHE_SEL        0x00040000


/*
 * Bits for SQ_PROGRAM_CNTL:
 */
/* note: only 0x3f worth of valid register values, but high bit is
 * set to indicate '0 registers used':
 */
#define SQ_PROGRAM_CNTL_VS_REGS(val)   ((val) & 0xff)
#define SQ_PROGRAM_CNTL_PS_REGS(val)   (((val) & 0xff) << 8)
#define SQ_PROGRAM_CNTL_VS_RESOURCE    0x00010000
#define SQ_PROGRAM_CNTL_PS_RESOURCE    0x00020000
#define SQ_PROGRAM_CNTL_PARAM_GEN      0x00040000
#define SQ_PROGRAM_CNTL_GEN_INDEX_PIX  0x00080000
#define SQ_PROGRAM_CNTL_VS_EXPORT_COUNT(val) (((val) & 0xf) << 20)
#define SQ_PROGRAM_CNTL_VS_EXPORT_MODE(val)  (((val) & 0x7) << 24)
enum sq_ps_vtx_mode {
	POSITION_1_VECTOR              = 0,
	POSITION_2_VECTORS_UNUSED      = 1,
	POSITION_2_VECTORS_SPRITE      = 2,
	POSITION_2_VECTORS_EDGE        = 3,
	POSITION_2_VECTORS_KILL        = 4,
	POSITION_2_VECTORS_SPRITE_KILL = 5,
	POSITION_2_VECTORS_EDGE_KILL   = 6,
	MULTIPASS                      = 7,
};
static inline uint32_t SQ_PROGRAM_CNTL_PS_EXPORT_MODE(enum sq_ps_vtx_mode val)
{
	return val << 27;
}
#define SQ_PROGRAM_CNTL_GEN_INDEX_VTX  0x80000000


/*
 * Bits for SQ_VS_CONST
 */

#define SQ_VS_CONST_BASE(val)          ((val) & 0x1ff)
#define SQ_VS_CONST_SIZE(val)          (((val) & 0x1ff) << 12)


/*
 * Bits for SQ_PS_CONST
 */

#define SQ_PS_CONST_BASE(val)          ((val) & 0x1ff)
#define SQ_PS_CONST_SIZE(val)          (((val) & 0x1ff) << 12)


/*
 * Bits for tex sampler:
 */

/* dword0 */
enum sq_tex_clamp {
	SQ_TEX_WRAP                    = 0,    /* GL_REPEAT */
	SQ_TEX_MIRROR                  = 1,    /* GL_MIRRORED_REPEAT */
	SQ_TEX_CLAMP_LAST_TEXEL        = 2,    /* GL_CLAMP_TO_EDGE */
	/* TODO confirm these: */
	SQ_TEX_MIRROR_ONCE_LAST_TEXEL  = 3,
	SQ_TEX_CLAMP_HALF_BORDER       = 4,
	SQ_TEX_MIRROR_ONCE_HALF_BORDER = 5,
	SQ_TEX_CLAMP_BORDER            = 6,
	SQ_TEX_MIRROR_ONCE_BORDER      = 7,
};
static inline uint32_t SQ_TEX0_CLAMP_X(enum sq_tex_clamp val)
{
	return (val & 0x7) << 10;
}
static inline uint32_t SQ_TEX0_CLAMP_Y(enum sq_tex_clamp val)
{
	return (val & 0x7) << 13;
}
static inline uint32_t SQ_TEX0_CLAMP_Z(enum sq_tex_clamp val)
{
	return (val & 0x7) << 16;
}
#define SQ_TEX0_PITCH(val)             (((val) >> 5) << 22)

/* dword2 */
#define SQ_TEX2_HEIGHT(val)            (((val) - 1) << 13)
#define SQ_TEX2_WIDTH(val)             ((val) - 1)

/* dword3 */
enum sq_tex_swiz {
	SQ_TEX_X    = 0,
	SQ_TEX_Y    = 1,
	SQ_TEX_Z    = 2,
	SQ_TEX_W    = 3,
	SQ_TEX_ZERO = 4,
	SQ_TEX_ONE  = 5,
};
static inline uint32_t SQ_TEX3_SWIZ_X(enum sq_tex_swiz val)
{
	return (val & 0x7) << 1;
}
static inline uint32_t SQ_TEX3_SWIZ_Y(enum sq_tex_swiz val)
{
	return (val & 0x7) << 4;
}
static inline uint32_t SQ_TEX3_SWIZ_Z(enum sq_tex_swiz val)
{
	return (val & 0x7) << 7;
}
static inline uint32_t SQ_TEX3_SWIZ_W(enum sq_tex_swiz val)
{
	return (val & 0x7) << 10;
}

enum sq_tex_filter {
	SQ_TEX_FILTER_POINT    = 0,
	SQ_TEX_FILTER_BILINEAR = 1,
	SQ_TEX_FILTER_BICUBIC  = 2,  /* presumed */
};
static inline uint32_t SQ_TEX3_XY_MAG_FILTER(enum sq_tex_filter val)
{
	return (val & 0x3) << 19;
}
static inline uint32_t SQ_TEX3_XY_MIN_FILTER(enum sq_tex_filter val)
{
	return (val & 0x3) << 21;
}


/*
 * Bits for RB_BLEND_CONTROL:
 */

enum rb_blend_op {
	RB_BLEND_ZERO = 0,
	RB_BLEND_ONE = 1,
	RB_BLEND_SRC_COLOR = 4,
	RB_BLEND_ONE_MINUS_SRC_COLOR = 5,
	RB_BLEND_SRC_ALPHA = 6,
	RB_BLEND_ONE_MINUS_SRC_ALPHA = 7,
	RB_BLEND_DST_COLOR = 8,
	RB_BLEND_ONE_MINUS_DST_COLOR = 9,
	RB_BLEND_DST_ALPHA = 10,
	RB_BLEND_ONE_MINUS_DST_ALPHA = 11,
	RB_BLEND_CONSTANT_COLOR = 12,
	RB_BLEND_ONE_MINUS_CONSTANT_COLOR = 13,
	RB_BLEND_CONSTANT_ALPHA = 14,
	RB_BLEND_ONE_MINUS_CONSTANT_ALPHA = 15,
	RB_BLEND_SRC_ALPHA_SATURATE = 16,
};

enum rb_comb_func {
	COMB_DST_PLUS_SRC = 0,
	COMB_SRC_MINUS_DST = 1,
	COMB_MIN_DST_SRC = 2,
	COMB_MAX_DST_SRC = 3,
	COMB_DST_MINUS_SRC = 4,
	COMB_DST_PLUS_SRC_BIAS = 5,
};

#define RB_BLENDCONTROL_COLOR_SRCBLEND_MASK      0x0000001f
static inline uint32_t RB_BLENDCONTROL_COLOR_SRCBLEND(enum rb_blend_op val)
{
	return val & RB_BLENDCONTROL_COLOR_SRCBLEND_MASK;
}
#define RB_BLENDCONTROL_COLOR_COMB_FCN_MASK      0x000000e0
static inline uint32_t RB_BLENDCONTROL_COLOR_COMB_FCN(enum rb_comb_func val)
{
	return (val << 5) & RB_BLENDCONTROL_COLOR_COMB_FCN_MASK;
}
#define RB_BLENDCONTROL_COLOR_DESTBLEND_MASK     0x00001f00
static inline uint32_t RB_BLENDCONTROL_COLOR_DESTBLEND(enum rb_blend_op val)
{
	return (val << 8) & RB_BLENDCONTROL_COLOR_DESTBLEND_MASK;
}
#define RB_BLENDCONTROL_ALPHA_SRCBLEND_MASK      0x001f0000
static inline uint32_t RB_BLENDCONTROL_ALPHA_SRCBLEND(enum rb_blend_op val)
{
	return (val << 16) & RB_BLENDCONTROL_ALPHA_SRCBLEND_MASK;
}
#define RB_BLENDCONTROL_ALPHA_COMB_FCN_MASK      0x00e00000
static inline uint32_t RB_BLENDCONTROL_ALPHA_COMB_FCN(enum rb_comb_func val)
{
	return (val << 21) & RB_BLENDCONTROL_ALPHA_COMB_FCN_MASK;
}
#define RB_BLENDCONTROL_ALPHA_DESTBLEND_MASK     0x1f000000
static inline uint32_t RB_BLENDCONTROL_ALPHA_DESTBLEND(enum rb_blend_op val)
{
	return (val << 24) & RB_BLENDCONTROL_ALPHA_DESTBLEND_MASK;
}
#define RB_BLENDCONTROL_BLEND_FORCE_ENABLE       0x20000000
#define RB_BLENDCONTROL_BLEND_FORCE              0x40000000


/*
 * Bits for RB_COLOR_MASK:
 */
#define RB_COLOR_MASK_WRITE_RED                  0x00000001
#define RB_COLOR_MASK_WRITE_GREEN                0x00000002
#define RB_COLOR_MASK_WRITE_BLUE                 0x00000004
#define RB_COLOR_MASK_WRITE_ALPHA                0x00000008


/*
 * Bits for RB_COLOR_INFO:
 */

#define RB_COLOR_INFO_COLOR_FORMAT_MASK          0x0000000f
static inline uint32_t RB_COLOR_INFO_COLOR_FORMAT(enum COLORFORMATX val)
{
	return val & RB_COLOR_INFO_COLOR_FORMAT_MASK;
}

#define RB_COLOR_INFO_COLOR_ROUND_MODE(val)      (((val) & 0x3) << 4)
#define RB_COLOR_INFO_COLOR_LINEAR               0x00000040
#define RB_COLOR_INFO_COLOR_ENDIAN(val)          (((val) & 0x3) << 7)
#define RB_COLOR_INFO_COLOR_SWAP(val)            (((val) & 0x3) << 9)
#define RB_COLOR_INFO_COLOR_BASE(val)            (((val) & 0xfffff) << 12)


/*
 * Bits for RB_MODECONTROL:
 */

enum rb_edram_mode {
	EDRAM_NOP = 0,
	COLOR_DEPTH = 4,
	DEPTH_ONLY = 5,
	EDRAM_COPY = 6,
};
static inline uint32_t RB_MODECONTROL_EDRAM_MODE(enum rb_edram_mode val)
{
	return val & 0x7;
}


/*
 * Bits for RB_DEPTHCONTROL:
 */

#define RB_DEPTHCONTROL_STENCIL_ENABLE      0x00000001
#define RB_DEPTHCONTROL_Z_ENABLE            0x00000002
#define RB_DEPTHCONTROL_Z_WRITE_ENABLE      0x00000004
#define RB_DEPTHCONTROL_EARLY_Z_ENABLE      0x00000008
#define RB_DEPTHCONTROL_ZFUNC_MASK          0x00000070
#define RB_DEPTHCONTROL_ZFUNC(depth_func) \
	(((depth_func) << 4) & RB_DEPTHCONTROL_ZFUNC_MASK)
#define RB_DEPTHCONTROL_BACKFACE_ENABLE     0x00000080
#define RB_DEPTHCONTROL_STENCILFUNC_MASK    0x00000700
#define RB_DEPTHCONTROL_STENCILFUNC(depth_func) \
	(((depth_func) << 8) & RB_DEPTHCONTROL_STENCILFUNC_MASK)
enum rb_stencil_op {
	STENCIL_KEEP = 0,
	STENCIL_ZERO = 1,
	STENCIL_REPLACE = 2,
	STENCIL_INCR_CLAMP = 3,
	STENCIL_DECR_CLAMP = 4,
	STENCIL_INVERT = 5,
	STENCIL_INCR_WRAP = 6,
	STENCIL_DECR_WRAP = 7
};
#define RB_DEPTHCONTROL_STENCILFAIL_MASK         0x00003800
static inline uint32_t RB_DEPTHCONTROL_STENCILFAIL(enum rb_stencil_op val)
{
	return (val << 11) & RB_DEPTHCONTROL_STENCILFAIL_MASK;
}
#define RB_DEPTHCONTROL_STENCILZPASS_MASK        0x0001c000
static inline uint32_t RB_DEPTHCONTROL_STENCILZPASS(enum rb_stencil_op val)
{
	return (val << 14) & RB_DEPTHCONTROL_STENCILZPASS_MASK;
}
#define RB_DEPTHCONTROL_STENCILZFAIL_MASK        0x000e0000
static inline uint32_t RB_DEPTHCONTROL_STENCILZFAIL(enum rb_stencil_op val)
{
	return (val << 17) & RB_DEPTHCONTROL_STENCILZFAIL_MASK;
}
#define RB_DEPTHCONTROL_STENCILFUNC_BF_MASK      0x00700000
#define RB_DEPTHCONTROL_STENCILFUNC_BF(depth_func) \
	(((depth_func) << 20) & RB_DEPTHCONTROL_STENCILFUNC_BF_MASK)
#define RB_DEPTHCONTROL_STENCILFAIL_BF_MASK      0x03800000
static inline uint32_t RB_DEPTHCONTROL_STENCILFAIL_BF(enum rb_stencil_op val)
{
	return (val << 23) & RB_DEPTHCONTROL_STENCILFAIL_BF_MASK;
}
#define RB_DEPTHCONTROL_STENCILZPASS_BF_MASK     0x1c000000
static inline uint32_t RB_DEPTHCONTROL_STENCILZPASS_BF(enum rb_stencil_op val)
{
	return (val << 26) & RB_DEPTHCONTROL_STENCILZPASS_BF_MASK;
}
#define RB_DEPTHCONTROL_STENCILZFAIL_BF_MASK     0xe0000000
static inline uint32_t RB_DEPTHCONTROL_STENCILZFAIL_BF(enum rb_stencil_op val)
{
	return (val << 29) & RB_DEPTHCONTROL_STENCILZFAIL_BF_MASK;
}


/*
 * Bits for RB_COPY_DEST_INFO:
 */

enum rb_surface_endian {
	ENDIAN_NONE = 0,
	ENDIAN_8IN16 = 1,
	ENDIAN_8IN32 = 2,
	ENDIAN_16IN32 = 3,
	ENDIAN_8IN64 = 4,
	ENDIAN_8IN128 = 5,
};
static inline uint32_t RB_COPY_DEST_INFO_DEST_ENDIAN(enum rb_surface_endian val)
{
	return (val & 0x7) << 0;
}
#define RB_COPY_DEST_INFO_LINEAR       0x00000008
static inline uint32_t RB_COPY_DEST_INFO_FORMAT(enum COLORFORMATX val)
{
	return val << 4;
}
#define RB_COPY_DEST_INFO_SWAP(val)    (((val) & 0x3) << 8) /* maybe VGT_DMA_SWAP_MODE? */
enum rb_dither_mode {
	DITHER_DISABLE = 0,
	DITHER_ALWAYS = 1,
	DITHER_IF_ALPHA_OFF = 2,
};
static inline uint32_t RB_COPY_DEST_INFO_DITHER_MODE(enum rb_dither_mode val)
{
	return val << 10;
}
enum rb_dither_type {
	DITHER_PIXEL = 0,
	DITHER_SUBPIXEL = 1,
};
static inline uint32_t RB_COPY_DEST_INFO_DITHER_TYPE(enum rb_dither_type val)
{
	return val << 12;
}
#define RB_COPY_DEST_INFO_WRITE_RED    0x00004000
#define RB_COPY_DEST_INFO_WRITE_GREEN  0x00008000
#define RB_COPY_DEST_INFO_WRITE_BLUE   0x00010000
#define RB_COPY_DEST_INFO_WRITE_ALPHA  0x00020000


/*
 * Bits for RB_COPY_DEST_OFFSET:
 */

#define RB_COPY_DEST_OFFSET_X(val)    ((val) & 0x3fff)
#define RB_COPY_DEST_OFFSET_Y(val)    (((val) & 0x3fff) << 13)


/*
 * Bits for RB_COPY_CONTROL:
 */

#define RB_COPY_CONTROL_DEPTH_CLEAR_ENABLE  0x00000008L
#define RB_COPY_CONTROL_CLEAR_MASK(val)     ((val & 0xf) << 4)


/*
 * Bits for RB_COLORCONTROL:
 */

#define RB_COLORCONTROL_ALPHA_FUNC(val)          ((val) & 0x7)
#define RB_COLORCONTROL_ALPHA_TEST_ENABLE        0x00000008
#define RB_COLORCONTROL_ALPHA_TO_MASK_ENABLE     0x00000010
#define RB_COLORCONTROL_BLEND_DISABLE            0x00000020
#define RB_COLORCONTROL_FOG_ENABLE               0x00000040
#define RB_COLORCONTROL_VS_EXPORTS_FOG           0x00000080
#define RB_COLORCONTROL_ROP_CODE(val)            (((val) & 0xf) << 8)
static inline uint32_t RB_COLORCONTROL_DITHER_MODE(enum rb_dither_mode val)
{
	return (val & 0x3) << 12;
}
static inline uint32_t RB_COLORCONTROL_DITHER_TYPE(enum rb_dither_type val)
{
	return (val & 0x3) << 14;
}
#define RB_COLORCONTROL_PIXEL_FOG                0x00010000
#define RB_COLORCONTROL_ALPHA_TO_MASK_OFFSET0(val) (((val) & 0x3) << 24)
#define RB_COLORCONTROL_ALPHA_TO_MASK_OFFSET1(val) (((val) & 0x3) << 26)
#define RB_COLORCONTROL_ALPHA_TO_MASK_OFFSET2(val) (((val) & 0x3) << 28)
#define RB_COLORCONTROL_ALPHA_TO_MASK_OFFSET3(val) (((val) & 0x3) << 30)


/*
 * Bits for RB_DEPTH_INFO:
 */

enum rb_depth_format {
	DEPTHX_16 = 0,
	DEPTHX_24_8 = 1,
	DEPTHX_INVALID,
};

static inline uint32_t RB_DEPTH_INFO_DEPTH_FORMAT(enum rb_depth_format val)
{
	return val & 0x1;
}
#define RB_DEPTH_INFO_DEPTH_BASE(val)            ((val) << 12)


/*
 * Bits for RB_STENCILREFMASK (RB_STENCILREFMASK_BF is same):
 */

#define RB_STENCILREFMASK_STENCILREF_MASK        0x000000ff
#define RB_STENCILREFMASK_STENCILREF(val)        ((val) & RB_STENCILREFMASK_STENCILREF_MASK)
#define RB_STENCILREFMASK_STENCILMASK_MASK       0x0000ff00
#define RB_STENCILREFMASK_STENCILMASK(val)       (((val) << 8) & RB_STENCILREFMASK_STENCILMASK_MASK)
#define RB_STENCILREFMASK_STENCILWRITEMASK_MASK  0x00ff0000
#define RB_STENCILREFMASK_STENCILWRITEMASK(val)  (((val) << 16) & RB_STENCILREFMASK_STENCILWRITEMASK_MASK)


/*
 * Bits for RB_BC_CONTROL:
 */

#define RB_BC_CONTROL_ACCUM_LINEAR_MODE_ENABLE            0x00000001
#define RB_BC_CONTROL_ACCUM_TIMEOUT_SELECT(val)           (((val) & 0x3) << 1)
#define RB_BC_CONTROL_DISABLE_EDRAM_CAM                   0x00000008
#define RB_BC_CONTROL_DISABLE_EZ_FAST_CONTEXT_SWITCH      0x00000010
#define RB_BC_CONTROL_DISABLE_EZ_NULL_ZCMD_DROP           0x00000020
#define RB_BC_CONTROL_DISABLE_LZ_NULL_ZCMD_DROP           0x00000040
#define RB_BC_CONTROL_ENABLE_AZ_THROTTLE                  0x00000080
#define RB_BC_CONTROL_AZ_THROTTLE_COUNT(val)              (((val) & 0x1f) << 8)
#define RB_BC_CONTROL_ENABLE_CRC_UPDATE                   0x00004000
#define RB_BC_CONTROL_CRC_MODE                            0x00008000
#define RB_BC_CONTROL_DISABLE_SAMPLE_COUNTERS             0x00010000
#define RB_BC_CONTROL_DISABLE_ACCUM                       0x00020000
#define RB_BC_CONTROL_ACCUM_ALLOC_MASK(val)               (((val) & 0xf) << 18)
#define RB_BC_CONTROL_LINEAR_PERFORMANCE_ENABLE           0x00400000
#define RB_BC_CONTROL_ACCUM_DATA_FIFO_LIMIT(val)          (((val) & 0xf) << 23)
#define RB_BC_CONTROL_MEM_EXPORT_TIMEOUT_SELECT(val)      (((val) & 0x3) << 27)
#define RB_BC_CONTROL_MEM_EXPORT_LINEAR_MODE_ENABLE       0x20000000
#define RB_BC_CONTROL_CRC_SYSTEM                          0x40000000
#define RB_BC_CONTROL_RESERVED6                           0x80000000


/*
 * Bits for RBBM_PM_OVERRIDE1:
 */

#define RBBM_PM_OVERRIDE1_RBBM_AHBCLK_PM_OVERRIDE         0x00000001
#define RBBM_PM_OVERRIDE1_SC_REG_SCLK_PM_OVERRIDE         0x00000002
#define RBBM_PM_OVERRIDE1_SC_SCLK_PM_OVERRIDE             0x00000004
#define RBBM_PM_OVERRIDE1_SP_TOP_SCLK_PM_OVERRIDE         0x00000008
#define RBBM_PM_OVERRIDE1_SP_V0_SCLK_PM_OVERRIDE          0x00000010
#define RBBM_PM_OVERRIDE1_SQ_REG_SCLK_PM_OVERRIDE         0x00000020
#define RBBM_PM_OVERRIDE1_SQ_REG_FIFOS_SCLK_PM_OVERRIDE   0x00000040
#define RBBM_PM_OVERRIDE1_SQ_CONST_MEM_SCLK_PM_OVERRIDE   0x00000080
#define RBBM_PM_OVERRIDE1_SQ_SQ_SCLK_PM_OVERRIDE          0x00000100
#define RBBM_PM_OVERRIDE1_SX_SCLK_PM_OVERRIDE             0x00000200
#define RBBM_PM_OVERRIDE1_SX_REG_SCLK_PM_OVERRIDE         0x00000400
#define RBBM_PM_OVERRIDE1_TCM_TCO_SCLK_PM_OVERRIDE        0x00000800
#define RBBM_PM_OVERRIDE1_TCM_TCM_SCLK_PM_OVERRIDE        0x00001000
#define RBBM_PM_OVERRIDE1_TCM_TCD_SCLK_PM_OVERRIDE        0x00002000
#define RBBM_PM_OVERRIDE1_TCM_REG_SCLK_PM_OVERRIDE        0x00004000
#define RBBM_PM_OVERRIDE1_TPC_TPC_SCLK_PM_OVERRIDE        0x00008000
#define RBBM_PM_OVERRIDE1_TPC_REG_SCLK_PM_OVERRIDE        0x00010000
#define RBBM_PM_OVERRIDE1_TCF_TCA_SCLK_PM_OVERRIDE        0x00020000
#define RBBM_PM_OVERRIDE1_TCF_TCB_SCLK_PM_OVERRIDE        0x00040000
#define RBBM_PM_OVERRIDE1_TCF_TCB_READ_SCLK_PM_OVERRIDE   0x00080000
#define RBBM_PM_OVERRIDE1_TP_TP_SCLK_PM_OVERRIDE          0x00100000
#define RBBM_PM_OVERRIDE1_TP_REG_SCLK_PM_OVERRIDE         0x00200000
#define RBBM_PM_OVERRIDE1_CP_G_SCLK_PM_OVERRIDE           0x00400000
#define RBBM_PM_OVERRIDE1_CP_REG_SCLK_PM_OVERRIDE         0x00800000
#define RBBM_PM_OVERRIDE1_CP_G_REG_SCLK_PM_OVERRIDE       0x01000000
#define RBBM_PM_OVERRIDE1_SPI_SCLK_PM_OVERRIDE            0x02000000
#define RBBM_PM_OVERRIDE1_RB_REG_SCLK_PM_OVERRIDE         0x04000000
#define RBBM_PM_OVERRIDE1_RB_SCLK_PM_OVERRIDE             0x08000000
#define RBBM_PM_OVERRIDE1_MH_MH_SCLK_PM_OVERRIDE          0x10000000
#define RBBM_PM_OVERRIDE1_MH_REG_SCLK_PM_OVERRIDE         0x20000000
#define RBBM_PM_OVERRIDE1_MH_MMU_SCLK_PM_OVERRIDE         0x40000000
#define RBBM_PM_OVERRIDE1_MH_TCROQ_SCLK_PM_OVERRIDE       0x80000000


/*
 * Bits for RBBM_PM_OVERRIDE2:
 */

#define RBBM_PM_OVERRIDE2_PA_REG_SCLK_PM_OVERRIDE         0x00000001
#define RBBM_PM_OVERRIDE2_PA_PA_SCLK_PM_OVERRIDE          0x00000002
#define RBBM_PM_OVERRIDE2_PA_AG_SCLK_PM_OVERRIDE          0x00000004
#define RBBM_PM_OVERRIDE2_VGT_REG_SCLK_PM_OVERRIDE        0x00000008
#define RBBM_PM_OVERRIDE2_VGT_FIFOS_SCLK_PM_OVERRIDE      0x00000010
#define RBBM_PM_OVERRIDE2_VGT_VGT_SCLK_PM_OVERRIDE        0x00000020
#define RBBM_PM_OVERRIDE2_DEBUG_PERF_SCLK_PM_OVERRIDE     0x00000040
#define RBBM_PM_OVERRIDE2_PERM_SCLK_PM_OVERRIDE           0x00000080
#define RBBM_PM_OVERRIDE2_GC_GA_GMEM0_PM_OVERRIDE         0x00000100
#define RBBM_PM_OVERRIDE2_GC_GA_GMEM1_PM_OVERRIDE         0x00000200
#define RBBM_PM_OVERRIDE2_GC_GA_GMEM2_PM_OVERRIDE         0x00000400
#define RBBM_PM_OVERRIDE2_GC_GA_GMEM3_PM_OVERRIDE         0x00000800


/*
 * Bits for TC_CNTL_STATUS:
 */

#define TC_CNTL_STATUS_L2_INVALIDATE             0x00000001


#endif /* FREEDRENO_A2XX_REG_H_ */
