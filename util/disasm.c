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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "disasm.h"
#include "a2xx_reg.h"
#include "fdre/asm/instr.h"

static const char *levels[] = {
		"\t",
		"\t\t",
		"\t\t\t",
		"\t\t\t\t",
		"\t\t\t\t\t",
		"\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t\t\t",
		"x",
		"x",
		"x",
		"x",
		"x",
		"x",
};

static enum debug_t debug;

/*
 * ALU instructions:
 */

#define REG_MASK 0x3f	/* not really sure how many regs yet */

static const char chan_names[] = {
		'x', 'y', 'z', 'w',
		/* these only apply to FETCH dst's: */
		'0', '1', '?', '_',
};

static void print_srcreg(uint32_t num, uint32_t type,
		uint32_t swiz, uint32_t negate, uint32_t abs)
{
	if (negate)
		printf("-");
	if (abs)
		printf("|");
	printf("%c%u", type ? 'R' : 'C', num);
	if (swiz) {
		int i;
		printf(".");
		for (i = 0; i < 4; i++) {
			printf("%c", chan_names[(swiz + i) & 0x3]);
			swiz >>= 2;
		}
	}
	if (abs)
		printf("|");
}

static void print_dstreg(uint32_t num, uint32_t mask, uint32_t dst_exp)
{
	printf("%s%u", dst_exp ? "export" : "R", num);
	if (mask != 0xf) {
		int i;
		printf(".");
		for (i = 0; i < 4; i++) {
			printf("%c", (mask & 0x1) ? chan_names[i] : '_');
			mask >>= 1;
		}
	}
}

static void print_export_comment(uint32_t num, enum shader_t type)
{
	const char *name = NULL;
	switch (type) {
	case SHADER_VERTEX:
		switch (num) {
		case 62: name = "gl_Position";  break;
		case 63: name = "gl_PointSize"; break;
		}
		break;
	case SHADER_FRAGMENT:
		switch (num) {
		case 0:  name = "gl_FragColor"; break;
		}
		break;
	}
	/* if we had a symbol table here, we could look
	 * up the name of the varying..
	 */
	if (name) {
		printf("\t; %s", name);
	}
}

struct {
	uint32_t num_srcs;
	const char *name;
} vector_instructions[0x20] = {
#define INSTR(opc, num_srcs) [opc] = { num_srcs, #opc }
		INSTR(ADDv, 2),
		INSTR(MULv, 2),
		INSTR(MAXv, 2),
		INSTR(MINv, 2),
		INSTR(SETEv, 2),
		INSTR(SETGTv, 2),
		INSTR(SETGTEv, 2),
		INSTR(SETNEv, 2),
		INSTR(FRACv, 1),
		INSTR(TRUNCv, 1),
		INSTR(FLOORv, 1),
		INSTR(MULADDv, 3),
		INSTR(CNDEv, 2),
		INSTR(CNDGTEv, 2),
		INSTR(CNDGTv, 2),
		INSTR(DOT4v, 2),
		INSTR(DOT3v, 2),
		INSTR(DOT2ADDv, 3),  // ???
		INSTR(CUBEv, 2),
		INSTR(MAX4v, 1),
		INSTR(PRED_SETE_PUSHv, 2),
		INSTR(PRED_SETNE_PUSHv, 2),
		INSTR(PRED_SETGT_PUSHv, 2),
		INSTR(PRED_SETGTE_PUSHv, 2),
		INSTR(KILLEv, 2),
		INSTR(KILLGTv, 2),
		INSTR(KILLGTEv, 2),
		INSTR(KILLNEv, 2),
		INSTR(DSTv, 2),
		INSTR(MOVAv, 1),
}, scalar_instructions[0x40] = {
		INSTR(ADDs, 1),
		INSTR(ADD_PREVs, 1),
		INSTR(MULs, 1),
		INSTR(MUL_PREVs, 1),
		INSTR(MUL_PREV2s, 1),
		INSTR(MAXs, 1),
		INSTR(MINs, 1),
		INSTR(SETEs, 1),
		INSTR(SETGTs, 1),
		INSTR(SETGTEs, 1),
		INSTR(SETNEs, 1),
		INSTR(FRACs, 1),
		INSTR(TRUNCs, 1),
		INSTR(FLOORs, 1),
		INSTR(EXP_IEEE, 1),
		INSTR(LOG_CLAMP, 1),
		INSTR(LOG_IEEE, 1),
		INSTR(RECIP_CLAMP, 1),
		INSTR(RECIP_FF, 1),
		INSTR(RECIP_IEEE, 1),
		INSTR(RECIPSQ_CLAMP, 1),
		INSTR(RECIPSQ_FF, 1),
		INSTR(RECIPSQ_IEEE, 1),
		INSTR(MOVAs, 1),
		INSTR(MOVA_FLOORs, 1),
		INSTR(SUBs, 1),
		INSTR(SUB_PREVs, 1),
		INSTR(PRED_SETEs, 1),
		INSTR(PRED_SETNEs, 1),
		INSTR(PRED_SETGTs, 1),
		INSTR(PRED_SETGTEs, 1),
		INSTR(PRED_SET_INVs, 1),
		INSTR(PRED_SET_POPs, 1),
		INSTR(PRED_SET_CLRs, 1),
		INSTR(PRED_SET_RESTOREs, 1),
		INSTR(KILLEs, 1),
		INSTR(KILLGTs, 1),
		INSTR(KILLGTEs, 1),
		INSTR(KILLNEs, 1),
		INSTR(KILLONEs, 1),
		INSTR(SQRT_IEEE, 1),
		INSTR(MUL_CONST_0, 1),
		INSTR(MUL_CONST_1, 1),
		INSTR(ADD_CONST_0, 1),
		INSTR(ADD_CONST_1, 1),
		INSTR(SUB_CONST_0, 1),
		INSTR(SUB_CONST_1, 1),
		INSTR(SIN, 1),
		INSTR(COS, 1),
		INSTR(RETAIN_PREV, 1),
#undef INSTR
};

