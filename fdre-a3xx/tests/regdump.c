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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <err.h>

#include "freedreno.h"
#include "redump.h"

#include "cubetex.h"

static void
read_file(const char *filename, void **ptr, size_t *size)
{
	int fd, ret;
	struct stat st;

	*ptr = MAP_FAILED;

	fd = open(filename, O_RDONLY);
	if (fd == -1)
		errx(1, "couldn't open `%s'", filename);

	ret = fstat(fd, &st);
	if (ret)
		errx(1, "couldn't stat `%s'", filename);

	*size = st.st_size;
	*ptr = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (*ptr == MAP_FAILED)
		errx(1, "couldn't map `%s'", filename);

	close(fd);
}

int main(int argc, char **argv)
{
	struct fd_state *state;
	struct fd_surface *surface, *tex;
	struct fd_perfctrs ctrs;
	size_t sz;

	float vertices1[] = {
			-1.0, -1.0, 0.0,
			+0.0, -1.0, 0.0,
			-1.0, +1.0, 0.0,
			+0.0, +1.0, 0.0
	};
	float vertices2[] = {
			+1.0, -1.0, 0.0,
			+0.0, -1.0, 0.0,
			+1.0, +1.0, 0.0,
			+0.0, +1.0, 0.0
	};

	char *vertex_shader_asm =
		"@attribute(r0.x) aPosition                                          \n"
		"@varying(r0.x)   vColor    ; same slot as aPosition                 \n"
		"@out(r0.x)       gl_Position                                        \n"
		"(sy)(ss)end                                                         \n";
	char *fragment_shader_asm =
		"@varying(r0.x)   vColor                                             \n"
		"@sampler(0)      uTexture                                           \n"
		"@out(r1.x)       gl_FragColor                                       \n"
		"(sy)(ss)(rpt3)bary.f (ei)r1.x, (r)0, r0.x                           \n"
		"end                                                                 \n";

	DEBUG_MSG("----------------------------------------------------------------");
	RD_START("regdump", "");

	if (argc > 1) {
		read_file(argv[1], &fragment_shader_asm, &sz);
		DEBUG_MSG("using fragment shader:\n%s", fragment_shader_asm);
	}
	if (argc > 2) {
		read_file(argv[2], &vertex_shader_asm, &sz);
		DEBUG_MSG("using vertex shader:\n%s", vertex_shader_asm);
	}

	state = fd_init();
	if (!state)
		return -1;

	surface = fd_surface_new_fmt(state, 4, 4, RB_R32G32B32A32_UINT);
	if (!surface)
		return -1;

	fd_make_current(state, surface);

	tex = fd_surface_new_fmt(state, cube_texture.width, cube_texture.height,
			RB_R8G8B8A8_UNORM);

	fd_surface_upload(tex, cube_texture.pixel_data);

	fd_vertex_shader_attach_asm(state, vertex_shader_asm);
	fd_fragment_shader_attach_asm(state, fragment_shader_asm);

	fd_link(state);

	fd_clear_color(state, (float[]){ 0.0, 0.0, 0.0, 1.0 });
	fd_clear(state, GL_COLOR_BUFFER_BIT);

	fd_set_texture(state, "uTexture", tex);

	fd_query_start(state);

	fd_attribute_pointer(state, "aPosition", VFMT_FLOAT_32_32_32, 4, vertices1);
	fd_draw_arrays(state, GL_TRIANGLE_STRIP, 0, 4);

	fd_attribute_pointer(state, "aPosition", VFMT_FLOAT_32_32_32, 4, vertices2);
	fd_draw_arrays(state, GL_TRIANGLE_STRIP, 0, 4);

	fd_query_end(state);

	fd_swap_buffers(state);

	fd_flush(state);

	fd_query_read(state, &ctrs);
	fd_query_dump(&ctrs);

	fd_dump_hex(surface);

	fd_fini(state);

	RD_END();

	return 0;
}
