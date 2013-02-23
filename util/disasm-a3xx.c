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

static const char *type[] = {
		[TYPE_F16] = "f16",
		[TYPE_F32] = "f32",
		[TYPE_U16] = "u16",
		[TYPE_U32] = "u32",
		[TYPE_S16] = "s16",
		[TYPE_S32] = "s32",
		[TYPE_U8]  = "u8",
		[TYPE_S8]  = "s8",
};

static void print_reg(reg_t reg, bool full, bool r, bool c, bool im, bool neg, bool abs)
{
	const char type = c ? 'c' : 'r';

	// XXX I prefer - and || for neg/abs, but preserving format used
	// by libllvm-a3xx for easy diffing..

	if (abs && neg)
		printf("(absneg)");
	else if (neg)
		printf("(neg)");
	else if (abs)
		printf("(abs)");

	if (r)
		printf("(r)");

	if (im) {
		printf("%d", reg.iim_val);
	} else if ((reg.num == 61) && !c) {
		printf("a0.%c", component[reg.comp]);
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
	case OPC_KILL:
		printf(" %sp0.%c", cat0->inv ? "!" : "",
				component[cat0->comp]);
		break;
	case OPC_BR:
		printf(" %sp0.%c, #%d", cat0->inv ? "!" : "",
				component[cat0->comp], cat0->immed);
		break;
	case OPC_JUMP:
	case OPC_CALL:
		printf(" #%d", cat0->immed);
		break;
	}

	if ((debug & PRINT_VERBOSE) && (cat0->dummy1|cat0->dummy2|cat0->dummy3))
		printf("\t{0: %x,%x,%x}", cat0->dummy1, cat0->dummy2, cat0->dummy3);
}

static void print_instr_cat1(instr_t *instr)
{
	instr_cat1_t *cat1 = &instr->cat1;

	// XXX maybe a bug in libllvm disassembler?
	if (cat1->src_rel)
		printf("(ul)");

	if (cat1->src_type == cat1->dst_type) {
		if ((cat1->src_type == TYPE_S16) && (((reg_t)cat1->dst).num == 61)) {
			/* special case (nmemonic?): */
			printf("mova");
		} else {
			printf("mov.%s%s", type[cat1->src_type], type[cat1->dst_type]);
		}
	} else {
		printf("cov.%s%s", type[cat1->src_type], type[cat1->dst_type]);
	}

	printf(" ");

	if (cat1->even)
		printf("(even)");

	if (cat1->pos_inf)
		printf("(pos_infinity)");

	print_reg((reg_t)(cat1->dst), type_size(cat1->dst_type) == 32,
			false, false, false, false, false);

	printf(", ");

	// XXX maybe we can handle this in print_reg instead of special casing?
	// then we could use same for cat0..
	if (cat1->src_im) {
		if (type_float(cat1->src_type))
			printf("(%f)", cat1->fim_val);
		else
			printf("%d", cat1->iim_val);
	} else if (cat1->src_rel && !cat1->src_c) {
		printf("c<a0.x + %d>", cat1->off);
	} else {
		print_reg((reg_t)(cat1->src), type_size(cat1->src_type) == 32,
				cat1->src_r, cat1->src_c, cat1->src_im, false, false);
	}

	if ((debug & PRINT_VERBOSE) && (cat1->must_be_0))
		printf("\t{1: %x}", cat1->must_be_0);
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
	case OPC_CMPV_F:
	case OPC_CMPV_U:
	case OPC_CMPV_S:
		printf(".%s", cond[cat2->cond]);
		break;
	}

	printf(" ");
	if (cat2->shr)
		printf("(shr)");
	print_reg((reg_t)(cat2->dst), cat2->full ^ cat2->dst_half,
			false, false, false, false, false);
	printf(", ");
// XXX src1_rel
	print_reg((reg_t)(cat2->src1), cat2->full, cat2->src1_r,
			cat2->src1_c, cat2->src1_im, cat2->src1_neg, cat2->src1_abs);
	switch (cat2->opc) {
	case OPC_ABSNEG_F:
	case OPC_ABSNEG_S:
	case OPC_CLZ_B:
	case OPC_CLZ_S:
	case OPC_SIGN_F:
	case OPC_FLOOR_F:
	case OPC_CEIL_F:
	case OPC_RNDNE_F:
	case OPC_RNDAZ_F:
	case OPC_TRUNC_F:
	case OPC_NOT_B:
	case OPC_BFREV_B:
	case OPC_SETRM:
	case OPC_CBITS_B:
		/* these only have one src reg */
		break;
	default:
		printf(", ");
// XXX src2_rel
		print_reg((reg_t)(cat2->src2), cat2->full, cat2->src2_r,
				cat2->src2_c, cat2->src2_im, cat2->src2_neg, cat2->src2_abs);
		break;
	}
}

