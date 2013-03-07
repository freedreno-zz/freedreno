/*
 * Copyright © 2012 Rob Clark <robclark@freedesktop.org>
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

#ifndef FREEDRENO_Z1XX_H_
#define FREEDRENO_Z1XX_H_

/* not entirely sure how much delta there is between z160 and z180..
 * for now I'm assuming they are largely similar
 */

enum z1xx_reg {
	G2D_BASE0                = 0x00,
	G2D_CFG0                 = 0x01,
	G2D_CFG1                 = 0x03,
	G2D_SCISSORX             = 0x08,
	G2D_SCISSORY             = 0x09,
	G2D_FOREGROUND           = 0x0a,
	G2D_BACKGROUND           = 0x0b,
	G2D_ALPHABLEND           = 0x0c,
	G2D_ROP                  = 0x0d,
	G2D_CONFIG               = 0x0e,
	G2D_INPUT                = 0x0f,
	G2D_MASK                 = 0x10,
	G2D_BLENDERCFG           = 0x11,
	G2D_CONST0               = 0xb0,
	G2D_CONST1               = 0xb1,
	G2D_CONST2               = 0xb2,
	G2D_CONST3               = 0xb3,
	G2D_CONST4               = 0xb4,
	G2D_CONST5               = 0xb5,
	G2D_CONST6               = 0xb6,
	G2D_CONST7               = 0xb7,
	G2D_GRADIENT             = 0xd0,
	G2D_XY                   = 0xf0,
	G2D_WIDTHHEIGHT          = 0xf1,
	G2D_SXY                  = 0xf2,
	G2D_SXY2                 = 0xf3,
	G2D_IDLE                 = 0xfe,
	G2D_COLOR                = 0xff,
	G2D_BLEND_A0             = 0x14,
	G2D_BLEND_A1             = 0x15,
	G2D_BLEND_A2             = 0x16,
	G2D_BLEND_A3             = 0x17,
	G2D_BLEND_C0             = 0x18,
	G2D_BLEND_C1             = 0x19,
	G2D_BLEND_C2             = 0x1a,
	G2D_BLEND_C3             = 0x1b,
	G2D_BLEND_C4             = 0x1c,
	G2D_BLEND_C5             = 0x1d,
	G2D_BLEND_C6             = 0x1e,
	G2D_BLEND_C7             = 0x1f,

	VGV1_DIRTYBASE           = 0x29,
	VGV1_CBASE1              = 0x2a,
	VGV1_UBASE2              = 0x2b,

	VGV3_NEXTADDR            = 0x75,
	VGV3_NEXTCMD             = 0x76,
	VGV3_WRITERAW            = 0x7c,
	VGV3_LAST                = 0x7f,

	GRADW_CONST0             = 0xc0,
	GRADW_CONST1             = 0xc1,
	GRADW_CONST2             = 0xc2,
	GRADW_CONST3             = 0xc3,
	GRADW_CONST4             = 0xc4,
	GRADW_CONST5             = 0xc5,
	GRADW_CONST6             = 0xc6,
	GRADW_CONST7             = 0xc7,
	GRADW_CONST8             = 0xc8,
	GRADW_CONST9             = 0xc9,
	GRADW_CONSTA             = 0xca,
	GRADW_CONSTB             = 0xcb,
	GRADW_TEXCFG             = 0xd1,
	GRADW_TEXSIZE            = 0xd2,
	GRADW_TEXBASE            = 0xd3,
	GRADW_TEXCFG2            = 0xd5,
	GRADW_INST0              = 0xe0,
	GRADW_INST1              = 0xe1,
	GRADW_INST2              = 0xe2,
	GRADW_INST3              = 0xe3,
	GRADW_INST4              = 0xe4,
	GRADW_INST5              = 0xe5,
	GRADW_INST6              = 0xe6,
	GRADW_INST7              = 0xe7,

};

enum g2d_format {
	G2D_1         = 0,
	G2D_1BW       = 1,
	G2D_4         = 2,
	G2D_8         = 3,
	G2D_4444      = 4,
	G2D_1555      = 5,
	G2D_0565      = 6,
	G2D_8888      = 7,
	G2D_YUY2      = 8,
	G2D_UYVY      = 9,
	G2D_YVYU      = 10,
	G2D_4444_RGBA = 11,
	G2D_5551_RGBA = 12,
	G2D_8888_RGBA = 13,
	G2D_A8        = 14,
};

enum g2d_wrap {
	G2D_CLAMP   = 0,
	G2D_REPEAT  = 1,
	G2D_MIRROR  = 2,
	G2D_BORDER  = 3,
};

/* used to write one register.. at most 24 bits or maybe less, register
 * value is OR'd with this
 */

static inline uint32_t REG(enum z1xx_reg reg)
{
	return reg << 24;
}

/* used to write one or more registers: */
static inline uint32_t REGM(enum z1xx_reg reg, uint8_t count)
{
	return REG(VGV3_WRITERAW) | (count << 8) | reg;
}


/*
 * Bits for G2D_IDLE:
 */
#define G2D_IDLE_IRQ         (1 << 0)
#define G2D_IDLE_BCFLUSH     (1 << 1)
#define G2D_IDLE_V3          (1 << 2)


