/*
 * Copyright (c) 2013 Rob Clark <robdclark@gmail.com>
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

/* Code based on example from https://code.google.com/p/opengles-book-samples/
 */

#include "test-util-3d.h"
#include "esUtil.h"

static EGLint const config_attribute_list[] = {
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_DEPTH_SIZE, 8,
	EGL_STENCIL_SIZE, 8,
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
static EGLSurface surface;
static GLuint programObject;
static GLint  positionLoc;
static GLint  normalLoc;
static GLint samplerLoc;
static GLuint textureId;
static int      numIndices;
static GLfloat *vertices;
static GLfloat *normals;
static GLuint *indices;
static GLint width, height;

static GLbyte vShaderStr[] =
   "attribute vec4 a_position;   \n"
   "attribute vec3 a_normal;     \n"
   "varying vec3 v_normal;       \n"
   "void main()                  \n"
   "{                            \n"
   "   gl_Position = a_position; \n"
   "   v_normal = a_normal;      \n"
   "}                            \n";

static GLbyte fShaderStrCube[] =
   "precision mediump float;                            \n"
   "varying vec3 v_normal;                              \n"
   "uniform samplerCube s_texture;                      \n"
   "void main()                                         \n"
   "{                                                   \n"
   "  gl_FragColor = textureCube(s_texture, v_normal);  \n"
   "}                                                   \n";

static GLbyte fShaderStr2d[] =
   "precision mediump float;                            \n"
   "varying vec3 v_normal;                              \n"
   "uniform sampler2D s_texture;                        \n"
   "void main()                                         \n"
   "{                                                   \n"
   "  gl_FragColor = texture2D(s_texture, v_normal.xy); \n"
   "}                                                   \n";

static GLubyte *tex(GLubyte *fill)
{
	static GLubyte buf[1024*1024*4];
#if 1
	int i, j;
	for (i = 0; i < ARRAY_SIZE(buf); )
		for (j = 0; j < 3; )
			buf[i++] = fill[j++];
#else
	static int cnt = 0;
	memset(buf, cnt++, sizeof(buf));
#endif
	return buf;
}

///
// Create a simple cubemap with a 1x1 face with a different
// color for each face
static GLuint CreateSimpleTextureCubemap(int cube, int w, int h)
{
   GLuint textureId;
   // Six 1x1 RGB faces
   GLubyte cubePixels[6][3] =
   {
      // Face 0 - Red
      255, 0, 0,
      // Face 1 - Green,
      0, 255, 0,
      // Face 3 - Blue
      0, 0, 255,
      // Face 4 - Yellow
      255, 255, 0,
      // Face 5 - Purple
      255, 0, 255,
      // Face 6 - White
      255, 255, 255
   };
   int type = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;

   // Generate a texture object
   glGenTextures ( 1, &textureId );

   // Bind the texture object
   glBindTexture ( type, textureId );

   if (cube) {
   // Load the cube face - Positive X
   glTexImage2D ( GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_RGB, w, h, 0,
                  GL_RGB, GL_UNSIGNED_BYTE, tex(cubePixels[0]) );

   // Load the cube face - Negative X
   glTexImage2D ( GL_TEXTURE_CUBE_MAP_NEGATIVE_X, 0, GL_RGB, w, h, 0,
                  GL_RGB, GL_UNSIGNED_BYTE, tex(cubePixels[1]) );

   // Load the cube face - Positive Y
   glTexImage2D ( GL_TEXTURE_CUBE_MAP_POSITIVE_Y, 0, GL_RGB, w, h, 0,
                  GL_RGB, GL_UNSIGNED_BYTE, tex(cubePixels[2]) );

   // Load the cube face - Negative Y
   glTexImage2D ( GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, 0, GL_RGB, w, h, 0,
                  GL_RGB, GL_UNSIGNED_BYTE, tex(cubePixels[3]) );

   // Load the cube face - Positive Z
   glTexImage2D ( GL_TEXTURE_CUBE_MAP_POSITIVE_Z, 0, GL_RGB, w, h, 0,
                  GL_RGB, GL_UNSIGNED_BYTE, tex(cubePixels[4]) );

   // Load the cube face - Negative Z
   glTexImage2D ( GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 0, GL_RGB, w, h, 0,
                  GL_RGB, GL_UNSIGNED_BYTE, tex(cubePixels[5]) );
   } else {
   glTexImage2D ( GL_TEXTURE_2D, 0, GL_RGB, w, h, 0,
                  GL_RGB, GL_UNSIGNED_BYTE, tex(cubePixels[0]) );
   }

   // Set the filtering mode
   glTexParameteri ( type, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
   glTexParameteri ( type, GL_TEXTURE_MAG_FILTER, GL_NEAREST );

   return textureId;
}

///
// Draw a triangle using the shader pair created in Init()
//
void Draw (int cube)
{
   // Set the viewport
   glViewport ( 0, 0, width, height );

   // Clear the color buffer
   glClear ( GL_COLOR_BUFFER_BIT );

   glCullFace ( GL_BACK );
   glEnable ( GL_CULL_FACE );

   // Use the program object
   glUseProgram ( programObject );

   // Load the vertex position
   glVertexAttribPointer ( positionLoc, 3, GL_FLOAT,
                           GL_FALSE, 0, vertices );
   // Load the normal
   glVertexAttribPointer ( normalLoc, 3, GL_FLOAT,
                           GL_FALSE, 0, normals );

   glEnableVertexAttribArray ( positionLoc );
   glEnableVertexAttribArray ( normalLoc );

   // Bind the texture
   glActiveTexture ( GL_TEXTURE0 );
   glBindTexture ( cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, textureId );

   // Set the sampler texture unit to 0
   glUniform1i ( samplerLoc, 0 );

   glDrawElements ( GL_TRIANGLES, numIndices,
                    GL_UNSIGNED_INT, indices );
}

static void test_cubemap(int cube, int w, int h)
{
	RD_START("cubemap", "cube=%d, %dx%d", cube, w, h);

	display = get_display();

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	DEBUG_MSG("num_config: %d", num_config);

	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));

	surface = make_window(display, config, 400, 240);

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("Buffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));

	programObject = get_program(vShaderStr, cube ? fShaderStrCube : fShaderStr2d);
	link_program(programObject);

	// Get the attribute locations
	GCHK(positionLoc = glGetAttribLocation(programObject, "a_position"));
	GCHK(normalLoc = glGetAttribLocation(programObject, "a_normal"));

	// Get the sampler locations
	GCHK(samplerLoc = glGetUniformLocation(programObject, "s_texture"));

	// Load the texture
	GCHK(textureId = CreateSimpleTextureCubemap(cube, w, h));

	// Generate the vertex data
	numIndices = esGenSphere(20, 0.75f, &vertices, &normals, NULL, &indices);

	GCHK(glClearColor(0.0f, 0.0f, 0.0f, 0.0f));

	GCHK(Draw(cube));

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

#ifndef BIONIC
	sleep(5);
#endif

	ECHK(eglDestroySurface(display, surface));
#ifdef BIONIC
	ECHK(eglTerminate(display));
#endif

	RD_END();
}

int main(int argc, char *argv[])
{
	TEST_START();
	TEST(test_cubemap(0, 1, 1));
	TEST(test_cubemap(1, 1, 1));
	TEST(test_cubemap(1, 4, 4));
	TEST(test_cubemap(1, 40, 40));
	TEST(test_cubemap(1, 256, 256));
	TEST(test_cubemap(1, 512, 512));
	TEST(test_cubemap(1, 1024, 1024));
	TEST_END();
	return 0;
}

#ifdef BIONIC
void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}
#endif
