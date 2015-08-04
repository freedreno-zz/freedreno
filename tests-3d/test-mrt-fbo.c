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
#include "cubetex.h"

#define MAX_MRT 8 /* 4 for a3xx, 8 for a4xx.. */

static EGLint const config_attribute_list[] = {
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_NONE
};

static const EGLint context_attribute_list[] = {
	EGL_CONTEXT_CLIENT_VERSION, 2,
	EGL_NONE
};

static inline uint32_t env2u(const char *envvar)
{
	char *str = getenv(envvar);
	if (str)
		return strtoul(str, NULL, 0);
	return 0;
}

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
	"out vec4 col1;               \n"
	"out vec4 col2;               \n"
	"out vec4 col3;               \n"
	"out vec4 col4;               \n"
	"out vec4 col5;               \n"
	"out vec4 col6;               \n"
	"out vec4 col7;               \n"
	"                             \n"
	"void main()                  \n"
	"{                            \n"
	"    col0 = uColor;           \n"
	"    col1 = col0 * 0.5;       \n"
	"    col2 = col1 * 0.5;       \n"
	"    col3 = col2 * 0.5;       \n"
#if (MAX_MRT >= 8)
	"    col4 = col3 * 0.5;       \n"
	"    col5 = col4 * 0.5;       \n"
	"    col6 = col5 * 0.5;       \n"
	"    col7 = col6 * 0.5;       \n"
#endif
	"}                            \n";


static const GLenum bufs[16] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
		GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4,
		GL_COLOR_ATTACHMENT5, GL_COLOR_ATTACHMENT6, GL_COLOR_ATTACHMENT7,
		GL_COLOR_ATTACHMENT8, GL_COLOR_ATTACHMENT9, GL_COLOR_ATTACHMENT10,
		GL_COLOR_ATTACHMENT11, GL_COLOR_ATTACHMENT12, GL_COLOR_ATTACHMENT13,
		GL_COLOR_ATTACHMENT14, GL_COLOR_ATTACHMENT15};


/* Run through multiple variants to detect mrt settings
 */
void test_mrt_fbo(unsigned w, unsigned h, unsigned type, int mrt, unsigned mask, int zs)
{
	int i;
	GLint width, height;
	GLuint fbo, fbotex[1+MAX_MRT];
	GLenum mrt_bufs[16];

	GLfloat quad_color[] =  {1.0, 0.0, 0.0, 1.0};
	GLfloat quad_color2[] =  {1.0, 0.0, 1.0, 1.0};
	GLfloat vertices[] = {
			-0.45, -0.75, 0.0,
			 0.45, -0.75, 0.0,
			-0.45,  0.75, 0.0,
			 0.45,  0.75, 0.0 };
	EGLSurface surface;

	RD_START("mrt-fbo", "%dx%d, mrt=%04x, mask=%04x, zs=%d", w, h, mrt, mask, zs);

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

	GCHK(glGenFramebuffers(1, &fbo));
	GCHK(glGenTextures(mrt+1, fbotex));

	for (i = 0; i < mrt; i++) {
		GCHK(glBindTexture(GL_TEXTURE_2D, fbotex[i]));
		GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
		GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
		GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
		GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
		GCHK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, type, 0));
	}

	if (zs) {
		GLenum intfmt, fmt, type;
		DEBUG_MSG("zs=%d\n", zs);
		switch (zs) {
		default:
		case 1:
			intfmt = GL_DEPTH24_STENCIL8;
			fmt = GL_DEPTH_STENCIL;
			type = GL_UNSIGNED_INT_24_8;
			GCHK(glEnable(GL_DEPTH_TEST));
			GCHK(glEnable(GL_STENCIL_TEST));
			break;
		case 2:
			intfmt = GL_DEPTH32F_STENCIL8;
			fmt = GL_DEPTH_STENCIL;
			type = GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
			GCHK(glEnable(GL_DEPTH_TEST));
			GCHK(glEnable(GL_STENCIL_TEST));
			break;
		case 3:
			intfmt = GL_DEPTH_COMPONENT32F;
			fmt = GL_DEPTH_COMPONENT;
			type = GL_FLOAT;
			GCHK(glEnable(GL_DEPTH_TEST));
			break;
		case 4:
			intfmt = GL_DEPTH_COMPONENT24;
			fmt = GL_DEPTH_COMPONENT;
			type = GL_UNSIGNED_INT;
			GCHK(glEnable(GL_DEPTH_TEST));
			break;
		}
		GCHK(glBindTexture(GL_TEXTURE_2D, fbotex[mrt]));
		GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
		GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
		GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
		GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
		GCHK(glTexImage2D(GL_TEXTURE_2D, 0, intfmt, width, height, 0, fmt,
				type, cube_texture.pixel_data));
	}

	GCHK(glBindFramebuffer(GL_FRAMEBUFFER, fbo));
	for (i = 0; i < mrt; i++)
		GCHK(glFramebufferTexture2D(GL_FRAMEBUFFER, bufs[i], GL_TEXTURE_2D, fbotex[i], 0));

	if (zs) {
		GCHK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
				GL_TEXTURE_2D, fbotex[mrt], 0));
		if (zs < 3) {
			GCHK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
					GL_TEXTURE_2D, fbotex[mrt], 0));
		}
	}

	DEBUG_MSG("status=%04x", glCheckFramebufferStatus(GL_FRAMEBUFFER));

	GCHK(glBindFramebuffer(GL_FRAMEBUFFER, fbo));
	GCHK(glViewport(0, 0, width, height));

	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices));
	GCHK(glEnableVertexAttribArray(0));

	/* now set up our uniform. */
	GCHK(uniform_location = glGetUniformLocation(program, "uColor"));

