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

#include "ir-a3xx.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "util.h"
#include "instr-a3xx.h"

/* simple allocator to carve allocations out of an up-front allocated heap,
 * so that we can free everything easily in one shot.
 */
static void * ir3_alloc(struct ir3_shader *shader, int sz)
{
	void *ptr = &shader->heap[shader->heap_idx];
	shader->heap_idx += ALIGN(sz, 4);
	return ptr;
}

static char * ir3_strdup(struct ir3_shader *shader, const char *str)
{
	char *ptr = NULL;
	if (str) {
		int len = strlen(str);
		ptr = ir3_alloc(shader, len+1);
		memcpy(ptr, str, len);
		ptr[len] = '\0';
	}
	return ptr;
}

struct ir3_shader * ir3_shader_create(void)
{
	DEBUG_MSG("");
	return calloc(1, sizeof(struct ir3_shader));
}

void ir3_shader_destroy(struct ir3_shader *shader)
{
	DEBUG_MSG("");
	free(shader);
}

#define iassert(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "bogus instruction at line %d\n", instr->line); \
		assert(cond); \
		return -1; \
	} } while (0)

static uint32_t reg(struct ir3_register *reg,
		struct ir3_shader_info *info, uint32_t valid_flags)
{
	reg_t val = { .dummy32 = 0 };

	assert(!(reg->flags & ~valid_flags));

	if (reg->flags & IR3_REG_IMMED) {
		val.iim_val = reg->iim_val;
	} else {
		val.comp = reg->num & 0x3;
		val.num  = reg->num >> 2;

		if (reg->flags & IR3_REG_CONST) {
			info->max_const = max(info->max_const, val.num);
		} else {
			/* update register stats: */
			if (reg->flags & IR3_REG_HALF) {
				info->max_half_reg =
					max(info->max_half_reg, val.num);
			} else {
				info->max_reg =
					max(info->max_reg, val.num);
			}
		}
	}

	return val.dummy32;
}

static int emit_cat0(struct ir3_instruction *instr, void *ptr,
		struct ir3_shader_info *info)
{
	instr_cat0_t *cat0 = ptr;

	cat0->immed    = instr->cat0.immed;
	cat0->repeat   = instr->repeat;
	cat0->ss       = !!(instr->flags & IR3_INSTR_SS);
	cat0->inv      = instr->cat0.inv;
	cat0->comp     = instr->cat0.comp;
	cat0->opc      = instr->opc;
	cat0->jmp_tgt  = !!(instr->flags & IR3_INSTR_JP);
	cat0->sync     = !!(instr->flags & IR3_INSTR_SY);
	cat0->opc_cat  = 0;

	return 0;
}

static uint32_t type_flags(type_t type)
{
	return (type_size(type) == 32) ? 0 : IR3_REG_HALF;
}

static int emit_cat1(struct ir3_instruction *instr, void *ptr,
		struct ir3_shader_info *info)
{
	struct ir3_register *dst = instr->regs[0];
	struct ir3_register *src = instr->regs[1];
	instr_cat1_t *cat1 = ptr;

	iassert(instr->regs_count == 2);
	iassert(!((dst->flags ^ type_flags(instr->cat1.dst_type)) & IR3_REG_HALF));
	iassert((src->flags & IR3_REG_IMMED) ||
			!((src->flags ^ type_flags(instr->cat1.src_type)) & IR3_REG_HALF));

	if (src->flags & IR3_REG_IMMED) {
		cat1->iim_val = src->iim_val;
		cat1->src_im  = 1;
	} else if (src->flags & IR3_REG_RELATIV) {
		cat1->off       = src->offset;
		cat1->src_rel   = 1;
		cat1->must_be_3 = 3;
	} else {
		cat1->src  = reg(src, info, IR3_REG_IMMED | IR3_REG_RELATIV |
				IR3_REG_R | IR3_REG_CONST | IR3_REG_HALF);
	}

