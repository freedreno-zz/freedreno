/*
 * Copyright © 2012 Rob Clark
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

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

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

static const uint32_t gpuaddr_colors[] = {
		0x00ff0000,
		0x0000ff00,
		0x000000ff,
		0x00cc0000,
		0x0000cc00,
		0x000000cc,
};

struct context {
	int       fd;
	uint32_t *buf;           /* current row buffer */
	int       sz;            /* current row buffer size */
	uint32_t  gpuaddrs[32];
	int       ngpuaddrs;
};

struct context ctxts[32];
int nctxts;
typedef int offsets_t[ARRAY_SIZE(ctxts)];

static void handle_string(struct context *ctx)
{
	printf("%s", (char *)ctx->buf);
}

static void handle_gpuaddr(struct context *ctx)
{
	uint32_t gpuaddr = *ctx->buf;
	printf("<font color=\"#%06x\"><b>%08x</b></font>",
			gpuaddr_colors[ctx->ngpuaddrs], gpuaddr);
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
		j = find_pattern(dword, i, offsets);
		if (j >= 0)
			rank = ARRAY_SIZE(patterns) - 1 - j;
	}

// maybe we should include the next couple dwords in the search.. although
// probably don't want to include up until the end because later skipped
// dwords would probably throw things off..
	return rank + find_rank(i + 1, offsets) / 2;
//	return rank;  // ???
}

static void adjust_offsets(struct context *ctx, int i, offsets_t offsets)
{
	int k;
	int max_sz = 0, rank, new_rank;
	offsets_t new_offsets;

	for (k = 0; k < nctxts; k++)
		if (ctxts[k].sz > max_sz)
			max_sz = ctxts[k].sz;

	rank = find_rank(i, offsets);

	/* see if we can achieve a better rank by inserting a skipped dword..
	 * so far I don't see more than a single optional dword in sequence,
	 * but if there is possibility for more then I might need to adjust
	 * this:
	 */
	memcpy(new_offsets, offsets, sizeof(offsets_t));
	for (k = 0; k < nctxts; k++) {
fprintf(stderr, "%d: sz=%d, off=%d, max=%d\n", k, ctxts[k].sz, offsets[k], max_sz);
		if ((ctxts[k].sz/4 + offsets[k]) < max_sz/4) {
			new_offsets[k] += 1;
			new_rank = find_rank(i, new_offsets);
fprintf(stderr, "%08x: new_rank=%d, rank=%d\n", ctx->buf[i], new_rank, rank);
			if (new_rank > rank) {
fprintf(stderr, "keep it!\n");
				/* keep this */
				rank = new_rank;
				memcpy(offsets, new_offsets, sizeof(offsets_t));
			} else {
				/* discard this */
				new_offsets[k] -= 1;
			}
		}
	}
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
			printf("<font face=\"monospace\" color=\"#%06x\"><b>%08x</b></font><br>",
					gpuaddr_colors[j], dword);
			continue;
		}

		/* check for known/common patterns: */
		// TODO

		/* check for similarity with other ctxts: */
		j = find_pattern(dword, i + offset, offsets);
		if (j >= 0) {
			uint32_t mask = 0xff000000;
			uint32_t shift = 24;
			for (k = 0; k < 4; k++, mask >>= 8, shift -= 8) {
				uint32_t color = (patterns[j] & mask) ? 0x0000ff : 0x000000;
				printf("<font face=\"monospace\" color=\"#%06x\">%02x</font>",
						color, (dword & mask) >> shift);
			}
			printf("<br>");
			continue;
		}

		printf("<font face=\"monospace\" color=\"#000000\">%08x</font><br>", dword);
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

static void (*sect_handlers[])(struct context *ctx) = {
	[RD_TEST] = handle_string,
	[RD_CMD]  = handle_string,
	[RD_GPUADDR] = handle_gpuaddr,
	[RD_CONTEXT] = handle_context,
	[RD_CMDSTREAM] = handle_cmdstream,
};

static const char *sect_names[] = {
		[RD_TEST]      = "test",
		[RD_CMD]       = "cmd",
		[RD_GPUADDR]   = "gpuaddr",
		[RD_CONTEXT]   = "context",
		[RD_CMDSTREAM] = "cmdstream",
};

int main(int argc, char **argv)
{
	int i, n;

	for (i = 1; i < argc; i++) {
		struct context *ctx = &ctxts[nctxts++];
		ctx->fd = open(argv[i], O_RDONLY);
		if (ctx->fd < 0) {
			printf("could not open: %s\n", argv[i]);
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
					printf("unexpected type '%d', expected '%d'\n", type, row_type);
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

