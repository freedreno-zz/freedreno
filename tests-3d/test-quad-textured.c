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
		"uniform sampler2D uTexture2;  \n"
		"in vec2 vTexCoord;           \n"
		"out vec4 gl_FragColor;       \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_FragColor = texture(uTexture, vTexCoord);\n"
		"    gl_FragColor += texture(uTexture2, vTexCoord);\n"
		"}                            \n";

const char *fragment_shader_source_shadow =
		"#version 300 es              \n"
		"precision mediump float;     \n"
		"                             \n"
		"uniform sampler2DShadow uTexture;  \n"
		"uniform sampler2D uTexture2;  \n"
		"in vec2 vTexCoord;           \n"
		"out vec4 gl_FragColor;       \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_FragColor = vec4(texture(uTexture, vTexCoord.xyy));\n"
		"    gl_FragColor += texture(uTexture2, vTexCoord);\n"
		"}                            \n";


void test_quad_textured(int shadow, int cfunc)
{
	GLint width, height;
	GLuint textures[2], texture_handle;
	GLfloat vVertices[] = {
			// front
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

	RD_START("quad-textured", "shadow=%d, cfunc=%x", shadow, cfunc);

	display = get_display();

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	DEBUG_MSG("num_config: %d", num_config);

	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));

	surface = make_window(display, config, 255, 255);

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("Buffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));

	printf("EGL Version %s\n", eglQueryString(display, EGL_VERSION));
	printf("EGL Vendor %s\n", eglQueryString(display, EGL_VENDOR));
	printf("EGL Extensions %s\n", eglQueryString(display, EGL_EXTENSIONS));
	printf("GL Version %s\n", glGetString(GL_VERSION));
	printf("GL extensions: %s\n", glGetString(GL_EXTENSIONS));

	program = get_program(vertex_shader_source, shadow ?
			fragment_shader_source_shadow : fragment_shader_source);

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

	GCHK(glGenTextures(2, &textures));

	GCHK(glActiveTexture(GL_TEXTURE0));
	GCHK(glBindTexture(GL_TEXTURE_2D, textures[0]));

	if (shadow) {
		GCHK(glTexImage2D(
				GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
				cube_texture.width/2, cube_texture.height/2, 0,
				GL_DEPTH_COMPONENT, GL_FLOAT, cube_texture.pixel_data));
	} else {
		GCHK(glTexImage2D(
				GL_TEXTURE_2D, 0, GL_RGB,
				cube_texture.width, cube_texture.height, 0,
				GL_RGB, GL_UNSIGNED_BYTE, cube_texture.pixel_data));
	}

	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));

	if (shadow) {
#define GL_TEXTURE_COMPARE_MODE           0x884C
#define GL_TEXTURE_COMPARE_FUNC           0x884D
#define GL_COMPARE_REF_TO_TEXTURE         0x884E
		GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
				GL_COMPARE_REF_TO_TEXTURE));
		GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC,
				cfunc));
	} else {
		float minlod = cfunc, maxlod = cfunc;
		GCHK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, minlod));
		GCHK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, maxlod));
		switch (cfunc) {
		case 0:
			GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
			break;
		case 1:
			GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST));
			break;
		case 2:
			GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST));
			break;
		case 3:
			GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR));
			break;
		case 4:
			GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR));
			break;
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
		case 5:
			GCHK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 4));
			break;
		case 6:
			GCHK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8));
			break;
		case 7:
			GCHK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 16));
			break;
		case 8:
			GCHK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 32));
			break;
		case 9:
			GCHK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 320));
			break;
		}
	}

	GCHK(texture_handle = glGetUniformLocation(program, "uTexture"));
	GCHK(glUniform1i(texture_handle, 0)); /* '0' refers to texture unit 0. */

	GCHK(glActiveTexture(GL_TEXTURE1));
	GCHK(glBindTexture(GL_TEXTURE_2D, textures[1]));

	GCHK(glTexImage2D(
			GL_TEXTURE_2D, 0, GL_RGBA,
			cube_texture.width/3, cube_texture.height-1, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, cube_texture.pixel_data+1));

	/* Note: cube turned black until these were defined. */
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));

	GCHK(texture_handle = glGetUniformLocation(program, "uTexture"));
	GCHK(glUniform1i(texture_handle, 1)); /* '1' refers to texture unit 1. */

	GCHK(glEnable(GL_CULL_FACE));

	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	sleep(1);

	ECHK(eglDestroySurface(display, surface));

	ECHK(eglTerminate(display));

	RD_END();
}

int main(int argc, char *argv[])
{
	TEST_START();
	TEST(test_quad_textured(0, 0));
	TEST(test_quad_textured(0, 1));
	TEST(test_quad_textured(0, 2));
	TEST(test_quad_textured(0, 3));
	TEST(test_quad_textured(0, 4));
	TEST(test_quad_textured(0, 5));
	TEST(test_quad_textured(0, 6));
	TEST(test_quad_textured(0, 7));
	TEST(test_quad_textured(0, 8));
	TEST(test_quad_textured(0, 9));
	TEST(test_quad_textured(1, GL_NEVER));
	TEST(test_quad_textured(1, GL_LESS));
	TEST(test_quad_textured(1, GL_EQUAL));
	TEST(test_quad_textured(1, GL_LEQUAL));
	TEST(test_quad_textured(1, GL_GREATER));
	TEST(test_quad_textured(1, GL_NOTEQUAL));
	TEST(test_quad_textured(1, GL_GEQUAL));
	TEST(test_quad_textured(1, GL_ALWAYS));
	TEST_END();

	return 0;
}