	cat1->dst      = reg(dst, info, IR3_REG_RELATIV | IR3_REG_EVEN |
			IR3_REG_POS_INF | IR3_REG_HALF);
	cat1->repeat   = instr->repeat;
	cat1->src_r    = !!(src->flags & IR3_REG_R);
	cat1->ss       = !!(instr->flags & IR3_INSTR_SS);
	cat1->dst_type = instr->cat1.dst_type;
	cat1->dst_rel  = !!(dst->flags & IR3_REG_RELATIV);
	cat1->src_type = instr->cat1.src_type;
	cat1->src_c    = !!(src->flags & IR3_REG_CONST);
	cat1->even     = !!(dst->flags & IR3_REG_EVEN);
	cat1->pos_inf  = !!(dst->flags & IR3_REG_POS_INF);
	cat1->jmp_tgt  = !!(instr->flags & IR3_INSTR_JP);
	cat1->sync     = !!(instr->flags & IR3_INSTR_SY);
	cat1->opc_cat  = 1;

	return 0;
}

static int emit_cat2(struct ir3_instruction *instr, void *ptr,
		struct ir3_shader_info *info)
{
	struct ir3_register *dst = instr->regs[0];
	struct ir3_register *src1 = instr->regs[1];
	struct ir3_register *src2 = instr->regs[2];
	instr_cat2_t *cat2 = ptr;

	iassert((instr->regs_count == 2) || (instr->regs_count == 3));

	cat2->src1     = reg(src1, info, IR3_REG_RELATIV | IR3_REG_CONST |
			IR3_REG_IMMED | IR3_REG_NEGATE | IR3_REG_ABS | IR3_REG_R |
			IR3_REG_HALF);
	cat2->src1_rel = !!(src1->flags & IR3_REG_RELATIV);
	cat2->src1_c   = !!(src1->flags & IR3_REG_CONST);
	cat2->src1_im  = !!(src1->flags & IR3_REG_IMMED);
	cat2->src1_neg = !!(src1->flags & IR3_REG_NEGATE);
	cat2->src1_abs = !!(src1->flags & IR3_REG_ABS);
	cat2->src1_r   = !!(src1->flags & IR3_REG_R);

	if (src2) {
		iassert((src2->flags & IR3_REG_IMMED) ||
				!((src1->flags ^ src2->flags) & IR3_REG_HALF));
		cat2->src2     = reg(src2, info, IR3_REG_RELATIV | IR3_REG_CONST |
				IR3_REG_IMMED | IR3_REG_NEGATE | IR3_REG_ABS | IR3_REG_R |
				IR3_REG_HALF);
		cat2->src2_rel = !!(src2->flags & IR3_REG_RELATIV);
		cat2->src2_c   = !!(src2->flags & IR3_REG_CONST);
		cat2->src2_im  = !!(src2->flags & IR3_REG_IMMED);
		cat2->src2_neg = !!(src2->flags & IR3_REG_NEGATE);
		cat2->src2_abs = !!(src2->flags & IR3_REG_ABS);
		cat2->src2_r   = !!(src2->flags & IR3_REG_R);
	}

	cat2->dst      = reg(dst, info, IR3_REG_EI | IR3_REG_HALF);
	cat2->repeat   = instr->repeat;
	cat2->ss       = !!(instr->flags & IR3_INSTR_SS);
	cat2->ul       = !!(instr->flags & IR3_INSTR_UL);
	cat2->dst_half = !!((src1->flags ^ dst->flags) & IR3_REG_HALF);
	cat2->ei       = !!(dst->flags & IR3_REG_EI);
	cat2->cond     = instr->cat2.condition;
	cat2->full     = ! (src1->flags & IR3_REG_HALF);
	cat2->opc      = instr->opc;
	cat2->jmp_tgt  = !!(instr->flags & IR3_INSTR_JP);
	cat2->sync     = !!(instr->flags & IR3_INSTR_SY);
	cat2->opc_cat  = 2;

	return 0;
}

