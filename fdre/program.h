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

#ifndef PROGRAM_H_
#define PROGRAM_H_

#include "ring.h"

struct fd_program;

enum fd_shader_type {
	FD_SHADER_VERTEX   = 0,
	FD_SHADER_FRAGMENT = 1,
};

struct fd_program * fd_program_new(void);

int fd_program_attach_asm(struct fd_program *program,
		enum fd_shader_type type, const char *src);

struct ir_attribute;
struct ir_const;
struct ir_sampler;
struct ir_uniform;

struct ir_attribute ** fd_program_attributes(struct fd_program *program,
		enum fd_shader_type type, int *cnt);
struct ir_const ** fd_program_consts(struct fd_program *program,
		enum fd_shader_type type, int *cnt);
struct ir_sampler ** fd_program_samplers(struct fd_program *program,
		enum fd_shader_type type, int *cnt);
struct ir_uniform ** fd_program_uniforms(struct fd_program *program,
		enum fd_shader_type type, int *cnt);

int fd_program_emit_shader(struct fd_program *program,
		enum fd_shader_type type, struct fd_ringbuffer *ring);

int fd_program_emit_sq_program_cntl(struct fd_program *program,
		struct fd_ringbuffer *ring);

#endif /* PROGRAM_H_ */
