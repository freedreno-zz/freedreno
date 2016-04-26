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

/* Code copied from strip_smoothed test from lima driver project adapted to the
 * logging that I use..
 */

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


void test_draw(GLenum mode)
{
	GLint width, height;
	static const GLfloat vVertices[512] = {
			-0.7,  0.7, -0.7,
			-0.7,  0.2, -0.4,
			 0.0,  0.3, -0.5,
			-0.2, -0.3,  0.3,
			 0.5, -0.2,  0.4,
			 0.7, -0.7,  0.7 };
	static const GLfloat vColors[512] = {
			0.1, 0.1, 0.1, 1.0,
			1.0, 0.0, 0.0, 1.0,
			0.0, 0.0, 1.0, 1.0,
			1.0, 1.0, 0.0, 1.0,
			0.0, 1.0, 1.0, 1.0,
			0.9, 0.9, 0.9, 1.0};
	static const char *modes[] = {
			"GL_POINTS",
			"GL_LINES",
			"GL_LINE_LOOP",
			"GL_LINE_STRIP",
			"GL_TRIANGLES",
			"GL_TRIANGLE_STRIP",
			"GL_TRIANGLE_FAN",
	};
	EGLSurface surface;

	RD_START("draw", "mode=%s", modes[mode]);

	display = get_display();

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	DEBUG_MSG("num_config: %d", num_config);

	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));

	surface = make_window(display, config, 128, 128);

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("Buffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));

	program = get_program(vertex_shader_source, fragment_shader_source);

	GCHK(glBindAttribLocation(program, 0, "aPosition"));
	GCHK(glBindAttribLocation(program, 1, "aColor"));

	link_program(program);

	GCHK(glViewport(0, 0, width, height));


	/* clear the color buffer */
	GCHK(glClearColor(0.3125, 0.3125, 0.3125, 1.0));
	GCHK(glClear(GL_COLOR_BUFFER_BIT));

	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vVertices));
	GCHK(glEnableVertexAttribArray(0));

	GCHK(glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, vColors));
	GCHK(glEnableVertexAttribArray(1));

	GCHK(glDrawArrays(mode, 0, 6));

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	ECHK(eglDestroySurface(display, surface));

	ECHK(eglTerminate(display));

	RD_END();
}

int main(int argc, char *argv[])
{
	TEST_START();
	TEST(test_draw(GL_POINTS));
	TEST(test_draw(GL_LINES));
	TEST(test_draw(GL_LINE_LOOP));
	TEST(test_draw(GL_LINE_STRIP));
	TEST(test_draw(GL_TRIANGLES));
	TEST(test_draw(GL_TRIANGLE_STRIP));
	TEST(test_draw(GL_TRIANGLE_FAN));
	TEST_END();

	return 0;
}

