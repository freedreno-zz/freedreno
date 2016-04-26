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

#include "test-util-3d.h"

#include "esUtil.h"
#include "esTransform.c"

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
		"uniform mat4 modelviewMatrix;\n"
		"uniform mat4 modelviewprojectionMatrix;\n"
		"uniform mat3 normalMatrix;\n"
		"\n"
		"attribute vec4 in_position;    \n"
		"attribute vec3 in_normal;      \n"
		"attribute vec2 in_TexCoord;    \n"
		"\n"
		"vec4 lightSource = vec4(2.0, 2.0, 20.0, 0.0);\n"
		"                             \n"
		"varying vec4 vVaryingColor;  \n"
		"varying vec2 vTexCoord;      \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_Position = modelviewprojectionMatrix * in_position;\n"
		"    vec3 vEyeNormal = normalMatrix * in_normal;\n"
		"    vec4 vPosition4 = modelviewMatrix * in_position;\n"
		"    vec3 vPosition3 = vPosition4.xyz / vPosition4.w;\n"
		"    vec3 vLightDir = normalize(lightSource.xyz - vPosition3);\n"
		"    float diff = max(0.0, dot(vEyeNormal, vLightDir));\n"
		"    vVaryingColor = vec4(diff * vec3(1.0, 1.0, 1.0), 1.0);\n"
		"    vTexCoord = in_TexCoord; \n"
		"}                            \n";

const char *fragment_shader_source =
		"precision mediump float;     \n"
		"                             \n"
		"uniform sampler2D uTexture;  \n"
		"                             \n"
		"varying vec4 vVaryingColor;         \n"
		"varying vec2 vTexCoord;      \n"
		"                             \n"
		"void main()                  \n"
		"{                            \n"
		"    gl_FragColor = vVaryingColor * texture2D(uTexture, vTexCoord);\n"
		"}                            \n";


