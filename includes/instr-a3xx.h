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
	OPC_BR = 1,
	OPC_JUMP = 2,
	OPC_CALL = 3,
	OPC_RET = 4,
	OPC_END = 6,

	/* category 1: */
	/* no opc.. all category 1 are variants of mov */

	/* category 2: */
	OPC_ADD_F = 0,
	OPC_MIN_F = 1,
	OPC_MAX_F = 2,
	OPC_MUL_F = 3,
	OPC_CMPS_F = 5,
	OPC_ABSNEG_F = 6,
	OPC_ADD_S = 17,
	OPC_SUB_S = 19,
	OPC_CMPS_U = 20,
	OPC_CMPS_S = 21,
	OPC_MIN_S = 23,
	OPC_MAX_S = 25,
	OPC_ABSNEG_S = 26,
	OPC_AND_B = 28,
	OPC_OR_B = 29,
	OPC_XOR_B = 31,
	OPC_MUL_S = 49,
	OPC_MULL_U = 50,
	OPC_CLZ_B = 53,
	OPC_SHL_B = 54,
	OPC_SHR_B = 55,
	OPC_ASHR_B = 56,
	OPC_BARY_F = 57,

	/* category 3: */
	OPC_MADSH_M16 = 3,
	OPC_MAD_F32 = 7,
	OPC_SEL_B16 = 8,
	OPC_SEL_B32 = 9,
	OPC_SEL_F32 = 13,

	/* category 4: */
	OPC_RCP = 0,
	OPC_RSQ = 1,
	OPC_LOG2 = 2,
	OPC_EXP2 = 3,
	OPC_SIN = 4,
	OPC_COS = 5,
	OPC_SQRT = 6,

	/* category 5: */
	OPC_ISAM = 0,
	OPC_SAM = 3,
	OPC_GETSIZE = 10,
	OPC_GETINFO = 13,

	/* category 6: */
	// XXX these don't quite fit.. possibly the F32/U32/etc is not
	// part of the opc, but another field..
	//    ldg.u32 -> c006....
	//    ldg.f32 -> c002....
	//    stg.u32 -> c0c6....
	//    stg.f32 -> c0c2....
	OPC_LDG = 0,        /* load-global */
	OPC_LDP = 2,
	OPC_STG = 3,        /* store-global */
	OPC_STP = 5,
	OPC_STI = 6,

} opc_t;

typedef enum {
	TYPE_F16 = 0,
	TYPE_F32 = 1,
	TYPE_U16 = 2,
	TYPE_U32 = 3,
	TYPE_S16 = 4,
	TYPE_S32 = 5,
	TYPE_U8  = 6,
	TYPE_S8  = 7,  // XXX I assume?
} type_t;

static inline uint32_t type_size(type_t type)
{
	switch (type) {
	case TYPE_F32:
	case TYPE_U32:
	case TYPE_S32:
		return 32;
	case TYPE_F16:
	case TYPE_U16:
	case TYPE_S16:
		return 16;
	case TYPE_U8:
	case TYPE_S8:
		return 8;
	default:
		assert(0); /* invalid type */
		return 0;
	}
}

static inline int type_float(type_t type)
{
	return (type == TYPE_F32) || (type == TYPE_F16);
}

typedef union PACKED {
	/* normal gpr or const src register: */
	struct PACKED {
		uint32_t comp  : 2;
		uint32_t num   : 9;
	};
	/* for immediate val: */
	int32_t  iim_val : 11;
	/* to make compiler happy: */
	uint32_t dummy12   : 11;
	uint32_t dummy8    : 8;
} reg_t;

// XXX remove this:
typedef struct PACKED {
	/* dword0: */
	uint32_t src1     : 8;
	uint32_t dummy1   : 8;
	uint32_t src2     : 8;
	uint32_t dummy2   : 8;

	/* dword1: */
	uint32_t dst      : 8;
	uint32_t repeat   : 3;
	uint32_t dummy3   : 1;
	uint32_t ss       : 1;
	uint32_t dummy4   : 8;
	uint32_t opc      : 6;
	uint32_t jmp_tgt  : 1;
	uint32_t sync     : 1;
	uint32_t opc_cat  : 3;
} instr_generic_t;

typedef struct PACKED {
	/* dword0: */
	int16_t  immed    : 16;
	uint32_t dummy1   : 16;

	/* dword1: */
	uint32_t dummy2   : 8;
	uint32_t repeat   : 3;
	uint32_t dummy3   : 9;
	uint32_t inv      : 1;
	uint32_t comp     : 2;
	uint32_t opc      : 4;
	uint32_t jmp_tgt  : 1;
	uint32_t sync     : 1;
	uint32_t opc_cat  : 3;
} instr_cat0_t;

