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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "program.h"
#include "ir-a3xx.h"
#include "ring.h"
#include "util.h"


struct fd_shader {
	uint32_t bin[256];
	uint32_t sizedwords;
	struct ir3_shader_info info;
	struct ir3_shader *ir;
};

struct fd_program {
	struct fd_shader vertex_shader, fragment_shader;
};

static struct fd_shader *get_shader(struct fd_program *program,
		enum fd_shader_type type)
{
	switch (type) {
	case FD_SHADER_VERTEX:   return &program->vertex_shader;
	case FD_SHADER_FRAGMENT: return &program->fragment_shader;
	}
	assert(0);
	return NULL;
}

struct fd_program * fd_program_new(void)
{
	return calloc(1, sizeof(struct fd_program));
}

int fd_program_attach_asm(struct fd_program *program,
		enum fd_shader_type type, const char *src)
{
	struct fd_shader *shader = get_shader(program, type);
	int sizedwords;

	if (shader->ir)
		ir3_shader_destroy(shader->ir);

	memset(shader, 0, sizeof(*shader));

	shader->ir = fd_asm_parse(src);
	if (!shader->ir) {
		ERROR_MSG("parse failed");
		return -1;
	}
	sizedwords = ir3_shader_assemble(shader->ir, shader->bin,
			ARRAY_SIZE(shader->bin), &shader->info);
	if (sizedwords <= 0) {
		ERROR_MSG("assembler failed");
		return -1;
	}
	shader->sizedwords = sizedwords;
	return 0;
}

struct ir3_sampler ** fd_program_samplers(struct fd_program *program,
		enum fd_shader_type type, int *cnt)
{
	struct fd_shader *shader = get_shader(program, type);
	*cnt = shader->ir->samplers_count;
	return shader->ir->samplers;
}

static uint32_t footprint(int32_t max)
{
	/* The register footprint is in units of vec4, ie. the registers
	 * are allocated in sets of four consecutive scalers (rN.x -> rN.w)
	 */
	if (max < 0)
		return 0;
	return (max >> 2) + 1;
}

static uint32_t instrlen(struct fd_shader *shader)
{
	/* the instructions length is in units of instruction groups
	 * (4 instructions, 8 dwords):
	 */
	return shader->sizedwords / 8;
}

static uint32_t constlen(struct fd_shader *shader)
{
	/* the constants length is in units of vec4's, and is the sum of
	 * the uniforms and the built-in compiler constants
	 */
	uint32_t i, len = shader->ir->consts_count;
	for (i = 0; i < shader->ir->uniforms_count; i++)
		len += shader->ir->uniforms[i]->num;
	return len;
}

static uint32_t totalattr(struct fd_shader *shader)
{
	uint32_t i, n = 0;
	for (i = 0; i < shader->ir->attributes_count; i++)
		n += shader->ir->attributes[i]->num;
	return n * 4;
}

static void
emit_shader(struct fd_ringbuffer *ring, struct fd_shader *shader,
		enum adreno_state_block state_block)
{
	uint32_t i;
	OUT_PKT3(ring, CP_LOAD_STATE, 2 + shader->sizedwords);
	OUT_RING(ring, CP_LOAD_STATE_0_DST_OFF(0) |
			CP_LOAD_STATE_0_STATE_SRC(SS_DIRECT) |
			CP_LOAD_STATE_0_STATE_BLOCK(state_block) |
			CP_LOAD_STATE_0_NUM_UNIT(instrlen(shader)));
	OUT_RING(ring, CP_LOAD_STATE_1_STATE_TYPE(ST_SHADER) |
			CP_LOAD_STATE_1_EXT_SRC_ADDR(0));
	for (i = 0; i < shader->sizedwords; i++)
		OUT_RING(ring, shader->bin[i]);
}

static void emit_vtx_fetch(struct fd_ringbuffer *ring,
		struct fd_shader *shader, struct fd_parameters *attr)
{
	uint32_t i;

