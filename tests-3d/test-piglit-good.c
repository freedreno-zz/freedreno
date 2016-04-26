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
	EGL_BUFFER_SIZE, 4,
	EGL_RED_SIZE, 1,
	EGL_GREEN_SIZE, 1,
	EGL_BLUE_SIZE, 1,
	EGL_ALPHA_SIZE, 1,
	EGL_DEPTH_SIZE, 0,
	EGL_STENCIL_SIZE, 0,
	EGL_SAMPLE_BUFFERS, 0,
	EGL_SAMPLES, 0,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
	EGL_NONE,
};

static const EGLint context_attribute_list[] = {
	EGL_CONTEXT_CLIENT_VERSION, 2,
//	EGL_CONTEXT_MINOR_VERSION_KHR, 0,
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
"#version 100                                                                        \n"
"attribute vec4 vertex;                                                              \n"
"mat4 projection = mat4(                                                             \n"
"    2.0/250.0, 0.0, 0.0, -1.0,                                                      \n"
"    0.0, 2.0/250.0, 0.0, -1.0,                                                      \n"
"    0.0, 0.0, -1.0, 0.0,                                                            \n"
"    0.0, 0.0, 0.0, 1.0);                                                            \n"
"uniform int row;                                                                    \n"
"uniform float expect;                                                               \n"
"varying mat2 m;                                                                     \n"
"varying vec4 color;                                                                 \n"
"                                                                                    \n"
"void main()                                                                         \n"
"{                                                                                   \n"
"    gl_Position = vertex;                                                           \n"
"    gl_Position *= projection;                                                      \n"
"                                                                                    \n"
"    m = mat2(1.0, 2.0, 3.0, 4.0);                                                   \n"
"                                                                                    \n"
"    /* From page 23 (page 30 of the PDF) of the GLSL 1.10 spec:                     \n"
"     *                                                                              \n"
"     *     \"A vertex shader may also read varying variables, getting back the      \n"
"     *     same values it has written. Reading a varying variable in a vertex       \n"
"     *     shader returns undefined values if it is read before being               \n"
"     *     written.\"                                                               \n"
"     */                                                                             \n"
"    color = (m[1][row] == expect)                                                   \n"
"        ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);                      \n"
"}                                                                                   \n";

const char *fragment_shader_source =
"#version 100                                                                        \n"
"precision mediump float;                                                            \n"
"precision mediump int;                                                              \n"
"precision highp float;                                                              \n"
"precision highp int;                                                                \n"
"uniform int row;                                                                    \n"
"uniform float expect;                                                               \n"
"varying mat2 m;                                                                     \n"
"varying vec4 color;                                                                 \n"
"                                                                                    \n"
"void main()                                                                         \n"
"{                                                                                   \n"
"    /* There is some trickery here.  The fragment shader has to actually use        \n"
"     * the varyings generated by the vertex shader, or the compiler (more           \n"
"     * likely the linker) might demote the varying outputs to just be vertex        \n"
"     * shader global variables.  Since the point of the test is the vertex          \n"
"     * shader reading from a varying, that would defeat the test.                   \n"
"     */                                                                             \n"
"    gl_FragColor = (m[1][row] == expect)                                            \n"
"        ? color : vec4(1.0, 0.0, 0.0, 1.0);                                         \n"
"}                                                                                   \n";

void test_piglit(void)
{
	RD_START("piglit-good", "");
	display = get_display();

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	DEBUG_MSG("num_config: %d", num_config);

	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));

	surface = make_window(display, config, 250, 250);

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("Buffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));

	program = get_program(vertex_shader_source, fragment_shader_source);

	GCHK(glBindAttribLocation(program, 0, "vertex"));

	link_program(program);

	GCHK(glViewport(0, 0, width, height));

	/* clear the color buffer */
	GCHK(glClearColor(0.5, 0.5, 0.5, 0.5));
	GCHK(glClear(GL_COLOR_BUFFER_BIT));

	GCHK(glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, (GLfloat[]){
		20.0, 5.0, 0.0, 1.0, 30.0, 5.0, 0.0, 1.0, 20.0, 15.0, 0.0, 1.0, 30.0, 15.0, 0.0, 1.0
	}));
	GCHK(glEnableVertexAttribArray(0));

	/* now set up our uniforms. */
	GCHK(uniform_location = glGetUniformLocation(program, "row"));
	GCHK(glUniform1i(uniform_location, 1));
	GCHK(uniform_location = glGetUniformLocation(program, "expect"));
	GCHK(glUniform1fv(uniform_location, 1, (GLfloat[]){ 4 }));

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
	TEST(test_piglit());
	TEST_END();

	return 0;
}

