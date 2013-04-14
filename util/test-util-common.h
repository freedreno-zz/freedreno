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

#ifndef TEST_UTIL_COMMON_H_
#define TEST_UTIL_COMMON_H_

#include <stdarg.h>
#include <stddef.h>
#include "bmp.h"
#include "redump.h"

void exit(int status);
int printf(const char *,...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
void *calloc(size_t nmemb, size_t size);
void *malloc(size_t size);
void free(void *ptr);
size_t strlen(const char *s);

typedef struct
{
	int quot;                   /* Quotient.  */
	int rem;                    /* Remainder.  */
} div_t;

div_t div(int numerator, int denominator);
void sincos(double x, double *sin, double *cos);
void sincosf(float x, float *sin, float *cos);
void sincosl(long double x, long double *sin, long double *cos);
void *memcpy(void *dest, const void *src, size_t n);


/*****************************************************************************/

#define DEBUG_MSG(fmt, ...) \
		do { \
			static char __rd_buf[4096]; \
			if (rd_write_section) \
			rd_write_section(RD_CMD, __rd_buf, snprintf(__rd_buf, sizeof(__rd_buf), "%s:%d: "fmt, \
							__FUNCTION__, __LINE__, ##__VA_ARGS__)); \
			printf(fmt " (%s:%d)\n", \
					##__VA_ARGS__, __FUNCTION__, __LINE__); \
		} while (0)
#define ERROR_MSG(fmt, ...) \
		do { printf("ERROR: " fmt " (%s:%d)\n", \
				##__VA_ARGS__, __FUNCTION__, __LINE__); } while (0)

#define FIXED(v)   ((unsigned int) ((v) << 16))
#define ALIGN(v,a) (((v) + (a) - 1) & ~((a) - 1))

#endif /* TEST_UTIL_COMMON_H_ */
