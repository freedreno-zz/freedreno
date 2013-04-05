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
		"@attribute(R1)  aPosition                                       \n"
		"EXEC                                                            \n"
		"   (S)FETCH:  VERTEX  R1.xyz1 = R0.x FMT_32_32_32_FLOAT SIGNED  \n"
		"                                       STRIDE(12) CONST(20, 0)  \n"
		"ALLOC POSITION SIZE(0x0)                                        \n"
		"EXEC                                                            \n"
		"      ALU:    MAXv    export62 = R1, R1    ; gl_Position        \n"
		"ALLOC PARAM/PIXEL SIZE(0x0)                                     \n"
		"EXEC_END                                                        \n"
		"NOP                                                             \n";
	const char *fragment_shader_asm =
		"@uniform(C0) uColor                                             \n"
		"ALLOC PARAM/PIXEL SIZE(0x0)                                     \n"
		"EXEC_END                                                        \n"
		"      ALU:    MAXv    export0 = C0, C0    ; gl_FragColor        \n";
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

	fd_clear_color(state, 0xff505050);
	fd_clear(state, GL_COLOR_BUFFER_BIT);

	fd_attribute_pointer(state, "aPosition", 3, 7, vertices);

	/* draw triangle: */
	fd_uniform_attach(state, "uColor", 4, 1, triangle_color);
	fd_draw_arrays(state, GL_TRIANGLES, 0, 3);

	/* draw quad: */
	fd_uniform_attach(state, "uColor", 4, 1, quad_color);
	fd_draw_arrays(state, GL_TRIANGLE_STRIP, 3, 4);

	fd_swap_buffers(state);

	fd_flush(state);

	fd_dump_bmp(surface, "triangle-quad.bmp");

	fd_fini(state);

	RD_END();

	return 0;
}
