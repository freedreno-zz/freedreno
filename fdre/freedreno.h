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

struct fd_state;
struct fd_surface;

struct fd_state * fd_init(void);
void fd_fini(struct fd_state *state);

int fd_vertex_shader_attach_bin(struct fd_state *state,
		const void *bin, uint32_t sz);
int fd_fragment_shader_attach_bin(struct fd_state *state,
		const void *bin, uint32_t sz);
int fd_vertex_shader_attach_asm(struct fd_state *state,
		const char *src, uint32_t sz);
int fd_fragment_shader_attach_asm(struct fd_state *state,
		const char *src, uint32_t sz);
int fd_link(struct fd_state *state);

int fd_attribute_pointer(struct fd_state *state, const char *name,
		uint32_t size, uint32_t count, const void *data);
int fd_uniform_attach(struct fd_state *state, const char *name,
		uint32_t size, uint32_t count, const void *data);

int fd_clear(struct fd_state *state, uint32_t color);
int fd_cull(struct fd_state *state, GLenum mode);
int fd_depth_func(struct fd_state *state, GLenum depth_func);
int fd_enable(struct fd_state *state, GLenum cap);
int fd_disable(struct fd_state *state, GLenum cap);
int fd_draw_arrays(struct fd_state *state, GLenum mode,
		uint32_t start, uint32_t count);
int fd_swap_buffers(struct fd_state *state);
int fd_flush(struct fd_state *state);

struct fd_surface * fd_surface_new(struct fd_state *state,
		uint32_t width, uint32_t height);
void fd_surface_del(struct fd_state *state, struct fd_surface *surface);

void fd_make_current(struct fd_state *state,
		struct fd_surface *surface);
int fd_dump_bmp(struct fd_surface *surface, const char *filename);

#endif /* FREEDRENO_H_ */
