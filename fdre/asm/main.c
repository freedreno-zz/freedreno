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

void fd_asm_parse(const char *src);

int main(int argc, char **argv)
{
	static char src[256 * 1024];
	char *infile;
	int fd, ret;

	if (argc != 2) {
		fprintf(stderr, "usage: %s [infile]\n", argv[0]);
		return -1;
	}

	infile = argv[1];
	fd = open(infile, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "could not open '%s': %s\n",
				infile, strerror(errno));
		return -1;
	}

	ret = read(fd, src, sizeof(src) - 1);
	if (ret <= 0) {
		fprintf(stderr, "could not read '%s': %s\n",
				infile, strerror(errno));
		return -1;
	}
	printf("parsing:\n%s\n", src);

	fd_asm_parse(src);

	return 0;
}

