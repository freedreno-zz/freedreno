/*
 * Copyright (c) 2013 Rob Clark <robdclark@gmail.com>
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

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* note: we have to do this, instead of use stdio.h, because glibc
 * stdio.h plays some games with redirecting sscanf which doesn't
 * work with bionic libc
 */
int sscanf(const char *str, const char *format, ...);
int printf(const char *format, ...);
int sprintf(char *str, const char *format, ...);
/* *********** */
struct cl_compiler {
	uint32_t unknown[32];
};

struct cl_binary {
	uint32_t size_bytes;
	uint32_t *dwords;
};

struct cl_program {
	uint32_t unknown1[7];
	struct cl_binary *binary;
	uint32_t unknown2[24];
};

struct cl_ddl {
	uint32_t unknown[32];
};

struct cl_object {
	uint32_t unknown[32];
};

struct cl_compiler * cl_create_compiler(void);
struct cl_program * cl_linked_program_from_source(struct cl_compiler *compiler,
		const char *src, int sz, int unknown1, char *opts, int opts_len);
struct cl_ddl * cl_get_linked_program_ddl(struct cl_program *program);
int cl_get_linked_program_target(struct cl_program *program);
struct cl_object * cl_get_linked_program_object(struct cl_program *program);
int cl_get_assembly_from_linked_program(struct cl_program *program,
		char **str, int *count);
/* *********** */

static char * readline(int fd)
{
	static char buf[256];
	char *line = buf;
	while(1) {
		int ret = read(fd, line, 1);
		if (ret != 1)
			return 0;
		if (*line == '\n')
			break;
		line++;
	}
	*(++line) = '\0';
	return buf;
}

static char * readfile(const char *path)
{
	static char src[64 * 1024];
	int fd, ret;

	memset(src, 0, sizeof(src));

	fd = open(path, 0);
	if (fd < 0)
		return NULL;

	ret = read(fd, src, sizeof(src));
	if (ret < 0)
		return NULL;

	return src;
}

/* note: we know what we are searching for is dword aligned,
 * so we get to simplify a bit based on that..
 */
static uint32_t * scan_memory(uint32_t *start, uint32_t *end,
		uint32_t *buf, int sizedwords)
{
	int i;
	while (start < end) {
		if (&start[sizedwords-1] >= end)
			return NULL;
		for (i = 0; (i < sizedwords) && (start[i] == buf[i]); i++) {}
		if (i == sizedwords)
			return start;
		start++;
	}
	return NULL;
}

/* Feed in some pre-compiled shader to disassemble, rather than
 * what was compiled from the ocl src.  This way we can more
 * easily disassemble shaders from the glsl compiler (for ex,
 * extracted from cmdstream).
 *
 * But this is less straightforward than you'd expect.  There
 * are two copies of the raw shader in memory.  But the one at
 * program->binary->dwords is not the one the disassembler uses
 * and I haven't found yet the pointer to the other copy.  So
 * instead we have to search the heap for the 2nd copy, and
 * overwrite it.  This probably isn't quite perfect.. you probably
 * want to make sure the program you feed in is smaller than the
 * ocl program that would otherwise you will be truncated to avoid
 * possible buffer overruns..  If the program is smaller, it will
 * be padded with nop's.
 */
static void poke_disasm(const char *disasm, struct cl_program *program)
{
	int fd;
	char *line;
	uint32_t *dwords;
	uint32_t heap_start = 0, heap_end = 0;

	/* first we need to find the bounds of the heap: */
	fd = open("/proc/self/maps", 0);
	if (fd < 0)
		return;
	while ((line = readline(fd))) {
		/* looking for something like:
		 * 00011000-002a8000 rw-p 00000000 00:00 0          [heap]
		 */
		if (strstr(line, "[heap]")) {
			sscanf(line, "%08x-%08x", &heap_start, &heap_end);
			break;
		}
	}

	if (!(heap_start && heap_end))
		return;

	/* now search for 2nd copy of shader: */
	dwords = scan_memory((uint32_t *)heap_start, (uint32_t *)heap_end,
			program->binary->dwords, program->binary->size_bytes/4);

	/* if we found our own copy, keep looking: */
	if (dwords == program->binary->dwords)
		dwords = scan_memory(&dwords[1], (uint32_t *)heap_end,
				program->binary->dwords, program->binary->size_bytes/4);

	if (!dwords)
		return;

	printf("found original shader in memory, overwriting with our own!");

	memcpy(dwords, readfile(disasm), program->binary->size_bytes);
}

int main(int argc, char **argv)
{
	struct cl_compiler *compiler;
	struct cl_program *program;
	struct cl_ddl *ddl;
	int dump_shaders = 0, count = 0, opts_len = 0;
	struct cl_object *object;
	char *src, *infile, *opts = NULL, *disasm;
	char *assembly[20];

	/* lame argument parsing: */
	if ((argc > 1) && !strcmp(argv[1], "--dump-shaders")) {
		dump_shaders = 1;
		argv++;
		argc--;
	}

	if ((argc > 2) && !strcmp(argv[1], "--opts")) {
		opts = argv[2];
		opts_len = strlen(opts) + 1;
		argv += 2;
		argc -= 2;
	}

	if ((argc > 2) && !strcmp(argv[1], "--disasm")) {
		disasm = argv[2];
		argv += 2;
		argc -= 2;
	}

	if (argc != 2) {
		printf("usage: cltool [--dump-shaders] [--opts options-string] [--disasm rawfile.co3] testkernel.cl\n");
		return -1;
	}

	infile = argv[1];

	src = readfile(infile);
	if (!src)
		return -1;

	/* don't bother showing the dummy ocl program we are compiling
	 * if we are going to overwrite it with our own shader to
	 * disassemble..
	 */
	if (!disasm)
		printf("== Compiling Kernel: ==\n%s\n", src);

	compiler = cl_create_compiler();
	program = cl_linked_program_from_source(compiler, src, strlen(src)+1, 1,
			opts, opts_len);

	if (disasm)
		poke_disasm(disasm, program);

	cl_get_assembly_from_linked_program(program, assembly, &count);
	printf("%s\n", assembly[0]);

	if (dump_shaders) {
		static char filename[256];
		int fd;

		sprintf(filename, "%.*s.co3", strlen(infile) - 3, infile);
#define O_WRONLY	     01
#define O_TRUNC		  01000	/* not fcntl */
#define O_CREAT		   0100	/* not fcntl */
		fd = open(filename, O_WRONLY| O_TRUNC | O_CREAT, 0644);
		write(fd, program->binary->dwords, program->binary->size_bytes);
	}

	return 0;
}