static int emit_cat3(struct ir3_instruction *instr, void *ptr,
		struct ir3_shader_info *info)
{
	struct ir3_register *dst = instr->regs[0];
	struct ir3_register *src1 = instr->regs[1];
	struct ir3_register *src2 = instr->regs[2];
	struct ir3_register *src3 = instr->regs[3];
	instr_cat3_t *cat3 = ptr;
	uint32_t src_flags = 0;

	switch (instr->opc) {
	case OPC_MAD_F16:
	case OPC_MAD_U16:
	case OPC_MAD_S16:
	case OPC_SEL_B16:
	case OPC_SEL_S16:
	case OPC_SEL_F16:
	case OPC_SAD_S16:
	case OPC_SAD_S32:  // really??
		src_flags |= IR3_REG_HALF;
		break;
	default:
		break;
	}

	iassert(instr->regs_count == 4);
	iassert(!((src1->flags ^ src_flags) & IR3_REG_HALF));
	iassert(!((src2->flags ^ src_flags) & IR3_REG_HALF));
	iassert(!((src3->flags ^ src_flags) & IR3_REG_HALF));

	cat3->src1     = reg(src1, info, IR3_REG_RELATIV | IR3_REG_CONST |
			IR3_REG_NEGATE | IR3_REG_R | IR3_REG_HALF);
	cat3->src1_rel = !!(src1->flags & IR3_REG_RELATIV);
	cat3->src1_c   = !!(src1->flags & IR3_REG_CONST);
	cat3->src1_neg = !!(src1->flags & IR3_REG_NEGATE);
	cat3->src1_r   = !!(src1->flags & IR3_REG_R);

	cat3->src2     = reg(src2, info, IR3_REG_CONST | IR3_REG_NEGATE |
			IR3_REG_R | IR3_REG_HALF);
	cat3->src2_c   = !!(src2->flags & IR3_REG_CONST);
	cat3->src2_neg = !!(src2->flags & IR3_REG_NEGATE);
	cat3->src2_r   = !!(src2->flags & IR3_REG_R);

	cat3->src3     = reg(src3, info, IR3_REG_RELATIV | IR3_REG_CONST |
			IR3_REG_NEGATE | IR3_REG_R | IR3_REG_HALF);
	cat3->src3_rel = !!(src3->flags & IR3_REG_RELATIV);
	cat3->src3_c   = !!(src3->flags & IR3_REG_CONST);
	cat3->src3_neg = !!(src3->flags & IR3_REG_NEGATE);
	cat3->src3_r   = !!(src3->flags & IR3_REG_R);

	cat3->dst      = reg(dst, info, IR3_REG_HALF);
	cat3->repeat   = instr->repeat;
	cat3->ss       = !!(instr->flags & IR3_INSTR_SS);
	cat3->ul       = !!(instr->flags & IR3_INSTR_UL);
	cat3->dst_half = !!((src_flags ^ dst->flags) & IR3_REG_HALF);
	cat3->opc      = instr->opc;
	cat3->jmp_tgt  = !!(instr->flags & IR3_INSTR_JP);
	cat3->sync     = !!(instr->flags & IR3_INSTR_SY);
	cat3->opc_cat  = 3;

	return 0;
}

static int emit_cat4(struct ir3_instruction *instr, void *ptr,
		struct ir3_shader_info *info)
{
	struct ir3_register *dst = instr->regs[0];
	struct ir3_register *src = instr->regs[1];
	instr_cat4_t *cat4 = ptr;

	iassert(instr->regs_count == 2);

	cat4->src      = reg(src, info, IR3_REG_RELATIV | IR3_REG_CONST |
			IR3_REG_IMMED | IR3_REG_NEGATE | IR3_REG_ABS | IR3_REG_R |
			IR3_REG_HALF);
	cat4->src_rel  = !!(src->flags & IR3_REG_RELATIV);
	cat4->src_c    = !!(src->flags & IR3_REG_CONST);
	cat4->src_im   = !!(src->flags & IR3_REG_IMMED);
	cat4->src_neg  = !!(src->flags & IR3_REG_NEGATE);
	cat4->src_abs  = !!(src->flags & IR3_REG_ABS);
	cat4->src_r    = !!(src->flags & IR3_REG_R);

