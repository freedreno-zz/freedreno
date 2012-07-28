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

#include "ir.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "util.h"

#define REG_MASK 0x3f	/* not really sure how many regs yet */
#define ADDR_MASK 0xfff

static int cf_emit(struct ir_cf *cf1, struct ir_cf *cf2, uint32_t *dwords);

static int instr_emit(struct ir_instruction *instr, uint32_t *dwords);

static uint32_t reg_fetch_src_swiz(struct ir_register *reg);
static uint32_t reg_fetch_dst_swiz(struct ir_register *reg);
static uint32_t reg_alu_dst_swiz(struct ir_register *reg);
static uint32_t reg_alu_src_swiz(struct ir_register *reg);


/* simple allocator to carve allocations out of an up-front allocated heap,
 * so that we can free everything easily in one shot.
 */
static void * ir_alloc(struct ir_shader *shader, int sz)
{
	void *ptr = &shader->heap[shader->heap_idx];
	shader->heap_idx += ALIGN(sz, 4);
	return ptr;
}

static char * ir_strdup(struct ir_shader *shader, const char *str)
{
	char *ptr = NULL;
	if (str) {
		int len = strlen(str);
		ptr = ir_alloc(shader, len+1);
		memcpy(ptr, str, len);
		ptr[len] = '\0';
	}
	return ptr;
}

struct ir_shader * ir_shader_create(void)
{
	DEBUG_MSG("");
	return calloc(1, sizeof(struct ir_shader));
}

void ir_shader_destroy(struct ir_shader *shader)
{
	DEBUG_MSG("");
	free(shader);
}

/* resolve addr/cnt/sequence fields in the individual CF's */
static int shader_resolve(struct ir_shader *shader)
{
	uint32_t addr;
	int i, j;

	addr = shader->cfs_count / 2;
	for (i = 0; i < shader->cfs_count; i++) {
		struct ir_cf *cf = shader->cfs[i];
		if ((cf->cf_type == T_EXEC) || (cf->cf_type == T_EXEC_END)) {
			uint32_t sequence = 0;

			if (cf->exec.addr && (cf->exec.addr != addr))
				WARN_MSG("invalid addr '%d' at CF %d", cf->exec.addr, i);
			if (cf->exec.cnt && (cf->exec.cnt != cf->exec.instrs_count))
				WARN_MSG("invalid cnt '%d' at CF %d", cf->exec.cnt, i);

			for (j = cf->exec.instrs_count - 1; j >= 0; j--) {
				struct ir_instruction *instr = cf->exec.instrs[j];
				sequence <<= 2;
				if (instr->instr_type == T_FETCH)
					sequence |= 0x1;
				if (instr->sync)
					sequence |= 0x2;
			}

			cf->exec.addr = addr;
			cf->exec.cnt  = cf->exec.instrs_count;
			cf->exec.sequence = sequence;

			addr += cf->exec.instrs_count;
		}
	}

	return 0;
}

int ir_shader_assemble(struct ir_shader *shader,
		uint32_t *dwords, int sizedwords)
{
	uint32_t i, j;
	uint32_t *ptr = dwords;
	int ret;

	/* we need an even # of CF's.. insert a NOP if needed */
	if (shader->cfs_count != ALIGN(shader->cfs_count, 2))
		ir_cf_create(shader, T_NOP);

	/* first pass, resolve sizes and addresses: */
	ret = shader_resolve(shader);
	if (ret) {
		ERROR_MSG("resolve failed: %d", ret);
		return ret;
	}

	/* second pass, emit CF program in pairs: */
	for (i = 0; i < shader->cfs_count; i += 2) {
		ret = cf_emit(shader->cfs[i], shader->cfs[i+1], ptr);
		if (ret) {
			ERROR_MSG("CF emit failed: %d\n", ret);
			return ret;
		}
		ptr += 3;
	}

	/* third pass, emit ALU/FETCH: */
	for (i = 0; i < shader->cfs_count; i++) {
		struct ir_cf *cf = shader->cfs[i];
		if ((cf->cf_type == T_EXEC) || (cf->cf_type == T_EXEC_END)) {
			for (j = 0; j < cf->exec.instrs_count; j++) {
				ret = instr_emit(cf->exec.instrs[j], ptr);
				if (ret) {
					ERROR_MSG("instruction emit failed: %d", ret);
					return ret;
				}
				ptr += 3;
			}
		}
	}

	return ptr - dwords;
}


