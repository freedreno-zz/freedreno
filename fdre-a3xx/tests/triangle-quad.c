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

#include <stdlib.h>
#include <stdio.h>

#include "freedreno.h"
#include "redump.h"

int main(int argc, char **argv)
{
	struct fd_state *state;
	struct fd_surface *surface;
	struct fd_perfctrs ctrs;

	float vertices[] = {
			/* triangle */
			-0.8, +0.50, 0.0,
			-0.2, +0.50, 0.0,
			-0.5, -0.50, 0.0,
			/* quad */
			+0.2, -0.50, 0.0,
			+0.8, -0.50, 0.0,
			+0.2, +0.50, 0.0,
			+0.8, +0.50, 0.0,
	};

	float triangle_color[] = {
			0.0, 1.0, 0.0, 1.0
	};

	float quad_color[] = {
			1.0, 0.0, 0.0, 1.0
	};

#if 0
	const char *vertex_shader_source =
		"attribute vec4 aPosition;    \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_Position = aPosition; \n"
		"}                            \n";
	const char *fragment_shader_source =
		"precision highp float;       \n"
		"uniform vec4 uColor;         \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_FragColor = uColor;   \n"
		"}                            \n";
#else
	const char *vertex_shader_asm =
		"@attribute(r0.x)  aPosition                                      \n"
		"(sy)(ss)end                                                      \n";
	const char *fragment_shader_asm =
		"@uniform(hc0.x) uColor                                           \n"
		// XXX why not use a (rpt3)??
		"(sy)(ss)mov.f16f16 hr0.x, hc0.x                                  \n"
		"mov.f16f16 hr0.y, hc0.y                                          \n"
		"mov.f16f16 hr0.z, hc0.z                                          \n"
		"mov.f16f16 hr0.w, hc0.w                                          \n"
		"end                                                              \n";
#endif

	DEBUG_MSG("----------------------------------------------------------------");
	RD_START("fd-triangle-quad", "");

	state = fd_init();
	if (!state)
		return -1;

	surface = fd_surface_new(state, 256, 256);
	if (!surface)
		return -1;

	fd_make_current(state, surface);

	fd_vertex_shader_attach_asm(state, vertex_shader_asm);
	fd_fragment_shader_attach_asm(state, fragment_shader_asm);

	fd_link(state);

	fd_clear_color(state, (float[]){ 0.5, 0.5, 0.5, 1.0 });
	fd_clear(state, GL_COLOR_BUFFER_BIT);

	fd_attribute_pointer(state, "aPosition", VFMT_FLOAT_32_32_32, 7, vertices);

	fd_query_start(state);

	/* draw triangle: */
	fd_uniform_attach(state, "uColor", 4, 1, triangle_color);
	fd_draw_arrays(state, GL_TRIANGLES, 0, 3);

	/* draw quad: */
	fd_uniform_attach(state, "uColor", 4, 1, quad_color);
	fd_draw_arrays(state, GL_TRIANGLE_STRIP, 3, 4);

	fd_query_end(state);

	fd_swap_buffers(state);

	fd_flush(state);

	fd_query_read(state, &ctrs);
	fd_query_dump(&ctrs);

	fd_dump_bmp(surface, "triangle-quad.bmp");

	sleep(1);

	fd_fini(state);

	RD_END();

	return 0;
}
