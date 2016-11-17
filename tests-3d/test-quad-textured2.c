/*
 * Copyright (c) 2011-2012 Luc Verhaegen <libv@codethink.co.uk>
 * Copyright (c) 2012 Jonathan Maw <jonathan.maw@codethink.co.uk>
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

/* Code based on cube_textured test from lima driver project (Jonathan Maw)
 * adapted to the logging that I use..
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
		"in vec4 in_position;         \n"
		"in vec2 in_TexCoord;         \n"
		"\n"
		"out vec2 vTexCoord;          \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_Position = in_position;\n"
		"    vTexCoord = in_TexCoord; \n"
		"}                            \n";

const char *fragment_shader_source =
		"#version 300 es              \n"
		"precision mediump float;     \n"
		"                             \n"
		"uniform sampler2D uTexture;  \n"
		"in vec2 vTexCoord;           \n"
		"out vec4 gl_FragColor;       \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_FragColor = texture(uTexture, vTexCoord);\n"
		"}                            \n";


void test(int texwidth, int texheight, GLenum iformat, GLenum format, GLenum type)
{
	GLint width, height;
	GLint modelviewmatrix_handle, modelviewprojectionmatrix_handle, normalmatrix_handle;
	GLuint texturename = 0, texture_handle;
	GLfloat vVertices[] = {
			-0.45, -0.75, 0.0,
			 0.45, -0.75, 0.0,
			-0.45,  0.75, 0.0,
			 0.45,  0.75, 0.0
	};

	GLfloat vTexCoords[] = {
			1.0f, 1.0f,
			0.0f, 1.0f,
			1.0f, 0.0f,
			0.0f, 0.0f,
	};

	EGLSurface surface;
	static uint8_t *buf = NULL;

	if (!buf) {
		int i;
		buf = malloc(texwidth * texheight * 16);
		for (i = 0; i < (texwidth * texheight * 16); i++)
			buf[i] = i;
	}

	RD_START("quad-textured2", "texwidth=%d, texheight=%d, iformat=%s, format=%s, type=%s",
			texwidth, texheight, formatname(iformat),
			formatname(format), typename(type));

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

	GCHK(glBindAttribLocation(program, 0, "in_position"));
	GCHK(glBindAttribLocation(program, 1, "in_TexCoord"));

	link_program(program);

	GCHK(glViewport(0, 0, width, height));


	/* clear the color buffer */
	GCHK(glClearColor(0.5, 0.5, 0.5, 1.0));
	GCHK(glClear(GL_COLOR_BUFFER_BIT));

	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vVertices));
	GCHK(glEnableVertexAttribArray(0));

	GCHK(glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, vTexCoords));
	GCHK(glEnableVertexAttribArray(1));

	GCHK(glActiveTexture(GL_TEXTURE0));
	GCHK(glGenTextures(1, &texturename));
	GCHK(glBindTexture(GL_TEXTURE_2D, texturename));
	GCHK(glTexImage2D(GL_TEXTURE_2D, 0, iformat, texwidth, texheight,
			0, format, type, buf));

	/* Note: cube turned black until these were defined. */
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_REPEAT));

	GCHK(texture_handle = glGetUniformLocation(program, "uTexture"));
	GCHK(glUniform1i(texture_handle, 0)); /* '0' refers to texture unit 0. */

	GCHK(glEnable(GL_CULL_FACE));

	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	ECHK(eglDestroySurface(display, surface));

	ECHK(eglTerminate(display));

	RD_END();
}