struct ir_cf * ir_cf_create(struct ir_shader *shader, int cf_type)
{
	struct ir_cf *cf = ir_alloc(shader, sizeof(struct ir_cf));
	DEBUG_MSG("%d", cf_type);
	cf->shader = shader;
	cf->cf_type = cf_type;
	assert(shader->cfs_count < ARRAY_SIZE(shader->cfs));
	shader->cfs[shader->cfs_count++] = cf;
	return cf;
}

static uint32_t cf_op(struct ir_cf *cf)
{
	switch (cf->cf_type) {
	default:
		ERROR_MSG("invalid CF: %d\n", cf->cf_type);
	case T_NOP:         return 0x0;
	case T_EXEC:        return 0x1;
	case T_EXEC_END:    return 0x2;
	case T_ALLOC:       return 0xc;
	}
}

/*
 * CF instruction format:
 * -- ----------- ------
 *
 *     dword0:   0..11   -  addr/size 1
 *              12..15   -  count 1
 *              16..31   -  sequence.. 2 bits per instruction in the EXEC
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
 *     dword2:   0..23   -  <UNKNOWN>
 *              24..31   -  op 2
 *
 */

static int cf_emit(struct ir_cf *cf1, struct ir_cf *cf2, uint32_t *dwords)
{
	dwords[0] = dwords[1] = dwords[2] = 0;

	dwords[1] |= cf_op(cf1) << 12;
	dwords[2] |= cf_op(cf2) << 28;

	switch (cf1->cf_type) {
	case T_EXEC:
	case T_EXEC_END:
		assert(cf1->exec.addr <= ADDR_MASK);
		assert(cf1->exec.cnt <= 0xf);
		assert(cf1->exec.sequence <= 0xffff);
		dwords[0] |= cf1->exec.addr;
		dwords[0] |= cf1->exec.cnt << 12;
		dwords[0] |= cf1->exec.sequence << 16;
		break;
	case T_ALLOC:
		assert(cf1->alloc.size <= ADDR_MASK);
		dwords[0] |= cf1->alloc.size;
		dwords[1] |= ((cf1->alloc.type == T_COORD) ? 0x2 : 0x4) << 8;
		break;
	}

	switch (cf2->cf_type) {
	case T_EXEC:
	case T_EXEC_END:
		assert(cf2->exec.addr <= ADDR_MASK);
		assert(cf2->exec.cnt <= 0xf);
		assert(cf2->exec.sequence <= 0xffff);
		dwords[1] |= cf2->exec.addr << 16;
		dwords[1] |= cf2->exec.cnt << 28;
		assert(cf2->exec.sequence == 0);  /* don't know where this goes yet */
		break;
	case T_ALLOC:
		assert(cf2->alloc.size <= ADDR_MASK);
		dwords[1] |= cf2->alloc.size << 16;
		dwords[2] |= ((cf2->alloc.type == T_COORD) ? 0x2 : 0x4) << 24;
		break;
	}

	return 0;
}


struct ir_instruction * ir_instr_create(struct ir_cf *cf, int instr_type)
{
	struct ir_instruction *instr =
			ir_alloc(cf->shader, sizeof(struct ir_instruction));
	DEBUG_MSG("%d", instr_type);
	instr->shader = cf->shader;
	instr->instr_type = instr_type;
	assert(cf->exec.instrs_count < ARRAY_SIZE(cf->exec.instrs));
	cf->exec.instrs[cf->exec.instrs_count++] = instr;
	return instr;
}

static uint32_t instr_fetch_opc(struct ir_instruction *instr)
{
	switch (instr->fetch.opc) {
	default:
		ERROR_MSG("invalid fetch opc: %d\n", instr->fetch.opc);
	case T_SAMPLE: return 0x01;
	case T_VERTEX: return 0x00;
	}
}