typedef struct PACKED {
	/* dword0: */
	union PACKED {
		/* for normal src register: */
		struct PACKED {
			uint32_t src : 11;
			uint32_t pad : 21;
		};
		/* for address relative: */
		struct PACKED {
			// XXX need to find some more examples.. everything I find has
			// bits 10 & 11 set.. unsure if bit 9 is part of the offset or
			// not..
			uint32_t off : 10;
			uint32_t unknown : 22;
		};
		/* for immediate: */
		int32_t iim_val;
		float   fim_val;
	};

	/* dword1: */
	uint32_t dst        : 8;
	uint32_t repeat     : 3;
	uint32_t src_r      : 1;
	uint32_t ss         : 1;
	uint32_t addr_rel   : 1;
	uint32_t dst_type   : 3;
	uint32_t dummy1     : 1;
	uint32_t src_type   : 3;
	uint32_t src_c      : 1;
	uint32_t src_im     : 1;
	uint32_t even       : 1;
	uint32_t dummy2     : 3;
	uint32_t jmp_tgt    : 1;
	uint32_t sync       : 1;
	uint32_t opc_cat    : 3;
} instr_cat1_t;

typedef struct PACKED {
	/* dword0: */
	uint32_t src1     : 11;
	uint32_t dummy1   : 1;
	uint32_t src1_c   : 1;
	uint32_t dummy2   : 2;  // XXX im, neg
	uint32_t src1_abs : 1;
	uint32_t src2     : 11;
	uint32_t dummy3   : 1;
	// XXX if these are in same order everwhere, combine into one flags field..
	uint32_t src2_c   : 1;   /* const */
	uint32_t src2_im  : 1;   /* immediate */
	uint32_t src2_neg : 1;   /* negate */
	uint32_t dummy4   : 1;  // XXX abs

	/* dword1: */
	uint32_t dst      : 8;
	uint32_t repeat   : 3;
	uint32_t src1_r   : 1;
	uint32_t ss       : 1;
	uint32_t dummy5   : 1;
	uint32_t dst_half : 1;   /* or widen/narrow? */
	uint32_t dummy6   : 1;
	uint32_t cond     : 3;
	uint32_t src2_r   : 1;
	uint32_t full     : 1;   /* not half */
	uint32_t opc      : 6;
	uint32_t jmp_tgt  : 1;
	uint32_t sync     : 1;
	uint32_t opc_cat  : 3;
} instr_cat2_t;

typedef struct PACKED {
	/* dword0: */
	uint32_t src1     : 11;
	uint32_t dummy1   : 1;
	uint32_t src1_c   : 1;
	uint32_t dummy2   : 3;  // XXX im, neg, abs
	uint32_t src3     : 11;
	uint32_t dummy3   : 1;
	uint32_t src3_c   : 1;
	uint32_t src3_r   : 1;
	uint32_t dummy4   : 2;  // XXX im, neg, abs?

	/* dword1: */
	uint32_t dst      : 8;
	uint32_t repeat   : 3;
	uint32_t src1_r   : 1;
	uint32_t ss       : 1;
	uint32_t dummy5   : 2;
	uint32_t src2     : 8;
	uint32_t opc      : 4;
	uint32_t jmp_tgt  : 1;
	uint32_t sync     : 1;
	uint32_t opc_cat  : 3;
} instr_cat3_t;

typedef struct PACKED {
	/* dword0: */
	uint32_t src1     : 8;
	uint32_t dummy1   : 24;

	/* dword1: */
	uint32_t dst      : 8;
	uint32_t repeat   : 3;
	uint32_t dummy2   : 1;
	uint32_t ss       : 1;
	uint32_t dummy3   : 8;
	uint32_t opc      : 6;
	uint32_t jmp_tgt  : 1;
	uint32_t sync     : 1;
	uint32_t opc_cat  : 3;
} instr_cat4_t;

typedef struct PACKED {
	/* dword0: */
	uint32_t dummy1   : 32;

	/* dword1: */
	uint32_t dummy2   : 22;
	uint32_t opc      : 5;
	uint32_t jmp_tgt  : 1;
	uint32_t sync     : 1;
	uint32_t opc_cat  : 3;
} instr_cat5_t;

typedef struct PACKED {
	/* dword0: */
	uint32_t dummy1   : 32;

	/* dword1: */
	uint32_t dummy2   : 17;
	uint32_t type     : 3;
	uint32_t dummy3   : 2; // XXX maybe still part of 'type'?
	uint32_t opc      : 5;
	uint32_t jmp_tgt  : 1;
	uint32_t sync     : 1;
	uint32_t opc_cat  : 3;
} instr_cat6_t;

typedef union PACKED {
	instr_generic_t generic;
	instr_cat0_t cat0;
	instr_cat1_t cat1;
	instr_cat2_t cat2;
	instr_cat3_t cat3;
	instr_cat4_t cat4;
	instr_cat5_t cat5;
	instr_cat6_t cat6;
	struct PACKED {
		uint64_t dummy    : 59;
		uint32_t jmp_tgt  : 1;  /* jump target? */
		uint32_t sync     : 1;  /* shown as "(sy)" in kernel comments.. I think this is sync bit */
		uint32_t opc_cat  : 3;
	};
} instr_t;

#endif /* INSTR_A3XX_H_ */
