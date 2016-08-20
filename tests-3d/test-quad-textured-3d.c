/*
 * Copyright (c) 2011-2012 Luc Verhaegen <libv@codethink.co.uk>
 * Copyright (c) 2012 Jonathan Maw <jonathan.maw@codethink.co.uk>
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

/* Code based on cube_textured test from lima driver project (Jonathan Maw)
 * adapted to the logging that I use..
 */

#include <GLES3/gl3.h>
#include "test-util-3d.h"

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
		"#version 300 es              \n"
		"in vec4 in_position;         \n"
		"in vec3 in_TexCoord;         \n"
		"\n"
		"out vec3 vTexCoord;          \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_Position = in_position;\n"
		"    vTexCoord = in_TexCoord; \n"
		"}                            \n";

const char *fragment_shader_source =
		"#version 300 es              \n"
		"precision mediump float;     \n"
		"                             \n"
		"uniform sampler3D uTexture;  \n"
		"in vec3 vTexCoord;           \n"
		"out vec4 gl_FragColor;       \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_FragColor = texture(uTexture, vTexCoord);\n"
		"}                            \n";

const char *fragment_shader_source_array =
		"#version 300 es              \n"
		"precision mediump float;     \n"
		"                             \n"
		"uniform sampler2DArray uTexture;  \n"
		"in vec3 vTexCoord;           \n"
		"out vec4 gl_FragColor;       \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_FragColor = texture(uTexture, vTexCoord);\n"
		"}                            \n";


void test_quad_textured(GLenum type, GLenum format, int w, int h, int d)
{
	GLint width, height;
	GLuint texturename = 0, texture_handle;
	GLfloat vVertices[] = {
			// front
			-0.45, -0.75, 0.0,
			 0.45, -0.75, 0.0,
			-0.45,  0.75, 0.0,
			 0.45,  0.75, 0.0
	};

	GLfloat vTexCoords[] = {
			1.0f, 1.0f,
			0.0f, 1.0f,
			1.0f, 0.0f,
			0.0f, 0.0f,
	};
	uint8_t *pixel_data;
	int i;

	EGLSurface surface;

	RD_START("quad-textured-3d", "type=%s, format=%s, %dx%dx%d",
			textypename(type), formatname(format), w, h, d);

	pixel_data = malloc(w * h * d * 4);

	/* initialize to a recognizable pattern: */
	for (i = 0; i < (w * h * d * 4); i++)
		pixel_data[i] = i;

	display = get_display();

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	DEBUG_MSG("num_config: %d", num_config);

	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));

	surface = make_window(display, config, 255, 255);

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("Buffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));

	printf("EGL Version %s\n", eglQueryString(display, EGL_VERSION));
	printf("EGL Vendor %s\n", eglQueryString(display, EGL_VENDOR));
	printf("EGL Extensions %s\n", eglQueryString(display, EGL_EXTENSIONS));
	printf("GL Version %s\n", glGetString(GL_VERSION));
	printf("GL extensions: %s\n", glGetString(GL_EXTENSIONS));

	program = get_program(vertex_shader_source,
			(type == GL_TEXTURE_3D) ? fragment_shader_source : fragment_shader_source_array);

	GCHK(glBindAttribLocation(program, 0, "in_position"));
	GCHK(glBindAttribLocation(program, 1, "in_TexCoord"));

	link_program(program);

	GCHK(glViewport(0, 0, width, height));

	/* clear the color buffer */
	GCHK(glClearColor(0.5, 0.5, 0.5, 1.0));
	GCHK(glClear(GL_COLOR_BUFFER_BIT));

	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vVertices));
	GCHK(glEnableVertexAttribArray(0));

	GCHK(glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, vTexCoords));
	GCHK(glEnableVertexAttribArray(1));

	GCHK(glActiveTexture(GL_TEXTURE0));
	GCHK(glGenTextures(1, &texturename));
	GCHK(glBindTexture(type, texturename));

