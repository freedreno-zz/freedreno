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

#include <xf86drm.h>
#include <X11/Xlib.h>
#include <X11/Xmd.h>
#include <X11/extensions/dri2proto.h>
#include "X11/extensions/dri2.h"
#include <fcntl.h>

#include "ws.h"
#include "util.h"

struct fd_winsys_dri2 {
	struct fd_winsys base;

	int width, height;
	struct fd_surface *surface;
	Display *dpy;
	Window win;
	DRI2Buffer *dri2buf;

};

static inline struct fd_winsys_dri2 * to_dri2_ws(struct fd_winsys *ws)
{
	return (struct fd_winsys_dri2 *)ws;
}


static void destroy(struct fd_winsys *ws)
{
	struct fd_winsys_dri2 *ws_dri2 = to_dri2_ws(ws);

	if (ws->pipe)
		fd_pipe_del(ws->pipe);

	if (ws->dev)
		fd_device_del(ws->dev);

	DRI2DestroyDrawable(ws_dri2->dpy, ws_dri2->win);
	free(ws_dri2->dri2buf);

	XDestroyWindow(ws_dri2->dpy, ws_dri2->win);
	XCloseDisplay(ws_dri2->dpy);

	free(ws_dri2);
}

static struct fd_surface * get_surface(struct fd_winsys *ws,
		uint32_t *width, uint32_t *height)
{
	struct fd_winsys_dri2 *ws_dri2 = to_dri2_ws(ws);
	struct fd_surface *surface;

	if (!ws_dri2->surface) {
		surface = calloc(1, sizeof(*surface));
		assert(surface);

		/* TODO don't hardcode: */
		surface->color  = COLORX_8_8_8_8;
		surface->cpp    = 4;
		surface->width  = ws_dri2->width;
		surface->height = ws_dri2->height;
		surface->pitch  = ws_dri2->dri2buf->pitch[0] / surface->cpp;

		surface->bo = fd_bo_from_name(ws->dev,
				ws_dri2->dri2buf->names[0]);

		ws_dri2->surface = surface;
	} else {
		surface = ws_dri2->surface;
	}

	if (width)
		*width = surface->width;

	if (height)
		*height = surface->height;

	return surface;
}

static int post_surface(struct fd_winsys *ws, struct fd_surface *surface)
{
	struct fd_winsys_dri2 *ws_dri2 = to_dri2_ws(ws);
	CARD64 count;

	if (!ws_dri2->surface)
		get_surface(ws, NULL, NULL);

	/* if we are rendering to front-buffer, we can skip this */
	if (surface != ws_dri2->surface) {
		char *dstptr = fd_bo_map(ws_dri2->surface->bo);
		char *srcptr = fd_bo_map(surface->bo);
		uint32_t len = surface->pitch * surface->cpp;
		uint32_t i;

		if (len > ws_dri2->dri2buf->pitch[0])
			len = ws_dri2->dri2buf->pitch[0];

		for (i = 0; i < surface->height; i++) {
			memcpy(dstptr, srcptr, len);
			dstptr += ws_dri2->dri2buf->pitch[0];
			srcptr += len;
		}
	}

	DRI2SwapBuffers(ws_dri2->dpy, ws_dri2->win, 0, 0, 0, &count);
	DEBUG_MSG("DRI2SwapBuffers: count=%lu", count);

	return 0;
}

static Bool WireToEvent(Display *dpy, XExtDisplayInfo *info,
		XEvent *event, xEvent *wire)
{
	switch ((wire->u.u.type & 0x7f) - info->codes->first_event) {
	case DRI2_BufferSwapComplete:
		DEBUG_MSG("BufferSwapComplete");
		return True;
	case DRI2_InvalidateBuffers:
		DEBUG_MSG("InvalidateBuffers");
		return False;
	default:
		/* client doesn't support server event */
		break;
	}

	return False;
}

