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
	uint32_t width, height;

	static const GLfloat vertices[] = {
			-0.75f, +0.25f, +0.50f, // Quad #0
			-0.25f, +0.25f, +0.50f,
			-0.25f, +0.75f, +0.50f,
			-0.75f, +0.75f, +0.50f,
			+0.25f, +0.25f, +0.90f, // Quad #1
			+0.75f, +0.25f, +0.90f,
			+0.75f, +0.75f, +0.90f,
			+0.25f, +0.75f, +0.90f,
			-0.75f, -0.75f, +0.50f, // Quad #2
			-0.25f, -0.75f, +0.50f,
			-0.25f, -0.25f, +0.50f,
			-0.75f, -0.25f, +0.50f,
			+0.25f, -0.75f, +0.50f, // Quad #3
			+0.75f, -0.75f, +0.50f,
			+0.75f, -0.25f, +0.50f,
			+0.25f, -0.25f, +0.50f,
			-1.00f, -1.00f, +0.00f, // Big Quad
			+1.00f, -1.00f, +0.00f,
			+1.00f, +1.00f, +0.00f,
			-1.00f, +1.00f, +0.00f,
	};

	static const GLubyte indices[][6] = {
			{  0,  1,  2,  0,  2,  3 }, // Quad #0
			{  4,  5,  6,  4,  6,  7 }, // Quad #1
			{  8,  9, 10,  8, 10, 11 }, // Quad #2
			{ 12, 13, 14, 12, 14, 15 }, // Quad #3
			{ 16, 17, 18, 16, 18, 19 }, // Big Quad
	};

