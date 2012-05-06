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

/* Code copy triangle_quad test from lima driver project adapted to the
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
	EGL_SAMPLES, 4,
	EGL_NONE
};

static EGLint const pbuffer_attribute_list[] = {
	EGL_WIDTH, 400,
	EGL_HEIGHT, 240,
	EGL_LARGEST_PBUFFER, EGL_TRUE,
	EGL_NONE
};

static const EGLint context_attribute_list[] = {
	EGL_CONTEXT_CLIENT_VERSION, 2,
	EGL_NONE
};

void test_triangle_quad(void)
{
	EGLDisplay display;
	EGLConfig config;
	EGLint num_config;
	EGLContext context;
	EGLSurface surface;
	GLuint program;
	GLint width, height;
	int uniform_location;
	const char *vertex_shader_source =
		"precision mediump float;     \n"
		"attribute vec4 aPosition;    \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_Position = aPosition; \n"
		"}                            \n";

	const char *fragment_shader_source =
		"precision mediump float;     \n"
		"uniform vec4 uColor;         \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_FragColor = uColor;   \n"
		"}                            \n";

	GLfloat vertices[] = {
		/* triangle */
		-0.8,  0.50, 0.0,
		-0.2,  0.50, 0.0,
		-0.5, -0.50, 0.0,
		/* quad */
		0.2, -0.50, 0.0,
		0.8, -0.50, 0.0,
		0.2,  0.50, 0.0,
		0.8,  0.50, 0.0 };
	GLfloat triangle_color[] = {0.0, 1.0, 0.0, 1.0 };
	GLfloat quad_color[] = {1.0, 0.0, 0.0, 1.0 };


	DEBUG_MSG("----------------------------------------------------------------");
	RD_START("triangle-quad", "");
	display = get_display();

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	DEBUG_MSG("num_config: %d", num_config);

	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));

	ECHK(surface = eglCreatePbufferSurface(display, config, pbuffer_attribute_list));

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("PBuffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));
	GCHK(glFlush());

	program = get_program(vertex_shader_source, fragment_shader_source);

	GCHK(glBindAttribLocation(program, 0, "aPosition"));

	link_program(program);

	GCHK(glViewport(0, 0, width, height));
	GCHK(glFlush());

	/* clear the color buffer */
	GCHK(glClearColor(0.3125, 0.3125, 0.3125, 1.0));
	GCHK(glFlush());
	GCHK(glClear(GL_COLOR_BUFFER_BIT));
	GCHK(glFlush());

	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices));
	GCHK(glFlush());
	GCHK(glEnableVertexAttribArray(0));
	GCHK(glFlush());

	/* now set up our uniform. */
	GCHK(uniform_location = glGetUniformLocation(program, "uColor"));

	GCHK(glUniform4fv(uniform_location, 1, triangle_color));
	GCHK(glFlush());
	GCHK(glDrawArrays(GL_TRIANGLES, 0, 3));
	GCHK(glFlush());

	GCHK(glUniform4fv(uniform_location, 1, quad_color));
	GCHK(glFlush());
	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 3, 4));
	GCHK(glFlush());

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	usleep(1000000);

	RD_END();
}

int main(int argc, char *argv[])
{
	test_triangle_quad();
}

#ifdef BIONIC
void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}
#endif
