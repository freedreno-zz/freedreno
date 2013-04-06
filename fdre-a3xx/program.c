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
#include "ir-a3xx.h"
#include "ring.h"
#include "util.h"


struct fd_shader {
	uint32_t bin[256];
	uint32_t sizedwords;
	struct ir3_shader_info info;
	struct ir3_shader *ir;
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
		ir3_shader_destroy(shader->ir);

	memset(shader, 0, sizeof(*shader));

	shader->ir = fd_asm_parse(src);
	if (!shader->ir) {
		ERROR_MSG("parse failed");
		return -1;
	}
	sizedwords = ir3_shader_assemble(shader->ir, shader->bin,
			ARRAY_SIZE(shader->bin), &shader->info);
	if (sizedwords <= 0) {
		ERROR_MSG("assembler failed");
		return -1;
	}
	shader->sizedwords = sizedwords;
	return 0;
}

struct ir3_attribute ** fd_program_attributes(struct fd_program *program,
		enum fd_shader_type type, int *cnt)
{
	struct fd_shader *shader = get_shader(program, type);
	*cnt = shader->ir->attributes_count;
	return shader->ir->attributes;
}

struct ir3_const ** fd_program_consts(struct fd_program *program,
		enum fd_shader_type type, int *cnt)
{
	struct fd_shader *shader = get_shader(program, type);
	*cnt = shader->ir->consts_count;
	return shader->ir->consts;
}

struct ir3_sampler ** fd_program_samplers(struct fd_program *program,
		enum fd_shader_type type, int *cnt)
{
	struct fd_shader *shader = get_shader(program, type);
	*cnt = shader->ir->samplers_count;
	return shader->ir->samplers;
}

struct ir3_uniform ** fd_program_uniforms(struct fd_program *program,
		enum fd_shader_type type, int *cnt)
{
	struct fd_shader *shader = get_shader(program, type);
	*cnt = shader->ir->uniforms_count;
	return shader->ir->uniforms;
}

int fd_program_emit_shader(struct fd_program *program,
		enum fd_shader_type type, struct fd_ringbuffer *ring)
{
	return -1;
}
