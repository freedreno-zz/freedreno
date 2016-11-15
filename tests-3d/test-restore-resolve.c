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
 *
 * this one is similar to test-quad-flat but the parameter that is varied is
 * the pbuffer size, to observe how the driver splits up rendering of different
 * sizes when GMEM overflows..
 */

#include "test-util-3d.h"


static EGLint const config_attribute_list[] = {
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_ALPHA_SIZE, 8,
	EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_DEPTH_SIZE, 8,
	EGL_STENCIL_SIZE, 8,
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


/* Run through multiple variants to detect clear color, quad color (frag
 * shader param), and vertices
 */
void test_quad_flat2(int w, int h)
{
	GLint width, height;

	GLfloat quad_color[] =  {1.0, 0.0, 0.0, 1.0};
	GLfloat quad_color2[] = {1.0, 0.1, 0.2, 1.0};
	GLfloat vertices[64*3];
	EGLSurface surface;
	int i, j;

	RD_START("restore-resolve", "%dx%d", w, h);

	for (i = 0; i < ARRAY_SIZE(vertices); i++)
		vertices[i] = (drand48() * 2.0) - 1.0;

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

	GCHK(glViewport(0, 0, width, height));

	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices));
	GCHK(glEnableVertexAttribArray(0));

	/* now set up our uniform. */
	GCHK(uniform_location = glGetUniformLocation(program, "uColor"));

	GCHK(glClearDepthf(0.13));
	GCHK(glClearStencil(9));
	GCHK(glClearColor(0.1, 0.2, 0.3, 0.4));
	GCHK(glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT));
	GCHK(glFlush());
	readback();

	GCHK(glDepthFunc(GL_ALWAYS));
	GCHK(glEnable(GL_DEPTH_TEST));

	GCHK(glStencilFunc(GL_ALWAYS, 0x128, 0x34));
	GCHK(glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE));
	GCHK(glEnable(GL_STENCIL_TEST));

	GCHK(glEnable(GL_BLEND));

#define NDRAWS 10

	for (i = 0; i < 2; i++) {
		for (j = 0; j < NDRAWS; j++) {
			GCHK(glUniform4fv(uniform_location, 1, (j & 1) ? quad_color : quad_color2));
			GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));
		}
		GCHK(glFlush());
		readback();
	}

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	ECHK(eglDestroySurface(display, surface));

	ECHK(eglTerminate(display));

	RD_END();
}

int main(int argc, char *argv[])
{
	int i;
	TEST_START();
	TEST(test_quad_flat2(64, 64));
	TEST(test_quad_flat2(400, 240));
	TEST(test_quad_flat2(500, 240));
	TEST(test_quad_flat2(400, 340));
	TEST(test_quad_flat2(800, 600));
	for (i = 1; i < 2048; i *= 2)
		TEST(test_quad_flat2(64, i));
	for (i = 1; i < 2048; i *= 2)
		TEST(test_quad_flat2(i, 64));
	TEST_END();

	return 0;
}

