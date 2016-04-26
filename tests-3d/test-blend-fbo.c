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

#include <GLES3/gl3.h>

#include "test-util-3d.h"

static EGLint const config_attribute_list[] = {
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_NONE
};

static const EGLint context_attribute_list[] = {
	EGL_CONTEXT_CLIENT_VERSION, 3,
	EGL_NONE
};

static EGLDisplay display;
static EGLConfig config;
static EGLint num_config;
static EGLContext context;
static GLuint program;
static int uniform_location;
const char *vertex_shader_source =
	"#version 300 es              \n"
	"in vec4 aPosition;           \n"
	"                             \n"
	"void main()                  \n"
	"{                            \n"
	"    gl_Position = aPosition; \n"
	"}                            \n";
const char *fragment_shader_source =
	"#version 300 es              \n"
	"precision highp float;       \n"
	"uniform vec4 uColor;         \n"
	"out vec4 col0;               \n"
	"                             \n"
	"void main()                  \n"
	"{                            \n"
	"    col0 = uColor;           \n"
	"}                            \n";

/* Run through multiple variants to detect mrt settings
 */
void test_blend_fbo(GLint ifmt, GLenum fmt, GLenum type, int mode)
{
	int i;
	GLint width, height;
	GLuint fbo, fbotex;
	GLenum mrt_bufs[16];

	GLfloat quad_color[] =  {1.0, 0.0, 0.0, 1.0};
	GLfloat vertices[] = {
			-0.45, -0.75, 0.1,
			 0.45, -0.75, 0.1,
			-0.45,  0.75, 0.1,
			 0.45,  0.75, 0.1 };
	EGLSurface surface;

	RD_START("svga-fbo", "mrt[%d]: fmt=%s (%x), ifmt=%s (%x), type=%s (%x)", i,
			formatname(fmt), fmt,
			formatname(ifmt), ifmt,
			typename(type), type);

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

	GCHK(glGenFramebuffers(1, &fbo));
	GCHK(glGenTextures(1, &fbotex));
	GCHK(glBindFramebuffer(GL_FRAMEBUFFER, fbo));

	GCHK(glBindTexture(GL_TEXTURE_2D, fbotex));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
	GCHK(glTexImage2D(GL_TEXTURE_2D, 0, ifmt, width, height, 0, fmt, type, 0));
	GCHK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D, fbotex, 0));

	DEBUG_MSG("status=%04x", glCheckFramebufferStatus(GL_FRAMEBUFFER));

	GCHK(glBindFramebuffer(GL_FRAMEBUFFER, fbo));

	GCHK(glDrawBuffers(1, (const GLenum[]){GL_COLOR_ATTACHMENT0}));

	GCHK(glViewport(0, 0, width, height));

	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices));
	GCHK(glEnableVertexAttribArray(0));

	/* now set up our uniform. */
	GCHK(uniform_location = glGetUniformLocation(program, "uColor"));

	GCHK(glUniform4fv(uniform_location, 1, quad_color));

	if (mode == 0) {
		GCHK(glDisable(GL_BLEND));
	} else {
		GCHK(glEnable(GL_BLEND));
		GCHK(glBlendEquation(GL_FUNC_ADD));
		switch (mode) {
		case 1:
			GCHK(glBlendColor(0.1, 0.2, 0.3, 0.4));
			GCHK(glBlendFunc(GL_ZERO, GL_ZERO));
			break;
		case 2:
			GCHK(glBlendColor(0.2, 0.3, 0.4, 0.5));
			GCHK(glBlendFunc(GL_ZERO, GL_ONE));
			break;
		case 3:
			GCHK(glBlendColor(0.3, 0.4, 0.5, 0.6));
			GCHK(glBlendFunc(GL_ONE, GL_ZERO));
			break;
		case 4:
			GCHK(glBlendColor(0.4, 0.5, 0.6, 0.7));
			GCHK(glBlendFunc(GL_DST_COLOR, GL_DST_COLOR));
			break;
		case 5:
			GCHK(glBlendColor(0.5, 0.6, 0.7, 0.8));
			GCHK(glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_DST_COLOR));
			break;
		case 6:
			GCHK(glBlendColor(0.6, 0.7, 0.8, 0.9));
			GCHK(glBlendFunc(GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA));
			break;
		case 7:
			GCHK(glBlendColor(0.7, 0.8, 0.9, 1.0));
			GCHK(glBlendFunc(GL_SRC_ALPHA_SATURATE, GL_CONSTANT_COLOR));
			break;
		case 8:
			GCHK(glBlendColor(0.7, 0.8, 0.9, 1.0));
			GCHK(glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA));
			break;
		case 9:
			GCHK(glBlendColor(0.7, 0.8, 0.9, 1.0));
			GCHK(glBlendFunc(GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR));
			break;
		}
	}

	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

	GCHK(glFlush());

//	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	ECHK(eglDestroySurface(display, surface));

	ECHK(eglTerminate(display));

	RD_END();
}

int main(int argc, char *argv[])
{
	int i;
	TEST_START();
	for (i = 0; i < 10; i++) {
		TEST(test_blend_fbo(GL_R8,            GL_RED,          GL_UNSIGNED_BYTE, i));
		TEST(test_blend_fbo(GL_RGB8,          GL_RGB,          GL_UNSIGNED_BYTE, i));
		TEST(test_blend_fbo(GL_RGB16F,        GL_RGB,          GL_HALF_FLOAT   , i));
		TEST(test_blend_fbo(GL_RGBA8,         GL_RGBA,         GL_UNSIGNED_BYTE, i));
		TEST(test_blend_fbo(GL_RGB10_A2UI,    GL_RGBA_INTEGER, GL_UNSIGNED_INT_2_10_10_10_REV, i));
		TEST(test_blend_fbo(GL_RGBA8UI,       GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, i));
		TEST(test_blend_fbo(GL_RGB5_A1,       GL_RGBA,         GL_UNSIGNED_BYTE, i));
	}
	TEST_END();

	return 0;
}

