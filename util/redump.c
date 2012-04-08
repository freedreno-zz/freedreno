/*
 * Copyright © 2012 Rob Clark <robclark@freedesktop.org>
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

static const uint32_t patterns[] = {
		/* these should be ordered by most inclusive pattern, ie. most 'f's */
		0xffffffff,
		0xffffff00,
		0xffff00ff,
		0xff00ffff,
		0x00ffffff,
		0xffff0000,
		0x0000ffff,
		0xff000000,
		0x00ff0000,
		0x0000ff00,
		0x000000ff,
};

static const struct {
	uint32_t val, mask, color;
} known_patterns[] = {
		{ 0x7c000275, 0xffffffff, 0xdd0000 },
		{ 0x7c000100, 0xffffff00, 0x990099 },
};

static const uint32_t gpuaddr_colors[] = {
		0x00ff0000,
		0x0000ff00,
		0x000000ff,
		0x00cc0000,
		0x0000cc00,
		0x000000cc,
};

static const uint32_t param_colors[] = {
		0x00ff1111,
		0x0011ff11,
		0x001111ff,
		0x0011aaaa,
		0x00aa11aa,
		0x00aaaa11,
		0x00448888,
		0x00884488,
		0x00888844,
		0x00557777,
		0x00775577,
		0x00777755,
		0x00777777,
		// XXX don't forget to update if more params added:
		0x00ffffff,
		0x00ffffff,
		0x00ffffff,
		0x00ffffff,
		0x00ffffff,
		0x00ffffff,
		0x00ffffff,
		0x00ffffff,
		0x00ffffff,
		0x00ffffff,
		0x00ffffff,
		0x00ffffff,
};

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

struct param {
	enum rd_param_type type;
	uint32_t val, bitlen;
};

struct context {
	int       fd;
	uint32_t *buf;           /* current row buffer */
	int       sz;            /* current row buffer size */
	uint32_t  gpuaddrs[32];
	int       ngpuaddrs;
	struct param params[32];
	int       nparams;
};

struct context ctxts[64];
int nctxts;
typedef int offsets_t[ARRAY_SIZE(ctxts)];

static void handle_string(struct context *ctx)
{
	printf("%s", (char *)ctx->buf);
}

static void handle_gpuaddr(struct context *ctx)
{
	uint32_t gpuaddr = ctx->buf[0];
	printf("<font color=\"#%06x\"><b>%08x</b></font><br>",
			gpuaddr_colors[ctx->ngpuaddrs], gpuaddr);
	printf("(len: %x)", ctx->buf[1]);
	ctx->gpuaddrs[ctx->ngpuaddrs++] = gpuaddr;
}

static int find_gpuaddr(struct context *ctx, uint32_t dword)
{
	int i;
	for (i = 0; i < ctx->ngpuaddrs; i++)
		if (dword == ctx->gpuaddrs[i])
			return i;
	return -1;
}

static int find_pattern(uint32_t dword, int i, offsets_t offsets)
{
	int j, k;
	for (j = 0; j < ARRAY_SIZE(patterns); j++) {
		int found = 1;
		uint32_t pattern = patterns[j];
		for (k = 0; k < nctxts; k++) {
			uint32_t other_dword = ctxts[k].buf[i - offsets[k]];
			if ((dword & pattern) != (other_dword & pattern)) {
				found = 0;
				break;
			}
		}
		if (found)
			return j;
	}
	return -1;
}

static int find_rank(int i, offsets_t offsets)
{
	int j, k, rank = 0;
	uint32_t dword;

	/* check if we are past the end: */
	for (k = 0; k < nctxts; k++)
		if (i >= (ctxts[k].sz/ 4 + offsets[k]))
			return 0;

	dword = ctxts[0].buf[i - offsets[0]];

	j = find_gpuaddr(&ctxts[0], dword);
	if (j >= 0) {
		/* highest rank, if all are gpuaddr: */
		rank = ARRAY_SIZE(patterns);
		for (k = 0; k < nctxts; k++) {
			struct context *ctx = &ctxts[k];
			if (j != find_gpuaddr(ctx, ctx->buf[i - offsets[k]])) {
				rank = 0;
				break;
			}
		}
	} else {
		/* followed by pattern match.. in order of priority */
		/* TODO we need some fuzziness for partial match here.. */
		j = find_pattern(dword, i, offsets);
		if (j >= 0)
			rank = ARRAY_SIZE(patterns) - 1 - j;
	}

	return rank + find_rank(i + 1, offsets) / 2;
}

