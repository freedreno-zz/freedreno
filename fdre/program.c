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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "program.h"
#include "ir.h"
#include "ring.h"
#include "util.h"


struct fd_shader {
	uint32_t bin[256];
	uint32_t sizedwords;
	struct ir_shader_info info;
	struct ir_shader *ir;
};

struct fd_program {
	struct fd_shader vertex_shader, fragment_shader;
};

static struct fd_shader *get_shader(struct fd_program *program,
		enum fd_shader_type type)
{
	switch (type) {
	case FD_SHADER_VERTEX:   return &program->vertex_shader;
	case FD_SHADER_FRAGMENT: return &program->fragment_shader;
	}
	assert(0);
	return NULL;
}

struct fd_program * fd_program_new(void)
{
	return calloc(1, sizeof(struct fd_program));
}

int fd_program_attach_asm(struct fd_program *program,
		enum fd_shader_type type, const char *src)
{
	struct fd_shader *shader = get_shader(program, type);
	int sizedwords;

	if (shader->ir)
		ir_shader_destroy(shader->ir);

	memset(shader, 0, sizeof(*shader));

	shader->ir = fd_asm_parse(src);
	if (!shader->ir) {
		ERROR_MSG("parse failed");
		return -1;
	}
	sizedwords = ir_shader_assemble(shader->ir, shader->bin,
			ARRAY_SIZE(shader->bin), &shader->info);
	if (sizedwords <= 0) {
		ERROR_MSG("assembler failed");
		return -1;
	}
	shader->sizedwords = sizedwords;
	return 0;
}

struct ir_attribute ** fd_program_attributes(struct fd_program *program,
		enum fd_shader_type type, int *cnt)
{
	struct fd_shader *shader = get_shader(program, type);
	*cnt = shader->ir->attributes_count;
	return shader->ir->attributes;
}

struct ir_const ** fd_program_consts(struct fd_program *program,
		enum fd_shader_type type, int *cnt)
{
	struct fd_shader *shader = get_shader(program, type);
	*cnt = shader->ir->consts_count;
	return shader->ir->consts;
}

struct ir_sampler ** fd_program_samplers(struct fd_program *program,
		enum fd_shader_type type, int *cnt)
{
	struct fd_shader *shader = get_shader(program, type);
	*cnt = shader->ir->samplers_count;
	return shader->ir->samplers;
}

struct ir_uniform ** fd_program_uniforms(struct fd_program *program,
		enum fd_shader_type type, int *cnt)
{
	struct fd_shader *shader = get_shader(program, type);
	*cnt = shader->ir->uniforms_count;
	return shader->ir->uniforms;
}

int fd_program_emit_shader(struct fd_program *program,
		enum fd_shader_type type, struct fd_ringbuffer *ring)
{
	struct fd_shader *shader = get_shader(program, type);
	uint32_t i;

	OUT_PKT3(ring, CP_IM_LOAD_IMMEDIATE, 2 + shader->sizedwords);
	OUT_RING(ring, type);
	OUT_RING(ring, shader->sizedwords);
	for (i = 0; i < shader->sizedwords; i++)
		OUT_RING(ring, shader->bin[i]);

	return 0;
}

int fd_program_emit_sq_program_cntl(struct fd_program *program,
		struct fd_ringbuffer *ring)
{
	struct ir_shader_info *vsi = &get_shader(program, FD_SHADER_VERTEX)->info;
	struct ir_shader_info *fsi = &get_shader(program, FD_SHADER_FRAGMENT)->info;
	uint8_t vs_gprs = (vsi->max_reg < 0) ? 0x80 : vsi->max_reg;
	uint8_t fs_gprs = (fsi->max_reg < 0) ? 0x80 : fsi->max_reg;

	OUT_PKT3(ring, CP_SET_CONSTANT, 2);
	OUT_RING(ring, CP_REG(REG_A2XX_SQ_PROGRAM_CNTL));
	OUT_RING(ring, A2XX_SQ_PROGRAM_CNTL_PS_EXPORT_MODE(POSITION_2_VECTORS_SPRITE) |
			A2XX_SQ_PROGRAM_CNTL_VS_RESOURCE |
			A2XX_SQ_PROGRAM_CNTL_PS_RESOURCE |
			A2XX_SQ_PROGRAM_CNTL_VS_EXPORT_COUNT(fsi->max_input_reg) |
			A2XX_SQ_PROGRAM_CNTL_PS_REGS(fs_gprs) |
			A2XX_SQ_PROGRAM_CNTL_VS_REGS(vs_gprs));

	return 0;
}