static int disasm_alu(uint32_t *dwords, int level, int sync, enum shader_t type)
{
	instr_alu_t *alu = (instr_alu_t *)dwords;

	printf("%s", levels[level]);
	if (debug & PRINT_RAW) {
		printf("%08x %08x %08x\t", dwords[0], dwords[1], dwords[2]);
	}

	printf("   %sALU:\t", sync ? "(S)" : "   ");

	printf(vector_instructions[alu->vector_opc].name);

	if (alu->pred_select & 0x2) {
		/* seems to work similar to conditional execution in ARM instruction
		 * set, so let's use a similar syntax for now:
		 */
		printf((alu->pred_select & 0x1) ? "EQ" : "NE");
	}

	printf("\t");

	print_dstreg(alu->vector_dest, alu->vector_write_mask, alu->export_data);
	printf(" = ");
	if (vector_instructions[alu->vector_opc].num_srcs == 3) {
		print_srcreg(alu->src3_reg, alu->src3_sel, alu->src3_swiz,
				alu->src3_reg_negate, alu->src3_reg_abs);
		printf(", ");
	}
	print_srcreg(alu->src1_reg, alu->src1_sel, alu->src1_swiz,
			alu->src1_reg_negate, alu->src1_reg_abs);
	if (vector_instructions[alu->vector_opc].num_srcs > 1) {
		printf(", ");
		print_srcreg(alu->src2_reg, alu->src2_sel, alu->src2_swiz,
				alu->src2_reg_negate, alu->src2_reg_abs);
	}

	if (alu->export_data)
		print_export_comment(alu->vector_dest, type);

	printf("\n");

	if (alu->scalar_write_mask || !alu->vector_write_mask) {
		/* 2nd optional scalar op: */

		printf("%s", levels[level]);
		if (debug & PRINT_RAW)
			printf("                          \t");

		if (scalar_instructions[alu->scalar_opc].name) {
			printf("\t    \t%s\t", scalar_instructions[alu->scalar_opc].name);
		} else {
			printf("\t    \tOP(%u)\t", alu->scalar_opc);
		}

		print_dstreg(alu->scalar_dest, alu->scalar_write_mask, alu->export_data);
		printf(" = ");
		print_srcreg(alu->src3_reg, alu->src3_sel, alu->src3_swiz,
				alu->src3_reg_negate, alu->src3_reg_abs);
		// TODO ADD/MUL must have another src?!?
		if (alu->export_data)
			print_export_comment(alu->scalar_dest, type);
		printf("\n");
	}

	return 0;
}

