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

struct attribute {
	uint32_t unknown1;
	uint32_t reg;       /* seems to be the register the fetch instruction loads to */
	uint32_t const_idx; /* the CONST() indx value for sampler */
	uint32_t unknown2;
	uint32_t unknown3;
	uint32_t unknown4;
	uint32_t unknown5;
	char name[];
};

struct uniform {
	uint32_t unknown1;
	uint32_t unknown2;
	uint32_t unknown3;
	uint32_t unknown4;
	uint32_t const_base; /* const base register (for uniforms that take more than one const reg, ie. matrices) */
	uint32_t unknown6;
	uint32_t const_reg; /* the const register holding the value */
	uint32_t unknown7;
	uint32_t unknown8;
	uint32_t unknown9;
	char name[];
};

struct sampler {
	uint32_t unknown1;
	uint32_t unknown2;
	uint32_t unknown3;
	uint32_t unknown4;
	uint32_t unknown5;
	uint32_t unknown6;
	uint32_t const_idx; /* the CONST() indx value for the sampler */
	uint32_t unknown7;
	char name[];
};

struct varying {
	uint32_t unknown1;
	uint32_t unknown2;
	uint32_t unknown3;
	uint32_t reg;       /* the register holding the value (on entry to the shader) */
	char name[];
};

struct constant {
	uint32_t unknown1;
	uint32_t unknown2;
	uint32_t unknown3;
	uint32_t const_idx;
	float val[];
};

static int full_dump = 1;

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

static void clean_ascii(char *buf, int sz)
{
	uint8_t *ptr = (uint8_t *)buf;
	uint8_t *end = ptr + sz;
	while (ptr < end) {
		*(ptr++) ^= 0xff;
	}
}

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

static void dump_attribute(struct attribute *attrib)
{
	printf("\tR%d, CONST(%d): %s\n", attrib->reg,
			attrib->const_idx, attrib->name);
}

static void dump_uniform(struct uniform *uniform)
{
	if (uniform->const_reg == -1) {
		printf("\tC%d+: %s\n", uniform->const_base, uniform->name);
	} else {
		printf("\tC%d: %s\n", uniform->const_reg, uniform->name);
	}
}

static void dump_sampler(struct sampler *sampler)
{
	printf("\tCONST(%d): %s\n", sampler->const_idx, sampler->name);
}

static void dump_varying(struct varying *varying)
{
	printf("\tR%d: %s\n", varying->reg, varying->name);
}

static void dump_constant(struct constant *constant)
{
	printf("\tC%d: %f, %f, %f, %f\n", constant->const_idx,
			constant->val[0], constant->val[1],
			constant->val[2], constant->val[3]);
}

