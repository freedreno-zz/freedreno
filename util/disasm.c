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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "disasm.h"


static const char *levels[] = {
		"\t",
		"\t\t",
		"\t\t\t",
		"\t\t\t\t",
		"\t\t\t\t\t",
		"\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t\t\t",
		"x",
		"x",
		"x",
		"x",
		"x",
		"x",
};

#define print_raw     1
#define print_unknown 1   /* raw with already identified bitfields masked */

/*
 * 00      CF:     ADDR(0x2) CNT(0x5)
 *         00955002 00001000 c4000000
 *                 ALU:    10002021 1ffff688 00000002
 *                           ^ ^ ^
 *                           | | +-- src reg (coord, bit offset 5)
 *                           | +---- dst register
 *                           +------ sampler # (CONST(n)
 *                 ALU:    10101021 1ffff688 00000002
 *                 ALU:    10200001 1ffff688 00000002
 *                 ALU:    140f0001 00220000 e0020100
 *                                ^   ^^^^    ^ ^ ^
 *                                |    | |    | | +-- src2
 *                                |    | |    | +---- src1
 *                                |    | |    +------ op, 0:ADDv, 1:MULv
 *                                |    | +----------- src2 swizzle
 *                                |    +------------- src1 swizzle
 *                                |
 *                                +------------------ dst
 *                 ALU:    140f0000 00008800 e1000100
 * 01      CF:     ADDR(0x7) CNT(0x1)
 *         00001007 00002000 00000000
 *                 ALU:    140f8000 00430000 a1000000
 *
 * Interpretation of swizzle fields:
 *
 *       bits 7..6 - chan[3] (w) swizzle
 *            5..4 - chan[2] (z) swizzle
 *            3..2 - chan[1] (y) swizzle
 *            1..0 - chan[0] (x) swizzle
 *
 *       chan[0]: 00 - x
 *                01 - y
 *                10 - z
 *                11 - w
 *
 *       chan[1]: 11 - x
 *                00 - y
 *                01 - z
 *                10 - w
 *
 *       chan[2]: 10 - x
 *                11 - y
 *                00 - z
 *                01 - w
 *
 *       chan[3]: 00 - w
 *                01 - x
 *                10 - y
 *                11 - z
 *
 * Note: .x is same as .xxxx, .y same as .yyyy, etc.  So must be some other
 * bit(s) which control MULv, whether the operand is interpreted as vector
 * or scalar.
 *
 * FETCH instruction format:
 * ----- ----------- ------
 *
 *     dword0:   0..4    -  <UNKNOWN>
 *               5..9?   -  src register
 *              9?..11   -  <UNKNOWN>
 *              12..16?  -  dest register
 *             17?..19   -  <UNKNOWN>
 *              20..23?  -  const
 *
 *     dword1:   0..31   -  <UNKNOWN>
 *
 *     dword2:   0..31   -  <UNKNOWN>
 *
 *
 * ALU instruction format:
 * --- ----------- ------
 *
 *     dword0:   0..4?   -  vector dest register
 *              5?..7    -  <UNKNOWN>
 *               8..12?  -  scalar dest register
 *             13?..14   -  <UNKNOWN>
 *                15     -  export flag
 *              16..19   -  vector dest write mask (xyzw)
 *              20..23   -  scalar dest write mask (xyzw)
 *              24..31   -  <UNKNOWN>
 *
 *     dword 1:  0..1    -  scalar src1 channel
 *                            00 - x
 *                            01 - y
 *                            10 - z
 *                            11 - w
 *               2..5    -  <UNKNOWN>
 *     maybe upper bits of scalar src1??
 *               6..7    -  scalar src2 channel
 *                            01 - x
 *                            10 - y
 *                            11 - z
 *                            00 - w
 *               8..15   -  vector src2 swizzle
 *              16..23   -  vector src1 swizzle
 *                24     -  <UNKNOWN>
 *                25     -  vector src2 negate
 *                26     -  vector src1 negate
 *              27..31   -  <UNKNOWN>
 *
 *     dword 2:  0..4?   -  scalar src2 register
 *              5?..7    -  <UNKNOWN>
 *               8..12?  -  vector src2 register
 *             13?..15   -  <UNKNOWN>
 *              16..20?  -  vector src1 register
 *             21?.. 23  -  <UNKNOWN>
 *              24..28   -  vector operation
 *                29     -  <UNKNOWN>  (maybe, or maybe part of vector opc?)
 *                30     -  vector src2 type/bank
 *                            0 - Constant bank (C)  -  uniforms and consts
 *                            1 - Register bank (R)  -  varyings and locals
 *                31     -  vector src1 type/bank  (same as above)
 *
 *  still looking for:
 *    scalar opc
 *    scalar src1 reg
 *    scalar src1/src2 type/back
 *    scalar src1/src2 negate?
 *
 * Shader Outputs:
 *     vertex shader:
 *         R30: gl_Position
 *         R31: gl_PointSize
 *     fragment shader:
 *         R0:  gl_FragColor
 *         ??:  gl_FragData   --   TODO
 *
 */

