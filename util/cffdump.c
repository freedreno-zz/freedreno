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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <errno.h>

#include "redump.h"
#include "disasm.h"
#include "script.h"
#include "io.h"
#include "rnnutil.h"

/* ************************************************************************* */
/* originally based on kernel recovery dump code: */
#include "adreno_common.xml.h"
#include "adreno_pm4.xml.h"
#include "a2xx.xml.h"
#include "a3xx.xml.h"
#include "a4xx.xml.h"
#include "a5xx.xml.h"

typedef enum {
	true = 1, false = 0,
} bool;

static bool needs_wfi = false;
static bool dump_shaders = false;
static bool no_color = false;
static bool summary = false;
static bool allregs = false;
static bool dump_textures = false;
static int vertices;
static unsigned gpu_id = 220;

static inline unsigned regcnt(void)
{
	if (gpu_id >= 500)
		return 0xffff;
	else
		return 0x7fff;
}

/* note: not sure if CP_SET_DRAW_STATE counts as a complete extra level
 * of IB or if it is restricted to just have register writes:
 */
static int draws[3];
static int ib;

static int draw_filter;
static int draw_count;

/* query mode.. to handle symbolic register name queries, we need to
 * defer parsing query string until after gpu_id is know and rnn db
 * loaded:
 */
static char **querystrs;
static int *queryvals;
int nquery;

static char *script;

static bool quiet(int lvl)
{
	if ((draw_filter != -1) && (draw_filter != draw_count))
		return true;
	if ((lvl >= 3) && (summary || querystrs || script))
		return true;
	if ((lvl >= 2) && (querystrs || script))
		return true;
	return false;
}

