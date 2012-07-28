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

#ifndef IR_H_
#define IR_H_

#include <stdint.h>

/* low level intermediate representation of an adreno shader program */

#include "parser.h"  /* use parser tokens for instr/cf types */

struct ir_shader;

struct ir_register {
	enum {
		IR_REG_CONST  = 0x1,
		IR_REG_EXPORT = 0x2,
		IR_REG_NEGATE = 0x4,
		IR_REG_ABS    = 0x8,
	} flags;
	int num;
	const char *swizzle;
};

struct ir_instruction {
	struct ir_shader *shader;
	int instr_type;     /* T_FETCH or T_ALU */
	int sync;
	unsigned regs_count;
	struct ir_register *regs[5];
	union {
		/* FETCH specific: */
		struct {
			int opc;
			int constant;
			/* maybe vertex fetch specific: */
			int type;   /* T_GL_FLOAT, T_GL_SHORT, etc */
			int sign;   /* T_SIGNED or T_UNSIGNED */
			uint32_t stride;
		} fetch;
		/* ALU specific: */
		struct {
			int vector_opc;
			int scalar_opc;
		} alu;
	};
};

struct ir_cf {
	struct ir_shader *shader;
	int cf_type;

	union {
		/* EXEC/EXEC_END specific: */
		struct {
			unsigned instrs_count;
			struct ir_instruction *instrs[0xf];
			uint32_t addr, cnt, sequence;
		} exec;
		/* ALLOC specific: */
		struct {
			int type;   /* T_COORD or T_PARAM_PIXEL */
			int size;
		} alloc;
	};
};

struct ir_shader {
	unsigned cfs_count;
	struct ir_cf *cfs[64];
	uint32_t heap[100 * 4096];
	unsigned heap_idx;
};

struct ir_shader * ir_shader_create(void);
void ir_shader_destroy(struct ir_shader *shader);
int ir_shader_assemble(struct ir_shader *shader,
		uint32_t *dwords, int sizedwords);

struct ir_cf * ir_cf_create(struct ir_shader *shader, int cf_type);

struct ir_instruction * ir_instr_create(struct ir_cf *cf, int instr_type);

struct ir_register * ir_reg_create(struct ir_instruction *instr,
		int num, const char *swizzle, int flags);

#endif /* IR_H_ */