#define REG_MASK 0x1f	/* not really sure how many regs yet */

static const char chan_names[] = { 'x', 'y', 'z', 'w' };

static void print_srcreg(uint32_t num, uint32_t type,
		uint32_t swiz, uint32_t negate)
{
	if (negate)
		printf("-");
	printf("%c%u", type ? 'C' : 'R', num);
	if (swiz) {
		int i;
		printf(".");
		for (i = 0; i < 4; i++) {
			printf("%c", chan_names[(swiz + i) & 0x3]);
			swiz >>= 2;
		}
	}
}

static void print_scalar_srcreg(uint32_t num, uint32_t type,
		uint32_t chan, uint32_t negate)
{
	if (negate)
		printf("-");
	printf("%c%u.%c", type ? 'C' : 'R', num, chan_names[chan]);
}

static void print_dstreg(uint32_t num, uint32_t mask, uint32_t dst_exp)
{
	printf("%s%u", dst_exp ? "export" : "R", num);
	if (mask != 0xf) {
		int i;
		printf(".");
		for (i = 0; i < 4; i++) {
			printf("%c", (mask & 0x1) ? chan_names[i] : '_');
			mask >>= 1;
		}
	}
}

static int disasm_alu(uint32_t *dwords, int level, enum shader_t type)
{
	static const char *op_names[0x20] = {
			[0]  = "ADDv",
			[1]  = "MULv",
			[2]  = "MAXv",
			[11] = "MULADDv",
			[15] = "DOT4v",
			[16] = "DOT3v",
	};
	uint32_t dst_reg   =   dwords[0] & REG_MASK;
	uint32_t dst_mask  =  (dwords[0] >> 16) & 0xf;
	uint32_t dst_exp   =  (dwords[0] & 0x00008000);
	uint32_t src1_swiz =  (dwords[1] >> 16) & 0xff;
	uint32_t src2_swiz =  (dwords[1] >> 8) & 0xff;
	uint32_t src1_neg  =  (dwords[1] & 0x04000000);
	uint32_t src2_neg  =  (dwords[1] & 0x02000000);
	uint32_t src1_reg  =  (dwords[2] >> 16) & REG_MASK;
	uint32_t src2_reg  =  (dwords[2] >> 8) & REG_MASK;
	uint32_t src1_type = !(dwords[2] & 0x80000000);
	uint32_t src2_type = !(dwords[2] & 0x40000000);
	uint32_t opc       =  (dwords[2] >> 24) & 0x1f;

	printf("%s", levels[level]);
	if (print_raw) {
		printf("%08x %08x %08x\t", dwords[0], dwords[1], dwords[2]);
	}
	if (print_unknown) {
		printf("%08x %08x %08x\t",
				dwords[0] & ~(REG_MASK | (0xf << 16) | (REG_MASK << 8) |
						(0xf << 20) | 0x00008000),
				dwords[1] & ~((0xff << 16) | (0xff << 8) | 0x04000000 |
						0x02000000 | 0x3 | (0x3 << 6)),
				dwords[2] & ~((REG_MASK << 16) | (REG_MASK << 8) |
						0x80000000 | 0x40000000 | (0x1f << 24) | REG_MASK));
	}

	if (op_names[opc]) {
		printf("\tALU:\t%s\t", op_names[opc]);
	} else {
		printf("\tALU:\tOP(%u)\t", opc);
	}

	print_dstreg(dst_reg, dst_mask, dst_exp);
	printf(" = ");
	print_srcreg(src1_reg, src1_type, src1_swiz, src1_neg);
	printf(", ");
	print_srcreg(src2_reg, src2_type, src2_swiz, src2_neg);

	if (dst_exp) {
		const char *name = NULL;
		switch (type) {
		case SHADER_VERTEX:
			switch (dst_reg) {
			case 30: name = "gl_Position";  break;
			case 31: name = "gl_PointSize"; break;
			}
			break;
		case SHADER_FRAGMENT:
			switch (dst_reg) {
			case 0:  name = "gl_FragColor"; break;
			}
			break;
		}
		/* if we had a symbol table here, we could look
		 * up the name of the varying..
		 */
		if (name) {
			printf("\t; %s", name);
		}
	}

	printf("\n");

	if (dwords[0] & 0x00f00000) {
		/* 2nd optional scalar op: */
		uint32_t sdst_reg   =  (dwords[0] >> 8) & REG_MASK;
		uint32_t sdst_mask  =  (dwords[0] >> 20) & 0xf;
		uint32_t ssrc1_neg  = 0; // XXX
		uint32_t ssrc1_reg  = 99; // XXX
		uint32_t ssrc1_type = 0; // XXX
		uint32_t ssrc1_chan =   dwords[1] & 0x3;
		uint32_t ssrc2_neg  = 0; // XXX
		uint32_t ssrc2_reg  =   dwords[2] & REG_MASK;
		uint32_t ssrc2_type = 0; // XXX
		uint32_t ssrc2_chan = ((dwords[1] >> 6) + 3) & 0x3;

		// looking for 1 reg,  (4 or 5 bits?)
		//             2 types (1 bit each)
		//             op code (?? bits)

		printf("%s", levels[level]);
		if (print_raw)
			printf("                          \t");
		if (print_unknown)
			printf("                          \t");

		// XXX not sure the opcode..
		printf("\t    \t????\t");

		print_dstreg(sdst_reg, sdst_mask, 0);  // can scalar op export??
		printf(" = ");
		print_scalar_srcreg(ssrc1_reg, ssrc1_type, ssrc1_chan, ssrc1_neg);
		printf(", ");
		print_scalar_srcreg(ssrc2_reg, ssrc2_type, ssrc2_chan, ssrc2_neg);
		printf("\n");
	}

	return 0;
}

