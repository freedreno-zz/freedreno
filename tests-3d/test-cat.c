/*
 * Copyright (c) 2011-2012 Luc Verhaegen <libv@codethink.co.uk>
 * Copyright (c) 2012 Rob Clark <robdclark@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* Code copied from cube test from lima driver project adapted to the
 * logging that I use..
 */

#include "test-util-3d.h"

#include "esUtil.h"
#include "esTransform.c"

#include "../fdre-a2xx/tests/cat-model.c"

static EGLint const config_attribute_list[] = {
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_DEPTH_SIZE, 8,
	EGL_NONE
};

static const EGLint context_attribute_list[] = {
	EGL_CONTEXT_CLIENT_VERSION, 2,
	EGL_NONE
};

static EGLDisplay display;
static EGLConfig config;
static EGLint num_config;
static EGLContext context;
static GLuint program;
const char *vertex_shader_source =
		"attribute vec3 position;                                                \n"
		"attribute vec3 normal;                                                  \n"
		"                                                                        \n"
		"uniform mat4 ModelViewProjectionMatrix;                                 \n"
		"uniform mat3 NormalMatrix;                                              \n"
		"uniform mat4 ModelViewMatrix;                                           \n"
		"                                                                        \n"
		"varying vec3 vertex_normal;                                             \n"
		"varying vec4 vertex_position;                                           \n"
		"                                                                        \n"
		"void main(void)                                                         \n"
		"{                                                                       \n"
		"    vec4 current_position = vec4(position, 1.0);                        \n"
		"                                                                        \n"
		"    // Transform the normal to eye coordinates                          \n"
		"    vertex_normal = normalize(NormalMatrix * normal);                   \n"
		"                                                                        \n"
		"    // Transform the current position to eye coordinates                \n"
		"    vertex_position = ModelViewMatrix * current_position;               \n"
		"                                                                        \n"
		"    // Transform the current position to clip coordinates               \n"
		"    gl_Position = ModelViewProjectionMatrix * current_position;         \n"
		"}                                                                       \n";

const char *fragment_shader_source =
		"precision mediump float;                                                                \n"
		"const vec4 MaterialDiffuse = vec4(0.000000, 0.000000, 1.000000, 1.000000);              \n"
		"const vec4 LightColor0 = vec4(0.800000, 0.800000, 0.800000, 1.000000);                  \n"
		"const vec4 LightSourcePosition0 = vec4(0.000000, 1.000000, 0.000000, 1.000000);         \n"
		"varying vec3 vertex_normal;                                                             \n"
		"varying vec4 vertex_position;                                                           \n"
		"                                                                                        \n"
		"void main(void)                                                                         \n"
		"{                                                                                       \n"
		"    const vec4 light_position = LightSourcePosition0;                                   \n"
		"    const vec4 diffuse_light_color = LightColor0;                                       \n"
		"    const vec4 lightAmbient = vec4(0.1, 0.1, 0.1, 1.0);                                 \n"
		"    const vec4 lightSpecular = vec4(0.8, 0.8, 0.8, 1.0);                                \n"
		"    const vec4 matAmbient = vec4(0.2, 0.2, 0.2, 1.0);                                   \n"
		"    const vec4 matSpecular = vec4(1.0, 1.0, 1.0, 1.0);                                  \n"
		"    const float matShininess = 100.0;                                                   \n"
		"    vec3 eye_direction = normalize(-vertex_position.xyz);                               \n"
		"    vec3 light_direction = normalize(light_position.xyz/light_position.w -              \n"
		"                                     vertex_position.xyz/vertex_position.w);            \n"
		"    vec3 normalized_normal = normalize(vertex_normal);                                  \n"
		"    vec3 reflection = reflect(-light_direction, normalized_normal);                     \n"
		"    float specularTerm = pow(max(0.0, dot(reflection, eye_direction)), matShininess);   \n"
		"    float diffuseTerm = max(0.0, dot(normalized_normal, light_direction));              \n"
		"    vec4 specular = (lightSpecular * matSpecular);                                      \n"
		"    vec4 ambient = (lightAmbient * matAmbient);                                         \n"
		"    vec4 diffuse = (diffuse_light_color * MaterialDiffuse);                             \n"
		"    vec4 result = (specular * specularTerm) + ambient + (diffuse * diffuseTerm);        \n"
		"    gl_FragColor = result;                                                              \n"
		"}                                                                                       \n";