static uint32_t instr_fetch_type(struct ir_instruction *instr)
{
	switch (instr->fetch.type) {
	default:
		ERROR_MSG("invalid fetch type: %d\n", instr->fetch.type);
	case T_GL_FLOAT: return 0x39;
	case T_GL_SHORT: return 0x1a;
	case T_GL_BYTE:  return 0x06;
	case T_GL_FIXED: return 0x23;
	}
}

static uint32_t instr_vector_opc(struct ir_instruction *instr)
{
	switch (instr->alu.vector_opc) {
	default:
		ERROR_MSG("invalid vector opc: %d\n", instr->alu.vector_opc);
	case T_ADDv:    return 0x00;
	case T_MULv:    return 0x01;
	case T_MAXv:    return 0x02;
	case T_MINv:    return 0x03;
	case T_FLOORv:  return 0x0a;
	case T_MULADDv: return 0x0b;
	case T_DOT4v:   return 0x0f;
	case T_DOT3v:   return 0x10;
	}
}

static uint32_t instr_scalar_opc(struct ir_instruction *instr)
{
	switch (instr->alu.scalar_opc) {
	default:
		ERROR_MSG("invalid scalar: %d\n", instr->alu.scalar_opc);
	case T_MOV:   return 0x02;
	case T_EXP2:  return 0x07;
	case T_LOG2:  return 0x08;
	case T_RCP:   return 0x09;
	case T_RSQ:   return 0x0b;
	case T_PSETE: return 0x0d;
	case T_SQRT:  return 0x14;
	case T_MUL:   return 0x15;
	case T_ADD:   return 0x16;
	}
}

/*
 * FETCH instruction format:
 * ----- ----------- ------
 *
 *     dword0:   0..4?   -  fetch operation
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
 *              13..15   -  <UNKNOWN>
 *              16..21?  -  type
 *                            0x39 - GL_FLOAT
 *                            0x1a - GL_SHORT
 *                            0x06 - GL_BYTE
 *                            0x23 - GL_FIXED
 *             22?..31   -  <UNKNOWN>
 *
 *     dword2:   0..15   -  stride (more than 0xff and data is copied/packed)
 *              16..31   -  <UNKNOWN>
 *
 * note: at least VERTEX fetch instructions get patched up at runtime
 * based on the size of attributes attached.. for example, if vec4, but
 * size given in glVertexAttribPointer() then the write mask and size
 * (stride?) is updated
 */

static int instr_emit_fetch(struct ir_instruction *instr, uint32_t *dwords)
{
	int reg = 0;
	struct ir_register *dst_reg = instr->regs[reg++];
	struct ir_register *src_reg = instr->regs[reg++];

	dwords[0] = dwords[1] = dwords[2] = 0;

	assert(instr->fetch.constant <= 0xf);

	dwords[0] |= instr_fetch_opc(instr)      << 0;
	dwords[0] |= src_reg->num                << 5;
	dwords[0] |= dst_reg->num                << 12;
	dwords[0] |= instr->fetch.constant       << 20;
	dwords[0] |= reg_fetch_src_swiz(src_reg) << 26;
	dwords[1] |= reg_fetch_dst_swiz(dst_reg) << 0;

	if (instr->fetch.opc == T_VERTEX) {
		assert(instr->fetch.stride <= 0xff);

		dwords[1] |= ((instr->fetch.sign == T_SIGNED) ? 1 : 0)
		                                     << 12;
		dwords[1] |= instr_fetch_type(instr) << 16;
		dwords[2] |= instr->fetch.stride     << 0;

		/* XXX this seems to always be set, except on the
		 * internal shaders used for GMEM->MEM blits
		 */
		dwords[1] |= 0x1                     << 13;
	}

	return 0;
}

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
 */