static void print_instr_cat3(instr_t *instr)
{
	instr_cat3_t *cat3 = &instr->cat3;
	bool full = true;

	// XXX is this based on opc or some other bit?
	switch (cat3->opc) {
	case OPC_MAD_U16:
	case OPC_MAD_S16:
	case OPC_SEL_B16:
	case OPC_SEL_S16:
	case OPC_SEL_F16:
	case OPC_SAD_S16:
	case OPC_SAD_S32:  // really??
		full = false;
		break;
	}

	printf(" ");
	print_reg((reg_t)(cat3->dst), full ^ cat3->dst_half,
			false, false, false, false, false);
	printf(", ");
	print_reg((reg_t)(cat3->src1), full,
			cat3->src1_r, cat3->src1_c, false, false, false);
	printf(", ");
	print_reg((reg_t)cat3->src2, full,
			cat3->src2_r, cat3->src2_c, false, false, false);
	printf(", ");
	print_reg((reg_t)(cat3->src3), full,
			cat3->src3_r, cat3->src3_c, false, false, false);

	if ((debug & PRINT_VERBOSE) && (cat3->dummy1|cat3->dummy2))
		printf("\t{3: %x,%x}", cat3->dummy1, cat3->dummy2);
}

static void print_instr_cat4(instr_t *instr)
{
	instr_cat4_t *cat4 = &instr->cat4;

	printf(" ");
	print_reg((reg_t)(cat4->dst), cat4->full ^ cat4->dst_half,
			false, false, false, false, false);
	printf(", ");
	print_reg((reg_t)(cat4->src), cat4->full,
			cat4->src_r, cat4->src_c, cat4->src_im,
			cat4->src_neg, cat4->src_abs);

	if ((debug & PRINT_VERBOSE) && (cat4->dummy1|cat4->dummy2))
		printf("\t{4: %x,%x}", cat4->dummy1, cat4->dummy2);
}

static void print_instr_cat5(instr_t *instr)
{
	instr_cat5_t *cat5 = &instr->cat5;
	int i;

	if (cat5->is_3d)   printf(".3d");
	if (cat5->is_a)    printf(".a");
	if (cat5->is_o)    printf(".o");
	if (cat5->is_p)    printf(".p");
	if (cat5->is_s)    printf(".s");
	if (cat5->is_s2en) printf(".s2en");

	printf(" ");

	switch (cat5->opc) {
	case OPC_DSXPP_1:
	case OPC_DSYPP_1:
		break;
	default:
		printf("(%s)", type[cat5->type]);
		break;
	}

	printf("(");
	// XXX double check swizzle order, this might be backwards:
	if (cat5->wrmask & (1 << 3))
		printf("x");
	if (cat5->wrmask & (1 << 2))
		printf("y");
	if (cat5->wrmask & (1 << 1))
		printf("z");
	if (cat5->wrmask & (1 << 0))
		printf("w");
	printf(")");

	print_reg((reg_t)(cat5->dst), type_size(cat5->type) == 32,
			false, false, false, false, false);

	switch (cat5->opc) {
	case OPC_GETINFO:
		break;
	default:
		printf(", ");
		print_reg((reg_t)(cat5->src), true, false, false, false, false, false);
		break;
	}

	switch (cat5->opc) {
	case OPC_ISAM:
	case OPC_SAM:
	case OPC_ISAML:
	case OPC_ISAMM:
	case OPC_SAMB:
	case OPC_SAML:
	case OPC_SAMGQ:
	case OPC_GETLOD:
	case OPC_CONV:
	case OPC_CONVM:
	case OPC_GATHER4R:
	case OPC_GATHER4G:
	case OPC_GATHER4B:
	case OPC_GATHER4A:
	case OPC_SAMGP0:
	case OPC_SAMGP1:
	case OPC_SAMGP2:
	case OPC_SAMGP3:
		// XXX invert this probably..
		printf(", s#%d", cat5->samp);
		break;
	}

	switch (cat5->opc) {
	case OPC_DSX:
	case OPC_DSY:
	case OPC_DSXPP_1:
	case OPC_DSYPP_1:
	case OPC_RGETPOS:
	case OPC_RGETINFO:
		break;
	default:
		printf(", t#%d", cat5->tex);
		break;
	}

	if ((debug & PRINT_VERBOSE) && (cat5->dummy1|cat5->dummy2))
		printf("\t{5: %x,%x}", cat5->dummy1, cat5->dummy2);
}

