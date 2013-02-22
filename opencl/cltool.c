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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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

char * readfile(const char *path)
{
	static char src[64 * 1024];
	int fd, ret;

	fd = open(path, 0);
	if (fd < 0)
		return NULL;

	ret = read(fd, src, sizeof(src));
	if (ret < 0)
		return NULL;

	return src;
}


int main(int argc, char **argv)
{
	struct cl_compiler *compiler;
	struct cl_program *program;
	struct cl_ddl *ddl;
	int dump_shaders = 0, count = 0, opts_len = 0;
	struct cl_object *object;
	char *src, *infile, *opts = NULL;
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

	if (argc != 2) {
		printf("usage: cltool [--dump-shaders] [--opts options-string] testkernel.cl\n");
		return -1;
	}

	infile = argv[1];

	src = readfile(infile);
	if (!src)
		return -1;

	printf("== Compiling Kernel: ==\n%s\n", src);

	compiler = cl_create_compiler();
	program = cl_linked_program_from_source(compiler, src, strlen(src)+1, 1,
			opts, opts_len);
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

