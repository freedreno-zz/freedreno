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
#include "a2xx_reg.h"

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

static enum debug_t debug;

/*
 * ALU instruction format:
 * --- ----------- ------
 *
 *     dword0:   0..5?   -  vector dest register
 *              6?..7    -  <UNKNOWN>
 *               8..13?  -  scalar dest register
 *                14     -  <UNKNOWN>
 *                15     -  export flag
 *              16..19   -  vector dest write mask (wxyz)
 *              20..23   -  scalar dest write mask (wxyz)
 *              24..26   -  <UNKNOWN>
 *              27..31   -  scalar operation
 *
 *     dword 1:  0..7    -  src3 swizzle
 *               8..15   -  src2 swizzle
 *              16..23   -  src1 swizzle
 *                24     -  src3 negate
 *                25     -  src2 negate
 *                26     -  src1 negate
 *                27     -  predicate case (1 - execute if true, 0 - execute if false)
 *                28     -  predicate (conditional execution)
 *              29..31   -  <UNKNOWN>
 *
 *
 *     dword 2:  0..5?   -  src3 register
 *                6      -  <UNKNOWN>
 *                7      -  src3 abs (assumed)
 *               8..13?  -  src2 register
 *                14     -  <UNKNOWN>
 *                15     -  src2 abs
 *              16..21?  -  src1 register
 *                22     -  <UNKNOWN>
 *                23     -  src1 abs
 *              24..28   -  vector operation
 *                29     -  src3 type/bank
 *                            0 - Constant bank (C)  -  uniforms and consts
 *                            1 - Register bank (R)  -  varyings and locals
 *                30     -  vector src2 type/bank  (same as above)
 *                31     -  vector src1 type/bank  (same as above)
 *
 * Interpretation of ALU swizzle fields:
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
 *       chan[3]: 01 - x
 *                10 - y
 *                11 - z
 *                00 - w
 *
 *  still looking for:
 *    scalar src1 reg?  Is there one?
 *    scalar src1 type/bank
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

#define REG_MASK 0x3f	/* not really sure how many regs yet */
#define ADDR_MASK 0xfff

static const char chan_names[] = {
		'x', 'y', 'z', 'w',
		/* these only apply to FETCH dst's: */
		'0', '1', '?', '_',
};

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
		case 62: name = "gl_Position";  break;
		case 63: name = "gl_PointSize"; break;
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
	MUL2 = 0,
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
		INSTR(FLOORv, 1),
		INSTR(MULADDv, 3),
		INSTR(DOT4v, 2),
		INSTR(DOT3v, 2),
}, scalar_instructions[0x20] = {
		INSTR(MUL2, 1),
		INSTR(MOV, 1),  // todo doesn’t seem was can do a scalar max?? so I assume this is a single src MOV
		INSTR(EXP2, 1),
		INSTR(LOG2, 1),
		INSTR(RCP, 1),
		INSTR(RSQ, 1),
		INSTR(PSETE, 1),
		INSTR(SQRT, 1),
		INSTR(MUL, 2),
		INSTR(ADD, 2),
#undef INSTR
};

