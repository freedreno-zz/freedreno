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
	G2D_IDLE                 = 0xfe,
	G2D_COLOR                = 0xff,

	VGV1_DIRTYBASE           = 0x29,
	VGV1_CBASE1              = 0x2a,
	VGV1_UBASE2              = 0x2b,

	VGV3_NEXTADDR            = 0x75,
	VGV3_NEXTCMD             = 0x76,
	VGV3_WRITERAW            = 0x7c,
	VGV3_LAST                = 0x7f,

	GRADW_TEXCFG             = 0xd1,
	GRADW_TEXSIZE            = 0xd2,
	GRADW_TEXBASE            = 0xd3,
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

/* bits for G2D_IDLE: */
#define G2D_IDLE_IRQ         (1 << 0)
#define G2D_IDLE_BCFLUSH     (1 << 1)
#define G2D_IDLE_V3          (1 << 2)

/* bits for G2D_CONFIG: */
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

/* bits for G2D_INPUT: */
#define G2D_INPUT_COLOR                (1 << 0)
#define G2D_INPUT_SCOORD1              (1 << 1)
#define G2D_INPUT_SCOORD2              (1 << 2)
#define G2D_INPUT_COPYCOORD            (1 << 3)
#define G2D_INPUT_VGMODE               (1 << 4)
#define G2D_INPUT_LINEMODE             (1 << 5)

#endif /* FREEDRENO_Z1XX_H_ */