/*
 * VTX FETCH instruction format:
 * --- ----- ----------- ------
 *
 * Note: w/ sequence of VERTEX fetches, re-ordered into order of attributes
 * in memory, I get:
 *
 *    +------ bits 25..26
 *   /     +- dword[0]
 *  /     /
 * / /----+---\
 * 0 11 4 82000 40393a88 0000000c    FETCH:	VERTEX	R2.xyz1 = R0.xyx FMT_32_32_32_FLOAT SIGNED STRIDE(12) CONST(4)
 * 1 1b 4 81000 00263688 00000010    FETCH:	VERTEX	R1.xyzw = R0.zyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(4)
 * 2 15 4 83000 40263688 00000010    FETCH:	VERTEX	R3.xyzw = R0.yyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(4)
 * 0 11 5 84000 40263688 00000010    FETCH:	VERTEX	R4.xyzw = R0.xyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(5)
 * 1 13 5 86000 40263688 00000010    FETCH:	VERTEX	R6.xyzw = R0.xyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(5)
 * 2 15 5 85000 40263688 00000010    FETCH:	VERTEX	R5.xyzw = R0.yyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(5)
 * 0 11 6 88000 40263688 00000010    FETCH:	VERTEX	R8.xyzw = R0.xyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(6)
 * 1 13 6 87000 40263688 00000010    FETCH:	VERTEX	R7.xyzw = R0.xyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(6)
 * 2 15 6 8b000 40263688 00000010    FETCH:	VERTEX	R11.xyzw = R0.yyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(6)
 * 0 11 7 89000 40263688 00000010    FETCH:	VERTEX	R9.xyzw = R0.xyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(7)
 * 1 13 7 8c000 40263688 00000010    FETCH:	VERTEX	R12.xyzw = R0.xyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(7)
 * 2 15 7 8a000 40263688 00000010    FETCH:	VERTEX	R10.xyzw = R0.yyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(7)
 * 0 11 8 8e000 40263688 00000010    FETCH:	VERTEX	R14.xyzw = R0.xyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(8)
 * 1 13 8 8d000 40263688 00000010    FETCH:	VERTEX	R13.xyzw = R0.xyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(8)
 * 2 15 8 8f000 40263688 00000010    FETCH:	VERTEX	R15.xyzw = R0.yyx FMT_32_32_32_32_FLOAT SIGNED STRIDE(16) CONST(8)
 *
 * So maybe 25..26 is src reg swizzle, and somewhere in 20..23 is some sort of
 * offset?
 *
 * Note that GL_MAX_VERTEX_ATTRIBS is 16 (although possibly should be 15,
 * as I get an error binding attribute >14!)
 *
 *     dword0:   0..4?   -  fetch operation - 0x00
 *               5..10?  -  src register
 *                11     -  <UNKNOWN>
 *              12..17?  -  dest register
 *             18?..19   -  <UNKNOWN>
 *              20..23?  -  const
 *              24..25   -  <UNKNOWN>  (maybe part of const?)
 *              25..26   -  src swizzle (x)
 *                            00 - x
 *                            01 - y
 *                            10 - z
 *                            11 - w
 *              27..31   -  unknown
 *
 *     dword1:   0..11   -  dest swizzle/mask, 3 bits per channel (w/z/y/x),
 *                          low two bits of each determine position src channel,
 *                          high bit set 1 to mask
 *                12     -  signedness ('1' signed, '0' unsigned)
 *              13..15   -  <UNKNOWN>
 *              16..21?  -  type - see 'enum SURFACEFORMAT'
 *             22?..31   -  <UNKNOWN>
 *
 *     dword2:   0..15   -  stride (more than 0xff and data is copied/packed)
 *              16..31   -  <UNKNOWN>
 *
 * note: at least VERTEX fetch instructions get patched up at runtime
 * based on the size of attributes attached.. for example, if vec4, but
 * size given in glVertexAttribPointer() then the write mask and size
 * (stride?) is updated
 *
 * TEX FETCH instruction format:
 * --- ----- ----------- ------
 *
 *     dword0:   0..4?   -  fetch operation - 0x01
 *               5..10?  -  src register
 *                11     -  <UNKNOWN>
 *              12..17?  -  dest register
 *             18?..19   -  <UNKNOWN>
 *              20..23?  -  const
 *              24..25   -  <UNKNOWN>  (maybe part of const?)
 *              26..31   -  src swizzle (z/y/x)
 *                            00 - x
 *                            01 - y
 *                            10 - z
 *                            11 - w
 *
 *     dword1:   0..11   -  dest swizzle/mask, 3 bits per channel (w/z/y/x),
 *                          low two bits of each determine position src channel,
 *                          high bit set 1 to mask
 *                12     -  signedness ('1' signed, '0' unsigned)
 *              13..31   -  <UNKNOWN>
 *
 *     dword2:   0..31   -  <UNKNOWN>
 */

enum fetch_opc {
	VERTEX = 0x00,
	SAMPLE = 0x01,
};

struct {
	const char *name;
} fetch_instructions[0x1f] = {
#define INSTR(opc) [opc] = { #opc }
		INSTR(VERTEX),
		INSTR(SAMPLE),
#undef INSTR
};