void test_cube_textured(GLint mag_filter, GLint min_filter,
		GLint wrap_s, GLint wrap_t, GLint wrap_r, GLenum format, GLenum type)
{
	GLint width, height;
	GLint modelviewmatrix_handle, modelviewprojectionmatrix_handle, normalmatrix_handle;
	GLuint texturename = 0, texture_handle;
	GLfloat vVertices[] = {
			// front
			-1.0f, -1.0f, +1.0f, // point blue
			+1.0f, -1.0f, +1.0f, // point magenta
			-1.0f, +1.0f, +1.0f, // point cyan
			+1.0f, +1.0f, +1.0f, // point white
			// back
			+1.0f, -1.0f, -1.0f, // point red
			-1.0f, -1.0f, -1.0f, // point black
			+1.0f, +1.0f, -1.0f, // point yellow
			-1.0f, +1.0f, -1.0f, // point green
			// right
			+1.0f, -1.0f, +1.0f, // point magenta
			+1.0f, -1.0f, -1.0f, // point red
			+1.0f, +1.0f, +1.0f, // point white
			+1.0f, +1.0f, -1.0f, // point yellow
			// left
			-1.0f, -1.0f, -1.0f, // point black
			-1.0f, -1.0f, +1.0f, // point blue
			-1.0f, +1.0f, -1.0f, // point green
			-1.0f, +1.0f, +1.0f, // point cyan
			// top
			-1.0f, +1.0f, +1.0f, // point cyan
			+1.0f, +1.0f, +1.0f, // point white
			-1.0f, +1.0f, -1.0f, // point green
			+1.0f, +1.0f, -1.0f, // point yellow
			// bottom
			-1.0f, -1.0f, -1.0f, // point black
			+1.0f, -1.0f, -1.0f, // point red
			-1.0f, -1.0f, +1.0f, // point blue
			+1.0f, -1.0f, +1.0f  // point magenta
	};

	GLfloat vTexCoords[] = {
			//front
			1.0f, 1.0f, //point blue
			0.0f, 1.0f, //point magenta
			1.0f, 0.0f, //point cyan
			0.0f, 0.0f, //point white
			//back
			1.0f, 1.0f, //point red
			0.0f, 1.0f, //point black
			1.0f, 0.0f, //point yellow
			0.0f, 0.0f, //point green
			//right
			1.0f, 1.0f, //point magenta
			0.0f, 1.0f, //point red
			1.0f, 0.0f, //point white
			0.0f, 0.0f, //point yellow
			//left
			1.0f, 1.0f, //point black
			0.0f, 1.0f, //point blue
			1.0f, 0.0f, //point green
			0.0f, 0.0f, //point cyan
			//top
			1.0f, 1.0f, //point cyan
			0.0f, 1.0f, //point white
			1.0f, 0.0f, //point green
			0.0f, 0.0f, //point yellow
			//bottom
			1.0f, 0.0f, //point black
			0.0f, 0.0f, //point red
			1.0f, 1.0f, //point blue
			0.0f, 1.0f, //point magenta
	};

	GLfloat vNormals[] = {
			// front
			+0.0f, +0.0f, +1.0f, // forward
			+0.0f, +0.0f, +1.0f, // forward
			+0.0f, +0.0f, +1.0f, // forward
			+0.0f, +0.0f, +1.0f, // forward
			// back
			+0.0f, +0.0f, -1.0f, // backbard
			+0.0f, +0.0f, -1.0f, // backbard
			+0.0f, +0.0f, -1.0f, // backbard
			+0.0f, +0.0f, -1.0f, // backbard
			// right
			+1.0f, +0.0f, +0.0f, // right
			+1.0f, +0.0f, +0.0f, // right
			+1.0f, +0.0f, +0.0f, // right
			+1.0f, +0.0f, +0.0f, // right
			// left
			-1.0f, +0.0f, +0.0f, // left
			-1.0f, +0.0f, +0.0f, // left
			-1.0f, +0.0f, +0.0f, // left
			-1.0f, +0.0f, +0.0f, // left
			// top
			+0.0f, +1.0f, +0.0f, // up
			+0.0f, +1.0f, +0.0f, // up
			+0.0f, +1.0f, +0.0f, // up
			+0.0f, +1.0f, +0.0f, // up
			// bottom
			+0.0f, -1.0f, +0.0f, // down
			+0.0f, -1.0f, +0.0f, // down
			+0.0f, -1.0f, +0.0f, // down
			+0.0f, -1.0f, +0.0f  // down
	};
	EGLSurface surface;
	const int texwidth = 333;
	const int texheight = 222;
	static uint8_t *buf = NULL;

	if (!buf) {
		int i;
		buf = malloc(texwidth * texheight * 16);
		for (i = 0; i < (texwidth * texheight * 16); i++)
			buf[i] = i;
	}

	RD_START("cube-textured", "mag_filter=%04x, min_filter=%04x, "
			"wrap_s=%04x, wrap_t=%04x, wrap_r=%04x, format=%s, type=%s",
			mag_filter, min_filter, wrap_s, wrap_t, wrap_r,
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
	GCHK(glBindAttribLocation(program, 1, "in_normal"));
	GCHK(glBindAttribLocation(program, 2, "in_TexCoord"));

	link_program(program);

	GCHK(glViewport(0, 0, width, height));


	/* clear the color buffer */
	GCHK(glClearColor(0.5, 0.5, 0.5, 1.0));
	GCHK(glClear(GL_COLOR_BUFFER_BIT));

	GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vVertices));
	GCHK(glEnableVertexAttribArray(0));

	GCHK(glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, vNormals));
	GCHK(glEnableVertexAttribArray(1));

	GCHK(glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, vTexCoords));
	GCHK(glEnableVertexAttribArray(2));

	ESMatrix modelview;
	esMatrixLoadIdentity(&modelview);
	esTranslate(&modelview, 0.0f, 0.0f, -8.0f);
	esRotate(&modelview, 45.0f, 1.0f, 0.0f, 0.0f);
	esRotate(&modelview, 45.0f, 0.0f, 1.0f, 0.0f);
	esRotate(&modelview, 10.0f, 0.0f, 0.0f, 1.0f);

	GLfloat aspect = (GLfloat)(height) / (GLfloat)(width);

	ESMatrix projection;
	esMatrixLoadIdentity(&projection);
	esFrustum(&projection, -2.8f, +2.8f, -2.8f * aspect, +2.8f * aspect, 6.0f, 10.0f);

	ESMatrix modelviewprojection;
	esMatrixLoadIdentity(&modelviewprojection);
	esMatrixMultiply(&modelviewprojection, &modelview, &projection);

	float normal[9];
	normal[0] = modelview.m[0][0];
	normal[1] = modelview.m[0][1];
	normal[2] = modelview.m[0][2];
	normal[3] = modelview.m[1][0];
	normal[4] = modelview.m[1][1];
	normal[5] = modelview.m[1][2];
	normal[6] = modelview.m[2][0];
	normal[7] = modelview.m[2][1];
	normal[8] = modelview.m[2][2];

	GCHK(glActiveTexture(GL_TEXTURE0));
	GCHK(glGenTextures(1, &texturename));
	GCHK(glBindTexture(GL_TEXTURE_2D, texturename));
	GCHK(glTexImage2D(GL_TEXTURE_2D, 0, format, texwidth, texheight,
			0, format, type, buf));

	/* Note: cube turned black until these were defined. */
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t));
	GCHK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R_OES, wrap_r));

	GCHK(modelviewmatrix_handle = glGetUniformLocation(program, "modelviewMatrix"));
	GCHK(modelviewprojectionmatrix_handle = glGetUniformLocation(program, "modelviewprojectionMatrix"));
	GCHK(normalmatrix_handle = glGetUniformLocation(program, "normalMatrix"));
	GCHK(texture_handle = glGetUniformLocation(program, "uTexture"));

	GCHK(glUniformMatrix4fv(modelviewmatrix_handle, 1, GL_FALSE, &modelview.m[0][0]));
	GCHK(glUniformMatrix4fv(modelviewprojectionmatrix_handle, 1, GL_FALSE, &modelviewprojection.m[0][0]));
	GCHK(glUniformMatrix3fv(normalmatrix_handle, 1, GL_FALSE, normal));
	GCHK(glUniform1i(texture_handle, 0)); /* '0' refers to texture unit 0. */

	GCHK(glEnable(GL_CULL_FACE));

	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));
	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 4, 4));
	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 8, 4));
	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 12, 4));
	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 16, 4));
	GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 20, 4));

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	ECHK(eglDestroySurface(display, surface));

	ECHK(eglTerminate(display));

	RD_END();
}

