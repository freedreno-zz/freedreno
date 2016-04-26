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

static struct {
	const char *name;
	GLenum pname;
} int_params[] = {
#define PARAM(x) { #x, x }
	PARAM(GL_MAX_3D_TEXTURE_SIZE_OES),
	PARAM(GL_MAX_SAMPLES_ANGLE),
	PARAM(GL_MAX_SAMPLES_APPLE),
	PARAM(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT),
	PARAM(GL_MAX_SAMPLES_IMG),
	PARAM(GL_MAX_TEXTURE_SIZE),
	PARAM(GL_MAX_VIEWPORT_DIMS),
	PARAM(GL_MAX_VERTEX_ATTRIBS),
	PARAM(GL_MAX_VERTEX_UNIFORM_VECTORS),
	PARAM(GL_MAX_VARYING_VECTORS),
	PARAM(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS),
	PARAM(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS),
	PARAM(GL_MAX_TEXTURE_IMAGE_UNITS),
	PARAM(GL_MAX_FRAGMENT_UNIFORM_VECTORS),
	PARAM(GL_MAX_CUBE_MAP_TEXTURE_SIZE),
	PARAM(GL_MAX_RENDERBUFFER_SIZE),
	PARAM(GL_TEXTURE_NUM_LEVELS_QCOM),
#undef PARAM
};

void test_caps(void)
{
	GLint width, height;
	EGLint pbuffer_attribute_list[] = {
		EGL_WIDTH, 256,
		EGL_HEIGHT, 256,
		EGL_LARGEST_PBUFFER, EGL_TRUE,
		EGL_NONE
	};
	EGLSurface surface;
	int i;

	RD_START("caps", "");

	ECHK(surface = eglCreatePbufferSurface(display, config, pbuffer_attribute_list));

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("PBuffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));
	GCHK(glFlush());

	printf("EGL Version %s\n", eglQueryString(display, EGL_VERSION));
	printf("EGL Vendor %s\n", eglQueryString(display, EGL_VENDOR));
	printf("EGL Extensions %s\n", eglQueryString(display, EGL_EXTENSIONS));
	printf("GL Version %s\n", glGetString(GL_VERSION));
	printf("GL extensions: %s\n", glGetString(GL_EXTENSIONS));

	for (i = 0; i < ARRAY_SIZE(int_params); i++) {
		GLint val[4] = {};
		GLenum err;

		glGetIntegerv(int_params[i].pname, val);

		err = glGetError();
		if (err != GL_NO_ERROR) {
			printf("no %s: %s\n", int_params[i].name, glStrError(err));
		} else {
			printf("%s: %d %d %d %d\n", int_params[i].name,
					val[0], val[1], val[2], val[3]);
		}
	}

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

	TEST(test_caps());

	ECHK(eglTerminate(display));

	TEST_END();

	return 0;
}

