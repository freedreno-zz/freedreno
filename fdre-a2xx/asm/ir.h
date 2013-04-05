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

#include "util.h"

/* low level intermediate representation of an adreno shader program */

#include "parser.h"  /* use parser tokens for instr/cf types */

struct ir_shader;

struct ir_shader * fd_asm_parse(const char *src);

struct ir_shader_info {
	int8_t   max_reg;   /* highest GPR # used by shader */
	uint8_t  max_input_reg;
	uint64_t regs_written;
};

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
			unsigned const_idx;
			/* maybe vertex fetch specific: */
			unsigned const_idx_sel;
			enum a2xx_sq_surfaceformat fmt;
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

/* somewhat arbitrary limits.. */
#define MAX_ATTRIBUTES 32
#define MAX_CONSTS     32
#define MAX_SAMPLERS   32
#define MAX_UNIFORMS   32
#define MAX_VARYINGS   32

struct ir_attribute {
	const char *name;
	int rstart;         /* first register */
	int num;            /* number of registers */
};

struct ir_const {
	float val[4];
	int cstart;         /* first const register */
};

struct ir_sampler {
	const char *name;
	int idx;
};

struct ir_uniform {
	const char *name;
	int cstart;         /* first const register */
	int num;            /* number of const registers */
};

struct ir_varying {
	const char *name;
	int rstart;         /* first register */
	int num;            /* number of registers */
};

struct ir_shader {
	unsigned cfs_count;
	struct ir_cf *cfs[64];
	uint32_t heap[100 * 4096];
	unsigned heap_idx;

	/* @ headers: */
	uint32_t attributes_count;
	struct ir_attribute *attributes[MAX_ATTRIBUTES];

	uint32_t consts_count;
	struct ir_const *consts[MAX_CONSTS];

	uint32_t samplers_count;
	struct ir_sampler *samplers[MAX_SAMPLERS];

	uint32_t uniforms_count;
	struct ir_uniform *uniforms[MAX_UNIFORMS];

	uint32_t varyings_count;
	struct ir_varying *varyings[MAX_VARYINGS];

};

struct ir_shader * ir_shader_create(void);
void ir_shader_destroy(struct ir_shader *shader);
int ir_shader_assemble(struct ir_shader *shader,
		uint32_t *dwords, int sizedwords,
		struct ir_shader_info *info);

struct ir_attribute * ir_attribute_create(struct ir_shader *shader,
		int rstart, int num, const char *name);
struct ir_const * ir_const_create(struct ir_shader *shader,
		int cstart, float v0, float v1, float v2, float v3);
struct ir_sampler * ir_sampler_create(struct ir_shader *shader,
		int idx, const char *name);
struct ir_uniform * ir_uniform_create(struct ir_shader *shader,
		int cstart, int num, const char *name);
struct ir_varying * ir_varying_create(struct ir_shader *shader,
		int rstart, int num, const char *name);

struct ir_cf * ir_cf_create(struct ir_shader *shader, int cf_type);

struct ir_instruction * ir_instr_create(struct ir_cf *cf, int instr_type);

struct ir_register * ir_reg_create(struct ir_instruction *instr,
		int num, const char *swizzle, int flags);

#endif /* IR_H_ */
