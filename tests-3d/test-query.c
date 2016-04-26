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

/* Code copied from triangle_quad test from lima driver project adapted to the
 * logging that I use..
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
static EGLSurface surface;
static GLuint program;
static GLint width, height;
static GLuint query;
static int uniform_location;
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

#ifndef GL_TIME_ELAPSED_EXT
#define GL_TIME_ELAPSED_EXT               0x88BF
#endif

/* Run through multiple variants to detect clear color, quad color (frag
 * shader param), and vertices
 */
void test_query(int querytype, int w, int h)
{
	static const GLfloat clear_color[] = {0.0, 0.0, 0.0, 0.0};
	static const GLfloat quad_color[]  = {1.0, 0.0, 0.0, 1.0};
	static const GLfloat quad2_color[]  = {0.0, 1.0, 0.0, 1.0};
	static const GLfloat vertices[] = {
			-0.45, -0.75, 0.0,
			 0.45, -0.75, 0.0,
			-0.45,  0.75, 0.0,
			 0.45,  0.75, 0.0,
	};
	static const GLfloat vertices2[] = {
			-0.15, -0.23, 1.0,
			 0.25, -0.33, 1.0,
			-0.35,  0.43, 1.0,
			 0.45,  0.53, 1.0,
	};
	static const char *queryname[] = {
			"none",
			"samples-passed",
			"time-elapsed",
	};

	RD_START("query", "query=%s", queryname[querytype]);
	display = get_display();

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	DEBUG_MSG("num_config: %d", num_config);

	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));

	surface = make_window(display, config, w, h);

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("Buffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));

	program = get_program(vertex_shader_source, fragment_shader_source);

	GCHK(glBindAttribLocation(program, 0, "aPosition"));

	link_program(program);

	GCHK(glGenQueries(1, &query));

	GCHK(glDepthMask(GL_TRUE));
	GCHK(glEnable(GL_DEPTH_TEST));

	GCHK(glViewport(0, 0, width, height));

	if (clear_color) {
		/* clear the color buffer */
		GCHK(glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]));
		GCHK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
	}

	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices));
	GCHK(glEnableVertexAttribArray(0));

	/* now set up our uniform. */
	GCHK(uniform_location = glGetUniformLocation(program, "uColor"));

	GCHK(glUniform4fv(uniform_location, 1, quad_color));
	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

	switch (querytype) {
	case 1:
		GCHK(glBeginQuery(GL_ANY_SAMPLES_PASSED, query));
		break;
	case 2:
		GCHK(glBeginQuery(GL_TIME_ELAPSED_EXT, query));
		break;
	}

	/* Quad 2 render */
	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices2));

	/* now set up our uniform. */
	GCHK(uniform_location = glGetUniformLocation(program, "uColor"));

	GCHK(glUniform4fv(uniform_location, 1, quad2_color));
	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

	switch (querytype) {
	case 1:
		GCHK(glEndQuery(GL_ANY_SAMPLES_PASSED));
		break;
	case 2:
		GCHK(glEndQuery(GL_TIME_ELAPSED_EXT));
		break;
	}

	if (querytype > 0) {
		GLuint result;
		do
		{
			GCHK(glGetQueryObjectuiv(query, GL_QUERY_RESULT_AVAILABLE, &result));
		} while (!result);
		GCHK(glGetQueryObjectuiv(query, GL_QUERY_RESULT, &result));

		DEBUG_MSG("Query ended with %d", result);

	}

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	usleep(1000000);

	GCHK(glDeleteQueries(1, &query));

	eglTerminate(display);

	RD_END();
}

int main(int argc, char *argv[])
{
	TEST_START();
	TEST(test_query(0,  400,  240));
	TEST(test_query(1,  400,  240));
	TEST(test_query(2,  400,  240));
	TEST(test_query(0,  800,  600));
	TEST(test_query(1,  800,  600));
	TEST(test_query(2,  800,  600));
	TEST(test_query(0, 1920, 1080));
	TEST(test_query(1, 1920, 1080));
	TEST(test_query(2, 1920, 1080));
	TEST_END();

	return 0;
}

