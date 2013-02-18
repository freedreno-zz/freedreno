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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "disasm.h"
#include "instr-a3xx.h"

typedef enum {
	true = 1, false = 0,
} bool;

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

static void print_reg(reg_t *reg, uint8_t flags, bool immed)
{
	static const char *component = "xyzw";
	const char type = (flags & 0x10) ? 'c' : 'r';

	if (flags & 0x40)  /* neg */
		printf("-");
	if (flags & 0x80)  /* abs */
		printf("|");

	if (flags & 0x20)  /* repeat.. if rpt is set, keep same value for repeated instrs */
		printf("(r)");

	if (immed) {
		// TODO do we interpret the entire 8 bits as immediate value, since
		// the component seems to be unusued?
		printf("%d", reg->num);
	} else {
		printf("%c%d.%c", type, reg->num, component[reg->comp]);
	}

	if (flags & 0x80)  /* abs */
		printf("|");
}

static void print_instr(uint32_t *dwords, int level)
{
	instr_t *instr = (instr_t *)dwords;

	printf("%s%08x %08x\t", levels[level], dwords[0], dwords[1]);

	if (instr->sync)
		printf("(sy)");
	if (instr->repeat)
		printf("(rpt%d)", instr->repeat);

	/* handle these two as special cases for now, until instruction
	 * encoding is better understood:
	 */
	if ((dwords[0] == 0x00000000) && ((dwords[1] & ~0x10000700) == 0x00000000)) {
		printf("nop");
	} else if ((dwords[0] == 0x00000000) && ((dwords[1] & ~0x10000700) == 0x03000000)) {
		printf("end");
	} else {
		const char *name = NULL;
		bool src1_immed = false;

		switch (instr->num_src) {
		case 1:
			switch (instr->opc) {
			case OPC_MOV:	name = "mov.f32f32";	break;
			}
			break;
		case 2:
			switch (instr->opc) {
			case OPC_ADD:	name = "add.f32f32";	break;
			case OPC_BARY:	name = "bary.f"; 		src1_immed = true;	break;
			case OPC_MUL:	name = "mul.f32f32";	break;
			}
			break;
		case 3:
			switch (instr->opc) {
			case OPC_MULADD:	name = "muladd";	break;
			}
			break;
		}

		if (name)
			printf("%s", name);
		else
			printf("unknown(%d,%d)", instr->num_src, instr->opc);

		printf(" ");
		print_reg(&instr->dst, 0, false);
		printf(", ");
		print_reg(&instr->src1, instr->src1_flags, src1_immed);
		if (instr->num_src >= 2) {
			printf(", ");
			print_reg(&instr->src2, instr->src2_flags, false);
		}
		if (instr->num_src >= 3) {
			// ???
		}
	}

	printf("\n");
}

int disasm_a3xx(uint32_t *dwords, int sizedwords, int level, enum shader_t type)
{
	int i;

	assert((sizedwords % 2) == 0);

	for (i = 0; i < sizedwords; i += 2)
		print_instr(&dwords[i], level);

	return 0;
}