	cat4->dst      = reg(dst, info, IR3_REG_HALF);
	cat4->repeat   = instr->repeat;
	cat4->ss       = !!(instr->flags & IR3_INSTR_SS);
	cat4->ul       = !!(instr->flags & IR3_INSTR_UL);
	cat4->dst_half = !!((src->flags ^ dst->flags) & IR3_REG_HALF);
	cat4->full     = ! (src->flags & IR3_REG_HALF);
	cat4->opc      = instr->opc;
	cat4->jmp_tgt  = !!(instr->flags & IR3_INSTR_JP);
	cat4->sync     = !!(instr->flags & IR3_INSTR_SY);
	cat4->opc_cat  = 4;

	return 0;
}

static int emit_cat5(struct ir3_instruction *instr, void *ptr,
		struct ir3_shader_info *info)
{
	struct ir3_register *dst = instr->regs[0];
	struct ir3_register *src1 = instr->regs[1];
	struct ir3_register *src2 = instr->regs[2];
	struct ir3_register *src3 = instr->regs[3];
	instr_cat5_t *cat5 = ptr;

	iassert(!((dst->flags ^ type_flags(instr->cat5.type)) & IR3_REG_HALF));

	if (src1) {
		cat5->full = ! (src1->flags & IR3_REG_HALF);
		cat5->src1 = reg(src1, info, IR3_REG_HALF);
	}


	if (instr->flags & IR3_INSTR_S2EN) {
		if (src2) {
			iassert(!((src1->flags ^ src2->flags) & IR3_REG_HALF));
			cat5->s2en.src2 = reg(src2, info, IR3_REG_HALF);
		}
		if (src3) {
			iassert(src3->flags & IR3_REG_HALF);
			cat5->s2en.src3 = reg(src3, info, IR3_REG_HALF);
		}
		iassert(!(instr->cat5.samp | instr->cat5.tex));
	} else {
		iassert(!src3);
		if (src2) {
			iassert(!((src1->flags ^ src2->flags) & IR3_REG_HALF));
			cat5->norm.src2 = reg(src2, info, IR3_REG_HALF);
		}
		cat5->norm.samp = instr->cat5.samp;
		cat5->norm.tex  = instr->cat5.tex;
	}

	cat5->dst      = reg(dst, info, IR3_REG_HALF);
	cat5->wrmask   = dst->wrmask;
	cat5->type     = instr->cat5.type;
	cat5->is_3d    = !!(instr->flags & IR3_INSTR_3D);
	cat5->is_a     = !!(instr->flags & IR3_INSTR_A);
	cat5->is_s     = !!(instr->flags & IR3_INSTR_S);
	cat5->is_s2en  = !!(instr->flags & IR3_INSTR_S2EN);
	cat5->is_o     = !!(instr->flags & IR3_INSTR_O);
	cat5->is_p     = !!(instr->flags & IR3_INSTR_P);
	cat5->opc      = instr->opc;
	cat5->jmp_tgt  = !!(instr->flags & IR3_INSTR_JP);
	cat5->sync     = !!(instr->flags & IR3_INSTR_SY);
	cat5->opc_cat  = 5;

	return 0;
}

static int emit_cat6(struct ir3_instruction *instr, void *ptr,
		struct ir3_shader_info *info)
{
	struct ir3_register *dst = instr->regs[0];
	struct ir3_register *src = instr->regs[1];
	instr_cat6_t *cat6 = ptr;

	iassert(instr->regs_count == 2);

