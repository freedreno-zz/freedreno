/*
 * Copyright (c) 2013 Rob Clark <robdclark@gmail.cabelsom>
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

#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "rnn.h"
#include "rnndec.h"
#include "adreno_pm4.xml.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))


/*
 * PM4/micro-engine disassembler:
 */

typedef union PACKED {
	struct {
		uint32_t d0, d1, d2;
	};
	struct {
		uint32_t immed;
		uint32_t pad0 : 10;
		uint32_t dst  : 4;
		uint32_t pad1 : 7;
		uint32_t op   : 2;
		uint32_t pad2 : 2;
		uint32_t cond : 3;  /* 6=eq, 3=gte (or lt?) */
		uint32_t pad3 : 2;
		uint32_t src  : 2;
		uint32_t off;
	};
} pm4_instr_t;

static const pm4_instr_t known = {
		.immed = ~0,
		.dst   = ~0,
		.op    = ~0,
		.cond  = ~0,
		.src   = ~0,
		.off   = ~0,
};

static const char *cond[8] = {
		[3] = "gte", // or lt??
		[6] = "eq",
};

static const char *op[8] = {
		[1] = "mov",
		[2] = "jmp",
		[3] = "br",
//		[5] = "tbl",  /* jumptable lookup */
};

static const char *dst[32] = {
		[1] = "addr",
		[2] = "data",
};

static uint32_t label_offsets[0x512];
static int num_label_offsets;

static int label_idx(uint32_t offset, int create)
{
	int i;
	for (i = 0; i < num_label_offsets; i++)
		if (offset == label_offsets[i])
			return i;
	if (!create)
		return -1;
	label_offsets[i] = offset;
	num_label_offsets = i+1;
	return i;
}

static struct {
	uint32_t offset;
	uint32_t num_jump_labels;
	uint32_t jump_labels[256];
} jump_labels[1024];
int num_jump_labels;

/*
type3 pkt's that we know are sent by user/kernel:

[robclark@thunkpad:~/src/freedreno/dumps/a220]$ for f in *.rd; do cffdump --summary $f | grep opcode | awk '{print $2}'; done | sort | uniq
CP_DRAW_INDX
CP_EVENT_WRITE
CP_IM_LOAD_IMMEDIATE
CP_INDIRECT_BUFFER
CP_INVALIDATE_STATE
CP_MEM_WRITE
CP_NOP
CP_SET_CONSTANT
CP_SET_DRAW_INIT_FLAGS
CP_SET_SHADER_BASES
CP_WAIT_FOR_IDLE
CP_WAIT_REG_EQ
----
CP_REG_TO_MEM
CP_CONTEXT_UPDATE  (#ifdef CONFIG_MSM_KGSL_DISABLE_SHADOW_WRITES)
CP_LOAD_CONSTANT_CONTEXT
CP_IM_LOAD
CP_REG_RMW
CP_IM_STORE
CP_ME_INIT
CP_INTERRUPT


[robclark@thunkpad:~/src/freedreno/dumps/a320]$ for f in *.rd; do cffdump --summary $f | grep opcode | awk '{print $2}'; done | sort | uniq
CP_DRAW_INDX
CP_EVENT_WRITE
CP_INDIRECT_BUFFER
CP_INVALIDATE_STATE
CP_LOAD_STATE
CP_MEM_WRITE
CP_NOP
CP_REG_RMW
CP_SET_BIN
CP_SET_BIN_DATA
CP_WAIT_FOR_IDLE
----
CP_REG_TO_MEM
CP_LOAD_CONSTANT_CONTEXT
CP_COND_EXEC
CP_SET_CONSTANT
CP_DRAW_INDX_2
CP_CONTEXT_UPDATE
CP_ME_INIT
CP_INTERRUPT

-------------

Based on dumping ME state and MEQ before/after, this is my theory
about which packets are handled in ME and/or PFP:

pm4                        ME    PFP
PKT3:CP_ME_INIT            y     ? (truncated)
PKT0                       y     n?
PKT3:CP_SET_CONSTANT       y     y
PKT3:CP_INTERRUPT          y     n?
PKT3:CP_EVENT_WRITE        y     n?
PKT3:CP_WAIT_FOR_IDLE      y     n?
PKT3:CP_LOAD_STATE         y     n?
PKT3:CP_SET_BIN_MASK       n     y
PKT3:CP_SET_BIN_SELECT     n     y
PKT3:CP_INVALIDATE_STATE   n     y
PKT3:CP_NOP                n     y
PKT3:CP_REG_RMW            y     n?
PKT3:CP_SET_BIN            y     n?
PKT3:CP_SET_BIN_DATA       y     n?
PKT3:CP_MEM_WRITE          y     n?
PKT3:CP_DRAW_INDX          y     y

n? -> packet is written as-is to MEQ so not sure if PFP does
      anything with it or just blind passthru

 */