static int instr_emit_alu(struct ir_instruction *instr, uint32_t *dwords)
{
	int reg = 0;
	struct ir_register *dst_reg  = instr->regs[reg++];
	struct ir_register *src1_reg;
	struct ir_register *src2_reg;
	struct ir_register *src3_reg;

	/* handle instructions w/ 3 src operands: */
	if (instr->alu.vector_opc == T_MULADDv) {
		/* note: disassembler lists 3rd src first, ie:
		 *   MULADDv Rdst = Rsrc3 + (Rsrc1 * Rsrc2)
		 * which is the reason for this strange ordering.
		 */
		src3_reg = instr->regs[reg++];
	} else {
		src3_reg = NULL;
	}

	src1_reg = instr->regs[reg++];
	src2_reg = instr->regs[reg++];

	dwords[0] = dwords[1] = dwords[2] = 0;

	assert((dst_reg->flags & ~IR_REG_EXPORT) == 0);
	assert(!dst_reg->swizzle || (strlen(dst_reg->swizzle) == 4));
	assert((src1_reg->flags & IR_REG_EXPORT) == 0);
	assert(!src1_reg->swizzle || (strlen(src1_reg->swizzle) == 4));
	assert((src2_reg->flags & IR_REG_EXPORT) == 0);
	assert(!src2_reg->swizzle || (strlen(src2_reg->swizzle) == 4));

	dwords[0] |= dst_reg->num                                << 0;
	dwords[0] |= ((dst_reg->flags & IR_REG_EXPORT) ? 1 : 0)  << 15;
	dwords[0] |= reg_alu_dst_swiz(dst_reg)                   << 16;
	dwords[0] |= 1                                           << 26; /* always set? */
	dwords[1] |= reg_alu_src_swiz(src2_reg)                  << 8;
	dwords[1] |= reg_alu_src_swiz(src1_reg)                  << 16;
	dwords[1] |= ((src2_reg->flags & IR_REG_NEGATE) ? 1 : 0) << 25;
	dwords[1] |= ((src1_reg->flags & IR_REG_NEGATE) ? 1 : 0) << 26;
	// TODO predicate case/condition.. need to add to parser
	dwords[2] |= src2_reg->num                               << 8;
	dwords[2] |= ((src2_reg->flags & IR_REG_ABS) ? 1 : 0)    << 15;
	dwords[2] |= src1_reg->num                               << 16;
	dwords[2] |= ((src1_reg->flags & IR_REG_ABS) ? 1 : 0)    << 23;
	dwords[2] |= instr_vector_opc(instr)                     << 24;
	dwords[2] |= ((src2_reg->flags & IR_REG_CONST) ? 0 : 1)  << 30;
	dwords[2] |= ((src1_reg->flags & IR_REG_CONST) ? 0 : 1)  << 31;

	if (instr->alu.scalar_opc) {
		struct ir_register *sdst_reg = instr->regs[reg++];

		assert(sdst_reg->flags == dst_reg->flags);

		if (src3_reg) {
			assert(src3_reg == instr->regs[reg++]);
		} else {
			src3_reg = instr->regs[reg++];
		}

		dwords[0] |= sdst_reg->num                              << 8;
		dwords[0] |= reg_alu_dst_swiz(sdst_reg)                 << 20;
		dwords[0] |= instr_scalar_opc(instr)                    << 27;
	} else {
		/* not sure if this is required, but adreno compiler seems
		 * to always set scalar opc to MOV if it is not used:
		 */
		dwords[0] |= 0x2                                        << 27;
	}

	if (src3_reg) {
		dwords[1] |= reg_alu_src_swiz(src3_reg)                 << 0;
		dwords[1] |= ((src3_reg->flags & IR_REG_NEGATE) ? 1 : 0)<< 24;
		dwords[2] |= src3_reg->num                              << 0;
		dwords[2] |= ((src3_reg->flags & IR_REG_ABS) ? 1 : 0)   << 7;
		dwords[2] |= ((src3_reg->flags & IR_REG_CONST) ? 0 : 1) << 29;
	} else {
		/* not sure if this is required, but adreno compiler seems
		 * to always set register bank for 3rd src if unused:
		 */
		dwords[2] |= 1                                          << 29;
	}

	return 0;
}

static int instr_emit(struct ir_instruction *instr, uint32_t *dwords)
{
	switch (instr->instr_type) {
	case T_FETCH: return instr_emit_fetch(instr, dwords);
	case T_ALU:   return instr_emit_alu(instr, dwords);
	}
	return -1;
}