void test_cat(void)
{
	GLint width, height;
	GLint modelviewmatrix_handle, modelviewprojectionmatrix_handle, normalmatrix_handle;
	GLuint position_vbo, normal_vbo;
	EGLSurface surface;
	float scale = 1.3;

	RD_START("cat", "");

	display = get_display();

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	DEBUG_MSG("num_config: %d", num_config);

	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));

	surface = make_window(display, config, 400, 240);

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("Buffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));

	program = get_program(vertex_shader_source, fragment_shader_source);

	GCHK(glBindAttribLocation(program, 0, "normal"));
	GCHK(glBindAttribLocation(program, 1, "position"));

	/* upload the attribute vbo's, only done once: */
	GCHK(glGenBuffers(1, &normal_vbo));
	GCHK(glBindBuffer(GL_ARRAY_BUFFER, normal_vbo));
	GCHK(glBufferData(GL_ARRAY_BUFFER, sizeof(cat_normal), cat_normal, GL_STATIC_DRAW));

	GCHK(glGenBuffers(1, &position_vbo));
	GCHK(glBindBuffer(GL_ARRAY_BUFFER, position_vbo));
	GCHK(glBufferData(GL_ARRAY_BUFFER, sizeof(cat_position), cat_position, GL_STATIC_DRAW));

	link_program(program);

	GCHK(glViewport(0, 0, width, height));

	/* clear the color buffer */
	GCHK(glClearColor(0.5, 0.5, 0.5, 1.0));
	GCHK(glEnable(GL_DEPTH_TEST));
	GCHK(glDepthFunc(GL_LEQUAL));
	GCHK(glEnable(GL_CULL_FACE));
	GCHK(glCullFace(GL_BACK));
	GCHK(glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT));

	GCHK(glEnableVertexAttribArray(0));
	GCHK(glBindBuffer(GL_ARRAY_BUFFER, normal_vbo));
	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL));

	GCHK(glEnableVertexAttribArray(1));
	GCHK(glBindBuffer(GL_ARRAY_BUFFER, position_vbo));
	GCHK(glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, NULL));

	ESMatrix modelview;
	esMatrixLoadIdentity(&modelview);
	esTranslate(&modelview, 0.0f, 0.0f, -8.0f);
	esRotate(&modelview, 45.0f, 0.0f, 1.0f, 0.0f);

	GLfloat aspect = (GLfloat)(height) / (GLfloat)(width);

	ESMatrix projection;
	esMatrixLoadIdentity(&projection);
	esFrustum(&projection,
			-scale, +scale,
			-scale * aspect, +scale * aspect,
			5.5f, 10.0f);

	ESMatrix modelviewprojection;
	esMatrixLoadIdentity(&modelviewprojection);
	esMatrixMultiply(&modelviewprojection, &modelview, &projection);

	float normal[9];
	normal[0] = modelview.m[0][0];
	normal[1] = modelview.m[0][1];
	normal[2] = modelview.m[0][2];
	normal[3] = modelview.m[1][0];
	normal[4] = modelview.m[1][1];
	normal[5] = modelview.m[1][2];
	normal[6] = modelview.m[2][0];
	normal[7] = modelview.m[2][1];
	normal[8] = modelview.m[2][2];

	GCHK(modelviewmatrix_handle = glGetUniformLocation(program, "ModelViewMatrix"));
	GCHK(modelviewprojectionmatrix_handle = glGetUniformLocation(program, "ModelViewProjectionMatrix"));
	GCHK(normalmatrix_handle = glGetUniformLocation(program, "NormalMatrix"));

	GCHK(glUniformMatrix4fv(modelviewmatrix_handle, 1, GL_FALSE, &modelview.m[0][0]));
	GCHK(glUniformMatrix4fv(modelviewprojectionmatrix_handle, 1, GL_FALSE, &modelviewprojection.m[0][0]));
	GCHK(glUniformMatrix3fv(normalmatrix_handle, 1, GL_FALSE, normal));

	GCHK(glDrawArrays(GL_TRIANGLES, 0, cat_vertices));

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	ECHK(eglDestroySurface(display, surface));

	ECHK(eglTerminate(display));

	RD_END();
}

int main(int argc, char *argv[])
{
	TEST_START();
	TEST(test_cat());
	TEST_END();
	return 0;
}

