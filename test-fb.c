/*
 * Copyright © 2012 Rob Clark <robclark@freedesktop.org>
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
#include <fcntl.h>
#include <sys/ioctl.h>
#include "fb.h"

#include "test-util.h"

void test_fill(uint32_t w, uint32_t h, uint32_t format, unsigned long phys)
{
	PixmapPtr dest;
	C2D_RECT rect;
	c2d_ts_handle curTimestamp;

	DEBUG_MSG("----------------------------------------------------------------");
	DEBUG_MSG("fb: %04dx%04d-%08x", w, h, format);
	RD_START("fb", "%dx%d-%08x", w, h, format);

	if (phys) {
		dest = create_pixmap_phys(w, h, format, phys);
	} else {
		dest = create_pixmap(w, h, format);
	}


	rect.x = 1 + (w / 64);
	rect.y = 2 + (w / 32);
	rect.width = w - 2 * rect.x;
	rect.height = h - 2 * rect.y;

	// note: look for pattern 0xff556677 in memory to find cmdstream:
	CHK(c2dFillSurface(dest->id, 0xff556677, &rect));
	CHK(c2dFlush(dest->id, &curTimestamp));
	CHK(c2dWaitTimestamp(curTimestamp));

	// second blit.. fill a sub-rect in center of surface:
	rect.x = (w - 10) / 2;
	rect.y = (h - 16) / 2;
	rect.width = 10;
	rect.height = 16;
	CHK(c2dFillSurface(dest->id, 0xff223344, &rect));
	CHK(c2dFlush(dest->id, &curTimestamp));
	CHK(c2dWaitTimestamp(curTimestamp));

	sleep(5);

	RD_END();

//	dump_pixmap(dest, "fill-%04dx%04d-%08x.bmp", w, h, format);
}

#define MSMFB_IOCTL_MAGIC 'm'
#define MSMFB_RESUME_SW_REFRESHER _IOW(MSMFB_IOCTL_MAGIC, 129, unsigned int)

/* note: this test seems to trigger gpu faults.. probably not mapping fb
 * properly.  So not sure how much faith to put in this particular test..
 */

int main(int argc, char **argv)
{
	struct fb_fix_screeninfo fixed_info;
	struct fb_var_screeninfo mode_info;
	int fd = open("/dev/fb0", O_RDWR, 0);
	int ret;

	/* Unblank the screen if it was previously blanked */
	ret = ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK);

	/* Make sure the software refresher is on */
	ioctl(fd, MSMFB_RESUME_SW_REFRESHER, 0);

	/* Get the fixed info (par) structure */
	ioctl(fd, FBIOGET_FSCREENINFO, &fixed_info);

	/* Get the current screen setting */
	ioctl(fd, FBIOGET_VSCREENINFO, &mode_info);

	test_fill(640, 480, C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA,
			fixed_info.smem_start);

	test_fill(640, 480, C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA, 0);

	test_fill(640, 480, C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_DISABLE_ALPHA,
			fixed_info.smem_start);

	return 0;
}

void _start(int argc, char **argv)
{
	exit(main(argc, argv));
}