/*
 * Bits for G2D_CONFIG:
 */
#define G2D_CONFIG_DST                 (1 << 0)
#define G2D_CONFIG_SRC1                (1 << 1)
#define G2D_CONFIG_SRC2                (1 << 2)
#define G2D_CONFIG_SRC3                (1 << 3)
#define G2D_CONFIG_SRCCK               (1 << 4)
#define G2D_CONFIG_DSTCK               (1 << 5)
#define G2D_CONFIG_ROTATE(val)         (((val) & 0x3) << 6)
#define G2D_CONFIG_OBS_GAMMA           (1 << 8)
#define G2D_CONFIG_IGNORECKALPHA       (1 << 9)
#define G2D_CONFIG_DITHER              (1 << 10)
#define G2D_CONFIG_WRITESRGB           (1 << 11)
#define G2D_CONFIG_ARGBMASK(val)       (((val) & 0xf) << 12)
#define G2D_CONFIG_ALPHATEX            (1 << 16)
#define G2D_CONFIG_PALMLINES           (1 << 17)
#define G2D_CONFIG_NOLASTPIXEL         (1 << 18)
#define G2D_CONFIG_NOPROTECT           (1 << 19)


/*
 * Bits for G2D_INPUT:
 */
#define G2D_INPUT_COLOR                (1 << 0)
#define G2D_INPUT_SCOORD1              (1 << 1)
#define G2D_INPUT_SCOORD2              (1 << 2)
#define G2D_INPUT_COPYCOORD            (1 << 3)
#define G2D_INPUT_VGMODE               (1 << 4)
#define G2D_INPUT_LINEMODE             (1 << 5)


/*
 * Bits for G2D_BLENDERCFG
 */
#define G2D_BLENDERCFG_PASSES(val)     ((val) & 0x7)
#define G2D_BLENDERCFG_ALPHAPASSES(val) (((val) & 0x3) << 3)
#define G2D_BLENDERCFG_ENABLE          (1 << 5)
#define G2D_BLENDERCFG_OOALPHA         (1 << 6)
#define G2D_BLENDERCFG_OBS_DIVALPHA    (1 << 7)
#define G2D_BLENDERCFG_NOMASK          (1 << 8)


/*
 * Bits for G2D_SXY (same for G2D_SXY2):
 * (yes, only 11 bits, compared to 12 bits for XY/WIDTHHEIGHT..)
 */
#define G2D_SXYn_Y(val)                ((val) & 0x7ff)
#define G2D_SXYn_X(val)                (((val) & 0x7ff) << 16)


/*
 * Bits for G2D_XY:
 */
#define G2D_XY_Y(val)                  ((val) & 0xfff)
#define G2D_XY_X(val)                  (((val) & 0xfff) << 16)


/*
 * Bits for G2D_WIDTHHEIGHT:
 */
#define G2D_WIDTHHEIGHT_HEIGHT(val)    ((val) & 0xfff)
#define G2D_WIDTHHEIGHT_WIDTH(val)     (((val) & 0xfff) << 16)


/*
 * Bits for G2D_CFGn:
 */
#define G2D_CFGn_PITCH(val)            ((val) & 0xfff)
static inline uint32_t G2D_CFGn_FORMAT(enum g2d_format fmt)
{
	return (fmt & 0xf) << 12;
}


/*
 * Bits for GRADW_TEXCFG: (similar to C2D_CFGn, but some bitfields different)
 */
#define GRADW_TEXCFG_PITCH(val)        ((val) & 0xfff)
static inline uint32_t GRADW_TEXCFG_FORMAT(enum g2d_format fmt)
{
	return (fmt & 0xf) << 12;
}
#define GRADW_TEXCFG_TILED             (1 << 16)
static inline uint32_t GRADW_TEXCFG_WRAPU(enum g2d_wrap wrap)
{
	return (wrap & 0x3) << 17;
}
static inline uint32_t GRADW_TEXCFG_WRAPV(enum g2d_wrap wrap)
{
	return (wrap & 0x3) << 19;
}
#define GRADW_TEXCFG_BILIN             (1 << 21)
#define GRADW_TEXCFG_SRGB              (1 << 22)
#define GRADW_TEXCFG_PREMULTIPLY       (1 << 23)
#define GRADW_TEXCFG_SWAPWORDS         (1 << 24)
#define GRADW_TEXCFG_SWAPBYTES         (1 << 25)
#define GRADW_TEXCFG_SWAPALL           (1 << 26)
#define GRADW_TEXCFG_SWAPRB            (1 << 27)
#define GRADW_TEXCFG_TEX2D             (1 << 28)
#define GRADW_TEXCFG_SWAPBITS          (1 << 29)


/*
 * Bits for GRADW_TEXCFG2:
 */
#define GRADW_TEXCFG2_ALPHA_TEX        (1 << 7)


/*
 * Bits for GRADW_TEXSIZE:
 */
#define GRADW_TEXSIZE_WIDTH(val)       ((val) & 0x7ff)
#define GRADW_TEXSIZE_HEIGHT(val)      (((val) & 0x7ff) << 13)

#endif /* FREEDRENO_Z1XX_H_ */
