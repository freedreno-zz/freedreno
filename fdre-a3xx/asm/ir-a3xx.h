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

#ifndef IR3_H_
#define IR3_H_

#include <stdint.h>

#include "util.h"
#include "instr-a3xx.h"

/* low level intermediate representation of an adreno shader program */

#include "parser.h"  /* use parser tokens for instr/cf types */

struct ir3_shader;

struct ir3_shader * fd_asm_parse(const char *src);

struct ir3_shader_info {
	int8_t   max_reg;   /* highest GPR # used by shader */
	int8_t   max_half_reg;
	int8_t   max_const;
};

struct ir3_register {
	enum {
		IR3_REG_CONST  = 0x001,
		IR3_REG_IMMED  = 0x002,
		IR3_REG_HALF   = 0x004,
		IR3_REG_RELATIV= 0x008,
		IR3_REG_R      = 0x010,
		IR3_REG_NEGATE = 0x020,
		IR3_REG_ABS    = 0x040,
		IR3_REG_EVEN   = 0x080,
		IR3_REG_POS_INF= 0x100,
		IR3_REG_EI     = 0x200,
	} flags;
	union {
		/* normal registers: */
		struct {
			/* the component is in the low two bits of the reg #, so
			 * rN.x becomes: (n << 2) | x
			 */
			int num;
			int wrmask;
		};
		/* immediate: */
		int     iim_val;
		float   fim_val;
		/* relative: */
		int offset;
	};
};

struct ir3_instruction {
	struct ir3_shader *shader;
	int category;
	opc_t opc;
	enum {
		IR3_INSTR_SY    = 0x001,
		IR3_INSTR_SS    = 0x002,
		IR3_INSTR_JP    = 0x004,
		IR3_INSTR_UL    = 0x008,
		IR3_INSTR_3D    = 0x010,
		IR3_INSTR_A     = 0x020,
		IR3_INSTR_O     = 0x040,
		IR3_INSTR_P     = 0x080,
		IR3_INSTR_S     = 0x100,
		IR3_INSTR_S2EN  = 0x200,
		IR3_INSTR_G     = 0x400,
	} flags;
	int repeat;
	unsigned regs_count;
	struct ir3_register *regs[4];
	union {
		struct {
			char inv;
			char comp;
			int  immed;
		} cat0;
		struct {
			type_t src_type, dst_type;
		} cat1;
		struct {
			enum {
				IR3_COND_LT = 0,
				IR3_COND_LE = 1,
				IR3_COND_GT = 2,
				IR3_COND_GE = 3,
				IR3_COND_EQ = 4,
				IR3_COND_NE = 5,
			} condition;
		} cat2;
		struct {
			unsigned samp, tex;
			type_t type;
		} cat5;
		struct {
			type_t type;
			int src_offset;
			int dst_offset;
			int iim_val;
		} cat6;
	};
	int line;
};

/* somewhat arbitrary limits.. */
#define MAX_ATTRIBUTES 16
#define MAX_CONSTS     32
#define MAX_SAMPLERS   32
#define MAX_UNIFORMS   32
#define MAX_VARYINGS   16
#define MAX_BUFS       16
#define MAX_OUTS       2

struct ir3_attribute {
	const char *name;
	struct ir3_register *rstart;  /* first register */
	int num;                      /* number of registers */
};

struct ir3_const {
	uint32_t val[4];
	struct ir3_register *cstart;  /* first const register */
};

struct ir3_sampler {
	const char *name;
	int idx;
};

struct ir3_uniform {
	const char *name;
	struct ir3_register *cstart;  /* first const register */
	int num;                      /* number of const registers */
};

struct ir3_varying {
	const char *name;
	struct ir3_register *rstart;  /* first register */
	int num;                      /* number of registers */
};

struct ir3_buf {
	const char *name;
	struct ir3_register *cstart;  /* first const register */
};

struct ir3_out {
	const char *name;
	struct ir3_register *rstart;  /* first register */
	int num;                      /* number of registers */
};

/* this is just large to cope w/ the large test *.asm: */
#define MAX_INSTRS 10240

struct ir3_shader {
	unsigned instrs_count;
	struct ir3_instruction *instrs[MAX_INSTRS];
	uint32_t heap[128 * MAX_INSTRS];
	unsigned heap_idx;

	/* @ headers: */
	uint32_t attributes_count;
	struct ir3_attribute *attributes[MAX_ATTRIBUTES];

	uint32_t consts_count;
	struct ir3_const *consts[MAX_CONSTS];

	uint32_t samplers_count;
	struct ir3_sampler *samplers[MAX_SAMPLERS];

	uint32_t uniforms_count;
	struct ir3_uniform *uniforms[MAX_UNIFORMS];

	uint32_t varyings_count;
	struct ir3_varying *varyings[MAX_VARYINGS];

	uint32_t bufs_count;
	struct ir3_buf *bufs[MAX_BUFS];

	/* To specify the position of gl_Postion (default r0.x), gl_PointSize
	 * (default r63.x), and gl_FragColor (default r0.x):
	 */
	uint32_t outs_count;
	struct ir3_out *outs[MAX_OUTS];
};

struct ir3_shader * ir3_shader_create(void);
void ir3_shader_destroy(struct ir3_shader *shader);
int ir3_shader_assemble(struct ir3_shader *shader,
		uint32_t *dwords, uint32_t sizedwords,
		struct ir3_shader_info *info);

struct ir3_attribute * ir3_attribute_create(struct ir3_shader *shader,
		int rstart, int num, const char *name);
struct ir3_const * ir3_const_create(struct ir3_shader *shader,
		int cstart, uint32_t v0, uint32_t v1, uint32_t v2, uint32_t v3);
struct ir3_sampler * ir3_sampler_create(struct ir3_shader *shader,
		int idx, const char *name);
struct ir3_uniform * ir3_uniform_create(struct ir3_shader *shader,
		int cstart, int num, const char *name);
struct ir3_varying * ir3_varying_create(struct ir3_shader *shader,
		int rstart, int num, const char *name);
struct ir3_buf * ir3_buf_create(struct ir3_shader *shader,
		int cstart, const char *name);
struct ir3_out * ir3_out_create(struct ir3_shader *shader,
		int rstart, int num, const char *name);

struct ir3_instruction * ir3_instr_create(struct ir3_shader *shader, int category, opc_t opc);

struct ir3_register * ir3_reg_create(struct ir3_instruction *instr,
		int num, int flags);

#endif /* IR3_H_ */
