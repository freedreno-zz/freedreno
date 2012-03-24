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
		0xffffffff,
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
	int   fd;
	void *buf;          /* current row buffer */
	int   sz;           /* current row buffer size */
	uint32_t gpuaddrs[32];
	int   ngpuaddrs;
};

struct context ctxts[32];
int nctxts;

static void handle_string(struct context *ctx)
{
	printf("%s", (char *)ctx->buf);
}

static void handle_gpuaddr(struct context *ctx)
{
	uint32_t gpuaddr = *(uint32_t *)(ctx->buf);
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

static int find_pattern(struct context *ctx, uint32_t dword, int i)
{
	int j, k;
	for (j = 0; j < ARRAY_SIZE(patterns); j++) {
		int found = 1;
		uint32_t pattern = patterns[j];
		for (k = 0; k < nctxts; k++) {
			uint32_t other_dword = ((uint32_t *)ctxts[k].buf)[i];
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

static void handle_hexdump(struct context *ctx)
{
	uint32_t *dwords = ctx->buf;
	int i, j, k;

	for (i = 0; i < ctx->sz/4; i++) {
		int found = 0;
		uint32_t dword = dwords[i];

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
		j = find_pattern(ctx, dword, i);
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
					ctx->buf = malloc(ctx->sz + 1);
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

