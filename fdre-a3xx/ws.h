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

#ifndef WS_H_
#define WS_H_

#include <freedreno_drmif.h>

/* TODO this needs to move somewhere.. */
#include "util.h"
struct fd_surface {
	struct fd_bo *bo;
	uint32_t cpp;	/* bytes per pixel */
	uint32_t width, height, pitch;	/* width/height/pitch in pixels */
	enum a3xx_color_fmt color;
};

struct fd_winsys {
	struct fd_device *dev;

	/* thanks to the bizarre way we have to map the fbdev buffer (since msm
	 * driver isn't a proper KMS driver), we need the winsys to create the
	 * pipe too..
	 */
	struct fd_pipe *pipe;

	void (*destroy)(struct fd_winsys *ws);
	struct fd_surface * (*get_surface)(struct fd_winsys *ws,
			uint32_t *width, uint32_t *height);
	int (*post_surface)(struct fd_winsys *ws, struct fd_surface *surface);
};

struct fd_winsys * fd_winsys_fbdev_open(void);
#ifdef HAVE_X11
struct fd_winsys * fd_winsys_dri2_open(void);
#endif

#endif /* WS_H_ */
