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

static GLuint
get_program(const char *vertex_shader_source, const char *fragment_shader_source)
{
	GLuint vertex_shader, fragment_shader, program;
	GLint ret;

	ECHK(vertex_shader = glCreateShader(GL_VERTEX_SHADER));

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

	ECHK(fragment_shader = glCreateShader(GL_FRAGMENT_SHADER));

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

	ECHK(program = glCreateProgram());

	GCHK(glAttachShader(program, vertex_shader));
	GCHK(glAttachShader(program, fragment_shader));

	return program;
}

static void
link_program(GLuint program)
{
	GLint ret;

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
		return;
	}

	DEBUG_MSG("program linking succeeded!");

	GCHK(glUseProgram(program));
}

#endif /* TEST_UTIL_3D_H_ */