#if 0
	GCHK(glEnable(GL_BLEND));
	GCHK(glBlendFunc(GL_ONE, GL_ONE));

	GCHK(glDepthFunc(GL_LEQUAL));
	GCHK(glStencilFunc(GL_LEQUAL, 0x12, 0x34));
#endif

	for (i = 0; i < mrt; i++) {
		if (mask & (1 << i)) {
			mrt_bufs[i] = GL_NONE;
		} else {
			mrt_bufs[i] = bufs[i];
		}
	}

	if (mrt > 0)
		GCHK(glDrawBuffers(mrt, mrt_bufs));

	for (i = 0; i < mrt; i++) {
		float f = (float)i / (float)MAX_MRT;
		float clear[] = { f, f, f, 1.0 };
		if (!(mask & (1 << i)))
			GCHK(glClearBufferfv(GL_COLOR, i, clear));
	}

	GCHK(glUniform4fv(uniform_location, 1, quad_color));
	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));
	GCHK(glFlush());
	readback();

	GCHK(glUniform4fv(uniform_location, 1, quad_color2));
	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

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
	TEST(test_mrt_fbo(64, 64, GL_UNSIGNED_BYTE, 0, 0, 0));
	TEST(test_mrt_fbo(64, 64, GL_UNSIGNED_BYTE, 0, 0, 1));
	TEST(test_mrt_fbo(64, 64, GL_UNSIGNED_BYTE, 0, 0, 2));
	TEST(test_mrt_fbo(64, 64, GL_UNSIGNED_BYTE, 0, 0, 3));
	TEST(test_mrt_fbo(128, 128, GL_UNSIGNED_BYTE, 0, 0, 0));
	TEST(test_mrt_fbo(128, 128, GL_UNSIGNED_BYTE, 0, 0, 1));
	TEST(test_mrt_fbo(128, 128, GL_UNSIGNED_BYTE, 0, 0, 2));
	TEST(test_mrt_fbo(128, 128, GL_UNSIGNED_BYTE, 0, 0, 3));
	TEST(test_mrt_fbo(256, 128, GL_UNSIGNED_BYTE, 0, 0, 0));
	TEST(test_mrt_fbo(256, 128, GL_UNSIGNED_BYTE, 0, 0, 1));
	TEST(test_mrt_fbo(256, 128, GL_UNSIGNED_BYTE, 0, 0, 2));
	TEST(test_mrt_fbo(256, 128, GL_UNSIGNED_BYTE, 0, 0, 3));
	for (i = 0; i <= MAX_MRT; i++)
		TEST(test_mrt_fbo(64, 64, GL_UNSIGNED_BYTE, i, 0, 1));
	for (i = 0; i <= MAX_MRT; i++)
		TEST(test_mrt_fbo(64, 64, GL_UNSIGNED_BYTE, i, 0, 2));
	TEST(test_mrt_fbo(64, 64, GL_UNSIGNED_BYTE, MAX_MRT, 0x01, 1));
	TEST(test_mrt_fbo(64, 64, GL_UNSIGNED_BYTE, MAX_MRT, 0x02, 1));
	TEST(test_mrt_fbo(64, 64, GL_UNSIGNED_BYTE, MAX_MRT, 0x06, 1));
	TEST(test_mrt_fbo(64, 64, GL_UNSIGNED_BYTE, MAX_MRT, 0, 0));
	TEST(test_mrt_fbo(64, 64, GL_UNSIGNED_BYTE, MAX_MRT, 0, 3));
	TEST(test_mrt_fbo(64, 64, GL_UNSIGNED_BYTE, MAX_MRT, 0, 4));
	TEST_END();

	return 0;
}

#ifdef BIONIC
void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}
#endif

