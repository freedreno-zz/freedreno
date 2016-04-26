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
#ifndef GL_ES_VERSION_2_0
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

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

#define ENUM(x) case x: return #x
static inline const char *
typename(GLenum type)
{
	switch(type) {
	ENUM(GL_BYTE);
	ENUM(GL_UNSIGNED_BYTE);
	ENUM(GL_SHORT);
	ENUM(GL_UNSIGNED_SHORT);
	ENUM(GL_INT);
	ENUM(GL_UNSIGNED_INT);
	ENUM(GL_FIXED);
	ENUM(GL_FLOAT);
#ifdef GL_UNSIGNED_INT_10_10_10_2_OES
	ENUM(GL_UNSIGNED_INT_10_10_10_2_OES);
#endif
#ifdef GL_INT_10_10_10_2_OES
	ENUM(GL_INT_10_10_10_2_OES);
#endif
	ENUM(GL_UNSIGNED_SHORT_5_6_5);
	ENUM(GL_UNSIGNED_SHORT_4_4_4_4);
	ENUM(GL_UNSIGNED_SHORT_5_5_5_1);
#ifdef GL_HALF_FLOAT_OES
	ENUM(GL_HALF_FLOAT_OES);
#endif
#ifdef GL_BGRA_EXT
	ENUM(GL_BGRA_EXT);
#endif
#ifdef GL_UNSIGNED_INT_2_10_10_10_REV_EXT
	ENUM(GL_UNSIGNED_INT_2_10_10_10_REV_EXT);
#endif
#ifdef GL_UNSIGNED_INT_2_10_10_10_REV
	ENUM(GL_UNSIGNED_INT_2_10_10_10_REV);
#endif
#ifdef GL_HALF_FLOAT
	ENUM(GL_HALF_FLOAT);
#endif
	}
	ERROR_MSG("invalid type: %04x", type);
	exit(1);
	return NULL;
}

static inline const char *
formatname(GLenum format)
{
	switch (format) {
	ENUM(GL_RGB);
	ENUM(GL_RGBA);
	ENUM(GL_ALPHA);
	ENUM(GL_LUMINANCE);
	ENUM(GL_LUMINANCE_ALPHA);
	ENUM(GL_DEPTH_COMPONENT);
#ifdef GL_R8
	ENUM(GL_R8);
#endif
#ifdef GL_RGB8
	ENUM(GL_RGB8);
#endif
#ifdef GL_RED
	ENUM(GL_RED);
#endif
#ifdef GL_RGBA_INTEGER
	ENUM(GL_RGBA_INTEGER);
#endif
#ifdef GL_RGB16F
	ENUM(GL_RGB16F);
#endif
#ifdef GL_SRGB_EXT
	ENUM(GL_SRGB_EXT);
#endif
#ifdef GL_SRGB_ALPHA_EXT
	ENUM(GL_SRGB_ALPHA_EXT);
#endif
#ifdef GL_SRGB8_ALPHA8_EXT
	ENUM(GL_SRGB8_ALPHA8_EXT);
#endif
#ifdef GL_SRGB
	ENUM(GL_SRGB);
#endif
#ifdef GL_SRGB8
	ENUM(GL_SRGB8);
#endif
#ifdef GL_SRGB8_ALPHA8
	ENUM(GL_SRGB8_ALPHA8);
#endif
#ifdef GL_RGB10_A2UI
	ENUM(GL_RGB10_A2UI);
#endif
#ifdef GL_RGBA8UI
	ENUM(GL_RGBA8UI);
#endif
#ifdef GL_RGB5_A1
	ENUM(GL_RGB5_A1);
#endif
#ifdef GL_RGBA8
	ENUM(GL_RGBA8);
#endif
	}
	ERROR_MSG("invalid format: %04x", format);
	exit(1);
	return NULL;
}

static inline const char *
textypename(GLenum type)
{
	switch (type) {
	ENUM(GL_TEXTURE_2D);
#ifdef GL_TEXTURE_2D_ARRAY
	ENUM(GL_TEXTURE_2D_ARRAY);
#endif
#ifdef GL_TEXTURE_3D
	ENUM(GL_TEXTURE_3D);
#endif
	ENUM(GL_TEXTURE_CUBE_MAP);
	}
	ERROR_MSG("invalid type: %04x", type);
	exit(1);
	return NULL;
}

static char *
eglStrError(EGLint error)
{
	switch (error) {
	ENUM(EGL_SUCCESS);
	ENUM(EGL_NOT_INITIALIZED);
	ENUM(EGL_BAD_ALLOC);
	ENUM(EGL_BAD_CONFIG);
	ENUM(EGL_BAD_PARAMETER);
	ENUM(EGL_BAD_MATCH);
	ENUM(EGL_BAD_ATTRIBUTE);
	default:
		return "UNKNOWN";
	}
}

static char *
glStrError(GLenum error)
{
	switch (error) {
	// TODO
	ENUM(GL_INVALID_ENUM);
	ENUM(GL_INVALID_OPERATION);
	ENUM(GL_INVALID_FRAMEBUFFER_OPERATION);
	default:
		return "UNKNOWN";
	}
}

