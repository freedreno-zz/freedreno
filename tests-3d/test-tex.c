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

#include "test-util-3d.h"

#include "cubetex.h"


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
static int uniform_location;

const char *vertex_shader_source[] = {
		/* 0 texture: */
		"attribute vec4 aPosition;      \n"
		"                               \n"
		"void main()                    \n"
		"{                              \n"
		"    vec4 p = aPosition;        \n"
		"    gl_Position = p;           \n"
		"}                              \n",

		/* 1 texture: */
		"attribute vec4 aPosition;      \n"
		"uniform sampler2D uTex1;       \n"
		"                               \n"
		"void main()                    \n"
		"{                              \n"
		"    vec4 p = aPosition;        \n"
		"    p = texture2D(uTex1, p.xy);\n"
		"    gl_Position = p;           \n"
		"}                              \n",

		/* 2 texture: */
		"attribute vec4 aPosition;      \n"
		"uniform sampler2D uTex1;       \n"
		"uniform sampler2D uTex2;       \n"
		"                               \n"
		"void main()                    \n"
		"{                              \n"
		"    vec4 p = aPosition;        \n"
		"    p = texture2D(uTex1, p.xy);\n"
		"    p = texture2D(uTex2, p.xy);\n"
		"    gl_Position = p;           \n"
		"}                              \n",

		/* 3 texture: */
		"attribute vec4 aPosition;      \n"
		"uniform sampler2D uTex1;       \n"
		"uniform sampler2D uTex2;       \n"
		"uniform sampler2D uTex3;       \n"
		"                               \n"
		"void main()                    \n"
		"{                              \n"
		"    vec4 p = aPosition;        \n"
		"    p = texture2D(uTex1, p.xy);\n"
		"    p = texture2D(uTex2, p.xy);\n"
		"    p = texture2D(uTex3, p.xy);\n"
		"    gl_Position = p;           \n"
		"}                              \n",
};

const char *fragment_shader_source[] = {
		/* 0 texture: */
		"precision highp float;         \n"
		"uniform vec4 uColor;           \n"
		"                               \n"
		"void main()                    \n"
		"{                              \n"
		"    vec4 c = uColor;           \n"
		"    gl_FragColor = c;          \n"
		"}                              \n",

		/* 1 texture: */
		"precision highp float;         \n"
		"uniform vec4 uColor;           \n"
		"uniform sampler2D uTex1;       \n"
		"                               \n"
		"void main()                    \n"
		"{                              \n"
		"    vec4 c = uColor;           \n"
		"    c = texture2D(uTex1, c.xy);\n"
		"    gl_FragColor = c;          \n"
		"}                              \n",

		/* 2 texture: */
		"precision highp float;         \n"
		"uniform vec4 uColor;           \n"
		"uniform sampler2D uTex1;       \n"
		"uniform sampler2D uTex2;       \n"
		"                               \n"
		"void main()                    \n"
		"{                              \n"
		"    vec4 c = uColor;           \n"
		"    c = texture2D(uTex1, c.xy);\n"
		"    c = texture2D(uTex2, c.xy);\n"
		"    gl_FragColor = c;          \n"
		"}                              \n",

		/* 3 texture: */
		"precision highp float;         \n"
		"uniform vec4 uColor;           \n"
		"uniform sampler2D uTex1;       \n"
		"uniform sampler2D uTex2;       \n"
		"uniform sampler2D uTex3;       \n"
		"                               \n"
		"void main()                    \n"
		"{                              \n"
		"    vec4 c = uColor;           \n"
		"    c = texture2D(uTex1, c.xy);\n"
		"    c = texture2D(uTex2, c.xy);\n"
		"    c = texture2D(uTex3, c.xy);\n"
		"    gl_FragColor = c;          \n"
		"}                              \n",
};

const GLenum tex_enums[] = {
		GL_TEXTURE0,
		GL_TEXTURE1,
		GL_TEXTURE2,
		GL_TEXTURE3,
};

/* Run through multiple variants to detect clear color, quad color (frag
 * shader param), and vertices
 */
void test_tex(int nvtex, int nftex)
{
	GLint width, height;
	EGLint pbuffer_attribute_list[] = {
		EGL_WIDTH, 256,
		EGL_HEIGHT, 256,
		EGL_LARGEST_PBUFFER, EGL_TRUE,
		EGL_NONE
	};
	GLfloat quad_color[] =  {1.0, 0.0, 0.0, 1.0};
	GLfloat vertices[] = {
			-0.45, -0.75, 0.0,
			 0.45, -0.75, 0.0,
			-0.45,  0.75, 0.0,
			 0.45,  0.75, 0.0 };
	EGLSurface surface;
	int n;

	RD_START("tex", "%d vtex, %d ftex", nvtex, nftex);

	ECHK(surface = eglCreatePbufferSurface(display, config, pbuffer_attribute_list));

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("PBuffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));

	program = get_program(vertex_shader_source[nvtex], fragment_shader_source[nftex]);

	GCHK(glBindAttribLocation(program, 0, "aPosition"));

	link_program(program);

	GCHK(glViewport(0, 0, width, height));

	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices));
	GCHK(glEnableVertexAttribArray(0));

	/* now set up our uniforms/textures. */
	GCHK(uniform_location = glGetUniformLocation(program, "uColor"));
	GCHK(glUniform4fv(uniform_location, 1, quad_color));
	for (n = 0; n < max(nvtex, nftex); n++) {
		GLuint texturename = 0, texture_handle;
		static char name[8];

		GCHK(glActiveTexture(tex_enums[n]));
		GCHK(glGenTextures(1, &texturename));
		GCHK(glBindTexture(GL_TEXTURE_2D, texturename));
		GCHK(glTexImage2D(
				GL_TEXTURE_2D, 0, GL_RGB,
				cube_texture.width, cube_texture.height, 0,
				GL_RGB, GL_UNSIGNED_BYTE, cube_texture.pixel_data));

		GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
		GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
		GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
		GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));
		GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R_OES, GL_REPEAT));

		sprintf(name, "uTex%d", n+1);
		GCHK(texture_handle = glGetUniformLocation(program, name));
		GCHK(glUniform1i(texture_handle, n));
	}

	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));
	GCHK(glFlush());

	ECHK(eglSwapBuffers(display, surface));

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

	TEST(test_tex(0, 0));
	TEST(test_tex(0, 1));
	TEST(test_tex(1, 0));
	TEST(test_tex(1, 1));
	TEST(test_tex(1, 2));
	TEST(test_tex(2, 1));
	TEST(test_tex(2, 2));
	TEST(test_tex(0, 3));
	TEST(test_tex(3, 2));

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
