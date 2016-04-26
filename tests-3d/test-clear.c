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
static EGLSurface surface;
static GLuint program;
static GLint width, height;
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
static void
test_clear(GLbitfield b, GLfloat clearcolor[4], GLint clearstencil, GLfloat cleardepth)
{
	RD_START("clear", "%08x: color={%f,%f,%f,%f}, stencil=0x%x, depth=%f",
		b, clearcolor[0], clearcolor[1], clearcolor[2], clearcolor[3],
		clearstencil, cleardepth);
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

	GCHK(glClearColor(clearcolor[0], clearcolor[1], clearcolor[2], clearcolor[3]));
	GCHK(glClearStencil(clearstencil));
	GCHK(glClearDepthf(cleardepth));
	GCHK(glClear(b));

	GCHK(glFlush());
	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	usleep(1000000);

	eglTerminate(display);

	RD_END();
}

int main(int argc, char *argv[])
{
	TEST_START();
	TEST(test_clear(GL_COLOR_BUFFER_BIT, (GLfloat[]) {0.1, 0.2, 0.3, 0.4}, 0x1, 0.75));
	TEST(test_clear(GL_COLOR_BUFFER_BIT, (GLfloat[]) {0.1, 0.2, 0.3, 0.4}, 0x1, 0.75));
	TEST(test_clear(GL_COLOR_BUFFER_BIT, (GLfloat[]) {0.1, 0.6, 0.3, 0.4}, 0xf1, 0.5));
	TEST(test_clear(GL_STENCIL_BUFFER_BIT, (GLfloat[]) {0.1, 0.2, 0.3, 0.4}, 0x1, 0.75));
	TEST(test_clear(GL_DEPTH_BUFFER_BIT, (GLfloat[]) {0.1, 0.2, 0.3, 0.4}, 0x1, 0.75));
	TEST(test_clear(GL_STENCIL_BUFFER_BIT|GL_DEPTH_BUFFER_BIT, (GLfloat[]) {0.1, 0.2, 0.3, 0.4}, 0x1, 0.75));
	TEST(test_clear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT|GL_DEPTH_BUFFER_BIT, (GLfloat[]) {0.1, 0.2, 0.3, 0.4}, 0x1, 0.75));
	TEST_END();
	return 0;
}

