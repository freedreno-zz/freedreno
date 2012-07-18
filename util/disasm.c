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
 *              24..26   -  <UNKNOWN>
 *              27..31   -  scalar operation
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
 *                27     -  predicate case (1 - execute if true, 0 - execute if false)
 *                28     -  predicate (conditional execution)
 *              29..31   -  <UNKNOWN>
 *
 *
 *     dword 2:  0..4?   -  src3 register
 *              5?..6    -  <UNKNOWN>
 *                7      -  src3 abs (assumed)
 *               8..12?  -  src2 register
 *             13?..14   -  <UNKNOWN>
 *                15     -  src2 abs
 *              16..20?  -  src1 register
 *             21?.. 22  -  <UNKNOWN>
 *                23     -  src1 abs
 *              24..28   -  vector operation
 *                29     -  src3 type/bank
 *                            0 - Constant bank (C)  -  uniforms and consts
 *                            1 - Register bank (R)  -  varyings and locals
 *                30     -  vector src2 type/bank  (same as above)
 *                31     -  vector src1 type/bank  (same as above)
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
 *  still looking for:
 *    scalar opc
 *    scalar src1 reg?  Is there one?
 *    scalar src1 type/back
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
#define ADDR_MASK 0xfff

static const char chan_names[] = { 'x', 'y', 'z', 'w' };

