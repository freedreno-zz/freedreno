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

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <assert.h>

#include "script.h"
#include "rnnutil.h"

static lua_State *L;

#if 0
#define DBG(fmt, ...) \
		do { printf(" ** %s:%d ** "fmt "\n", \
				__FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)
#else
#define DBG(fmt, ...) do {} while (0)
#endif

uint32_t reg_written(uint32_t regbase);
uint32_t reg_lastval(uint32_t regbase);
uint32_t reg_val(uint32_t regbase);


/* does not return */
static void error(const char *fmt)
{
	fprintf(stderr, fmt, lua_tostring(L, -1));
	exit(1);
}

/* Expose rnn decode to script environment as "rnn" library:
 */

struct rnndoff {
	struct rnn *rnn;
	struct rnndelem *elem;
	uint64_t offset;
};

static void push_rnndoff(lua_State *L, struct rnn *rnn,
		struct rnndelem *elem, uint64_t offset)
{
	struct rnndoff *rnndoff = lua_newuserdata(L, sizeof(*rnndoff));
	rnndoff->rnn = rnn;
	rnndoff->elem = elem;
	rnndoff->offset = offset;
}

static int l_rnn_etype_array(lua_State *L, struct rnn *rnn,
		struct rnndelem *elem, uint64_t offset);
static int l_rnn_etype_reg(lua_State *L, struct rnn *rnn,
		struct rnndelem *elem, uint64_t offset);

static int pushdecval(struct lua_State *L, struct rnn *rnn,
		uint32_t regval, struct rnntypeinfo *info)
{
	union rnndecval val;
	switch (rnn_decodelem(rnn, info, regval, &val)) {
	case RNN_TTYPE_INT:
	case RNN_TTYPE_ENUM:
	case RNN_TTYPE_INLINE_ENUM:
		lua_pushinteger(L, val.i);
		return 1;
	case RNN_TTYPE_UINT:
	case RNN_TTYPE_HEX:
		lua_pushunsigned(L, val.u);
		return 1;
	case RNN_TTYPE_FLOAT:
		lua_pushnumber(L, val.f);
		return 1;
	case RNN_TTYPE_BOOLEAN:
		lua_pushboolean(L, val.u);
		return 1;
	case RNN_TTYPE_INVALID:
	default:
		return 0;
	}

}

static int l_rnn_etype(lua_State *L, struct rnn *rnn,
		struct rnndelem *elem, uint64_t offset)
{
	int ret;
	DBG("elem=%p (%d), offset=%lu", elem, elem->type, offset);
	switch (elem->type) {
	case RNN_ETYPE_REG:
		/* if a register has no bitfields, just return
		 * the raw value:
		 */
		ret = pushdecval(L, rnn, reg_val(offset), &elem->typeinfo);
		if (ret)
			return ret;
		return l_rnn_etype_reg(L, rnn, elem, offset);
	case RNN_ETYPE_ARRAY:
		return l_rnn_etype_array(L, rnn, elem, offset);
	default:
		/* hmm.. */
		printf("unhandled type: %d\n", elem->type);
		return 0;
	}
}

/*
 * Struct Object:
 * To implement stuff like 'RB_MRT[n].CONTROL' we need a struct-object
 * to represent the current array index (ie. 'RB_MRT[n]')
 */

static int l_rnn_struct_meta_index(lua_State *L)
{
	struct rnndoff *rnndoff = lua_touserdata(L, 1);
	const char *name = lua_tostring(L, 2);
	struct rnndelem *elem = rnndoff->elem;
	int i;

	for (i = 0; i < elem->subelemsnum; i++) {
		struct rnndelem *subelem = elem->subelems[i];
		if (!strcmp(name, subelem->name)) {
			return l_rnn_etype(L, rnndoff->rnn, subelem,
					rnndoff->offset + subelem->offset);
		}
	}

	return 0;
}

static const struct luaL_Reg l_meta_rnn_struct[] = {
	{"__index", l_rnn_struct_meta_index},
	{NULL, NULL}  /* sentinel */
};

static int l_rnn_etype_struct(lua_State *L, struct rnn *rnn,
		struct rnndelem *elem, uint64_t offset)
{
	push_rnndoff(L, rnn, elem, offset);

	luaL_newmetatable(L, "rnnmetastruct");
	luaL_setfuncs(L, l_meta_rnn_struct, 0);
	lua_pop(L, 1);

	luaL_setmetatable(L, "rnnmetastruct");

	return 1;
}

/*
 * Array Object:
 */

static int l_rnn_array_meta_index(lua_State *L)
{
	struct rnndoff *rnndoff = lua_touserdata(L, 1);
	int idx = lua_tointeger(L, 2);
	struct rnndelem *elem = rnndoff->elem;
	uint64_t offset = rnndoff->offset + (elem->stride * idx);

	DBG("rnndoff=%p, idx=%d, numsubelems=%d",
			rnndoff, idx, rnndoff->elem->subelemsnum);

	/* if just a single sub-element, it is directly a register,
	 * otherwise we need to accumulate the array index while
	 * we wait for the register name within the array..
	 */
	if (elem->subelemsnum == 1) {
		return l_rnn_etype(L, rnndoff->rnn, elem->subelems[0], offset);
	} else {
		return l_rnn_etype_struct(L, rnndoff->rnn, elem, offset);
	}

	return 0;
}

static const struct luaL_Reg l_meta_rnn_array[] = {
	{"__index", l_rnn_array_meta_index},
	{NULL, NULL}  /* sentinel */
};

static int l_rnn_etype_array(lua_State *L, struct rnn *rnn,
		struct rnndelem *elem, uint64_t offset)
{
	push_rnndoff(L, rnn, elem, offset);

	luaL_newmetatable(L, "rnnmetaarray");
	luaL_setfuncs(L, l_meta_rnn_array, 0);
	lua_pop(L, 1);

	luaL_setmetatable(L, "rnnmetaarray");

	return 1;
}

/*
 * Register element:
 */

static int l_rnn_reg_meta_index(lua_State *L)
{
	struct rnndoff *rnndoff = lua_touserdata(L, 1);
	const char *name = lua_tostring(L, 2);
	struct rnndelem *elem = rnndoff->elem;
	struct rnntypeinfo *info = &elem->typeinfo;
	struct rnnbitfield **bitfields;
	int bitfieldsnum;
	int i;

	switch (info->type) {
	case RNN_TTYPE_BITSET:
		bitfields = info->ebitset->bitfields;
		bitfieldsnum = info->ebitset->bitfieldsnum;
		break;
	case RNN_TTYPE_INLINE_BITSET:
		bitfields = info->bitfields;
		bitfieldsnum = info->bitfieldsnum;
		break;
	default:
		printf("invalid register type: %d\n", info->type);
		return 0;
	}

	for (i = 0; i < bitfieldsnum; i++) {
		struct rnnbitfield *bf = bitfields[i];
		if (!strcmp(name, bf->name)) {
			uint32_t regval = (reg_val(rnndoff->offset) & bf->mask) >> bf->low;

			DBG("name=%s, info=%p, subelemsnum=%d, type=%d, regval=%x",
					name, info, rnndoff->elem->subelemsnum,
					bf->typeinfo.type, regval);

			return pushdecval(L, rnndoff->rnn, regval, &bf->typeinfo);
		}
	}

	printf("invalid member: %s\n", name);
	return 0;
}

static const struct luaL_Reg l_meta_rnn_reg[] = {
	{"__index", l_rnn_reg_meta_index},
	{NULL, NULL}  /* sentinel */
};

static int l_rnn_etype_reg(lua_State *L, struct rnn *rnn,
		struct rnndelem *elem, uint64_t offset)
{
	push_rnndoff(L, rnn, elem, offset);

	luaL_newmetatable(L, "rnnmetareg");
	luaL_setfuncs(L, l_meta_rnn_reg, 0);
	lua_pop(L, 1);

	luaL_setmetatable(L, "rnnmetareg");

	return 1;
}

/*
 *
 */

static int l_rnn_meta_index(lua_State *L)
{
	struct rnn *rnn = lua_touserdata(L, 1);
	const char *name = lua_tostring(L, 2);
	struct rnndelem *elem;

	elem = rnn_regelem(rnn, name);
	if (!elem)
		return 0;

	return l_rnn_etype(L, rnn, elem, elem->offset);
}

static int l_rnn_meta_gc(lua_State *L)
{
	// TODO
	//struct rnn *rnn = lua_touserdata(L, 1);
	//rnn_deinit(rnn);
	return 0;
}

static const struct luaL_Reg l_meta_rnn[] = {
	{"__index", l_rnn_meta_index},
	{"__gc", l_rnn_meta_gc},
	{NULL, NULL}  /* sentinel */
};

static int l_rnn_init(lua_State *L)
{
	const char *gpuname = lua_tostring(L, 1);
	struct rnn *rnn = lua_newuserdata(L, sizeof(*rnn));
	_rnn_init(rnn, 0);
	rnn_load(rnn, gpuname);

	luaL_newmetatable(L, "rnnmeta");
	luaL_setfuncs(L, l_meta_rnn, 0);
	lua_pop(L, 1);

	luaL_setmetatable(L, "rnnmeta");

	return 1;
}

static int l_rnn_enumname(lua_State *L)
{
	struct rnn *rnn = lua_touserdata(L, 1);
	const char *name = lua_tostring(L, 2);
	uint32_t val = (uint32_t)lua_tonumber(L, 3);
	lua_pushstring(L, rnn_enumname(rnn, name, val));
	return 1;
}

static int l_rnn_regname(lua_State *L)
{
	struct rnn *rnn = lua_touserdata(L, 1);
	uint32_t regbase = (uint32_t)lua_tonumber(L, 2);
	lua_pushstring(L, rnn_regname(rnn, regbase, 1));
	return 1;
}

static int l_rnn_regval(lua_State *L)
{
	struct rnn *rnn = lua_touserdata(L, 1);
	uint32_t regbase = (uint32_t)lua_tonumber(L, 2);
	uint32_t regval = (uint32_t)lua_tonumber(L, 3);
	struct rnndecaddrinfo *info = rnn_reginfo(rnn, regbase);
	char *decoded;
	if (info && info->typeinfo) {
		decoded = rnndec_decodeval(rnn->vc, info->typeinfo, regval, info->width);
	} else {
		asprintf(&decoded, "%08x", regval);
	}
	lua_pushstring(L, decoded);
	free(decoded);
	if (info) {
		free(info->name);
		free(info);
	}
	return 1;
}

static const struct luaL_Reg l_rnn[] = {
	{"init", l_rnn_init},
	{"enumname", l_rnn_enumname},
	{"regname", l_rnn_regname},
	{"regval", l_rnn_regval},
	{NULL, NULL}  /* sentinel */
};



/* Expose the register state to script enviroment as a "regs" library:
 */

static int l_reg_written(lua_State *L)
{
	uint32_t regbase = (uint32_t)lua_tonumber(L, 1);
	lua_pushnumber(L, reg_written(regbase));
	return 1;
}

static int l_reg_lastval(lua_State *L)
{
	uint32_t regbase = (uint32_t)lua_tonumber(L, 1);
	lua_pushnumber(L, reg_lastval(regbase));
	return 1;
}

static int l_reg_val(lua_State *L)
{
	uint32_t regbase = (uint32_t)lua_tonumber(L, 1);
	lua_pushnumber(L, reg_val(regbase));
	return 1;
}

static const struct luaL_Reg l_regs[] = {
	{"written", l_reg_written},
	{"lastval", l_reg_lastval},
	{"val",     l_reg_val},
	{NULL, NULL}  /* sentinel */
};

/* called at start to load the script: */
int script_load(const char *file)
{
	int ret;

	assert(!L);

	L = luaL_newstate();
	luaL_openlibs(L);
	luaL_openlib(L, "regs", l_regs, 0);
	luaL_openlib(L, "rnn", l_rnn, 0);

	ret = luaL_loadfile(L, file);
	if (ret)
		error("%s\n");

	ret = lua_pcall(L, 0, LUA_MULTRET, 0);
	if (ret)
		error("%s\n");

	return 0;
}


/* called at start of each cmdstream file: */
void script_start_cmdstream(const char *name)
{
	if (!L)
		return;

	lua_getglobal(L, "start_cmdstream");
	lua_pushstring(L, name);

	/* do the call (1 arguments, 0 result) */
	if (lua_pcall(L, 1, 0, 0) != 0)
		error("error running function `f': %s\n");
}

/* called at each DRAW_INDX, calls script drawidx fxn to process
 * the current state
 */
void script_draw(const char *primtype, uint32_t nindx)
{
	if (!L)
		return;

	lua_getglobal(L, "draw");
	lua_pushstring(L, primtype);
	lua_pushnumber(L, nindx);

	/* do the call (2 arguments, 0 result) */
	if (lua_pcall(L, 2, 0, 0) != 0)
		error("error running function `f': %s\n");
}

/* maybe at some point it is interesting to add additional script
 * hooks for CP_EVENT_WRITE, etc?
 */

/* called at end of each cmdstream file: */
void script_end_cmdstream(void)
{
	if (!L)
		return;

	lua_getglobal(L, "end_cmdstream");

	/* do the call (0 arguments, 0 result) */
	if (lua_pcall(L, 0, 0, 0) != 0)
		error("error running function `f': %s\n");
}

/* called after last cmdstream file: */
void script_finish(void)
{
	if (!L)
		return;

	lua_getglobal(L, "finish");

	/* do the call (0 arguments, 0 result) */
	if (lua_pcall(L, 0, 0, 0) != 0)
		error("error running function `f': %s\n");

	lua_close(L);
	L = NULL;
}
