/*
 * Copyright 2011      Luc Verhaegen <libv@codethink.co.uk>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */
/*
 * Quick 'n Dirty bitmap dumper, created by poking at a pre-existing .bmp.
 *
 * TODO: this doesn't really handle 16bpp..
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define FILENAME_SIZE 1024

struct bmp_header {
	unsigned short magic;
	unsigned int size;
	unsigned int unused;
	unsigned int start;
} __attribute__((__packed__));

struct dib_header {
	unsigned int size;
	unsigned int width;
	unsigned int height;
	unsigned short planes;
	unsigned short bpp;
	unsigned int compression;
	unsigned int data_size;
	unsigned int h_res;
	unsigned int v_res;
	unsigned int colours;
	unsigned int important_colours;
	unsigned int red_mask;
	unsigned int green_mask;
	unsigned int blue_mask;
	unsigned int alpha_mask;
	unsigned int colour_space;
	unsigned int unused[12];
} __attribute__((__packed__));

static int
bmp_header_write(int fd, int width, int height)
{
	int ret;
	struct bmp_header bmp_header = {
		.magic = 0x4d42,
		.size = (width * height * 4) +
		sizeof(struct bmp_header) + sizeof(struct dib_header),
		.start = sizeof(struct bmp_header) + sizeof(struct dib_header),
	};
	struct dib_header dib_header = {
		.size = sizeof(struct dib_header),
		.width = width,
		.height = height,
		.planes = 1,
		.bpp = 32,
		.compression = 3,
		.data_size = 4 * width * height,
		.h_res = 0xB13,
		.v_res = 0xB13,
		.colours = 0,
		.important_colours = 0,
		.red_mask = 0x000000FF,
		.green_mask = 0x0000FF00,
		.blue_mask = 0x00FF0000,
		.alpha_mask = 0xFF000000,
		.colour_space = 0x57696E20,
	};

	ret = write(fd, &bmp_header, sizeof(struct bmp_header));
	if (ret < 0)
		return ret;

	ret = write(fd, &dib_header, sizeof(struct dib_header));
	if (ret < 0)
		return ret;

	return 0;
}

int
bmp_dump(char *buffer, int width, int height, int pitch, const char *filename)
{
	int fd, ret, i;

	fd = open(filename, O_WRONLY| O_TRUNC | O_CREAT, 0644);
	if (fd == -1) {
		printf("Failed to open %s\n", filename);
		return fd;
	}

	ret = bmp_header_write(fd, width, height);
	if (ret < 0) {
		printf("Error: %s\n", strerror(errno));
		return ret;
	}

	// TODO support for other than 32bpp..
	for (i = 0; i < height; i++) {
		char *ptr = buffer + (i * pitch);
		ret = write(fd, ptr, width * 4);
		if (ret < 0) {
			printf("Error: %s\n", strerror(errno));
			return ret;
		}
	}
	return 0;
}

