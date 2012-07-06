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
#include "disasm.h"

struct pgm_header {
	uint32_t size;
	uint32_t unknown1;
	uint32_t unknown2;
	uint32_t unknown3;
	uint32_t unknown4;
	uint32_t unknown5;
	uint32_t unknown6;
	uint32_t unknown7;
	uint32_t unknown8;
	uint32_t num_attribs;
	uint32_t num_uniforms;
	uint32_t num_samplers;
	uint32_t num_varyings;
};

struct vs_header {
	uint32_t unknown1;  /* seems to be # of sections up to and including shader */
	uint32_t unknown2;  /* seems to be low byte or so of SQ_PROGRAM_CNTL */
	uint32_t unknown3;
	uint32_t unknown4;
	uint32_t unknown5;
	uint32_t unknown6;
	uint32_t unknown7;
	uint32_t unknown8;
	uint32_t unknown9;  /* seems to be # of sections following shader */
};

struct fs_header {
	uint32_t unknown1;
};

char *find_sect_end(char *buf, int sz)
{
	uint8_t *ptr = (uint8_t *)buf;
	uint8_t *end = ptr + sz - 3;

	while (ptr < end) {
		uint32_t d = 0;

		d |= ptr[0] <<  0;
		d |= ptr[1] <<  8;
		d |= ptr[2] << 16;
		d |= ptr[3] << 24;

		/* someone at QC likes baseball */
		if (d == 0xba5eba11)
			return ptr;

		ptr++;
	}
	return NULL;
}

/* convert float to dword */
static inline float d2f(uint32_t d)
{
	union {
		float f;
		uint32_t d;
	} u = {
		.d = d,
	};
	return u.f;
}

static void dump_hex(char *buf, int sz)
{
	uint8_t *ptr = (uint8_t *)buf;
	uint8_t *end = ptr + sz;
	int i = 0;

	while (ptr < end) {
		uint32_t d = 0;

		printf((i % 8) ? " " : "\t");

		d |= *(ptr++) <<  0;
		d |= *(ptr++) <<  8;
		d |= *(ptr++) << 16;
		d |= *(ptr++) << 24;

		printf("%08x", d);

		if ((i % 8) == 7) {
			printf("\n");
		}

		i++;
	}

	if (i % 8) {
		printf("\n");
	}
}

static void dump_float(char *buf, int sz)
{
	uint8_t *ptr = (uint8_t *)buf;
	uint8_t *end = ptr + sz - 3;
	int i = 0;

	while (ptr < end) {
		uint32_t d = 0;

		printf((i % 8) ? " " : "\t");

		d |= *(ptr++) <<  0;
		d |= *(ptr++) <<  8;
		d |= *(ptr++) << 16;
		d |= *(ptr++) << 24;

		printf("%8f", d2f(d));

		if ((i % 8) == 7) {
			printf("\n");
		}

		i++;
	}

	if (i % 8) {
		printf("\n");
	}
}

#define is_ok_ascii(c) \
	(isascii(c) && ((c == '\t') || !iscntrl(c)))

static void dump_ascii(char *buf, int sz)
{
	uint8_t *ptr = (uint8_t *)buf;
	uint8_t *end = ptr + sz;
	printf("\t");
	while (ptr < end) {
		uint8_t c = *(ptr++) ^ 0xff;
		if (c == '\n') {
			printf("\n\t");
		} else if (c == '\0') {
			printf("\n\t-----------------------------------\n\t");
		} else if (is_ok_ascii(c)) {
			printf("%c", c);
		} else {
			printf("?");
		}
	}
	printf("\n");
}

void *next_sect(char **buf, int *sz, int *sect_size)
{
	char *end = find_sect_end(*buf, *sz);
	void *sect;

	*sect_size = end - *buf;

	/* copy the section to keep things nicely 32b aligned: */
	sect = malloc(ALIGN(*sect_size, 4));
	memcpy(sect, *buf, *sect_size);

	*sz -= *sect_size + 4;
	*buf = end + 4;

	return sect;
}

