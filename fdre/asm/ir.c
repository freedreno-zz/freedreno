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
#include <assert.h>

#include "util.h"

/* simple allocator to carve allocations out of an up-front allocated heap,
 * so that we can free everything easily in one shot.
 */
static void * ir_alloc(struct ir_shader *shader, int sz)
{
	void *ptr = &shader->heap[shader->heap_idx];
	shader->heap_idx += ALIGN(sz, 4);
	return ptr;
}

struct ir_shader * ir_shader_create(void)
{
	INFO_MSG("");
	return calloc(1, sizeof(struct ir_shader));
}

void ir_shader_destroy(struct ir_shader *shader)
{
	INFO_MSG("");
	free(shader);
}

int ir_shader_assemble(struct ir_shader *shader,
		uint32_t *dwords, int sizedwords)
{
	return -1; // TODO
}


struct ir_cf * ir_cf_create(struct ir_shader *shader, int cf_type)
{
	struct ir_cf *cf = ir_alloc(shader, sizeof(struct ir_cf));
	INFO_MSG("%d", cf_type);
	cf->shader = shader;
	cf->cf_type = cf_type;
	assert(shader->cfs_count < ARRAY_SIZE(shader->cfs));
	shader->cfs[shader->cfs_count++] = cf;
	return cf;
}

struct ir_instruction * ir_instr_create(struct ir_cf *cf, int instr_type)
{
	struct ir_instruction *instr =
			ir_alloc(cf->shader, sizeof(struct ir_instruction));
	INFO_MSG("%d", instr_type);
	instr->shader = cf->shader;
	instr->instr_type = instr_type;
	assert(cf->exec.instrs_count < ARRAY_SIZE(cf->exec.instrs));
	cf->exec.instrs[cf->exec.instrs_count++] = instr;
	return instr;
}

struct ir_register * ir_reg_create(struct ir_instruction *instr,
		int num, const char *swizzle, int flags)
{
	struct ir_register *reg =
			ir_alloc(instr->shader, sizeof(struct ir_register));
	INFO_MSG("%x, %d, %s", flags, num, swizzle);
	reg->flags = flags;
	reg->num = num;
	reg->swizzle = swizzle;
	assert(instr->regs_count < ARRAY_SIZE(instr->regs));
	instr->regs[instr->regs_count++] = reg;
	return reg;
}
