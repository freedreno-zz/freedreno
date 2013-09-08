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
static GLint  texCoordLoc;
static GLint samplerLoc;
static GLint offsetLoc;
static GLuint textureId;
static GLint width, height;

static GLbyte vShaderStr[] =
   "uniform float u_offset;      \n"
   "attribute vec4 a_position;   \n"
   "attribute vec2 a_texCoord;   \n"
   "varying vec2 v_texCoord;     \n"
   "void main()                  \n"
   "{                            \n"
   "   gl_Position = a_position; \n"
   "   gl_Position.x += u_offset;\n"
   "   v_texCoord = a_texCoord;  \n"
   "}                            \n";

static GLbyte fShaderStr[] =
   "precision mediump float;                            \n"
   "varying vec2 v_texCoord;                            \n"
   "uniform sampler2D s_texture;                        \n"
   "void main()                                         \n"
   "{                                                   \n"
   "  gl_FragColor = texture2D( s_texture, v_texCoord );\n"
   "}                                                   \n";

///
//  From an RGB8 source image, generate the next level mipmap
//
GLboolean GenMipMap2D( GLubyte *src, GLubyte **dst, int srcWidth, int srcHeight, int *dstWidth, int *dstHeight )
{
   int x,
       y;
   int texelSize = 3;

   *dstWidth = srcWidth / 2;
   if ( *dstWidth <= 0 )
      *dstWidth = 1;

   *dstHeight = srcHeight / 2;
   if ( *dstHeight <= 0 )
      *dstHeight = 1;

   *dst = malloc ( sizeof(GLubyte) * texelSize * (*dstWidth) * (*dstHeight) );
   if ( *dst == NULL )
      return GL_FALSE;

   for ( y = 0; y < *dstHeight; y++ )
   {
      for( x = 0; x < *dstWidth; x++ )
      {
         int srcIndex[4];
         float r = 0.0f,
               g = 0.0f,
               b = 0.0f;
         int sample;

         // Compute the offsets for 2x2 grid of pixels in previous
         // image to perform box filter
         srcIndex[0] =
            (((y * 2) * srcWidth) + (x * 2)) * texelSize;
         srcIndex[1] =
            (((y * 2) * srcWidth) + (x * 2 + 1)) * texelSize;
         srcIndex[2] =
            ((((y * 2) + 1) * srcWidth) + (x * 2)) * texelSize;
         srcIndex[3] =
            ((((y * 2) + 1) * srcWidth) + (x * 2 + 1)) * texelSize;

         // Sum all pixels
         for ( sample = 0; sample < 4; sample++ )
         {
            r += src[srcIndex[sample]];
            g += src[srcIndex[sample] + 1];
            b += src[srcIndex[sample] + 2];
         }

         // Average results
         r /= 4.0;
         g /= 4.0;
         b /= 4.0;

         // Store resulting pixels
         (*dst)[ ( y * (*dstWidth) + x ) * texelSize ] = (GLubyte)( r );
         (*dst)[ ( y * (*dstWidth) + x ) * texelSize + 1] = (GLubyte)( g );
         (*dst)[ ( y * (*dstWidth) + x ) * texelSize + 2] = (GLubyte)( b );
      }
   }

   return GL_TRUE;
}

///
//  Generate an RGB8 checkerboard image
//
GLubyte* GenCheckImage( int width, int height, int checkSize )
{
   int x,
       y;
   GLubyte *pixels = malloc( width * height * 3 );

   if ( pixels == NULL )
      return NULL;

   for ( y = 0; y < height; y++ )
      for ( x = 0; x < width; x++ )
      {
         GLubyte rColor = 0;
         GLubyte bColor = 0;

         if ( ( x / checkSize ) % 2 == 0 )
         {
            rColor = 255 * ( ( y / checkSize ) % 2 );
            bColor = 255 * ( 1 - ( ( y / checkSize ) % 2 ) );
         }
         else
         {
            bColor = 255 * ( ( y / checkSize ) % 2 );
            rColor = 255 * ( 1 - ( ( y / checkSize ) % 2 ) );
         }

         pixels[(y * height + x) * 3] = rColor;
         pixels[(y * height + x) * 3 + 1] = 0;
         pixels[(y * height + x) * 3 + 2] = bColor;
      }

   return pixels;
}