	for (i = 0; i < shader->ir->attributes_count; i++) {
		bool switchnext = (i != (shader->ir->attributes_count - 1));
		struct ir3_attribute *a = shader->ir->attributes[i];
		struct fd_param *p = find_param(attr, a->name);
		uint32_t s = fmt2size(p->fmt);

		OUT_PKT0(ring, REG_A3XX_VFD_FETCH(i), 2);
		OUT_RING(ring, A3XX_VFD_FETCH_INSTR_0_FETCHSIZE(s - 1) |
				A3XX_VFD_FETCH_INSTR_0_BUFSTRIDE(s) |
				COND(switchnext, A3XX_VFD_FETCH_INSTR_0_SWITCHNEXT) |
				A3XX_VFD_FETCH_INSTR_0_INDEXCODE(i) |
				A3XX_VFD_FETCH_INSTR_0_STEPRATE(1));
		OUT_RELOC(ring, p->bo, 0, 0);   /* VFD_FETCH[i].INSTR_1 */

		OUT_PKT0(ring, REG_A3XX_VFD_DECODE_INSTR(i), 1);
		OUT_RING(ring, A3XX_VFD_DECODE_INSTR_WRITEMASK(0xf) |
				A3XX_VFD_DECODE_INSTR_CONSTFILL |
				A3XX_VFD_DECODE_INSTR_FORMAT(p->fmt) |
				A3XX_VFD_DECODE_INSTR_REGID(a->rstart->num) |
				A3XX_VFD_DECODE_INSTR_SHIFTCNT(s) |
				A3XX_VFD_DECODE_INSTR_LASTCOMPVALID |
				COND(switchnext, A3XX_VFD_DECODE_INSTR_SWITCHNEXT));
	}
}

static void emit_uniconst(struct fd_ringbuffer *ring,
		struct fd_shader *shader, struct fd_parameters *uniforms,
		enum adreno_state_block state_block)
{
	static uint32_t buf[512]; /* cheesy, but test code isn't multithreaded */
	uint32_t i, j, k, sz = 0;

	memset(buf, 0, sizeof(buf));

	for (i = 0; i < shader->ir->consts_count; i++) {
		struct ir3_const *c = shader->ir->consts[i];
		uint32_t off = c->cstart->num;
		memcpy(&buf[off], c->val, sizeof(c->val));
		sz = max(sz, off + (sizeof(c->val) / sizeof(buf[0])));
	}

	for (i = 0; i < shader->ir->uniforms_count; i++) {
		struct ir3_uniform *u = shader->ir->uniforms[i];
		struct fd_param *p = find_param(uniforms, u->name);
		const uint32_t *dwords = p->data;
		uint32_t off = u->cstart->num;

		for (j = 0; j < p->count; j++) {
			for (k = 0; k < p->size; k++)
				buf[off++] = *(dwords++);
			/* zero pad if needed: */
			for (; k < ALIGN(p->size, 4); k++)
				buf[off++] = 0;
		}

		sz = max(sz, off);
	}

	/* if no constants, don't emit the CP_LOAD_STATE */
	if (sz == 0)
		return;

	OUT_PKT3(ring, CP_LOAD_STATE, 2 + sz);
	OUT_RING(ring, CP_LOAD_STATE_0_DST_OFF(0) |
			CP_LOAD_STATE_0_STATE_SRC(SS_DIRECT) |
			CP_LOAD_STATE_0_STATE_BLOCK(state_block) |
			CP_LOAD_STATE_0_NUM_UNIT(sz/2));
	OUT_RING(ring, CP_LOAD_STATE_1_STATE_TYPE(ST_CONSTANTS) |
			CP_LOAD_STATE_1_EXT_SRC_ADDR(0));
	for (i = 0; i < sz; i++)
		OUT_RING(ring, buf[i]);
}

void fd_program_emit_state(struct fd_program *program,
		struct fd_parameters *uniforms, struct fd_parameters *attr,
		struct fd_ringbuffer *ring)
{
	struct fd_shader *vs = get_shader(program, FD_SHADER_VERTEX);
	struct fd_shader *fs = get_shader(program, FD_SHADER_FRAGMENT);
	struct ir3_shader_info *vsi = &vs->info;
	struct ir3_shader_info *fsi = &fs->info;
	uint32_t vsconstlen = constlen(vs);
	uint32_t fsconstlen = constlen(fs);
	int i;

