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
#include <errno.h>

#include "ir-a3xx.h"
#include "util.h"

int main(int argc, char **argv)
{
	struct ir3_shader *shader;
	struct ir3_shader_info info;
	static char src[256 * 1024];
	static uint32_t dwords[64 * 1024];
	static int sizedwords;
	char *infile, *outfile;
	int fd, ret;

	if (argc != 3) {
		ERROR_MSG("usage: %s [infile] [outfile]", argv[0]);
		return -1;
	}

	infile = argv[1];
	outfile = argv[2];

	fd = open(infile, O_RDONLY);
	if (fd < 0) {
		ERROR_MSG("could not open '%s': %s", infile, strerror(errno));
		return -1;
	}

	ret = read(fd, src, sizeof(src) - 1);
	if (ret <= 0) {
		ERROR_MSG("could not read '%s': %s", infile, strerror(errno));
		return -1;
	}
	printf("parsing:\n%s\n", src);

	close(fd);

	shader = fd_asm_parse(src);
	if (!shader) {
		ERROR_MSG("parse failed");
		return -1;
	}

	sizedwords = ir3_shader_assemble(shader, dwords, ARRAY_SIZE(dwords), &info);
	if (sizedwords <= 0) {
		ERROR_MSG("assembler failed");
		return -1;
	}

	fd = open(outfile, O_WRONLY| O_TRUNC | O_CREAT, 0644);
	if (fd < 0) {
		ERROR_MSG("could not open '%s': %s", outfile, strerror(errno));
		return -1;
	}

	ret = write(fd, dwords, sizedwords * 4);
	if (ret <= 0) {
		ERROR_MSG("could not write '%s': %s", outfile, strerror(errno));
		return -1;
	}

	ir3_shader_destroy(shader);

	return 0;
}
