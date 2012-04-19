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


/* ************************************************************************* */
/* based on kernel recovery dump code: */
#include "a2xx_reg.h"
#include "adreno_pm4types.h"

typedef enum {
	true = 1, false = 0,
} bool;

#define INVALID_RB_CMD 0xaaaaaaaa
//#define NUM_DWORDS_OF_RINGBUFFER_HISTORY 100

/* CP timestamp register */
#define	REG_CP_TIMESTAMP		 REG_SCRATCH_REG0

struct pm_id_name {
	uint32_t id;
	char name[9];
};

static const struct pm_id_name pm0_types[] = {
	{REG_PA_SC_AA_CONFIG,		"RPASCAAC"},
	{REG_RBBM_PM_OVERRIDE2,		"RRBBPMO2"},
	{REG_SCRATCH_REG2,		"RSCRTRG2"},
	{REG_SQ_GPR_MANAGEMENT,		"RSQGPRMN"},
	{REG_SQ_INST_STORE_MANAGMENT,	"RSQINSTS"},
	{REG_TC_CNTL_STATUS,		"RTCCNTLS"},
	{REG_TP0_CHICKEN,		"RTP0CHCK"},
	{REG_CP_TIMESTAMP,		"CP_TM_ST"},
};

static const struct pm_id_name pm3_types[] = {
	{CP_COND_EXEC,			"CND_EXEC"},
	{CP_CONTEXT_UPDATE,		"CX__UPDT"},
	{CP_DRAW_INDX,			"DRW_NDX_"},
	{CP_DRAW_INDX_BIN,		"DRW_NDXB"},
	{CP_EVENT_WRITE,		"EVENT_WT"},
	{CP_IM_LOAD,			"IN__LOAD"},
	{CP_IM_LOAD_IMMEDIATE,		"IM_LOADI"},
	{CP_IM_STORE,			"IM_STORE"},
	{CP_INDIRECT_BUFFER,		"IND_BUF_"},
	{CP_INDIRECT_BUFFER_PFD,	"IND_BUFP"},
	{CP_INTERRUPT,			"PM4_INTR"},
	{CP_INVALIDATE_STATE,		"INV_STAT"},
	{CP_LOAD_CONSTANT_CONTEXT,	"LD_CN_CX"},
	{CP_ME_INIT,			"ME__INIT"},
	{CP_NOP,			"PM4__NOP"},
	{CP_REG_RMW,			"REG__RMW"},
	{CP_REG_TO_MEM,		"REG2_MEM"},
	{CP_SET_BIN_BASE_OFFSET,	"ST_BIN_O"},
	{CP_SET_CONSTANT,		"ST_CONST"},
	{CP_SET_PROTECTED_MODE,	"ST_PRT_M"},
	{CP_SET_SHADER_BASES,		"ST_SHD_B"},
	{CP_WAIT_FOR_IDLE,		"WAIT4IDL"},
};

static uint32_t adreno_is_pm4_len(uint32_t word)
{
	if (word == INVALID_RB_CMD)
		return 0;

	return (word >> 16) & 0x3FFF;
}

static bool adreno_is_pm4_type(uint32_t word)
{
	int i;

	if (word == INVALID_RB_CMD)
		return 1;

	if (adreno_is_pm4_len(word) > 16)
		return 0;

	if ((word & (3<<30)) == CP_TYPE0_PKT) {
		for (i = 0; i < ARRAY_SIZE(pm0_types); ++i) {
			if ((word & 0x7FFF) == pm0_types[i].id)
				return 1;
		}
		return 0;
	}
	if ((word & (3<<30)) == CP_TYPE3_PKT) {
		for (i = 0; i < ARRAY_SIZE(pm3_types); ++i) {
			if ((word & 0xFFFF) == (pm3_types[i].id << 8))
				return 1;
		}
		return 0;
	}
	return 0;
}

static const char *adreno_pm4_name(uint32_t word)
{
	int i;

	if (word == INVALID_RB_CMD)
		return "--------";

	if ((word & (3<<30)) == CP_TYPE0_PKT) {
		for (i = 0; i < ARRAY_SIZE(pm0_types); ++i) {
			if ((word & 0x7FFF) == pm0_types[i].id)
				return pm0_types[i].name;
		}
		return "????????";
	}
	if ((word & (3<<30)) == CP_TYPE3_PKT) {
		for (i = 0; i < ARRAY_SIZE(pm3_types); ++i) {
			if ((word & 0xFFFF) == (pm3_types[i].id << 8))
				return pm3_types[i].name;
		}
		return "????????";
	}
	return "????????";
}
/* ************************************************************************* */

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

struct buffer {
	void *hostptr;
	unsigned int gpuaddr, len;
};

static struct buffer buffers[32];
static int nbuffers;

static int buffer_contains_gpuaddr(struct buffer *buf, uint32_t gpuaddr, uint32_t len)
{
	return (buf->gpuaddr <= gpuaddr) && (gpuaddr < (buf->gpuaddr + buf->len));
}

