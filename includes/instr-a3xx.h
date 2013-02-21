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
	/* category 0: */
	OPC_NOP = 0,
	OPC_BR = 4,
	OPC_BR_2 = 5, // XXX low bit not opcode!!
	OPC_JUMP = 8,
	OPC_CALL = 12,
	OPC_RET = 16,
	OPC_END = 24,

	/* category 1: */
	// XXX cov.* also intersects w/ this.. so opc is probably different
	// range of bits for category 1??
	OPC_MOV_F32F32_2 = 0, // XXX maybe we have one less bit for opc in category 1 ??
	OPC_MOV_F32F32 = 1,
	OPC_MOV_S32S32 = 2,

	/* category 2: */
	OPC_ADD_F = 0,
	OPC_MIN_F = 1,
	OPC_MAX_F = 2,
	OPC_MUL_F = 3,
	OPC_CMPS_F = 5,
	OPC_ABSNEG_F = 6,  // XXX 1 src
	OPC_ADD_S = 17,
	OPC_SUB_S = 19,
	OPC_CMPS_U = 20,
	OPC_CMPS_S = 21,
	OPC_MIN_S = 23,
	OPC_MAX_S = 25,
	OPC_ABSNEG_S = 26, // XXX seems to only have 1 src register???
	OPC_AND_B = 28,
	OPC_OR_B = 29,
	OPC_XOR_B = 31,
	OPC_MUL_S = 49,
	OPC_MULL_U = 50,
	OPC_CLZ_B = 53,  // XXX seems to only have 1 src register???
	OPC_SHL_B = 54,
	OPC_SHR_B = 55,
	OPC_ASHR_B = 56,

	/* category 3: */
	// XXX not, 3rd src register appears to be in dword1[15..22], but
	// that intersects w/ opc.. so possibly different categories have
	// different encoding..
	OPC_MADSH_M16 = 12,
	OPC_SEL_B16 = 32,
	OPC_SEL_B32 = 36,
	OPC_SEL_F32 = 52,

	/* category 4: */
	// XXX only one src register for these..
	OPC_RCP = 0,
	OPC_RSQ = 1,
	OPC_LOG2 = 2,
	OPC_EXP2 = 3,
	OPC_SIN = 4,
	OPC_COS = 5,
	OPC_SQRT = 6,

	/* category 6: */
	// XXX these don't quite fit.. possibly the F32/U32/etc is not
	// part of the opc, but another field..
	//    ldg.u32 -> c006....
	//    ldg.f32 -> c002....
	//    stg.u32 -> c0c6....
	//    stg.f32 -> c0c2....
	OPC_LDG = 0,        /* load-global */
	OPC_LDP = 4,
	OPC_STG = 6,        /* store-global */
	OPC_STP = 10,

// ????
//	OPC_BARY = 1,       /* barycentric interpolation */
} opc_t;

typedef union PACKED {
	struct PACKED {
		uint32_t comp  : 2;
		uint32_t num   : 6;
	};
	uint32_t const_val : 8;
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
	uint32_t unknown3 : 1;
	uint32_t ss       : 1;  /* ?? maybe only for category 0 ?? */
	uint32_t unknown4 : 8;
	opc_t    opc      : 6;
	uint32_t jmp_tgt  : 1;  /* jump target? */
	uint32_t sync     : 1;  /* shown as "(sy)" in kernel comments.. I think this is sync bit */
	uint32_t opc_cat  : 3;  /* maybe?  seems to be 1 for 1src ops, like mov, 2 for 2src ops like add/mul.. */
} instr_t;

#endif /* INSTR_A3XX_H_ */