	// TODO don't hard-code gl_Position to r0.x:
	uint32_t posregid = 0;

	// TODO don't hard-code gl_PointSize to r63.x:
	uint32_t psizeregid = (63 << 2);

	// TODO don't hard-code gl_FragColor to r0.x:
	uint32_t colorregid = 0;

	// TODO figure out # of register varying slots (ie. a vec4 -> 4, float -> 1, mat4 -> 16):
	uint32_t numvar = 0;

	assert (vs->ir->varyings_count == fs->ir->varyings_count);

	OUT_PKT0(ring, REG_A3XX_HLSQ_CONTROL_0_REG, 6);
	OUT_RING(ring, A3XX_HLSQ_CONTROL_0_REG_FSTHREADSIZE(FOUR_QUADS) |
			A3XX_HLSQ_CONTROL_0_REG_SPSHADERRESTART |
			A3XX_HLSQ_CONTROL_0_REG_SPCONSTFULLUPDATE);
	OUT_RING(ring, A3XX_HLSQ_CONTROL_1_REG_VSTHREADSIZE(TWO_QUADS) |
			A3XX_HLSQ_CONTROL_1_REG_VSSUPERTHREADENABLE);
	OUT_RING(ring, A3XX_HLSQ_CONTROL_2_REG_PRIMALLOCTHRESHOLD(31));
	OUT_RING(ring, 0x00000000);        /* HLSQ_CONTROL_3_REG */
	OUT_RING(ring, A3XX_HLSQ_VS_CONTROL_REG_CONSTLENGTH(vsconstlen) |
			A3XX_HLSQ_VS_CONTROL_REG_CONSTSTARTOFFSET(0) |
			A3XX_HLSQ_VS_CONTROL_REG_INSTRLENGTH(instrlen(vs)));
	OUT_RING(ring, A3XX_HLSQ_FS_CONTROL_REG_CONSTLENGTH(fsconstlen) |
			A3XX_HLSQ_FS_CONTROL_REG_CONSTSTARTOFFSET(128) |
			A3XX_HLSQ_FS_CONTROL_REG_INSTRLENGTH(instrlen(fs)));

	OUT_PKT0(ring, REG_A3XX_SP_SP_CTRL_REG, 1);
	OUT_RING(ring, A3XX_SP_SP_CTRL_REG_CONSTMODE(0) |
			A3XX_SP_SP_CTRL_REG_SLEEPMODE(1) |
			// XXX sometimes 0, sometimes 1:
			A3XX_SP_SP_CTRL_REG_LOMODE(1));

	/* emit unknown sequence of writes to 0x0ec4/0x0ec8 that the blob
	 * emits as part of the program state (it seems)..
	 */
	for (i = 0; i < 6; i++) {
		OUT_PKT0(ring, REG_A3XX_UNKNOWN_0EC4, 1);
		OUT_RING(ring, 0x00000000);    /* UNKNOWN_0EC4 */

		OUT_PKT0(ring, REG_A3XX_UNKNOWN_0EC8, 1);
		OUT_RING(ring, 0x00000000);    /* UNKNOWN_0EC8 */
	}

	OUT_PKT0(ring, REG_A3XX_SP_VS_LENGTH_REG, 1);
	OUT_RING(ring, A3XX_SP_VS_LENGTH_REG_SHADERLENGTH(instrlen(vs)));

	OUT_PKT0(ring, REG_A3XX_SP_VS_CTRL_REG0, 3);
	OUT_RING(ring, A3XX_SP_VS_CTRL_REG0_THREADMODE(MULTI) |
			A3XX_SP_VS_CTRL_REG0_INSTRBUFFERMODE(BUFFER) |
			A3XX_SP_VS_CTRL_REG0_HALFREGFOOTPRINT(footprint(vsi->max_half_reg)) |
			A3XX_SP_VS_CTRL_REG0_FULLREGFOOTPRINT(footprint(vsi->max_reg)) |
			A3XX_SP_VS_CTRL_REG0_INOUTREGOVERLAP(0) |
			A3XX_SP_VS_CTRL_REG0_THREADSIZE(TWO_QUADS) |
			A3XX_SP_VS_CTRL_REG0_SUPERTHREADMODE |
			A3XX_SP_VS_CTRL_REG0_LENGTH(instrlen(vs)));
	OUT_RING(ring, A3XX_SP_VS_CTRL_REG1_CONSTLENGTH(vsconstlen) |
			A3XX_SP_VS_CTRL_REG1_INITIALOUTSTANDING(0) |
			A3XX_SP_VS_CTRL_REG1_HALFPRECVAROFFSET(4));
	OUT_RING(ring, A3XX_SP_VS_PARAM_REG_POSREGID(posregid) |
			A3XX_SP_VS_PARAM_REG_PSIZEREGID(psizeregid) |
			A3XX_SP_VS_PARAM_REG_TOTALVSOUTVAR(fs->ir->varyings_count));

