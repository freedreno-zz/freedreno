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

#include "test-util-3d.h"

int openfile(const char *fmt, int i)
{
	static char path[256];
	sprintf(path, fmt, i);
	return open(path, 0);
}

int test_compiler(int i)
{
	static char vert_shader[64 * 1024], frag_shader[64 * 1024];
	GLuint program;
	int vert_fd, frag_fd;
	int ret;

	vert_fd = openfile("shaders/%04d.vs", i);
	frag_fd = openfile("shaders/%04d.fs", i);
	if ((vert_fd < 0) || (frag_fd < 0))
		return -1;

	ret = read(vert_fd, vert_shader, sizeof(vert_shader));
	if (ret < 0)
		return ret;
	vert_shader[ret] = '\0';

	ret = read(frag_fd, frag_shader, sizeof(frag_shader));
	if (ret < 0)
		return ret;
	frag_shader[ret] = '\0';

	DEBUG_MSG("----------------------------------------------------------------");
	RD_START("compiler", "%d", i);

	program = get_program(vert_shader, frag_shader);

	link_program(program);
	GCHK(glFlush());

	RD_END();

	return 0;
}

int main(int argc, char *argv[])
{
	GLint width, height;
	EGLint pbuffer_attribute_list[] = {
		EGL_WIDTH, 256,
		EGL_HEIGHT, 256,
		EGL_LARGEST_PBUFFER, EGL_TRUE,
		EGL_NONE
	};
	const EGLint config_attribute_list[] = {
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_DEPTH_SIZE, 8,
		EGL_NONE
	};
	const EGLint context_attribute_list[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	EGLDisplay display;
	EGLConfig config;
	EGLint num_config;
	EGLContext context;
	EGLSurface surface;
	int i;

	display = get_display();

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	DEBUG_MSG("num_config: %d", num_config);

	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));
	ECHK(surface = eglCreatePbufferSurface(display, config, pbuffer_attribute_list));

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("PBuffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));
	GCHK(glFlush());

	for (i = 0; ; i++) {
		if (test_compiler(i)) {
			break;
		}
	}

	ECHK(eglDestroySurface(display, surface));
	ECHK(eglTerminate(display));
}

#ifdef BIONIC
void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}
#endif