#ifndef BIONIC
#  include <X11/Xlib.h>
#  include <X11/Xutil.h>
#  include <X11/keysym.h>
#endif

static EGLNativeDisplayType native_dpy;

static EGLDisplay
get_display(void)
{
	EGLDisplay display;
	EGLint egl_major, egl_minor;

#ifdef BIONIC
	native_dpy = EGL_DEFAULT_DISPLAY;
#else
	native_dpy = XOpenDisplay(NULL);
#endif
	display = eglGetDisplay(native_dpy);
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

static EGLSurface make_window(EGLDisplay display, EGLConfig config, int width, int height)
{
	EGLSurface surface;
#ifdef BIONIC
	EGLint pbuffer_attribute_list[] = {
		EGL_WIDTH, width,
		EGL_HEIGHT, height,
		EGL_LARGEST_PBUFFER, EGL_TRUE,
		EGL_NONE
	};
	ECHK(surface = eglCreatePbufferSurface(display, config, pbuffer_attribute_list));
#else
	XVisualInfo *visInfo, visTemplate;
	int num_visuals;
	Window root, xwin;
	XSetWindowAttributes attr;
	unsigned long mask;
	EGLint vid;
	const char *title = "egl";

	ECHK(eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &vid));

	/* The X window visual must match the EGL config */
	visTemplate.visualid = vid;
	visInfo = XGetVisualInfo(native_dpy, VisualIDMask, &visTemplate, &num_visuals);
	if (!visInfo) {
		ERROR_MSG("failed to get an visual of id 0x%x", vid);
		exit(-1);
	}

	root = RootWindow(native_dpy, DefaultScreen(native_dpy));

	/* window attributes */
	attr.background_pixel = 0;
	attr.border_pixel = 0;
	attr.colormap = XCreateColormap(native_dpy,
			root, visInfo->visual, AllocNone);
	attr.event_mask = StructureNotifyMask | ExposureMask | KeyPressMask;
	mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

	xwin = XCreateWindow(native_dpy, root, 0, 0, width, height,
			0, visInfo->depth, InputOutput, visInfo->visual, mask, &attr);
	if (!xwin) {
		ERROR_MSG("failed to create a window");
		exit (-1);
	}

	XFree(visInfo);

	/* set hints and properties */
	{
		XSizeHints sizehints;
		sizehints.x = 0;
		sizehints.y = 0;
		sizehints.width  = width;
		sizehints.height = height;
		sizehints.flags = USSize | USPosition;
		XSetNormalHints(native_dpy, xwin, &sizehints);
		XSetStandardProperties(native_dpy, xwin,
				title, title, None, (char **) NULL, 0, &sizehints);
	}

	XMapWindow(native_dpy, xwin);

	surface = eglCreateWindowSurface(display, config, xwin, NULL);
#endif
	return surface;
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

static void readback(void)
{
	char buf[64];
	GCHK(glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, buf));
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

static inline void
hexdump(const void *data, int size)
{
	unsigned char *buf = (void *) data;
	char alpha[17];
	int i;

	for (i = 0; i < size; i++) {
		if (!(i % 16))
			printf("\t\t\t%08X", (unsigned int) i);
		if (!(i % 4))
			printf(" ");

		if (((void *) (buf + i)) < ((void *) data)) {
			printf("   ");
			alpha[i % 16] = '.';
		} else {
			printf(" %02x", buf[i]);

			if (isprint(buf[i]) && (buf[i] < 0xA0))
				alpha[i % 16] = buf[i];
			else
				alpha[i % 16] = '.';
		}

		if ((i % 16) == 15) {
			alpha[16] = 0;
			printf("\t|%s|\n", alpha);
		}
	}

	if (i % 16) {
		for (i %= 16; i < 16; i++) {
			printf("   ");
			alpha[i] = '.';

			if (i == 15) {
				alpha[16] = 0;
				printf("\t|%s|\n", alpha);
			}
		}
	}
}

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

#ifdef BIONIC
	/* dump program binary: */
	// TODO move this into wrap-gles.c .. just putting it here for now
	// since I haven't created wrap-gles.c yet
	GCHK(glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH_OES, &len));
	binary = calloc(1, len);
	GCHK(glGetProgramBinaryOES(program, len, &ret, &binary_format, binary));
	DEBUG_MSG("program dump: len=%d, actual len=%d", len, ret);
	hexdump(binary, len);
	RD_WRITE_SECTION(RD_PROGRAM, binary, len);
	free(binary);
#endif
}

/* ************************************************************************* */
/* helper macros for tests, to let test-runner select test to run via
 * TESTNUM env variable.  Note env variable used because passing args
 * when tests are compiled for bionic doesn't really work.
 */

static inline int __gettest(void)
{
	const char *testnum = getenv("TESTNUM");
	if (testnum)
		return strtol(testnum, NULL, 0);
	return -1;
}

#define TEST_START() \
	int __n = 0, __test = __gettest()

#define TEST(t) do { \
		if ((__test == __n++) || (__test == -1)) { \
			t; \
		} \
	} while (0)

#define TEST_END() do { \
		if (__test >= __n++) \
			exit(42); \
	} while (0)

#endif /* TEST_UTIL_3D_H_ */
