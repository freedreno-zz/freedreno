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

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/fb.h>

#include "ws.h"
#include "util.h"

struct fd_winsys_fbdev {
	struct fd_winsys base;

	struct fd_surface *surface;
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	void *ptr;
	int fd;
};

static inline struct fd_winsys_fbdev * to_fbdev_ws(struct fd_winsys *ws)
{
	return (struct fd_winsys_fbdev *)ws;
}


static void destroy(struct fd_winsys *ws)
{
	struct fd_winsys_fbdev *ws_fbdev = to_fbdev_ws(ws);

	if (ws->pipe)
		fd_pipe_del(ws->pipe);

	if (ws->dev)
		fd_device_del(ws->dev);

	if (ws_fbdev->fd)
		close(ws_fbdev->fd);

	free(ws_fbdev);
}

static struct fd_surface * get_surface(struct fd_winsys *ws,
		uint32_t *width, uint32_t *height)
{
	struct fd_winsys_fbdev *ws_fbdev = to_fbdev_ws(ws);
	struct fd_surface *surface;

	if (!ws_fbdev->surface) {
		surface = calloc(1, sizeof(*surface));
		assert(surface);

		/* TODO don't hardcode: */
		surface->color  = RB_R8G8B8A8_UNORM;
		surface->cpp    = 4;
		surface->width  = ws_fbdev->var.xres;
		surface->height = ws_fbdev->var.yres;
		surface->pitch  = ws_fbdev->fix.line_length / surface->cpp;

		surface->bo = fd_bo_from_fbdev(ws->pipe, ws_fbdev->fd,
				ws_fbdev->var.yres_virtual * ws_fbdev->fix.line_length);

		ws_fbdev->surface = surface;
	} else {
		surface = ws_fbdev->surface;
	}

	if (width)
		*width = surface->width;

	if (height)
		*height = surface->height;

	return surface;
}

static int post_surface(struct fd_winsys *ws, struct fd_surface *surface)
{
	struct fd_winsys_fbdev *ws_fbdev = to_fbdev_ws(ws);

	/* if we are rendering to front-buffer, we can skip this */
	if (surface != ws_fbdev->surface) {
		char *dstptr = ws_fbdev->ptr;
		char *srcptr = fd_bo_map(surface->bo);
		uint32_t len = surface->pitch * surface->cpp;
		uint32_t i;

		if (len > ws_fbdev->fix.line_length)
			len = ws_fbdev->fix.line_length;

		for (i = 0; i < surface->height; i++) {
			memcpy(dstptr, srcptr, len);
			dstptr += ws_fbdev->fix.line_length;
			srcptr += len;
		}
	}

	return 0;
}

struct fd_winsys * fd_winsys_fbdev_open(void)
{
	struct fd_winsys_fbdev *ws_fbdev = calloc(1, sizeof(*ws_fbdev));
	struct fd_winsys *ws = &ws_fbdev->base;
	int fd, ret;

	fd = drmOpen("kgsl", NULL);
	if (fd < 0) {
		ERROR_MSG("could not open kgsl device: %d (%s)",
				fd, strerror(errno));
		goto fail;
	}

	ws->dev = fd_device_new(fd);
	ws->pipe = fd_pipe_new(ws->dev, FD_PIPE_3D);

	fd = open("/dev/fb0", O_RDWR);
	ret = ioctl(fd, FBIOGET_VSCREENINFO, &ws_fbdev->var);
	if (ret) {
		ERROR_MSG("failed to get var: %d (%s)",
				ret, strerror(errno));
		goto fail;
	}
	ret = ioctl(fd, FBIOGET_FSCREENINFO, &ws_fbdev->fix);
	if (ret) {
		ERROR_MSG("failed to get fix: %d (%s)",
				ret, strerror(errno));
		goto fail;
	}

	INFO_MSG("res %dx%d virtual %dx%d, line_len %d",
			ws_fbdev->var.xres, ws_fbdev->var.yres,
			ws_fbdev->var.xres_virtual,
			ws_fbdev->var.yres_virtual,
			ws_fbdev->fix.line_length);

	ws_fbdev->var.yoffset = ws_fbdev->var.xoffset = 0;
	ret = ioctl(fd, FBIOPAN_DISPLAY, &ws_fbdev->var);
	if (ret) {
		WARN_MSG("failed to pan: %d (%s)",
				ret, strerror(errno));
	}

	ws_fbdev->fd = fd;
	ws_fbdev->ptr = mmap(0,
			ws_fbdev->var.yres_virtual * ws_fbdev->fix.line_length,
			PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
	if(ws_fbdev->ptr == MAP_FAILED) {
		ERROR_MSG("mmap failed");
		goto fail;
	}

	ws->destroy = destroy;
	ws->get_surface = get_surface;
	ws->post_surface = post_surface;

	return ws;

fail:
	destroy(ws);
	return NULL;
}
