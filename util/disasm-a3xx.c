/*
 * Copyright (c) 2013 Rob Clark <robdclark@gmail.com>
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
#include <string.h>
#include <assert.h>

#include "disasm.h"
#include "instr-a3xx.h"

typedef enum {
	true = 1, false = 0,
} bool;

extern enum debug_t debug;

static const char *levels[] = {
		"",
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

static void print_reg(reg_t *reg, uint8_t flags, bool immed)
{
	static const char *component = "xyzw";
	const char type = (flags & 0x10) ? 'c' : 'r';

	if (flags & 0x40)  /* neg */
		printf("-");
	if (flags & 0x80)  /* abs */
		printf("|");

	if (flags & 0x20)  /* repeat.. if rpt is set, keep same value for repeated instrs */
		printf("(r)");

	if (immed) {
		printf("%d", reg->const_val);
	} else {
		printf("%c%d.%c", type, reg->num, component[reg->comp]);
	}

	if (flags & 0x80)  /* abs */
		printf("|");
}

struct {
	const char *name;
} opcs[0x1ff] = {
#define OPC(cat, opc, name) [((cat) << 6) | (opc)] = { #name }
	/* category 0: */
	OPC(0, OPC_NOP, nop),
	OPC(0, OPC_BR, br),
	OPC(0, OPC_BR_2, br),
	OPC(0, OPC_JUMP, jump),
	OPC(0, OPC_CALL, call),
	OPC(0, OPC_RET, ret),
	OPC(0, OPC_END, end),

	/* category 1: */
	OPC(1, OPC_MOV_F32F32_2, mov.f32f32), // XXX ???
	OPC(1, OPC_MOV_F32F32, mov.f32f32),
	OPC(1, OPC_MOV_S32S32, mov.s32s32),

	/* category 2: */
	OPC(2, OPC_ADD_F, add.f),
	OPC(2, OPC_MIN_F, min.f),
	OPC(2, OPC_MAX_F, max.f),
	OPC(2, OPC_MUL_F, mul.f),
	OPC(2, OPC_CMPS_F, cmps.f),
	OPC(2, OPC_ABSNEG_F, absneg.f),
	OPC(2, OPC_ADD_S, add.s),
	OPC(2, OPC_SUB_S, sub.s),
	OPC(2, OPC_CMPS_U, cmps.u),
	OPC(2, OPC_CMPS_S, cmps.s),
	OPC(2, OPC_MIN_S, min.s),
	OPC(2, OPC_MAX_S, max.s),
	OPC(2, OPC_ABSNEG_S, absneg.s),
	OPC(2, OPC_AND_B, and.b),
	OPC(2, OPC_OR_B, or.b),
	OPC(2, OPC_XOR_B, xor.b),
	OPC(2, OPC_MUL_S, mul.s),
	OPC(2, OPC_MULL_U, mull.u),
	OPC(2, OPC_CLZ_B, clz.b),
	OPC(2, OPC_SHL_B, shl.b),
	OPC(2, OPC_SHR_B, shr.b),
	OPC(2, OPC_ASHR_B, ashr.b),

	/* category 3: */
	OPC(3, OPC_MADSH_M16, madsh.m16),
	OPC(3, OPC_SEL_B16, sel.b16),
	OPC(3, OPC_SEL_B32, sel.b32),
	OPC(3, OPC_SEL_F32, sel.f32),

	/* category 4: */
	OPC(4, OPC_RCP, rcp),
	OPC(4, OPC_RSQ, rsq),
	OPC(4, OPC_LOG2, log2),
	OPC(4, OPC_EXP2, exp2),
	OPC(4, OPC_SIN, sin),
	OPC(4, OPC_COS, cos),
	OPC(4, OPC_SQRT, sqrt),

	/* category 6: */
	OPC(6, OPC_LDG, ldg),
	OPC(6, OPC_LDP, ldp),
	OPC(6, OPC_STG, stg),
	OPC(6, OPC_STP, stp),

#undef OPC
};
#define GETOPC(instr) (&(opcs[((instr)->opc_cat << 6) | (instr)->opc]))


static void print_instr(uint32_t *dwords, int level, int n)
{
	instr_t *instr = (instr_t *)dwords;
	const char *name;

	printf("%s%04d[%08xx_%08xx] ", levels[level], n, dwords[1], dwords[0]);

	/* print unknown bits: */
	if (debug & PRINT_RAW)
		printf("[%08xx_%08xx] ", dwords[1] & 0x001ff800, dwords[0] & 0x00000000);

	if (debug & PRINT_VERBOSE)
		printf("%d,%02d ", instr->opc_cat, instr->opc);

	if (instr->sync)
		printf("(sy)");
	// XXX this at least doesn't apply to category 6, maybe only applies to category 0:
	if ((instr->opc_cat != 6) && instr->ss)
		printf("(ss)");
	if (instr->jmp_tgt)
		printf("(jp)");
	// XXX this at least doesn't apply to category 6:
	if ((instr->opc_cat != 6) && instr->repeat)
		printf("(rpt%d)", instr->repeat);

	name = GETOPC(instr)->name;

	if (name)
		printf("%s", name);
	else
		printf("unknown(%d,%d)", instr->opc_cat, instr->opc);

	///////////////////////////////////////////
	// XXX some ugly special case stuff.. XXX
	if (instr->opc_cat == 6) {
		uint32_t type = (dwords[1] >> 16) & 0xf;
		if (type == 0x0)
			printf(".f16");
		else if (type == 0x2)
			printf(".f32");
		else if (type == 0x4)
			printf(".u16");
		else if (type == 0x6)
			printf(".u32");
		else if (type == 0xe)
			printf(".s8");
		else
			printf(".??");
	} else if (instr->opc_cat == 2) {
		uint32_t type = (dwords[1] >> 16) & 0x6;
		switch (instr->opc) {
		case OPC_CMPS_F:
		case OPC_CMPS_U:
		case OPC_CMPS_S:
			if (type == 0x0)
				printf(".lt");
			else if (type == 0x2)
				printf(".gt");
			else if (type == 0x4)
				printf(".eq");
			else
				printf(".??");
			break;
		}
	}
	///////////////////////////////////////////

	if (instr->opc_cat >= 1) {
		printf(" ");
		print_reg(&instr->dst, 0, false);
		printf(", ");
		print_reg(&instr->src1, instr->src1_flags, false /* ?? */);
	}
	if ((instr->opc_cat >= 2) && (instr->opc_cat < 4)) {
		printf(", ");
		print_reg(&instr->src2, instr->src2_flags, false);
	}
	if ((instr->opc_cat >= 3) && (instr->opc_cat < 4)) {
		// ???
	}

	printf("\n");
}

int disasm_a3xx(uint32_t *dwords, int sizedwords, int level, enum shader_t type)
{
	int i;

	assert((sizedwords % 2) == 0);

	for (i = 0; i < sizedwords; i += 2)
		print_instr(&dwords[i], level, i/2);

	return 0;
}