struct {
	const char *name;
} fetch_types[0xff] = {
#define TYPE(id) [id] = { #id }
		TYPE(FMT_1_REVERSE),
		TYPE(FMT_32_FLOAT),
		TYPE(FMT_32_32_FLOAT),
		TYPE(FMT_32_32_32_FLOAT),
		TYPE(FMT_32_32_32_32_FLOAT),
		TYPE(FMT_16),
		TYPE(FMT_16_16),
		TYPE(FMT_16_16_16_16),
		TYPE(FMT_8),
		TYPE(FMT_8_8),
		TYPE(FMT_8_8_8_8),
		TYPE(FMT_32),
		TYPE(FMT_32_32),
		TYPE(FMT_32_32_32_32),
#undef TYPE
};

static int disasm_fetch(uint32_t *dwords, int level, int sync)
{
	uint32_t src_const = (dwords[0] >> 20) & 0xf;
	uint32_t src_reg   = (dwords[0] >> 5) & REG_MASK;
	uint32_t dst_reg   = (dwords[0] >> 12) & REG_MASK;
	uint32_t fetch_opc =  dwords[0] & 0x1f;
	uint32_t dst_swiz  =  dwords[1] & 0xfff;
	int i;

	printf("%s", levels[level]);
	if (debug & PRINT_RAW) {
		printf("%08x %08x %08x\t", dwords[0], dwords[1], dwords[2]);
	}

	printf("   %sFETCH:\t", sync ? "(S)" : "   ");
	if (fetch_instructions[fetch_opc].name) {
		printf(fetch_instructions[fetch_opc].name);
	} else {
		printf("OP(%u)", fetch_opc);
	}

	printf("\tR%u.", dst_reg);
	for (i = 0; i < 4; i++) {
		printf("%c", chan_names[dst_swiz & 0x7]);
		dst_swiz >>= 3;
	}

	if (fetch_opc == VERTEX) {
		uint32_t src_swiz  = (dwords[0] >> 25) & 0x3;
		uint32_t sign      = (dwords[1] >> 12) & 0x1;
		uint32_t type      = (dwords[1] >> 16) & 0x3f;
		uint32_t stride    =  dwords[2] & 0xff;
		printf(" = R%u.", src_reg);
		printf("%c", chan_names[src_swiz & 0x3]);
		if (fetch_types[type].name) {
			printf(" %s", fetch_types[type].name);
		} else  {
			printf(" TYPE(0x%x)", type);
		}
		printf(" %s", sign ? "SIGNED" : "UNSIGNED");
		printf(" STRIDE(%d)", stride);
	} else {
		uint32_t src_swiz  = (dwords[0] >> 26) & 0x3f;
		printf(" = R%u.", src_reg);
		for (i = 0; i < 3; i++) {
			printf("%c", chan_names[src_swiz & 0x3]);
			src_swiz >>= 2;
		}
	}

	printf(" CONST(%u)\n", src_const);

	return 0;
}

/*
 * CF instructions:
 */

static int cf_exec(instr_cf_t *cf)
{
	return (cf->opc == EXEC) ||
			(cf->opc == EXEC_END) ||
			(cf->opc == COND_EXEC) ||
			(cf->opc == COND_EXEC_END) ||
			(cf->opc == COND_PRED_EXEC) ||
			(cf->opc == COND_PRED_EXEC_END) ||
			(cf->opc == COND_EXEC_PRED_CLEAN) ||
			(cf->opc == COND_EXEC_PRED_CLEAN_END);
}

static int cf_cond_exec(instr_cf_t *cf)
{
	return (cf->opc == COND_EXEC) ||
			(cf->opc == COND_EXEC_END) ||
			(cf->opc == COND_PRED_EXEC) ||
			(cf->opc == COND_PRED_EXEC_END) ||
			(cf->opc == COND_EXEC_PRED_CLEAN) ||
			(cf->opc == COND_EXEC_PRED_CLEAN_END);
}

static void print_cf_nop(instr_cf_t *cf)
{
}

static void print_cf_exec(instr_cf_t *cf)
{
	printf(" ADDR(0x%x) CNT(0x%x)", cf->exec.address, cf->exec.count);
	if (cf->exec.yeild)
		printf(" YIELD");
	if (cf->exec.vc)
		printf(" VC(0x%x)", cf->exec.vc);
	if (cf->exec.bool_addr)
		printf(" BOOL_ADDR(0x%x)", cf->exec.bool_addr);
	if (cf->exec.address_mode == ABSOLUTE_ADDR)
		printf(" ABSOLUTE_ADDR");
	if (cf_cond_exec(cf))
		printf(" COND(%d)", cf->exec.condition);
}