#define NumTests  4
	static const GLfloat colors[NumTests][4] = {
			{ 1.0f, 0.0f, 0.0f, 1.0f },
			{ 0.0f, 1.0f, 0.0f, 1.0f },
			{ 0.0f, 0.0f, 1.0f, 1.0f },
			{ 1.0f, 1.0f, 0.0f, 0.0f },
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
		"@attribute(R1)  aPosition                                        \n"
		"EXEC                                                             \n"
		"   (S)FETCH:   VERTEX   R1.xyz1 = R0.x FMT_32_32_32_FLOAT SIGNED \n"
		"                                      STRIDE(12) CONST(20, 0)    \n"
		"ALLOC POSITION SIZE(0x0)                                         \n"
		"EXEC                                                             \n"
		"      ALU:   MAXv   export62 = R1, R1   ; gl_Position            \n"
		"ALLOC PARAM/PIXEL SIZE(0x0)                                      \n"
		"EXEC_END                                                         \n"
		"NOP                                                              \n";
	const char *fragment_shader_asm =
		"@uniform(C0) uColor                                              \n"
		"ALLOC PARAM/PIXEL SIZE(0x0)                                      \n"
		"EXEC_END                                                         \n"
		"      ALU:    MAXv export0 = C0, C0    ; gl_FragColor            \n";
#endif
	GLint numStencilBits;
	GLuint stencilValues[NumTests] = {
			0x7, // Result of test 0
			0x0, // Result of test 1
			0x2, // Result of test 2
			0xff // Result of test 3.  We need to fill this value in a run-time
	};
	int i;

	DEBUG_MSG("----------------------------------------------------------------");
	RD_START("fd-stencil", "");

	state = fd_init();
	if (!state)
		return -1;

	surface = fd_surface_screen(state, &width, &height);
	if (!surface)
		return -1;

	fd_enable(state, GL_DEPTH_TEST);
	fd_enable(state, GL_STENCIL_TEST);

	/* this needs to come after enabling depth/stencil test as these
	 * effect bin sizes:
	 */
	fd_make_current(state, surface);

	fd_vertex_shader_attach_asm(state, vertex_shader_asm);
	fd_fragment_shader_attach_asm(state, fragment_shader_asm);

	fd_link(state);

	fd_clear_color(state, 0x00000000);
	fd_clear_stencil(state, 0x1);
	fd_clear_depth(state, 0.75);

	// Clear the color, depth, and stencil buffers.  At this
	//   point, the stencil buffer will be 0x1 for all pixels
	fd_clear(state, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	fd_attribute_pointer(state, "aPosition", 3, 20, vertices);

	fd_uniform_attach(state, "uColor", 4, 1, (GLfloat[]){
		0.0, 0.0, 0.0, 0.0,
	});

	// Test 0:
	//
	// Initialize upper-left region.  In this case, the
	//   stencil-buffer values will be replaced because the
	//   stencil test for the rendered pixels will fail the
	//   stencil test, which is
	//
	//        ref   mask   stencil  mask
	//      ( 0x7 & 0x3 ) < ( 0x1 & 0x7 )
	//
	//   The value in the stencil buffer for these pixels will
	//   be 0x7.
	//
	fd_stencil_func(state, GL_LESS, 0x7, 0x3);
	fd_stencil_op(state, GL_REPLACE, GL_DECR, GL_DECR);
	fd_draw_elements(state, GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, indices[0]);

	// Test 1:
	//
	// Initialize the upper-right region.  Here, we'll decrement
	//   the stencil-buffer values where the stencil test passes
	//   but the depth test fails.  The stencil test is
	//
	//        ref  mask    stencil  mask
	//      ( 0x3 & 0x3 ) > ( 0x1 & 0x3 )
	//
	//    but where the geometry fails the depth test.  The
	//    stencil values for these pixels will be 0x0.
	//
	fd_stencil_func(state, GL_GREATER, 0x3, 0x3);
	fd_stencil_op(state, GL_KEEP, GL_DECR, GL_KEEP);
	fd_draw_elements(state, GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, indices[1]);

	// Test 2:
	//
	// Initialize the lower-left region.  Here we'll increment
	//   (with saturation) the stencil value where both the
	//   stencil and depth tests pass.  The stencil test for
	//   these pixels will be
	//
	//        ref  mask     stencil  mask
	//      ( 0x1 & 0x3 ) == ( 0x1 & 0x3 )
	//
	//   The stencil values for these pixels will be 0x2.
	//
	fd_stencil_func(state, GL_EQUAL, 0x1, 0x3);
	fd_stencil_op(state, GL_KEEP, GL_INCR, GL_INCR);
	fd_draw_elements(state, GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, indices[2]);

	// Test 3:
	//
	// Finally, initialize the lower-right region.  We'll invert
	//   the stencil value where the stencil tests fails.  The
	//   stencil test for these pixels will be
	//
	//        ref   mask    stencil  mask
	//      ( 0x2 & 0x1 ) == ( 0x1 & 0x1 )
	//
	//   The stencil value here will be set to ~((2^s-1) & 0x1),
	//   (with the 0x1 being from the stencil clear value),
	//   where 's' is the number of bits in the stencil buffer
	//
	fd_stencil_func(state, GL_EQUAL, 0x2, 0x1);
	fd_stencil_op(state, GL_INVERT, GL_KEEP, GL_KEEP);
	fd_draw_elements(state, GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, indices[3]);

	// Since we don't know at compile time how many stencil bits are present,
	//   we'll query, and update the value correct value in the
	//   stencilValues arrays for the fourth tests.  We'll use this value
	//   later in rendering.
	numStencilBits = 8;

	stencilValues[3] = ~(((1 << numStencilBits) - 1) & 0x1) & 0xff;

	// Use the stencil buffer for controlling where rendering will
	//   occur.  We disable writing to the stencil buffer so we
	//   can test against them without modifying the values we
	//   generated.
	fd_stencil_mask(state, 0x0);

	for (i = 0; i < NumTests; i++) {
		fd_stencil_func(state, GL_EQUAL, stencilValues[i], 0xff);
		fd_uniform_attach(state, "uColor", 4, 1, colors[i]);
		fd_draw_elements(state, GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, indices[4]);
	}

	fd_swap_buffers(state);

	fd_flush(state);

	fd_dump_bmp(surface, "stencil.bmp");

	fd_fini(state);

	RD_END();

	return 0;
}
