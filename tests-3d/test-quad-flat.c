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
void test_quad_flat(GLfloat *clear_color, GLfloat *quad_color, GLfloat *vertices)
{
	RD_START("quad-flat", "");
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

	GCHK(glBindAttribLocation(program, 0, "aPosition"));

	link_program(program);

	GCHK(glViewport(0, 0, width, height));

	if (clear_color) {
		/* clear the color buffer */
		GCHK(glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]));
		GCHK(glClear(GL_COLOR_BUFFER_BIT));
	}

	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices));
	GCHK(glEnableVertexAttribArray(0));

	/* now set up our uniform. */
	GCHK(uniform_location = glGetUniformLocation(program, "uColor"));

	GCHK(glUniform4fv(uniform_location, 1, quad_color));
	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	usleep(1000000);

	eglTerminate(display);

	RD_END();
}

int main(int argc, char *argv[])
{
	TEST_START();
	TEST(test_quad_flat(NULL,
			(GLfloat[]) {1.0, 0.0, 0.0, 1.0},
			(GLfloat[]) {
				-0.45, -0.75, 0.0,
				 0.45, -0.75, 0.0,
				-0.45,  0.75, 0.0,
				 0.45,  0.75, 0.0}));
	TEST(test_quad_flat((GLfloat[]){0.3125, 0.3125, 0.3125, 1.0},
			(GLfloat[]) {1.0, 0.0, 0.0, 1.0},
			(GLfloat[]) {
				-0.45, -0.75, 0.0,
				 0.45, -0.75, 0.0,
				-0.45,  0.75, 0.0,
				 0.45,  0.75, 0.0}));
	TEST(test_quad_flat((GLfloat[]){0.5125, 0.4125, 0.3125, 0.5},
			(GLfloat[]) {1.0, 0.0, 0.0, 1.0},
			(GLfloat[]) {
				-0.45, -0.75, 0.0,
				 0.45, -0.75, 0.0,
				-0.45,  0.75, 0.0,
				 0.45,  0.75, 0.0}));
	TEST(test_quad_flat((GLfloat[]){0.5125, 0.4125, 0.3125, 0.5},
			(GLfloat[]) {0.1, 0.2, 0.3, 0.4},
			(GLfloat[]) {
				-0.45, -0.75, 0.0,
				 0.45, -0.75, 0.0,
				-0.45,  0.75, 0.0,
				 0.45,  0.75, 0.0}));
	TEST(test_quad_flat(NULL,
			(GLfloat[]) {0.1, 0.2, 0.3, 0.4},
			(GLfloat[]) {
				-0.15, -0.23, 0.12,
				 0.25, -0.33, 0.22,
				-0.35,  0.43, 0.32,
				 0.45,  0.53, 0.42}));
	TEST_END();

	return 0;
}