static int disasm_fetch(uint32_t *dwords, int level)
{
	// XXX I guess there are other sorts of fetches too??
	// XXX write mask?  swizzle?
	static const char *fetch_type = "SAMPLE";
	uint32_t src_const = (dwords[0] >> 20) & 0xf;
	uint32_t src_reg = (dwords[0] >> 5) & REG_MASK;
	uint32_t dst_reg = (dwords[0] >> 12) & REG_MASK;

	printf("%s", levels[level]);
	if (print_raw) {
		printf("%08x %08x %08x\t", dwords[0], dwords[1], dwords[2]);
	}
	if (print_unknown) {
		printf("%08x %08x %08x\t",
				dwords[0] & ~((REG_MASK << 5) | (REG_MASK << 12) | (0xf << 20)),
				dwords[1] & ~0x00000000,
				dwords[2] & ~0x00000000);
	}

	printf("\tFETCH:\t%s\tR%u = R%u CONST(%u)\n", fetch_type, dst_reg,
			src_reg, src_const);
	return 0;
}

static int disasm_inst(uint32_t *dwords, int level, enum shader_t type)
{
	int ret = 0;

	/* I don't know if this is quite the right way to separate
	 * instruction types or not:
	 */
	if (dwords[2] & 0xf0000000) {
		ret = disasm_alu(dwords, level, type);
	} else {
		ret = disasm_fetch(dwords, level);
	}

	return ret;
}

static int disasm_cf(uint32_t *dwords, int level,
		uint32_t idx, uint32_t off, uint32_t cnt)
{
	printf("%s", levels[level]);
	if (print_raw) {
		printf("%08x %08x %08x\t", dwords[0], dwords[1], dwords[2]);
	}
	if (print_unknown) {
		printf("%08x %08x %08x\t",
				dwords[0] & ~0x0000ffff,
				dwords[1] & ~0x00000000,
				dwords[2] & ~0x00000000);
	}
	printf("%02d  CF:\tADDR(0x%x) CNT(0x%x)\n", idx, off, cnt);
	return 0;
}

int disasm(uint32_t *dwords, int sizedwords, int level, enum shader_t type)
{
	uint32_t first_off = (dwords[0] & 0xfff);
	uint32_t i = 0, j;
	uint32_t alu_off = first_off * 3;

	/* seems to be special case for last CF: */
	if (dwords[0] == 0) {
		uint32_t off = 1;
		uint32_t cnt = (sizedwords / 3) - off;
		alu_off = off * 3;
		disasm_cf(dwords, level, 0, off, cnt);
	}

	/* decode CF instructions: */
	for (i = 0; i < first_off; i++) {
		uint32_t idx = i * 3;
		uint32_t off = (dwords[idx] & 0x0fff);
		uint32_t cnt = (dwords[idx] & 0xf000) >> 12;

		/* seems to be special case for last CF: */
		if (dwords[idx] == 0) {
			printf("?");
			off = alu_off / 3;
			cnt = (sizedwords / 3) - off;
		}

		/* make sure we parsed the expected amount of data: */
		while (alu_off != (off * 3)) {
			printf("?");
			disasm_inst(dwords + alu_off, level, type);
			alu_off += 3;
		}

		disasm_cf(&dwords[idx], level, i, off, cnt);

		for (j = 0; j < cnt; j++) {
			disasm_inst(dwords + alu_off, level, type);
			alu_off += 3;
		}
	}

	/* make sure we parsed the expected amount of data: */
	while (alu_off != sizedwords) {
		printf("?");
		disasm_inst(dwords + alu_off, level, type);
		alu_off += 3;
	}

	return 0;
}
