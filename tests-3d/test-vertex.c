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
// note: GL_MAX_VERTEX_ATTRIBS is 16
const char *vertex_shader_source =
		"#version 300 es              \n"
		"in vec4 aPosition;           \n"
		"in vec4 aColor1;             \n"
		"in vec4 aColor2;             \n"
		"in vec4 aColor3;             \n"
		"in vec4 aColor4;             \n"
		"in vec4 aColor5;             \n"
		"in vec4 aColor6;             \n"
		"in vec4 aColor7;             \n"
		"in vec4 aColor8;             \n"
		"                             \n"
		"out vec4 vColor;             \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    vColor = aColor1 * aColor2 * aColor3 * aColor4 * \n"
		"             aColor5 * aColor6 * aColor7 * aColor8; \n"
		"    gl_Position = aPosition; \n"
		"}                            \n";
const char *fragment_shader_source =
		"#version 300 es              \n"
		"precision mediump float;     \n"
		"                             \n"
		"in vec4 vColor;              \n"
		"out vec4 gl_FragColor;       \n"
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
	uint8_t vColors1[] = {
			0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
			0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
			0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
			0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f };
	uint8_t vColors2[] = {
			0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
			0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
			0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
			0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f };
	uint8_t vColors3[] = {
			0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
			0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
			0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
			0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f };
	uint8_t vColors4[] = {
			0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
			0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
			0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
			0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f };
	EGLSurface surface;

	void _glVertexAttribPointer (GLuint indx, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid* ptr)
	{
		DEBUG_MSG("indx=%d, size=%d, type=%s, normalized=%d, stride=%d, ptr=%p",
				indx, size, typename(type), normalized, stride, ptr);
		glVertexAttribPointer (indx, size, type, normalized, stride, ptr);
		glEnableVertexAttribArray(indx);
	}

	RD_START("vertex", "sizes: %d, %d, %d, %d, types: %s, %s, %s, %s",
			sizes[0], sizes[1], sizes[2], sizes[3],
			typename(types[0]), typename(types[1]), typename(types[2]), typename(types[3]));

	ECHK(surface = eglCreatePbufferSurface(display, config, pbuffer_attribute_list));

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("PBuffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));
	GCHK(glFlush());

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

	link_program(program);
	GCHK(glFlush());

	GCHK(glViewport(0, 0, width, height));
	GCHK(glFlush());


	/* clear the color buffer */
	GCHK(glClearColor(0.0, 0.0, 0.0, 1.0));
	GCHK(glFlush());
	GCHK(glClear(GL_COLOR_BUFFER_BIT));
	GCHK(glFlush());

	GCHK(_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vVertices));

	GCHK(_glVertexAttribPointer(1, sizes[0], types[0], GL_FALSE, 0, vColors1));
	GCHK(_glVertexAttribPointer(2, sizes[1], types[1], GL_FALSE, 0, vColors2));
	GCHK(_glVertexAttribPointer(3, sizes[2], types[2], GL_FALSE, 0, vColors3));
	GCHK(_glVertexAttribPointer(4, sizes[3], types[3], GL_FALSE, 0, vColors4));

	GCHK(_glVertexAttribPointer(5, sizes[0], types[1], GL_TRUE, 0, vColors1));
	GCHK(_glVertexAttribPointer(6, sizes[1], types[2], GL_TRUE, 0, vColors2));
	GCHK(_glVertexAttribPointer(7, sizes[2], types[3], GL_TRUE, 0, vColors3));
	GCHK(_glVertexAttribPointer(8, sizes[3], types[0], GL_TRUE, 0, vColors4));

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
	TEST_START();

	display = get_display();

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	DEBUG_MSG("num_config: %d", num_config);

	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));

	TEST(test_vertex((GLint[]){ 1, 2, 3, 4 },
			(GLenum[]){ GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT }));
	TEST(test_vertex((GLint[]){ 1, 2, 3, 4 },
			(GLenum[]){ GL_BYTE, GL_BYTE, GL_BYTE, GL_BYTE }));
	TEST(test_vertex((GLint[]){ 1, 2, 3, 4 },
			(GLenum[]){ GL_UNSIGNED_BYTE, GL_UNSIGNED_BYTE, GL_UNSIGNED_BYTE, GL_UNSIGNED_BYTE }));
	TEST(test_vertex((GLint[]){ 1, 2, 3, 4 },
			(GLenum[]){ GL_SHORT, GL_SHORT, GL_SHORT, GL_SHORT }));
	TEST(test_vertex((GLint[]){ 1, 2, 3, 4 },
			(GLenum[]){ GL_UNSIGNED_SHORT, GL_UNSIGNED_SHORT, GL_UNSIGNED_SHORT, GL_UNSIGNED_SHORT }));
	TEST(test_vertex((GLint[]){ 1, 2, 3, 4 },
			(GLenum[]){ GL_INT, GL_INT, GL_INT, GL_INT }));
	TEST(test_vertex((GLint[]){ 1, 2, 3, 4 },
			(GLenum[]){ GL_UNSIGNED_INT, GL_UNSIGNED_INT, GL_UNSIGNED_INT, GL_UNSIGNED_INT }));
	TEST(test_vertex((GLint[]){ 1, 2, 3, 4 },
			(GLenum[]){ GL_FIXED, GL_FIXED, GL_FIXED, GL_FIXED }));
	TEST(test_vertex((GLint[]){ 1, 2, 3, 4 },
			(GLenum[]){ GL_HALF_FLOAT, GL_HALF_FLOAT, GL_HALF_FLOAT, GL_HALF_FLOAT }));
	TEST(test_vertex((GLint[]){ 1, 2, 3, 4 },
			(GLenum[]){ GL_UNSIGNED_INT_2_10_10_10_REV, GL_UNSIGNED_INT_2_10_10_10_REV, GL_UNSIGNED_INT_2_10_10_10_REV, GL_UNSIGNED_INT_2_10_10_10_REV }));
	TEST(test_vertex((GLint[]){ 1, 2, 3, 4 },
			(GLenum[]){ GL_INT_2_10_10_10_REV, GL_INT_2_10_10_10_REV, GL_INT_2_10_10_10_REV, GL_INT_2_10_10_10_REV }));

	ECHK(eglTerminate(display));
	TEST_END();

	return 0;
}
#ifdef BIONIC
void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}
#endif
