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

#include "redump.h"

#include "freedreno_z1xx.h"

static void reg_hex(const char *name, uint32_t dword)
{
	printf("\t%s: %08x (%d)\n", name, dword, dword);
}

static const struct {
	const char *name;
	void (*dump)(const char *name, uint32_t dword);
} regs[0xff+1] = {
#define REG(name, fxn) [name] = { #name, (fxn) }
		REG(G2D_BASE0, reg_hex),
		REG(G2D_CFG0, reg_hex),
		REG(G2D_CFG1, reg_hex),
		REG(G2D_SCISSORX, reg_hex),
		REG(G2D_SCISSORY, reg_hex),
		REG(G2D_FOREGROUND, reg_hex),
		REG(G2D_BACKGROUND, reg_hex),
		REG(G2D_ALPHABLEND, reg_hex),
		REG(G2D_ROP, reg_hex),
		REG(G2D_CONFIG, reg_hex),
		REG(G2D_INPUT, reg_hex),
		REG(G2D_MASK, reg_hex),
		REG(G2D_BLENDERCFG, reg_hex),
		REG(G2D_CONST0, reg_hex),
		REG(G2D_CONST1, reg_hex),
		REG(G2D_CONST2, reg_hex),
		REG(G2D_CONST3, reg_hex),
		REG(G2D_CONST4, reg_hex),
		REG(G2D_CONST5, reg_hex),
		REG(G2D_CONST6, reg_hex),
		REG(G2D_CONST7, reg_hex),
		REG(G2D_GRADIENT, reg_hex),
		REG(G2D_XY, reg_hex),
		REG(G2D_WIDTHHEIGHT, reg_hex),
		REG(G2D_SXY, reg_hex),
		REG(G2D_SXY2, reg_hex),
		REG(G2D_IDLE, reg_hex),
		REG(G2D_COLOR, reg_hex),
		REG(G2D_BLEND_A0, reg_hex),
		REG(G2D_BLEND_A1, reg_hex),
		REG(G2D_BLEND_A2, reg_hex),
		REG(G2D_BLEND_A3, reg_hex),
		REG(G2D_BLEND_C0, reg_hex),
		REG(G2D_BLEND_C1, reg_hex),
		REG(G2D_BLEND_C2, reg_hex),
		REG(G2D_BLEND_C3, reg_hex),
		REG(G2D_BLEND_C4, reg_hex),
		REG(G2D_BLEND_C5, reg_hex),
		REG(G2D_BLEND_C6, reg_hex),
		REG(G2D_BLEND_C7, reg_hex),


		REG(VGV1_DIRTYBASE, reg_hex),
		REG(VGV1_CBASE1, reg_hex),
		REG(VGV1_UBASE2, reg_hex),

		REG(VGV3_NEXTADDR, reg_hex),
		REG(VGV3_NEXTCMD, reg_hex),
		REG(VGV3_WRITERAW, reg_hex),
		REG(VGV3_LAST, reg_hex),

		REG(GRADW_CONST0, reg_hex),
		REG(GRADW_CONST1, reg_hex),
		REG(GRADW_CONST2, reg_hex),
		REG(GRADW_CONST3, reg_hex),
		REG(GRADW_CONST4, reg_hex),
		REG(GRADW_CONST5, reg_hex),
		REG(GRADW_CONST6, reg_hex),
		REG(GRADW_CONST7, reg_hex),
		REG(GRADW_CONST8, reg_hex),
		REG(GRADW_CONST9, reg_hex),
		REG(GRADW_CONSTA, reg_hex),
		REG(GRADW_CONSTB, reg_hex),
		REG(GRADW_TEXCFG, reg_hex),
		REG(GRADW_TEXSIZE, reg_hex),
		REG(GRADW_TEXBASE, reg_hex),
		REG(GRADW_TEXCFG2, reg_hex),
		REG(GRADW_INST0, reg_hex),
		REG(GRADW_INST1, reg_hex),
		REG(GRADW_INST2, reg_hex),
		REG(GRADW_INST3, reg_hex),
		REG(GRADW_INST4, reg_hex),
		REG(GRADW_INST5, reg_hex),
		REG(GRADW_INST6, reg_hex),
		REG(GRADW_INST7, reg_hex),
#undef REG
};

static void dump_register(uint32_t reg, uint32_t dword)
{
	if (regs[reg].name)
		regs[reg].dump(regs[reg].name, dword);
	else
		printf("\tunknown(%02x): %08x (%d)\n", reg, dword, dword);
}

static void dump_cmdstream(uint32_t *dwords, uint32_t sizedwords)
{
	int i, j;
	for (i = 0; i < sizedwords; i++) {
		uint32_t dword = dwords[i];
		uint32_t reg = dword >> 24;
		if (reg == VGV3_WRITERAW) {
			uint32_t count = (dword >> 8) & 0xffff;
			reg = dword & 0xff;
			for (j = 0; (j < count) && (i < sizedwords); j++) {
				dump_register(reg, dwords[++i]);
				reg++;
			}
		} else {
			dump_register(reg, dword & 0x00ffffff);
		}
	}
}

static const char *param_names[] = {
		"sw",
		"sh",
		"pitch",
		"color",
		"bx",
		"by",
		"bw",
		"bh",
		"bx2",
		"by2",
		// XXX don't forget to update if more params added:
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
};

static void dump_file(int fd)
{
	enum rd_sect_type type = RD_NONE;
	void *buf = NULL;
	int sz;

	while ((read(fd, &type, sizeof(type)) > 0) && (read(fd, &sz, 4) > 0)) {
		free(buf);

		buf = malloc(sz + 1);
		((char *)buf)[sz] = '\0';
		read(fd, buf, sz);

		switch(type) {
		case RD_TEST:
			printf("test: %s\n", (char *)buf);
			break;
		case RD_CMD:
			printf("cmd: %s\n", (char *)buf);
			break;
		case RD_CMDSTREAM:
			dump_cmdstream(buf, sz/4);
			break;
		case RD_PARAM:
			printf("param: %s: %u\n", param_names[((uint32_t *)buf)[0]],
					((uint32_t *)buf)[1]);
			break;
		default:
			break;
		}
	}
}

int main(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		int fd = open(argv[i], O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "could not open: %s\n", argv[1]);
			return -1;
		}
		dump_file(fd);
	}

	return 0;
}
