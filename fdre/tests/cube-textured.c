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
#include "cubetex.h"

int main(int argc, char **argv)
{
	struct fd_state *state;
	struct fd_surface *surface, *tex;

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

	GLfloat vTexCoords[] = {
			//front
			1.0f, 1.0f, //point blue
			0.0f, 1.0f, //point magenta
			1.0f, 0.0f, //point cyan
			0.0f, 0.0f, //point white
			//back
			1.0f, 1.0f, //point red
			0.0f, 1.0f, //point black
			1.0f, 0.0f, //point yellow
			0.0f, 0.0f, //point green
			//right
			1.0f, 1.0f, //point magenta
			0.0f, 1.0f, //point red
			1.0f, 0.0f, //point white
			0.0f, 0.0f, //point yellow
			//left
			1.0f, 1.0f, //point black
			0.0f, 1.0f, //point blue
			1.0f, 0.0f, //point green
			0.0f, 0.0f, //point cyan
			//top
			1.0f, 1.0f, //point cyan
			0.0f, 1.0f, //point white
			1.0f, 0.0f, //point green
			0.0f, 0.0f, //point yellow
			//bottom
			1.0f, 0.0f, //point black
			0.0f, 0.0f, //point red
			1.0f, 1.0f, //point blue
			0.0f, 1.0f, //point magenta
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
			"uniform mat4 modelviewMatrix;\n"
			"uniform mat4 modelviewprojectionMatrix;\n"
			"uniform mat3 normalMatrix;\n"
			"\n"
			"attribute vec4 in_position;    \n"
			"attribute vec3 in_normal;      \n"
			"attribute vec2 in_TexCoord;    \n"
			"\n"
			"vec4 lightSource = vec4(2.0, 2.0, 20.0, 0.0);\n"
			"                             \n"
			"varying vec4 vVaryingColor;  \n"
			"varying vec2 vTexCoord;      \n"
			"                             \n"
			"void main()                  \n"
			"{                            \n"
			"    gl_Position = modelviewprojectionMatrix * in_position;\n"
			"    vec3 vEyeNormal = normalMatrix * in_normal;\n"
			"    vec4 vPosition4 = modelviewMatrix * in_position;\n"
			"    vec3 vPosition3 = vPosition4.xyz / vPosition4.w;\n"
			"    vec3 vLightDir = normalize(lightSource.xyz - vPosition3);\n"
			"    float diff = max(0.0, dot(vEyeNormal, vLightDir));\n"
			"    vVaryingColor = vec4(diff * vec3(1.0, 1.0, 1.0), 1.0);\n"
			"    vTexCoord = in_TexCoord; \n"
			"}                            \n";

	const char *fragment_shader_source =
			"precision mediump float;     \n"
			"                             \n"
			"uniform sampler2D uTexture;  \n"
			"                             \n"
			"varying vec4 vVaryingColor;         \n"
			"varying vec2 vTexCoord;      \n"
			"                             \n"
			"void main()                  \n"
			"{                            \n"
			"    gl_FragColor = vVaryingColor * texture2D(uTexture, vTexCoord);\n"
			"}                            \n";
#else
	const char *vertex_shader_asm =
		"@varying(R0)     vTexCoord                                                        \n"
		"@varying(R1)     vVaryingColor                                                    \n"
		"@attribute(R2)   in_position                                                      \n"
		"@attribute(R3)   in_normal                                                        \n"
		"@attribute(R1)   in_TexCoord                                                      \n"
		"@uniform(C0-C3)  modelviewMatrix                                                  \n"
		"@uniform(C4-C7)  modelviewprojectionMatrix                                        \n"
		"@uniform(C8-C10) normalMatrix                                                     \n"
		"@const(C11)      2.000000, 2.000000, 20.000000, 0.000000                          \n"
		"@const(C12)      1.000000, 1.000000, 1.000000, 0.000000                           \n"
		"EXEC                                                                              \n"
		"      FETCH:  VERTEX  R1.xy11 = R0.x FMT_32_32_FLOAT SIGNED STRIDE(8) CONST(20, 2)\n"
		"      FETCH:  VERTEX  R2.xyz1 = R0.x FMT_32_32_32_FLOAT SIGNED STRIDE(12) CONST(20, 0)\n"
		"      FETCH:  VERTEX  R3.xyz_ = R0.x FMT_32_32_32_FLOAT SIGNED STRIDE(12) CONST(20, 1)\n"
		"   (S)ALU:    MULv    R0 = R2.wwww, C7                                            \n"
		"      ALU:    MULADDv R0 = R0, R2.zzzz, C6                                        \n"
		"      ALU:    MULADDv R0 = R0, R2.yyyy, C5                                        \n"
		"ALLOC POSITION SIZE(0x0)                                                          \n"
		"EXEC                                                                              \n"
		"      ALU:    MULADDv export62 = R0, R2.xxxx, C4    ; gl_Position                 \n"
		"      ALU:    MULv    R0 = R2.wwww, C3                                            \n"
		"      ALU:    MULADDv R0 = R0, R2.zzzz, C2                                        \n"
		"      ALU:    MULADDv R0 = R0, R2.yyyy, C1                                        \n"
		"      ALU:    MULADDv R0 = R0, R2.xxxx, C0                                        \n"
		"      ALU:    MULv    R2.xyz_ = R3.zzzw, C10                                      \n"
		"              RECIP_IEEE     R4.x___ = R0                                         \n"
		"EXEC                                                                              \n"
		"      ALU:    MULADDv R0.xyz_ = C11.xxzw, -R0, R4.xxxw                            \n"
		"      ALU:    DOT3v   R4.x___ = R0, R0                                            \n"
		"      ALU:    MULADDv R2.xyz_ = R2, R3.yyyw, C9                                   \n"
		"      ALU:    MULADDv R2.xyz_ = R2, R3.xxxw, C8                                   \n"
		"ALLOC PARAM/PIXEL SIZE(0x1)                                                       \n"
		"EXEC_END                                                                          \n"
		"      ALU:    MAXv    export0 = R1, R1                                            \n"
		"      ALU:    MAXv    R0.____ = R0, R0                                            \n"
		"              RECIPSQ_IEEE     R0.___w = R4.xyzx                                  \n"
		"      ALU:    MULv    R0.xyz_ = R0, R0.wwww                                       \n"
		"      ALU:    DOT3v   R0.x___ = R2, R0                                            \n"
		"      ALU:    MAXv    export1.xyz_ = R0.xxxw, C11.wwww                            \n"
		"      ALU:    MAXv    export1.___w = C12.yxzx, C12.yxzx                           \n";

	const char *fragment_shader_asm =
		"@varying(R0)    vTexCoord                                                         \n"
		"@varying(R1)    vVaryingColor                                                     \n"
		"@sampler(0)     uTexture                                                          \n"
		"EXEC                                                                              \n"
		"   (S)FETCH:  SAMPLE  R0.xyzw = R0.xyx CONST(0)                                   \n"
		"ALLOC PARAM/PIXEL SIZE(0x0)                                                       \n"
		"EXEC_END                                                                          \n"
		"      ALU:    MULv    export0 = R1, R0 ; gl_FragColor                             \n"
		"NOP                                                                               \n";
#endif
	uint32_t width = 0, height = 0;
	int i, n = 1;

	if (argc == 2)
		n = atoi(argv[1]);

	DEBUG_MSG("----------------------------------------------------------------");
	RD_START("fd-cube-textured", "");

	state = fd_init();
	if (!state)
		return -1;

	surface = fd_surface_screen(state, &width, &height);
	if (!surface)
		return -1;

	fd_make_current(state, surface);

	tex = fd_surface_new_fmt(state, cube_texture.width, cube_texture.height,
			COLORX_8_8_8_8);

	fd_surface_upload(tex, cube_texture.pixel_data);

	fd_vertex_shader_attach_asm(state, vertex_shader_asm);
	fd_fragment_shader_attach_asm(state, fragment_shader_asm);

	fd_link(state);

	fd_enable(state, GL_CULL_FACE);
	fd_tex_param(state, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	fd_tex_param(state, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	for (i = 0; i < n; i++) {
		GLfloat aspect = (GLfloat)height / (GLfloat)width;
		ESMatrix modelview;
		ESMatrix projection;
		ESMatrix modelviewprojection;
		float normal[9];
		float scale = 1.8;

		esMatrixLoadIdentity(&modelview);
		esTranslate(&modelview, 0.0f, 0.0f, -8.0f);
		esRotate(&modelview, 45.0f + (0.25f * i), 1.0f, 0.0f, 0.0f);
		esRotate(&modelview, 45.0f - (0.15f * i), 0.0f, 1.0f, 0.0f);
		esRotate(&modelview, 10.0f + (0.25f * i), 0.0f, 0.0f, 1.0f);

		esMatrixLoadIdentity(&projection);
		esFrustum(&projection,
				-scale, +scale,
				-scale * aspect, +scale * aspect,
				6.0f, 10.0f);

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

		fd_clear_color(state, 0xff404040);
		fd_clear(state, GL_COLOR_BUFFER_BIT);

		fd_set_texture(state, "uTexture", tex);

		fd_attribute_pointer(state, "in_position", 3, 24, vVertices);
		fd_attribute_pointer(state, "in_normal", 3, 24, vNormals);
		fd_attribute_pointer(state, "in_TexCoord", 2, 24, vTexCoords);

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

	fd_dump_bmp(surface, "cube-textured.bmp");

	fd_fini(state);

	RD_END();

	return 0;
}