	// TODO set SP_VS_OUT[0-8] properly based on varying positions
	// TODO we need to set SP_VS_VPC_DST[0-4] properly based on varying position/sizes
	// TODO SP_VS_OBJ_OFFSET_REG / SP_VS_OBJ_START_REG

	OUT_PKT0(ring, REG_A3XX_SP_FS_LENGTH_REG, 1);
	OUT_RING(ring, A3XX_SP_FS_LENGTH_REG_SHADERLENGTH(instrlen(fs)));

	OUT_PKT0(ring, REG_A3XX_SP_FS_CTRL_REG0, 2);
	OUT_RING(ring, A3XX_SP_FS_CTRL_REG0_THREADMODE(MULTI) |
			A3XX_SP_FS_CTRL_REG0_INSTRBUFFERMODE(BUFFER) |
			A3XX_SP_FS_CTRL_REG0_HALFREGFOOTPRINT(footprint(fsi->max_half_reg)) |
			A3XX_SP_FS_CTRL_REG0_FULLREGFOOTPRINT(footprint(fsi->max_reg)) |
			A3XX_SP_FS_CTRL_REG0_INOUTREGOVERLAP(1) |
			A3XX_SP_FS_CTRL_REG0_THREADSIZE(FOUR_QUADS) |
			A3XX_SP_FS_CTRL_REG0_SUPERTHREADMODE |
			A3XX_SP_FS_CTRL_REG0_LENGTH(instrlen(fs)));
	OUT_RING(ring, A3XX_SP_FS_CTRL_REG1_CONSTLENGTH(fsconstlen) |
			A3XX_SP_FS_CTRL_REG1_INITIALOUTSTANDING(0) |
			A3XX_SP_FS_CTRL_REG1_HALFPRECVAROFFSET(63));

	// TODO SP_FS_OBJ_OFFSET_REG / SP_FS_OBJ_START_REG

	OUT_PKT0(ring, REG_A3XX_SP_FS_FLAT_SHAD_MODE_REG_0, 2);
	OUT_RING(ring, 0x00000000);        /* SP_FS_FLAT_SHAD_MODE_REG_0 */
	OUT_RING(ring, 0x00000000);        /* SP_FS_FLAT_SHAD_MODE_REG_1 */

	OUT_PKT0(ring, REG_A3XX_SP_FS_OUTPUT_REG, 1);
	OUT_RING(ring, 0x00000000);        /* SP_FS_OUTPUT_REG */

	OUT_PKT0(ring, REG_A3XX_SP_FS_MRT_REG(0), 4);
	OUT_RING(ring, A3XX_SP_FS_MRT_REG_REGID(colorregid) |  /* SP_FS_MRT[0].REG */
			A3XX_SP_FS_MRT_REG_PRECISION(1));
	OUT_RING(ring, A3XX_SP_FS_MRT_REG_REGID(0) |           /* SP_FS_MRT[1].REG */
			A3XX_SP_FS_MRT_REG_PRECISION(0));
	OUT_RING(ring, A3XX_SP_FS_MRT_REG_REGID(0) |           /* SP_FS_MRT[2].REG */
			A3XX_SP_FS_MRT_REG_PRECISION(0));
	OUT_RING(ring, A3XX_SP_FS_MRT_REG_REGID(0) |           /* SP_FS_MRT[3].REG */
			A3XX_SP_FS_MRT_REG_PRECISION(0));

