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
#include "cat-model.h"
#include "lolstex1.h"
#include "lolstex2.h"

int main(int argc, char **argv)
{
	struct fd_state *state;
	struct fd_surface *surface, *lolstex1, *lolstex2;
	struct fd_program *cat_program, *tex_program;
	struct fd_bo *position_vbo, *normal_vbo;
	const char *cat_vertex_shader_asm =
		"@varying(R0)     vertex_normal                                                      \n"
		"@varying(R1)     vertex_position                                                    \n"
		"@attribute(R1)   normal                                                             \n"
		"@attribute(R2)   position                                                           \n"
		"@uniform(C0-C3)  ModelViewMatrix                                                    \n"
		"@uniform(C4-C7)  ModelViewProjectionMatrix                                          \n"
		"@uniform(C8-C10) NormalMatrix                                                       \n"
		"@const(C11)      1.000000, 0.000000, 0.000000, 0.000000                             \n"
		"EXEC                                                                                \n"
		"      FETCH:    VERTEX  R1.xyz_ = R0.x FMT_32_32_32_FLOAT SIGNED STRIDE(12) CONST(20, 0)\n"
		"      FETCH:    VERTEX  R2.xyz_ = R0.x FMT_32_32_32_FLOAT SIGNED STRIDE(12) CONST(20, 1)\n"
		"   (S)ALU:      MULADDv R0 = C7, R2.zzzz, C6                                        \n"
		"      ALU:      MULADDv R0 = R0, R2.yyyy, C5                                        \n"
		"ALLOC POSITION SIZE(0x0)                                                            \n"
		"EXEC                                                                                \n"
		"      ALU:      MULADDv export62 = R0, R2.xxxx, C4      ; gl_Position               \n"
		"      ALU:      MULv    R0.xyz_ = R1.zzzw, C10                                      \n"
		"      ALU:      MULADDv R0.xyz_ = R0, R1.yyyw, C9                                   \n"
		"      ALU:      MULADDv R0.xyz_ = R0, R1.xxxw, C8                                   \n"
		"      ALU:      DOT3v   R1.x___ = R0, R0                                            \n"
		"      ALU:      MULADDv R3 = C3, R2.zzzz, C2                                        \n"
		"EXEC                                                                                \n"
		"      ALU:      MULADDv R3 = R3, R2.yyyy, C1                                        \n"
		"      ALU:      MAXv    R0.____ = R0, R0                                            \n"
		"                RECIPSQ_IEEE     R0.___w = R1.xyzx                                  \n"
		"ALLOC PARAM/PIXEL SIZE(0x1)                                                         \n"
		"EXEC_END                                                                            \n"
		"      ALU:      MULADDv export1 = R3, R2.xxxx, C0                                   \n"
		"      ALU:      MULv    export0.xyz_ = R0, R0.wwww                                  \n";

	const char *cat_fragment_shader_asm =
/*
precision mediump float;
const vec4 MaterialDiffuse = vec4(0.000000, 0.000000, 1.000000, 1.000000);
const vec4 LightColor0 = vec4(0.800000, 0.800000, 0.800000, 1.000000);
const vec4 light_position = vec4(0.000000, 1.000000, 0.000000, 1.000000);
varying vec3 vertex_normal;
varying vec4 vertex_position;

void main(void)
{
    const vec4 diffuse_light_color = LightColor0;
    const vec4 lightAmbient = vec4(0.1, 0.1, 0.1, 1.0);
    const vec4 lightSpecular = vec4(0.8, 0.8, 0.8, 1.0);
    const vec4 matAmbient = vec4(0.2, 0.2, 0.2, 1.0);
    const vec4 matSpecular = vec4(1.0, 1.0, 1.0, 1.0);
    const float matShininess = 100.0;                     // C4.x
    vec3 eye_direction = normalize(-vertex_position.xyz);
    vec3 light_direction = normalize(light_position.xyz/light_position.w -
                                     vertex_position.xyz/vertex_position.w);
    vec3 normalized_normal = normalize(vertex_normal);

    // reflect(i,n) -> i - 2 * dot(n,i) * n
    vec3 reflection = reflect(-light_direction, normalized_normal);
    float specularTerm = pow(max(0.0, dot(reflection, eye_direction)), matShininess);
    float diffuseTerm = max(0.0, dot(normalized_normal, light_direction));
    vec4 specular = (lightSpecular * matSpecular);
    vec4 ambient = (lightAmbient * matAmbient);
    vec4 diffuse = (diffuse_light_color * MaterialDiffuse);
    vec4 result = (specular * specularTerm) + ambient + (diffuse * diffuseTerm);
    gl_FragColor = result;
}
*/
		"@varying(R0)    vertex_normal                                                                                \n"
		"@varying(R1)    vertex_position                                                                              \n"
		"@const(C0)      0.000000, 1.000000, 0.000000, 0.000000                                                       \n"
		"@const(C1)      0.800000, 0.800000, 0.800000, 1.000000                                                       \n"
		"@const(C2)      0.020000, 0.020000, 0.020000, 1.000000                                                       \n"
		"@const(C3)      0.000000, 0.000000, 0.800000, 1.000000                                                       \n"
		"@const(C4)      100.000000, 0.000000, 0.000000, 0.000000                                                     \n"
		"EXEC                                                                                                         \n"
		"   (S)ALU:      DOT3v   R2.x___ = R0, R0                ; normalize(vertex_normal)                           \n"
		"                RECIP_IEEE     R3.x___ = R1             ; 1/vertex_position.x ? R1.wyzw?                     \n"
		"      ALU:      MULADDv R3.xyz_ = C0.xyxw, -R1, R3.xxxw ; light_position.xyz/1.0 -                           \n"
		"                                                        ; vertex_position.xyz/vertex_position.w              \n"
		"      ALU:      DOT3v   R2.x___ = R3, R3                ; normalize(light_position...)                       \n"
		"                RECIPSQ_IEEE     R0.___w = R2.xyzx      ; normalize(vertex_normal)                           \n"
		"; here the RSQ sees the R2 value written in first instruction, rather than                                   \n"
		"; the R2 dst being calculated as part of the same VLIW instruction                                           \n"
		"      ALU:      MULv    R0.xyz_ = R0, R0.wwww           ; normalized_normal = normalize(vertex_normal)       \n"
		"                RECIPSQ_IEEE     R0.___w = R2.xyzx      ; normalize(light_position...)                       \n"
		"      ALU:      MULv    R2.xyz_ = R3, R0.wwww           ; light_direction = normalize(light_position...)     \n"
		"      ALU:      DOT3v   R3.x___ = -R1, -R1              ; normalize(-vertex_position.xyz)                    \n"
		"EXEC                                                                                                         \n"
		"; reflect(i,n) -> i - 2 * dot(n,i) * n                                                                       \n"
		"      ALU:      DOT3v   R3.x___ = -R2, R0               ; reflect(-light_direction, normalized_normal)       \n"
		"                RECIPSQ_IEEE     R0.___w = R3.xyzx      ; normalize(-vertex_position.xyz);                   \n"
		"      ALU:      MULv    R1.xyz_ = -R1, R0.wwww          ; eye_direction = normalize(-vertex_position.xyz)    \n"
		"                ADDs    R3.x___ = R3.xyzx               ; 2 * dot(n, i)                                      \n"
		"      ALU:      MULADDv R3.xyz_ = -R2, R0, -R3.xxxw     ; reflect(..) -> i + (n * (2 * dot(n, i))            \n"
		"      ALU:      DOT3v   R1.x___ = R1, R3                ; dot(reflection, eye_direction)                     \n"
		"      ALU:      MAXv    R1.x___ = R1, C0                ; max(0.0, dot(reflection, eye_direction)            \n"
		"      ALU:      DOT3v   R0.x___ = R2, R0                ; dot(normalized_normal, light_direction)            \n"
		"                LOG_CLAMP    R0.___w = R1.xyzx          ; pow(max(0.0, dot(...)), matShininess)              \n"
		"EXEC                                                                                                         \n"
		"      ALU:      MAXv    R0.x___ = R0, C0                ; diffuseTerm = max(0.0, dot(...))                   \n"
		"                MUL_CONST_0     R0.___w = C4.wyzx       ; specularTerm = pow(..., matShininess)              \n"
		"      ALU:      MAXv    R0.____ = R0, R0                                                                     \n"
		"                EXP_IEEE    R1.x___ = R0                ; specularTerm = pow(..)                             \n"
		"; C2 is ambient  = (lightAmbient * matAmbient)   = vec4(0.1,0.1,0.1,1.0) * vec4(0.2,0.2,0.2,1.0)             \n"
		"; C1 is specular = (lightSpecular * matSpecular) = vec4(0.8,0.8,0.8,1.0) * vec4(1.0,1.0,1.0,1.0)             \n"
		"      ALU:      MULADDv R1.x__w = C2, R1.xyzx, C1       ; ambient + (specularTerm * specular)                \n"
		"ALLOC PARAM/PIXEL SIZE(0x0)                                                                                  \n"
		"EXEC_END ADDR(0x12) CNT(0x1)                                                                                 \n"
		"      ALU:      MULADDv export0 = R1.xxxw, R0.xxxx, C3.xxzw     ; gl_FragColor                               \n"
		"NOP                                                                                                          \n";

	static const GLfloat texcoords[] = {
			0.0f, 1.0f,
			1.0f, 1.0f,
			0.0f, 0.0f,
			1.0f, 0.0f,
	};

	static const GLfloat tex1_vertices[] = {
			-0.95, +0.45, -1.0,
			+0.45, +0.45, -1.0,
			-0.95, +0.95, -1.0,
			+0.45, +0.95, -1.0
	};

	static const GLfloat tex2_vertices[] = {
			-0.45, -0.95, -1.0,
			+0.95, -0.95, -1.0,
			-0.45, -0.45, -1.0,
			+0.95, -0.45, -1.0
	};

	const char *tex_vertex_shader_asm =
		"@attribute(R1)  aPosition                                        \n"
		"@attribute(R2)  aTexCoord                                        \n"
		"@varying(R0)    vTexCoord                                        \n"
		"EXEC                                                             \n"
		"      FETCH:   VERTEX   R2.xy11 = R0.x FMT_32_32_FLOAT SIGNED    \n"
		"                                      STRIDE(8) CONST(20, 1)     \n"
		"   (S)FETCH:   VERTEX   R1.xyz1 = R0.x FMT_32_32_32_FLOAT SIGNED \n"
		"                                      STRIDE(12) CONST(20, 0)    \n"
		"ALLOC POSITION SIZE(0x0)                                         \n"
		"EXEC                                                             \n"
		"      ALU:   MAXv   export62 = R1, R1   ; gl_Position            \n"
		"ALLOC PARAM/PIXEL SIZE(0x0)                                      \n"
		"EXEC_END                                                         \n"
		"      ALU:   MAXv   export0 = R2, R2    ; vTexCoord              \n"
		"NOP                                                              \n";

	const char *tex_fragment_shader_asm =
		"@varying(R0)    vTexCoord                                        \n"
		"@sampler(0)     uTexture                                         \n"
		"EXEC                                                             \n"
		"   (S)FETCH:  SAMPLE  R0.xyzw = R0.xyx CONST(0)                  \n"
		"ALLOC PARAM/PIXEL SIZE(0x0)                                      \n"
		"EXEC_END                                                         \n"
		"      ALU:    MAXv    export0 = R0, R0 ; gl_FragColor            \n"
		"NOP                                                              \n";

	uint32_t width = 0, height = 0;
	int i, n = 1;

	if (argc == 2)
		n = atoi(argv[1]);

	DEBUG_MSG("----------------------------------------------------------------");
	RD_START("fd-cat", "");

	state = fd_init();
	if (!state)
		return -1;

	surface = fd_surface_screen(state, &width, &height);
	if (!surface)
		return -1;

	/* load textures: */
	lolstex1 = fd_surface_new_fmt(state, lolstex1_image.width, lolstex1_image.height,
			COLORX_8_8_8_8);
	fd_surface_upload(lolstex1, lolstex1_image.pixel_data);

	lolstex2 = fd_surface_new_fmt(state, lolstex2_image.width, lolstex2_image.height,
			COLORX_8_8_8_8);
	fd_surface_upload(lolstex2, lolstex2_image.pixel_data);

	fd_enable(state, GL_CULL_FACE);
	fd_depth_func(state, GL_LEQUAL);
	fd_enable(state, GL_DEPTH_TEST);
	fd_tex_param(state, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	fd_tex_param(state, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	fd_blend_func(state, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	/* this needs to come after enabling depth test as enabling depth test
	 * effects bin sizes:
	 */
	fd_make_current(state, surface);

	/* construct the two shader programs: */
	cat_program = fd_program_new();
	fd_program_attach_asm(cat_program, FD_SHADER_VERTEX, cat_vertex_shader_asm);
	fd_program_attach_asm(cat_program, FD_SHADER_FRAGMENT, cat_fragment_shader_asm);

	tex_program = fd_program_new();
	fd_program_attach_asm(tex_program, FD_SHADER_VERTEX, tex_vertex_shader_asm);
	fd_program_attach_asm(tex_program, FD_SHADER_FRAGMENT, tex_fragment_shader_asm);

	fd_link(state);

	position_vbo = fd_attribute_bo_new(state, cat_position_sz, cat_position);
	normal_vbo = fd_attribute_bo_new(state, cat_normal_sz, cat_normal);

	for (i = 0; i < n; i++) {
		GLfloat aspect = (GLfloat)height / (GLfloat)width;
		ESMatrix modelview;
		ESMatrix projection;
		ESMatrix modelviewprojection;
		float normal[9];
		float scale = 1.8;

		esMatrixLoadIdentity(&modelview);
		esTranslate(&modelview, 0.0f, 0.0f, -8.0f);
		esRotate(&modelview, 45.0f - (0.5f * i), 0.0f, 1.0f, 0.0f);

		esMatrixLoadIdentity(&projection);
		esFrustum(&projection,
				-scale, +scale,
				-scale * aspect, +scale * aspect,
				5.5f, 10.0f);

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

		fd_clear_color(state, 0xff000000);
		fd_clear(state, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		fd_attribute_bo(state, "normal", normal_vbo);
		fd_attribute_bo(state, "position", position_vbo);

		fd_uniform_attach(state, "ModelViewMatrix",
				4, 4, &modelview.m[0][0]);
		fd_uniform_attach(state, "ModelViewProjectionMatrix",
				4, 4,  &modelviewprojection.m[0][0]);
		fd_uniform_attach(state, "NormalMatrix",
				3, 3, normal);

		/* draw cat: */
		fd_disable(state, GL_BLEND);
		fd_set_program(state, cat_program);
		fd_draw_arrays(state, GL_TRIANGLES, 0, cat_vertices);

		/* setup to draw text (common to tex1 and tex2): */
		fd_enable(state, GL_BLEND);
		fd_set_program(state, tex_program);
		fd_attribute_pointer(state, "aTexCoord", 2, 4, texcoords);

		/* draw tex1: */
		fd_set_texture(state, "uTexture", lolstex1);
		fd_attribute_pointer(state, "aPosition", 3, 4, tex1_vertices);
		fd_draw_arrays(state, GL_TRIANGLE_STRIP, 0, 4);

		/* draw tex2: */
		fd_set_texture(state, "uTexture", lolstex2);
		fd_attribute_pointer(state, "aPosition", 3, 4, tex2_vertices);
		fd_draw_arrays(state, GL_TRIANGLE_STRIP, 0, 4);

		fd_swap_buffers(state);
	}

	fd_flush(state);

	fd_dump_bmp(surface, "lolscat.bmp");

	fd_fini(state);

	RD_END();

	return 0;
}