	switch (instr->opc) {
	/* load instructions: */
	case OPC_LDG:
	case OPC_LDP:
	case OPC_LDL:
	case OPC_LDLW:
	case OPC_LDLV:
	case OPC_PREFETCH: {
		instr_cat6a_t *cat6a = ptr;

		iassert(!((dst->flags ^ type_flags(instr->cat6.type)) & IR3_REG_HALF));

		cat6a->must_be_one1  = 1;
		cat6a->must_be_one2  = 1;
		cat6a->off = instr->cat6.offset;
		cat6a->src = reg(src, info, 0);
		cat6a->dst = reg(dst, info, IR3_REG_HALF);
		break;
	}
	/* store instructions: */
	case OPC_STG:
	case OPC_STP:
	case OPC_STL:
	case OPC_STLW:
	case OPC_STI: {
		instr_cat6b_t *cat6b = ptr;
		uint32_t src_flags = type_flags(instr->cat6.type);
		uint32_t dst_flags = (instr->opc == OPC_STI) ? IR3_REG_HALF : 0;

		iassert(!((src->flags ^ src_flags) & IR3_REG_HALF));

		cat6b->must_be_one1  = 1;
		cat6b->must_be_one2  = 1;
		cat6b->src    = reg(src, info, src_flags);
		cat6b->off_hi = instr->cat6.offset >> 8;
		cat6b->off    = instr->cat6.offset;
		cat6b->dst    = reg(dst, info, dst_flags);

		break;
	}
	default:
		// TODO
		break;
	}

	cat6->iim_val  = instr->cat6.iim_val;
	cat6->type     = instr->cat6.type;
	cat6->opc      = instr->opc;
	cat6->jmp_tgt  = !!(instr->flags & IR3_INSTR_JP);
	cat6->sync     = !!(instr->flags & IR3_INSTR_SY);
	cat6->opc_cat  = 6;

	return 0;
}

static int (*emit[])(struct ir3_instruction *instr, void *ptr,
		struct ir3_shader_info *info) = {
	emit_cat0, emit_cat1, emit_cat2, emit_cat3, emit_cat4, emit_cat5, emit_cat6,
};

int ir3_shader_assemble(struct ir3_shader *shader,
		uint32_t *dwords, uint32_t sizedwords,
		struct ir3_shader_info *info)
{
	uint32_t i;

	info->max_reg       = -1;
	info->max_half_reg  = -1;
	info->max_const     = -1;

	/* need to include the attributes/vbo's in the register accounting,
	 * since they use registers, but are fetched outside of the shader
	 * program:
	 *
	 * TODO need to remember to handle this somehow in gallium driver,
	 * which wouldn't be using the assembler metadata (attributes/
	 * uniforms/etc)
	 */
	for (i = 0; i < shader->attributes_count; i++)
		reg(shader->attributes[i]->rstart, info, IR3_REG_HALF);

	/* need a integer number of instruction "groups" (sets of four
	 * instructions), so pad out w/ NOPs if needed:
	 */
	while (shader->instrs_count != ALIGN(shader->instrs_count, 4))
		ir3_instr_create(shader, 0, OPC_NOP);

	/* each instruction is 64bits: */
	if (sizedwords < (2 * shader->instrs_count))
		return -ENOSPC;

	memset(dwords, 0, 4 * 2 * shader->instrs_count);

	for (i = 0; i < shader->instrs_count; i++) {
		struct ir3_instruction *instr = shader->instrs[i];
		int ret = emit[instr->category](instr, dwords, info);
		if (ret)
			return ret;
		dwords += 2;
	}

	/* on success, return the shader size: */
	return 2 * shader->instrs_count;
}

static struct ir3_register * reg_create(struct ir3_shader *shader,
		int num, int flags)
{
	struct ir3_register *reg =
			ir3_alloc(shader, sizeof(struct ir3_register));
	DEBUG_MSG("%x, %d, %c", flags, num>>2, "xyzw"[num & 0x3]);
	reg->flags = flags;
	reg->num = num;
	return reg;
}

/* this is a bit of the logic from the parser (using bit 0 to indicate
 * half vs full register) leaking through.. but all this stuff can be
 * stripped out of the version of the IR code that ends up in gallium
 * so I guess it doesn't matter if it is a bit fugly..
 */
static struct ir3_register * reg_create_from_num(struct ir3_shader *shader,
		int num, int flags)
{
	if (num & 0x1)
		flags |= IR3_REG_HALF;
	return reg_create(shader, num>>1, flags);
}