static int32_t u2i(uint32_t val, int nbits)
{
	return ((val >> (nbits-1)) * ~((1 << nbits) - 1)) | val;
}

static void print_instr_cat6(instr_t *instr)
{
	instr_cat6_t *cat6 = &instr->cat6;

	printf(".%s ", type[cat6->type]);

	switch (cat6->opc) {
	case OPC_LDG:
	case OPC_LDP:
		/* load instructions: */
		print_reg((reg_t)(cat6->a.dst), type_size(cat6->type) == 32,
				false, false, false, false, false);
		printf(",");
		printf("%c[", (cat6->opc == OPC_LDG) ? 'g' : 'p');
		print_reg((reg_t)(cat6->a.src), true,
				false, false, false, false, false);
		if (cat6->a.off)
			printf("%+d", cat6->a.off);
		printf("]");
		break;
	case OPC_STG:
	case OPC_STP:
		/* store instructions: */
		printf("%c[", (cat6->opc == OPC_STG) ? 'g' : 'p');
		print_reg((reg_t)(cat6->b.dst), true,
				false, false, false, false, false);
		if (cat6->b.off || cat6->b.off_hi)
			printf("%+d", u2i((cat6->b.off_hi << 8) | cat6->b.off, 13));
		printf("]");
		printf(",");
		print_reg((reg_t)(cat6->b.src), type_size(cat6->type) == 32,
				false, false, false, false, false);

		break;
	case OPC_STI:
		/* sti has same encoding as other store instructions, but
		 * slightly different syntax:
		 */
		print_reg((reg_t)(cat6->b.dst), false, // XXX is it always half?
				false, false, false, false, false);
		if (cat6->b.off || cat6->b.off_hi)
			printf("%+d", u2i((cat6->b.off_hi << 8) | cat6->b.off, 13));
		printf(",");
		print_reg((reg_t)(cat6->b.src), type_size(cat6->type) == 32,
				false, false, false, false, false);
		break;
	}

	printf(", %d", cat6->iim_val);

	if (debug & PRINT_VERBOSE) {
		switch (cat6->opc) {
		case OPC_LDG:
		case OPC_LDP:
			/* load instructions: */
			if (cat6->a.dummy1|cat6->a.dummy2|cat6->a.dummy3)
				printf("\t{6: %x,%x,%x}", cat6->a.dummy1, cat6->a.dummy2, cat6->a.dummy3);
			if ((cat6->a.must_be_one1 != 1) || (cat6->a.must_be_one2 != 1))
				printf("{?? %d,%d ??}", cat6->a.must_be_one1, cat6->a.must_be_one2);
			break;
		case OPC_STG:
		case OPC_STP:
		case OPC_STI:
			/* store instructions: */
			if (cat6->b.dummy1|cat6->b.dummy2)
				printf("\t{6: %x,%x}", cat6->b.dummy1, cat6->b.dummy2);
			if ((cat6->b.must_be_one1 != 1) || (cat6->b.must_be_one2 != 1) ||
					(cat6->b.must_be_zero1 != 0))
				printf("{?? %d,%d,%d ??}", cat6->b.must_be_one1, cat6->b.must_be_one2,
						cat6->b.must_be_zero1);
			break;
		}
	}
}

/* size of largest OPC field of all the instruction categories: */
#define NOPC_BITS 6

