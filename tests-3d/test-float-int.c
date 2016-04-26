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

#include "cubetex.h"

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
const char *vert_float_float =
		"#version 300 es              \n"
		"in vec4 in_position;         \n"
		"in vec2 in_TexCoord;         \n"
		"\n"
		"out vec2 vTexCoord;          \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_Position = in_position;\n"
		"    vTexCoord = in_TexCoord; \n"
		"}                            \n";

const char *vert_int_float =
		"#version 300 es              \n"
		"in vec4 in_position;         \n"
		"flat in ivec2 in_TexCoord;   \n"
		"\n"
		"out vec2 vTexCoord;          \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_Position = in_position;\n"
		"    vTexCoord = vec2(in_TexCoord); \n"
		"}                            \n";

const char *vert_float_int =
		"#version 300 es              \n"
		"in vec4 in_position;         \n"
		"in vec2 in_TexCoord;         \n"
		"\n"
		"flat out ivec2 vTexCoord;    \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_Position = in_position;\n"
		"    vTexCoord = ivec2(in_TexCoord); \n"
		"}                            \n";

const char *vert_int_int =
		"#version 300 es              \n"
		"in vec4 in_position;         \n"
		"flat in ivec2 in_TexCoord;   \n"
		"\n"
		"flat out ivec2 vTexCoord;    \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_Position = in_position;\n"
		"    vTexCoord = in_TexCoord; \n"
		"}                            \n";

const char *frag_float =
		"#version 300 es              \n"
		"precision mediump float;     \n"
		"                             \n"
		"uniform sampler2D uTexture;  \n"
		"in vec2 vTexCoord;           \n"
		"out vec4 gl_FragColor;       \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_FragColor = texture(uTexture, vTexCoord);\n"
		"}                            \n";

const char *frag_int =
		"#version 300 es              \n"
		"precision mediump float;     \n"
		"                             \n"
		"uniform sampler2D uTexture;  \n"
		"flat in ivec2 vTexCoord;     \n"
		"out vec4 gl_FragColor;       \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_FragColor = texture(uTexture, vec2(vTexCoord));\n"
		"}                            \n";


void test_float_int(int test)
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

	EGLSurface surface;
	const char *frag, *vert, *desc;

	switch (test) {
	case 0:
		frag = frag_float;
		vert = vert_float_float;
		desc = "float attribute, float varying";
		break;
	case 1:
		frag = frag_float;
		vert = vert_int_float;
		desc = "int attribute, float varying";
		break;
	case 2:
		frag = frag_int;
		vert = vert_float_int;
		desc = "float attribute, int varying";
		break;
	case 3:
		frag = frag_int;
		vert = vert_int_int;
		desc = "int attribute, int varying";
		break;
	default:
		return;
	}

	RD_START("float-int", "%s", desc);

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

	program = get_program(vert, frag);

	GCHK(glBindAttribLocation(program, 0, "in_position"));
	GCHK(glBindAttribLocation(program, 1, "in_TexCoord"));

	link_program(program);

	GCHK(glViewport(0, 0, width, height));

	/* clear the color buffer */
	GCHK(glClearColor(0.5, 0.5, 0.5, 1.0));
	GCHK(glClear(GL_COLOR_BUFFER_BIT));

	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vVertices));
	GCHK(glEnableVertexAttribArray(0));

	switch (test) {
	case 0:
	case 2:
		GCHK(glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, vTexCoords));
		break;
	case 1:
	case 3:
		GCHK(glVertexAttribIPointer(1, 2, GL_INT, 0, vTexCoords));
		break;
	}
	GCHK(glEnableVertexAttribArray(1));

	GCHK(glActiveTexture(GL_TEXTURE0));
	GCHK(glGenTextures(1, &texturename));
	GCHK(glBindTexture(GL_TEXTURE_2D, texturename));

	GCHK(glTexImage2D(
			GL_TEXTURE_2D, 0, GL_RGB,
			cube_texture.width, cube_texture.height, 0,
			GL_RGB, GL_UNSIGNED_BYTE, cube_texture.pixel_data));

	/* Note: cube turned black until these were defined. */
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));

	GCHK(texture_handle = glGetUniformLocation(program, "uTexture"));

	GCHK(glUniform1i(texture_handle, 0)); /* '0' refers to texture unit 0. */

	GCHK(glEnable(GL_CULL_FACE));

	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	sleep(1);

	ECHK(eglDestroySurface(display, surface));

	ECHK(eglTerminate(display));

	RD_END();
}

int main(int argc, char *argv[])
{
	TEST_START();
	TEST(test_float_int(0));
	TEST(test_float_int(1));
	TEST(test_float_int(2));
	TEST(test_float_int(3));
	TEST_END();

	return 0;
}

