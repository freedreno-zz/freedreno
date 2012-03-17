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

/* bits and pieces borrowed from lima project.. concept is the same, wrap
 * various syscalls and log what happens
 * (although to start with I haven't got running hw so I'll just try to fake
 * the ioctls and see if I can figure out what the driver is trying to do..
 * let's see how far I get with that)
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <errno.h>

#include "kgsl_drm.h"
#include "msm_kgsl.h"

// don't use <stdio.h> from glibc..
struct _IO_FILE;
typedef struct _IO_FILE FILE;
FILE *fopen(const char *path, const char *mode);
int fscanf(FILE *stream, const char *format, ...);
int printf(const char *format, ...);

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

static void * libc_dlsym(const char *name)
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

#define PROLOG(func) 					\
	static typeof(func) *orig_##func = NULL;	\
	if (!orig_##func)				\
		orig_##func = libc_dlsym(#func);	\

struct device_info {
	const char *name;
	struct {
		const char *name;
	} ioctl_info[_IOC_NR(0xffffffff)];
};

#define IOCTL_INFO(n) \
		[_IOC_NR(n)] = { .name = #n }

struct device_info kgsl_3d0_info = {
		.name = "kgsl-3d0",
		.ioctl_info = {
				IOCTL_INFO(IOCTL_KGSL_DEVICE_GETPROPERTY),
				IOCTL_INFO(IOCTL_KGSL_SHAREDMEM_FROM_VMALLOC),
				IOCTL_INFO(IOCTL_KGSL_SHAREDMEM_FREE),
		},
};

// kgsl-2d => Z180 vector graphcis core.. not sure if it is interesting..
struct device_info kgsl_2d0_info = {
		.name = "kgsl-2d0",
		.ioctl_info = {
				// XXX
		},
};

static int kgsl_3d0, kgsl_2d0;

void
hexdump(const void *data, int size)
{
	unsigned char *buf = (void *) data;
	char alpha[17];
	int i;

	for (i = 0; i < size; i++) {
		if (!(i % 16))
			printf("\t\t%08X", (unsigned int) buf + i);

		if (((void *) (buf + i)) < ((void *) data)) {
			printf("   ");
			alpha[i % 16] = '.';
		} else {
			printf(" %02X", buf[i]);

			if (isprint(buf[i]) && (buf[i] < 0xA0))
				alpha[i % 16] = buf[i];
			else
				alpha[i % 16] = '.';
		}

		if ((i % 16) == 15) {
			alpha[16] = 0;
			printf("\t|%s|\n", alpha);
		}
	}

	if (i % 16) {
		for (i %= 16; i < 16; i++) {
			printf("   ");
			alpha[i] = '.';

			if (i == 15) {
				alpha[16] = 0;
				printf("\t|%s|\n", alpha);
			}
		}
	}
}


static void dump_ioctl(struct device_info *info, int dir,
		unsigned long int request, void *ptr, int ret)
{
	int nr = _IOC_NR(request);
	int sz = _IOC_SIZE(request);
	char c;
	const char *name;

	if (dir == _IOC_READ)
		c = '<';
	else
		c = '>';

	if (info->ioctl_info[nr].name)
		name = info->ioctl_info[nr].name;
	else
		name = "<unknown>";

	printf("%12s: %c %s (%08lx)", info->name, c, name, request);
	if (dir == _IOC_READ)
		printf(" => %d", ret);
	printf("\n");

	if (dir & _IOC_DIR(request))
		hexdump(ptr, sz);
}

static void dumpfile(const char *file)
{
	char buf[1024];
	int fd = open(file, 0);
	int n;

	while ((n = read(fd, buf, 1024)) > 0)
		write(1, buf, n);
	close(fd);
}

int open(const char* path, int flags, ...)
{
	mode_t mode = 0;
	int ret;
	PROLOG(open);

	if (flags & O_CREAT) {
		va_list  args;

		va_start(args, flags);
		mode = (mode_t) va_arg(args, int);
		va_end(args);

		ret = orig_open(path, flags, mode);
	} else {
		ret = orig_open(path, flags);
	}

	if (!strcmp(path, "/dev/kgsl-3d0")) {
		kgsl_3d0 = ret;
		printf("found kgsl_3d0: %d\n", kgsl_3d0);
	} else if (!strcmp(path, "/dev/kgsl-2d0")) {
		kgsl_2d0 = ret;
		printf("found kgsl_2d0: %d\n", kgsl_2d0);
#if 0
	} else if (!strcmp(path, "/dev/pmem_gpu1")) {
		// ???
#endif
	} else if (strstr(path, "/dev/")) {
		printf("#### missing device, path: %s\n", path);
	}

	return ret;
}

static long kgsl_ioctl_device_getproperty(void *data)
{
	int result = 0;
	struct kgsl_device_getproperty *param = data;
	switch(param->type) {
	case KGSL_PROP_VERSION: {
		struct kgsl_version version;

		printf("KGSL_PROP_VERSION\n");

		if (param->sizebytes != sizeof(version)) {
			result = -EINVAL;
			break;
		}

		version.drv_major = KGSL_VERSION_MAJOR;
		version.drv_minor = KGSL_VERSION_MINOR;
		version.dev_major = 1;
		version.dev_minor = 4;

		return 0;
	}
	case KGSL_PROP_MMU_ENABLE: {
		printf("KGSL_PROP_MMU_ENABLE\n");
		if (param->sizebytes != sizeof(int)) {
			result = -EINVAL;
			break;
		}
		*(int *)param->value = 1;
		return 0;
	}
	case KGSL_PROP_INTERRUPT_WAITS: {
		printf("KGSL_PROP_INTERRUPT_WAITS\n");
		if (param->sizebytes != sizeof(int)) {
			result = -EINVAL;
			break;
		}
		*(int *)param->value = 1;
		return 0;
	}
	default:
		printf("unknown type: %d\n", param->type);
		return -1;
	}
	return result;
}

static int len_from_vma(unsigned int hostptr)
{
	long long addr, endaddr, offset, inode;
	FILE *f;
	int ret;

	// TODO: only for debug..
	if (0)
		dumpfile("/proc/self/maps");

	f = fopen("/proc/self/maps", "r");

	do {
		char c;
		ret = fscanf(f, "%llx-%llx", &addr, &endaddr);
		if (addr == hostptr)
			return endaddr - addr;
		/* find end of line.. we could do this more cleverly w/ glibc.. :-( */
		while (((ret = fscanf(f, "%c", &c)) > 0) && (c != '\n'));
	} while (ret > 0);
	return -1;
}