static int adjust_offsets_recursive(struct context *ctx, int i,
		offsets_t offsets, int n, int max_sz)
{
	int rank;

	rank = find_rank(i, offsets);

	if (n < nctxts) {
		int new_rank;
		offsets_t new_offsets;
		memcpy(new_offsets, offsets, sizeof(offsets_t));

		if ((ctxts[n].sz/4 + offsets[n]) < max_sz)
			new_offsets[n] += 1;
		new_rank = adjust_offsets_recursive(ctx, i, new_offsets, n+1, max_sz);
		if (new_rank > rank) {
			rank = new_rank;
			memcpy(offsets, new_offsets, sizeof(offsets_t));
		}
	}

	return rank;
}

static void adjust_offsets(struct context *ctx, int i, offsets_t offsets)
{
	int k;
	int max_sz = 0;

	for (k = 0; k < nctxts; k++)
		if (ctxts[k].sz > max_sz)
			max_sz = ctxts[k].sz;

	/* convert to dwords: */
	max_sz /= 4;

	adjust_offsets_recursive(ctx, i, offsets, 0, max_sz);
}

static void handle_hexdump(struct context *ctx)
{
	uint32_t *dwords = ctx->buf;
	int i, j, k;
	offsets_t offsets = {0};
	int offset = 0;
	int idx = -1;

	/* figure out idx: */
	for (k = 0; k < nctxts; k++) {
		if (&ctxts[k] == ctx) {
			idx = k;
			break;
		}
	}

	for (i = 0; i < ctx->sz/4; i++) {
		int found = 0;
		uint32_t dword;
		uint32_t pattern = 0;
		uint32_t known_pattern = 0;
		uint32_t known_pattern_color = 0;
		uint32_t pmasks[32];
		uint32_t pcolors[32];
		const char *pnames[32];
		int nparams = 0;

		/* adjust offsets for fuzzy matching: */
		adjust_offsets(ctx, i + offset, offsets);
		j = offsets[idx] - offset;
		while (j--)
			printf("<font face=\"monospace\" color=\"#000000\">........</font><br>");
		offset = offsets[idx];

		dword = dwords[i];

		/* check for gpu address: */
		j = find_gpuaddr(ctx, dword);
		if (j >= 0) {
			printf("<font face=\"monospace\">%04x: <font color=\"#%06x\"><b>%08x</b></font> (gpuaddr)</font><br>",
					i, gpuaddr_colors[j], dword);
			continue;
		}

		/* check for similarity with other ctxts: */
		j = find_pattern(dword, i + offset, offsets);
		if (j >= 0)
			pattern = patterns[j];

		/* check for known patterns: */
		for (j = 0; j < ARRAY_SIZE(known_patterns); j++) {
			if (known_patterns[j].val == (dword & known_patterns[j].mask)) {
				known_pattern = known_patterns[j].mask;
				known_pattern_color = known_patterns[j].color;
				break;
			}
		}

		/* check for recognized params: */
		if (!known_pattern) {
			for (j = 0; j < ctx->nparams; j++) {
				struct param *param = &ctx->params[j];
				int alignedlen = ALIGN(param->bitlen, 8);
				uint64_t m = (uint64_t)(1 << param->bitlen) - 1;
				uint32_t val = param->val;
				/* ignore param vals of zero, to easy for false match: */
				if (!val)
					continue;
				do {
					if ((dword & m) == val) {
						int n = nparams++;
						pmasks[n]  = m;
						pcolors[n] = param_colors[param->type];
						pnames[n]  = param_names[param->type];
						break;
					}
					m <<= alignedlen;
					val <<= alignedlen;
				} while (m & (uint64_t)0xffffffff);
			}
		}

		if (pattern || known_pattern || nparams) {
			uint32_t mask = 0xff000000;
			uint32_t shift = 24;

			printf("<font face=\"monospace\">%04x: ", i);

			for (k = 0; k < 4; k++, mask >>= 8, shift -= 8) {
				uint32_t color = 0;

				if (pattern & mask)
					color = 0x0000ff;

				if (known_pattern & mask)
					color = known_pattern_color;

				for (j = 0; j < nparams; j++) {
					if (mask & pmasks[j]) {
						color = pcolors[j];
						printf("<b>");
						break;
					}
				}

				printf("<font color=\"#%06x\">%02x</font>",
						color, (dword & mask) >> shift);

				for (j = 0; j < nparams; j++) {
					if (mask & pmasks[j]) {
						printf("</b>");
						break;
					}
				}
			}
			if (nparams > 0) {
				printf(" (");
				for (j = 0; j < nparams; j++) {
					if (j != 0)
						printf(", ");
					printf("%s", pnames[j]);
				}
				printf("?)");
			}
			printf("</font><br>");
			continue;
		}

		printf("<font face=\"monospace\" color=\"#000000\">%04x: %08x</font><br>", i, dword);
	}
}