int main(int argc, char *argv[])
{
	static int s[][2] = {
			{   4,   4 },
//			{ 333, 222 },
	};
	int i;

	TEST_START();

	for (i = 0; i < ARRAY_SIZE(s); i++) {
		/* Table 1: Unsized Internal Formats: */
		TEST(test(s[i][0], s[i][1], GL_RGB,                GL_RGB,             GL_UNSIGNED_BYTE));
		TEST(test(s[i][0], s[i][1], GL_RGB,                GL_RGB,             GL_UNSIGNED_SHORT_5_6_5));
		TEST(test(s[i][0], s[i][1], GL_RGBA,               GL_RGBA,            GL_UNSIGNED_BYTE));
		TEST(test(s[i][0], s[i][1], GL_RGBA,               GL_RGBA,            GL_UNSIGNED_SHORT_4_4_4_4));
		TEST(test(s[i][0], s[i][1], GL_RGBA,               GL_RGBA,            GL_UNSIGNED_SHORT_5_5_5_1));
		TEST(test(s[i][0], s[i][1], GL_LUMINANCE_ALPHA,    GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE));
		TEST(test(s[i][0], s[i][1], GL_LUMINANCE,          GL_LUMINANCE,       GL_UNSIGNED_BYTE));
		TEST(test(s[i][0], s[i][1], GL_ALPHA,              GL_ALPHA,           GL_UNSIGNED_BYTE));

		/* Table 2: Sized Internal Formats: */
		TEST(test(s[i][0], s[i][1], GL_R8,                 GL_RED,             GL_UNSIGNED_BYTE));
		TEST(test(s[i][0], s[i][1], GL_R8_SNORM,           GL_RED,             GL_BYTE));
		TEST(test(s[i][0], s[i][1], GL_R16F,               GL_RED,             GL_HALF_FLOAT));
		TEST(test(s[i][0], s[i][1], GL_R16F,               GL_RED,             GL_FLOAT));
		TEST(test(s[i][0], s[i][1], GL_R32F,               GL_RED,             GL_FLOAT));
		TEST(test(s[i][0], s[i][1], GL_R8UI,               GL_RED_INTEGER,     GL_UNSIGNED_BYTE));
		TEST(test(s[i][0], s[i][1], GL_R8I,                GL_RED_INTEGER,     GL_BYTE));
		TEST(test(s[i][0], s[i][1], GL_R16UI,              GL_RED_INTEGER,     GL_UNSIGNED_SHORT));
		TEST(test(s[i][0], s[i][1], GL_R16I,               GL_RED_INTEGER,     GL_SHORT));
		TEST(test(s[i][0], s[i][1], GL_R32UI,              GL_RED_INTEGER,     GL_UNSIGNED_INT));
		TEST(test(s[i][0], s[i][1], GL_R32I,               GL_RED_INTEGER,     GL_INT));
		TEST(test(s[i][0], s[i][1], GL_RG8,                GL_RG,              GL_UNSIGNED_BYTE));
		TEST(test(s[i][0], s[i][1], GL_RG8_SNORM,          GL_RG,              GL_BYTE));
		TEST(test(s[i][0], s[i][1], GL_RG16F,              GL_RG,              GL_HALF_FLOAT));
		TEST(test(s[i][0], s[i][1], GL_RG16F,              GL_RG,              GL_FLOAT));
		TEST(test(s[i][0], s[i][1], GL_RG32F,              GL_RG,              GL_FLOAT));
		TEST(test(s[i][0], s[i][1], GL_RG8UI,              GL_RG_INTEGER,      GL_UNSIGNED_BYTE));
		TEST(test(s[i][0], s[i][1], GL_RG8I,               GL_RG_INTEGER,      GL_BYTE));
		TEST(test(s[i][0], s[i][1], GL_RG16UI,             GL_RG_INTEGER,      GL_UNSIGNED_SHORT));
		TEST(test(s[i][0], s[i][1], GL_RG16I,              GL_RG_INTEGER,      GL_SHORT));
		TEST(test(s[i][0], s[i][1], GL_RG32UI,             GL_RG_INTEGER,      GL_UNSIGNED_INT));
		TEST(test(s[i][0], s[i][1], GL_RG32I,              GL_RG_INTEGER,      GL_INT));
		TEST(test(s[i][0], s[i][1], GL_RGB8,               GL_RGB,             GL_UNSIGNED_BYTE));
		TEST(test(s[i][0], s[i][1], GL_SRGB8,              GL_RGB,             GL_UNSIGNED_BYTE));
		TEST(test(s[i][0], s[i][1], GL_RGB565,             GL_RGB,             GL_UNSIGNED_BYTE));
		TEST(test(s[i][0], s[i][1], GL_RGB565,             GL_RGB,             GL_UNSIGNED_SHORT_5_6_5));
		TEST(test(s[i][0], s[i][1], GL_RGB8_SNORM,         GL_RGB,             GL_BYTE));
		TEST(test(s[i][0], s[i][1], GL_R11F_G11F_B10F,     GL_RGB,             GL_UNSIGNED_INT_10F_11F_11F_REV));
		TEST(test(s[i][0], s[i][1], GL_R11F_G11F_B10F,     GL_RGB,             GL_HALF_FLOAT));
		TEST(test(s[i][0], s[i][1], GL_R11F_G11F_B10F,     GL_RGB,             GL_FLOAT));
		TEST(test(s[i][0], s[i][1], GL_RGB9_E5,            GL_RGB,             GL_UNSIGNED_INT_5_9_9_9_REV));
		TEST(test(s[i][0], s[i][1], GL_RGB9_E5,            GL_RGB,             GL_HALF_FLOAT));
		TEST(test(s[i][0], s[i][1], GL_RGB9_E5,            GL_RGB,             GL_FLOAT));
		TEST(test(s[i][0], s[i][1], GL_RGB16F,             GL_RGB,             GL_HALF_FLOAT));
		TEST(test(s[i][0], s[i][1], GL_RGB16F,             GL_RGB,             GL_FLOAT));
		TEST(test(s[i][0], s[i][1], GL_RGB32F,             GL_RGB,             GL_FLOAT));
		TEST(test(s[i][0], s[i][1], GL_RGB8UI,             GL_RGB_INTEGER,     GL_UNSIGNED_BYTE));
		TEST(test(s[i][0], s[i][1], GL_RGB8I,              GL_RGB_INTEGER,     GL_BYTE));
		TEST(test(s[i][0], s[i][1], GL_RGB16UI,            GL_RGB_INTEGER,     GL_UNSIGNED_SHORT));
		TEST(test(s[i][0], s[i][1], GL_RGB16I,             GL_RGB_INTEGER,     GL_SHORT));
		TEST(test(s[i][0], s[i][1], GL_RGB32UI,            GL_RGB_INTEGER,     GL_UNSIGNED_INT));
		TEST(test(s[i][0], s[i][1], GL_RGB32I,             GL_RGB_INTEGER,     GL_INT));
		TEST(test(s[i][0], s[i][1], GL_RGBA8,              GL_RGBA,            GL_UNSIGNED_BYTE));
		TEST(test(s[i][0], s[i][1], GL_SRGB8_ALPHA8,       GL_RGBA,            GL_UNSIGNED_BYTE));
		TEST(test(s[i][0], s[i][1], GL_RGBA8_SNORM,        GL_RGBA,            GL_BYTE));
		TEST(test(s[i][0], s[i][1], GL_RGB5_A1,            GL_RGBA,            GL_UNSIGNED_BYTE));
		TEST(test(s[i][0], s[i][1], GL_RGB5_A1,            GL_RGBA,            GL_UNSIGNED_SHORT_5_5_5_1));
		TEST(test(s[i][0], s[i][1], GL_RGB5_A1,            GL_RGBA,            GL_UNSIGNED_INT_2_10_10_10_REV));
		TEST(test(s[i][0], s[i][1], GL_RGBA4,              GL_RGBA,            GL_UNSIGNED_BYTE));
		TEST(test(s[i][0], s[i][1], GL_RGBA4,              GL_RGBA,            GL_UNSIGNED_SHORT_4_4_4_4));
		TEST(test(s[i][0], s[i][1], GL_RGB10_A2,           GL_RGBA,            GL_UNSIGNED_INT_2_10_10_10_REV));
		TEST(test(s[i][0], s[i][1], GL_RGBA16F,            GL_RGBA,            GL_HALF_FLOAT));
		TEST(test(s[i][0], s[i][1], GL_RGBA16F,            GL_RGBA,            GL_FLOAT));
		TEST(test(s[i][0], s[i][1], GL_RGBA32F,            GL_RGBA,            GL_FLOAT));
		TEST(test(s[i][0], s[i][1], GL_RGBA8UI,            GL_RGBA_INTEGER,    GL_UNSIGNED_BYTE));
		TEST(test(s[i][0], s[i][1], GL_RGBA8I,             GL_RGBA_INTEGER,    GL_BYTE));
		TEST(test(s[i][0], s[i][1], GL_RGB10_A2UI,         GL_RGBA_INTEGER,    GL_UNSIGNED_INT_2_10_10_10_REV));
		TEST(test(s[i][0], s[i][1], GL_RGBA16UI,           GL_RGBA_INTEGER,    GL_UNSIGNED_SHORT));
		TEST(test(s[i][0], s[i][1], GL_RGBA16I,            GL_RGBA_INTEGER,    GL_SHORT));
		TEST(test(s[i][0], s[i][1], GL_RGBA32I,            GL_RGBA_INTEGER,    GL_INT));
		TEST(test(s[i][0], s[i][1], GL_RGBA32UI,           GL_RGBA_INTEGER,    GL_UNSIGNED_INT));
		TEST(test(s[i][0], s[i][1], GL_DEPTH_COMPONENT16,  GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT));
		TEST(test(s[i][0], s[i][1], GL_DEPTH_COMPONENT16,  GL_DEPTH_COMPONENT, GL_UNSIGNED_INT));
		TEST(test(s[i][0], s[i][1], GL_DEPTH_COMPONENT24,  GL_DEPTH_COMPONENT, GL_UNSIGNED_INT));
		TEST(test(s[i][0], s[i][1], GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT));
		TEST(test(s[i][0], s[i][1], GL_DEPTH24_STENCIL8,   GL_DEPTH_STENCIL,   GL_UNSIGNED_INT_24_8));
		TEST(test(s[i][0], s[i][1], GL_DEPTH32F_STENCIL8,  GL_DEPTH_STENCIL,   GL_FLOAT_32_UNSIGNED_INT_24_8_REV));
	}

	TEST_END();

	return 0;
}

