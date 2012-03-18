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
#include "android_pmem.h"

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

static struct device_info kgsl_3d_info = {
		.name = "kgsl-3d",
		.ioctl_info = {
				IOCTL_INFO(IOCTL_KGSL_DEVICE_GETPROPERTY),
				IOCTL_INFO(IOCTL_KGSL_DEVICE_WAITTIMESTAMP),
				IOCTL_INFO(IOCTL_KGSL_RINGBUFFER_ISSUEIBCMDS),
				IOCTL_INFO(IOCTL_KGSL_CMDSTREAM_READTIMESTAMP),
				IOCTL_INFO(IOCTL_KGSL_CMDSTREAM_FREEMEMONTIMESTAMP),
				IOCTL_INFO(IOCTL_KGSL_DRAWCTXT_CREATE),
				IOCTL_INFO(IOCTL_KGSL_DRAWCTXT_DESTROY),
				IOCTL_INFO(IOCTL_KGSL_MAP_USER_MEM),
				IOCTL_INFO(IOCTL_KGSL_SHAREDMEM_FROM_PMEM),
				IOCTL_INFO(IOCTL_KGSL_SHAREDMEM_FREE),
				IOCTL_INFO(IOCTL_KGSL_SHAREDMEM_FROM_VMALLOC),
				IOCTL_INFO(IOCTL_KGSL_SHAREDMEM_FLUSH_CACHE),
				IOCTL_INFO(IOCTL_KGSL_GPUMEM_ALLOC),
				IOCTL_INFO(IOCTL_KGSL_CFF_SYNCMEM),
				IOCTL_INFO(IOCTL_KGSL_CFF_USER_EVENT),
				IOCTL_INFO(IOCTL_KGSL_TIMESTAMP_EVENT),
				/* kgsl-3d specific ioctls: */
				IOCTL_INFO(IOCTL_KGSL_DRAWCTXT_SET_BIN_BASE_OFFSET),
		},
};

// kgsl-2d => Z180 vector graphcis core.. not sure if it is interesting..
static struct device_info kgsl_2d_info = {
		.name = "kgsl-2d",
		.ioctl_info = {
				IOCTL_INFO(IOCTL_KGSL_DEVICE_GETPROPERTY),
				IOCTL_INFO(IOCTL_KGSL_DEVICE_WAITTIMESTAMP),
				IOCTL_INFO(IOCTL_KGSL_RINGBUFFER_ISSUEIBCMDS),
				IOCTL_INFO(IOCTL_KGSL_CMDSTREAM_READTIMESTAMP),
				IOCTL_INFO(IOCTL_KGSL_CMDSTREAM_FREEMEMONTIMESTAMP),
				IOCTL_INFO(IOCTL_KGSL_DRAWCTXT_CREATE),
				IOCTL_INFO(IOCTL_KGSL_DRAWCTXT_DESTROY),
				IOCTL_INFO(IOCTL_KGSL_MAP_USER_MEM),
				IOCTL_INFO(IOCTL_KGSL_SHAREDMEM_FROM_PMEM),
				IOCTL_INFO(IOCTL_KGSL_SHAREDMEM_FREE),
				IOCTL_INFO(IOCTL_KGSL_SHAREDMEM_FROM_VMALLOC),
				IOCTL_INFO(IOCTL_KGSL_SHAREDMEM_FLUSH_CACHE),
				IOCTL_INFO(IOCTL_KGSL_GPUMEM_ALLOC),
				IOCTL_INFO(IOCTL_KGSL_CFF_SYNCMEM),
				IOCTL_INFO(IOCTL_KGSL_CFF_USER_EVENT),
				IOCTL_INFO(IOCTL_KGSL_TIMESTAMP_EVENT),
				/* no kgsl-2d specific ioctls, I don't think.. */
		},
};

static struct device_info pmem_info = {
		.name = "pmem-gpu",
		.ioctl_info = {
				IOCTL_INFO(PMEM_GET_PHYS),
				IOCTL_INFO(PMEM_MAP),
				IOCTL_INFO(PMEM_GET_SIZE),
				IOCTL_INFO(PMEM_UNMAP),
				IOCTL_INFO(PMEM_ALLOCATE),
				IOCTL_INFO(PMEM_CONNECT),
				IOCTL_INFO(PMEM_GET_TOTAL_SIZE),
				IOCTL_INFO(HW3D_REVOKE_GPU),
				IOCTL_INFO(HW3D_GRANT_GPU),
				IOCTL_INFO(HW3D_WAIT_FOR_INTERRUPT),
				IOCTL_INFO(PMEM_CLEAN_INV_CACHES),
				IOCTL_INFO(PMEM_CLEAN_CACHES),
				IOCTL_INFO(PMEM_INV_CACHES),
				IOCTL_INFO(PMEM_GET_FREE_SPACE),
				IOCTL_INFO(PMEM_ALLOCATE_ALIGNED),
		},
};

