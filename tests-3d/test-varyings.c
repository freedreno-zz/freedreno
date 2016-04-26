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

static char * get_vs(int cnt)
{
	static char buf[40960];
	char *ptr = buf;
	int i, j;

	ptr += sprintf(ptr, "attribute vec4 aPosition;\n");
	ptr += sprintf(ptr, "uniform float i;\n");
	for (i = 0; i < cnt; i++)
		ptr += sprintf(ptr, "varying vec4 v%d;\n", i);
	ptr += sprintf(ptr, "void main()\n");
	ptr += sprintf(ptr, "{\n");
	for (i = 0; i < cnt; i++) {
		for (j = 0; j < 4; j++) {
			ptr += sprintf(ptr, "  v%d[%d] = i + %d.0;\n", i, j, (i * 4) + j);
		}
	}
	ptr += sprintf(ptr, "  gl_Position = aPosition;\n");
	ptr += sprintf(ptr, "}\n");

	return buf;
}

static char * get_fs(int cnt)
{
	static char buf[40960];
	char *ptr = buf;
	int i, j;

	ptr += sprintf(ptr, "precision highp float;\n");
	ptr += sprintf(ptr, "uniform float i;\n");
	for (i = 0; i < cnt; i++)
		ptr += sprintf(ptr, "varying vec4 v%d;\n", i);
	ptr += sprintf(ptr, "void main()\n");
	ptr += sprintf(ptr, "{\n");
	ptr += sprintf(ptr, "  bool failed = false;\n");
	for (i = 0; i < cnt; i++) {
		for (j = 0; j < 4; j++) {
			ptr += sprintf(ptr, "  if (v%d[%d] != (i + %d.0))\n", i, j, (i * 4) + j);
			ptr += sprintf(ptr, "    failed = true;\n");
		}
	}
	ptr += sprintf(ptr, "  if (failed)\n");
	ptr += sprintf(ptr, "    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n");
	ptr += sprintf(ptr, "  else\n");
	ptr += sprintf(ptr, "    gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n");
	ptr += sprintf(ptr, "}\n");

	return buf;
}


/* Run through multiple variants to detect clear color, quad color (frag
 * shader param), and vertices
 */
void test_varyings(int cnt)
{
	static const GLfloat vertices[] = {
			-0.45, -0.75, 0.0,
			 0.45, -0.75, 0.0,
			-0.45,  0.75, 0.0,
			 0.45,  0.75, 0.0
	};
	const char *vertex_shader_source = get_vs(cnt);
	const char *fragment_shader_source = get_fs(cnt);

	RD_START("varyings", "%d", cnt);
	display = get_display();

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	DEBUG_MSG("num_config: %d", num_config);

	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));

	surface = make_window(display, config, 64, 64);

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("Buffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));

	program = get_program(vertex_shader_source, fragment_shader_source);

	GCHK(glBindAttribLocation(program, 0, "aPosition"));

	link_program(program);

	GCHK(glViewport(0, 0, width, height));

	/* now set up our uniform. */
	GCHK(uniform_location = glGetUniformLocation(program, "i"));
	GCHK(glUniform1f(uniform_location, 0.0f));

	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices));
	GCHK(glEnableVertexAttribArray(0));

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
	TEST(test_varyings(1));
	TEST(test_varyings(2));
	TEST(test_varyings(3));
	TEST(test_varyings(4));
	TEST(test_varyings(5));
	TEST(test_varyings(6));
	TEST(test_varyings(7));
	TEST(test_varyings(8));
	TEST(test_varyings(9));
	TEST(test_varyings(10));
	TEST(test_varyings(11));
	TEST(test_varyings(12));
	TEST(test_varyings(13));
	TEST(test_varyings(14));
	TEST(test_varyings(15));
	TEST(test_varyings(16));
	TEST_END();

	return 0;
}

