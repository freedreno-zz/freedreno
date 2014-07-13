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


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <assert.h>

#include "script.h"

static lua_State *L;

/* does not return */
static void error(const char *fmt)
{
	fprintf(stderr, fmt, lua_tostring(L, -1));
	exit(1);
}

/* Expose the register state to script enviroment as a "regs" library:
 */

uint32_t reg_written(uint32_t regbase);
uint32_t reg_lastval(uint32_t regbase);
uint32_t reg_val(uint32_t regbase);

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
