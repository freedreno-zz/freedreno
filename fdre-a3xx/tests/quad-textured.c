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

#include <stdlib.h>
#include <stdio.h>

#include "freedreno.h"
#include "redump.h"
#include "cubetex.h"

int main(int argc, char **argv)
{
	struct fd_state *state;
	struct fd_surface *surface, *tex;

	float vertices[] = {
			-1.0, -1.0, 0.0,
			+1.0, -1.0, 0.0,
			-1.0, +1.0, 0.0,
			+1.0, +1.0, 0.0,
	};

	float texcoords[] = {
			1.0f, 1.0f,
			0.0f, 1.0f,
			1.0f, 0.0f,
			0.0f, 0.0f,
	};

	const char *vertex_shader_asm =
		"@attribute(r0.x)         aPosition                               \n"
		"@attribute(r1.x-r1.y)    aTexCoord                               \n"
		"@varying(r1.x-r1.y)      vTexCoord                               \n"
		"(sy)(ss)end                                                      \n";

	const char *fragment_shader_asm =
		"@varying(r1.x-r1.y)      vTexCoord                               \n"
		"@sampler(0)              uTexture                                \n"
		"(sy)(ss)(rpt1)bary.f (ei)r0.z, (r)0, r0.x                        \n"
		"(rpt5)nop                                                        \n"
		"sam (f16)(xyzw)hr0.x, r0.z, s#0, t#0                             \n"
		"end                                                              \n";

	uint32_t width = 0, height = 0;

	RD_START("fd-quad-textured", "");

	state = fd_init();
	if (!state)
		return -1;

	surface = fd_surface_screen(state, &width, &height);
//	surface = fd_surface_new(state, width, height);
	if (!surface)
		return -1;

	fd_make_current(state, surface);

	tex = fd_surface_new_fmt(state, cube_texture.width, cube_texture.height,
			RB_R8G8B8A8_UNORM);

	fd_surface_upload(tex, cube_texture.pixel_data);

	fd_set_texture(state, "uTexture", tex);

	fd_vertex_shader_attach_asm(state, vertex_shader_asm);
	fd_fragment_shader_attach_asm(state, fragment_shader_asm);

	fd_link(state);

	fd_clear_color(state, (float[]){ 0.5, 0.5, 0.5, 1.0 });
	fd_clear(state, GL_COLOR_BUFFER_BIT);

	fd_attribute_pointer(state, "aPosition", VFMT_FLOAT_32_32_32, 4, vertices);
	fd_attribute_pointer(state, "aTexCoord", VFMT_FLOAT_32_32, 4, texcoords);

	fd_draw_arrays(state, GL_TRIANGLE_STRIP, 0, 4);

	fd_swap_buffers(state);

	fd_flush(state);

	fd_dump_bmp(surface, "quad-textured.bmp");

	sleep(1);

	fd_fini(state);

	RD_END();

	return 0;
}