static Status EventToWire(Display *dpy, XExtDisplayInfo *info,
		XEvent *event, xEvent *wire)
{
	switch (event->type) {
	default:
		/* client doesn't support server event */
		break;
	}

	return Success;
}

static const DRI2EventOps ops = {
		.WireToEvent = WireToEvent,
		.EventToWire = EventToWire,
};

static int dri2_connect(Display *dpy, int driverType, char **driver)
{
	int eventBase, errorBase, major, minor;
	char *device;
	drm_magic_t magic;
	Window root;
	int fd;

	if (!DRI2InitDisplay(dpy, &ops)) {
		ERROR_MSG("DRI2InitDisplay failed");
		return -1;
	}

	if (!DRI2QueryExtension(dpy, &eventBase, &errorBase)) {
		ERROR_MSG("DRI2QueryExtension failed");
		return -1;
	}

	DEBUG_MSG("DRI2QueryExtension: eventBase=%d, errorBase=%d",
			eventBase, errorBase);

	if (!DRI2QueryVersion(dpy, &major, &minor)) {
		ERROR_MSG("DRI2QueryVersion failed");
		return -1;
	}

	DEBUG_MSG("DRI2QueryVersion: major=%d, minor=%d", major, minor);

	root = RootWindow(dpy, DefaultScreen(dpy));

	if (!DRI2Connect(dpy, root, driverType, driver, &device)) {
		ERROR_MSG("DRI2Connect failed");
		return -1;
	}

	DEBUG_MSG("DRI2Connect: driver=%s, device=%s", *driver, device);

	fd = open(device, O_RDWR);
	if (fd < 0) {
		ERROR_MSG("open failed");
		return fd;
	}

	if (drmGetMagic(fd, &magic)) {
		ERROR_MSG("drmGetMagic failed");
		return -1;
	}

	if (!DRI2Authenticate(dpy, root, magic)) {
		ERROR_MSG("DRI2Authenticate failed");
		return -1;
	}

	return fd;
}

struct fd_winsys * fd_winsys_dri2_open(void)
{
	struct fd_winsys_dri2 *ws_dri2 = calloc(1, sizeof(*ws_dri2));
	struct fd_winsys *ws = &ws_dri2->base;
	static unsigned attachments[] = {
			DRI2BufferBackLeft,
	};
	char *driver;
	Display *dpy;
	Window win;
	DRI2Buffer *dri2buf;
	int fd, nbufs, w, h;

	dpy = XOpenDisplay(NULL);
	if (!dpy)
		return NULL;
	win = XCreateSimpleWindow(dpy, RootWindow(dpy, 0),
			1, 1, 800, 600, 0, BlackPixel (dpy, 0),
			BlackPixel(dpy, 0));
	XMapWindow(dpy, win);
	XFlush(dpy);

	ws_dri2->dpy = dpy;
	ws_dri2->win = win;

	if ((fd = dri2_connect(dpy, DRI2DriverDRI, &driver)) < 0) {
		goto fail;
	}

	DEBUG_MSG("opened: %s", driver);

	ws->dev = fd_device_new(fd);
	ws->pipe = fd_pipe_new(ws->dev, FD_PIPE_3D);

	DRI2CreateDrawable(dpy, win);

	dri2buf = DRI2GetBuffers(dpy, win, &w, &h, attachments, 1, &nbufs);
	if (!dri2buf) {
		ERROR_MSG("DRI2GetBuffers failed");
		goto fail;
	}

	ws_dri2->width = w;
	ws_dri2->height = h;
	ws_dri2->dri2buf = dri2buf;

	DEBUG_MSG("DRI2GetBuffers: w=%d, h=%d, nbufs=%d", w, h, nbufs);

	ws->destroy = destroy;
	ws->get_surface = get_surface;
	ws->post_surface = post_surface;

	return ws;

fail:
	destroy(ws);
	return NULL;
}
