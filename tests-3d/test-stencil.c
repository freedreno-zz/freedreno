/*
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

/* Code based on example from https://code.google.com/p/opengles-book-samples/
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


static const GLfloat vVertices[] = {
		-0.75f, +0.25f, +0.50f, // Quad #0
		-0.25f, +0.25f, +0.50f,
		-0.25f, +0.75f, +0.50f,
		-0.75f, +0.75f, +0.50f,
		+0.25f, +0.25f, +0.90f, // Quad #1
		+0.75f, +0.25f, +0.90f,
		+0.75f, +0.75f, +0.90f,
		+0.25f, +0.75f, +0.90f,
		-0.75f, -0.75f, +0.50f, // Quad #2
		-0.25f, -0.75f, +0.50f,
		-0.25f, -0.25f, +0.50f,
		-0.75f, -0.25f, +0.50f,
		+0.25f, -0.75f, +0.50f, // Quad #3
		+0.75f, -0.75f, +0.50f,
		+0.75f, -0.25f, +0.50f,
		+0.25f, -0.25f, +0.50f,
		-1.00f, -1.00f, +0.00f, // Big Quad
		+1.00f, -1.00f, +0.00f,
		+1.00f, +1.00f, +0.00f,
		-1.00f, +1.00f, +0.00f
};

static const GLubyte indices[][6] = {
		{  0,  1,  2,  0,  2,  3 }, // Quad #0
		{  4,  5,  6,  4,  6,  7 }, // Quad #1
		{  8,  9, 10,  8, 10, 11 }, // Quad #2
		{ 12, 13, 14, 12, 14, 15 }, // Quad #3
		{ 16, 17, 18, 16, 18, 19 }  // Big Quad
};

#define NumTests  4
static const GLfloat colors[NumTests][4] = {
		{ 1.0f, 0.0f, 0.0f, 1.0f },
		{ 0.0f, 1.0f, 0.0f, 1.0f },
		{ 0.0f, 0.0f, 1.0f, 1.0f },
		{ 1.0f, 1.0f, 0.0f, 0.0f }
};


void test_stencil(void)
{
	GLint numStencilBits;
	GLuint stencilValues[NumTests] = {
			0x7, // Result of test 0
			0x0, // Result of test 1
			0x2, // Result of test 2
			0xff // Result of test 3.  We need to fill this value in a run-time
	};
	int i;

	RD_START("stencil", "");

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

	/* now set up our uniform. */
	GCHK(uniform_location = glGetUniformLocation(program, "uColor"));

	GCHK(glClearColor(0.0, 0.0, 0.0, 0.0));
	GCHK(glClearStencil(0x1));
	GCHK(glClearDepthf(0.75));

	GCHK(glEnable(GL_DEPTH_TEST));
	GCHK(glEnable(GL_STENCIL_TEST));
	GCHK(glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX));

	// Set the viewport
	GCHK(glViewport(0, 0, width, height));

	// Clear the color, depth, and stencil buffers.  At this
	//   point, the stencil buffer will be 0x1 for all pixels
	GCHK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));

	// Use the program object
	GCHK(glUseProgram(program));

	// Load the vertex position
	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vVertices));

	GCHK(glEnableVertexAttribArray(0));

	// Test 0:
	//
	// Initialize upper-left region.  In this case, the
	//   stencil-buffer values will be replaced because the
	//   stencil test for the rendered pixels will fail the
	//   stencil test, which is
	//
	//        ref   mask   stencil  mask
	//      ( 0x7 & 0x3 ) < ( 0x1 & 0x7 )
	//
	//   The value in the stencil buffer for these pixels will
	//   be 0x7.
	//
	GCHK(glStencilFunc(GL_LESS, 0x7, 0x3));
	GCHK(glStencilOp(GL_REPLACE, GL_DECR, GL_DECR));
	GCHK(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, indices[0]));

	// Test 1:
	//
	// Initialize the upper-right region.  Here, we'll decrement
	//   the stencil-buffer values where the stencil test passes
	//   but the depth test fails.  The stencil test is
	//
	//        ref  mask    stencil  mask
	//      ( 0x3 & 0x3 ) > ( 0x1 & 0x3 )
	//
	//    but where the geometry fails the depth test.  The
	//    stencil values for these pixels will be 0x0.
	//
	GCHK(glStencilFunc(GL_GREATER, 0x3, 0x3));
	GCHK(glStencilOp(GL_KEEP, GL_DECR, GL_KEEP));
	GCHK(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, indices[1]));

	// Test 2:
	//
	// Initialize the lower-left region.  Here we'll increment
	//   (with saturation) the stencil value where both the
	//   stencil and depth tests pass.  The stencil test for
	//   these pixels will be
	//
	//        ref  mask     stencil  mask
	//      ( 0x1 & 0x3 ) == ( 0x1 & 0x3 )
	//
	//   The stencil values for these pixels will be 0x2.
	//
	GCHK(glStencilFunc(GL_EQUAL, 0x1, 0x3));
	GCHK(glStencilOp(GL_KEEP, GL_INCR, GL_INCR));
	GCHK(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, indices[2]));

	// Test 3:
	//
	// Finally, initialize the lower-right region.  We'll invert
	//   the stencil value where the stencil tests fails.  The
	//   stencil test for these pixels will be
	//
	//        ref   mask    stencil  mask
	//      ( 0x2 & 0x1 ) == ( 0x1 & 0x1 )
	//
	//   The stencil value here will be set to ~((2^s-1) & 0x1),
	//   (with the 0x1 being from the stencil clear value),
	//   where 's' is the number of bits in the stencil buffer
	//
	GCHK(glStencilFunc(GL_EQUAL, 0x2, 0x1));
	GCHK(glStencilOp(GL_INVERT, GL_KEEP, GL_KEEP));
	GCHK(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, indices[3]));

	// Since we don't know at compile time how many stencil bits are present,
	//   we'll query, and update the value correct value in the
	//   stencilValues arrays for the fourth tests.  We'll use this value
	//   later in rendering.
	GCHK(glGetIntegerv(GL_STENCIL_BITS, &numStencilBits));

	stencilValues[3] = ~(((1 << numStencilBits) - 1) & 0x1) & 0xff;

	// Use the stencil buffer for controlling where rendering will
	//   occur.  We disable writing to the stencil buffer so we
	//   can test against them without modifying the values we
	//   generated.
	GCHK(glStencilMask(0x0));

	for (i = 0; i < NumTests; i++) {
		GCHK(glStencilFunc(GL_EQUAL, stencilValues[i], 0xff));
		GCHK(glUniform4fv(uniform_location, 1, colors[i]));
		GCHK(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, indices[4]));
	}

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	ECHK(eglDestroySurface(display, surface));

	usleep(1000000);

//	dump_bmp(display, surface, "stencil.bmp");

	ECHK(eglTerminate(display));

	RD_END();
}

int main(int argc, char *argv[])
{
	TEST_START();
	TEST(test_stencil());
	TEST_END();

	return 0;
}