static void print_cf_loop(instr_cf_t *cf)
{
	printf(" ADDR(0x%x) LOOP_ID(%d)", cf->loop.address, cf->loop.loop_id);
	if (cf->loop.address_mode == ABSOLUTE_ADDR)
		printf(" ABSOLUTE_ADDR");
}

static void print_cf_jmp_call(instr_cf_t *cf)
{
	printf(" ADDR(0x%x) DIR(%d)", cf->jmp_call.address, cf->jmp_call.direction);
	if (cf->jmp_call.force_call)
		printf(" FORCE_CALL");
	if (cf->jmp_call.predicated_jmp)
		printf(" COND(%d)", cf->jmp_call.condition);
	if (cf->jmp_call.bool_addr)
		printf(" BOOL_ADDR(0x%x)", cf->jmp_call.bool_addr);
	if (cf->jmp_call.address_mode == ABSOLUTE_ADDR)
		printf(" ABSOLUTE_ADDR");
}

static void print_cf_alloc(instr_cf_t *cf)
{
	static const char *bufname[] = {
			[SQ_NO_ALLOC] = "NO ALLOC",
			[SQ_POSITION] = "POSITION",
			[SQ_PARAMETER_PIXEL] = "PARAM/PIXEL",
			[SQ_MEMORY] = "MEMORY",
	};
	printf(" %s SIZE(0x%x)", bufname[cf->alloc.buffer_select], cf->alloc.size);
	if (cf->alloc.no_serial)
		printf(" NO_SERIAL");
	if (cf->alloc.alloc_mode) // ???
		printf(" ALLOC_MODE");
}

struct {
	const char *name;
	void (*fxn)(instr_cf_t *cf);
} cf_instructions[] = {
#define INSTR(opc, fxn) [opc] = { #opc, fxn }
		INSTR(NOP, print_cf_nop),
		INSTR(EXEC, print_cf_exec),
		INSTR(EXEC_END, print_cf_exec),
		INSTR(COND_EXEC, print_cf_exec),
		INSTR(COND_EXEC_END, print_cf_exec),
		INSTR(COND_PRED_EXEC, print_cf_exec),
		INSTR(COND_PRED_EXEC_END, print_cf_exec),
		INSTR(LOOP_START, print_cf_loop),
		INSTR(LOOP_END, print_cf_loop),
		INSTR(COND_CALL, print_cf_jmp_call),
		INSTR(RETURN, print_cf_jmp_call),
		INSTR(COND_JMP, print_cf_jmp_call),
		INSTR(ALLOC, print_cf_alloc),
		INSTR(COND_EXEC_PRED_CLEAN, print_cf_exec),
		INSTR(COND_EXEC_PRED_CLEAN_END, print_cf_exec),
		INSTR(MARK_VS_FETCH_DONE, print_cf_nop),  // ??
#undef INSTR
};

static void print_cf(instr_cf_t *cf, int level)
{
	printf("%s", levels[level]);
	if (debug & PRINT_RAW) {
		uint16_t *words = (uint16_t *)cf;
		printf("%04x %04x %04x            \t", words[0], words[1], words[2]);
	}
	printf(cf_instructions[cf->opc].name);
	cf_instructions[cf->opc].fxn(cf);
	printf("\n");
}

/*
 * The adreno shader microcode consists of two parts:
 *   1) A CF (control-flow) program, at the header of the compiled shader,
 *      which refers to ALU/FETCH instructions that follow it by address.
 *   2) ALU and FETCH instructions
 */

int disasm(uint32_t *dwords, int sizedwords, int level, enum shader_t type)
{
	instr_cf_t *cfs = (instr_cf_t *)dwords;
	int off, idx, max_idx;

	for (idx = 0; ; idx++) {
		instr_cf_t *cf = &cfs[idx];
		if (cf_exec(cf)) {
			max_idx = 2 * cf->exec.address;
			break;
		}
	}

	for (idx = 0; idx < max_idx; idx++) {
		instr_cf_t *cf = &cfs[idx];

		print_cf(cf, level);

		if (cf_exec(cf)) {
			uint32_t sequence = cf->exec.serialize;
			uint32_t i;
			for (i = 0; i < cf->exec.count; i++) {
				uint32_t alu_off = (cf->exec.address + i) * 3;
				if (sequence & 0x1) {
					disasm_fetch(dwords + alu_off, level, sequence & 0x2);
				} else {
					disasm_alu(dwords + alu_off, level, sequence & 0x2, type);
				}
				sequence >>= 2;
			}
		}
	}

	return 0;
}

void disasm_set_debug(enum debug_t d)
{
	debug= d;
}