static int gpuver;

static struct rnndeccontext *ctx;
static struct rnndb *db;
struct rnndomain *dom[2];

static void printreg(uint32_t regbase)
{
	struct rnndomain *d = NULL;

	if (rnndec_checkaddr(ctx, dom[0], regbase, 0))
		d = dom[0];
	else if (rnndec_checkaddr(ctx, dom[1], regbase, 0))
		d = dom[1];

	if (d) {
		struct rnndecaddrinfo *info = rnndec_decodeaddr(ctx, d, regbase, 0);
		if (info) {
			printf("%s", info->name);
			free(info->name);
			free(info);
			return;
		}
	}

	// XXX hack:
#define MASK 0x7fff
	else if ((regbase & MASK) > 2) {
		if (rnndec_checkaddr(ctx, dom[0], regbase & MASK, 0))
			d = dom[0];
		else if (rnndec_checkaddr(ctx, dom[1], regbase & MASK, 0))
			d = dom[1];

		if (d) {
			struct rnndecaddrinfo *info = rnndec_decodeaddr(ctx, d, regbase & MASK, 0);
			if (info) {
				printf("(%s | 0x%x)", info->name, regbase & ~MASK);
				free(info->name);
				free(info);
				return;
			}
		}
	}

	printf("<%08x>", regbase);
}

static char *getpm4(uint32_t id)
{
	return rnndec_decode_enum(ctx, "adreno_pm4_type3_packets", id);
}

/* seems to start in same place on all fw files I have.. but probably need
 * to find a better way to figure this out:
 */
#define JUMP_TABLE_START (ninstr - 28)

#define off2base(off) ((((off) - JUMP_TABLE_START) * 4) + CP_NOP)

static void add_jump_table_entry(uint32_t n, uint32_t offset)
{
	int i;

	offset &= 0x7ff;

	if (n > 128) /* can't possibly be a PM4 PKT3.. */
		return;

	for (i = 0; i < num_jump_labels; i++)
		if (jump_labels[i].offset == offset)
			goto add_label;

	num_jump_labels = i + 1;
	jump_labels[i].offset = offset;
	jump_labels[i].num_jump_labels = 0;

add_label:
	jump_labels[i].jump_labels[jump_labels[i].num_jump_labels++] = n;
	assert(jump_labels[i].num_jump_labels < 256);
//printf("add %d -> %04x (%d)\n", n, offset, i);
}

static int get_jump_table_entry(uint32_t offset)
{
	int i;

	offset &= 0x7ff;

	for (i = 0; i < num_jump_labels; i++)
		if (jump_labels[i].offset == offset)
			return i;

	return -1;
}