static void print_srcreg(uint32_t num, uint32_t type,
		uint32_t swiz, uint32_t negate)
{
	if (negate)
		printf("-");
	printf("%c%u", type ? 'R' : 'C', num);
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
	printf("%c%u.%c", type ? 'R' : 'C', num, chan_names[chan]);
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

static void print_export_comment(uint32_t num, enum shader_t type)
{
	const char *name = NULL;
	switch (type) {
	case SHADER_VERTEX:
		switch (num) {
		case 30: name = "gl_Position";  break;
		case 31: name = "gl_PointSize"; break;
		}
		break;
	case SHADER_FRAGMENT:
		switch (num) {
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

enum vector_opc {
	ADDv = 0,
	MULv = 1,
	MAXv = 2,
	MINv = 3,
	FLOORv = 10,
	MULADDv = 11,
	DOT4v = 15,
	DOT3v = 16,
};

enum scalar_opc {
	MOV = 2,
	EXP2 = 7,
	LOG2 = 8,
	RCP = 9,
	RSQ = 11,
	PSETE = 13,    /* called PRED_SETE in r600isa.pdf */
	SQRT = 20,
	MUL = 21,
	ADD = 22,
};

struct {
	uint32_t num_srcs;
	const char *name;
} vector_instructions[0x20] = {
#define INSTR(name, num_srcs) [name] = { num_srcs, #name }
		INSTR(ADDv, 2),
		INSTR(MULv, 2),
		INSTR(MAXv, 2),
		INSTR(MINv, 2),
		INSTR(FLOORv, 2),
		INSTR(MULADDv, 3),
		INSTR(DOT4v, 2),
		INSTR(DOT3v, 2),
}, scalar_instructions[0x20] = {
		INSTR(MOV, 1),  // todo doesn’t seem was can do a scalar max?? so I assume this is a single src MOV
		INSTR(EXP2, 1),
		INSTR(LOG2, 1),
		INSTR(RCP, 1),
		INSTR(RSQ, 1),
		INSTR(PSETE, 1),
		INSTR(SQRT, 1),
		INSTR(MUL, 2),
		INSTR(ADD, 2),
};

static int disasm_alu(uint32_t *dwords, int level, enum shader_t type)
{
	uint32_t dst_reg   =  dwords[0] & REG_MASK;
	uint32_t dst_mask  = (dwords[0] >> 16) & 0xf;
	uint32_t dst_exp   = (dwords[0] & 0x00008000);
	uint32_t sdst_reg  = (dwords[0] >> 8) & REG_MASK; /* scalar dst */
	uint32_t sdst_mask = (dwords[0] >> 20) & 0xf;
	uint32_t src1_swiz = (dwords[1] >> 16) & 0xff;
	uint32_t src2_swiz = (dwords[1] >> 8) & 0xff;
	uint32_t src1_neg  = (dwords[1] & 0x04000000);
	uint32_t src2_neg  = (dwords[1] & 0x02000000);
	uint32_t src1_reg  = (dwords[2] >> 16) & REG_MASK;
	uint32_t src2_reg  = (dwords[2] >> 8) & REG_MASK;
	uint32_t src3_reg  =  dwords[2] & REG_MASK;
	uint32_t src1_type = (dwords[2] >> 31) & 0x1;
	uint32_t src2_type = (dwords[2] >> 30) & 0x1;
	uint32_t src3_type = (dwords[2] >> 29) & 0x1;
	uint32_t vector_op = (dwords[2] >> 24) & 0x1f;
	uint32_t vector_pred = (dwords[1] >> 28) & 0x1;
	uint32_t vector_case = (dwords[1] >> 27) & 0x1;
	// TODO add abs
	// TODO add pred

	printf("%s", levels[level]);
	if (print_raw) {
		printf("%08x %08x %08x\t", dwords[0], dwords[1], dwords[2]);
	}
	if (print_unknown) {
		/* hmm, possibly some of these bits have other meanings if
		 * no scalar op.. like MULADDv?
		 */
		if (dwords[0] & 0x00f00000) {
			printf("%08x %08x %08x\t",
					dwords[0] & ~(REG_MASK | (0xf << 16) | (REG_MASK << 8) |
							(0xf << 20) | 0x00008000 | (0x1f << 27)),
					dwords[1] & ~((0xff << 16) | (0xff << 8) | 0x04000000 |
							0x02000000 | 0x3 | (0x3 << 6) | (0x1 << 27) | (0x1 << 28)),
					dwords[2] & ~((REG_MASK << 16) | (REG_MASK << 8) |
							(0x1 << 31) | (0x1 << 30) | (0x1 << 29) |
							(0x1f << 24) | REG_MASK));
		} else {
			printf("%08x %08x %08x\t",
					dwords[0] & ~(REG_MASK | (0xf << 16) | 0x00008000),
					dwords[1] & ~((0xff << 16) | (0xff << 8) | 0x04000000 |
							0x02000000 | (0x1 << 27) | (0x1 << 28)),
					dwords[2] & ~((REG_MASK << 16) | (REG_MASK << 8) |
							(0x1 << 31) | (0x1 << 30) | (0x1 << 29) |
							(0x1f << 24) | REG_MASK));
		}
	}

	if (vector_instructions[vector_op].name) {
		printf("\tALU:\t%s", vector_instructions[vector_op].name);
	} else {
		printf("\tALU:\tOP(%u)", vector_op);
	}

	if (vector_pred) {
		/* seems to work similar to conditional execution in ARM instruction
		 * set, so let's use a similar syntax for now:
		 */
		printf(vector_case ? "EQ" : "NE");
	}

	printf("\t");

	print_dstreg(dst_reg, dst_mask, dst_exp);
	printf(" = ");
	if (vector_instructions[vector_op].num_srcs == 3) {
		print_srcreg(src3_reg, src3_type, 0, 0);
		printf(", ");
	}
	print_srcreg(src1_reg, src1_type, src1_swiz, src1_neg);
	printf(", ");
	print_srcreg(src2_reg, src2_type, src2_swiz, src2_neg);

	if (dst_exp)
		print_export_comment(dst_reg, type);

	printf("\n");

	if (sdst_mask || !dst_mask) {
		/* 2nd optional scalar op: */
		uint32_t scalar_op =  (dwords[0] >> 27) & 0x1f;
		uint32_t src4_neg  = 0; // XXX
		uint32_t src4_reg  = 99; // XXX
		uint32_t src4_type = 1; // XXX
		uint32_t src4_chan =   dwords[1] & 0x3;

		/* possibly these are used for 3 src ops, like MULADDv?? */
		uint32_t src3_neg  = 0; // XXX
		uint32_t src3_chan = ((dwords[1] >> 6) + 3) & 0x3;

		printf("%s", levels[level]);
		if (print_raw)
			printf("                          \t");
		if (print_unknown)
			printf("                          \t");

		if (scalar_instructions[scalar_op].name) {
			printf("\t    \t%s\t", scalar_instructions[scalar_op].name);
		} else {
			printf("\t    \tOP(%u)\t", scalar_op);
		}

		print_dstreg(sdst_reg, sdst_mask, dst_exp);
		printf(" = ");
		print_scalar_srcreg(src3_reg, src3_type, src3_chan, src3_neg);
		if (scalar_instructions[scalar_op].num_srcs != 1) {
			printf(", ");
			print_scalar_srcreg(src4_reg, src4_type, src4_chan, src4_neg);
		}
		if (dst_exp)
			print_export_comment(sdst_reg, type);
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
	uint32_t addr2 = (dwords[1] >> 16) & ADDR_MASK;
	printf("%s", levels[level]);
	if (print_raw) {
		printf("%08x %08x %08x\t", dwords[0], dwords[1], dwords[2]);
	}
	if (print_unknown) {
		printf("%08x %08x %08x\t",
				dwords[0] & ~0x0000ffff,
				dwords[1] & ~((ADDR_MASK << 16)),
				dwords[2] & ~0x00000000);
	}
	printf("%02d  CF:\tADDR(0x%x) CNT(0x%x) ADDR2(0x%x)\n",
			idx, off, cnt, addr2);
	return 0;
}

int disasm(uint32_t *dwords, int sizedwords, int level, enum shader_t type)
{
	uint32_t first_off = (dwords[0] & ADDR_MASK);
	uint32_t i, j;
	uint32_t alu_off = first_off * 3;

	if (print_raw) {
		for (i = 0; i < first_off; i++) {
			uint32_t idx = i * 3;
			uint32_t off = (dwords[idx] & ADDR_MASK);
			uint32_t cnt = (dwords[idx] & 0xf000) >> 12;
			disasm_cf(&dwords[idx], level, i, off, cnt);
		}
		printf("\n");
	}

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
		uint32_t off = (dwords[idx] & ADDR_MASK);
		uint32_t cnt = (dwords[idx] & 0xf000) >> 12;

		/* seems to be special case for last CF: */
		if (dwords[idx] == 0) {
			printf("?");
			off = alu_off / 3;
			cnt = (sizedwords / 3) - off;
		}

		/* make sure we parsed the expected amount of data: */
		while (alu_off < (off * 3)) {
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
	while (alu_off < sizedwords) {
		printf("?");
		disasm_inst(dwords + alu_off, level, type);
		alu_off += 3;
	}

	return 0;
}
