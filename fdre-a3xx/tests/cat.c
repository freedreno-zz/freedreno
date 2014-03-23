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
		"@out(r2.y)     gl_Position                                       \n"
		"@varying(r0.x-r0.z)     vertex_normal                            \n"
		"@varying(r1.y-r2.x)     vertex_position                          \n"
		"@attribute(r0.x-r0.z)   normal                                   \n"
		"@attribute(r0.w-r1.y)   position                                 \n"
		"@uniform(c0.x-c3.w)  ModelViewMatrix                             \n"
		"@uniform(c4.x-c7.w)  ModelViewProjectionMatrix                   \n"
		"@uniform(c8.x-c10.w) NormalMatrix                                \n"
		"(sy)(ss)(rpt2)mul.f r1.z, r0.z, (r)c10.x                         \n"
		"(rpt3)mad.f32 r2.y, (r)c2.x, r1.y, (r)c3.x                       \n"
		"(rpt2)mad.f32 r1.z, (r)c9.x, r0.y, (r)r1.z                       \n"
		"(rpt3)mad.f32 r3.y, (r)c6.x, r1.y, (r)c7.x                       \n"
		"(rpt2)mad.f32 r0.x, (r)c8.x, r0.x, (r)r1.z                       \n"
		"(rpt3)mad.f32 r1.y, (r)c1.x, r1.x, (r)r2.y                       \n"
		"mul.f r2.y, r0.x, r0.x                                           \n"
		"(rpt3)mad.f32 r2.z, (r)c5.x, r1.x, (r)r3.y                       \n"
		"mad.f32 r1.x, r0.y, r0.y, r2.y                                   \n"
		"(rpt3)mad.f32 r1.y, (r)c0.x, r0.w, (r)r1.y                       \n"
		"mad.f32 r1.x, r0.z, r0.z, r1.x                                   \n"
		"(rpt3)mad.f32 r2.y, (r)c4.x, r0.w, (r)r2.z                       \n"
		"(rpt1)nop                                                        \n"
		"rsq r0.w, r1.x                                                   \n"
		"(ss)(rpt2)mul.f r0.x, (r)r0.x, r0.w                              \n"
		"end                                                              \n";

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
		"@out(hr0.y)             gl_FragColor                             \n"
		"@varying(r0.x-r0.z)     vertex_normal                            \n"
		"@varying(r1.y-r2.x)     vertex_position                          \n"
		"@const(c0.x)            0.000000, 1.000000, 0.000000, 0.000000   \n"
		"@const(c1.x)            0.800000, 0.800000, 0.800000, 1.000000   \n"
		"@const(c2.x)            0.020000, 0.020000, 0.020000, 1.000000   \n"
		"@const(c3.x)            0.000000, 0.000000, 0.800000, 1.000000   \n"
		"@const(c4.x)            100.000000, 0.000000, 0.000000, 0.000000 \n"
		"@const(c5.x)            2.000000, 0.000000, 0.000000, 0.000000   \n"
		"(sy)(ss)(rpt3)bary.f hr0.x, (r)3, r0.x                           \n"
		"(rpt2)bary.f (ei)hr1.x, (r)0, r0.x                               \n"
		"(rpt2)nop                                                        \n"
		"rcp hr0.w, hr0.w                                                 \n"
		"mul.f hr1.w, hr1.x, hr1.x                                        \n"
		"mul.f hr2.x, (neg)hr0.x, (neg)hr0.x                              \n"
		"mad.f16 hr1.w, hr1.y, hr1.y, hr1.w                               \n"
		"mad.f16 hr2.x, (neg)hr0.y, (neg)hr0.y, hr2.x                     \n"
		"mad.f16 hr1.w, hr1.z, hr1.z, hr1.w                               \n"
		"mad.f16 hr2.x, (neg)hr0.z, (neg)hr0.z, hr2.x                     \n"
		"(ss)(rpt1)mul.f hr2.y, (r)hr0.x, hr0.w                           \n"
		"mul.f hr0.w, hr0.z, hr0.w                                        \n"
		"(rpt1)nop                                                        \n"
		"rsq hr1.w, hr1.w                                                 \n"
		"add.f hr2.z, (neg)hr2.z, hc0.y                                   \n"
		"mul.f hr2.w, (neg)hr2.y, (neg)hr2.y                              \n"
		"rsq hr2.x, hr2.x                                                 \n"
		"(rpt1)nop                                                        \n"
		"mad.f16 hr2.w, hr2.z, hr2.z, hr2.w                               \n"
		"nop                                                              \n"
		"mad.f16 hr2.w, (neg)hr0.w, (neg)hr0.w, hr2.w                     \n"
		"(ss)(rpt2)mul.f hr1.x, (r)hr1.x, hr1.w                           \n"
		"(rpt2)mul.f hr0.x, (neg)(r)hr0.x, hr2.x                          \n"
		"rsq hr1.w, hr2.w                                                 \n"
		"(ss)mul.f hr2.w, (neg)hr2.y, hr1.w                               \n"
		"mul.f hr3.x, hr2.z, hr1.w                                        \n"
		"mul.f hr3.y, (neg)hr0.w, hr1.w                                   \n"
		"nop                                                              \n"
		"mul.f hr0.w, (neg)hr2.w, hr1.x                                   \n"
		"mul.f hr1.w, hr2.w, hr1.x                                        \n"
		"mad.f16 hr0.w, (neg)hr3.x, hr1.y, hr0.w                          \n"
		"mad.f16 hr1.w, hr3.x, hr1.y, hr1.w                               \n"
		"mad.f16 hr0.w, (neg)hr3.y, hr1.z, hr0.w                          \n"
		"mad.f16 hr1.w, hr3.y, hr1.z, hr1.w                               \n"
		"(rpt1)nop                                                        \n"
		"mul.f hr0.w, hr0.w, hc5.x                                        \n"
		"max.f hr1.w, hr1.w, hc0.x                                        \n"
		"(rpt1)nop                                                        \n"
		"(rpt2)mad.f16 hr0.w, (r)hr1.x, (neg)hr0.w, (neg)(r)hr2.w         \n"
		"mul.f hr1.z, hr1.w, hc3.z                                        \n"
		"mul.f hr0.x, hr0.x, hr0.w                                        \n"
		"nop                                                              \n"
		"mad.f16 hr0.x, hr0.y, hr1.x, hr0.x                               \n"
		"nop                                                              \n"
		"mad.f16 hr0.x, hr0.z, hr1.y, hr0.x                               \n"
		"(rpt2)nop                                                        \n"
		"max.f hr0.x, hr0.x, hc0.x                                        \n"
		"(rpt5)nop                                                        \n"
		"log2 hr0.x, hr0.x                                                \n"
		"(ss)mul.f hr0.x, hr0.x, hc4.x                                    \n"
		"(rpt5)nop                                                        \n"
		"exp2 hr0.x, hr0.x                                                \n"
		"(ss)mul.f hr0.y, hr0.x, hc1.x                                    \n"
		"add.f hr0.x, hr0.x, hc2.w                                        \n"
		"(rpt1)nop                                                        \n"
		"add.f hr0.y, hr0.y, hc2.x                                        \n"
		"add.f hr1.x, hr0.x, hr1.w                                        \n"
		"(rpt1)nop                                                        \n"
		"add.f hr0.w, hr0.y, hr1.z                                        \n"
		"mov.f16f16 hr0.z, hr0.y                                          \n"
		"end                                                              \n";

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
		"@out(r0.x)             gl_Position                               \n"
		"@attribute(r0.x-r0.w)  aPosition                                 \n"
		"@attribute(r1.x-r1.y)  aTexCoord                                 \n"
		"@varying(r1.x-r1.y)    vTexCoord                                 \n"
		"(sy)(ss)end                                                      \n";

	const char *tex_fragment_shader_asm =
		"@out(hr0.x)            gl_FragColor                              \n"
		"@varying(r1.x-r1.y)    vTexCoord                                 \n"
		"@sampler(0)            uTexture                                  \n"
		"(sy)(ss)(rpt1)bary.f (ei)r0.z, (r)0, r0.x                        \n"
		"(rpt5)nop                                                        \n"
		"sam (f16)(xyzw)hr0.x, r0.z, s#0, t#0                             \n"
		"end                                                              \n";

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
			RB_R8G8B8A8_UNORM);
	fd_surface_upload(lolstex1, lolstex1_image.pixel_data);

	lolstex2 = fd_surface_new_fmt(state, lolstex2_image.width, lolstex2_image.height,
			RB_R8G8B8A8_UNORM);
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
	cat_program = fd_program_new(state);
	fd_program_attach_asm(cat_program, FD_SHADER_VERTEX, cat_vertex_shader_asm);
	fd_program_attach_asm(cat_program, FD_SHADER_FRAGMENT, cat_fragment_shader_asm);

	tex_program = fd_program_new(state);
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
		float scale = 1.3;

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

		fd_clear_color(state, (float[]){ 0.0, 0.0, 0.0, 1.0 });
		fd_clear(state, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		fd_attribute_bo(state, "normal",
				VFMT_FLOAT_32_32_32, normal_vbo);
		fd_attribute_bo(state, "position",
				VFMT_FLOAT_32_32_32, position_vbo);

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
		fd_attribute_pointer(state, "aTexCoord",
				VFMT_FLOAT_32_32, 4, texcoords);

		/* draw tex1: */
		fd_set_texture(state, "uTexture", lolstex1);
		fd_attribute_pointer(state, "aPosition",
				VFMT_FLOAT_32_32_32, 4, tex1_vertices);
		fd_draw_arrays(state, GL_TRIANGLE_STRIP, 0, 4);

		/* draw tex2: */
		fd_set_texture(state, "uTexture", lolstex2);
		fd_attribute_pointer(state, "aPosition",
				VFMT_FLOAT_32_32_32, 4, tex2_vertices);
		fd_draw_arrays(state, GL_TRIANGLE_STRIP, 0, 4);

		fd_swap_buffers(state);
	}

	fd_flush(state);

	if (n == 1) {
		fd_dump_bmp(surface, "lolscat.bmp");
		sleep(1);
	}

	fd_fini(state);

	RD_END();

	return 0;
}