static int disasm_alu(uint32_t *dwords, int level, int sync, enum shader_t type)
{
	uint32_t dst_reg   =  dwords[0] & REG_MASK;
	uint32_t dst_mask  = (dwords[0] >> 16) & 0xf;
	uint32_t dst_exp   = (dwords[0] & 0x00008000);
	uint32_t sdst_reg  = (dwords[0] >> 8) & REG_MASK; /* scalar dst */
	uint32_t sdst_mask = (dwords[0] >> 20) & 0xf;
	uint32_t src1_swiz = (dwords[1] >> 16) & 0xff;
	uint32_t src2_swiz = (dwords[1] >> 8) & 0xff;
	uint32_t src3_swiz =  dwords[1] & 0xff;
	uint32_t src1_neg  = (dwords[1] >> 26) & 0x1;
	uint32_t src2_neg  = (dwords[1] >> 25) & 0x1;
	uint32_t src3_neg  = (dwords[1] >> 24) & 0x1;
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

	printf("%s", levels[level]);
	if (debug & PRINT_RAW) {
		printf("%08x %08x %08x\t", dwords[0], dwords[1], dwords[2]);
	}
	if (debug & PRINT_UNKNOWN) {
			printf("%08x %08x %08x\t",
					dwords[0] & ~(REG_MASK | (0xf << 16) | (REG_MASK << 8) |
							(0xf << 20) | 0x00008000 | (0x1f << 27)),
					dwords[1] & ~((0xff << 16) | (0xff << 8) | 0xff |
							(0x1 << 24) | (0x1 << 25) | (0x1 << 26) |
							(0x1 << 27) | (0x1 << 28)),
					dwords[2] & ~((REG_MASK << 16) | (REG_MASK << 8) |
							(0x1 << 31) | (0x1 << 30) | (0x1 << 29) |
							(0x1f << 24) | REG_MASK));
	}

	printf("   %sALU:\t", sync ? "(S)" : "   ");

	if (vector_instructions[vector_op].name) {
		printf(vector_instructions[vector_op].name);
	} else {
		printf("OP(%u)", vector_op);
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
		print_srcreg(src3_reg, src3_type, src3_swiz, src3_neg);
		printf(", ");
	}
	print_srcreg(src1_reg, src1_type, src1_swiz, src1_neg);
	if (vector_instructions[vector_op].num_srcs > 1) {
		printf(", ");
		print_srcreg(src2_reg, src2_type, src2_swiz, src2_neg);
	}

	if (dst_exp)
		print_export_comment(dst_reg, type);

	printf("\n");

	if (sdst_mask || !dst_mask) {
		/* 2nd optional scalar op: */
		uint32_t scalar_op =  (dwords[0] >> 27) & 0x1f;

		printf("%s", levels[level]);
		if (debug & PRINT_RAW)
			printf("                          \t");
		if (debug & PRINT_UNKNOWN)
			printf("                          \t");

		if (scalar_instructions[scalar_op].name) {
			printf("\t    \t%s\t", scalar_instructions[scalar_op].name);
		} else {
			printf("\t    \tOP(%u)\t", scalar_op);
		}

		print_dstreg(sdst_reg, sdst_mask, dst_exp);
		printf(" = ");
		print_srcreg(src3_reg, src3_type, src3_swiz, src3_neg);
		// TODO ADD/MUL must have another src?!?
		if (dst_exp)
			print_export_comment(sdst_reg, type);
		printf("\n");
	}

	return 0;
}