static int kgsl_3d0 = -1, kgsl_2d0 = -1, kgsl_2d1 = -1, pmem_gpu0 = -1, pmem_gpu1 = -1;


static struct device_info * get_kgsl_info(int fd)
{
	if ((fd == kgsl_2d0) || (fd == kgsl_2d1))
		return &kgsl_2d_info;
	else if (fd == kgsl_3d0)
		return &kgsl_3d_info;
	return NULL;
}

static void
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


static void dump_ioctl(struct device_info *info, int dir, int fd,
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

	printf("%c [%4d] %8s: %s (%08lx)", c, fd, info->name, name, request);
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

	if (ret != -1) {
		if (!strcmp(path, "/dev/kgsl-3d0")) {
			kgsl_3d0 = ret;
			printf("found kgsl_3d0: %d\n", kgsl_3d0);
		} else if (!strcmp(path, "/dev/kgsl-2d0")) {
			kgsl_2d0 = ret;
			printf("found kgsl_2d0: %d\n", kgsl_2d0);
		} else if (!strcmp(path, "/dev/kgsl-2d1")) {
			kgsl_2d1 = ret;
			printf("found kgsl_2d1: %d\n", kgsl_2d1);
		} else if (!strcmp(path, "/dev/pmem_gpu0")) {
			pmem_gpu0 = ret;
			printf("found pmem_gpu0: %d\n", pmem_gpu0);
		} else if (!strcmp(path, "/dev/pmem_gpu1")) {
			pmem_gpu1 = ret;
			printf("found pmem_gpu1: %d\n", pmem_gpu1);
		} else if (strstr(path, "/dev/")) {
			printf("#### missing device, path: %s: %d\n", path, ret);
		}
	}

	return ret;
}

static void kgsl_ioctl_ringbuffer_issueibcmds_pre(
		struct kgsl_ringbuffer_issueibcmds *param)
{
	int i;
	struct kgsl_ibdesc *ibdesc;
	printf("\t\tdrawctxt_id:\t%08x\n", param->drawctxt_id);
	/*
#define KGSL_CONTEXT_SAVE_GMEM	1
#define KGSL_CONTEXT_NO_GMEM_ALLOC	2
#define KGSL_CONTEXT_SUBMIT_IB_LIST	4
#define KGSL_CONTEXT_CTX_SWITCH	8
#define KGSL_CONTEXT_PREAMBLE	16

#define Z180_STREAM_PACKET_CALL 0x7C000275   <-- seems to be always first 4 bytes..

if there isn't a context switch, skip the first PACKETSIZE_STATESTREAM words:

PACKETSIZE_STATE:
	#define NUMTEXUNITS             4
	#define TEXUNITREGCOUNT         25
	#define VG_REGCOUNT             0x39

	#define PACKETSIZE_BEGIN        3
	#define PACKETSIZE_G2DCOLOR     2
	#define PACKETSIZE_TEXUNIT      (TEXUNITREGCOUNT * 2)
	#define PACKETSIZE_REG          (VG_REGCOUNT * 2)
	#define PACKETSIZE_STATE        (PACKETSIZE_TEXUNIT * NUMTEXUNITS + \
					 PACKETSIZE_REG + PACKETSIZE_BEGIN + \
					 PACKETSIZE_G2DCOLOR)

		((25 * 2) * 4 + (0x39 * 2) + 3 + 2) =>
		((25 * 2) * 4 + (57 * 2) + 3 + 2) =>
		319

PACKETSIZE_STATESTREAM:
	#define PACKETSIZE_STATESTREAM  (ALIGN((PACKETSIZE_STATE * \
					 sizeof(unsigned int)), 32) / \
					 sizeof(unsigned int))

	ALIGN((PACKETSIZE_STATE * sizeof(unsigned int)), 32) / sizeof(unsigned int) =>
	1280 / 4 =>
	320

so the context, restored on context switch, is the first: 320 (0x140) words
	*/
	printf("\t\tflags:\t\t%08x\n", param->flags);
	printf("\t\tnumibs:\t\t%08x\n", param->numibs);
	printf("\t\tibdesc_addr:\t%08x\n", param->ibdesc_addr);
	ibdesc = (struct kgsl_ibdesc *)param->ibdesc_addr;
	for (i = 0; i < param->numibs; i++) {
		// z180_cmdstream_issueibcmds or adreno_ringbuffer_issueibcmds
		printf("\t\tibdesc[%d].ctrl:\t\t%08x\n", i, ibdesc[i].ctrl);
		printf("\t\tibdesc[%d].sizedwords:\t%08x\n", i, ibdesc[i].sizedwords);
		printf("\t\tibdesc[%d].gpuaddr:\t%08x\n", i, ibdesc[i].gpuaddr);
		printf("\t\tibdesc[%d].hostptr:\t%p\n", i, ibdesc[i].hostptr);
		hexdump(ibdesc[i].hostptr, ibdesc[i].sizedwords * sizeof(unsigned int));
	}
}

