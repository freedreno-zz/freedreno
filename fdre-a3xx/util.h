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

#ifndef UTIL_H_
#define UTIL_H_

#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * Return float bits.
 */
static inline uint32_t fui(float f)
{
	union {
		float f;
		uint32_t ui;
	} fi;
	fi.f = f;
	return fi.ui;
}

#include "adreno_common.xml.h"
#include "adreno_pm4.xml.h"
#include "a3xx.xml.h"

#define CP_REG(reg) ((0x4 << 16) | ((unsigned int)((reg) - (0x2000))))

/* for conditionally setting boolean flag(s): */
#define COND(bool, val) ((bool) ? (val) : 0)

static inline uint32_t DRAW(enum pc_di_primtype prim_type,
		enum pc_di_src_sel source_select, enum pc_di_index_size index_size,
		enum pc_di_vis_cull_mode vis_cull_mode)
{
	return (prim_type         << 0) |
			(source_select     << 6) |
			((index_size & 1)  << 11) |
			((index_size >> 1) << 13) |
			(vis_cull_mode     << 9) |
			(1                 << 14);
}

/* convert x,y to dword */
static inline uint32_t xy2d(uint16_t x, uint16_t y)
{
	return ((y & 0x3fff) << 16) | (x & 0x3fff);
}

#define enable_debug 0  /* TODO make dynamic */

#define ALIGN(v,a) (((v) + (a) - 1) & ~((a) - 1))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define INFO_MSG(fmt, ...) \
		do { printf("[I] "fmt " (%s:%d)\n", \
				##__VA_ARGS__, __FUNCTION__, __LINE__); } while (0)
#define DEBUG_MSG(fmt, ...) \
		do if (enable_debug) { printf("[D] "fmt " (%s:%d)\n", \
				##__VA_ARGS__, __FUNCTION__, __LINE__); } while (0)
#define WARN_MSG(fmt, ...) \
		do { printf("[W] "fmt " (%s:%d)\n", \
				##__VA_ARGS__, __FUNCTION__, __LINE__); } while (0)
#define ERROR_MSG(fmt, ...) \
		do { printf("[E] " fmt " (%s:%d)\n", \
				##__VA_ARGS__, __FUNCTION__, __LINE__); } while (0)

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))



/* ************************************************************************* */

struct fd_param {
	const char *name;
	union {
		struct {                  /* attributes */
			struct fd_bo *bo;
			enum a3xx_fmt fmt;
		};
		struct fd_surface *tex;   /* textures */
		struct {                  /* uniforms */
			/* user ptr and dimensions for passed in uniform:
			 *   elem_size - size of individual element in bytes
			 *   size      - number of elements per group
			 *   count     - number of groups
			 * so total size in bytes is elem_size * size * count
			 */
			const void *data;
			uint32_t elem_size, size, count;
		};
	};
};

#define MAX_PARAMS 32
struct fd_parameters {
	struct fd_param params[MAX_PARAMS];
	uint32_t nparams;
};

static inline struct fd_param * find_param(struct fd_parameters *params,
		const char *name)
{
	uint32_t i;
	struct fd_param *p;

	/* if this param is already bound, just update it: */
	for (i = 0; i < params->nparams; i++) {
		p = &params->params[i];
		if (!strcmp(name, p->name)) {
			return p;
		}
	}

	if (i == ARRAY_SIZE(params->params)) {
		ERROR_MSG("too many params, cannot bind %s", name);
		return NULL;
	}

	p = &params->params[params->nparams++];
	p->name = name;

	return p;
}

static inline uint32_t fmt2size(enum a3xx_fmt fmt)
{
	switch (fmt) {
	case FMT_UBYTE_8:
	case FMT_NORM_UBYTE_8:
	case FMT_BYTE_8:
	case FMT_NORM_BYTE_8:
		return 1;

	case FMT_SHORT_16:
	case FMT_USHORT_16:
	case FMT_NORM_SHORT_16:
	case FMT_NORM_USHORT_16:
	case FMT_UBYTE_8_8:
	case FMT_NORM_UBYTE_8_8:
	case FMT_BYTE_8_8:
	case FMT_NORM_BYTE_8_8:
		return 2;

	case FMT_UBYTE_8_8_8:
	case FMT_NORM_UBYTE_8_8_8:
	case FMT_BYTE_8_8_8:
	case FMT_NORM_BYTE_8_8_8:
		return 3;

	case FMT_FLOAT_32:
	case FMT_FIXED_32:
	case FMT_SHORT_16_16:
	case FMT_USHORT_16_16:
	case FMT_NORM_SHORT_16_16:
	case FMT_NORM_USHORT_16_16:
	case FMT_UBYTE_8_8_8_8:
	case FMT_NORM_UBYTE_8_8_8_8:
	case FMT_BYTE_8_8_8_8:
	case FMT_NORM_BYTE_8_8_8_8:
		return 4;

	case FMT_SHORT_16_16_16:
	case FMT_USHORT_16_16_16:
	case FMT_NORM_SHORT_16_16_16:
	case FMT_NORM_USHORT_16_16_16:
		return 6;

	case FMT_FLOAT_32_32:
	case FMT_FIXED_32_32:
	case FMT_SHORT_16_16_16_16:
	case FMT_USHORT_16_16_16_16:
	case FMT_NORM_SHORT_16_16_16_16:
	case FMT_NORM_USHORT_16_16_16_16:
		return 8;

	case FMT_FLOAT_32_32_32:
	case FMT_FIXED_32_32_32:
		return 12;

	case FMT_FLOAT_32_32_32_32:
	case FMT_FIXED_32_32_32_32:
		return 16;

	default:
		assert(0); /* invalid format */
		return 0;
	}
}

#endif /* UTIL_H_ */
