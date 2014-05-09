/*
 * Copyright (c) 2014 Rob Clark <robdclark@gmail.com>
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

#include <stdlib.h>
#include <stdio.h>

#include "freedreno.h"
#include "redump.h"

static char testbuf[4096];

int main(int argc, char **argv)
{
	struct fd_state *state;
	struct fd_bo *inbuf, *outbuf;
	struct fd_program *kernel;
	uint32_t globalsize[] = {32, 16};
	uint32_t localsize[]  = {16, 8};
	unsigned i;

/*
__kernel void simple(__global float *out, __global float *in)
{
    int iGID = (get_global_id(0) * 32) + get_global_id(1);
    out[iGID] = in[iGID];
}
 */
	const char *kernel_asm =
		"@buf(c5.z) inbuf                                                 \n"
		"@buf(c5.x) outbuf                                                \n"
		"(sy)(rpt4)nop                                                    \n"
		"(sy)(ss)mov.s32s32 r0.w, 0                                       \n"
		"mov.f32f32 r1.y, c5.z                                            \n"
		"mov.f32f32 r1.z, c5.x                                            \n"
		"mov.s32s32 r1.w, 0                                               \n"
		"add.s r2.x, c2.y, r0.x                                           \n"
		"(rpt2)nop                                                        \n"
		"shl.b r2.x, r2.x, 5                                              \n"
		"add.s r2.y, c2.z, r0.y                                           \n"
		"mov.f32f32 r2.z, c4.z                                            \n"
		"(rpt2)nop                                                        \n"
		"cmps.u.lt r2.z, r2.z, 2                                          \n"
		"(rpt2)nop                                                        \n"
		"sel.b32 r1.w, r1.w, r2.z, r2.y                                   \n"
		"(rpt2)nop                                                        \n"
		"add.s r1.w, r1.w, r2.x                                           \n"
		"(rpt2)nop                                                        \n"
		"shl.b r1.w, r1.w, 2                                              \n"
		"(rpt2)nop                                                        \n"
		"add.s r1.y, r1.y, r1.w                                           \n"
		"(rpt5)nop                                                        \n"
		"ldg.f32 r1.y,g[r1.y], 1                                          \n"
		"add.s r1.z, r1.z, r1.w                                           \n"
		"(rpt5)nop                                                        \n"
		"(sy)stg.f32 g[r1.z],r1.y, 1                                      \n"
		"end                                                              \n";

	DEBUG_MSG("----------------------------------------------------------------");
	RD_START("compute-simple", "");

	for (i = 0; i < ARRAY_SIZE(testbuf); i++)
		testbuf[i] = i;

	state = fd_init();
	if (!state)
		return -1;

	kernel = fd_program_new(state);
	fd_program_attach_asm(kernel, FD_SHADER_COMPUTE, kernel_asm);
	fd_set_program(state, kernel);

	inbuf = fd_attribute_bo_new(state, sizeof(testbuf), testbuf);
	fd_set_buf(state, "inbuf", inbuf);

	outbuf = fd_attribute_bo_new(state, sizeof(testbuf), NULL);
	fd_set_buf(state, "outbuf", outbuf);

	fd_run_compute(state, 2, NULL, globalsize, localsize);

	fd_dump_hex_bo(outbuf, true);

	fd_fini(state);

	RD_END();

	return 0;
}
