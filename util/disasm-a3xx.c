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

static const char *component = "xyzw";

static void print_reg(reg_t reg, bool full, bool r, bool c, bool im, bool neg, bool abs)
{
	const char type = c ? 'c' : 'r';

	// XXX I prefer - and || for neg/abs, but preserving format used
	// by libllvm-a3xx for easy diffing..

	if (neg)
		printf("(neg)");

	if (r)
		printf("(r)");

	if (abs)
		printf("(abs)");

	if (im) {
		printf("%d", reg.const_val);
	} else if ((reg.num == 62) && !c) {
		printf("p0.%c", component[reg.comp]);
	} else {
		printf("%s%c%d.%c", full ? "" : "h", type, reg.num, component[reg.comp]);
	}
}

static void print_instr_cat0(instr_t *instr)
{
	instr_cat0_t *cat0 = &instr->cat0;

	switch (cat0->opc) {
	case OPC_BR:
		printf(" %sp0.%c, #%d", cat0->inv ? "!" : "",
				component[cat0->comp], cat0->immed);
		break;
	case OPC_JUMP:
		printf(" #%d", cat0->immed);
		break;
	}

	if ((debug & PRINT_VERBOSE) && (cat0->dummy1|cat0->dummy2|cat0->dummy3))
		printf("\t{0: %x,%x,%x}", cat0->dummy1, cat0->dummy2, cat0->dummy3);
}

static void print_instr_cat1(instr_t *instr)
{
	instr_cat1_t *cat1 = &instr->cat1;

	printf(" ");
	print_reg((reg_t)(cat1->dst), true, false, false, false, false, false);
	printf(", ");
	print_reg((reg_t)(cat1->src1), true, false, false, false, false, false);

	if ((debug & PRINT_VERBOSE) && (cat1->dummy1|cat1->dummy2|cat1->dummy3))
		printf("\t{1: %x,%x,%x}", cat1->dummy1, cat1->dummy2, cat1->dummy3);
}

static void print_instr_cat2(instr_t *instr)
{
	instr_cat2_t *cat2 = &instr->cat2;
	static const char *cond[] = {
			"lt",
			"le",
			"gt",
			"ge",
			"eq",
			"ne",
			"?6?",
	};

	switch (cat2->opc) {
	case OPC_CMPS_F:
	case OPC_CMPS_U:
	case OPC_CMPS_S:
		printf(".%s", cond[cat2->cond]);
		break;
	}

	printf(" ");
	print_reg((reg_t)(cat2->dst), cat2->full ^ cat2->dst_half, false, false, false, false, false);
	printf(", ");
	print_reg((reg_t)(cat2->src1), cat2->full, cat2->src1_r,
			cat2->src1_c, false, false, cat2->src1_abs);
	switch (cat2->opc) {
	case OPC_ABSNEG_F:
	case OPC_ABSNEG_S:
	case OPC_CLZ_B:
		/* these only have one src reg */
		break;
	default:
		printf(", ");
		print_reg((reg_t)(cat2->src2), cat2->full, cat2->src2_r,
				cat2->src2_c, cat2->src2_im, cat2->src2_neg, false);
		break;
	}
	if ((debug & PRINT_VERBOSE) && (cat2->dummy1|cat2->dummy2|cat2->dummy3|cat2->dummy4|cat2->dummy5|cat2->dummy6))
		printf("\t{2: %x,%x,%x,%x,%x,%x}", cat2->dummy1, cat2->dummy2, cat2->dummy3, cat2->dummy4, cat2->dummy5, cat2->dummy6);
}

static void print_instr_cat3(instr_t *instr)
{
	instr_cat3_t *cat3 = &instr->cat3;

	printf(" ");
	print_reg((reg_t)(cat3->dst), true, false, false, false, false, false);
	printf(", ");
	print_reg((reg_t)(cat3->src1), true, false, false, false, false, false);
	printf(", ");
	print_reg((reg_t)cat3->src2, true, false, false, false, false, false);
	printf(", ");
	print_reg((reg_t)(cat3->src3), true, false, false, false, false, false);

	if ((debug & PRINT_VERBOSE) && (cat3->dummy1|cat3->dummy2|cat3->dummy3|cat3->dummy4))
		printf("\t{3: %x,%x,%x,%x}", cat3->dummy1, cat3->dummy2, cat3->dummy3, cat3->dummy4);
}

