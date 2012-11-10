/*
 * Copyright (c) 2012 Rob Clark <robdclark@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef TEST_UTIL_3D_H_
#define TEST_UTIL_3D_H_


#include "test-util-common.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

/*****************************************************************************/

#define ECHK(x) do { \
		EGLBoolean status; \
		DEBUG_MSG(">>> %s", #x); \
		RD_WRITE_SECTION(RD_CMD, #x, strlen(#x)); \
		status = (EGLBoolean)(x); \
		if (!status) { \
			EGLint err = eglGetError(); \
			ERROR_MSG("<<< %s: failed: 0x%04x (%s)", #x, err, eglStrError(err)); \
			exit(-1); \
		} \
		DEBUG_MSG("<<< %s: succeeded", #x); \
	} while (0)

#define GCHK(x) do { \
		GLenum err; \
		DEBUG_MSG(">>> %s", #x); \
		RD_WRITE_SECTION(RD_CMD, #x, strlen(#x)); \
		x; \
		err = glGetError(); \
		if (err != GL_NO_ERROR) { \
			ERROR_MSG("<<< %s: failed: 0x%04x (%s)", #x, err, glStrError(err)); \
			exit(-1); \
		} \
		DEBUG_MSG("<<< %s: succeeded", #x); \
	} while (0)

static char *
eglStrError(EGLint error)
{
	switch (error) {
	case EGL_SUCCESS:
		return "EGL_SUCCESS";
	case EGL_BAD_ALLOC:
		return "EGL_BAD_ALLOC";
	case EGL_BAD_CONFIG:
		return "EGL_BAD_CONFIG";
	case EGL_BAD_PARAMETER:
		return "EGL_BAD_PARAMETER";
	case EGL_BAD_MATCH:
		return "EGL_BAD_MATCH";
	case EGL_BAD_ATTRIBUTE:
		return "EGL_BAD_ATTRIBUTE";
	default:
		return "UNKNOWN";
	}
}

static char *
glStrError(GLenum error)
{
	switch (error) {
	// TODO
	case GL_INVALID_ENUM:
		return "GL_INVALID_ENUM";
	case GL_INVALID_OPERATION:
		return "GL_INVALID_OPERATION";
	default:
		return "UNKNOWN";
	}
}


static EGLDisplay
get_display(void)
{
	EGLDisplay display;
	EGLint egl_major, egl_minor;

	display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (display == EGL_NO_DISPLAY) {
		ERROR_MSG("No display found!");
		exit(-1);
	}

	ECHK(eglInitialize(display, &egl_major, &egl_minor));

	DEBUG_MSG("Using display %p with EGL version %d.%d",
			display, egl_major, egl_minor);

	DEBUG_MSG("EGL Version \"%s\"", eglQueryString(display, EGL_VERSION));
	DEBUG_MSG("EGL Vendor \"%s\"", eglQueryString(display, EGL_VENDOR));
	DEBUG_MSG("EGL Extensions \"%s\"", eglQueryString(display, EGL_EXTENSIONS));

	return display;
}

static void dump_bmp(EGLDisplay display, EGLSurface surface, const char *filename)
{
	GLint width, height;
	void *buf;

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	buf = malloc(width * height * 4);

	GCHK(glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buf));

	wrap_bmp_dump(buf, width, height, width*4, filename);
}

static GLuint
get_program(const char *vertex_shader_source, const char *fragment_shader_source)
{
	GLuint vertex_shader, fragment_shader, program;
	GLint ret;

	DEBUG_MSG("vertex shader:\n%s", vertex_shader_source);
	DEBUG_MSG("fragment shader:\n%s", fragment_shader_source);

	RD_WRITE_SECTION(RD_VERT_SHADER,
			vertex_shader_source, strlen(vertex_shader_source));
	RD_WRITE_SECTION(RD_FRAG_SHADER,
			fragment_shader_source, strlen(fragment_shader_source));

	GCHK(vertex_shader = glCreateShader(GL_VERTEX_SHADER));

	GCHK(glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL));
	GCHK(glCompileShader(vertex_shader));

	GCHK(glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &ret));
	if (!ret) {
		char *log;

		ERROR_MSG("vertex shader compilation failed!:");
		glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &ret);

		if (ret > 1) {
			log = malloc(ret);
			glGetShaderInfoLog(vertex_shader, ret, NULL, log);
			printf("%s", log);
		}
		exit(-1);
	}

	DEBUG_MSG("Vertex shader compilation succeeded!");

	GCHK(fragment_shader = glCreateShader(GL_FRAGMENT_SHADER));

	GCHK(glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL));
	GCHK(glCompileShader(fragment_shader));

	GCHK(glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &ret));
	if (!ret) {
		char *log;

		ERROR_MSG("fragment shader compilation failed!:");
		glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &ret);

		if (ret > 1) {
			log = malloc(ret);
			glGetShaderInfoLog(fragment_shader, ret, NULL, log);
			printf("%s", log);
		}
		exit(-1);
	}

	DEBUG_MSG("Fragment shader compilation succeeded!");

	GCHK(program = glCreateProgram());

	GCHK(glAttachShader(program, vertex_shader));
	GCHK(glAttachShader(program, fragment_shader));

	return program;
}

/* ***** GL_OES_get_program_binary extension: ****************************** */
/* Accepted by the <pname> parameter of GetProgramiv:
 */
#define GL_PROGRAM_BINARY_LENGTH_OES	0x8741
/* Accepted by the <pname> parameter of GetBooleanv, GetIntegerv, and GetFloatv:
 */
#define GL_NUM_PROGRAM_BINARY_FORMATS_OES	0x87FE
#define GL_PROGRAM_BINARY_FORMATS_OES	0x87FF

void glGetProgramBinaryOES(GLuint program, GLsizei bufSize, GLsizei *length,
		GLenum *binaryFormat, GLvoid *binary);
void glProgramBinaryOES(GLuint program, GLenum binaryFormat,
		const GLvoid *binary, GLint length);
/* ************************************************************************* */

static void
link_program(GLuint program)
{
	GLint ret, len;
	GLenum binary_format;
	void *binary;

	GCHK(glLinkProgram(program));

	GCHK(glGetProgramiv(program, GL_LINK_STATUS, &ret));
	if (!ret) {
		char *log;

		ERROR_MSG("program linking failed!:");
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &ret);

		if (ret > 1) {
			log = malloc(ret);
			glGetProgramInfoLog(program, ret, NULL, log);
			printf("%s", log);
		}
		exit(-1);
	}

	DEBUG_MSG("program linking succeeded!");

	GCHK(glUseProgram(program));

	/* dump program binary: */
	// TODO move this into wrap-gles.c .. just putting it here for now
	// since I haven't created wrap-gles.c yet
	GCHK(glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH_OES, &len));
	binary = calloc(1, len);
	GCHK(glGetProgramBinaryOES(program, len, &ret, &binary_format, binary));
	DEBUG_MSG("program dump: len=%d, actual len=%d", len, ret);
	HEXDUMP(binary, len);
	RD_WRITE_SECTION(RD_PROGRAM, binary, len);
	free(binary);
}

#endif /* TEST_UTIL_3D_H_ */
