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

#endif /* FREEDRENO_A2XX_REG_H_ */