/*
 * VTX FETCH instruction format:
 * --- ----- ----------- ------
 *
 * Note: w/ sequence of VERTEX fetches, re-ordered into order of attributes
 * in memory, I get:
 *
 *    +------ bits 25..26
 *   /     +- dword[0]
 *  /     /
 * / /----+---\
 * 0 11 4 82000 40393a88 0000000c    FETCH:	VERTEX	R2.xyz1 = R0.xyx FMT_32_32_32_FLOAT SIGNED STRIDE(12) CONST(4)
 * 1 1b 4 81000 00263688 00000010    FETCH:	VERTEX	R1.xyzw = R0.zyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(4)
 * 2 15 4 83000 40263688 00000010    FETCH:	VERTEX	R3.xyzw = R0.yyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(4)
 * 0 11 5 84000 40263688 00000010    FETCH:	VERTEX	R4.xyzw = R0.xyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(5)
 * 1 13 5 86000 40263688 00000010    FETCH:	VERTEX	R6.xyzw = R0.xyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(5)
 * 2 15 5 85000 40263688 00000010    FETCH:	VERTEX	R5.xyzw = R0.yyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(5)
 * 0 11 6 88000 40263688 00000010    FETCH:	VERTEX	R8.xyzw = R0.xyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(6)
 * 1 13 6 87000 40263688 00000010    FETCH:	VERTEX	R7.xyzw = R0.xyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(6)
 * 2 15 6 8b000 40263688 00000010    FETCH:	VERTEX	R11.xyzw = R0.yyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(6)
 * 0 11 7 89000 40263688 00000010    FETCH:	VERTEX	R9.xyzw = R0.xyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(7)
 * 1 13 7 8c000 40263688 00000010    FETCH:	VERTEX	R12.xyzw = R0.xyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(7)
 * 2 15 7 8a000 40263688 00000010    FETCH:	VERTEX	R10.xyzw = R0.yyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(7)
 * 0 11 8 8e000 40263688 00000010    FETCH:	VERTEX	R14.xyzw = R0.xyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(8)
 * 1 13 8 8d000 40263688 00000010    FETCH:	VERTEX	R13.xyzw = R0.xyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(8)
 * 2 15 8 8f000 40263688 00000010    FETCH:	VERTEX	R15.xyzw = R0.yyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(8)
 *
 * So maybe 25..26 is src reg swizzle, and somewhere in 20..23 is some sort of
 * offset?
 *
 * Note that GL_MAX_VERTEX_ATTRIBS is 16 (although possibly should be 15,
 * as I get an error binding attribute >14!)
 *
 *     dword0:   0..4?   -  fetch operation - 0x00
 *               5..10?  -  src register
 *                11     -  <UNKNOWN>
 *              12..17?  -  dest register
 *             18?..19   -  <UNKNOWN>
 *              20..23?  -  const
 *              24..25   -  <UNKNOWN>  (maybe part of const?)
 *              25..26   -  src swizzle (x)
 *                            00 - x
 *                            01 - y
 *                            10 - z
 *                            11 - w
 *              27..31   -  unknown
 *
 *     dword1:   0..11   -  dest swizzle/mask, 3 bits per channel (w/z/y/x),
 *                          low two bits of each determine position src channel,
 *                          high bit set 1 to mask
 *                12     -  signedness ('1' signed, '0' unsigned)
 *              13..15   -  <UNKNOWN>
 *              16..21?  -  type - see 'enum SURFACEFORMAT'
 *             22?..31   -  <UNKNOWN>
 *
 *     dword2:   0..15   -  stride (more than 0xff and data is copied/packed)
 *              16..31   -  <UNKNOWN>
 *
 * note: at least VERTEX fetch instructions get patched up at runtime
 * based on the size of attributes attached.. for example, if vec4, but
 * size given in glVertexAttribPointer() then the write mask and size
 * (stride?) is updated
 *
 * TEX FETCH instruction format:
 * --- ----- ----------- ------
 *
 *     dword0:   0..4?   -  fetch operation - 0x01
 *               5..10?  -  src register
 *                11     -  <UNKNOWN>
 *              12..17?  -  dest register
 *             18?..19   -  <UNKNOWN>
 *              20..23?  -  const
 *              24..25   -  <UNKNOWN>  (maybe part of const?)
 *              26..31   -  src swizzle (z/y/x)
 *                            00 - x
 *                            01 - y
 *                            10 - z
 *                            11 - w
 *
 *     dword1:   0..11   -  dest swizzle/mask, 3 bits per channel (w/z/y/x),
 *                          low two bits of each determine position src channel,
 *                          high bit set 1 to mask
 *                12     -  signedness ('1' signed, '0' unsigned)
 *              13..31   -  <UNKNOWN>
 *
 *     dword2:   0..31   -  <UNKNOWN>
 */

enum fetch_opc {
	VERTEX = 0x00,
	SAMPLE = 0x01,
};

struct {
	const char *name;
} fetch_instructions[0x1f] = {
#define INSTR(opc) [opc] = { #opc }
		INSTR(VERTEX),
		INSTR(SAMPLE),
#undef INSTR
};

struct {
	const char *name;
} fetch_types[0xff] = {
#define TYPE(id) [id] = { #id }
		TYPE(FMT_1_REVERSE),
		TYPE(FMT_32_FLOAT),
		TYPE(FMT_32_32_FLOAT),
		TYPE(FMT_32_32_32_FLOAT),
		TYPE(FMT_32_32_32_32_FLOAT),
		TYPE(FMT_16),
		TYPE(FMT_16_16),
		TYPE(FMT_16_16_16_16),
		TYPE(FMT_8),
		TYPE(FMT_8_8),
		TYPE(FMT_8_8_8_8),
		TYPE(FMT_32),
		TYPE(FMT_32_32),
		TYPE(FMT_32_32_32_32),
#undef TYPE
};