static void disasm_pm4(uint32_t *buf, int sizedwords)
{
	int i, j, ninstr = sizedwords / 3;
	pm4_instr_t *instrs = (pm4_instr_t *)buf;
	uint32_t d1_mask;
	uint16_t d1_count[32];

	num_label_offsets = 0;
	num_jump_labels = 0;

	/* figure out the branch targets: */
	for (i = 0; i < ninstr; i++) {
		pm4_instr_t *instr = &instrs[i];

		if (i >= JUMP_TABLE_START) {
			uint16_t *jmptbl = (uint16_t *)&buf[i * 3];
			int n = off2base(i);
			for (j = 0; j < 4; j++) {
				/* all the firmwares I've seen so far use jump to
				 * 0x0002 to raise an error irq:
				 */
				if (jmptbl[j] != 0x0002) {
					add_jump_table_entry(n + j, jmptbl[j]);
				}
			}
		} else if (instr->d1 & 0x00400000) {
			label_idx(instr->d2, 1);
		}
	}

	d1_mask = 0;
	memset(d1_count, 0, sizeof(d1_count));

	for (i = 0; i < JUMP_TABLE_START; i++) {
		pm4_instr_t *instr = &instrs[i];
		int idx = label_idx(i, 0);
		int jump_label_idx = get_jump_table_entry(i);

		d1_mask |= instr->d1;
		for (j = 0; j < 32; j++)
			if (instr->d1 & (1 << j))
				d1_count[j]++;

		if (i == 0x0002)
			printf("\nINVALID_JUMPTABLE_ENTRY:\n");
		if (jump_label_idx >= 0) {
			int j;
			printf("\n");
			for (j = 0; j < jump_labels[jump_label_idx].num_jump_labels; j++) {
				uint32_t jump_label = jump_labels[jump_label_idx].jump_labels[j];
				char *name = getpm4(jump_label);
				if (name) {
					printf("%s:\n", name);
				} else {
					printf("UNKN%d:\n", jump_label);
				}
			}
		}

		printf("%04x:", i);
		if (idx >= 0) {
			printf(" l%02d:", idx);
		} else {
			printf("     ");
		}
		printf("\t%08x %08x %08x", instr->d0, instr->d1, instr->d2);

		if (!instr->d0 && !instr->d1 && !instr->d2) {
			printf("\tnop\n");
			continue;
		}

{
int n;
printf("\t");
for (n = 31; n >= 0; n--) {
	uint32_t b = (1 << n);
	if (b & known.d1)
		printf("%c", (b & instr->d1) ? '+' : '-');
	else
		printf("%c", (b & instr->d1) ? '1' : '0');
}
}
		printf("\t");
		if (op[instr->op])
			printf("%s", op[instr->op]);
		else
			printf("op%d", instr->op);

		if (instr->cond) {
			printf(".");
			if (cond[instr->cond])
				printf("%s", cond[instr->cond]);
			else
				printf("c%d", instr->cond);
		}

		if (dst[instr->dst])
			printf(" $%s", dst[instr->dst]);
		else
			printf(" $%d", instr->dst);

		switch (instr->src) {
		case 3:
			printf(", $meq");
			break;
		case 2:
			printf(", $s2?");
			break;
		case 1:
			printf(", $s1?");
			break;
		case 0:
			printf(", #");
			if (instr->immed > 2)
				printreg(instr->immed);
			else
				printf("<%08x>", instr->immed);
			break;
		}

		if (instr->d1 & 0x00400000) {
			printf("\t\t;");
			printf("(branch:#l%02d)", label_idx(instr->d2, 0));
		}
		printf("\n");
	}

	printf(";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n");
	printf("; d1_mask: %08x\n", d1_mask);
	printf("; d1_count:");
	for (j = 31; j >= 0; j--) {
		if ((j % 8) == 7)
			printf("\n;  %02d: ", j);
		printf(" %03u", (uint32_t)d1_count[j]);
	}
	printf("\n");

	printf(";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n");
	printf("; JUMP TABLE\n");
	for (i = JUMP_TABLE_START; i < ninstr; i++) {
		uint16_t *jmptbl = (uint16_t *)&buf[i * 3];
		int n = off2base(i);
		for (j = 0; j < 4; j++, n++) {
			uint16_t offset = jmptbl[j];
			char *name = getpm4(n);
			printf("%3d %02x: ", n, n);
			if (offset == 0x0002) {
				printf("....");
			} else {
				printf("%04x", offset);
				if (name) {
					printf("   ; %s", name);
				} else {
					printf("   ; UNKN%d", n);
				}
			}
			printf("\n");
		}
	}
}

/*
 * prefetch-parser disassembler:
 */

/* 64 for a4xx, or 32 otherwise: */
static int num_pfp_table_entries;

static void disasm_pfp(uint32_t *buf, int sizedwords)
{
	struct {
		uint16_t offset;
		uint16_t label;
	} *table_entries = (void *)&buf[sizedwords - num_pfp_table_entries];
	int i, j;

	assert(sizedwords >= num_pfp_table_entries);

	for (i = 0; i < sizedwords - num_pfp_table_entries; i++) {
		int haslabel = 0;
		for (j = 0; j < num_pfp_table_entries; j++) {
			if ((table_entries[j].offset == i) && table_entries[j].label) {
				int n = table_entries[j].label;
				char *name = getpm4(n);
				if (!haslabel) {
					printf("\n");
					haslabel = 1;
				}
				if (name) {
					printf("%s:\n", name);
				} else {
					printf("UNKN%d:\n", n);
				}
			}
		}
		printf("%04x: %08x\n", i, buf[i]);
	}

	printf(";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n");
	printf("; JUMP TABLE\n");
	for (i = 0; i < num_pfp_table_entries; i++) {
		int n = table_entries[i].label;
		printf("%04x: %04x %04x", 0x100 + i, n, table_entries[i].offset);
		if (n) {
			char *name = getpm4(n);
			if (name) {
				printf("  ; %s", name);
			} else {
				printf("  ; UNKN%d", n);
			}
		}
		printf("\n");
	}
}

