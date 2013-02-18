/*
 * Copyright (c) 2013 Rob Clark <robdclark@gmail.com>
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

#ifndef INSTR_A3XX_H_
#define INSTR_A3XX_H_

#define PACKED __attribute__((__packed__))

typedef enum {
	/* note, maybe MOV is done w/ ADD Rsrc + 0 ??? */
	OPC_ADD = 0, /* if num_src==2 */
	OPC_MOV = 0, /* if num_src==1 */

	OPC_BARY = 1, /* fetch varying?.. if num_src==2 */

	OPC_MUL = 3,

	OPC_MULADD = 4,  /* if num_src==3 */

} opc_t;

typedef struct PACKED {
	uint32_t comp : 2;
	uint32_t num  : 6;
} reg_t;

typedef struct PACKED {
	/* dword0: */
	reg_t    src1;
	uint32_t src1_flags : 8;
	reg_t    src2;
	uint32_t src2_flags : 8;

	/* dword1: */
	reg_t    dst;
	uint32_t repeat   : 3;
	uint32_t unknown3 : 10;
	opc_t    opc      : 3;
	uint32_t unknown4 : 4;  /* 3 for 'end', looks like 7 in fetch instructions, 0 for alu?? */
	uint32_t sync     : 1;  /* shown as "(sy)" in kernel comments.. I think this is sync bit */
	uint32_t num_src : 3;  /* maybe?  seems to be 1 for 1src ops, like mov, 2 for 2src ops like add/mul.. */
} instr_t;

#endif /* INSTR_A3XX_H_ */
