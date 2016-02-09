/*
 * Copyright (c) 2011-2012 Luc Verhaegen <libv@codethink.co.uk>
 * Copyright (c) 2016 Rob Clark <robdclark@gmail.com>
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

/* test using AMD_performance_monitor extension to excercise different
 * perf cntrs..
 */

#include <GLES3/gl3.h>
#include "test-util-3d.h"

#ifndef GL_AMD_performance_monitor
#define GL_COUNTER_TYPE_AMD                                     0x8BC0
#define GL_COUNTER_RANGE_AMD                                    0x8BC1
#define GL_UNSIGNED_INT64_AMD                                   0x8BC2
#define GL_PERCENTAGE_AMD                                       0x8BC3
#define GL_PERFMON_RESULT_AVAILABLE_AMD                         0x8BC4
#define GL_PERFMON_RESULT_SIZE_AMD                              0x8BC5
#define GL_PERFMON_RESULT_AMD                                   0x8BC6
#endif

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
static EGLSurface surface;
static GLuint program;
static GLint width, height;
static int uniform_location;
const char *vertex_shader_source =
	"attribute vec4 aPosition;    \n"
	"                             \n"
	"void main()                  \n"
	"{                            \n"
	"    gl_Position = aPosition; \n"
	"}                            \n";
const char *fragment_shader_source =
	"precision highp float;       \n"
	"uniform vec4 uColor;         \n"
	"                             \n"
	"void main()                  \n"
	"{                            \n"
	"    gl_FragColor = uColor;   \n"
	"}                            \n";

void test_perf(int n, int w, int h)
{
	static const GLfloat clear_color[] = {0.0, 0.0, 0.0, 0.0};
	static const GLfloat quad_color[]  = {1.0, 0.0, 0.0, 1.0};
	static const GLfloat quad2_color[]  = {0.0, 1.0, 0.0, 1.0};
	static const GLfloat vertices[] = {
			-0.45, -0.75, 0.0,
			 0.45, -0.75, 0.0,
			-0.45,  0.75, 0.0,
			 0.45,  0.75, 0.0,
	};
	static const GLfloat vertices2[] = {
			-0.15, -0.23, 1.0,
			 0.25, -0.33, 1.0,
			-0.35,  0.43, 1.0,
			 0.45,  0.53, 1.0,
	};

	RD_START("perf", "%d", n);
	display = get_display();

	/* get an appropriate EGL frame buffer configuration */
	ECHK(eglChooseConfig(display, config_attribute_list, &config, 1, &num_config));
	DEBUG_MSG("num_config: %d", num_config);

	/* create an EGL rendering context */
	ECHK(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribute_list));

	surface = make_window(display, config, w, h);

	ECHK(eglQuerySurface(display, surface, EGL_WIDTH, &width));
	ECHK(eglQuerySurface(display, surface, EGL_HEIGHT, &height));

	DEBUG_MSG("Buffer: %dx%d", width, height);

	/* connect the context to the surface */
	ECHK(eglMakeCurrent(display, surface, surface, context));

	program = get_program(vertex_shader_source, fragment_shader_source);

	GCHK(glBindAttribLocation(program, 0, "aPosition"));

	link_program(program);

	GCHK(glDepthMask(GL_TRUE));
	GCHK(glEnable(GL_DEPTH_TEST));

	GCHK(glViewport(0, 0, width, height));

	if (clear_color) {
		/* clear the color buffer */
		GCHK(glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]));
		GCHK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
	}

	/* Query available perf cntrs: */
	GLint num_groups;
	GLuint *groups;

	GCHK(glGetPerfMonitorGroupsAMD(&num_groups, 0, NULL));
	groups = (GLuint*) malloc(num_groups * sizeof(GLuint));
	GCHK(glGetPerfMonitorGroupsAMD(NULL, num_groups, groups));

	for (int i = 0 ; i < num_groups; i++ ) {
		GLint num_counters, max_active_counters;
		GLint *counters;
		char name[256];

		GCHK(glGetPerfMonitorCountersAMD(groups[i], &num_counters,
				&max_active_counters, 0, NULL));

		counters = (GLuint*)malloc(num_counters * sizeof(int));

		GCHK(glGetPerfMonitorCountersAMD(groups[i], NULL, NULL,
				num_counters, counters));

		GCHK(glGetPerfMonitorGroupStringAMD(groups[i], sizeof(name), NULL, name));

		DEBUG_MSG("######### group[%d]: %s\n", i, name);

		void *counter_range = calloc(1, 4096);

		for (int j = 0; j < num_counters; j++) {
			GLuint monitor;

			GCHK(glGetPerfMonitorCounterStringAMD(groups[i], counters[j],
					sizeof(name), NULL, name));
			DEBUG_MSG("#########   counter[%d]: %s\n", j, name);

			// Determine the counter type
			GLuint counter_type;
			GCHK(glGetPerfMonitorCounterInfoAMD(groups[i], counters[j],
					GL_COUNTER_TYPE_AMD, &counter_type));
			switch (counter_type) {
			case GL_UNSIGNED_INT:
				DEBUG_MSG("#########          type: unsigned int\n");
				break;
			case GL_UNSIGNED_INT64_AMD:
				DEBUG_MSG("#########          type: unsigned int64\n");
				break;
			case GL_PERCENTAGE_AMD:
				DEBUG_MSG("#########          type: float (percentage)\n");
				break;
			case GL_FLOAT:
				DEBUG_MSG("#########          type: float\n");
				break;
			}

			GCHK(glGetPerfMonitorCounterInfoAMD(groups[i], counters[j],
					GL_COUNTER_RANGE_AMD, counter_range));
			switch (counter_type) {
			case GL_UNSIGNED_INT:
				DEBUG_MSG("#########         range: %u..%u\n", ((uint32_t *)counter_range)[0], ((uint32_t *)counter_range)[1]);
				break;
			case GL_UNSIGNED_INT64_AMD:
				DEBUG_MSG("#########         range: %llx..%llx\n", ((uint64_t *)counter_range)[0], ((uint64_t *)counter_range)[1]);
				break;
			case GL_PERCENTAGE_AMD:
			case GL_FLOAT:
				DEBUG_MSG("#########         range: %f..%f\n", ((float *)counter_range)[0], ((float *)counter_range)[1]);
				break;
			}

			GCHK(glGenPerfMonitorsAMD(1, &monitor));
			GCHK(glSelectPerfMonitorCountersAMD(monitor, GL_TRUE, groups[i], 1, &counters[j]));

			GCHK(glBeginPerfMonitorAMD(monitor));

			/* draw something: */
			GCHK(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices));
			GCHK(glEnableVertexAttribArray(0));

			/* now set up our uniform. */
			GCHK(uniform_location = glGetUniformLocation(program, "uColor"));

			GCHK(glUniform4fv(uniform_location, 1, quad_color));
			GCHK(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

			GCHK(glEndPerfMonitorAMD(monitor));

			GLuint counter_data[4096];
			GCHK(glGetPerfMonitorCounterDataAMD(monitor, GL_PERFMON_RESULT_AMD,
					sizeof(counter_data), counter_data, NULL));
		}
	}

	ECHK(eglSwapBuffers(display, surface));
	GCHK(glFlush());

	usleep(1000000);

	eglTerminate(display);

	RD_END();
}

int main(int argc, char *argv[])
{
	TEST_START();
	TEST(test_perf(0,  128,  128));
	TEST_END();

	return 0;
}

#ifdef BIONIC
void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}
#endif
