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

/* Code copied from triangle_smoothed test from lima driver project adapted to the
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
// note: GL_MAX_VERTEX_ATTRIBS is 16
const char *vertex_shader_source =
		"attribute vec4 aPosition;    \n"
		"attribute vec4 aColor1;      \n"
		"attribute vec4 aColor2;      \n"
		"attribute vec4 aColor3;      \n"
		"attribute vec4 aColor4;      \n"
		"attribute vec4 aColor5;      \n"
		"attribute vec4 aColor6;      \n"
		"attribute vec4 aColor7;      \n"
		"attribute vec4 aColor8;      \n"
		"attribute vec4 aColor9;      \n"
		"attribute vec4 aColor10;     \n"
		"attribute vec4 aColor11;     \n"
		"attribute vec4 aColor12;     \n"
		"attribute vec4 aColor13;     \n"
		"attribute vec4 aColor14;     \n"
		"attribute vec4 aColor15;     \n"
		"                             \n"
		"varying vec4 vColor;         \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    vColor = aColor1 * aColor2 * aColor3 * aColor4 * \n"
		"             aColor5 * aColor6 * aColor7 * aColor8 * \n"
		"             aColor9 * aColor10 * aColor11 * aColor12 * \n"
		"             aColor13 * aColor14 * aColor15; \n"
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


/* test to study vertex fetch instructions/bindings */
void test_vertex(GLint *sizes, GLenum *types)
{
	GLint width, height;
	EGLint pbuffer_attribute_list[] = {
		EGL_WIDTH, 256,
		EGL_HEIGHT, 256,
		EGL_LARGEST_PBUFFER, EGL_TRUE,
		EGL_NONE
	};

	GLfloat vVertices[] = {
			 0.0f,  0.5f, 0.0f,
			-0.5f, -0.5f, 0.0f,
			 0.5f, -0.5f, 0.0f };
	GLfloat vColors1[] = {
			1.0f, 0.0f, 0.0f, 1.0f,
			0.0f, 1.0f, 0.0f, 1.0f,
			0.0f, 0.0f, 1.0f, 1.0f};
	GLfloat vColors2[] = {
			1.1f, 0.0f, 0.0f, 1.0f,
			0.0f, 1.0f, 0.0f, 1.0f,
			0.0f, 0.0f, 1.0f, 1.0f};
	GLfloat vColors3[] = {
			1.2f, 0.0f, 0.0f, 1.0f,
			0.0f, 1.0f, 0.0f, 1.0f,
			0.0f, 0.0f, 1.0f, 1.0f};
	GLfloat vColors4[] = {
			1.3f, 0.0f, 0.0f, 1.0f,
			0.0f, 1.0f, 0.0f, 1.0f,
			0.0f, 0.0f, 1.0f, 1.0f};
	EGLSurface surface;

	DEBUG_MSG("----------------------------------------------------------------");
	RD_START("vertex", "sizes: %d, %d, %d, %d, types: %04x, %04x, %04x, %04x",
			sizes[0], sizes[1], sizes[2], sizes[3],
			types[0], types[1], types[2], types[3]);

	ECHK(surface = eglCreatePbufferSurface(display, config, pbuffer_attribute_list));

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("PBuffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));
	GCHK(glFlush());

	if (!program) {
		program = get_program(vertex_shader_source, fragment_shader_source);

		GCHK(glBindAttribLocation(program, 0, "aPosition"));
		GCHK(glBindAttribLocation(program, 1, "aColor1"));
		GCHK(glBindAttribLocation(program, 2, "aColor2"));
		GCHK(glBindAttribLocation(program, 3, "aColor3"));
		GCHK(glBindAttribLocation(program, 4, "aColor4"));
		GCHK(glBindAttribLocation(program, 5, "aColor5"));
		GCHK(glBindAttribLocation(program, 6, "aColor6"));
		GCHK(glBindAttribLocation(program, 7, "aColor7"));
		GCHK(glBindAttribLocation(program, 8, "aColor8"));
		GCHK(glBindAttribLocation(program, 9, "aColor9"));
		GCHK(glBindAttribLocation(program, 10, "aColor10"));
		GCHK(glBindAttribLocation(program, 11, "aColor11"));
		GCHK(glBindAttribLocation(program, 12, "aColor12"));
		GCHK(glBindAttribLocation(program, 13, "aColor13"));
		GCHK(glBindAttribLocation(program, 14, "aColor14"));
		GCHK(glBindAttribLocation(program, 15, "aColor15"));

		link_program(program);
		GCHK(glFlush());
	}

	GCHK(glViewport(0, 0, width, height));
	GCHK(glFlush());


	/* clear the color buffer */
	GCHK(glClearColor(0.0, 0.0, 0.0, 1.0));
	GCHK(glFlush());
	GCHK(glClear(GL_COLOR_BUFFER_BIT));
	GCHK(glFlush());

	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vVertices));
	GCHK(glEnableVertexAttribArray(0));

	GCHK(glVertexAttribPointer(1, sizes[0], types[0], GL_FALSE, 0, vColors1));
	GCHK(glEnableVertexAttribArray(1));
	GCHK(glVertexAttribPointer(2, sizes[1], types[1], GL_FALSE, 0, vColors2));
	GCHK(glEnableVertexAttribArray(2));
	GCHK(glVertexAttribPointer(3, sizes[2], types[2], GL_FALSE, 0, vColors3));
	GCHK(glEnableVertexAttribArray(3));
	GCHK(glVertexAttribPointer(4, sizes[3], types[3], GL_FALSE, 0, vColors4));
	GCHK(glEnableVertexAttribArray(4));

	GCHK(glVertexAttribPointer(5, sizes[0], types[0], GL_FALSE, 0, vColors1));
	GCHK(glEnableVertexAttribArray(5));
	GCHK(glVertexAttribPointer(6, sizes[1], types[1], GL_FALSE, 0, vColors2));
	GCHK(glEnableVertexAttribArray(6));
	GCHK(glVertexAttribPointer(7, sizes[2], types[2], GL_FALSE, 0, vColors3));
	GCHK(glEnableVertexAttribArray(7));
	GCHK(glVertexAttribPointer(8, sizes[3], types[3], GL_FALSE, 0, vColors4));
	GCHK(glEnableVertexAttribArray(8));

	GCHK(glVertexAttribPointer(9, sizes[0], types[0], GL_FALSE, 0, vColors1));
	GCHK(glEnableVertexAttribArray(9));
	GCHK(glVertexAttribPointer(10, sizes[1], types[1], GL_FALSE, 0, vColors2));
	GCHK(glEnableVertexAttribArray(10));
	GCHK(glVertexAttribPointer(11, sizes[2], types[2], GL_FALSE, 0, vColors3));
	GCHK(glEnableVertexAttribArray(11));
	GCHK(glVertexAttribPointer(12, sizes[3], types[3], GL_FALSE, 0, vColors4));
	GCHK(glEnableVertexAttribArray(12));

	GCHK(glVertexAttribPointer(13, sizes[0], types[0], GL_FALSE, 0, vColors1));
	GCHK(glEnableVertexAttribArray(13));
	GCHK(glVertexAttribPointer(14, sizes[1], types[1], GL_FALSE, 0, vColors2));
	GCHK(glEnableVertexAttribArray(14));
	GCHK(glVertexAttribPointer(15, sizes[2], types[2], GL_FALSE, 0, vColors3));
	GCHK(glEnableVertexAttribArray(15));

	GCHK(glDrawArrays(GL_TRIANGLES, 0, 3));
	GCHK(glFlush());

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	ECHK(eglDestroySurface(display, surface));
	GCHK(glFlush());

	RD_END();
}

int main(int argc, char *argv[])
{
	display = get_display();

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	DEBUG_MSG("num_config: %d", num_config);

	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));

	/* ignore first test, to get rid of initial context setup */
	test_vertex((GLint[]){ 4, 4, 4, 4 },
			(GLenum[]){ GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT });

	test_vertex((GLint[]){ 4, 4, 4, 4 },
			(GLenum[]){ GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT });
	test_vertex((GLint[]){ 2, 2, 2, 2 },
			(GLenum[]){ GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT });
	test_vertex((GLint[]){ 4, 4, 4, 4 },
			(GLenum[]){ GL_BYTE, GL_BYTE, GL_BYTE, GL_BYTE });

	ECHK(eglTerminate(display));
}

#ifdef BIONIC
void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}
#endif