#define CHUNKSIZE 4096

static char * readfile(const char *path, int *sz)
{
	char *buf = NULL;
	int fd, ret, n = 0;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return NULL;

	while (1) {
		buf = realloc(buf, n + CHUNKSIZE);
		ret = read(fd, buf + n, CHUNKSIZE);
		if (ret < 0) {
			free(buf);
			*sz = 0;
			return NULL;
		} else if (ret < CHUNKSIZE) {
			n += ret;
			*sz = n;
			return buf;
		} else {
			n += CHUNKSIZE;
		}
	}
}

static int writefile(const char *path, void *ptr, int sz)
{
	int ret, fd;
	char *buf = ptr;

	fd = open(path, O_RDWR);
	if (fd < 0)
		return fd;

	while (sz) {
		ret = write(fd, buf, sz);
		if (ret < 0)
			return ret;
		buf += ret;
		sz  -= ret;
	}

	return 0;
}

/* hack.. so I can stop things in gdb and edit.. */
static int editloop = 1;
static int edit_pm4(uint32_t *buf, int sizedwords)
{
	pm4_instr_t *instrs = (pm4_instr_t *)buf;

	/* change editloop to >1 to return and write result, or
	 * 0 to exit without saving
	 */
	while (editloop == 1) {
		/* weeee!!! */
	}

	if (editloop == 42)
		printf("instrs=%p\n", instrs);

	return editloop;
}

int main(int argc, char **argv)
{
	uint32_t *buf;
	char *file, *name;
	int sz, edit = 0;

	if (!strcmp(argv[1], "-e")) {
		edit = 1;
		argv++;
		argc--;
	}

	if (argc != 2) {
		printf("usage: fwdump <pm4.fw>\n");
		return -1;
	}

	file = argv[1];

	if (strstr(file, "a4")) {
		printf("; matching a4xx\n");
		gpuver = 400;
		name = "A4XX";
		num_pfp_table_entries = 64;
	} else if (strstr(file, "a3")) {
		printf("; matching a3xx\n");
		gpuver = 300;
		name = "A3XX";
		num_pfp_table_entries = 32;
	} else {
		printf("; matching a2xx\n");
		gpuver = 200;
		name = "A2XX";
		num_pfp_table_entries = 32;
	}

	rnn_init();
	db = rnn_newdb();

	ctx = rnndec_newcontext(db);
	ctx->colors = &envy_null_colors;

	if (strstr(file, "radeon")) {
		rnn_parsefile(db, "radeon.xml");
		dom[0] = rnn_finddomain(db, "R600");
		dom[1] = dom[0];
	} else {
		rnn_parsefile(db, "adreno.xml");
		dom[0] = rnn_finddomain(db, name);
		dom[1] = rnn_finddomain(db, "AXXX");
	}

	buf = (uint32_t *)readfile(file, &sz);

	/* at least R600 ME fw appears to be same.. */
	if (strstr(file, "radeon")) {
		uint32_t *dwords = buf;
		int i, sizedwords = sz/4;
		for (i = 0; i < sizedwords; i++) {
			uint32_t dw = dwords[i];
			dw = ((dw & 0xff000000) >> 24) |
					((dw & 0x00ff0000) >> 8) |
					((dw & 0x0000ff00) << 8) |
					((dw & 0x000000ff) << 24);
			dwords[i] = dw;
		}
	}

	if (strstr(file, "_pm4") || strstr(file, "_me")) {
		if (edit) {
			printf("Editing PM4 %s\n", file);
			if (edit_pm4(&buf[1], sz/4 - 1))
				writefile(file, buf, sz);
		} else {
			printf("; Disassembling PM4 %s:\n", file);
			printf("; Version: %08x\n\n", buf[0]);
			disasm_pm4(&buf[1], sz/4 - 1);
		}
	} else if (strstr(file, "_pfp")) {
		printf("; Disassembling PFP %s:\n", file);
		printf("; Version: %08x\n\n", buf[0]);
		disasm_pfp(&buf[1], sz/4 - 1);
	}

	return 0;
}