void dump_program(char *buf, int sz)
{
	struct pgm_header *hdr;
	struct attribute *attribs[32];  /* don't really know the upper limit.. */
	struct uniform *uniforms[32];
	struct sampler *samplers[32];
	struct varying *varyings[32];
	int i, sect_size;
	uint8_t *ptr;

	hdr = next_sect(&buf, &sz, &sect_size);

	printf("######## HEADER: (size %d)\n", sect_size);
	printf("\tsize:       %d\n", hdr->size);
	printf("\tattributes: %d\n", hdr->num_attribs);
	printf("\tuniforms:   %d\n", hdr->num_uniforms);
	printf("\tsamplers:   %d\n", hdr->num_samplers);
	printf("\tvaryings:   %d\n", hdr->num_varyings);
	if (full_dump)
		dump_hex((void *)hdr, sect_size);
	printf("\n");

	/* there seems to be two 0xba5eba11's at the end of the header: */
	sz  -= 4;
	buf += 4;

	for (i = 0; (i < hdr->num_attribs) && (sz > 0); i++) {
		attribs[i] = next_sect(&buf, &sz, &sect_size);
		clean_ascii(attribs[i]->name, sect_size - 28);
		if (full_dump) {
			printf("######## ATTRIBUTE: (size %d)\n", sect_size);
			dump_attribute(attribs[i]);
			dump_hex((char *)attribs[i], sect_size);
		}
	}

	for (i = 0; (i < hdr->num_uniforms) && (sz > 0); i++) {
		uniforms[i] = next_sect(&buf, &sz, &sect_size);
		clean_ascii(uniforms[i]->name, sect_size - 41);
		if (full_dump) {
			printf("######## UNIFORM: (size %d)\n", sect_size);
			dump_uniform(uniforms[i]);
			dump_hex((char *)uniforms[i], sect_size);
		}
	}

	for (i = 0; (i < hdr->num_samplers) && (sz > 0); i++) {
		samplers[i] = next_sect(&buf, &sz, &sect_size);
		clean_ascii(samplers[i]->name, sect_size - 33);
		if (full_dump) {
			printf("######## SAMPLER: (size %d)\n", sect_size);
			dump_sampler(samplers[i]);
			dump_hex((char *)samplers[i], sect_size);
		}
	}

	for (i = 0; (i < hdr->num_varyings) && (sz > 0); i++) {
		varyings[i] = next_sect(&buf, &sz, &sect_size);
		clean_ascii(varyings[i]->name, sect_size - 16);
		if (full_dump) {
			printf("######## VARYING: (size %d)\n", sect_size);
			dump_varying(varyings[i]);
			dump_hex((char *)varyings[i], sect_size);
		}
	}

	/* dump vertex shaders: */
	for (i = 0; i < 3; i++) {
		struct vs_header *vs_hdr = next_sect(&buf, &sz, &sect_size);
		struct constant *constants[32];
		int j, level = 0;

		printf("\n");

		if (full_dump) {
			printf("#######################################################\n");
			printf("######## VS%d HEADER: (size %d)\n", i, sect_size);
			dump_hex((void *)vs_hdr, sect_size);
		}

		for (j = 0; j < vs_hdr->unknown1 - 1; j++) {
			constants[j] = next_sect(&buf, &sz, &sect_size);
			if (full_dump) {
				printf("######## VS%d CONST: (size=%d)\n", i, sect_size);
				dump_constant(constants[j]);
				dump_hex((char *)constants[j], sect_size);
			}
		}

		ptr = next_sect(&buf, &sz, &sect_size);
		printf("######## VS%d SHADER: (size=%d)\n", i, sect_size);
		if (full_dump) {
			dump_hex(ptr, sect_size);
			level = 1;
		} else {
			/* dump attr/uniform/sampler/varying/const summary: */
			for (j = 0; j < hdr->num_varyings; j++) {
				dump_varying(varyings[j]);
			}
			for (j = 0; j < hdr->num_attribs; j++) {
				dump_attribute(attribs[j]);
			}
			for (j = 0; j < hdr->num_uniforms; j++) {
				dump_uniform(uniforms[j]);
			}
			for (j = 0; j < hdr->num_samplers; j++) {
				dump_sampler(samplers[j]);
			}
			for (j = 0; j < vs_hdr->unknown1 - 1; j++) {
				if (constants[j]->unknown2 == 0) {
					dump_constant(constants[j]);
				}
			}
			printf("\n");
		}
		disasm((uint32_t *)(ptr + 32), (sect_size - 32) / 4, level, SHADER_VERTEX);
		free(ptr);

		for (j = 0; j < vs_hdr->unknown9; j++) {
			ptr = next_sect(&buf, &sz, &sect_size);
			if (full_dump) {
				printf("######## VS%d CONST?: (size=%d)\n", i, sect_size);
				dump_hex(ptr, sect_size);
			}
			free(ptr);
		}

		for (j = 0; j < vs_hdr->unknown1 - 1; j++) {
			free(constants[j]);
		}

		free(vs_hdr);
	}

	/* dump fragment shaders: */
	for (i = 0; i < 1; i++) {
		struct fs_header *fs_hdr = next_sect(&buf, &sz, &sect_size);
		struct constant *constants[32];
		int j, level = 0;

		printf("\n");

		if (full_dump) {
			printf("#######################################################\n");
			printf("######## FS%d HEADER: (size %d)\n", i, sect_size);
			dump_hex((void *)fs_hdr, sect_size);
		}

		for (j = 0; j < fs_hdr->unknown1 - 1; j++) {
			constants[j] = next_sect(&buf, &sz, &sect_size);
			if (full_dump) {
				printf("######## FS%d CONST: (size=%d)\n", i, sect_size);
				dump_constant(constants[j]);
				dump_hex((char *)constants[j], sect_size);
			}
		}

		ptr = next_sect(&buf, &sz, &sect_size);
		printf("######## FS%d SHADER: (size=%d)\n", i, sect_size);
		if (full_dump) {
			dump_hex(ptr, sect_size);
			level = 1;
		} else {
			/* dump attr/uniform/sampler/varying/const summary: */
			for (j = 0; j < hdr->num_varyings; j++) {
				dump_varying(varyings[j]);
			}
			for (j = 0; j < hdr->num_attribs; j++) {
				dump_attribute(attribs[j]);
			}
			for (j = 0; j < hdr->num_uniforms; j++) {
				dump_uniform(uniforms[j]);
			}
			for (j = 0; j < hdr->num_samplers; j++) {
				dump_sampler(samplers[j]);
			}
			for (j = 0; j < fs_hdr->unknown1 - 1; j++) {
				// seems unknown2==1 means compiler internal const..
				// seems to start counting again from C0, but I guess
				// the number should be added to the last non-internal
				// const??  Well, that is a theory..
				if (constants[j]->unknown2 == 0) {
					dump_constant(constants[j]);
				}
			}
			printf("\n");
		}
		disasm((uint32_t *)(ptr + 32), (sect_size - 32) / 4, level, SHADER_FRAGMENT);
		free(ptr);

		for (j = 0; j < fs_hdr->unknown1 - 1; j++) {
			free(constants[j]);
		}

		free(fs_hdr);
	}

	if (!full_dump)
		return;

	/* dump ascii version of shader program: */
	ptr = next_sect(&buf, &sz, &sect_size);
	printf("\n#######################################################\n");
	printf("######## SHADER SRC: (size=%d)\n", sect_size);
	dump_ascii(ptr, sect_size);
	free(ptr);

	/* dump remaining sections (there shouldn't be any): */
	while (sz > 0) {
		ptr = next_sect(&buf, &sz, &sect_size);
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
	const char *infile;
	int fd, sz, i, raw = 0;

	/* lame argument parsing: */
	if ((argc > 1) && !strcmp(argv[1], "--verbose")) {
		disasm_set_debug(PRINT_RAW | PRINT_UNKNOWN);
		argv++;
		argc--;
	}
	if ((argc > 1) && !strcmp(argv[1], "--raw")) {
		raw = 1;
		argv++;
		argc--;
	}
	if ((argc == 3) && !strcmp(argv[1], "--short")) {
		/* only short dump, original shader, symbol table, and disassembly */
		full_dump = 0;
		argv++;
		argc--;
	}

	if (argc != 2) {
		fprintf(stderr, "usage: pgmdump [--verbose] [--raw] [--short] testlog.rd\n");
		return -1;
	}

	infile = argv[1];

	fd = open(infile, O_RDONLY);
	if (fd < 0)
		fprintf(stderr, "could not open: %s\n", argv[1]);

	if (raw) {
		enum shader_t shader = 0;
		if (!strcmp(infile + strlen(infile) - 3, ".vo"))
			shader = SHADER_VERTEX;
		else if (!strcmp(infile + strlen(infile) - 3, ".fo"))
			shader = SHADER_FRAGMENT;
		buf = calloc(1, 100 * 1024);
		read(fd, buf, 100 * 1024);
		return disasm(buf, 100 * 1024, 0, shader);
	}

	while ((read(fd, &type, sizeof(type)) > 0) && (read(fd, &sz, 4) > 0)) {
		free(buf);

		/* note: allow hex dumps to go a bit past the end of the buffer..
		 * might see some garbage, but better than missing the last few bytes..
		 */
		buf = calloc(1, sz + 3);
		read(fd, buf, sz);

		switch(type) {
		case RD_TEST:
			if (full_dump)
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