	OUT_PKT0(ring, REG_A3XX_VPC_ATTR, 2);
	OUT_RING(ring, A3XX_VPC_ATTR_TOTALATTR(numvar) |
			A3XX_VPC_ATTR_THRDASSIGN(1) |
			A3XX_VPC_ATTR_LMSIZE(1));
	OUT_RING(ring, A3XX_VPC_PACK_NUMFPNONPOSVAR(numvar) |
			A3XX_VPC_PACK_NUMNONPOSVSVAR(numvar));

	OUT_PKT0(ring, REG_A3XX_VPC_VARYING_INTERP_MODE(0), 4);
	OUT_RING(ring, 0x00000000);        /* VPC_VARYING_INTERP[0].MODE */
	OUT_RING(ring, 0x00000000);        /* VPC_VARYING_INTERP[1].MODE */
	OUT_RING(ring, 0x00000000);        /* VPC_VARYING_INTERP[2].MODE */
	OUT_RING(ring, 0x00000000);        /* VPC_VARYING_INTERP[3].MODE */

	OUT_PKT0(ring, REG_A3XX_VPC_VARYING_PS_REPL_MODE(0), 4);
	OUT_RING(ring, 0x00000000);        /* VPC_VARYING_PS_REPL[0].MODE */
	OUT_RING(ring, 0x00000000);        /* VPC_VARYING_PS_REPL[1].MODE */
	OUT_RING(ring, 0x00000000);        /* VPC_VARYING_PS_REPL[2].MODE */
	OUT_RING(ring, 0x00000000);        /* VPC_VARYING_PS_REPL[3].MODE */

	OUT_PKT0(ring, REG_A3XX_VFD_VS_THREADING_THRESHOLD, 1);
	OUT_RING(ring, A3XX_VFD_VS_THREADING_THRESHOLD_REGID_THRESHOLD(15) |
			A3XX_VFD_VS_THREADING_THRESHOLD_REGID_VTXCNT(252));

	emit_shader(ring, vs, SB_VERTEX);

	OUT_PKT0(ring, REG_A3XX_VFD_PERFCOUNTER0_SELECT, 1);
	OUT_RING(ring, 0x00000000);        /* VFD_PERFCOUNTER0_SELECT */

	emit_shader(ring, fs, SB_FRAGMENT);

	OUT_PKT0(ring, REG_A3XX_VFD_PERFCOUNTER0_SELECT, 1);
	OUT_RING(ring, 0x00000000);        /* VFD_PERFCOUNTER0_SELECT */

	OUT_PKT0(ring, REG_A3XX_VFD_CONTROL_0, 2);
	OUT_RING(ring, A3XX_VFD_CONTROL_0_TOTALATTRTOVS(totalattr(vs)) |
			A3XX_VFD_CONTROL_0_PACKETSIZE(2) |
			A3XX_VFD_CONTROL_0_STRMDECINSTRCNT(attr->nparams) |
			A3XX_VFD_CONTROL_0_STRMFETCHINSTRCNT(attr->nparams));
	OUT_RING(ring, A3XX_VFD_CONTROL_1_MAXSTORAGE(1) | // XXX
			A3XX_VFD_CONTROL_1_REGID4VTX(63 << 2) |
			A3XX_VFD_CONTROL_1_REGID4INST(63 << 2));

	emit_vtx_fetch(ring, vs, attr);

	/* we have this sometimes, not others.. perhaps we could be clever
	 * and figure out actually when we need to invalidate cache:
	 */
	OUT_PKT0(ring, REG_A3XX_UCHE_CACHE_INVALIDATE0_REG, 2);
	OUT_RING(ring, A3XX_UCHE_CACHE_INVALIDATE0_REG_ADDR(0));
	OUT_RING(ring, A3XX_UCHE_CACHE_INVALIDATE1_REG_ADDR(0) |
			A3XX_UCHE_CACHE_INVALIDATE1_REG_OPCODE(INVALIDATE) |
			A3XX_UCHE_CACHE_INVALIDATE1_REG_ENTIRE_CACHE);

	emit_uniconst(ring, vs, uniforms, SB_VERTEX);
	emit_uniconst(ring, fs, uniforms, SB_FRAGMENT);
}