static int buffer_contains_hostptr(struct buffer *buf, void *hostptr)
{
	return (buf->hostptr <= hostptr) && (hostptr < (buf->hostptr + buf->len));
}

static uint32_t gpuaddr(void *hostptr)
{
	int i;
	for (i = 0; i < nbuffers; i++)
		if (buffer_contains_hostptr(&buffers[i], hostptr))
			return buffers[i].gpuaddr + (hostptr - buffers[i].hostptr);
	return 0;
}

#define GET_PM4_TYPE3_OPCODE(x) ((*(x) >> 8) & 0xFF)

static void dump_commands(uint32_t *dwords, uint32_t sizedwords, int level);

static void dump_type3(uint32_t *dwords, uint32_t sizedwords, int level)
{
	printf("%sopcode:%02x\n", levels[level], GET_PM4_TYPE3_OPCODE(dwords));

	switch (GET_PM4_TYPE3_OPCODE(dwords)) {
	case CP_INDIRECT_BUFFER_PFD:
	case CP_INDIRECT_BUFFER: {
		/* traverse indirect buffers */
		int i;
		uint32_t ibaddr = dwords[1];
		uint32_t ibsize = dwords[2];
		uint32_t *ptr = NULL;

		printf("%sibaddr:%02d\n", levels[level], ibaddr);
		printf("%sibsize:%02d\n", levels[level], ibsize);

		// XXX stripped out checking for loops.. but maybe we need that..
		// see kgsl_cffdump_handle_type3()

		/* map gpuaddr back to hostptr: */
		for (i = 0; i < nbuffers; i++) {
			if (buffer_contains_gpuaddr(&buffers[i], ibaddr, ibsize)) {
				ptr = buffers[i].hostptr + (ibaddr - buffers[i].gpuaddr);
				break;
			}
		}

		if (ptr) {
			dump_commands(ptr, ibsize, level);
		} else {
			fprintf(stderr, "could not find: %08x (%d)\n", ibaddr, ibsize);
		}
//		return kgsl_cffdump_parse_ibs(dev_priv, NULL,
//				ibaddr, ibsize, check_only);
		break;
	}
	}

}

static void dump_hex(uint32_t *dwords, uint32_t sizedwords, int level, const char *name)
{
	printf("%08x%s%s:", gpuaddr(dwords), levels[level], name);
	while (sizedwords--)
		printf(" %08x", *(dwords++));
	printf("\n");
}

static void dump_commands(uint32_t *dwords, uint32_t sizedwords, int level)
{
	int dwords_left = sizedwords;
	uint32_t count = 0; /* dword count including packet header */
	while (dwords_left > 0) {
		switch (*dwords >> 30) {
		case 0x0: /* type-0 */
			count = (*dwords >> 16)+2;
			dump_hex(dwords, count, level, "type-0");
			break;
		case 0x1: /* type-1 */
			count = 2;
			dump_hex(dwords, count, level, "type-1");
			break;
		case 0x3: /* type-3 */
			count = ((*dwords >> 16) & 0x3fff) + 2;
			dump_hex(dwords, count, level, "type-3");
			dump_type3(dwords, count, level+1);
			break;
		default:
			fprintf(stderr, "bad type!\n");
			return;
		}

		dwords += count;
		dwords_left -= count;

		//printf("*** dwords_left=%d, count=%d\n", dwords_left, count);
	}

	if (dwords_left < 0)
		printf("**** this ain't right!! dwords_left=%d\n", dwords_left);
}

int main(int argc, char **argv)
{
	enum rd_sect_type type = RD_NONE;
	void *buf = NULL;
	int fd, sz, i;

	if (argc != 2)
		fprintf(stderr, "usage: %s testlog.rd\n", argv[0]);

	fd = open(argv[1], O_RDONLY);
	if (fd < 0)
		fprintf(stderr, "could not open: %s\n", argv[1]);

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
		case RD_VERT_SHADER:
			printf("vertex shader:\n%s\n", (char *)buf);
			break;
		case RD_FRAG_SHADER:
			printf("fragment shader:\n%s\n", (char *)buf);
			break;
		case RD_PROGRAM:
			/* TODO */
			break;
		case RD_GPUADDR:
			buffers[nbuffers].gpuaddr = ((uint32_t *)buf)[0];
			buffers[nbuffers].len = ((uint32_t *)buf)[0];
			break;
		case RD_BUFFER_CONTENTS:
			buffers[nbuffers].hostptr = buf;
			nbuffers++;
			buf = NULL;
			break;
		case RD_CMDSTREAM:
			printf("cmdstream: %d dwords\n", sz/4);
			dump_commands(buf, sz/4, 0);
			for (i = 0; i < nbuffers; i++)
				free(buffers[i].hostptr);
			nbuffers = 0;
			break;
		}
	}

	return 0;
}