static int disasm_fetch(uint32_t *dwords, int level, int sync)
{
	uint32_t src_const = (dwords[0] >> 20) & 0xf;
	uint32_t src_reg   = (dwords[0] >> 5) & REG_MASK;
	uint32_t dst_reg   = (dwords[0] >> 12) & REG_MASK;
	uint32_t fetch_opc =  dwords[0] & 0x1f;
	uint32_t dst_swiz  =  dwords[1] & 0xfff;
	int i;

	printf("%s", levels[level]);
	if (debug & PRINT_RAW) {
		printf("%08x %08x %08x\t", dwords[0], dwords[1], dwords[2]);
	}
	if (debug & PRINT_UNKNOWN) {
		if (fetch_opc == VERTEX) {
			printf("%08x %08x %08x\t",
					dwords[0] & ~((REG_MASK << 5) | (REG_MASK << 12) |
							(0xf << 20) | 0x1f | (0x3 << 25)),
					dwords[1] & ~(0xfff | (0x1 << 12) | (0x3f << 16)),
					dwords[2] & ~(0xff));

		} else {
			printf("%08x %08x %08x\t",
					dwords[0] & ~((REG_MASK << 5) | (REG_MASK << 12) |
							(0xf << 20) | 0x1f | (0x3f << 26)),
					dwords[1] & ~(0xfff),
					dwords[2] & ~(0));
		}
	}

	printf("   %sFETCH:\t", sync ? "(S)" : "   ");
	if (fetch_instructions[fetch_opc].name) {
		printf(fetch_instructions[fetch_opc].name);
	} else {
		printf("OP(%u)", fetch_opc);
	}

	printf("\tR%u.", dst_reg);
	for (i = 0; i < 4; i++) {
		printf("%c", chan_names[dst_swiz & 0x7]);
		dst_swiz >>= 3;
	}

	if (fetch_opc == VERTEX) {
		uint32_t src_swiz  = (dwords[0] >> 25) & 0x3;
		uint32_t sign      = (dwords[1] >> 12) & 0x1;
		uint32_t type      = (dwords[1] >> 16) & 0x3f;
		uint32_t stride    =  dwords[2] & 0xff;
		printf(" = R%u.", src_reg);
		printf("%c", chan_names[src_swiz & 0x3]);
		if (fetch_types[type].name) {
			printf(" %s", fetch_types[type].name);
		} else  {
			printf(" TYPE(0x%x)", type);
		}
		printf(" %s", sign ? "SIGNED" : "UNSIGNED");
		printf(" STRIDE(%d)", stride);
	} else {
		uint32_t src_swiz  = (dwords[0] >> 26) & 0x3f;
		printf(" = R%u.", src_reg);
		for (i = 0; i < 3; i++) {
			printf("%c", chan_names[src_swiz & 0x3]);
			src_swiz >>= 2;
		}
	}

	printf(" CONST(%u)\n", src_const);

	return 0;
}

/*
 * CF instruction format:
 * -- ----------- ------
 *
 *     dword0:   0..11   -  addr/size 1
 *              12..15   -  count 1
 *              16..31   -  sequence 1.. 2 bits per instruction in the EXEC
 *                          clause, the low bit seems to control FETCH vs
 *                          ALU instruction type, the high bit seems to be
 *                          (S) modifier on instruction (which might make
 *                          the name SERIALIZE() in optimize-for-adreno.pdf
 *                          make sense.. although I don't quite understand
 *                          the meaning yet)
 *
 *     dword1:   0..7    -  <UNKNOWN>
 *               8..15?  -  op 1
 *              16..27   -  addr/size 2
 *              28..31   -  count 2
 *
 *     dword2:   0..15   -  sequence 2
 *              16..23   -  <UNKNOWN>
 *              24..31   -  op 2
 */

struct {
	uint32_t exec;
	const char *fmt;
} cf_instructions[0xff] = {
#define INSTR(opc, fmt, exec) [opc] = { exec, fmt }
		INSTR(0x00, "NOP", 0),
		INSTR(0x10, "EXEC ADDR(0x%x) CNT(0x%x)", 1),
		INSTR(0x20, "EXEC_END ADDR(0x%x) CNT(0x%x)", 1),
		INSTR(0xc2, "ALLOC COORD SIZE(0x%x)", 0),
		INSTR(0xc4, "ALLOC PARAM/PIXEL SIZE(0x%x)", 0),
#undef INSTR
};