struct ir_register * ir_reg_create(struct ir_instruction *instr,
		int num, const char *swizzle, int flags)
{
	struct ir_register *reg =
			ir_alloc(instr->shader, sizeof(struct ir_register));
	DEBUG_MSG("%x, %d, %s", flags, num, swizzle);
	assert(num <= REG_MASK);
	reg->flags = flags;
	reg->num = num;
	reg->swizzle = ir_strdup(instr->shader, swizzle);
	assert(instr->regs_count < ARRAY_SIZE(instr->regs));
	instr->regs[instr->regs_count++] = reg;
	return reg;
}

static uint32_t reg_fetch_src_swiz(struct ir_register *reg)
{
	uint32_t swiz = 0;
	int i;

	assert(reg->flags == 0);
	assert(!reg->swizzle || (strlen(reg->swizzle) == 3));

	DEBUG_MSG("fetch src R%d.%s", reg->num, reg->swizzle);

	if (reg->swizzle) {
		for (i = 2; i >= 0; i--) {
			swiz <<= 2;
			switch (reg->swizzle[i]) {
			default:
				ERROR_MSG("invalid fetch src swizzle: %s", reg->swizzle);
			case 'x': swiz |= 0x0; break;
			case 'y': swiz |= 0x1; break;
			case 'z': swiz |= 0x2; break;
			case 'w': swiz |= 0x3; break;
			}
		}
	} else {
		swiz = 0x24;
	}

	return swiz;
}

static uint32_t reg_fetch_dst_swiz(struct ir_register *reg)
{
	uint32_t swiz = 0;
	int i;

	assert(reg->flags == 0);
	assert(!reg->swizzle || (strlen(reg->swizzle) == 4));

	DEBUG_MSG("fetch dst R%d.%s", reg->num, reg->swizzle);

	if (reg->swizzle) {
		for (i = 3; i >= 0; i--) {
			swiz <<= 3;
			switch (reg->swizzle[i]) {
			default:
				ERROR_MSG("invalid dst swizzle: %s", reg->swizzle);
			case 'x': swiz |= 0x0; break;
			case 'y': swiz |= 0x1; break;
			case 'z': swiz |= 0x2; break;
			case 'w': swiz |= 0x3; break;
			case '0': swiz |= 0x4; break;
			case '1': swiz |= 0x5; break;
			case '_': swiz |= 0x7; break;
			}
		}
	} else {
		swiz = 0x688;
	}

	return swiz;
}

/* actually, a write-mask */
static uint32_t reg_alu_dst_swiz(struct ir_register *reg)
{
	uint32_t swiz = 0;
	int i;

	assert((reg->flags & ~IR_REG_EXPORT) == 0);
	assert(!reg->swizzle || (strlen(reg->swizzle) == 4));

	DEBUG_MSG("alu dst R%d.%s", reg->num, reg->swizzle);

	if (reg->swizzle) {
		for (i = 3; i >= 0; i--) {
			swiz <<= 1;
			if (reg->swizzle[i] == "xyzw"[i]) {
				swiz |= 0x1;
			} else if (reg->swizzle[i] != '_') {
				ERROR_MSG("invalid dst swizzle: %s", reg->swizzle);
				break;
			}
		}
	} else {
		swiz = 0xf;
	}

	return swiz;
}

static uint32_t reg_alu_src_swiz(struct ir_register *reg)
{
	uint32_t swiz = 0;
	int i;

	assert((reg->flags & IR_REG_EXPORT) == 0);
	assert(!reg->swizzle || (strlen(reg->swizzle) == 4));

	DEBUG_MSG("vector src R%d.%s", reg->num, reg->swizzle);

	if (reg->swizzle) {
		for (i = 3; i >= 0; i--) {
			swiz <<= 2;
			switch (reg->swizzle[i]) {
			default:
				ERROR_MSG("invalid vector src swizzle: %s", reg->swizzle);
			case 'x': swiz |= (0x0 - i) & 0x3; break;
			case 'y': swiz |= (0x1 - i) & 0x3; break;
			case 'z': swiz |= (0x2 - i) & 0x3; break;
			case 'w': swiz |= (0x3 - i) & 0x3; break;
			}
		}
	} else {
		swiz = 0x0;
	}

	return swiz;
}
