

#include <GLES/gl.h>  /* use OpenGL ES 1.x */
#include <GLES/glext.h>
#include <EGL/egl.h>

#include "test-util-3d.h"


static EGLint const config_attribute_list[] = {
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,
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
static GLint width, height;

static void test_gles1(int n)
{
	static const GLfloat verts[] = { 0.0, 0.0, 0.0 };

	RD_START("gles1alpha", "%d", n);
	display = get_display();

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	DEBUG_MSG("num_config: %d", num_config);

	eglBindAPI(EGL_OPENGL_API);

	surface = make_window(display, config, 400, 240);

	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("Buffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));

	GCHK(glClearColor(0.4, 0.4, 0.4, 0.0));

	GCHK(glClear(GL_COLOR_BUFFER_BIT));
	GCHK(glViewport(0, 0, width, height));

	switch (n) {
	case 0:
		break;
	case 1:
		GCHK(glEnable(GL_ALPHA_TEST));
		GCHK(glAlphaFunc(GL_EQUAL, 0.4));
		break;
	case 2:
		GCHK(glEnable(GL_ALPHA_TEST));
		GCHK(glAlphaFunc(GL_LEQUAL, 0.7));
		break;
	}

	GCHK(glColor4f(1.0, 1.0, 1.0, 1.0));
	GCHK(glVertexPointer(3, GL_FLOAT, 0, verts));
	GCHK(glEnableClientState(GL_VERTEX_ARRAY));
	GCHK(glDrawArrays(GL_POINTS, 0, 1));
	GCHK(glFlush());

	readback();

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	RD_END();
}

int
main(int argc, char *argv[])
{
	TEST_START();
	TEST(test_gles1(0));
	TEST(test_gles1(1));
	TEST(test_gles1(2));
	TEST_END();
	return 0;
}

