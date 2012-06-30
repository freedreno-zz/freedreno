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
#include "esUtil.h"

int main(int argc, char **argv)
{
	struct fd_state *state;
	struct fd_surface *surface;

	GLfloat vVertices[] = {
	  // front
	  -1.0f, -1.0f, +1.0f, // point blue
	  +1.0f, -1.0f, +1.0f, // point magenta
	  -1.0f, +1.0f, +1.0f, // point cyan
	  +1.0f, +1.0f, +1.0f, // point white
	  // back
	  +1.0f, -1.0f, -1.0f, // point red
	  -1.0f, -1.0f, -1.0f, // point black
	  +1.0f, +1.0f, -1.0f, // point yellow
	  -1.0f, +1.0f, -1.0f, // point green
	  // right
	  +1.0f, -1.0f, +1.0f, // point magenta
	  +1.0f, -1.0f, -1.0f, // point red
	  +1.0f, +1.0f, +1.0f, // point white
	  +1.0f, +1.0f, -1.0f, // point yellow
	  // left
	  -1.0f, -1.0f, -1.0f, // point black
	  -1.0f, -1.0f, +1.0f, // point blue
	  -1.0f, +1.0f, -1.0f, // point green
	  -1.0f, +1.0f, +1.0f, // point cyan
	  // top
	  -1.0f, +1.0f, +1.0f, // point cyan
	  +1.0f, +1.0f, +1.0f, // point white
	  -1.0f, +1.0f, -1.0f, // point green
	  +1.0f, +1.0f, -1.0f, // point yellow
	  // bottom
	  -1.0f, -1.0f, -1.0f, // point black
	  +1.0f, -1.0f, -1.0f, // point red
	  -1.0f, -1.0f, +1.0f, // point blue
	  +1.0f, -1.0f, +1.0f  // point magenta
	};

	GLfloat vColors[] = {
			// front
			0.0f,  0.0f,  1.0f, // blue
			1.0f,  0.0f,  1.0f, // magenta
			0.0f,  1.0f,  1.0f, // cyan
			1.0f,  1.0f,  1.0f, // white
			// back
			1.0f,  0.0f,  0.0f, // red
			0.0f,  0.0f,  0.0f, // black
			1.0f,  1.0f,  0.0f, // yellow
			0.0f,  1.0f,  0.0f, // green
			// right
			1.0f,  0.0f,  1.0f, // magenta
			1.0f,  0.0f,  0.0f, // red
			1.0f,  1.0f,  1.0f, // white
			1.0f,  1.0f,  0.0f, // yellow
			// left
			0.0f,  0.0f,  0.0f, // black
			0.0f,  0.0f,  1.0f, // blue
			0.0f,  1.0f,  0.0f, // green
			0.0f,  1.0f,  1.0f, // cyan
			// top
			0.0f,  1.0f,  1.0f, // cyan
			1.0f,  1.0f,  1.0f, // white
			0.0f,  1.0f,  0.0f, // green
			1.0f,  1.0f,  0.0f, // yellow
			// bottom
			0.0f,  0.0f,  0.0f, // black
			1.0f,  0.0f,  0.0f, // red
			0.0f,  0.0f,  1.0f, // blue
			1.0f,  0.0f,  1.0f  // magenta
	};

	GLfloat vNormals[] = {
			// front
			+0.0f, +0.0f, +1.0f, // forward
			+0.0f, +0.0f, +1.0f, // forward
			+0.0f, +0.0f, +1.0f, // forward
			+0.0f, +0.0f, +1.0f, // forward
			// back
			+0.0f, +0.0f, -1.0f, // backbard
			+0.0f, +0.0f, -1.0f, // backbard
			+0.0f, +0.0f, -1.0f, // backbard
			+0.0f, +0.0f, -1.0f, // backbard
			// right
			+1.0f, +0.0f, +0.0f, // right
			+1.0f, +0.0f, +0.0f, // right
			+1.0f, +0.0f, +0.0f, // right
			+1.0f, +0.0f, +0.0f, // right
			// left
			-1.0f, +0.0f, +0.0f, // left
			-1.0f, +0.0f, +0.0f, // left
			-1.0f, +0.0f, +0.0f, // left
			-1.0f, +0.0f, +0.0f, // left
			// top
			+0.0f, +1.0f, +0.0f, // up
			+0.0f, +1.0f, +0.0f, // up
			+0.0f, +1.0f, +0.0f, // up
			+0.0f, +1.0f, +0.0f, // up
			// bottom
			+0.0f, -1.0f, +0.0f, // down
			+0.0f, -1.0f, +0.0f, // down
			+0.0f, -1.0f, +0.0f, // down
			+0.0f, -1.0f, +0.0f  // down
	};

#if 0
	const char *vertex_shader_source =
		"uniform mat4 modelviewMatrix;      \n"
		"uniform mat4 modelviewprojectionMatrix;\n"
		"uniform mat3 normalMatrix;         \n"
		"                                   \n"
		"attribute vec4 in_position;        \n"
		"attribute vec3 in_normal;          \n"
		"attribute vec4 in_color;           \n"
		"\n"
		"vec4 lightSource = vec4(2.0, 2.0, 20.0, 0.0);\n"
		"                                   \n"
		"varying vec4 vVaryingColor;        \n"
		"                                   \n"
		"void main()                        \n"
		"{                                  \n"
		"    gl_Position = modelviewprojectionMatrix * in_position;\n"
		"    vec3 vEyeNormal = normalMatrix * in_normal;\n"
		"    vec4 vPosition4 = modelviewMatrix * in_position;\n"
		"    vec3 vPosition3 = vPosition4.xyz / vPosition4.w;\n"
		"    vec3 vLightDir = normalize(lightSource.xyz - vPosition3);\n"
		"    float diff = max(0.0, dot(vEyeNormal, vLightDir));\n"
		"    vVaryingColor = vec4(diff * in_color.rgb, 1.0);\n"
		"}                                  \n";
	const char *fragment_shader_source =
		"precision mediump float;           \n"
		"                                   \n"
		"varying vec4 vVaryingColor;        \n"
		"                                   \n"
		"void main()                        \n"
		"{                                  \n"
		"    gl_FragColor = vVaryingColor;  \n"
		"}                                  \n";
#else
	const uint32_t vertex_shader_bin[] = {
			0x00956003, 0x00001000, 0xc2000000, 0x00006009,
			0x400f1000, 0x10000000, 0x00000000, 0x5013c400,
			0x20000000, 0x1d481000, 0x00393e88, 0x0000000c,
			0x11482000, 0x40393a88, 0x0000000c, 0x13483000,
			0x40393e88, 0x0000000c, 0x140f0000, 0x001b0000,
			0xa1020700, 0x140f0000, 0x00c60000, 0xab020600,
			0x140f0000, 0x00b10000, 0xab020500, 0x140f803e,
			0x006c0000, 0xab020400, 0x140f0000, 0x001b0000,
			0xa1020300, 0x140f0000, 0x00c60000, 0xab020200,
			0x140f0000, 0x00b10000, 0xab020100, 0x140f0000,
			0x006c0000, 0xab020000, 0x4c170402, 0x00060000,
			0xa1030a00, 0x14070000, 0x04002c0c, 0xcb00040b,
			0x14010004, 0x00000000, 0xf0000000, 0x14070002,
			0x00310000, 0xab030902, 0x14070002, 0x002c0000,
			0xab030802, 0x58800000, 0x00000040, 0xe2000004,
			0x14070000, 0x00001b00, 0xe1000000, 0x14010000,
			0x00000000, 0xf0020000, 0x14010000, 0x00000300,
			0xa2000b00, 0x148f8000, 0x00002c00, 0xe1010000,
	};
	const uint32_t fragment_shader_bin[] = {
			0x00000000, 0x1001c400, 0x20000000, 0x140f8000,
			0x00000000, 0xe2000000,
	};
#endif
	int width = 256, height = 256;
	int i, n = 1;

	DEBUG_MSG("----------------------------------------------------------------");
	RD_START("fd-cube", "");

	state = fd_init();
	if (!state)
		return -1;

	surface = fd_surface_new(state, width, height);
	if (!surface)
		return -1;

	fd_make_current(state, surface);

	fd_vertex_shader_attach_bin(state, vertex_shader_bin,
			sizeof(vertex_shader_bin));
	fd_fragment_shader_attach_bin(state, fragment_shader_bin,
			sizeof(fragment_shader_bin));

	fd_link(state);

	fd_clear(state, 0xff808080);

	fd_enable(state, GL_CULL_FACE);

	for (i = 0; i < n; i++) {
		GLfloat aspect = (GLfloat)height / (GLfloat)width;
		ESMatrix modelview;
		ESMatrix projection;
		ESMatrix modelviewprojection;
		float normal[9];

		esMatrixLoadIdentity(&modelview);
		esTranslate(&modelview, 0.0f, 0.0f, -8.0f);
		esRotate(&modelview, 45.0f, 1.0f, 0.0f, 0.0f);
		esRotate(&modelview, 45.0f, 0.0f, 1.0f, 0.0f);
		esRotate(&modelview, 10.0f, 0.0f, 0.0f, 1.0f);

		esMatrixLoadIdentity(&projection);
		esFrustum(&projection, -2.8f, +2.8f, -2.8f * aspect, +2.8f * aspect, 6.0f, 10.0f);

		esMatrixLoadIdentity(&modelviewprojection);
		esMatrixMultiply(&modelviewprojection, &modelview, &projection);

		normal[0] = modelview.m[0][0];
		normal[1] = modelview.m[0][1];
		normal[2] = modelview.m[0][2];
		normal[3] = modelview.m[1][0];
		normal[4] = modelview.m[1][1];
		normal[5] = modelview.m[1][2];
		normal[6] = modelview.m[2][0];
		normal[7] = modelview.m[2][1];
		normal[8] = modelview.m[2][2];

		fd_attribute_pointer(state, "in_position", 3, 24, vVertices);
		fd_attribute_pointer(state, "in_normal", 3, 24, vNormals);
		fd_attribute_pointer(state, "in_color", 3, 24, vColors);

		fd_uniform_attach(state, "modelviewMatrix",
				4, 4, &modelview.m[0][0]);
		fd_uniform_attach(state, "modelviewprojectionMatrix",
				4, 4,  &modelviewprojection.m[0][0]);
		fd_uniform_attach(state, "normalMatrix",
				3, 3, normal);

		fd_draw_arrays(state, GL_TRIANGLE_STRIP, 0, 4);
		fd_draw_arrays(state, GL_TRIANGLE_STRIP, 4, 4);
		fd_draw_arrays(state, GL_TRIANGLE_STRIP, 8, 4);
		fd_draw_arrays(state, GL_TRIANGLE_STRIP, 12, 4);
		fd_draw_arrays(state, GL_TRIANGLE_STRIP, 16, 4);
		fd_draw_arrays(state, GL_TRIANGLE_STRIP, 20, 4);

		fd_swap_buffers(state);
	}

	fd_flush(state);

	fd_dump_bmp(surface, "cube.bmp");

	fd_fini(state);

	RD_END();

	return 0;
}