static void print_instr_cat4(instr_t *instr)
{
	instr_cat4_t *cat4 = &instr->cat4;

	printf(" ");
	print_reg((reg_t)(cat4->dst), true, false, false, false, false, false);
	printf(", ");
	print_reg((reg_t)(cat4->src1), true, false, false, false, false, false);

	if ((debug & PRINT_VERBOSE) && (cat4->dummy1|cat4->dummy2|cat4->dummy3))
		printf("\t{4: %x,%x,%x}", cat4->dummy1, cat4->dummy2, cat4->dummy3);
}

static void print_instr_cat5(instr_t *instr)
{
	instr_cat5_t *cat5 = &instr->cat5;

	if ((debug & PRINT_VERBOSE) && (cat5->dummy1|cat5->dummy2))
		printf("\t{5: %x,%x}", cat5->dummy1, cat5->dummy2);
}

static void print_instr_cat6(instr_t *instr)
{
	instr_cat6_t *cat6 = &instr->cat6;
	static const char *type[] = {
			"f16",
			"f32",
			"u16",
			"u32",
			"?s16?",
			"s32",
			"?u8?",
			"s8",
	};

	printf(".%s", type[cat6->type]);

	if ((debug & PRINT_VERBOSE) && (cat6->dummy1|cat6->dummy2|cat6->dummy3))
		printf("\t{6: %x,%x,%x}", cat6->dummy1, cat6->dummy2, cat6->dummy3);
}

struct opc_info {
	uint16_t cat;
	uint16_t opc;
	const char *name;
	void (*print)(instr_t *instr);
} opcs[0x1ff] = {
#define OPC(cat, opc, name, fxn) [((cat) << 6) | (opc)] = { (cat), (opc), #name, print_instr_##fxn }
	/* category 0: */
	OPC(0, OPC_NOP,          nop,           cat0),
	OPC(0, OPC_BR,           br,            cat0),
	OPC(0, OPC_JUMP,         jump,          cat0),
	OPC(0, OPC_CALL,         call,          cat0),
	OPC(0, OPC_RET,          ret,           cat0),
	OPC(0, OPC_END,          end,           cat0),

	/* category 1: */
	OPC(1, OPC_MOV_F32F32_2, mov.f32f32,    cat1), // XXX ???
	OPC(1, OPC_MOV_F32F32,   mov.f32f32,    cat1),
	OPC(1, OPC_MOV_S32S32,   mov.s32s32,    cat1),
	OPC(1, OPC_COV_F32F16,   cov.f32f16,    cat1),

	/* category 2: */
	OPC(2, OPC_ADD_F,        add.f,         cat2),
	OPC(2, OPC_MIN_F,        min.f,         cat2),
	OPC(2, OPC_MAX_F,        max.f,         cat2),
	OPC(2, OPC_MUL_F,        mul.f,         cat2),
	OPC(2, OPC_CMPS_F,       cmps.f,        cat2),
	OPC(2, OPC_ABSNEG_F,     absneg.f,      cat2),
	OPC(2, OPC_ADD_S,        add.s,         cat2),
	OPC(2, OPC_SUB_S,        sub.s,         cat2),
	OPC(2, OPC_CMPS_U,       cmps.u,        cat2),
	OPC(2, OPC_CMPS_S,       cmps.s,        cat2),
	OPC(2, OPC_MIN_S,        min.s,         cat2),
	OPC(2, OPC_MAX_S,        max.s,         cat2),
	OPC(2, OPC_ABSNEG_S,     absneg.s,      cat2),
	OPC(2, OPC_AND_B,        and.b,         cat2),
	OPC(2, OPC_OR_B,         or.b,          cat2),
	OPC(2, OPC_XOR_B,        xor.b,         cat2),
	OPC(2, OPC_MUL_S,        mul.s,         cat2),
	OPC(2, OPC_MULL_U,       mull.u,        cat2),
	OPC(2, OPC_CLZ_B,        clz.b,         cat2),
	OPC(2, OPC_SHL_B,        shl.b,         cat2),
	OPC(2, OPC_SHR_B,        shr.b,         cat2),
	OPC(2, OPC_ASHR_B,       ashr.b,        cat2),
	OPC(2, OPC_BARY_F,       bary.f,        cat2),

	/* category 3: */
	OPC(3, OPC_MADSH_M16,    madsh.m16,     cat3),
	OPC(3, OPC_MAD_F32,      mad.f32,       cat3),
	OPC(3, OPC_SEL_B16,      sel.b16,       cat3),
	OPC(3, OPC_SEL_B32,      sel.b32,       cat3),
	OPC(3, OPC_SEL_F32,      sel.f32,       cat3),

	/* category 4: */
	OPC(4, OPC_RCP,          rcp,           cat4),
	OPC(4, OPC_RSQ,          rsq,           cat4),
	OPC(4, OPC_LOG2,         log2,          cat4),
	OPC(4, OPC_EXP2,         exp2,          cat4),
	OPC(4, OPC_SIN,          sin,           cat4),
	OPC(4, OPC_COS,          cos,           cat4),
	OPC(4, OPC_SQRT,         sqrt,          cat4),

	/* category 5: */
	OPC(5, OPC_ISAM,         isam,          cat5),
	OPC(5, OPC_SAM,          sam,           cat5),
	OPC(5, OPC_GETSIZE,      getsize,       cat5),
	OPC(5, OPC_GETINFO,      getinfo,       cat5),

	/* category 6: */
	OPC(6, OPC_LDG,          ldg,           cat6),
	OPC(6, OPC_LDP,          ldp,           cat6),
	OPC(6, OPC_STG,          stg,           cat6),
	OPC(6, OPC_STP,          stp,           cat6),
	OPC(6, OPC_STI,          sti,           cat6),

#undef OPC
};

