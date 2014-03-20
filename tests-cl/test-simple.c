/*
 * Copyright (c) 2014 Rob Clark <robdclark@gmail.com>
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
		" __kernel void simple(__global float *out, __global float *in) \n"
		"{                                                              \n"
		"    int iGID = get_global_id(0);                               \n"
		"    out[iGID] = in[iGID];                                      \n"
		"}                                                              \n";

static char buffer[4096 * 4096 * 4];



static void run_test(uint32_t workdim, const size_t *workoff,
		const size_t *global, const size_t *local)
{
	unsigned int num_platforms;
	int err, i;
	size_t len;

	cl_platform_id platform;
	cl_device_id device_id;             // compute device id
	cl_context context;                 // compute context
	cl_command_queue commands;          // compute command queue
	cl_program program;                 // compute program
	cl_kernel kernel;                   // compute kernel

	cl_mem mem_in;
	cl_mem mem_out;

	static char buf[4096];
	char *p = buf;

	p += sprintf(p, "workdim=%u, workoff={", workdim);
	if (workoff)
		for (i = 0; i < workdim; i++)
			p += sprintf(p, "%u,", workoff[i]);
	p += sprintf(p, "}, global={");
	for (i = 0; i < workdim; i++)
		p += sprintf(p, "%u,", global[i]);
	p += sprintf(p, "}, local={");
	for (i = 0; i < workdim; i++)
		p += sprintf(p, "%u,", local[i]);
	p += sprintf(p, "}");
	RD_START("simple", "%s", buf);

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

	kernel = clCreateKernel(program, "simple", NULL);

	mem_in  = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(cl_float) * global[0] * 4, NULL, &err);
	mem_out = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(cl_float) * global[0], NULL, &err);

	CCHK(clSetKernelArg(kernel, 0, sizeof(cl_mem), (void*)&mem_out));
	CCHK(clSetKernelArg(kernel, 1, sizeof(cl_mem), (void*)&mem_in));

//	CCHK(clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL));

	CCHK(clEnqueueNDRangeKernel(commands, kernel, workdim, workoff, global, local, 0, NULL, NULL));
	CCHK(err);

	CCHK(clFinish(commands));

//	CCHK(clEnqueueReadBuffer( commands, output, CL_TRUE, 0, sizeof(float) * count, results, 0, NULL, NULL));

//	clReleaseMemObject(input1);
//	clReleaseMemObject(input2);
//	clReleaseMemObject(output);
	clReleaseProgram(program);
	clReleaseKernel(kernel);
	clReleaseCommandQueue(commands);
	clReleaseContext(context);

	RD_END();
}

int main(int argc, char *argv)
{
	const size_t sizes[32] = {
			64, 32, 16, 8, 4, 2, 1, 1, 1, 1,
	};
	const size_t sizes2[32] = {
			1, 2, 4, 8,
			16, 32, 64, 128,
			256, 512, 1024, 2048,
	};

	/* 1: */
	run_test(1, NULL, &sizes[1], &sizes[2]);
	run_test(2, NULL, &sizes[1], &sizes[2]);
	run_test(1, NULL, &sizes[2], &sizes[3]);
	run_test(2, NULL, &sizes[2], &sizes[3]);
	run_test(3, NULL, &sizes[2], &sizes[3]);
	/* 5: */
	run_test(1, NULL, &sizes[2], &sizes[4]);
	run_test(2, NULL, &sizes[2], &sizes[4]);
	run_test(3, NULL, &sizes[2], &sizes[4]);
	run_test(1, NULL, &sizes[3], &sizes[4]);
	run_test(2, NULL, &sizes[3], &sizes[4]);
	/* 10: */
	run_test(3, NULL, &sizes[3], &sizes[4]);
	run_test(1, &sizes[2], &sizes[2], &sizes[3]);
	run_test(2, &sizes[2], &sizes[2], &sizes[3]);
	run_test(3, &sizes[2], &sizes[2], &sizes[3]);
	run_test(2, &sizes[1], &sizes[2], &sizes[3]);
	/* 15: */
	run_test(3, &sizes[1], &sizes[2], &sizes[3]);
	run_test(3, &sizes2[4], &sizes[2], &sizes[3]);
	run_test(3, &sizes2[7], &sizes[2], &sizes[3]);
	run_test(1, NULL, &sizes[3], &sizes[3]);
	run_test(1, NULL, &sizes[3], &sizes[4]);
	/* 20: */
	run_test(1, NULL, &sizes[0], &sizes[5]);
	run_test(3, &sizes[3], &sizes2[8], &sizes2[0]);
	run_test(3, &sizes[3], &sizes2[9], &sizes2[1]);
}

#ifdef BIONIC
void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}
#endif
