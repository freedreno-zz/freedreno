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
	FD_SHADER_COMPUTE  = 2,
};

struct fd_state;

struct fd_program * fd_program_new(struct fd_state *state);

int fd_program_attach_asm(struct fd_program *program,
		enum fd_shader_type type, const char *src);

struct ir3_sampler;

struct ir3_sampler ** fd_program_samplers(struct fd_program *program,
		enum fd_shader_type type, int *cnt);
uint32_t fd_program_outloc(struct fd_program *program);
void fd_program_emit_state(struct fd_program *program, uint32_t first,
		struct fd_parameters *uniforms, struct fd_parameters *attr,
		struct fd_parameters *bufs, struct fd_ringbuffer *ring);
void fd_program_emit_compute_state(struct fd_program *program,
		struct fd_parameters *uniforms, struct fd_parameters *attr,
		struct fd_parameters *bufs, struct fd_ringbuffer *ring);

#endif /* PROGRAM_H_ */
