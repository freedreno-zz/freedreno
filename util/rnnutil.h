/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright (C) 2014 Rob Clark <robclark@freedesktop.org>
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
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef RNNUTIL_H_
#define RNNUTIL_H_

#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "rnn.h"
#include "rnndec.h"

struct rnn {
	struct rnndb *db;
	struct rnndeccontext *vc, *vc_nocolor;
	struct rnndomain *dom[2];
};

struct rnn *rnn_new(int nocolor);
void rnn_load(struct rnn *rnn, const char *gpuname);
const char *rnn_regname(struct rnn *rnn, uint32_t regbase, int color);
struct rnndecaddrinfo *rnn_reginfo(struct rnn *rnn, uint32_t regbase);
const char *rnn_enumname(struct rnn *rnn, const char *name, uint32_t val);

#endif /* RNNUTIL_H_ */