static void printl(int lvl, const char *fmt, ...)
{
	va_list args;
	if (quiet(lvl))
		return;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

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

#define NAME(x)	[x] = #x

static const char *fmt_name[] = {
		NAME(FMT_1_REVERSE),
		NAME(FMT_1),
		NAME(FMT_8),
		NAME(FMT_1_5_5_5),
		NAME(FMT_5_6_5),
		NAME(FMT_6_5_5),
		NAME(FMT_8_8_8_8),
		NAME(FMT_2_10_10_10),
		NAME(FMT_8_A),
		NAME(FMT_8_B),
		NAME(FMT_8_8),
		NAME(FMT_Cr_Y1_Cb_Y0),
		NAME(FMT_Y1_Cr_Y0_Cb),
		NAME(FMT_5_5_5_1),
		NAME(FMT_8_8_8_8_A),
		NAME(FMT_4_4_4_4),
		NAME(FMT_10_11_11),
		NAME(FMT_11_11_10),
		NAME(FMT_DXT1),
		NAME(FMT_DXT2_3),
		NAME(FMT_DXT4_5),
		NAME(FMT_24_8),
		NAME(FMT_24_8_FLOAT),
		NAME(FMT_16),
		NAME(FMT_16_16),
		NAME(FMT_16_16_16_16),
		NAME(FMT_16_EXPAND),
		NAME(FMT_16_16_EXPAND),
		NAME(FMT_16_16_16_16_EXPAND),
		NAME(FMT_16_FLOAT),
		NAME(FMT_16_16_FLOAT),
		NAME(FMT_16_16_16_16_FLOAT),
		NAME(FMT_32),
		NAME(FMT_32_32),
		NAME(FMT_32_32_32_32),
		NAME(FMT_32_FLOAT),
		NAME(FMT_32_32_FLOAT),
		NAME(FMT_32_32_32_32_FLOAT),
		NAME(FMT_32_AS_8),
		NAME(FMT_32_AS_8_8),
		NAME(FMT_16_MPEG),
		NAME(FMT_16_16_MPEG),
		NAME(FMT_8_INTERLACED),
		NAME(FMT_32_AS_8_INTERLACED),
		NAME(FMT_32_AS_8_8_INTERLACED),
		NAME(FMT_16_INTERLACED),
		NAME(FMT_16_MPEG_INTERLACED),
		NAME(FMT_16_16_MPEG_INTERLACED),
		NAME(FMT_DXN),
		NAME(FMT_8_8_8_8_AS_16_16_16_16),
		NAME(FMT_DXT1_AS_16_16_16_16),
		NAME(FMT_DXT2_3_AS_16_16_16_16),
		NAME(FMT_DXT4_5_AS_16_16_16_16),
		NAME(FMT_2_10_10_10_AS_16_16_16_16),
		NAME(FMT_10_11_11_AS_16_16_16_16),
		NAME(FMT_11_11_10_AS_16_16_16_16),
		NAME(FMT_32_32_32_FLOAT),
		NAME(FMT_DXT3A),
		NAME(FMT_DXT5A),
		NAME(FMT_CTX1),
		NAME(FMT_DXT3A_AS_1_1_1_1),
};

static void dump_commands(uint32_t *dwords, uint32_t sizedwords, int level);
static void dump_register_val(uint32_t regbase, uint32_t dword, int level);
static const char *regname(uint32_t regbase, int color);

struct buffer {
	void *hostptr;
	unsigned int gpuaddr, len;
};

static struct buffer buffers[512];
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

static uint32_t gpubaseaddr(uint32_t gpuaddr)
{
	int i;
	if (!gpuaddr)
		return 0;
	for (i = 0; i < nbuffers; i++)
		if (buffer_contains_gpuaddr(&buffers[i], gpuaddr, 0))
			return buffers[i].gpuaddr;
	return 0;
}

static void *hostptr(uint32_t gpuaddr)
{
	int i;
	if (!gpuaddr)
		return 0;
	for (i = 0; i < nbuffers; i++)
		if (buffer_contains_gpuaddr(&buffers[i], gpuaddr, 0))
			return buffers[i].hostptr + (gpuaddr - buffers[i].gpuaddr);
	return 0;
}

static unsigned hostlen(uint32_t gpuaddr)
{
	int i;
	if (!gpuaddr)
		return 0;
	for (i = 0; i < nbuffers; i++)
		if (buffer_contains_gpuaddr(&buffers[i], gpuaddr, 0))
			return buffers[i].len + buffers[i].gpuaddr - gpuaddr;
	return 0;
}

static void dump_hex(uint32_t *dwords, uint32_t sizedwords, int level)
{
	int i;
	for (i = 0; i < sizedwords; i++) {
		if ((i % 8) == 0)
			printf("%08x:%s", gpuaddr(dwords), levels[level]);
		else
			printf(" ");
		printf("%08x", *(dwords++));
		if ((i % 8) == 7)
			printf("\n");
	}
	if (i % 8)
		printf("\n");
}

static void dump_float(float *dwords, uint32_t sizedwords, int level)
{
	int i;
	for (i = 0; i < sizedwords; i++) {
		if ((i % 8) == 0)
			printf("%08x:%s", gpuaddr(dwords), levels[level]);
		else
			printf(" ");
		printf("%8f", *(dwords++));
		if ((i % 8) == 7)
			printf("\n");
	}
	if (i % 8)
		printf("\n");
}

/* I believe the surface format is low bits:
#define RB_COLOR_INFO__COLOR_FORMAT_MASK                   0x0000000fL
comments in sys2gmem_tex_const indicate that address is [31:12], but
looks like at least some of the bits above the format have different meaning..
*/
static void parse_dword_addr(uint32_t dword, uint32_t *gpuaddr,
		uint32_t *flags, uint32_t mask)
{
	*gpuaddr = dword & ~mask;
	*flags   = dword & mask;
}


#define INVALID_RB_CMD 0xaaaaaaaa

/* CP timestamp register */
#define	REG_CP_TIMESTAMP		 REG_SCRATCH_REG0


static uint32_t type0_reg_vals[0xffff + 1];
static uint8_t type0_reg_written[sizeof(type0_reg_vals)/8];
static uint32_t lastvals[ARRAY_SIZE(type0_reg_vals)];

bool reg_written(uint32_t regbase)
{
	return !!(type0_reg_written[regbase/8] & (1 << (regbase % 8)));
}

static void clear_written(void)
{
	memset(type0_reg_written, 0, sizeof(type0_reg_written));
}

uint32_t reg_lastval(uint32_t regbase)
{
	return lastvals[regbase];
}

static void clear_lastvals(void)
{
	memset(lastvals, 0, sizeof(lastvals));
}

uint32_t reg_val(uint32_t regbase)
{
	return type0_reg_vals[regbase];
}

static struct {
	uint32_t config;
	uint32_t address;
	uint32_t length;
} vsc_pipe_data[7];

static void reg_vsc_pipe_config(const char *name, uint32_t dword, int level)
{
	int idx;
	sscanf(name, "VSC_PIPE_CONFIG_%x", &idx) ||
		sscanf(name, "VSC_PIPE[0x%x].CONFIG", &idx) ||
		sscanf(name, "VSC_PIPE[%d].CONFIG", &idx);
	vsc_pipe_data[idx].config = dword;
}

static void reg_vsc_pipe_data_address(const char *name, uint32_t dword, int level)
{
	int idx;
	sscanf(name, "VSC_PIPE_DATA_ADDRESS_%x", &idx) ||
		sscanf(name, "VSC_PIPE[0x%x].DATA_ADDRESS", &idx) ||
		sscanf(name, "VSC_PIPE[%d].DATA_ADDRESS", &idx);
	vsc_pipe_data[idx].address = dword;
}

static void reg_vsc_pipe_data_length(const char *name, uint32_t dword, int level)
{
	int idx;
	void *buf;

	sscanf(name, "VSC_PIPE_DATA_LENGTH_%x", &idx) ||
		sscanf(name, "VSC_PIPE[0x%x].DATA_LENGTH", &idx) ||
		sscanf(name, "VSC_PIPE[%d].DATA_LENGTH", &idx);

	vsc_pipe_data[idx].length = dword;

	if (quiet(3))
		return;

	/* as this is the last register in the triplet written, we dump
	 * the pipe data here..
	 */
	buf = hostptr(vsc_pipe_data[idx].address);
	if (buf) {
		/* not sure how much of this is useful: */
		dump_hex(buf, min(vsc_pipe_data[idx].length/4, 16), level+1);
	}
}

/*
 * A3xx registers:
 */

typedef struct {
	uint32_t fetchsize  : 7;
	uint32_t bufstride  : 10;
	/* warning: after here differs for a4xx */
#if 1
	uint32_t pad : 15;
#else
	uint32_t switchnext : 1;
	uint32_t indexcode  : 6;
	uint32_t steprate   : 8;
#endif
} vfd_fetch_state_t;
static vfd_fetch_state_t vfd_fetch_state[0xf];

static void reg_vfd_fetch_instr_0_x(const char *name, uint32_t dword, int level)
{
	int idx;

	/* this is a bit ugly way, but oh well.. */
	sscanf(name, "VFD_FETCH_INSTR_0_%x", &idx) ||
		sscanf(name, "VFD_FETCH[0x%x].INSTR_0", &idx) ||
		sscanf(name, "VFD_FETCH[%d].INSTR_0", &idx);

	vfd_fetch_state[idx] = *(vfd_fetch_state_t *)&dword;
}

static void reg_vfd_fetch_instr_1_x(const char *name, uint32_t dword, int level)
{
	int idx;
	void *buf;

	/* this is a bit ugly way, but oh well.. */
	sscanf(name, "VFD_FETCH_INSTR_1_%x", &idx) ||
		sscanf(name, "VFD_FETCH[0x%x].INSTR_1", &idx) ||
		sscanf(name, "VFD_FETCH[%d].INSTR_1", &idx);

	if (quiet(3))
		return;

	buf = hostptr(dword);

	if (buf) {
		// XXX we probably need to know min/max vtx to know the
		// right values to dump..
		uint32_t sizedwords = vfd_fetch_state[idx].fetchsize + 1;
		dump_float(buf, sizedwords, level+1);
		dump_hex(buf, sizedwords, level+1);
	}
}

static void reg_dump_scratch(const char *name, uint32_t dword, int level)
{
	unsigned regbase;

	if (quiet(3))
		return;

	printf("%s:", levels[level]);
	for (regbase = REG_AXXX_CP_SCRATCH_REG0;
			regbase <= REG_AXXX_CP_SCRATCH_REG7;
			regbase++) {
		printf(" %08x", reg_val(regbase));
	}
	printf("\n");
}

static void reg_dump_gpuaddr(const char *name, uint32_t dword, int level)
{
	void *buf;

	if (quiet(3))
		return;

	buf = hostptr(dword);
	if (buf) {
		uint32_t sizedwords = 64;
		dump_hex(buf, sizedwords, level+1);
	}
}

static void dump_shader(const char *ext, char *buf, int bufsz)
{
	if (dump_shaders) {
		static int n = 0;
		char filename[8];
		int fd;
		sprintf(filename, "%04d.%s", n++, ext);
		fd = open(filename, O_WRONLY| O_TRUNC | O_CREAT, 0644);
		write(fd, buf, bufsz);
		close(fd);
	}
}

static void reg_disasm_gpuaddr(const char *name, uint32_t dword, int level)
{
	void *buf;

	dword &= 0xfffffff0;

	if (quiet(3))
		return;

	buf = hostptr(dword);
	if (buf) {
		uint32_t sizedwords = hostlen(dword) / 4;
		const char *ext;

		dump_hex(buf, 64, level+1);
		disasm_a3xx(buf, sizedwords, level+2, SHADER_FRAGMENT);

		/* this is a bit ugly way, but oh well.. */
		if (strstr(name, "SP_VS_OBJ")) {
			ext = "vo3";
		} else if (strstr(name, "SP_FS_OBJ")) {
			ext = "fo3";
		} else if (strstr(name, "SP_GS_OBJ")) {
			ext = "go3";
		} else {
			ext = NULL;
		}

		if (ext)
			dump_shader(ext, buf, sizedwords * 4);
	}
}

// HACK:
#define REG_A2XX_VSC_PIPE_CONFIG(i0)        (0x00000c06 + 0x3*(i0))
#define REG_A2XX_VSC_PIPE_DATA_ADDRESS(i0)  (0x00000c07 + 0x3*(i0))
#define REG_A2XX_VSC_PIPE_DATA_LENGTH(i0)   (0x00000c08 + 0x3*(i0))
#define REG_A3XX_VSC_PIPE_CONFIG(i0)        (0x00000c06 + 0x3*(i0))
#define REG_A3XX_VSC_PIPE_DATA_ADDRESS(i0)  (0x00000c07 + 0x3*(i0))
#define REG_A3XX_VSC_PIPE_DATA_LENGTH(i0)   (0x00000c08 + 0x3*(i0))
#define REG_A3XX_VFD_FETCH_INSTR_0(i0)      (0x00002246 + 0x2*(i0))
#define REG_A3XX_VFD_FETCH_INSTR_1(i0)      (0x00002247 + 0x2*(i0))

#define REG_A4XX_VFD_FETCH_INSTR_0(i0)      (0x0000220a + 0x4*(i0))
#define REG_A4XX_VFD_FETCH_INSTR_1(i0)      (0x0000220b + 0x4*(i0))


/*
 * Registers with special handling (rnndec_decode() handles rest):
 */
static const const struct {
	void (*fxn)(const char *name, uint32_t dword, int level);
} reg_axxx[0x7fff + 1] = {
#define REG(x, fxn) [REG_AXXX_ ## x] = { fxn }
		REG(CP_SCRATCH_REG0, reg_dump_scratch),
		REG(CP_SCRATCH_REG1, reg_dump_scratch),
		REG(CP_SCRATCH_REG2, reg_dump_scratch),
		REG(CP_SCRATCH_REG3, reg_dump_scratch),
		REG(CP_SCRATCH_REG4, reg_dump_scratch),
		REG(CP_SCRATCH_REG5, reg_dump_scratch),
		REG(CP_SCRATCH_REG6, reg_dump_scratch),
		REG(CP_SCRATCH_REG7, reg_dump_scratch),
#undef REG
}, reg_a2xx[0x7fff + 1] = {
#define REG(x, fxn) [REG_A2XX_ ## x] = { fxn }
		REG(VSC_PIPE_CONFIG(0), reg_vsc_pipe_config),
		REG(VSC_PIPE_DATA_ADDRESS(0), reg_vsc_pipe_data_address),
		REG(VSC_PIPE_DATA_LENGTH(0), reg_vsc_pipe_data_length),
		REG(VSC_PIPE_CONFIG(1), reg_vsc_pipe_config),
		REG(VSC_PIPE_DATA_ADDRESS(1), reg_vsc_pipe_data_address),
		REG(VSC_PIPE_DATA_LENGTH(1), reg_vsc_pipe_data_length),
		REG(VSC_PIPE_CONFIG(2), reg_vsc_pipe_config),
		REG(VSC_PIPE_DATA_ADDRESS(2), reg_vsc_pipe_data_address),
		REG(VSC_PIPE_DATA_LENGTH(2), reg_vsc_pipe_data_length),
		REG(VSC_PIPE_CONFIG(3), reg_vsc_pipe_config),
		REG(VSC_PIPE_DATA_ADDRESS(3), reg_vsc_pipe_data_address),
		REG(VSC_PIPE_DATA_LENGTH(3), reg_vsc_pipe_data_length),
		REG(VSC_PIPE_CONFIG(4), reg_vsc_pipe_config),
		REG(VSC_PIPE_DATA_ADDRESS(4), reg_vsc_pipe_data_address),
		REG(VSC_PIPE_DATA_LENGTH(4), reg_vsc_pipe_data_length),
		REG(VSC_PIPE_CONFIG(5), reg_vsc_pipe_config),
		REG(VSC_PIPE_DATA_ADDRESS(5), reg_vsc_pipe_data_address),
		REG(VSC_PIPE_DATA_LENGTH(5), reg_vsc_pipe_data_length),
		REG(VSC_PIPE_CONFIG(6), reg_vsc_pipe_config),
		REG(VSC_PIPE_DATA_ADDRESS(6), reg_vsc_pipe_data_address),
		REG(VSC_PIPE_DATA_LENGTH(6), reg_vsc_pipe_data_length),
		REG(VSC_PIPE_CONFIG(7), reg_vsc_pipe_config),
		REG(VSC_PIPE_DATA_ADDRESS(7), reg_vsc_pipe_data_address),
		REG(VSC_PIPE_DATA_LENGTH(7), reg_vsc_pipe_data_length),
#undef REG
}, reg_a3xx[0x7fff + 1] = {
#define REG(x, fxn) [REG_A3XX_ ## x] = { fxn }
		REG(VSC_SIZE_ADDRESS, reg_dump_gpuaddr),
		REG(VSC_PIPE_CONFIG(0), reg_vsc_pipe_config),
		REG(VSC_PIPE_DATA_ADDRESS(0), reg_vsc_pipe_data_address),
		REG(VSC_PIPE_DATA_LENGTH(0), reg_vsc_pipe_data_length),
		REG(VSC_PIPE_CONFIG(1), reg_vsc_pipe_config),
		REG(VSC_PIPE_DATA_ADDRESS(1), reg_vsc_pipe_data_address),
		REG(VSC_PIPE_DATA_LENGTH(1), reg_vsc_pipe_data_length),
		REG(VSC_PIPE_CONFIG(2), reg_vsc_pipe_config),
		REG(VSC_PIPE_DATA_ADDRESS(2), reg_vsc_pipe_data_address),
		REG(VSC_PIPE_DATA_LENGTH(2), reg_vsc_pipe_data_length),
		REG(VSC_PIPE_CONFIG(3), reg_vsc_pipe_config),
		REG(VSC_PIPE_DATA_ADDRESS(3), reg_vsc_pipe_data_address),
		REG(VSC_PIPE_DATA_LENGTH(3), reg_vsc_pipe_data_length),
		REG(VSC_PIPE_CONFIG(4), reg_vsc_pipe_config),
		REG(VSC_PIPE_DATA_ADDRESS(4), reg_vsc_pipe_data_address),
		REG(VSC_PIPE_DATA_LENGTH(4), reg_vsc_pipe_data_length),
		REG(VSC_PIPE_CONFIG(5), reg_vsc_pipe_config),
		REG(VSC_PIPE_DATA_ADDRESS(5), reg_vsc_pipe_data_address),
		REG(VSC_PIPE_DATA_LENGTH(5), reg_vsc_pipe_data_length),
		REG(VSC_PIPE_CONFIG(6), reg_vsc_pipe_config),
		REG(VSC_PIPE_DATA_ADDRESS(6), reg_vsc_pipe_data_address),
		REG(VSC_PIPE_DATA_LENGTH(6), reg_vsc_pipe_data_length),
		REG(VSC_PIPE_CONFIG(7), reg_vsc_pipe_config),
		REG(VSC_PIPE_DATA_ADDRESS(7), reg_vsc_pipe_data_address),
		REG(VSC_PIPE_DATA_LENGTH(7), reg_vsc_pipe_data_length),
		REG(VFD_FETCH_INSTR_0(0), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(0), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(1), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(1), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(2), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(2), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(3), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(3), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(4), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(4), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(5), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(5), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(6), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(6), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(7), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(7), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(8), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(8), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(9), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(9), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(10), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(10), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(11), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(11), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(12), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(12), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(13), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(13), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(14), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(14), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(15), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(15), reg_vfd_fetch_instr_1_x),
		REG(SP_VS_PVT_MEM_ADDR_REG, reg_dump_gpuaddr),
		REG(SP_FS_PVT_MEM_ADDR_REG, reg_dump_gpuaddr),
		REG(SP_VS_OBJ_START_REG, reg_disasm_gpuaddr),
		REG(SP_FS_OBJ_START_REG, reg_disasm_gpuaddr),
		REG(TPL1_TP_FS_BORDER_COLOR_BASE_ADDR, reg_dump_gpuaddr),
#undef REG
}, reg_a4xx[0x7fff + 1] = {
#define REG(x, fxn) [REG_A4XX_ ## x] = { fxn }
		REG(SP_VS_PVT_MEM_ADDR, reg_dump_gpuaddr),
		REG(SP_FS_PVT_MEM_ADDR, reg_dump_gpuaddr),
		REG(SP_GS_PVT_MEM_ADDR, reg_dump_gpuaddr),
		REG(SP_HS_PVT_MEM_ADDR, reg_dump_gpuaddr),
		REG(SP_DS_PVT_MEM_ADDR, reg_dump_gpuaddr),
		REG(SP_VS_OBJ_START, reg_disasm_gpuaddr),
		REG(SP_FS_OBJ_START, reg_disasm_gpuaddr),
		REG(SP_GS_OBJ_START, reg_disasm_gpuaddr),
		REG(SP_HS_OBJ_START, reg_disasm_gpuaddr),
		REG(SP_DS_OBJ_START, reg_disasm_gpuaddr),
		REG(VFD_FETCH_INSTR_0(0), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(0), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(1), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(1), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(2), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(2), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(3), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(3), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(4), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(4), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(5), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(5), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(6), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(6), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(7), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(7), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(8), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(8), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(9), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(9), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(10), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(10), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(11), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(11), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(12), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(12), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(13), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(13), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(14), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(14), reg_vfd_fetch_instr_1_x),
		REG(VFD_FETCH_INSTR_0(15), reg_vfd_fetch_instr_0_x),
		REG(VFD_FETCH_INSTR_1(15), reg_vfd_fetch_instr_1_x),
		REG(TPL1_TP_VS_BORDER_COLOR_BASE_ADDR, reg_dump_gpuaddr),
		REG(TPL1_TP_HS_BORDER_COLOR_BASE_ADDR, reg_dump_gpuaddr),
		REG(TPL1_TP_DS_BORDER_COLOR_BASE_ADDR, reg_dump_gpuaddr),
		REG(TPL1_TP_GS_BORDER_COLOR_BASE_ADDR, reg_dump_gpuaddr),
		REG(TPL1_TP_FS_BORDER_COLOR_BASE_ADDR, reg_dump_gpuaddr),
#undef REG
}, reg_a5xx[0xffff + 1] = {
#define REG(x, fxn) [REG_A5XX_ ## x] = { fxn }
		REG(SP_VS_OBJ_START, reg_disasm_gpuaddr),
		REG(SP_FS_OBJ_START, reg_disasm_gpuaddr),
#undef REG
}, *type0_reg;

static bool initialized = false;
static struct rnn *rnn;

static void init_rnn(const char *gpuname)
{
	rnn = rnn_new(no_color);

	rnn_load(rnn, gpuname);

	initialized = true;

	if (querystrs) {
		int i;
		queryvals = calloc(nquery, sizeof(queryvals[0]));

		for (i = 0; i < nquery; i++) {
			int val = strtol(querystrs[i], NULL, 0);

			if (val == 0) {
				unsigned regbase;
				/* really need a better way to do this this!! */
				for (regbase = 0; regbase < regcnt(); regbase++) {
					const char *name = regname(regbase, 0);
					if (!name)
						continue;
					if (!strcmp(name, querystrs[i])) {
						val = regbase;
						break;
					}
				}
			}

			queryvals[i] = val;
			printf("querystr: %s -> 0x%x\n", querystrs[i], queryvals[i]);
		}
	}
}

static void init_a2xx(void)
{
	if (type0_reg == reg_a2xx)
		return;
	type0_reg = reg_a2xx;
	init_rnn("a2xx");
}

static void init_a3xx(void)
{
	if (type0_reg == reg_a3xx)
		return;
	type0_reg = reg_a3xx;
	init_rnn("a3xx");
}

static void init_a4xx(void)
{
	if (type0_reg == reg_a4xx)
		return;
	type0_reg = reg_a4xx;
	init_rnn("a4xx");
}

static void init_a5xx(void)
{
	if (type0_reg == reg_a5xx)
		return;
	type0_reg = reg_a5xx;
	init_rnn("a5xx");
}

static void init(void)
{
	if (!initialized) {
		/* default to a2xx so we can still parse older rd files prior to RD_GPU_ID */
		init_a2xx();
	}
}

static const char *regname(uint32_t regbase, int color)
{
	init();
	return rnn_regname(rnn, regbase, color);
}

static void dump_register_val(uint32_t regbase, uint32_t dword, int level)
{
	struct rnndecaddrinfo *info = rnn_reginfo(rnn, regbase);

	if (info && info->typeinfo) {
		char *decoded = rnndec_decodeval(rnn->vc, info->typeinfo, dword, info->width);
		printf("%s%s: %s\n", levels[level], info->name, decoded);
		free(decoded);
	} else if (info) {
		printf("%s%s: %08x\n", levels[level], info->name, dword);

	} else {
		printf("%s<%04x>: %08x\n", levels[level], regbase, dword);
	}

	if (info) {
		free(info->name);
		free(info);
	}
}

static void dump_register(uint32_t regbase, uint32_t dword, int level)
{
	init();

	if (!quiet(3)) {
		dump_register_val(regbase, dword, level);
	}

	if (type0_reg[regbase].fxn) {
		type0_reg[regbase].fxn(regname(regbase, 0), dword, level);
	} else if (reg_axxx[regbase].fxn) {
		reg_axxx[regbase].fxn(regname(regbase, 0), dword, level);
	}
}

static bool is_banked_reg(uint32_t regbase)
{
	return (0x2000 <= regbase) && (regbase < 0x2400);
}

static void dump_registers(uint32_t regbase,
		uint32_t *dwords, uint32_t sizedwords, int level)
{
	while (sizedwords--) {
		int last_summary = summary;

		/* access to non-banked registers needs a WFI:
		 * TODO banked register range for a2xx??
		 */
		if (needs_wfi && !is_banked_reg(regbase))
			printl(2, "NEEDS WFI: %s (%x)\n", regname(regbase, 1), regbase);

		type0_reg_vals[regbase] = *dwords;
		type0_reg_written[regbase/8] |= (1 << (regbase % 8));
		dump_register(regbase, *dwords, level);
		regbase++;
		dwords++;
		summary = last_summary;
	}
}

static void dump_domain(uint32_t *dwords, uint32_t sizedwords, int level,
		const char *name)
{
	struct rnndomain *dom;
	int i;

	init();

	dom = rnn_finddomain(rnn->db, name);

	if (!dom)
		return;

	for (i = 0; i < sizedwords; i++) {
		struct rnndecaddrinfo *info = rnndec_decodeaddr(rnn->vc, dom, i, 0);
		char *decoded;
		if (!(info && info->typeinfo))
			break;
		decoded = rnndec_decodeval(rnn->vc, info->typeinfo, dwords[i], info->width);
		printf("%s%s\n", levels[level], decoded);
		free(decoded);
		free(info->name);
		free(info);
	}
}


static uint32_t bin_x1, bin_x2, bin_y1, bin_y2;

/* well, actually query and script.. */
static void do_query(const char *mode, uint32_t num_indices)
{
	int i;
	for (i = 0; i < nquery; i++) {
		uint32_t regbase = queryvals[i];
		if (reg_written(regbase)) {
			uint32_t lastval = reg_val(regbase);
			printf("%4d: %s(%u,%u-%u,%u):%u", draw_count, mode,
					bin_x1, bin_y1, bin_x2, bin_y2, num_indices);
			printf("\t%08x\t", lastval);
			dump_register_val(regbase, lastval, 0);
		}
	}

	if (num_indices > 0)
		script_draw(mode, num_indices);
}

static void cp_im_loadi(uint32_t *dwords, uint32_t sizedwords, int level)
{
	uint32_t start = dwords[1] >> 16;
	uint32_t size  = dwords[1] & 0xffff;
	const char *type = NULL, *ext = NULL;
	enum shader_t disasm_type;

	switch (dwords[0]) {
	case 0:
		type = "vertex";
		ext = "vo";
		disasm_type = SHADER_VERTEX;
		break;
	case 1:
		type = "fragment";
		ext = "fo";
		disasm_type = SHADER_FRAGMENT;
		break;
	default:
		type = "<unknown>"; break;
	}

	printf("%s%s shader, start=%04x, size=%04x\n", levels[level], type, start, size);
	disasm_a2xx(dwords + 2, sizedwords - 2, level+2, disasm_type);

	/* dump raw shader: */
	if (ext)
		dump_shader(ext, dwords + 2, (sizedwords - 2) * 4);
}

static void cp_load_state(uint32_t *dwords, uint32_t sizedwords, int level)
{
	enum adreno_state_block state_block_id = (dwords[0] >> 19) & 0x7;
	enum adreno_state_type state_type = dwords[1] & 0x3;
	uint32_t num_unit = (dwords[0] >> 22) & 0x1ff;
	uint32_t ext_src_addr;
	void *contents = NULL;
	int i;

	if (quiet(2))
		return;

	if (gpu_id >= 500) {
		ext_src_addr = dwords[1] & 0xfffffffc;
		// TODO 64b: high bits in dwords[2] ??
		contents = dwords + 3;
	} else {
		ext_src_addr = dwords[1] & 0xfffffffc;
		contents = dwords + 2;
	}

	/* we could either have a ptr to other gpu buffer, or directly have
	 * contents inline:
	 */
	if (ext_src_addr)
		contents = hostptr(ext_src_addr);

	if (!contents)
		return;

	switch (state_block_id) {
	case SB_FRAG_SHADER:
	case SB_GEOM_SHADER:
	case SB_VERT_SHADER:
		if (state_type == ST_SHADER) {
			const char *ext = NULL;

			if (gpu_id >= 400)
				num_unit *= 16;
			else if (gpu_id >= 300)
				num_unit *= 4;

			/* shaders:
			 *
			 * note: num_unit seems to be # of instruction groups, where
			 * an instruction group has 4 64bit instructions.
			 */
			if (state_block_id == SB_VERT_SHADER) {
				ext = "vo3";
			} else if (state_block_id == SB_GEOM_SHADER) {
				ext = "go3";
			} else {
				ext = "fo3";
			}

			if (contents)
				disasm_a3xx(contents, num_unit * 2, level+2, 0);

			/* dump raw shader: */
			if (ext)
				dump_shader(ext, contents, num_unit * 2 * 4);
		} else {
			/* uniforms/consts:
			 *
			 * note: num_unit seems to be # of pairs of dwords??
			 */

			if (gpu_id >= 400)
				num_unit *= 2;

			dump_float(contents, num_unit*2, level+1);
			dump_hex(contents, num_unit*2, level+1);
		}
		break;
	case SB_VERT_MIPADDR:
	case SB_FRAG_MIPADDR:
		if (state_type == ST_CONSTANTS) {
			uint32_t *addrs = contents;

			/* mipmap consts block just appears to be array of num_unit gpu addr's: */
			for (i = 0; i < num_unit; i++) {
				void *ptr = hostptr(addrs[i]);
				printf("%s%2d: %08x\n", levels[level+1], i, addrs[i]);
				if (dump_textures) {
					printf("base=%08x\n", gpubaseaddr(addrs[i]));
					dump_hex(ptr, hostlen(addrs[i])/4, level+1);
				}
			}
		} else {
			goto unknown;
		}
		break;
	case SB_FRAG_TEX:
	case SB_VERT_TEX:
		if (state_type == ST_SHADER) {
			uint32_t *texsamp = (uint32_t *)contents;
			for (i = 0; i < num_unit; i++) {
				/* work-around to reduce noise for opencl blob which always
				 * writes the max # regardless of # of textures used
				 */
				if ((num_unit == 16) && (texsamp[0] == 0) && (texsamp[1] == 0))
					break;

				if ((300 <= gpu_id) && (gpu_id < 400)) {
					dump_domain(texsamp, 2, level+2, "A3XX_TEX_SAMP");
					dump_hex(texsamp, 2, level+1);
					texsamp += 2;
				} else if ((400 <= gpu_id) && (gpu_id < 500)) {
					dump_domain(texsamp, 2, level+2, "A4XX_TEX_SAMP");
					dump_hex(texsamp, 2, level+1);
					texsamp += 2;
				} else if ((500 <= gpu_id) && (gpu_id < 600)) {
					dump_domain(texsamp, 4, level+2, "A5XX_TEX_SAMP");
					dump_hex(texsamp, 4, level+1);
					texsamp += 4;
				}
			}
		} else {
			uint32_t *texconst = (uint32_t *)contents;
			for (i = 0; i < num_unit; i++) {
				/* work-around to reduce noise for opencl blob which always
				 * writes the max # regardless of # of textures used
				 */
				if ((num_unit == 16) &&
					(texconst[0] == 0) && (texconst[1] == 0) &&
					(texconst[2] == 0) && (texconst[3] == 0))
					break;

				if ((300 <= gpu_id) && (gpu_id < 400)) {
					dump_domain(texconst, 4, level+2, "A3XX_TEX_CONST");
					dump_hex(texconst, 4, level+1);
					texconst += 4;
				} else if ((400 <= gpu_id) && (gpu_id < 500)) {
					dump_domain(texconst, 8, level+2, "A4XX_TEX_CONST");
					if (dump_textures) {
						uint32_t addr = texconst[4] & ~0x1f;
						printf("base=%08x\n", gpubaseaddr(addr));
						dump_hex(hostptr(addr), hostlen(addr)/4, level-2);
					}
					dump_hex(texconst, 8, level+1);
					texconst += 8;
				} else if ((500 <= gpu_id) && (gpu_id < 600)) {
					dump_domain(texconst, 12, level+2, "A5XX_TEX_CONST");
					dump_hex(texconst, 12, level+1);
					texconst += 12;
				}
			}
		}
		break;
	default:
unknown:
		/* hmm.. */
		dump_hex(contents, num_unit, level+1);
		break;
	}

}

static void cp_set_bin(uint32_t *dwords, uint32_t sizedwords, int level)
{
	bin_x1 = dwords[1] & 0xffff;
	bin_y1 = dwords[1] >> 16;
	bin_x2 = dwords[2] & 0xffff;
	bin_y2 = dwords[2] >> 16;
}

static void dump_tex_const(uint32_t *dwords, uint32_t sizedwords, uint32_t val, int level)
{
	uint32_t w, h, p;
	uint32_t gpuaddr, flags, mip_gpuaddr, mip_flags;
	uint32_t min, mag, swiz, clamp_x, clamp_y, clamp_z;
	static const char *filter[] = {
			"point", "bilinear", "bicubic",
	};
	static const char *clamp[] = {
			"wrap", "mirror", "clamp-last-texel",
	};
	static const char swiznames[] = "xyzw01??";

	/* see sys2gmem_tex_const[] in adreno_a2xxx.c */

	/* Texture, FormatXYZW=Unsigned, ClampXYZ=Wrap/Repeat,
	 * RFMode=ZeroClamp-1, Dim=1:2d, pitch
	 */
	p = (dwords[0] >> 22) << 5;
	clamp_x = (dwords[0] >> 10) & 0x3;
	clamp_y = (dwords[0] >> 13) & 0x3;
	clamp_z = (dwords[0] >> 16) & 0x3;

	/* Format=6:8888_WZYX, EndianSwap=0:None, ReqSize=0:256bit, DimHi=0,
	 * NearestClamp=1:OGL Mode
	 */
	parse_dword_addr(dwords[1], &gpuaddr, &flags, 0xfff);

	/* Width, Height, EndianSwap=0:None */
	w = (dwords[2] & 0x1fff) + 1;
	h = ((dwords[2] >> 13) & 0x1fff) + 1;

	/* NumFormat=0:RF, DstSelXYZW=XYZW, ExpAdj=0, MagFilt=MinFilt=0:Point,
	 * Mip=2:BaseMap
	 */
	mag = (dwords[3] >> 19) & 0x3;
	min = (dwords[3] >> 21) & 0x3;
	swiz = (dwords[3] >> 1) & 0xfff;

	/* VolMag=VolMin=0:Point, MinMipLvl=0, MaxMipLvl=1, LodBiasH=V=0,
	 * Dim3d=0
	 */
	// XXX

	/* BorderColor=0:ABGRBlack, ForceBC=0:diable, TriJuice=0, Aniso=0,
	 * Dim=1:2d, MipPacking=0
	 */
	parse_dword_addr(dwords[5], &mip_gpuaddr, &mip_flags, 0xfff);

	printf("%sset texture const %04x\n", levels[level], val);
	printf("%sclamp x/y/z: %s/%s/%s\n", levels[level+1],
			clamp[clamp_x], clamp[clamp_y], clamp[clamp_z]);
	printf("%sfilter min/mag: %s/%s\n", levels[level+1], filter[min], filter[mag]);
	printf("%sswizzle: %c%c%c%c\n", levels[level+1],
			swiznames[(swiz >> 0) & 0x7], swiznames[(swiz >> 3) & 0x7],
			swiznames[(swiz >> 6) & 0x7], swiznames[(swiz >> 9) & 0x7]);
	printf("%saddr=%08x (flags=%03x), size=%dx%d, pitch=%d, format=%s\n",
			levels[level+1], gpuaddr, flags, w, h, p,
			fmt_name[flags & 0xf]);
	printf("%smipaddr=%08x (flags=%03x)\n", levels[level+1],
			mip_gpuaddr, mip_flags);
}

static void dump_shader_const(uint32_t *dwords, uint32_t sizedwords, uint32_t val, int level)
{
	int i;
	printf("%sset shader const %04x\n", levels[level], val);
	for (i = 0; i < sizedwords; ) {
		uint32_t gpuaddr, flags;
		parse_dword_addr(dwords[i++], &gpuaddr, &flags, 0xf);
		void *addr = hostptr(gpuaddr);
		if (addr) {
			uint32_t size = dwords[i++];
			printf("%saddr=%08x, size=%d, format=%s\n", levels[level+1],
					gpuaddr, size, fmt_name[flags & 0xf]);
			// TODO maybe dump these as bytes instead of dwords?
			size = (size + 3) / 4; // for now convert to dwords
			dump_hex(addr, min(size, 64), level + 1);
			if (size > min(size, 64))
				printf("%s\t\t...\n", levels[level+1]);
			dump_float(addr, min(size, 64), level + 1);
			if (size > min(size, 64))
				printf("%s\t\t...\n", levels[level+1]);
		}
	}
}

static void cp_set_const(uint32_t *dwords, uint32_t sizedwords, int level)
{
	uint32_t val = dwords[0] & 0xffff;
	switch((dwords[0] >> 16) & 0xf) {
	case 0x0:
		dump_float((float *)(dwords+1), sizedwords-1, level+1);
		break;
	case 0x1:
		/* need to figure out how const space is partitioned between
		 * attributes, textures, etc..
		 */
		if (val < 0x78) {
			dump_tex_const(dwords+1, sizedwords-1, val, level);
		} else {
			dump_shader_const(dwords+1, sizedwords-1, val, level);
		}
		break;
	case 0x2:
		printf("%sset bool const %04x\n", levels[level], val);
		break;
	case 0x3:
		printf("%sset loop const %04x\n", levels[level], val);
		break;
	case 0x4:
		val += 0x2000;
		if (dwords[0] & 0x80000000) {
			uint32_t srcreg = dwords[1];
			uint32_t dstval = dwords[2];

			/* TODO: not sure what happens w/ payload != 2.. */
			assert(sizedwords == 3);
			assert(srcreg < ARRAY_SIZE(type0_reg_vals));

			/* note: rnn_regname uses a static buf so we can't do
			 * two regname() calls for one printf..
			 */
			printf("%s%s = %08x + ", levels[level], regname(val, 1), dstval);
			printf("%s (%08x)\n", regname(srcreg, 1), type0_reg_vals[srcreg]);

			dstval += type0_reg_vals[srcreg];

			dump_registers(val, &dstval, 1, level+1);
		} else {
			dump_registers(val, dwords+1, sizedwords-1, level+1);
		}
		break;
	}
}

static void cp_event_write(uint32_t *dwords, uint32_t sizedwords, int level)
{
	printl(2, "%sevent %s\n", levels[level],
			rnn_enumname(rnn, "vgt_event_type", dwords[0]));
}

static void dump_register_summary(int level)
{
	uint32_t i;

	/* dump current state of registers: */
	printl(2, "%sdraw[%i] register values\n", levels[level], draw_count);
	for (i = 0; i < regcnt(); i++) {
		uint32_t regbase = i;
		uint32_t lastval = reg_val(regbase);
		/* skip registers that have zero: */
		if (!lastval && !allregs)
			continue;
		if (!reg_written(regbase))
			continue;
		if (lastval != lastvals[regbase]) {
			printl(2, "!");
			lastvals[regbase] = lastval;
		}
		if (!quiet(2))
			dump_register(regbase, lastval, level);
	}

// XXX we probably want to separate "written ever" from "written since last draw"
//	clear_written();
}

static uint32_t draw_indx_common(uint32_t *dwords, int level)
{
	uint32_t prim_type     = dwords[1] & 0x1f;
	uint32_t source_select = (dwords[1] >> 6) & 0x3;
	uint32_t num_indices   = dwords[2];
	const char *primtype;

	primtype = rnn_enumname(rnn, "pc_di_primtype", prim_type);

	do_query(primtype, num_indices);

	printl(2, "%sdraw:          %d\n", levels[level], draws[ib]);
	printl(2, "%sprim_type:     %s (%d)\n", levels[level], primtype,
			prim_type);
	printl(2, "%ssource_select: %s (%d)\n", levels[level],
			rnn_enumname(rnn, "pc_di_src_sel", source_select),
			source_select);
	printl(2, "%snum_indices:   %d\n", levels[level], num_indices);

	vertices += num_indices;

	draws[ib]++;

	return num_indices;
}
static void cp_draw_indx(uint32_t *dwords, uint32_t sizedwords, int level)
{
	uint32_t num_indices = draw_indx_common(dwords, level);
	bool saved_summary = summary;

	summary = false;

	/* if we have an index buffer, dump that: */
	if (sizedwords == 5) {
		void *ptr = hostptr(dwords[3]);
		printl(2, "%sgpuaddr:       %08x\n", levels[level], dwords[3]);
		printl(2, "%sidx_size:      %d\n", levels[level], dwords[4]);
		if (ptr) {
			enum pc_di_index_size size =
					((dwords[1] >> 11) & 1) | ((dwords[1] >> 12) & 2);
			if (!quiet(2)) {
				int i;
				printf("%sidxs:         ", levels[level]);
				if (size == INDEX_SIZE_8_BIT) {
					uint8_t *idx = ptr;
					for (i = 0; i < dwords[4]; i++)
						printf(" %u", idx[i]);
				} else if (size == INDEX_SIZE_16_BIT) {
					uint16_t *idx = ptr;
					for (i = 0; i < dwords[4]/2; i++)
						printf(" %u", idx[i]);
				} else if (size == INDEX_SIZE_32_BIT) {
					uint32_t *idx = ptr;
					for (i = 0; i < dwords[4]/4; i++)
						printf(" %u", idx[i]);
				}
				printf("\n");
				dump_hex(ptr, dwords[4]/4, level+1);
			}
		}
	}

	/* don't bother dumping registers for the dummy draw_indx's.. */
	if (num_indices > 0)
		dump_register_summary(level);

	draw_count++;
	summary = saved_summary;

	needs_wfi = true;
}

static void cp_draw_indx_2(uint32_t *dwords, uint32_t sizedwords, int level)
{
	uint32_t num_indices = draw_indx_common(dwords, level);
	enum pc_di_index_size size =
			((dwords[1] >> 11) & 1) | ((dwords[1] >> 12) & 2);
	void *ptr = &dwords[3];
	int sz = 0;
	bool saved_summary = summary;

	summary = false;

	/* CP_DRAW_INDX_2 has embedded/inline idx buffer: */
	if (!quiet(2)) {
		int i;
		printf("%sidxs:         ", levels[level]);
		if (size == INDEX_SIZE_8_BIT) {
			uint8_t *idx = ptr;
			for (i = 0; i < num_indices; i++)
				printf(" %u", idx[i]);
			sz = num_indices;
		} else if (size == INDEX_SIZE_16_BIT) {
			uint16_t *idx = ptr;
			for (i = 0; i < num_indices; i++)
				printf(" %u", idx[i]);
			sz = num_indices * 2;
		} else if (size == INDEX_SIZE_32_BIT) {
			uint32_t *idx = ptr;
			for (i = 0; i < num_indices; i++)
				printf(" %u", idx[i]);
			sz = num_indices * 4;
		}
		printf("\n");
		dump_hex(ptr, sz / 4, level+1);
	}

	/* don't bother dumping registers for the dummy draw_indx's.. */
	if (num_indices > 0)
		dump_register_summary(level);

	draw_count++;
	summary = saved_summary;
}

static void cp_draw_indx_offset(uint32_t *dwords, uint32_t sizedwords, int level)
{
	uint32_t num_indices = dwords[2];
	uint32_t prim_type = dwords[0] & 0x1f;
	bool saved_summary = summary;

	do_query(rnn_enumname(rnn, "pc_di_primtype", prim_type), num_indices);

	summary = false;

	/* don't bother dumping registers for the dummy draw_indx's.. */
	if (num_indices > 0)
		dump_register_summary(level);

	draw_count++;
	summary = saved_summary;
}

static void cp_run_cl(uint32_t *dwords, uint32_t sizedwords, int level)
{
	bool saved_summary = summary;

	do_query("COMPUTE", 1);

	summary = false;

	dump_register_summary(level);

	draw_count++;
	summary = saved_summary;
}

static void cp_nop(uint32_t *dwords, uint32_t sizedwords, int level)
{
	const char *buf = (void *)dwords;
	int i;

	/* attempt to decode as string: */
	printf("%08x:%s", gpuaddr(dwords), levels[level]);
	for (i = 0; i < 4 * sizedwords; i++) {
		if (buf[i] == '\0')
			break;
		if (isascii(buf[i]))
			printf("%c", buf[i]);
	}
	printf("\n");
}

static void cp_indirect(uint32_t *dwords, uint32_t sizedwords, int level)
{
	/* traverse indirect buffers */
	int i;
	uint32_t ibaddr, ibsize;
	uint32_t *ptr = NULL;

	if (gpu_id >= 500) {
		/* a5xx+.. high 32b of gpu addr, then size: */
		// TODO 64b: high bits in dwords[1] ??
		ibaddr = dwords[0];
		ibsize = dwords[2];
	} else {
		ibaddr = dwords[0];
		ibsize = dwords[1];
	}

	if (!quiet(3)) {
		printf("%sibaddr:%08x\n", levels[level], ibaddr);
		printf("%sibsize:%08x\n", levels[level], ibsize);
	} else {
		level--;
	}

	/* map gpuaddr back to hostptr: */
	for (i = 0; i < nbuffers; i++) {
		if (buffer_contains_gpuaddr(&buffers[i], ibaddr, ibsize)) {
			ptr = buffers[i].hostptr + (ibaddr - buffers[i].gpuaddr);
			break;
		}
	}

	if (ptr) {
		ib++;
		dump_commands(ptr, ibsize, level);
		ib--;
	} else {
		fprintf(stderr, "could not find: %08x (%d)\n", ibaddr, ibsize);
	}
}

static void cp_wfi(uint32_t *dwords, uint32_t sizedwords, int level)
{
	needs_wfi = false;
}

static void cp_mem_write(uint32_t *dwords, uint32_t sizedwords, int level)
{
	uint32_t gpuaddr = dwords[0];

	if (quiet(2))
		return;

	printf("%sgpuaddr:%08x\n", levels[level], gpuaddr);
	dump_float((float *)&dwords[1], sizedwords-1, level+1);
}

static void cp_rmw(uint32_t *dwords, uint32_t sizedwords, int level)
{
	uint32_t val = dwords[0] & 0xffff;
	uint32_t and = dwords[1];
	uint32_t or  = dwords[2];
	printl(3, "%srmw (%s & 0x%08x) | 0x%08x)\n", levels[level], regname(val, 1), and, or);
	if (needs_wfi)
		printl(2, "NEEDS WFI: rmw (%s & 0x%08x) | 0x%08x)\n", regname(val, 1), and, or);
	type0_reg_vals[val] = (type0_reg_vals[val] & and) | or;
	type0_reg_written[val/8] |= (1 << (val % 8));
}

static void cp_reg_to_mem(uint32_t *dwords, uint32_t sizedwords, int level)
{
	uint32_t val = dwords[0] & 0xffff;
	uint32_t cnt = 1 + ((dwords[0] >> 19) & 0x7ff);   /* not quite sure bitfield size */
	uint32_t mem = dwords[0];
	/* no real idea about the top too bits.. */
	printl(3, "%sread: %s\n", levels[level], regname(val, 1));
	printl(3, "%scount: %d\n", levels[level], cnt);
	printl(3, "%sdest: %08x\n", levels[level], mem);
}

static void cp_set_draw_state(uint32_t *dwords, uint32_t sizedwords, int level)
{
	uint32_t i;

	for (i = 0; i < sizedwords; ) {
		uint32_t count = dwords[i] & 0xffff;
		uint32_t addr, *ptr;

		if (gpu_id >= 500) {
			addr = dwords[i + 1];
			// TODO 64b: high bits in dwords[i + 2]
			i += 3;
		} else {
			addr = dwords[i + 1];
			i += 2;
		}

		ptr = hostptr(addr);

		if (ptr) {
			if (!quiet(2))
				dump_hex(ptr, count, level+1);

			ib++;
			dump_commands(ptr, count, level+1);
			ib--;
		}
	}
}


#define CP(x, fxn)   [CP_ ## x] = { fxn }
static const struct {
	void (*fxn)(uint32_t *dwords, uint32_t sizedwords, int level);
} type3_op[0xff] = {
		CP(ME_INIT, NULL),
		CP(NOP, cp_nop),
		CP(INDIRECT_BUFFER, cp_indirect),
		CP(INDIRECT_BUFFER_PFD, cp_indirect),
		CP(WAIT_FOR_IDLE, cp_wfi),
		CP(WAIT_REG_MEM, NULL),
		CP(WAIT_REG_EQ, NULL),
		CP(WAIT_REG_GTE, NULL),
		CP(WAIT_UNTIL_READ, NULL),
		CP(WAIT_IB_PFD_COMPLETE, NULL),
		CP(REG_RMW, cp_rmw),
		CP(REG_TO_MEM, cp_reg_to_mem),
		CP(MEM_WRITE, cp_mem_write),
		CP(MEM_WRITE_CNTR, NULL),
		CP(COND_EXEC, NULL),
		CP(COND_WRITE, NULL),
		CP(EVENT_WRITE, cp_event_write),
		CP(EVENT_WRITE_SHD, NULL),
		CP(EVENT_WRITE_CFL, NULL),
		CP(EVENT_WRITE_ZPD, NULL),
		CP(RUN_OPENCL, cp_run_cl),
		CP(DRAW_INDX, cp_draw_indx),
		CP(DRAW_INDX_2, cp_draw_indx_2),
		CP(DRAW_INDX_BIN, NULL),
		CP(DRAW_INDX_2_BIN, NULL),
		CP(VIZ_QUERY, NULL),
		CP(SET_STATE, NULL),
		CP(SET_CONSTANT, cp_set_const),
		CP(IM_LOAD, NULL),
		CP(IM_LOAD_IMMEDIATE, cp_im_loadi),
		CP(LOAD_CONSTANT_CONTEXT, NULL),
		CP(INVALIDATE_STATE, NULL),
		CP(SET_SHADER_BASES, NULL),
		CP(SET_BIN_MASK, NULL),
		CP(SET_BIN_SELECT, NULL),
		CP(CONTEXT_UPDATE, NULL),
		CP(INTERRUPT, NULL),
		CP(IM_STORE, NULL),
		CP(SET_PROTECTED_MODE, NULL),

		/* for a20x */
		//CP(SET_BIN_BASE_OFFSET, NULL),

		/* for a22x */
		CP(SET_DRAW_INIT_FLAGS, NULL),

		/* for a3xx */
		CP(LOAD_STATE, cp_load_state),
		CP(SET_BIN_DATA, NULL),
		CP(SET_BIN, cp_set_bin),

		/* for a4xx */
		CP(SET_DRAW_STATE, cp_set_draw_state),
		CP(DRAW_INDX_OFFSET, cp_draw_indx_offset),
};


static inline uint pm4_calc_odd_parity_bit(uint val)
{
	return (0x9669 >> (0xf & ((val) ^
			((val) >> 4) ^ ((val) >> 8) ^ ((val) >> 12) ^
			((val) >> 16) ^ ((val) >> 20) ^ ((val) >> 24) ^
			((val) >> 28)))) & 1;
}

#define pkt_is_type0(pkt) (((pkt) & 0XC0000000) == CP_TYPE0_PKT)
#define type0_pkt_size(pkt) ((((pkt) >> 16) & 0x3FFF) + 1)
#define type0_pkt_offset(pkt) ((pkt) & 0x7FFF)

/*
 * Check both for the type3 opcode and make sure that the reserved bits [1:7]
 * and 15 are 0
 */

#define pkt_is_type3(pkt) \
        ((((pkt) & 0xC0000000) == CP_TYPE3_PKT) && \
         (((pkt) & 0x80FE) == 0))

#define cp_type3_opcode(pkt) (((pkt) >> 8) & 0xFF)
#define type3_pkt_size(pkt) ((((pkt) >> 16) & 0x3FFF) + 1)

#define pkt_is_type4(pkt) \
        ((((pkt) & 0xF0000000) == CP_TYPE4_PKT) && \
         ((((pkt) >> 27) & 0x1) == \
         pm4_calc_odd_parity_bit(type4_pkt_offset(pkt))) \
         && ((((pkt) >> 7) & 0x1) == \
         pm4_calc_odd_parity_bit(type4_pkt_size(pkt))))

#define type4_pkt_offset(pkt) (((pkt) >> 8) & 0x7FFFF)
#define type4_pkt_size(pkt) ((pkt) & 0x7F)

#define pkt_is_type7(pkt) \
        ((((pkt) & 0xF0000000) == CP_TYPE7_PKT) && \
         (((pkt) & 0x0F000000) == 0) && \
         ((((pkt) >> 23) & 0x1) == \
         pm4_calc_odd_parity_bit(cp_type7_opcode(pkt))) \
         && ((((pkt) >> 15) & 0x1) == \
         pm4_calc_odd_parity_bit(type7_pkt_size(pkt))))

#define cp_type7_opcode(pkt) (((pkt) >> 16) & 0x7F)
#define type7_pkt_size(pkt) ((pkt) & 0x3FFF)


static void dump_commands(uint32_t *dwords, uint32_t sizedwords, int level)
{
	int dwords_left = sizedwords;
	uint32_t count = 0; /* dword count including packet header */
	uint32_t val;

	if (!dwords) {
		printf("NULL cmd buffer!\n");
		return;
	}

	draws[ib] = 0;

	while (dwords_left > 0) {

		/* hack, this looks like a -1 underflow, in some versions
		 * when it tries to write zero registers via pkt0
		 */
//		if ((dwords[0] >> 16) == 0xffff)
//			goto skip;

		if (pkt_is_type0(dwords[0])) {
			printl(3, "t0");
			count = type0_pkt_size(dwords[0]) + 1;
			val = type0_pkt_offset(dwords[0]);
			printl(3, "%swrite %s%s\n", levels[level+1], regname(val, 1),
					(dwords[0] & 0x8000) ? " (same register)" : "");
			dump_registers(val, dwords+1, count-1, level+2);
			if (!quiet(3))
				dump_hex(dwords, count, level+1);
		} else if (pkt_is_type4(dwords[0])) {
			/* basically the same(ish) as type0 prior to a5xx */
			printl(3, "t4");
			count = type4_pkt_size(dwords[0]) + 1;
			val = type4_pkt_offset(dwords[0]);
			printl(3, "%swrite %s\n", levels[level+1], regname(val, 1));
			dump_registers(val, dwords+1, count-1, level+2);
			if (!quiet(3))
				dump_hex(dwords, count, level+1);
#if 0
		} else if (pkt_is_type1(dwords[0])) {
			printl(3, "t1");
			count = 3;
			val = dwords[0] & 0xfff;
			printl(3, "%swrite %s\n", levels[level+1], regname(val, 1));
			dump_registers(val, dwords+1, 1, level+2);
			val = (dwords[0] >> 12) & 0xfff;
			printl(3, "%swrite %s\n", levels[level+1], regname(val, 1));
			dump_registers(val, dwords+2, 1, level+2);
			if (!quiet(3))
				dump_hex(dwords, count, level+1);
		} else if (pkt_is_type2(dwords[0])) {
			printl(3, "t2");
			printf("%sNOP\n", levels[level+1]);
			count = 1;
			if (!quiet(3))
				dump_hex(dwords, count, level+1);
#endif
		} else if (pkt_is_type3(dwords[0])) {
			printl(3, "t3");
			count = type3_pkt_size(dwords[0]) + 1;
			val = cp_type3_opcode(dwords[0]);
			init();
			if (!quiet(2)) {
				const char *name;
				name = rnn_enumname(rnn, "adreno_pm4_type3_packets", val);
				printf("\t%sopcode: %s%s%s (%02x) (%d dwords)%s\n", levels[level],
						rnn->vc->colors->bctarg, name, rnn->vc->colors->reset,
						val, count, (dwords[0] & 0x1) ? " (predicated)" : "");
				if (name)
					dump_domain(dwords+1, count-1, level+2, name);
			}
			if (type3_op[val].fxn)
				type3_op[val].fxn(dwords+1, count-1, level+1);
			if (!quiet(2))
				dump_hex(dwords, count, level+1);
		} else if (pkt_is_type7(dwords[0])) {
			printl(3, "t7");
			count = type7_pkt_size(dwords[0]) + 1;
			val = cp_type7_opcode(dwords[0]);
			init();
			if (!quiet(2)) {
				const char *name;
				name = rnn_enumname(rnn, "adreno_pm4_type3_packets", val);
				printf("\t%sopcode: %s%s%s (%02x) (%d dwords)%s\n", levels[level],
						rnn->vc->colors->bctarg, name, rnn->vc->colors->reset,
						val, count, (dwords[0] & 0x1) ? " (predicated)" : "");
				if (name)
					dump_domain(dwords+1, count-1, level+2, name);
			}
			if (type3_op[val].fxn)
				type3_op[val].fxn(dwords+1, count-1, level+1);
			if (!quiet(2))
				dump_hex(dwords, count, level+1);
		} else {
			fprintf(stderr, "bad type!\n");
			return;
		}

		dwords += count;
		dwords_left -= count;

	}

	if (dwords_left < 0)
		printf("**** this ain't right!! dwords_left=%d\n", dwords_left);
}

static int handle_file(const char *filename, int start, int end, int draw);

static void print_usage(const char *name)
{
	printf("Usage: %s [OPTIONS]... FILE...\n", name);
	printf("    --verbose         - more verbose disassembly\n");
	printf("    --dump-shaders    - dump each shader to raw file\n");
	printf("    --no-color        - disable colorized output (default for non-console\n");
	printf("                        output)\n");
	printf("    --color           - enable colorized output (default for tty output)\n");
	printf("    --summary         - don't show individual register writes, but just show\n");
	printf("                        register values on draws\n");
	printf("    --allregs         - show all registers (including ones not written since\n");
	printf("                        previous draw) at each draw\n");
	printf("    --start N         - decode start frame number\n");
	printf("    --end N           - decode end frame number\n");
	printf("    --frame N         - decode specified frame number\n");
	printf("    --draw N          - decode specified draw number\n");
	printf("    --textures        - dump texture contents (if possible)\n");
	printf("    --script FILE     - run specified lua script to analyze state at draws\n");
	printf("    --query/-q REG    - query mode, dump only specified query registers on\n");
	printf("                        each draw; multiple --query/-q args can be given to\n");
	printf("                        dump multiple registers; register can be specified\n");
	printf("                        either by name or numeric offset\n");
	printf("    --help            - show this message\n");
}


static pid_t pager_pid;

static void pager_death(int n)
{
	exit(0);
}

static void pager_open(void)
{
	int fd[2];

	if (pipe(fd) < 0) {
		fprintf(stderr, "Failed to create pager pipe: %m\n");
		exit(-1);
	}

	pager_pid = fork();
	if (pager_pid < 0) {
		fprintf(stderr, "Failed to fork pager: %m\n");
		exit(-1);
	}

	if (pager_pid == 0) {
		const char* less_opts;

		dup2(fd[0], STDIN_FILENO);
		close(fd[0]);
		close(fd[1]);

		less_opts = "FRSMKX";
		setenv("LESS", less_opts, 1);

		execlp("less", "less", NULL);

	} else {
		/* we want to kill the parent process when pager exits: */
		signal(SIGCHLD, pager_death);
		dup2(fd[1], STDOUT_FILENO);
		close(fd[0]);
		close(fd[1]);
	}
}

static int pager_close(void)
{
	siginfo_t status;

	close(STDOUT_FILENO);

	while (true) {
		memset(&status, 0, sizeof(status));
		if (waitid(P_PID, pager_pid, &status, WEXITED) < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}

		return 0;
	}
}

int main(int argc, char **argv)
{
	int ret, n = 1;
	int start = 0, end = 0x7ffffff, draw = -1;
	int interactive = isatty(STDOUT_FILENO);

	no_color = !interactive;

	while (n < argc) {
		if (!strcmp(argv[n], "--verbose")) {
			disasm_set_debug(PRINT_RAW);
			n++;
			continue;
		}

		if (!strcmp(argv[n], "--dump-shaders")) {
			dump_shaders = true;
			n++;
			continue;
		}

		if (!strcmp(argv[n], "--no-color")) {
			no_color = true;
			n++;
			continue;
		}

		if (!strcmp(argv[n], "--color")) {
			no_color = false;
			n++;
			continue;
		}

		if (!strcmp(argv[n], "--summary")) {
			summary = true;
			n++;
			continue;
		}

		if (!strcmp(argv[n], "--allregs")) {
			allregs = true;
			n++;
			continue;
		}

		if (!strcmp(argv[n], "--start")) {
			n++;
			start = atoi(argv[n]);
			n++;
			continue;
		}

		if (!strcmp(argv[n], "--end")) {
			n++;
			end = atoi(argv[n]);
			n++;
			continue;
		}

		if (!strcmp(argv[n], "--frame")) {
			n++;
			end = start = atoi(argv[n]);
			n++;
			continue;
		}

		if (!strcmp(argv[n], "--draw")) {
			n++;
			draw = atoi(argv[n]);
			n++;
			continue;
		}

		if (!strcmp(argv[n], "--textures")) {
			n++;
			dump_textures = true;
			continue;
		}

		if (!strcmp(argv[n], "--script")) {
			n++;
			script = argv[n];
			if (script_load(script)) {
				fprintf(stderr, "error loading %s\n", argv[n]);
				return 1;
			}
			n++;
			continue;
		}

		if (!strcmp(argv[n], "--query") ||
				!strcmp(argv[n], "-q")) {
			n++;
			querystrs = realloc(querystrs, (nquery + 1) * sizeof(*querystrs));
			querystrs[nquery] = argv[n];
			nquery++;
			n++;
			interactive = 0;
			continue;
		}

		if (!strcmp(argv[n], "--help")) {
			n++;
			print_usage(argv[0]);
			return 0;
		}

		break;
	}

	if (interactive) {
		pager_open();
	}

	rnn = rnn_new(no_color);

	while (n < argc) {
		ret = handle_file(argv[n], start, end, draw);
		if (ret) {
			fprintf(stderr, "error reading: %s\n", argv[n]);
			fprintf(stderr, "continuing..\n");
		}
		n++;
	}

	if (ret) {
		print_usage(argv[0]);
		return ret;
	}

	script_finish();

	if (interactive) {
		pager_close();
	}

	return 0;
}

static int handle_file(const char *filename, int start, int end, int draw)
{
	enum rd_sect_type type = RD_NONE;
	void *buf = NULL;
	struct io *io;
	int submit = 0, got_gpu_id = 0;
	int sz, i, ret = 0;
	bool needs_reset;

	draw_filter = draw;
	draw_count = 0;

	printf("Reading %s...\n", filename);

	script_start_cmdstream(filename);

	if (!strcmp(filename, "-"))
		io = io_openfd(0);
	else
		io = io_open(filename);

	if (!io) {
		fprintf(stderr, "could not open: %s\n", filename);
		return -1;
	}

	clear_written();
	clear_lastvals();

	if (check_extension(filename, ".txt")) {
		/* read in from hexdump.. this could probably be more flexibile,
		 * but right now the format is:
		 *
		 *   "%x(ignored): %x %x %x %x %x %x %x %x
		 *
		 * and buf size is hard coded..  this is just for a quick hack
		 * I needed, if txt input is really useful this should be made
		 * less lame..
		 */
#define SZ 40960
		char *strbuf  = calloc(SZ, 1);
		uint32_t *buf = calloc(SZ, 1);
		uint32_t *bufp = buf;
		uint32_t dummy, sizedwords = 0;
		int n;

		io_readn(io, strbuf, SZ);

		do {
			n = sscanf(strbuf, "%x: %x %x %x %x %x %x %x %x", &dummy,
							&bufp[0], &bufp[1], &bufp[2], &bufp[3],
							&bufp[4], &bufp[5], &bufp[6], &bufp[7]);
			if (n <= 0)
				break;

			sizedwords += n - 1;
			bufp += 8;

			/* scan fwd until next newline: */
			while (strbuf[0] != '\n')
				strbuf++;
			strbuf++;

		} while (1);

		init_a3xx();

		printf("############################################################\n");
		printf("cmdstream: %d dwords\n", sizedwords);
		dump_commands(buf, sizedwords, 0);
		printf("############################################################\n");
		printf("vertices: %d\n", vertices);

		return 0;
	}

	while (true) {
		uint32_t arr[2];

		ret = io_readn(io, arr, 8);
		if (ret <= 0)
			goto end;

		while ((arr[0] == 0xffffffff) && (arr[1] == 0xffffffff)) {
			ret = io_readn(io, arr, 8);
			if (ret <= 0)
				goto end;
		}

		type = arr[0];
		sz = arr[1];

		if (sz < 0) {
			ret = -1;
			goto end;
		}

		free(buf);

		needs_wfi = false;

		buf = malloc(sz + 1);
		((char *)buf)[sz] = '\0';
		ret = io_readn(io, buf, sz);
		if (ret < 0)
			goto end;

		switch(type) {
		case RD_TEST:
			printl(1, "test: %s\n", (char *)buf);
			break;
		case RD_CMD:
			printl(2, "cmd: %s\n", (char *)buf);
			break;
		case RD_VERT_SHADER:
			printl(2, "vertex shader:\n%s\n", (char *)buf);
			break;
		case RD_FRAG_SHADER:
			printl(2, "fragment shader:\n%s\n", (char *)buf);
			break;
		case RD_GPUADDR:
			if (needs_reset) {
				for (i = 0; i < nbuffers; i++) {
					free(buffers[i].hostptr);
					buffers[i].hostptr = NULL;
				}
				nbuffers = 0;
				needs_reset = false;
			}
			buffers[nbuffers].gpuaddr = ((uint32_t *)buf)[0];
			buffers[nbuffers].len = ((uint32_t *)buf)[1];
			break;
		case RD_BUFFER_CONTENTS:
			buffers[nbuffers].hostptr = buf;
			nbuffers++;
			assert(nbuffers < ARRAY_SIZE(buffers));
			buf = NULL;
			break;
		case RD_CMDSTREAM_ADDR:
			if ((start <= submit) && (submit <= end)) {
				printl(2, "############################################################\n");
				printl(2, "cmdstream: %d dwords\n", ((uint32_t *)buf)[1]);
				dump_commands(hostptr(((uint32_t *)buf)[0]),
						((uint32_t *)buf)[1], 0);
				printl(2, "############################################################\n");
				printl(2, "vertices: %d\n", vertices);
			}
			needs_reset = true;
			submit++;
			break;
		case RD_GPU_ID:
			if (!got_gpu_id) {
				gpu_id = *((unsigned int *)buf);
				printl(2, "gpu_id: %d\n", gpu_id);
				if (gpu_id >= 500)
					init_a5xx();
				else if (gpu_id >= 400)
					init_a4xx();
				else if (gpu_id >= 300)
					init_a3xx();
				else
					init_a2xx();
				got_gpu_id = 1;
			}
			break;
		default:
			break;
		}
	}

	script_end_cmdstream();

	io_close(io);

	return 0;

end:
	if (ret < 0) {
		printf("corrupt file\n");
	}
	return 0;
}
