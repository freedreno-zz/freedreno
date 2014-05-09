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

#ifndef FREEDRENO_H_
#define FREEDRENO_H_

#include <GLES2/gl2.h>

#include "util.h"
#include "program.h"

struct fd_state;
struct fd_surface;
struct fd_bo;

struct fd_state * fd_init(void);
void fd_fini(struct fd_state *state);

int fd_vertex_shader_attach_asm(struct fd_state *state, const char *src);
int fd_fragment_shader_attach_asm(struct fd_state *state, const char *src);
int fd_link(struct fd_state *state);
int fd_set_program(struct fd_state *state, struct fd_program *program);

struct fd_bo * fd_attribute_bo_new(struct fd_state *state,
		uint32_t size, const void *data);
int fd_attribute_bo(struct fd_state *state, const char *name,
		enum a3xx_vtx_fmt fmt, struct fd_bo * bo);
int fd_attribute_pointer(struct fd_state *state, const char *name,
		enum a3xx_vtx_fmt fmt, uint32_t count, const void *data);
int fd_uniform_attach(struct fd_state *state, const char *name,
		uint32_t size, uint32_t count, const void *data);
int fd_set_texture(struct fd_state *state, const char *name,
		struct fd_surface *tex);
int fd_set_buf(struct fd_state *state, const char *name, struct fd_bo *bo);

void fd_clear_color(struct fd_state *state, float color[4]);
void fd_clear_stencil(struct fd_state *state, uint32_t s);
void fd_clear_depth(struct fd_state *state, float depth);
int fd_clear(struct fd_state *state, GLbitfield mask);
int fd_cull(struct fd_state *state, GLenum mode);
int fd_depth_func(struct fd_state *state, GLenum depth_func);
int fd_enable(struct fd_state *state, GLenum cap);
int fd_disable(struct fd_state *state, GLenum cap);
int fd_blend_func(struct fd_state *state, GLenum sfactor, GLenum dfactor);
int fd_stencil_func(struct fd_state *state, GLenum func,
		GLint ref, GLuint mask);
int fd_stencil_op(struct fd_state *state, GLenum sfail,
		GLenum zfail, GLenum zpass);
int fd_stencil_mask(struct fd_state *state, GLuint mask);
int fd_tex_param(struct fd_state *state, GLenum name, GLint param);

int fd_draw_elements(struct fd_state *state, GLenum mode, GLsizei count,
		GLenum type, const GLvoid* indices);
int fd_draw_arrays(struct fd_state *state, GLenum mode,
		GLint first, GLsizei count);
int fd_run_compute(struct fd_state *state, uint32_t workdim,
		uint32_t *globaloff, uint32_t *globalsize, uint32_t *localsize);

int fd_swap_buffers(struct fd_state *state);
int fd_flush(struct fd_state *state);

struct fd_surface * fd_surface_screen(struct fd_state *state,
		uint32_t *width, uint32_t *height);
struct fd_surface * fd_surface_new(struct fd_state *state,
		uint32_t width, uint32_t height);
struct fd_surface * fd_surface_new_fmt(struct fd_state *state,
		uint32_t width, uint32_t height, enum a3xx_color_fmt color_format);
void fd_surface_del(struct fd_state *state, struct fd_surface *surface);
void fd_surface_upload(struct fd_surface *surface, const void *data);

void fd_make_current(struct fd_state *state,
		struct fd_surface *surface);
int fd_dump_hex(struct fd_surface *surface);
int fd_dump_hex_bo(struct fd_bo *bo, bool flt);
int fd_dump_bmp(struct fd_surface *surface, const char *filename);

struct fd_perfctrs {
	union {
		struct {
			/* I believe these are all 64bit: */
			uint64_t ctr0;
			uint64_t ctr1;
			uint64_t ctr2;
			uint64_t ctr3;
			uint64_t ctr4;
			uint64_t ctr5;
			uint64_t ctr6;
			uint64_t ctr7;
			uint64_t ctr8;
			uint64_t ctr9;
			uint64_t ctrA;
			uint64_t ctrB;
			uint64_t ctrC;
			uint64_t ctrD;
			uint64_t ctrE;
			uint64_t ctrF;
		};
		uint64_t ctr[16];
	};
};

int fd_query_start(struct fd_state *state);
int fd_query_end(struct fd_state *state);
int fd_query_read(struct fd_state *state, struct fd_perfctrs *ctrs);
void fd_query_dump(struct fd_perfctrs *ctrs);

#endif /* FREEDRENO_H_ */
