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

/*
 * 00      CF:     ADDR(0x2) CNT(0x5)
 *         00955002 00001000 c4000000
 *                 ALU:    10002021 1ffff688 00000002
 *                           ^ ^ ^
 *                           | | +-- src reg (coord, bit offset 5)
 *                           | +---- dst register
 *                           +------ sampler # (CONST(n)
 *                 ALU:    10101021 1ffff688 00000002
 *                 ALU:    10200001 1ffff688 00000002
 *                 ALU:    140f0001 00220000 e0020100
 *                                ^   ^^^^    ^ ^ ^
 *                                |    | |    | | +-- src2
 *                                |    | |    | +---- src1
 *                                |    | |    +------ op, 0:ADDv, 1:MULv
 *                                |    | +----------- src2 swizzle
 *                                |    +------------- src1 swizzle
 *                                |
 *                                +------------------ dst
 *                 ALU:    140f0000 00008800 e1000100
 * 01      CF:     ADDR(0x7) CNT(0x1)
 *         00001007 00002000 00000000
 *                 ALU:    140f8000 00430000 a1000000
 *
 */

#define REG_MASK 0x1f	/* not really sure how many regs yet */

static int disasm_alu(uint32_t *dwords, int level)
{
	static const char *op_name[0xf] = {
			[0]  = "ADDv",
			[1]  = "MULv",
			[2]  = "MAXv",
			[11] = "MULADDv",
	};
	uint32_t dst_reg  =  dwords[0] & REG_MASK;
	uint32_t src1_reg = (dwords[2] >> 16) & REG_MASK;
	uint32_t src2_reg = (dwords[2] >> 8) & REG_MASK;
	char src1_type    = (dwords[2] & 0x80000000) ? 'R' : 'C';
	char src2_type    = (dwords[2] & 0x40000000) ? 'R' : 'C';
	uint32_t op       = (dwords[2] >> 24) & 0xf;
	// TODO swizzle

	if (op_name[op]) {
		printf("%s", op_name[op]);
	} else {
		printf("OP(%u)", op);
	}

	printf("\tR%u = %c%u, %c%u\n", dst_reg, src1_type, src1_reg,
			src2_type, src2_reg);
	return 0;
}

static int disasm_fetch(uint32_t *dwords, int level)
{
	// XXX I guess there are other sorts of fetches too??
	static const char *fetch_type = "SAMPLE";
	uint32_t src_const = (dwords[0] >> 20) & 0xf;
	uint32_t src_reg = (dwords[0] >> 5) & REG_MASK;
	uint32_t dst_reg = (dwords[0] >> 12) & REG_MASK;

	printf("%s\tR%u = R%u CONST(%u)\n", fetch_type, dst_reg,
			src_reg, src_const);
	return 0;
}

static int disasm_inst(uint32_t *dwords, int level)
{
	int ret = 0;

	/* I don't know if this is quite the right way to separate
	 * instruction types or not:
	 */
	if (dwords[2] & 0xf0000000) {
		printf("%s\tALU:\t", levels[level]);
		ret = disasm_alu(dwords, level);
	} else {
		printf("%s\tFETCH:\t", levels[level]);
		ret = disasm_fetch(dwords, level);
	}
	printf("%s\t\t%08x %08x %08x\n", levels[level], dwords[0], dwords[1], dwords[2]);
	return ret;
}

int disasm(uint32_t *dwords, int sizedwords, int level)
{
	uint32_t first_off = (dwords[0] & 0xfff);
	uint32_t i = 0, j;
	uint32_t alu_off = first_off * 3;

	/* seems to be special case for last CF: */
	if (dwords[0] == 0) {
		uint32_t off = 1;
		uint32_t cnt = (sizedwords / 3) - off;
		alu_off = off * 3;
		printf("%s%02d\tCF:\tADDR(0x%x) CNT(0x%x)\n", levels[level], 0, off, cnt);
		printf("%s\t%08x %08x %08x\n", levels[level], dwords[0], dwords[1], dwords[2]);
	}

	/* decode CF instructions: */
	for (i = 0; i < first_off; i++) {
		uint32_t idx = i * 3;
		uint32_t off = (dwords[idx] & 0x0fff);
		uint32_t cnt = (dwords[idx] & 0xf000) >> 12;

		/* seems to be special case for last CF: */
		if (dwords[idx] == 0) {
			printf("?");
			off = alu_off / 3;
			cnt = (sizedwords / 3) - off;
		}

		/* make sure we parsed the expected amount of data: */
		while (alu_off != (off * 3)) {
			printf("?");
			disasm_inst(dwords + alu_off, level + 1);
			alu_off += 3;
		}

		printf("%s%02d\tCF:\tADDR(0x%x) CNT(0x%x)\n", levels[level], i, off, cnt);
		printf("%s\t%08x %08x %08x\n", levels[level], dwords[idx], dwords[idx+1], dwords[idx+2]);

		for (j = 0; j < cnt; j++) {
			disasm_inst(dwords + alu_off, level + 1);
			alu_off += 3;
		}
	}

	/* make sure we parsed the expected amount of data: */
	while (alu_off != sizedwords) {
		printf("?");
		disasm_inst(dwords + alu_off, level + 1);
		alu_off += 3;
	}

	return 0;
}