//void glTexImage3D (GLenum target, GLint level, GLint internalformat,
//		GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format,
//		GLenum type, const void *pixels);
	GCHK(glTexImage3D(type, 0, format,
			w, h, d, 0,
			format, GL_UNSIGNED_BYTE, pixel_data));

	/* Note: cube turned black until these were defined. */
	GCHK(glTexParameteri(type, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
	GCHK(glTexParameteri(type, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
	GCHK(glTexParameteri(type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	GCHK(glTexParameteri(type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
	GCHK(glTexParameteri(type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));

	GCHK(texture_handle = glGetUniformLocation(program, "uTexture"));

	GCHK(glUniform1i(texture_handle, 0)); /* '0' refers to texture unit 0. */

	GCHK(glEnable(GL_CULL_FACE));

	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	sleep(1);

	ECHK(eglDestroySurface(display, surface));

	ECHK(eglTerminate(display));

	free(pixel_data);

	RD_END();
}

int main(int argc, char *argv[])
{
	TEST_START();
	TEST(test_quad_textured(GL_TEXTURE_3D, GL_ALPHA, 4, 4, 4));
	TEST(test_quad_textured(GL_TEXTURE_2D_ARRAY, GL_ALPHA, 4, 4, 4));
	TEST(test_quad_textured(GL_TEXTURE_3D, GL_ALPHA, 33, 4, 4));
	TEST(test_quad_textured(GL_TEXTURE_3D, GL_ALPHA, 4, 33, 4));
	TEST(test_quad_textured(GL_TEXTURE_3D, GL_RGBA, 33, 4, 4));
	TEST(test_quad_textured(GL_TEXTURE_3D, GL_RGBA, 4, 33, 4));
	TEST(test_quad_textured(GL_TEXTURE_2D_ARRAY, GL_ALPHA, 33, 4, 4));
	TEST(test_quad_textured(GL_TEXTURE_2D_ARRAY, GL_ALPHA, 4, 33, 4));
	TEST(test_quad_textured(GL_TEXTURE_2D_ARRAY, GL_RGBA, 33, 4, 4));
	TEST(test_quad_textured(GL_TEXTURE_2D_ARRAY, GL_RGBA, 4, 33, 4));
	/* test larger sizes: */
	TEST(test_quad_textured(GL_TEXTURE_3D, GL_RGBA, 256, 256, 1));
	TEST(test_quad_textured(GL_TEXTURE_3D, GL_RGBA, 256, 256, 128));
	TEST(test_quad_textured(GL_TEXTURE_3D, GL_RGBA, 256, 256, 256));
	TEST(test_quad_textured(GL_TEXTURE_3D, GL_RGBA, 256, 256, 512));
	TEST(test_quad_textured(GL_TEXTURE_3D, GL_RGBA, 256, 256, 1024));
	TEST(test_quad_textured(GL_TEXTURE_3D, GL_RGBA, 512, 512, 512));
	TEST(test_quad_textured(GL_TEXTURE_2D_ARRAY, GL_RGBA, 256, 256, 4));
	TEST(test_quad_textured(GL_TEXTURE_2D_ARRAY, GL_RGBA, 512, 512, 4));
	TEST(test_quad_textured(GL_TEXTURE_2D_ARRAY, GL_RGBA, 2048, 2048, 4));
	TEST(test_quad_textured(GL_TEXTURE_2D_ARRAY, GL_RGBA, 4096, 4096, 4));
	TEST(test_quad_textured(GL_TEXTURE_2D_ARRAY, GL_RGBA, 32, 32, 32));
	TEST(test_quad_textured(GL_TEXTURE_2D_ARRAY, GL_RGBA, 32, 32, 256));
	TEST(test_quad_textured(GL_TEXTURE_2D_ARRAY, GL_RGBA, 32, 32, 512));
	TEST(test_quad_textured(GL_TEXTURE_2D_ARRAY, GL_RGBA, 32, 32, 1024));
	TEST(test_quad_textured(GL_TEXTURE_2D_ARRAY, GL_RGBA, 32, 32, 2048));

	TEST_END();

	return 0;
}

