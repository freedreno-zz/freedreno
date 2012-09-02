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
	POINTLIST = 1,
	LINELIST  = 2,
	LINESTRIP = 3,
	TRILIST   = 4,
	TRIFAN    = 5,
	TRISTRIP  = 6,
	RECTLIST  = 8,
};

/* see VGT:VGT_DRAW_INITIATOR.SOURCE_SELECT? */
enum pc_di_src_sel {
	IMMEDIATE  = 1,
	AUTO_INDEX = 2,
};

/* see VGT_DMA_INDEX_TYPE.INDEX_TYPE? */
enum pc_di_index_size {
	INDEX_SIZE_IGN    = 0,
	INDEX_SIZE_16_BIT = 0,
	INDEX_SIZE_32_BIT = 1,
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
 * Bits for PA_SU_SC_MODE_CNTL:
 * (seems to be same as r600)
 */

#define PA_SU_SC_CULL_FRONT            0x00000001
#define PA_SU_SC_CULL_BACK             0x00000002
#define PA_SU_SC_POLY_OFFSET_FRONT     0x00000800
#define PA_SU_SC_POLY_OFFSET_BACK      0x00001000
#define PA_SU_SC_PROVOKING_VTX_LAST    0x00080000
#define PA_SU_SC_VTX_WINDOW_OFF_ENABLE 0x00010000
enum pa_su_sc_draw {
	POINTS       = 0,
	LINES        = 1,
	TRIANGLES    = 2,
};
static inline uint32_t PA_SU_SC_POLYMODE_FRONT_PTYPE(enum pa_su_sc_draw val)
{
	return val << 5;
}
static inline uint32_t PA_SU_SC_POLYMODE_BACK_PTYPE(enum pa_su_sc_draw val)
{
	return val << 8;
}


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
 * Bits for SQ_PROGRAM_CNTL
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
 * Bits for tex sampler:
 */

/* dword0 */
#define SQ_TEX0_WRAP                   0    /* GL_REPEAT */
#define SQ_TEX0_MIRROR                 1    /* GL_MIRRORED_REPEAT */
#define SQ_TEX0_CLAMP_LAST_TEXEL       2    /* GL_CLAMP_TO_EDGE */
#define SQ_TEX0_CLAMP_X(val)           ((val) << 10)
#define SQ_TEX0_CLAMP_Y(val)           ((val) << 13)
#define SQ_TEX0_PITCH(val)             (((val) >> 5) << 22)

/* dword2 */
#define SQ_TEX2_HEIGHT(val)            (((val) - 1) << 13)
#define SQ_TEX2_WIDTH(val)             ((val) - 1)

/* dword3 */
#define SQ_TEX3_XY_FILTER_POINT        0
#define SQ_TEX3_XY_FILTER_BILINEAR     1
#define SQ_TEX3_XY_FILTER_BICUBIC      2  /* presumed */
#define SQ_TEX3_XY_MAG_FILTER(val)     ((val) << 19)
#define SQ_TEX3_XY_MIN_FILTER(val)     ((val) << 21)


/*
 * Bits for RB_BLEND_CONTROL:
 */
#define RB_BLEND_ZERO                  0x0
#define RB_BLEND_ONE                   0x1
#define RB_BLEND_SRC_COLOR             0x4
#define RB_BLEND_ONE_MINUS_SRC_COLOR   0x5
#define RB_BLEND_SRC_ALPHA             0x6
#define RB_BLEND_ONE_MINUS_SRC_ALPHA   0x7
#define RB_BLEND_DST_COLOR             0x8
#define RB_BLEND_ONE_MINUS_DST_COLOR   0x9
#define RB_BLEND_DST_ALPHA             0xa
#define RB_BLEND_ONE_MINUS_DST_ALPHA   0xb
#define RB_BLEND_CONST_COLOR           0xc
#define RB_BLEND_CONST_ALPHA           0xe
#define RB_BLEND_ONE_MINUS_CONST_ALPHA 0xf

#define RB_BLEND_CONTROL_COLOR_SRC(val)  ((val) << 0)
#define RB_BLEND_CONTROL_COLOR_DST(val)  ((val) << 8)
#define RB_BLEND_CONTROL_ALPHA_SRC(val)  ((val) << 16)
#define RB_BLEND_CONTROL_ALPHA_DST(val)  ((val) << 24)


/*
 * Bits for RB_MODECONTROL
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

#define RB_DEPTHCONTROL_ENABLE         0x00000002
#define RB_DEPTHCONTROL_FUNC_MASK      0x00000070
#define RB_DEPTH_CONTROL_FUNC(depth_func) \
	((((depth_func) - GL_NEVER) << 4) & RB_DEPTHCONTROL_FUNC_MASK)

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
 * Bits for RB_COLORCONTROL:
 */

enum rb_compare_ref {
	REF_NEVER = 0,
	REF_LESS = 1,
	REF_EQUAL = 2,
	REF_LEQUAL = 3,
	REF_GREATER = 4,
	REF_NOTEQUAL = 5,
	REF_GEQUAL = 6,
	REF_ALWAYS = 7
};
static inline uint32_t RB_COLORCONTROL_ALPHA_FUNC(enum rb_compare_ref val)
{
	return val & 0x7;
}
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

#endif /* FREEDRENO_A2XX_REG_H_ */