static void kgsl_ioctl_sharedmem_from_vmalloc_pre(
		struct kgsl_sharedmem_from_vmalloc *param)
{
	int len;

	/* just make gpuaddr == hostptr.. should make it easy to track */
	printf("\t\tflags:\t\t%08x\n", param->flags);
	printf("\t\thostptr:\t%08x\n", param->hostptr);
	if (param->gpuaddr) {
		len = param->gpuaddr;
	} else {
		/* note: if gpuaddr not specified, need to figure out length from
		 * vma.. that is nasty!
		 */
		len = len_from_vma(param->hostptr);
	}
	printf("\t\tlen:\t\t%08x\n", len);
}

static void kgsl_ioctl_sharedmem_from_vmalloc_post(
		struct kgsl_sharedmem_from_vmalloc *param)
{
	printf("\t\tgpuaddr:\t%08x\n", param->gpuaddr);
}

static void kgsl_ioctl_sharedmem_free(struct kgsl_sharedmem_free *param)
{
	printf("\t\tgpuaddr:\t%08x\n", param->gpuaddr);
}

void kgsl_3d0_ioctl_pre(unsigned long int request, void *ptr)
{
	dump_ioctl(&kgsl_3d0_info, _IOC_WRITE, request, ptr, 0);
	switch(_IOC_NR(request)) {
	case _IOC_NR(IOCTL_KGSL_SHAREDMEM_FROM_VMALLOC):
		kgsl_ioctl_sharedmem_from_vmalloc_pre(ptr);
		break;
	case _IOC_NR(IOCTL_KGSL_SHAREDMEM_FREE):
		printf("found: IOCTL_KGSL_SHAREDMEM_FREE(%p)\n", ptr);
		kgsl_ioctl_sharedmem_free(ptr);
		break;
	}
}

void kgsl_3d0_ioctl_post(unsigned long int request, void *ptr, int ret)
{
	dump_ioctl(&kgsl_3d0_info, _IOC_READ, request, ptr, ret);
	switch(_IOC_NR(request)) {
	case _IOC_NR(IOCTL_KGSL_DEVICE_GETPROPERTY):
		kgsl_ioctl_device_getproperty(ptr);
		break;
	case _IOC_NR(IOCTL_KGSL_SHAREDMEM_FROM_VMALLOC):
		kgsl_ioctl_sharedmem_from_vmalloc_post(ptr);
		break;
	}
}

void kgsl_2d0_ioctl_pre(unsigned long int request, void *ptr)
{
	dump_ioctl(&kgsl_2d0_info, _IOC_WRITE, request, ptr, 0);
}

void kgsl_2d0_ioctl_post(unsigned long int request, void *ptr, int ret)
{
	dump_ioctl(&kgsl_2d0_info, _IOC_READ, request, ptr, ret);
}

int ioctl(int fd, unsigned long int request, ...)
{
	int ioc_size = _IOC_SIZE(request);
	int ret;
	PROLOG(ioctl);
	void *ptr;

	if (ioc_size) {
		va_list args;

		va_start(args, request);
		ptr = va_arg(args, void *);
		va_end(args);
	} else {
		ptr = NULL;
	}

	if (fd == kgsl_3d0)
		kgsl_3d0_ioctl_pre(request, ptr);
	else if (fd == kgsl_2d0)
		kgsl_2d0_ioctl_pre(request, ptr);

	ret = orig_ioctl(fd, request, ptr);

	if (fd == kgsl_3d0)
		kgsl_3d0_ioctl_post(request, ptr, ret);
	else if (fd == kgsl_2d0)
		kgsl_2d0_ioctl_post(request, ptr, ret);

	return ret;
}