int main(int argc, char *argv[])
{
	TEST_START();

	TEST(test_cube_textured(GL_LINEAR,  GL_LINEAR,  GL_REPEAT,          GL_REPEAT,          GL_REPEAT,          GL_RGB, GL_UNSIGNED_BYTE));
	TEST(test_cube_textured(GL_NEAREST, GL_LINEAR,  GL_REPEAT,          GL_REPEAT,          GL_CLAMP_TO_EDGE,   GL_RGB, GL_UNSIGNED_BYTE));
	TEST(test_cube_textured(GL_LINEAR,  GL_NEAREST, GL_CLAMP_TO_EDGE,   GL_REPEAT,          GL_MIRRORED_REPEAT, GL_RGB, GL_UNSIGNED_BYTE));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT,          GL_CLAMP_TO_EDGE,   GL_REPEAT,          GL_RGB, GL_UNSIGNED_BYTE));
	TEST(test_cube_textured(GL_LINEAR,  GL_LINEAR,  GL_MIRRORED_REPEAT, GL_REPEAT,          GL_REPEAT,          GL_RGB, GL_UNSIGNED_BYTE));
	TEST(test_cube_textured(GL_LINEAR,  GL_LINEAR,  GL_CLAMP_TO_EDGE,   GL_MIRRORED_REPEAT, GL_REPEAT,          GL_RGB, GL_UNSIGNED_BYTE));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_ALPHA,     GL_UNSIGNED_BYTE));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_LUMINANCE, GL_UNSIGNED_BYTE));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGB,       GL_UNSIGNED_BYTE));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGB,       GL_UNSIGNED_SHORT_5_6_5));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGBA,      GL_UNSIGNED_BYTE));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGBA,      GL_UNSIGNED_SHORT_4_4_4_4));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGBA,      GL_UNSIGNED_SHORT_5_5_5_1));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGBA,      GL_FLOAT));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGB,       GL_FLOAT));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_ALPHA,     GL_FLOAT));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_LUMINANCE, GL_FLOAT));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_LUMINANCE_ALPHA, GL_FLOAT));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT));

	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_ALPHA,     GL_HALF_FLOAT_OES));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_LUMINANCE, GL_HALF_FLOAT_OES));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_LUMINANCE_ALPHA, GL_HALF_FLOAT_OES));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGB,       GL_HALF_FLOAT_OES));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGBA,      GL_HALF_FLOAT_OES));

//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_ALPHA,     GL_BGRA_EXT));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_LUMINANCE, GL_BGRA_EXT));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_LUMINANCE_ALPHA, GL_BGRA_EXT));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGB,       GL_BGRA_EXT));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGBA,      GL_BGRA_EXT));

//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_ALPHA,     GL_UNSIGNED_INT_2_10_10_10_REV_EXT));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_LUMINANCE, GL_UNSIGNED_INT_2_10_10_10_REV_EXT));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_LUMINANCE_ALPHA, GL_UNSIGNED_INT_2_10_10_10_REV_EXT));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGB,       GL_UNSIGNED_INT_2_10_10_10_REV_EXT));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGBA,      GL_UNSIGNED_INT_2_10_10_10_REV_EXT));

//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_ALPHA,     GL_UNSIGNED_SHORT));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_LUMINANCE, GL_UNSIGNED_SHORT));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_LUMINANCE_ALPHA, GL_UNSIGNED_SHORT));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGB,       GL_UNSIGNED_SHORT));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGBA,      GL_UNSIGNED_SHORT));

//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_ALPHA,     GL_UNSIGNED_INT));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_LUMINANCE, GL_UNSIGNED_INT));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_LUMINANCE_ALPHA, GL_UNSIGNED_INT));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGB,       GL_UNSIGNED_INT));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGBA,      GL_UNSIGNED_INT));

//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_ALPHA,     GL_FIXED));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_LUMINANCE, GL_FIXED));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_LUMINANCE_ALPHA, GL_FIXED));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGB,       GL_FIXED));
//	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_RGBA,      GL_FIXED));

	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_SRGB_EXT,  GL_UNSIGNED_BYTE));
	TEST(test_cube_textured(GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, GL_REPEAT, GL_SRGB_ALPHA_EXT, GL_UNSIGNED_BYTE));

	TEST_END();

	return 0;
}