static void handle_context(struct context *ctx)
{
	/* ignore for now */
}

static void handle_cmdstream(struct context *ctx)
{
	handle_hexdump(ctx);
}

static void handle_param(struct context *ctx)
{
	struct param *param = &ctx->params[ctx->nparams++];
	param->type   = ctx->buf[0];
	param->val    = ctx->buf[1];
	param->bitlen = ctx->buf[2];
	printf("%s<br>", param_names[param->type]);
	printf("<font color=\"#%06x\"><b>%08x</b></font><br>",
			param_colors[param->type], param->val);
	printf("(bitlen: %d)", param->bitlen);
	if (param->val >= (1 << param->bitlen)) {
		fprintf(stderr, "invalid param: %08x (name: %s, bitlen: %d)\n",
				param->val, param_names[param->type], param->bitlen);
	}
}

static void handle_flush(struct context *ctx)
{
	ctx->nparams = 0;
}

static void (*sect_handlers[])(struct context *ctx) = {
	[RD_TEST] = handle_string,
	[RD_CMD]  = handle_string,
	[RD_GPUADDR] = handle_gpuaddr,
	[RD_CONTEXT] = handle_context,
	[RD_CMDSTREAM] = handle_cmdstream,
	[RD_PARAM] = handle_param,
	[RD_FLUSH] = handle_flush,
};

static const char *sect_names[] = {
	[RD_TEST]      = "test",
	[RD_CMD]       = "cmd",
	[RD_GPUADDR]   = "gpuaddr",
	[RD_CONTEXT]   = "context",
	[RD_CMDSTREAM] = "cmdstream",
	[RD_PARAM]     = "param",
	[RD_FLUSH]     = "flush",
};

int main(int argc, char **argv)
{
	int i, n;

	for (i = 1; i < argc; i++) {
		struct context *ctx = &ctxts[nctxts++];
		ctx->fd = open(argv[i], O_RDONLY);
		if (ctx->fd < 0) {
			fprintf(stderr, "could not open: %s\n", argv[i]);
			return -1;
		}
	}

	printf("<html><body><table border=\"1\">\n");
	do {
		enum rd_sect_type row_type = RD_NONE;

		for (i = 0; i < nctxts; i++) {
			struct context *ctx = &ctxts[i];
			enum rd_sect_type type = RD_NONE;

			ctx->sz = 0;
			free(ctx->buf);
			ctx->buf = NULL;

			if ((read(ctx->fd, &type, sizeof(type)) > 0) &&
					(read(ctx->fd, &ctx->sz, 4) > 0)) {
				if (row_type == RD_NONE)
					row_type = type;

				if (type == row_type) {
					/* allocate  bit extra, because there could be some optional
					 * words in the cmdstreams, and they might not all be the
					 * same size..
					 */
					ctx->buf = calloc(1, ctx->sz + 1 + 20);
					read(ctx->fd, ctx->buf, ctx->sz);
					((char *)ctx->buf)[ctx->sz] = '\0';
				} else {
					fprintf(stderr, "unexpected type '%d', expected '%d'\n", type, row_type);
					return -1;
				}
			}

		}

		if (row_type == RD_NONE) {
			/* end of input? */
			break;
		}

		printf("<tr><th>%s</th>", sect_names[row_type]);

		for (i = 0, n = 0; i < nctxts; i++) {
			struct context *ctx = &ctxts[i];

			printf("<td>");
			if (ctx->sz > 0) {
				sect_handlers[row_type](ctx);
				n++;
			}
			printf("</td>");
		}

		printf("</tr>\n");
	} while(n > 0);
	printf("</table></body></html>\n");

	return 0;
}