struct cf {
	uint32_t  addr;
	uint32_t  cnt;
	uint32_t  op;
	uint32_t *dwords;

	/* I think the same as SERIALIZE() in optimize-for-adreno.pdf
	 * screenshot.. but that name doesn't really make sense to me, as
	 * it appears to differentiate fetch vs alu instructions:
	 */
	uint32_t  sequence;
};

static void print_cf(struct cf *cf, int level)
{
	printf("%s", levels[level]);
	if (debug & PRINT_RAW) {
		if (cf->dwords) {
			printf("%08x %08x %08x\t",
					cf->dwords[0], cf->dwords[1], cf->dwords[2]);
		} else {
			printf("                          \t");
		}
	}
	if (debug & PRINT_UNKNOWN) {
		if (cf->dwords) {
		printf("%08x %08x %08x\t",
				cf->dwords[0] & ~(ADDR_MASK | (0xf << 12) | (0xffff << 16)),
				cf->dwords[1] & ~((ADDR_MASK << 16) |
						(0xf << 28) | (0xff << 8)),
				cf->dwords[2] & ~((0xff << 24) | 0xffff));
		} else {
			printf("                          \t");
		}
	}
	if (cf_instructions[cf->op].fmt) {
		printf(cf_instructions[cf->op].fmt, cf->addr, cf->cnt);
	} else {
		printf("CF(0x%x) ADDR(0x%x) CNT(0x%x)", cf->op, cf->addr, cf->cnt);
	}
	printf("\n");
}

static int parse_cf(uint32_t *dwords, int sizedwords, struct cf *cfs)
{
	int idx = 0;
	int off = 0;

	do {
		struct cf *cf;
		uint32_t addr  =  dwords[0] & ADDR_MASK;
		uint32_t cnt   = (dwords[0] >> 12) & 0xf;
		uint32_t seqn1 = (dwords[0] >> 16) & 0xffff;
		uint32_t op    = (dwords[1] >> 8) & 0xff;
		uint32_t addr2 = (dwords[1] >> 16) & ADDR_MASK;
		uint32_t cnt2  = (dwords[1] >> 28) & 0xf;
		uint32_t op2   = (dwords[2] >> 24) & 0xff;
		uint32_t seqn2 =  dwords[2] & 0xffff;

		if (!off)
			off = addr ? addr : addr2;

		cf = &cfs[idx++];
		cf->dwords = dwords;
		cf->addr = addr;
		cf->cnt  = cnt;
		cf->op   = op;
		cf->sequence = seqn1;

		cf = &cfs[idx++];
		cf->dwords = NULL;
		cf->addr = addr2;
		cf->cnt  = cnt2;
		cf->op   = op2;
		cf->sequence = seqn2;

		dwords += 3;

	} while (--off > 0);

	return idx;
}

/*
 * The adreno shader microcode consists of two parts:
 *   1) A CF (control-flow) program, at the header of the compiled shader,
 *      which refers to ALU/FETCH instructions that follow it by address.
 *   2) ALU and FETCH instructions
 */

int disasm(uint32_t *dwords, int sizedwords, int level, enum shader_t type)
{
	static struct cf cfs[64];
	int off, idx, max_idx;

	idx = 0;
	max_idx = parse_cf(dwords, sizedwords, cfs);

	while (idx < max_idx) {
		struct cf *cf = &cfs[idx++];
		uint32_t sequence = cf->sequence;
		uint32_t i;

		print_cf(cf, level);

		if (cf_instructions[cf->op].exec) {
			for (i = 0; i < cf->cnt; i++) {
				uint32_t alu_off = (cf->addr + i) * 3;
				if (sequence & 0x1) {
					disasm_fetch(dwords + alu_off, level, sequence & 0x2);
				} else {
					disasm_alu(dwords + alu_off, level, sequence & 0x2, type);
				}
				sequence >>= 2;
			}
		}
	}

	return 0;
}

void disasm_set_debug(enum debug_t d)
{
	debug= d;
}
