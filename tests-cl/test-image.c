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

#include <CL/opencl.h>

#include "test-util-common.h"

#define CCHK(x) do { \
		int __err; \
		DEBUG_MSG(">>> %s", #x); \
		RD_WRITE_SECTION(RD_CMD, #x, strlen(#x)); \
		__err = x; \
		if (__err != CL_SUCCESS) { \
			ERROR_MSG("<<< %s: failed: %d", #x, __err); \
			exit(-1); \
		} \
		DEBUG_MSG("<<< %s: succeeded", #x); \
	} while (0)

const char *KernelSource = "\n"
		"__kernel void                                                    \n"
		"test_kernel(__read_only image2d_t input1,                        \n"
		"            __read_only image2d_t input2,                        \n"
		"            __write_only image2d_t output)                       \n"
		"{                                                                \n"
		"  const int2 pos = {get_global_id(0), get_global_id(1)};         \n"
		"  float4 sum = (float4)(0.0f);                                   \n"
		"  for(int y = -FILTER_SIZE; y <= FILTER_SIZE; y++) {             \n"
		"    for(int x = -FILTER_SIZE; x <= FILTER_SIZE; x++) {           \n"
		"      sum += read_imagef(input1, SAMP_MODE, pos + (int2)(x,y));  \n"
		"      sum += read_imagef(input2, SAMP_MODE, pos + (int2)(x,y));  \n"
		"    }                                                            \n"
		"  }                                                              \n"
		"  write_imagef(output, (int2)(pos.x, pos.y), sum);               \n"
		"}                                                                \n";

static char buffer[4096 * 4096 * 4];

static void run_test(int image_width, int image_height, int image_pitch,
		int chan_order, int chan_type)
{
	unsigned int num_platforms;
	int err, i;
#define NAME(x) [x & 0xf] = #x
	static const char *channel_orders[] = {
			NAME(CL_R),
			NAME(CL_A),
			NAME(CL_RG),
			NAME(CL_RA),
			NAME(CL_RGB),
			NAME(CL_RGBA),
			NAME(CL_BGRA),
			NAME(CL_ARGB),
			NAME(CL_INTENSITY),
			NAME(CL_LUMINANCE),
			NAME(CL_Rx),
			NAME(CL_RGx),
			NAME(CL_RGBx),
	};
	static const char *channel_types[] = {
			NAME(CL_SNORM_INT8),
			NAME(CL_SNORM_INT16),
			NAME(CL_UNORM_INT8),
			NAME(CL_UNORM_INT16),
			NAME(CL_UNORM_SHORT_565),
			NAME(CL_UNORM_SHORT_555),
			NAME(CL_UNORM_INT_101010),
			NAME(CL_SIGNED_INT8),
			NAME(CL_SIGNED_INT16),
			NAME(CL_SIGNED_INT32),
			NAME(CL_UNSIGNED_INT8),
			NAME(CL_UNSIGNED_INT16),
			NAME(CL_UNSIGNED_INT32),
			NAME(CL_HALF_FLOAT),
			NAME(CL_FLOAT),
	};

	size_t global;                      // global domain size for our calculation
	size_t local;                       // local domain size for our calculation
	size_t bin_size;
	size_t len;
	unsigned char *bin;

	cl_platform_id platform;
	cl_device_id device_id;             // compute device id
	cl_context context;                 // compute context
	cl_command_queue commands;          // compute command queue
	cl_program program;                 // compute program
	cl_kernel kernel;                   // compute kernel

	cl_mem input1, input2;              // device memory used for the input array
	cl_mem output;                      // device memory used for the output array

	RD_START("image", "width=%d, height=%d, pitch=%d, order=%s, type=%s",
			image_width, image_height, image_pitch,
			channel_orders[chan_order & 0xf], channel_types[chan_type & 0xf]);

	CCHK(clGetPlatformIDs(1, &platform, &num_platforms));
	CCHK(clGetPlatformInfo(platform, CL_PLATFORM_EXTENSIONS, sizeof(buffer), buffer, &len));
	DEBUG_MSG("extensions=%s\n", buffer);

	CCHK(clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device_id, NULL));

	CCHK(clGetDeviceInfo(device_id, CL_DEVICE_VERSION, sizeof(buffer), buffer, &len));
	DEBUG_MSG("version=%s\n", buffer);
	CCHK(clGetDeviceInfo(device_id, CL_DEVICE_EXTENSIONS, sizeof(buffer), buffer, &len));
	DEBUG_MSG("device extensions=%s\n", buffer);

	context = clCreateContext(0, 1, &device_id, NULL, NULL, NULL);
	commands = clCreateCommandQueue(context, device_id, 0, NULL);
	program = clCreateProgramWithSource(context, 1, (const char **) &KernelSource, NULL, NULL);

	CCHK(clBuildProgram(program, 0, NULL, "-cl-mad-enable -DFILTER_SIZE=1 "
			"-DSAMP_MODE=CLK_NORMALIZED_COORDS_FALSE|CLK_ADDRESS_CLAMP_TO_EDGE|CLK_FILTER_NEAREST",
			NULL, NULL));

	kernel = clCreateKernel(program, "test_kernel", NULL);

	/* fill buffer with dummy pattern: */
	for (i = 0; i < 256; i++)
		buffer[i] = i;

	const cl_image_format format = { chan_order, chan_type };
	input1 = clCreateImage2D(context, CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR, &format,
			image_width, image_height, image_pitch, buffer, &err);
	CCHK(err);
	input2 = clCreateImage2D(context, CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR, &format,
			image_width+1, image_height+1, image_pitch, buffer + 4, &err);
	CCHK(err);

	output = clCreateImage2D(context, CL_MEM_WRITE_ONLY, &format,
			image_width+2, image_height+2, 0, NULL, &err);
	CCHK(err);

	CCHK(clSetKernelArg(kernel, 0, sizeof(cl_mem), &input1));
	CCHK(clSetKernelArg(kernel, 1, sizeof(cl_mem), &input2));
	CCHK(clSetKernelArg(kernel, 2, sizeof(cl_mem), &output));

	CCHK(clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL));

	global = 1024;
	CCHK(clEnqueueNDRangeKernel(commands, kernel, 1, NULL, &global, &local, 0, NULL, NULL));

	CCHK(clFinish(commands));

//	CCHK(clEnqueueReadBuffer( commands, output, CL_TRUE, 0, sizeof(float) * count, results, 0, NULL, NULL));

	clReleaseMemObject(input1);
	clReleaseMemObject(input2);
	clReleaseMemObject(output);
	clReleaseProgram(program);
	clReleaseKernel(kernel);
	clReleaseCommandQueue(commands);
	clReleaseContext(context);

	RD_END();
}

int main(int argc, char *argv)
{
	run_test(204, 132, 4096, CL_RGBA, CL_UNORM_INT8);
	run_test(204, 132, 4096, CL_RGBA, CL_UNORM_INT16);
	run_test(204, 132, 4096, CL_RGBA, CL_SIGNED_INT8);
	run_test(204, 132, 4096, CL_RGBA, CL_SIGNED_INT16);
	run_test(204, 132, 4096, CL_RGBA, CL_SIGNED_INT32);
	run_test(204, 132, 4096, CL_RGBA, CL_UNSIGNED_INT8);
	run_test(204, 132, 4096, CL_RGBA, CL_UNSIGNED_INT16);
	run_test(204, 132, 4096, CL_RGBA, CL_UNSIGNED_INT32);
	run_test(204, 132, 4096, CL_RGBA, CL_HALF_FLOAT);
	run_test(204, 132, 4096, CL_RGBA, CL_FLOAT);
	run_test(204, 132, 4096, CL_BGRA, CL_UNORM_INT8);
	run_test(204, 132, 4096, CL_BGRA, CL_UNORM_INT16);
	run_test(204, 132, 4096, CL_BGRA, CL_SIGNED_INT8);
	run_test(204, 132, 4096, CL_BGRA, CL_SIGNED_INT16);
	run_test(204, 132, 4096, CL_BGRA, CL_SIGNED_INT32);
	run_test(204, 132, 4096, CL_BGRA, CL_UNSIGNED_INT8);
	run_test(204, 132, 4096, CL_BGRA, CL_UNSIGNED_INT16);
	run_test(204, 132, 4096, CL_BGRA, CL_UNSIGNED_INT32);
	run_test(204, 132, 4096, CL_BGRA, CL_HALF_FLOAT);
	run_test(204, 132, 4096, CL_BGRA, CL_FLOAT);
	run_test(204, 132, 4096, CL_R, CL_UNORM_INT16);
	run_test(204, 132, 4096, CL_R, CL_SIGNED_INT16);
	run_test(204, 132, 4096, CL_R, CL_SIGNED_INT32);
	run_test(204, 132, 4096, CL_R, CL_UNSIGNED_INT16);
	run_test(204, 132, 4096, CL_R, CL_UNSIGNED_INT32);
	run_test(204, 132, 4096, CL_R, CL_HALF_FLOAT);
	run_test(204, 132, 4096, CL_R, CL_FLOAT);
	return 0;
}

#ifdef BIONIC
void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}
#endif
