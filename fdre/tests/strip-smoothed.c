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
			-0.7, +0.7, 0.0,
			-0.7, +0.2, 0.0,
			+0.0, -0.2, 0.0,
			+0.0, -0.7, 0.0,
			+0.7, +0.7, 0.0,
			+0.7, +0.2, 0.0,
	};

	float colors[] = {
			1.0, 0.0, 0.0, 1.0,
			1.0, 1.0, 0.0, 1.0,
			1.0, 0.0, 1.0, 1.0,
			0.0, 1.0, 0.0, 1.0,
			0.0, 0.0, 1.0, 1.0,
			0.0, 1.0, 1.0, 1.0,
	};

#if 0
	const char *vertex_shader_source =
		"attribute vec4 aPosition;    \n"
		"attribute vec4 aColor;       \n"
		"                             \n"
		"varying vec4 vColor;         \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    vColor = aColor;         \n"
		"    gl_Position = aPosition; \n"
		"}                            \n";
	const char *fragment_shader_source =
		"precision mediump float;     \n"
		"                             \n"
		"varying vec4 vColor;         \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_FragColor = vColor;   \n"
		"}                            \n";
#else
	const char *vertex_shader_asm =
		"@varying(R0)    vColor                                              \n"
		"@attribute(R2)  aPosition                                           \n"
		"@attribute(R1)  aColor                                              \n"
		"EXEC                                                                \n"
		"      FETCH:   VERTEX	R1.xyzw = R0.x FMT_32_32_32_32_FLOAT SIGNED  \n"
		"                                       STRIDE(16) CONST(20, 1)      \n"
		"      FETCH:   VERTEX	R2.xyz1 = R0.x FMT_32_32_32_FLOAT SIGNED     \n"
		"                                       STRIDE(12) CONST(20, 0)      \n"
		"ALLOC POSITION SIZE(0x0)                                            \n"
		"EXEC                                                                \n"
		"      ALU:     MAXv export62 = R2, R2  ; gl_Position                \n"
		"ALLOC PARAM/PIXEL SIZE(0x0)                                         \n"
		"EXEC_END                                                            \n"
		"      ALU:     MAXv export0 = R1, R1                                \n"
		"NOP                                                                 \n";
	const char *fragment_shader_asm =
		"@varying(R0)    vColor                                              \n"
		"ALLOC PARAM/PIXEL SIZE(0x0)                                         \n"
		"EXEC_END                                                            \n"
		"      ALU:    MAXv export0 = R0, R0    ; gl_FragColor               \n";
#endif

	DEBUG_MSG("----------------------------------------------------------------");
	RD_START("fd-triangle-smoothed", "");

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

	fd_clear_color(state, 0xff000000);
	fd_clear(state, GL_COLOR_BUFFER_BIT);

	fd_attribute_pointer(state, "aPosition", 3, 6, vertices);
	fd_attribute_pointer(state, "aColor", 4, 6, colors);

	fd_draw_arrays(state, GL_TRIANGLE_STRIP, 0, 6);

	fd_swap_buffers(state);

	fd_flush(state);

	fd_dump_bmp(surface, "strip-smoothed.bmp");

	fd_fini(state);

	RD_END();

	return 0;
}