void dump_program(char *buf, int sz)
{
	struct pgm_header *hdr;
	int i, sect_size;
	uint8_t *ptr;

	hdr = next_sect(&buf, &sz, &sect_size);

	printf("######## HEADER: (size %d)\n", sect_size);
	printf("\tsize:       %d\n", hdr->size);
	printf("\tattributes: %d\n", hdr->num_attribs);
	printf("\tuniforms:   %d\n", hdr->num_uniforms);
	printf("\tsamplers:   %d\n", hdr->num_samplers);
	printf("\tvaryings:   %d\n", hdr->num_varyings);
	printf("as hex:\n");
	dump_hex((void *)hdr, sect_size);

	/* there seems to be two 0xba5eba11's at the end of the header: */
	sz  -= 4;
	buf += 4;

	for (i = 0; (i < hdr->num_attribs) && (sz > 0); i++) {
		ptr = next_sect(&buf, &sz, &sect_size);
		printf("######## ATTRIBUTE: (size %d)\n", sect_size);
		dump_ascii(ptr + 28, sect_size - 29);
		printf("as hex:\n");
		dump_hex(ptr, sect_size);
		free(ptr);
	}

	for (i = 0; (i < hdr->num_uniforms) && (sz > 0); i++) {
		ptr = next_sect(&buf, &sz, &sect_size);
		printf("######## UNIFORM: (size %d)\n", sect_size);
		dump_ascii(ptr + 40, sect_size - 42);
		printf("as hex:\n");
		dump_hex(ptr, sect_size);
		free(ptr);
	}

	for (i = 0; (i < hdr->num_samplers) && (sz > 0); i++) {
		ptr = next_sect(&buf, &sz, &sect_size);
		printf("######## SAMPLER: (size %d)\n", sect_size);
		dump_ascii(ptr + 32, sect_size - 34);
		printf("as hex:\n");
		dump_hex(ptr, sect_size);
		free(ptr);
	}

	for (i = 0; (i < hdr->num_varyings) && (sz > 0); i++) {
		ptr = next_sect(&buf, &sz, &sect_size);
		printf("######## VARYING: (size %d)\n", sect_size);
		dump_ascii(ptr + 16, sect_size - 17);
		printf("as hex:\n");
		dump_hex(ptr, sect_size);
		free(ptr);
	}

	/* dump vertex shaders: */
	for (i = 0; i < 3; i++) {
		struct vs_header *vs_hdr = next_sect(&buf, &sz, &sect_size);
		int j;

		printf("\n#######################################################\n");
		printf("######## VS%d HEADER: (size %d)\n", i, sect_size);
		printf("as hex:\n");
		dump_hex((void *)vs_hdr, sect_size);

		for (j = 0; j < vs_hdr->unknown1 - 1; j++) {
			ptr = next_sect(&buf, &sz, &sect_size);
			printf("######## VS%d CONST: (size=%d)\n", i, sect_size);
			printf("as hex:\n");
			dump_hex(ptr, sect_size);
			printf("as float:\n");
			dump_float(ptr, sect_size);
			free(ptr);
		}

		ptr = next_sect(&buf, &sz, &sect_size);
		printf("######## VS%d SHADER: (size=%d)\n", i, sect_size);
		dump_hex(ptr, sect_size);
		disasm((uint32_t *)(ptr + 32), (sect_size - 32) / 4, 1);
		free(ptr);

		for (j = 0; j < vs_hdr->unknown9; j++) {
			ptr = next_sect(&buf, &sz, &sect_size);
			printf("######## VS%d CONST?: (size=%d)\n", i, sect_size);
			printf("as hex:\n");
			dump_hex(ptr, sect_size);
			free(ptr);
		}

		free(vs_hdr);
	}

	/* dump fragment shaders: */
	for (i = 0; i < 1; i++) {
		struct fs_header *fs_hdr = next_sect(&buf, &sz, &sect_size);
		int j;

		printf("\n#######################################################\n");
		printf("######## FS%d HEADER: (size %d)\n", i, sect_size);
		printf("as hex:\n");
		dump_hex((void *)fs_hdr, sect_size);

		for (j = 0; j < fs_hdr->unknown1 - 1; j++) {
			ptr = next_sect(&buf, &sz, &sect_size);
			printf("######## FS%d CONST: (size=%d)\n", i, sect_size);
			printf("as hex:\n");
			dump_hex(ptr, sect_size);
			printf("as float:\n");
			dump_float(ptr, sect_size);
			free(ptr);
		}

		ptr = next_sect(&buf, &sz, &sect_size);
		printf("######## FS%d SHADER: (size=%d)\n", i, sect_size);
		dump_hex(ptr, sect_size);
		disasm((uint32_t *)(ptr + 32), (sect_size - 32) / 4, 1);
		free(ptr);

		free(fs_hdr);
	}

	/* dump ascii version of shader program: */
	ptr = next_sect(&buf, &sz, &sect_size);
	printf("\n#######################################################\n");
	printf("######## SHADER SRC: (size=%d)\n", sect_size);
	dump_ascii(ptr, sect_size);

	/* dump remaining sections (there shouldn't be any): */
	while (sz > 0) {
		uint8_t *ptr = next_sect(&buf, &sz, &sect_size);
		printf("######## section (size=%d)\n", sect_size);
		printf("as hex:\n");
		dump_hex(ptr, sect_size);
		printf("as float:\n");
		dump_float(ptr, sect_size);
		printf("as ascii:\n");
		dump_ascii(ptr, sect_size);
		free(ptr);
	}
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

		/* note: allow hex dumps to go a bit past the end of the buffer..
		 * might see some garbage, but better than missing the last few bytes..
		 */
		buf = calloc(1, sz + 3);
		read(fd, buf, sz);

		switch(type) {
		case RD_TEST:
			printf("test: %s\n", (char *)buf);
			break;
		case RD_VERT_SHADER:
			printf("vertex shader:\n%s\n", (char *)buf);
			break;
		case RD_FRAG_SHADER:
			printf("fragment shader:\n%s\n", (char *)buf);
			break;
		case RD_PROGRAM:
			printf("############################################################\n");
			printf("program:\n");
			dump_program(buf, sz);
			printf("############################################################\n");
			break;
		}
	}

	return 0;
}