static void kgsl_ioctl_ringbuffer_issueibcmds_post(
		struct kgsl_ringbuffer_issueibcmds *param)
{
	printf("\t\ttimestamp:\t%08x\n", param->timestamp);
}

static void kgsl_ioctl_drawctxt_create_pre(
		struct kgsl_drawctxt_create *param)
{
	printf("\t\tflags:\t\t%08x\n", param->flags);
}

static void kgsl_ioctl_drawctxt_create_post(
		struct kgsl_drawctxt_create *param)
{
	printf("\t\tdrawctxt_id:\t%08x\n", param->drawctxt_id);
}

#define PROP_INFO(n) [n] = #n
static const char *propnames[] = {
		PROP_INFO(KGSL_PROP_DEVICE_INFO),
		PROP_INFO(KGSL_PROP_DEVICE_SHADOW),
		PROP_INFO(KGSL_PROP_DEVICE_POWER),
		PROP_INFO(KGSL_PROP_SHMEM),
		PROP_INFO(KGSL_PROP_SHMEM_APERTURES),
		PROP_INFO(KGSL_PROP_MMU_ENABLE),
		PROP_INFO(KGSL_PROP_INTERRUPT_WAITS),
		PROP_INFO(KGSL_PROP_VERSION),
		PROP_INFO(KGSL_PROP_GPU_RESET_STAT),
};

static void kgsl_ioctl_device_getproperty_post(
		struct kgsl_device_getproperty *param)
{
	printf("\t\ttype:\t\t%08x (%s)\n", param->type, propnames[param->type]);
	hexdump(param->value, param->sizebytes);
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

static void kgsl_ioctl_pre(int fd, unsigned long int request, void *ptr)
{
	dump_ioctl(get_kgsl_info(fd), _IOC_WRITE, fd, request, ptr, 0);
	switch(_IOC_NR(request)) {
	case _IOC_NR(IOCTL_KGSL_RINGBUFFER_ISSUEIBCMDS):
		kgsl_ioctl_ringbuffer_issueibcmds_pre(ptr);
		break;
	case _IOC_NR(IOCTL_KGSL_DRAWCTXT_CREATE):
		kgsl_ioctl_drawctxt_create_pre(ptr);
		break;
	case _IOC_NR(IOCTL_KGSL_SHAREDMEM_FROM_VMALLOC):
		kgsl_ioctl_sharedmem_from_vmalloc_pre(ptr);
		break;
	case _IOC_NR(IOCTL_KGSL_SHAREDMEM_FREE):
		kgsl_ioctl_sharedmem_free(ptr);
		break;
	}
}

static void kgsl_ioctl_post(int fd, unsigned long int request, void *ptr, int ret)
{
	dump_ioctl(get_kgsl_info(fd), _IOC_READ, fd, request, ptr, ret);
	switch(_IOC_NR(request)) {
	case _IOC_NR(IOCTL_KGSL_RINGBUFFER_ISSUEIBCMDS):
		kgsl_ioctl_ringbuffer_issueibcmds_post(ptr);
		break;
	case _IOC_NR(IOCTL_KGSL_DRAWCTXT_CREATE):
		kgsl_ioctl_drawctxt_create_post(ptr);
		break;
	case _IOC_NR(IOCTL_KGSL_DEVICE_GETPROPERTY):
		kgsl_ioctl_device_getproperty_post(ptr);
		break;
	case _IOC_NR(IOCTL_KGSL_SHAREDMEM_FROM_VMALLOC):
		kgsl_ioctl_sharedmem_from_vmalloc_post(ptr);
		break;
	}
}

static void pmem_ioctl_pre(int fd, unsigned long int request, void *ptr)
{
	dump_ioctl(&pmem_info, _IOC_WRITE, fd, request, ptr, 0);
}

static void pmem_ioctl_post(int fd, unsigned long int request, void *ptr, int ret)
{
	dump_ioctl(&pmem_info, _IOC_READ, fd, request, ptr, ret);
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

	if (get_kgsl_info(fd))
		kgsl_ioctl_pre(fd, request, ptr);
	else if ((fd == pmem_gpu0) || (fd == pmem_gpu1))
		pmem_ioctl_pre(fd, request, ptr);
	else
		printf("> [%4d]         : <unknown> (%08lx)", fd, request);

	ret = orig_ioctl(fd, request, ptr);

	if (get_kgsl_info(fd))
		kgsl_ioctl_post(fd, request, ptr, ret);
	else if ((fd == pmem_gpu0) || (fd == pmem_gpu1))
		pmem_ioctl_post(fd, request, ptr, ret);
	else
		printf("< [%4d]         : <unknown> (%08lx)", fd, request);

	return ret;
}