struct opc_info {
	uint16_t cat;
	uint16_t opc;
	const char *name;
	void (*print)(instr_t *instr);
} opcs[1 << (3+NOPC_BITS)] = {
#define OPC(cat, opc, name) [((cat) << NOPC_BITS) | (opc)] = { (cat), (opc), #name, print_instr_cat##cat }
	/* category 0: */
	OPC(0, OPC_NOP,          nop),
	OPC(0, OPC_BR,           br),
	OPC(0, OPC_JUMP,         jump),
	OPC(0, OPC_CALL,         call),
	OPC(0, OPC_RET,          ret),
	OPC(0, OPC_KILL,         kill),
	OPC(0, OPC_END,          end),
	OPC(0, OPC_EMIT,         emit),
	OPC(0, OPC_CUT,          cut),
	OPC(0, OPC_CHMASK,       chmask),
	OPC(0, OPC_CHSH,         chsh),
	OPC(0, OPC_FLOW_REV,     flow_rev),

	/* category 1: */
	OPC(1, 0, ),

	/* category 2: */
	OPC(2, OPC_ADD_F,        add.f),
	OPC(2, OPC_MIN_F,        min.f),
	OPC(2, OPC_MAX_F,        max.f),
	OPC(2, OPC_MUL_F,        mul.f),
	OPC(2, OPC_SIGN_F,       sign.f),
	OPC(2, OPC_CMPS_F,       cmps.f),
	OPC(2, OPC_ABSNEG_F,     absneg.f),
	OPC(2, OPC_CMPV_F,       cmpv.f),
	OPC(2, OPC_FLOOR_F,      floor.f),
	OPC(2, OPC_CEIL_F,       ceil.f),
	OPC(2, OPC_RNDNE_F,      rndne.f),
	OPC(2, OPC_RNDAZ_F,      rndaz.f),
	OPC(2, OPC_TRUNC_F,      trunc.f),
	OPC(2, OPC_ADD_U,        add.u),
	OPC(2, OPC_ADD_S,        add.s),
	OPC(2, OPC_SUB_U,        sub.u),
	OPC(2, OPC_SUB_S,        sub.s),
	OPC(2, OPC_CMPS_U,       cmps.u),
	OPC(2, OPC_CMPS_S,       cmps.s),
	OPC(2, OPC_MIN_U,        min.u),
	OPC(2, OPC_MIN_S,        min.s),
	OPC(2, OPC_MAX_U,        max.u),
	OPC(2, OPC_MAX_S,        max.s),
	OPC(2, OPC_ABSNEG_S,     absneg.s),
	OPC(2, OPC_AND_B,        and.b),
	OPC(2, OPC_OR_B,         or.b),
	OPC(2, OPC_NOT_B,        not.b),
	OPC(2, OPC_XOR_B,        xor.b),
	OPC(2, OPC_CMPV_U,       cmpv.u),
	OPC(2, OPC_CMPV_S,       cmpv.s),
	OPC(2, OPC_MUL_U,        mul.u),
	OPC(2, OPC_MUL_S,        mul.s),
	OPC(2, OPC_MULL_U,       mull.u),
	OPC(2, OPC_BFREV_B,      bfrev.b),
	OPC(2, OPC_CLZ_S,        clz.s),
	OPC(2, OPC_CLZ_B,        clz.b),
	OPC(2, OPC_SHL_B,        shl.b),
	OPC(2, OPC_SHR_B,        shr.b),
	OPC(2, OPC_ASHR_B,       ashr.b),
	OPC(2, OPC_BARY_F,       bary.f),
	OPC(2, OPC_MGEN_B,       mgen.b),
	OPC(2, OPC_GETBIT_B,     getbit.b),
	OPC(2, OPC_SETRM,        setrm),
	OPC(2, OPC_CBITS_B,      cbits.b),
	OPC(2, OPC_SHB,          shb),
	OPC(2, OPC_MSAD,         msad),

	/* category 3: */
	OPC(3, OPC_MAD_U16,      mad.u16),
	OPC(3, OPC_MADSH_U16,    madsh.u16),
	OPC(3, OPC_MAD_S16,      mad.s16),
	OPC(3, OPC_MADSH_M16,    madsh.m16),
	OPC(3, OPC_MAD_U24,      mad.u24),
	OPC(3, OPC_MAD_S24,      mad.s24),
	OPC(3, OPC_MAD_F16,      mad.f16),
	OPC(3, OPC_MAD_F32,      mad.f32),
	OPC(3, OPC_SEL_B16,      sel.b16),
	OPC(3, OPC_SEL_B32,      sel.b32),
	OPC(3, OPC_SEL_S16,      sel.s16),
	OPC(3, OPC_SEL_S32,      sel.s32),
	OPC(3, OPC_SEL_F16,      sel.f16),
	OPC(3, OPC_SEL_F32,      sel.f32),
	OPC(3, OPC_SAD_S16,      sad.s16),
	OPC(3, OPC_SAD_S32,      sad.s32),

	/* category 4: */
	OPC(4, OPC_RCP,          rcp),
	OPC(4, OPC_RSQ,          rsq),
	OPC(4, OPC_LOG2,         log2),
	OPC(4, OPC_EXP2,         exp2),
	OPC(4, OPC_SIN,          sin),
	OPC(4, OPC_COS,          cos),
	OPC(4, OPC_SQRT,         sqrt),

	/* category 5: */
	OPC(5, OPC_ISAM,         isam),
	OPC(5, OPC_ISAML,        isaml),
	OPC(5, OPC_ISAMM,        isamm),
	OPC(5, OPC_SAM,          sam),
	OPC(5, OPC_SAMB,         samb),
	OPC(5, OPC_SAML,         saml),
	OPC(5, OPC_SAMGQ,        samgq),
	OPC(5, OPC_GETLOD,       getlod),
	OPC(5, OPC_CONV,         conv),
	OPC(5, OPC_CONVM,        convm),
	OPC(5, OPC_GETSIZE,      getsize),
	OPC(5, OPC_GETBUF,       getbuf),
	OPC(5, OPC_GETPOS,       getpos),
	OPC(5, OPC_GETINFO,      getinfo),
	OPC(5, OPC_DSX,          dsx),
	OPC(5, OPC_DSY,          dsy),
	OPC(5, OPC_GATHER4R,     gather4r),
	OPC(5, OPC_GATHER4G,     gather4g),
	OPC(5, OPC_GATHER4B,     gather4b),
	OPC(5, OPC_GATHER4A,     gather4a),
	OPC(5, OPC_SAMGP0,       samgp0),
	OPC(5, OPC_SAMGP1,       samgp1),
	OPC(5, OPC_SAMGP2,       samgp2),
	OPC(5, OPC_SAMGP3,       samgp3),
	OPC(5, OPC_DSXPP_1,      dsxpp.1),
	OPC(5, OPC_DSYPP_1,      dsypp.1),
	OPC(5, OPC_RGETPOS,      rgetpos),
	OPC(5, OPC_RGETINFO,     rgetinfo),


	/* category 6: */
	OPC(6, OPC_LDG,          ldg),
	OPC(6, OPC_LDL,          ldl),
	OPC(6, OPC_LDP,          ldp),
	OPC(6, OPC_STG,          stg),
	OPC(6, OPC_STL,          stl),
	OPC(6, OPC_STP,          stp),
	OPC(6, OPC_STI,          sti),
	OPC(6, OPC_G2L,          g2l),
	OPC(6, OPC_L2G,          l2g),
	OPC(6, OPC_PREFETCH,     prefetch),
	OPC(6, OPC_LDLW,         ldlw),
	OPC(6, OPC_STLW,         stlw),
	OPC(6, OPC_RESFMT,       resfmt),
	OPC(6, OPC_RESINFO,      resinf),
	OPC(6, OPC_ATOMIC_ADD_L,     atomic.add.l),
	OPC(6, OPC_ATOMIC_SUB_L,     atomic.sub.l),
	OPC(6, OPC_ATOMIC_XCHG_L,    atomic.xchg.l),
	OPC(6, OPC_ATOMIC_INC_L,     atomic.inc.l),
	OPC(6, OPC_ATOMIC_DEC_L,     atomic.dec.l),
	OPC(6, OPC_ATOMIC_CMPXCHG_L, atomic.cmpxchg.l),
	OPC(6, OPC_ATOMIC_MIN_L,     atomic.min.l),
	OPC(6, OPC_ATOMIC_MAX_L,     atomic.max.l),
	OPC(6, OPC_ATOMIC_AND_L,     atomic.and.l),
	OPC(6, OPC_ATOMIC_OR_L,      atomic.or.l),
	OPC(6, OPC_ATOMIC_XOR_L,     atomic.xor.l),
	OPC(6, OPC_LDGB_TYPED_4D,    ldgb.typed.4d),
	OPC(6, OPC_STGB_4D_4,    stgb.4d.4),
	OPC(6, OPC_STIB,         stib),
	OPC(6, OPC_LDC_4,        ldc.4),
	OPC(6, OPC_LDLV,         ldlv),


#undef OPC
};

#define GETINFO(instr) (&(opcs[((instr)->opc_cat << NOPC_BITS) | getopc(instr)]))

static uint32_t getopc(instr_t *instr)
{
	switch (instr->opc_cat) {
	case 0:  return instr->cat0.opc;
	case 1:  return 0;
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
	if (instr->ss && ((1 <= instr->opc_cat) && (instr->opc_cat <= 4)))
		printf("(ss)");
	if (instr->jmp_tgt)
		printf("(jp)");
	if (instr->repeat && (instr->opc_cat <= 4))
		printf("(rpt%d)", instr->repeat);
	if (instr->ul && ((2 <= instr->opc_cat) && (instr->opc_cat <= 4)))
		printf("(ul)");

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