struct ir3_attribute * ir3_attribute_create(struct ir3_shader *shader,
		int rstart, int num, const char *name)
{
	struct ir3_attribute *a = ir3_alloc(shader, sizeof(struct ir3_attribute));
	a->name   = ir3_strdup(shader, name);
	a->rstart = reg_create_from_num(shader, rstart, 0);
	a->num    = num;
	assert(shader->attributes_count < ARRAY_SIZE(shader->attributes));
	shader->attributes[shader->attributes_count++] = a;
	return a;
}

struct ir3_const * ir3_const_create(struct ir3_shader *shader,
		int cstart, float v0, float v1, float v2, float v3)
{
	struct ir3_const *c = ir3_alloc(shader, sizeof(struct ir3_const));
	c->val[0] = v0;
	c->val[1] = v1;
	c->val[2] = v2;
	c->val[3] = v3;
	c->cstart = reg_create_from_num(shader, cstart, IR3_REG_CONST);
	assert(shader->consts_count < ARRAY_SIZE(shader->consts));
	shader->consts[shader->consts_count++] = c;
	return c;
}

struct ir3_sampler * ir3_sampler_create(struct ir3_shader *shader,
		int idx, const char *name)
{
	struct ir3_sampler *s = ir3_alloc(shader, sizeof(struct ir3_sampler));
	s->name   = ir3_strdup(shader, name);
	s->idx    = idx;
	assert(shader->samplers_count < ARRAY_SIZE(shader->samplers));
	shader->samplers[shader->samplers_count++] = s;
	return s;
}

struct ir3_uniform * ir3_uniform_create(struct ir3_shader *shader,
		int cstart, int num, const char *name)
{
	struct ir3_uniform *u = ir3_alloc(shader, sizeof(struct ir3_uniform));
	u->name   = ir3_strdup(shader, name);
	u->cstart = reg_create_from_num(shader, cstart, IR3_REG_CONST);
	u->num    = num;
	assert(shader->uniforms_count < ARRAY_SIZE(shader->uniforms));
	shader->uniforms[shader->uniforms_count++] = u;
	return u;
}

struct ir3_varying * ir3_varying_create(struct ir3_shader *shader,
		int rstart, int num, const char *name)
{
	struct ir3_varying *v = ir3_alloc(shader, sizeof(struct ir3_varying));
	v->name   = ir3_strdup(shader, name);
	v->rstart = reg_create_from_num(shader, rstart, 0);
	v->num    = num;
	assert(shader->varyings_count < ARRAY_SIZE(shader->varyings));
	shader->varyings[shader->varyings_count++] = v;
	return v;
}

struct ir3_out * ir3_out_create(struct ir3_shader *shader,
		int rstart, int num, const char *name)
{
	struct ir3_out *o = ir3_alloc(shader, sizeof(struct ir3_varying));
	o->name   = ir3_strdup(shader, name);
	o->rstart = reg_create_from_num(shader, rstart, 0);
	o->num    = num;
	assert(shader->outs_count < ARRAY_SIZE(shader->outs));
	shader->outs[shader->outs_count++] = o;
	return o;
}

struct ir3_instruction * ir3_instr_create(struct ir3_shader *shader,
		int category, opc_t opc)
{
	struct ir3_instruction *instr =
			ir3_alloc(shader, sizeof(struct ir3_instruction));
	instr->shader = shader;
	instr->category = category;
	instr->opc = opc;
	assert(shader->instrs_count < ARRAY_SIZE(shader->instrs));
	shader->instrs[shader->instrs_count++] = instr;
	return instr;
}

struct ir3_register * ir3_reg_create(struct ir3_instruction *instr,
		int num, int flags)
{
	struct ir3_register *reg = reg_create(instr->shader, num, flags);
	assert(instr->regs_count < ARRAY_SIZE(instr->regs));
	instr->regs[instr->regs_count++] = reg;
	return reg;
}