#define GETINFO(instr) (&(opcs[((instr)->opc_cat << 6) | getopc(instr)]))

static uint32_t getopc(instr_t *instr)
{
	switch (instr->opc_cat) {
	case 0:  return instr->cat0.opc;
	case 1:  return instr->cat1.opc;
	case 2:  return instr->cat2.opc;
	case 3:  return instr->cat3.opc;
	case 4:  return instr->cat4.opc;
	case 5:  return instr->cat5.opc;
	case 6:  return instr->cat6.opc;
	default: return 0;
	}
}

static void print_instr(uint32_t *dwords, int level, int n)
{
	instr_t *instr = (instr_t *)dwords;
	uint32_t opc = getopc(instr);
	const char *name;

	printf("%s%04d[%08xx_%08xx] ", levels[level], n, dwords[1], dwords[0]);

#if 0
	/* print unknown bits: */
	if (debug & PRINT_RAW)
		printf("[%08xx_%08xx] ", dwords[1] & 0x001ff800, dwords[0] & 0x00000000);

	if (debug & PRINT_VERBOSE)
		printf("%d,%02d ", instr->opc_cat, opc);
#endif

	/* NOTE: order flags are printed is a bit fugly.. but for now I
	 * try to match the order in llvm-a3xx disassembler for easy
	 * diff'ing..
	 */

	if (instr->sync)
		printf("(sy)");
	// XXX this at least doesn't apply to category 6, maybe only applies to category 0:
	if ((instr->opc_cat < 5) && instr->generic.ss)
		printf("(ss)");
	if (instr->jmp_tgt)
		printf("(jp)");
	// XXX this at least doesn't apply to category 6:
	if ((instr->opc_cat < 5) && instr->generic.repeat)
		printf("(rpt%d)", instr->generic.repeat);

	name = GETINFO(instr)->name;

	if (name) {
		printf("%s", name);
		GETINFO(instr)->print(instr);
	} else {
		printf("unknown(%d,%d)", instr->opc_cat, opc);
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
