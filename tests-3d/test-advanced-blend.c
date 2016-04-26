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

#ifndef GL_KHR_blend_equation_advanced
#define GL_KHR_blend_equation_advanced 1
#define GL_MULTIPLY_KHR                   0x9294
#define GL_SCREEN_KHR                     0x9295
#define GL_OVERLAY_KHR                    0x9296
#define GL_DARKEN_KHR                     0x9297
#define GL_LIGHTEN_KHR                    0x9298
#define GL_COLORDODGE_KHR                 0x9299
#define GL_COLORBURN_KHR                  0x929A
#define GL_HARDLIGHT_KHR                  0x929B
#define GL_SOFTLIGHT_KHR                  0x929C
#define GL_DIFFERENCE_KHR                 0x929E
#define GL_EXCLUSION_KHR                  0x92A0
#define GL_HSL_HUE_KHR                    0x92AD
#define GL_HSL_SATURATION_KHR             0x92AE
#define GL_HSL_COLOR_KHR                  0x92AF
#define GL_HSL_LUMINOSITY_KHR             0x92B0
GL_APICALL void GL_APIENTRY glBlendBarrierKHR (void);
#endif /* GL_KHR_blend_equation_advanced */

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
		"#version 300 es              \n"
		"in vec4 aPosition;           \n"
		"in vec4 aColor;              \n"
		"out vec4 vColor;             \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    vColor = aColor;         \n"
		"    gl_Position = aPosition; \n"
		"}                            \n";

const char *fragment_shader_source =
		"#version 300 es              \n"
		"#extension GL_KHR_blend_equation_advanced : enable\n"
		"precision highp float;       \n"
		"                             \n"
		"in vec4 vColor;              \n"
		"layout(blend_support_all_equations) out vec4 gl_FragColor;       \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_FragColor = vColor;   \n"
		"}                            \n";

void test_advanced_blend(GLenum blend)
{
	GLint width, height;

	GLfloat vVertices[] = {
			 0.0f,  0.5f, 0.0f,
			-0.5f, -0.5f, 0.0f,
			 0.5f, -0.5f, 0.0f };
	GLfloat vColors[] = {
			1.0f, 0.0f, 0.0f, 1.0f,
			0.0f, 1.0f, 0.0f, 1.0f,
			0.0f, 0.0f, 1.0f, 1.0f};
	EGLSurface surface;

	RD_START("advanced-blend", "");

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
	GCHK(glBindAttribLocation(program, 1, "aColor"));

	link_program(program);

	DEBUG_MSG("GL Version %s", glGetString(GL_VERSION));
	DEBUG_MSG("GL Extensions \"%s\"", glGetString(GL_EXTENSIONS));

	GCHK(glViewport(0, 0, width, height));

	/* clear the color buffer */
	GCHK(glClearColor(0.0, 0.0, 0.0, 1.0));
	GCHK(glEnable(GL_DEPTH_TEST));
	GCHK(glDepthFunc(GL_LEQUAL));
	GCHK(glEnable(GL_CULL_FACE));
	GCHK(glCullFace(GL_BACK));
	GCHK(glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT));

	if (blend) {
		GCHK(glBlendEquation(blend));
		GCHK(glEnable(GL_BLEND));
	} else {
		GCHK(glDisable(GL_BLEND));
	}

	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vVertices));
	GCHK(glEnableVertexAttribArray(0));

	GCHK(glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, vColors));
	GCHK(glEnableVertexAttribArray(1));

	GCHK(glDrawArrays(GL_TRIANGLES, 0, 3));

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	ECHK(eglDestroySurface(display, surface));

	ECHK(eglTerminate(display));

	RD_END();
}

int main(int argc, char *argv[])
{
	TEST_START();
	TEST(test_advanced_blend(0));
	TEST(test_advanced_blend(GL_MULTIPLY_KHR));
	TEST(test_advanced_blend(GL_SCREEN_KHR));
	TEST(test_advanced_blend(GL_OVERLAY_KHR));
	TEST(test_advanced_blend(GL_DARKEN_KHR));
	TEST(test_advanced_blend(GL_LIGHTEN_KHR));
	TEST(test_advanced_blend(GL_COLORDODGE_KHR));
	TEST(test_advanced_blend(GL_COLORBURN_KHR));
	TEST(test_advanced_blend(GL_HARDLIGHT_KHR));
	TEST(test_advanced_blend(GL_SOFTLIGHT_KHR));
	TEST(test_advanced_blend(GL_DIFFERENCE_KHR));
	TEST(test_advanced_blend(GL_EXCLUSION_KHR));
	TEST_END();
	return 0;
}

