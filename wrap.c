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
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <ctype.h>
#include <errno.h>

#include "kgsl_drm.h"
#include "msm_kgsl.h"

static void *libc_dl;

static int libc_dlopen(void)
{
	libc_dl = dlopen("libc.so", RTLD_LAZY);
	if (!libc_dl) {
		printf("Failed to dlopen %s: %s\n",
		       "libc.so", dlerror());
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

static int kgsl_fd;

static void dump(const char *file)
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

	if (!strcmp(path, "/dev/kgsl-3d0")) {
		/* emulated */
		ret = kgsl_fd = orig_open("/tmp/foo", O_CREAT, 0644);
		printf("found kgsl: %d\n", kgsl_fd);
		return ret;
	} else if (!strcmp(path, "/dev/kgsl-2d0")) {
		// ???
	} else if (!strcmp(path, "/dev/pmem_gpu1")) {
		// ???
	}

	if (flags & O_CREAT) {
		va_list  args;

		va_start(args, flags);
		mode = (mode_t) va_arg(args, int);
		va_end(args);

		ret = orig_open(path, flags, mode);
	} else {
		ret = orig_open(path, flags);
	}

	if (ret < 0 && strstr(path, "/dev/")) {
		printf("missing device, path: %s\n", path);
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

static long kgsl_ioctl_sharedmem_from_vmalloc(void *data)
{
	struct kgsl_sharedmem_from_vmalloc *param = data;
	/* just make gpuaddr == hostptr.. should make it easy to track */
	printf("flags=%08x, hostptr=%08x len=%d (?)\n",
			param->flags, param->hostptr, param->gpuaddr);
	/* note: if gpuaddr not specified, need to figure out length from
	 * vma.. that is nasty!
	 */
//	dump("/proc/self/maps");
//	int spin = 1;
//	while (spin);
	/*
0x800008d0 in kgsl_ioctl_sharedmem_from_vmalloc (data=0xbe9ce1ec) at wrap.c:182
182		while (spin);
(gdb) bt
#0  0x800008d0 in kgsl_ioctl_sharedmem_from_vmalloc (data=0xbe9ce1ec) at wrap.c:182
#1  0x80000972 in kgsl_ioctl (request=3222014243, ptr=0xbe9ce1ec) at wrap.c:203
#2  0x80000a24 in ioctl (fd=3, request=3222014243) at wrap.c:229
#3  0x80204024 in ?? ()   <== libgsl.so on 1st call..
Cannot access memory at address 0x0
#4  0x80204024 in ?? ()
Cannot access memory at address 0x0
--------
00008000-00009000 r-xp 00000000 08:01 31180347   /home/robclark/src/blob/msm/test
00010000-00011000 rw-p 00000000 08:01 31180347   /home/robclark/src/blob/msm/test
01350000-01355000 rw-p 00000000 00:00 0          [heap]
80000000-80001000 r-xp 00000000 08:01 31180348   /home/robclark/src/blob/msm/wrap.so
80001000-80008000 r-xp 00000000 00:00 0
80008000-80009000 rw-p 00000000 08:01 31180348   /home/robclark/src/blob/msm/wrap.so
80100000-8010a000 r-xp 00000000 08:01 85844672   /system/lib/libC2D2.so
8010a000-8010b000 rw-p 0000a000 08:01 85844672   /system/lib/libC2D2.so
80200000-8020a000 r-xp 00000000 08:01 85844834   /system/lib/libgsl.so
8020a000-8020b000 rw-p 0000a000 08:01 85844834   /system/lib/libgsl.so
80300000-8033d000 r-xp 00000000 08:01 85844767   /system/lib/libOpenVG.so
8033d000-8033e000 rw-p 0003d000 08:01 85844767   /system/lib/libOpenVG.so
9d100000-9d138000 r-xp 00000000 08:01 85844745   /system/lib/libstlport.so
9d138000-9d13a000 rw-p 00038000 08:01 85844745   /system/lib/libstlport.so
af900000-af90e000 r-xp 00000000 08:01 85844814   /system/lib/libcutils.so
af90e000-af90f000 rw-p 0000e000 08:01 85844814   /system/lib/libcutils.so
af90f000-af91e000 rw-p 00000000 00:00 0
afa00000-afa03000 r-xp 00000000 08:01 85844806   /system/lib/liblog.so
afa03000-afa04000 rw-p 00003000 08:01 85844806   /system/lib/liblog.so
afb00000-afb16000 r-xp 00000000 08:01 85844808   /system/lib/libm.so
afb16000-afb17000 rw-p 00016000 08:01 85844808   /system/lib/libm.so
afc00000-afc01000 r-xp 00000000 08:01 85844673   /system/lib/libstdc++.so
afc01000-afc02000 rw-p 00001000 08:01 85844673   /system/lib/libstdc++.so
afd00000-afd41000 r-xp 00000000 08:01 85844741   /system/lib/libc.so
afd41000-afd44000 rw-p 00041000 08:01 85844741   /system/lib/libc.so
afd44000-afd4f000 rw-p 00000000 00:00 0
b0001000-b0009000 r-xp 00001000 08:01 85844129   /system/bin/linker
b0009000-b000a000 rw-p 00009000 08:01 85844129   /system/bin/linker
b000a000-b0015000 rw-p 00000000 00:00 0
b6ff6000-b6ff7000 rw-s 00000000 00:04 2241036    /dev/zero (deleted)
b6ff7000-b6ff8000 r--p 00000000 00:00 0
be9ae000-be9cf000 rw-p 00000000 00:00 0          [stack]
ffff0000-ffff1000 r-xp 00000000 00:00 0          [vectors]

	 */
	param->gpuaddr = param->hostptr;
	return 0;
}

static long kgsl_ioctl_sharedmem_free(void *data)
{
	struct kgsl_sharedmem_free *param = data;
	printf("gpuaddr=%08x\n", param->gpuaddr);
	return 0;
}

int kgsl_ioctl(unsigned long int request, void *ptr)
{
	printf("kgsl ioctl: %08lx\n", request);
	switch(_IOC_NR(request)) {
	case _IOC_NR(IOCTL_KGSL_DEVICE_GETPROPERTY):
		printf("found: IOCTL_KGSL_DEVICE_GETPROPERTY(%p)\n", ptr);
		return kgsl_ioctl_device_getproperty(ptr);
	case _IOC_NR(IOCTL_KGSL_SHAREDMEM_FROM_VMALLOC):
		printf("found: IOCTL_KGSL_SHAREDMEM_FROM_VMALLOC(%p)\n", ptr);
		return kgsl_ioctl_sharedmem_from_vmalloc(ptr);
	case _IOC_NR(IOCTL_KGSL_SHAREDMEM_FREE):
		printf("found: IOCTL_KGSL_SHAREDMEM_FREE(%p)\n", ptr);
		return kgsl_ioctl_sharedmem_free(ptr);
	default:
		printf("found ioctl: %08lx %p\n", request, ptr);
		break;
	}
	return -1;
}

int ioctl(int fd, unsigned long int request, ...)
{
	int ioc_size = _IOC_SIZE(request);
	int ret;
	PROLOG(ioctl);

	if (ioc_size) {
		va_list args;
		void *ptr;

		va_start(args, request);
		ptr = va_arg(args, void *);
		va_end(args);

		if (fd == kgsl_fd)
			ret = kgsl_ioctl(request, ptr);
		else
			ret = orig_ioctl(fd, request, ptr);
	} else {
		if (fd == kgsl_fd)
			ret = kgsl_ioctl(request, NULL);
		else
			ret = orig_ioctl(fd, request);
	}

	return ret;
}


