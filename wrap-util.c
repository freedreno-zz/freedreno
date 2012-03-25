/*
 * Copyright © 2012 Rob Clark
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

#include "wrap.h"

static int fd = -1;

void rd_start(const char *name, const char *fmt, ...)
{
	char buf[256];
	static int cnt = 0;
	va_list  args;

	sprintf(buf, "%s-%04d.rd", name, cnt++);

	fd = open(buf, O_WRONLY| O_TRUNC | O_CREAT, 0644);

	va_start(args, fmt);
	vsprintf(buf, fmt, args);
	va_end(args);

	rd_write_section(RD_TEST, buf, strlen(buf));
}

void rd_end(void)
{
	close(fd);
	fd = -1;
}

void rd_write_section(enum rd_sect_type type, void *buf, int sz)
{
	if (fd == -1)
		return;
	write(fd, &type, sizeof(type));
	write(fd, &sz, 4);
	write(fd, buf, sz);
}


static void *libc_dl;

static int libc_dlopen(void)
{
	libc_dl = dlopen("libc.so", RTLD_LAZY);
	if (!libc_dl) {
		printf("Failed to dlopen %s: %s\n", "libc.so", dlerror());
		exit(-1);
	}

	return 0;
}

void * libc_dlsym(const char *name)
{
	void *func;

	if (!libc_dl)
		libc_dlopen();

	func = dlsym(libc_dl, name);

	if (!func) {
		printf("Failed to find %s in %s: %s\n",
		       name, "libc.so", dlerror());
		exit(-1);
	}

	return func;
}
