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

/* Code copied from strip_smoothed test from lima driver project adapted to the
 * logging that I use..
 */

#include "test-util-3d.h"


static EGLint const config_attribute_list[] = {
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_DEPTH_SIZE, 0,
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
		"attribute vec4 aPosition;    \n"
		"attribute vec4 aColor;       \n"
		"                             \n"
		"varying vec4 vColor;         \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    vColor = aColor;         \n"
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

static GLuint framebuffer;
//static GLuint depthRenderbuffer;
static GLuint texture;

static void setup_fbo(int texWidth, int texHeight)
{
	GLint  maxRenderbufferSize;
	GLenum status;

	// generate the framebuffer, renderbuffer, and texture object names
	glGenFramebuffers(1, &framebuffer);
//	glGenRenderbuffers(1, &depthRenderbuffer);
	glGenTextures(1, &texture);

	// bind texture and load the texture mip-level 0
	// texels are RGB565
	// no texels need to be specified as we are going to draw into
	// the texture
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texWidth, texHeight,
			0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	// bind renderbuffer and create a 16-bit depth buffer
	// width and height of renderbuffer = width and height of
	// the texture
//	glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);
//	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
//			texWidth, texHeight);

	// bind the framebuffer
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

	// specify texture as color attachment
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D, texture, 0);

//	// specify depth_renderbufer as depth attachment
//	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
//			GL_RENDERBUFFER, depthRenderbuffer);

	// check for framebuffer complete
	status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
DEBUG_MSG("status=%04x", status);
}

static void cleanup_fbo(void)
{
//	glDeleteRenderbuffers(1, &depthRenderbuffer);
	glDeleteFramebuffers(1, &framebuffer);
	glDeleteTextures(1, &texture);
}

void test_strip_smoothed(int fbo)
{
	GLint width, height;
	GLfloat vVertices[] = {
			-0.7,  0.7, -0.7,
			-0.7,  0.2, -0.4,
			 0.0,  0.3, -0.5,
			-0.2, -0.3,  0.3,
			 0.5, -0.2,  0.4,
			 0.7, -0.7,  0.7 };
	GLfloat vColors[] = {
			0.1, 0.1, 0.1, 1.0,
			1.0, 0.0, 0.0, 1.0,
			0.0, 0.0, 1.0, 1.0,
			1.0, 1.0, 0.0, 1.0,
			0.0, 1.0, 1.0, 1.0,
			0.9, 0.9, 0.9, 1.0};
	EGLSurface surface;

	RD_START("strip-smoothed", "fbo=%d", fbo);

	display = get_display();

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	DEBUG_MSG("num_config: %d", num_config);

	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));

	surface = make_window(display, config, 256, 256);

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("Buffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));

	program = get_program(vertex_shader_source, fragment_shader_source);

	GCHK(glBindAttribLocation(program, 0, "aPosition"));
	GCHK(glBindAttribLocation(program, 1, "aColor"));

	link_program(program);

	GCHK(glViewport(0, 0, width, height));

	if (fbo)
		GCHK(setup_fbo(width, height));

	/* clear the color buffer */
	GCHK(glClearColor(0.3125, 0.3125, 0.3125, 1.0));
	GCHK(glClear(GL_COLOR_BUFFER_BIT));

	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vVertices));
	GCHK(glEnableVertexAttribArray(0));

	GCHK(glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, vColors));
	GCHK(glEnableVertexAttribArray(1));

	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 6));

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	if (fbo)
		GCHK(cleanup_fbo());

	ECHK(eglDestroySurface(display, surface));

	ECHK(eglTerminate(display));

	RD_END();
}

int main(int argc, char *argv[])
{
	TEST_START();
	TEST(test_strip_smoothed(0));
	TEST(test_strip_smoothed(1));
	TEST_END();

	return 0;
}