///
// Create a mipmapped 2D texture image
//
GLuint CreateMipMappedTexture2D(int maxlevels, int width, int height, unsigned filt)
{
   // Texture object handle
   GLuint textureId;
   int    level;
   GLubyte *pixels;
   GLubyte *prevImage;
   GLubyte *newImage = NULL;

   pixels = GenCheckImage( width, height, 8 );
   if ( pixels == NULL )
      return 0;

   // Generate a texture object
   glGenTextures ( 1, &textureId );

   // Bind the texture object
   glBindTexture ( GL_TEXTURE_2D, textureId );

   // Load mipmap level 0
   glTexImage2D ( GL_TEXTURE_2D, 0, GL_RGB, width, height,
                  0, GL_RGB, GL_UNSIGNED_BYTE, pixels );

   level = 1;
   prevImage = &pixels[0];

   while (level <= maxlevels && width > 1 && height > 1)
   {
      int newWidth,
          newHeight;

      // Generate the next mipmap level
      GenMipMap2D( prevImage, &newImage, width, height,
                   &newWidth, &newHeight );

      // Load the mipmap level
      glTexImage2D( GL_TEXTURE_2D, level, GL_RGB,
                    newWidth, newHeight, 0, GL_RGB,
                    GL_UNSIGNED_BYTE, newImage );

      // Free the previous image
      free ( prevImage );

      // Set the previous image for the next iteration
      prevImage = newImage;
      level++;

      // Half the width and height
      width = newWidth;
      height = newHeight;
   }

   free ( newImage );

   // Set the filtering mode
   glTexParameteri ( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt );
   glTexParameteri ( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

   return textureId;

}

///
// Draw a triangle using the shader pair created in Init()
//
static void Draw(void)
{
   GLfloat vVertices[] = { -0.5f,  0.5f, 0.0f, 1.5f,  // Position 0
                            0.0f,  0.0f,              // TexCoord 0
                           -0.5f, -0.5f, 0.0f, 0.75f, // Position 1
                            0.0f,  1.0f,              // TexCoord 1
                            0.5f, -0.5f, 0.0f, 0.75f, // Position 2
                            1.0f,  1.0f,              // TexCoord 2
                            0.5f,  0.5f, 0.0f, 1.5f,  // Position 3
                            1.0f,  0.0f               // TexCoord 3
                         };
   GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

   // Set the viewport
   glViewport ( 0, 0, width, height );

   // Clear the color buffer
   glClear ( GL_COLOR_BUFFER_BIT );

   // Use the program object
   glUseProgram ( programObject );

   // Load the vertex position
   glVertexAttribPointer ( positionLoc, 4, GL_FLOAT,
                           GL_FALSE, 6 * sizeof(GLfloat), vVertices );
   // Load the texture coordinate
   glVertexAttribPointer ( texCoordLoc, 2, GL_FLOAT,
                           GL_FALSE, 6 * sizeof(GLfloat), &vVertices[4] );

   glEnableVertexAttribArray ( positionLoc );
   glEnableVertexAttribArray ( texCoordLoc );

   // Bind the texture
   glActiveTexture ( GL_TEXTURE0 );
   glBindTexture ( GL_TEXTURE_2D, textureId );

   // Set the sampler texture unit to 0
   glUniform1i ( samplerLoc, 0 );

   // Draw quad with nearest sampling
   glTexParameteri ( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
   glUniform1f ( offsetLoc, -0.6f );
   glDrawElements ( GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices );

   // Draw quad with trilinear filtering
   glTexParameteri ( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
   glUniform1f ( offsetLoc, 0.6f );
   glDrawElements ( GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices );

}

static void test_mipmap(int maxlevels, int w, int h, unsigned filt)
{
	RD_START("mipmap", "maxlevels=%d, texsize=%dx%d", maxlevels, w, h);

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

	programObject = get_program(vShaderStr, fShaderStr);
	link_program(programObject);

	// Get the attribute locations
	GCHK(positionLoc = glGetAttribLocation(programObject, "a_position"));
	GCHK(texCoordLoc = glGetAttribLocation(programObject, "a_texCoord"));

	// Get the sampler location
	GCHK(samplerLoc = glGetUniformLocation(programObject, "s_texture"));

	// Get the offset location
	GCHK(offsetLoc = glGetUniformLocation(programObject, "u_offset"));

	// Load the texture
	GCHK(textureId = CreateMipMappedTexture2D(maxlevels, w, h, filt));

	GCHK(glClearColor(0.0f, 0.0f, 0.0f, 0.0f));

	GCHK(Draw());

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
	test_mipmap(0, 64, 64, GL_NEAREST_MIPMAP_NEAREST);
	test_mipmap(3, 64, 64, GL_LINEAR_MIPMAP_NEAREST);
	test_mipmap(8, 256, 256, GL_NEAREST_MIPMAP_LINEAR);
	test_mipmap(10, 1024, 1024, GL_LINEAR_MIPMAP_LINEAR);
}

#ifdef BIONIC
void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}
#endif
