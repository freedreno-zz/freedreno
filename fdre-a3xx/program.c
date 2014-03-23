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
#include "freedreno.h"
#include "ir-a3xx.h"
#include "ring.h"
#include "util.h"


struct fd_shader {
	uint32_t bin[512];
	uint32_t sizedwords;
	struct fd_bo *bo;
	struct ir3_shader_info info;
	struct ir3_shader *ir;
};

struct fd_program {
	struct fd_state *state;
	struct fd_shader vertex_shader, fragment_shader, compute_shader;
};

static struct fd_shader *get_shader(struct fd_program *program,
		enum fd_shader_type type)
{
	switch (type) {
	case FD_SHADER_VERTEX:   return &program->vertex_shader;
	case FD_SHADER_FRAGMENT: return &program->fragment_shader;
	case FD_SHADER_COMPUTE:  return &program->compute_shader;
	}
	assert(0);
	return NULL;
}

struct fd_program * fd_program_new(struct fd_state *state)
{
	struct fd_program *program = calloc(1, sizeof(struct fd_program));
	program->state = state;
	return program;
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

	shader->bo = fd_attribute_bo_new(program->state,
			sizedwords * 4, shader->bin);

	return 0;
}

struct ir3_sampler ** fd_program_samplers(struct fd_program *program,
		enum fd_shader_type type, int *cnt)
{
	struct fd_shader *shader = get_shader(program, type);
	*cnt = shader->ir->samplers_count;
	return shader->ir->samplers;
}

uint32_t fd_program_outloc(struct fd_program *program)
{
	struct fd_shader *vs = get_shader(program, FD_SHADER_VERTEX);
	/* we only support vec4 varyings, otherwise we'd have to
	 * count 'em up:
	 */
	return 8 + (4 * vs->ir->varyings_count);
}

static uint32_t getpos(struct fd_shader *shader, const char *name,
		uint32_t default_regid)
{
	uint32_t i;
	for (i = 0; i < shader->ir->outs_count; i++)
		if (!strcmp(shader->ir->outs[i]->name, name))
			return shader->ir->outs[i]->rstart->num;
	return default_regid;
}

static bool ishalf(struct fd_shader *shader, const char *name)
{
	uint32_t i;
	for (i = 0; i < shader->ir->outs_count; i++)
		if (!strcmp(shader->ir->outs[i]->name, name))
			return !!(shader->ir->outs[i]->rstart->flags & IR3_REG_HALF);
	return 0;
}

static uint32_t instrlen(struct fd_shader *shader)
{
	/* the instructions length is in units of instruction groups
	 * (4 instructions, 8 dwords):
	 */
	return shader->sizedwords / 8;
}

static uint32_t totalattr(struct fd_shader *shader)
{
	uint32_t i, n = 0;
	for (i = 0; i < shader->ir->attributes_count; i++)
		n += shader->ir->attributes[i]->num;
	return n;
}

static uint32_t totalvar(struct fd_shader *shader)
{
	/* in units of scalar, ie. vec4 -> 4 */
	uint32_t i, n = 0;
	for (i = 0; i < shader->ir->varyings_count; i++)
		n += shader->ir->varyings[i]->num;
	return n;
}

/* # of components -> writemask */
static uint32_t regmask(uint32_t num)
{
	return (1 << num) - 1;
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
		struct fd_shader *shader, struct fd_parameters *attr,
		uint32_t first)
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
		OUT_RELOC(ring, p->bo, s * first, 0);    /* VFD_FETCH[i].INSTR_1 */

		OUT_PKT0(ring, REG_A3XX_VFD_DECODE_INSTR(i), 1);
		OUT_RING(ring, A3XX_VFD_DECODE_INSTR_WRITEMASK(regmask(a->num)) |
				A3XX_VFD_DECODE_INSTR_CONSTFILL |
				A3XX_VFD_DECODE_INSTR_FORMAT(p->fmt) |
				A3XX_VFD_DECODE_INSTR_REGID(a->rstart->num) |
				A3XX_VFD_DECODE_INSTR_SHIFTCNT(s) |
				A3XX_VFD_DECODE_INSTR_LASTCOMPVALID |
				COND(switchnext, A3XX_VFD_DECODE_INSTR_SWITCHNEXT));
	}
}

static struct fd_bo *get_buf(struct fd_shader *shader,
		struct fd_parameters *bufs, int num)
{
	uint32_t i;

	if (!bufs)
		return NULL;

	for (i = 0; i < shader->ir->bufs_count; i++) {
		struct ir3_buf *b = shader->ir->bufs[i];

		if (b->cstart->num == num) {
			struct fd_param *p = find_param(bufs, b->name);
			if (p)
				return p->bo;
		}
	}

	return NULL;
}

static void emit_uniconst(struct fd_ringbuffer *ring,
		struct fd_shader *shader, struct fd_parameters *uniforms,
		struct fd_parameters *bufs, enum adreno_state_block state_block)
{
	static uint32_t buf[512]; /* cheesy, but test code isn't multithreaded */
	uint32_t i, j, k, sz = 0, base = ~0;

	memset(buf, 0, sizeof(buf));

	for (i = 0; i < shader->ir->consts_count; i++) {
		struct ir3_const *c = shader->ir->consts[i];
		uint32_t off = c->cstart->num;
		base = min(base, off);
		memcpy(&buf[off], c->val, sizeof(c->val));
		sz = max(sz, off + (sizeof(c->val) / sizeof(buf[0])));
	}

	for (i = 0; i < shader->ir->uniforms_count; i++) {
		struct ir3_uniform *u = shader->ir->uniforms[i];
		struct fd_param *p = find_param(uniforms, u->name);
		const uint32_t *dwords = p->data;
		uint32_t off = u->cstart->num;

		base = min(base, off);

		for (j = 0; j < p->count; j++) {
			for (k = 0; k < p->size; k++)
				buf[off++] = *(dwords++);
			/* zero pad if needed: */
			for (; k < ALIGN(p->size, 4); k++)
				buf[off++] = 0;
		}

		sz = max(sz, off);
	}

	/* don't forget buf's: */
	for (i = 0; i < shader->ir->bufs_count; i++) {
		struct ir3_buf *b = shader->ir->bufs[i];
		uint32_t off = b->cstart->num;
		base = min(base, off);
		sz = max(sz, off);
	}

	/* if no constants, don't emit the CP_LOAD_STATE */
	if (sz == 0)
		return;

	/* align things to vec4: */
	base &= ~0x3;
	sz = ALIGN(sz, 4);
	sz -= base;

	OUT_PKT3(ring, CP_LOAD_STATE, 2 + sz);
	OUT_RING(ring, CP_LOAD_STATE_0_DST_OFF(base/2) |
			CP_LOAD_STATE_0_STATE_SRC(SS_DIRECT) |
			CP_LOAD_STATE_0_STATE_BLOCK(state_block) |
			CP_LOAD_STATE_0_NUM_UNIT(sz/2));
	OUT_RING(ring, CP_LOAD_STATE_1_STATE_TYPE(ST_CONSTANTS) |
			CP_LOAD_STATE_1_EXT_SRC_ADDR(0));
	for (i = 0; i < sz; i++) {
		struct fd_bo *bo = get_buf(shader, bufs, i + base);
		if (bo) {
			OUT_RELOC(ring, bo, 0, 0);
		} else {
			OUT_RING(ring, buf[i]);
		}
	}
}

static void emit_global_mem(struct fd_ringbuffer *ring,
		struct fd_shader *shader, struct fd_parameters *bufs)
{
	uint32_t i;

	for (i = 0; i < shader->ir->bufs_count; i++) {
		struct ir3_buf *b = shader->ir->bufs[i];
		struct fd_param *p = find_param(bufs, b->name);

		OUT_PKT0(ring, REG_A3XX_SP_GLOBAL_MEM_ADDR, 1);
		OUT_RELOC(ring, p->bo, 0, 0);       /* SP_GLOBAL_MEM_ADDR */

		OUT_PKT0(ring, REG_A3XX_SP_GLOBAL_MEM_SIZE, 1);
		OUT_RING(ring, fd_bo_size(p->bo));  /* SP_GLOBAL_MEM_SIZE */
	}
}

void fd_program_emit_state(struct fd_program *program, uint32_t first,
		struct fd_parameters *uniforms, struct fd_parameters *attr,
		struct fd_parameters *bufs, struct fd_ringbuffer *ring)
{
	struct fd_shader *vs = get_shader(program, FD_SHADER_VERTEX);
	struct fd_shader *fs = get_shader(program, FD_SHADER_FRAGMENT);
	struct ir3_shader_info *vsi = &vs->info;
	struct ir3_shader_info *fsi = &fs->info;
	uint32_t vsconstlen = vsi->max_const + 1;
	uint32_t fsconstlen = fsi->max_const + 1;
	uint32_t i, outloc;

	uint32_t posregid   = getpos(vs, "gl_Position", 0);
	uint32_t psizeregid = getpos(vs, "gl_PointSize", (63 << 2));
	uint32_t colorregid = getpos(fs, "gl_FragColor", 0);

	uint32_t numvar = totalvar(fs);

	assert (vs->ir->varyings_count == fs->ir->varyings_count);

	OUT_PKT0(ring, REG_A3XX_HLSQ_CONTROL_0_REG, 6);
	OUT_RING(ring, A3XX_HLSQ_CONTROL_0_REG_FSTHREADSIZE(FOUR_QUADS) |
			A3XX_HLSQ_CONTROL_0_REG_SPSHADERRESTART |
			A3XX_HLSQ_CONTROL_0_REG_SPCONSTFULLUPDATE);
	OUT_RING(ring, A3XX_HLSQ_CONTROL_1_REG_VSTHREADSIZE(TWO_QUADS) |
			A3XX_HLSQ_CONTROL_1_REG_ZWCOORD |
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
			// XXX "resolve" (?) bit set on gmem->mem pass..
			COND(!uniforms, A3XX_SP_SP_CTRL_REG_RESOLVE) |
			// XXX sometimes 0, sometimes 1:
			A3XX_SP_SP_CTRL_REG_L0MODE(1));

	/* emit unknown sequence of writes to 0x0ec4/0x0ec8 that the blob
	 * emits as part of the program state (it seems)..
	 */
	for (i = 0; i < 6; i++) {
		OUT_PKT0(ring, REG_A3XX_SP_PERFCOUNTER0_SELECT, 1);
		OUT_RING(ring, 0x00000000);    /* SP_PERFCOUNTER0_SELECT */

		OUT_PKT0(ring, REG_A3XX_SP_PERFCOUNTER3_SELECT, 1);
		OUT_RING(ring, 0x00000000);    /* SP_PERFCOUNTER3_SELECT */
	}

	OUT_PKT0(ring, REG_A3XX_SP_VS_LENGTH_REG, 1);
	OUT_RING(ring, A3XX_SP_VS_LENGTH_REG_SHADERLENGTH(instrlen(vs)));

	OUT_PKT0(ring, REG_A3XX_SP_VS_CTRL_REG0, 3);
	OUT_RING(ring, A3XX_SP_VS_CTRL_REG0_THREADMODE(MULTI) |
			A3XX_SP_VS_CTRL_REG0_INSTRBUFFERMODE(BUFFER) |
			A3XX_SP_VS_CTRL_REG0_HALFREGFOOTPRINT(vsi->max_half_reg + 1) |
			A3XX_SP_VS_CTRL_REG0_FULLREGFOOTPRINT(vsi->max_reg + 1) |
			A3XX_SP_VS_CTRL_REG0_INOUTREGOVERLAP(0) |
			A3XX_SP_VS_CTRL_REG0_THREADSIZE(TWO_QUADS) |
			A3XX_SP_VS_CTRL_REG0_SUPERTHREADMODE |
			A3XX_SP_VS_CTRL_REG0_LENGTH(instrlen(vs)));

	OUT_RING(ring, A3XX_SP_VS_CTRL_REG1_CONSTLENGTH(vsconstlen) |
			A3XX_SP_VS_CTRL_REG1_INITIALOUTSTANDING(totalattr(vs)) |
			A3XX_SP_VS_CTRL_REG1_CONSTFOOTPRINT(max(vsi->max_const, 0)));
	OUT_RING(ring, A3XX_SP_VS_PARAM_REG_POSREGID(posregid) |
			A3XX_SP_VS_PARAM_REG_PSIZEREGID(psizeregid) |
			A3XX_SP_VS_PARAM_REG_TOTALVSOUTVAR(fs->ir->varyings_count));

	for (i = 0; i < vs->ir->varyings_count; ) {
		struct ir3_varying *v;
		uint32_t reg = 0;

		OUT_PKT0(ring, REG_A3XX_SP_VS_OUT_REG(i/2), 1);

		v = vs->ir->varyings[i++];
		if (v) {
			reg |= A3XX_SP_VS_OUT_REG_A_REGID(v->rstart->num);
			reg |= A3XX_SP_VS_OUT_REG_A_COMPMASK(regmask(v->num));
		}

		v = vs->ir->varyings[i++];
		if (v) {
			reg |= A3XX_SP_VS_OUT_REG_B_REGID(v->rstart->num);
			reg |= A3XX_SP_VS_OUT_REG_B_COMPMASK(regmask(v->num));
		}

		OUT_RING(ring, reg);
	}

	outloc = 8;    /* I assume 0 and 4 are gl_Position/gl_PointSize? */
	for (i = 0; i < vs->ir->varyings_count; ) {
		struct ir3_varying *v;
		uint32_t reg = 0;

		OUT_PKT0(ring, REG_A3XX_SP_VS_VPC_DST_REG(i/4), 1);

		/* note: if we supported anything other than vec4 varyings, we'd
		 * actually be incrementing outloc by the actual varying size in
		 * units of scalar registers (ie. vec3 -> 3)
		 */

		v = vs->ir->varyings[i++];
		if (v) {
			reg |= A3XX_SP_VS_VPC_DST_REG_OUTLOC0(outloc);
			outloc += v->num;
		}

		v = vs->ir->varyings[i++];
		if (v) {
			reg |= A3XX_SP_VS_VPC_DST_REG_OUTLOC1(outloc);
			outloc += v->num;
		}

		v = vs->ir->varyings[i++];
		if (v) {
			reg |= A3XX_SP_VS_VPC_DST_REG_OUTLOC2(outloc);
			outloc += v->num;
		}

		v = vs->ir->varyings[i++];
		if (v) {
			reg |= A3XX_SP_VS_VPC_DST_REG_OUTLOC3(outloc);
			outloc += v->num;
		}

		OUT_RING(ring, reg);
	}

	// TODO SP_VS_OBJ_OFFSET_REG / SP_VS_OBJ_START_REG

	OUT_PKT0(ring, REG_A3XX_SP_FS_LENGTH_REG, 1);
	OUT_RING(ring, A3XX_SP_FS_LENGTH_REG_SHADERLENGTH(instrlen(fs)));

	OUT_PKT0(ring, REG_A3XX_SP_FS_CTRL_REG0, 2);
	OUT_RING(ring, A3XX_SP_FS_CTRL_REG0_THREADMODE(MULTI) |
			A3XX_SP_FS_CTRL_REG0_INSTRBUFFERMODE(BUFFER) |
			A3XX_SP_FS_CTRL_REG0_HALFREGFOOTPRINT(fsi->max_half_reg + 1) |
			A3XX_SP_FS_CTRL_REG0_FULLREGFOOTPRINT(fsi->max_reg + 1) |
			A3XX_SP_FS_CTRL_REG0_INOUTREGOVERLAP(1) |
			A3XX_SP_FS_CTRL_REG0_THREADSIZE(FOUR_QUADS) |
			A3XX_SP_FS_CTRL_REG0_SUPERTHREADMODE |
			COND(fs->ir->samplers_count > 0, A3XX_SP_FS_CTRL_REG0_PIXLODENABLE) |
			A3XX_SP_FS_CTRL_REG0_LENGTH(instrlen(fs)));
	OUT_RING(ring, A3XX_SP_FS_CTRL_REG1_CONSTLENGTH(fsconstlen) |
			A3XX_SP_FS_CTRL_REG1_INITIALOUTSTANDING(0) |
			A3XX_SP_FS_CTRL_REG1_CONSTFOOTPRINT(max(fsi->max_const, 0)) |
			A3XX_SP_FS_CTRL_REG1_HALFPRECVAROFFSET(63));

	// TODO SP_FS_OBJ_OFFSET_REG / SP_FS_OBJ_START_REG

	OUT_PKT0(ring, REG_A3XX_SP_FS_FLAT_SHAD_MODE_REG_0, 2);
	OUT_RING(ring, 0x00000000);        /* SP_FS_FLAT_SHAD_MODE_REG_0 */
	OUT_RING(ring, 0x00000000);        /* SP_FS_FLAT_SHAD_MODE_REG_1 */

	OUT_PKT0(ring, REG_A3XX_SP_FS_OUTPUT_REG, 1);
	OUT_RING(ring, 0x00000000);        /* SP_FS_OUTPUT_REG */

	OUT_PKT0(ring, REG_A3XX_SP_FS_MRT_REG(0), 4);
	OUT_RING(ring, A3XX_SP_FS_MRT_REG_REGID(colorregid) |  /* SP_FS_MRT[0].REG */
			COND(ishalf(fs, "gl_FragColor"), A3XX_SP_FS_MRT_REG_HALF_PRECISION));
	OUT_RING(ring, A3XX_SP_FS_MRT_REG_REGID(0));           /* SP_FS_MRT[1].REG */
	OUT_RING(ring, A3XX_SP_FS_MRT_REG_REGID(0));           /* SP_FS_MRT[2].REG */
	OUT_RING(ring, A3XX_SP_FS_MRT_REG_REGID(0));           /* SP_FS_MRT[3].REG */

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

	emit_shader(ring, vs, SB_VERT_SHADER);

	OUT_PKT0(ring, REG_A3XX_VFD_PERFCOUNTER0_SELECT, 1);
	OUT_RING(ring, 0x00000000);        /* VFD_PERFCOUNTER0_SELECT */

	emit_shader(ring, fs, SB_FRAG_SHADER);

	OUT_PKT0(ring, REG_A3XX_VFD_PERFCOUNTER0_SELECT, 1);
	OUT_RING(ring, 0x00000000);        /* VFD_PERFCOUNTER0_SELECT */

	OUT_PKT0(ring, REG_A3XX_VFD_CONTROL_0, 2);
	OUT_RING(ring, A3XX_VFD_CONTROL_0_TOTALATTRTOVS(totalattr(vs)) |
			A3XX_VFD_CONTROL_0_PACKETSIZE(2) |
			A3XX_VFD_CONTROL_0_STRMDECINSTRCNT(vs->ir->attributes_count) |
			A3XX_VFD_CONTROL_0_STRMFETCHINSTRCNT(vs->ir->attributes_count));
	OUT_RING(ring, A3XX_VFD_CONTROL_1_MAXSTORAGE(1) | // XXX
			A3XX_VFD_CONTROL_1_REGID4VTX(63 << 2) |
			A3XX_VFD_CONTROL_1_REGID4INST(63 << 2));

	emit_vtx_fetch(ring, vs, attr, first);

	/* we have this sometimes, not others.. perhaps we could be clever
	 * and figure out actually when we need to invalidate cache:
	 */
	OUT_PKT0(ring, REG_A3XX_UCHE_CACHE_INVALIDATE0_REG, 2);
	OUT_RING(ring, A3XX_UCHE_CACHE_INVALIDATE0_REG_ADDR(0));
	OUT_RING(ring, A3XX_UCHE_CACHE_INVALIDATE1_REG_ADDR(0) |
			A3XX_UCHE_CACHE_INVALIDATE1_REG_OPCODE(INVALIDATE) |
			A3XX_UCHE_CACHE_INVALIDATE1_REG_ENTIRE_CACHE);

	/* for RB_RESOLVE_PASS, I think the consts are not needed: */
	if (uniforms) {
		emit_uniconst(ring, vs, uniforms, bufs, SB_VERT_SHADER);
		emit_uniconst(ring, fs, uniforms, bufs, SB_FRAG_SHADER);
	}
}

void fd_program_emit_compute_state(struct fd_program *program,
		struct fd_parameters *uniforms, struct fd_parameters *attr,
		struct fd_parameters *bufs, struct fd_ringbuffer *ring)
{
	struct fd_shader *cs = get_shader(program, FD_SHADER_COMPUTE);
	struct ir3_shader_info *csi = &cs->info;
	uint32_t csconstlen = csi->max_const + 1;

	OUT_PKT0(ring, REG_A3XX_HLSQ_CONTROL_0_REG, 2);
	OUT_RING(ring, A3XX_HLSQ_CONTROL_0_REG_FSTHREADSIZE(TWO_QUADS) |
			A3XX_HLSQ_CONTROL_0_REG_CHUNKDISABLE |
			A3XX_HLSQ_CONTROL_0_REG_LAZYUPDATEDISABLE |
			A3XX_HLSQ_CONTROL_0_REG_SINGLECONTEXT |
			0x2000100);
	OUT_RING(ring, A3XX_HLSQ_CONTROL_1_REG_VSTHREADSIZE(TWO_QUADS));

	OUT_PKT0(ring, REG_A3XX_HLSQ_FS_CONTROL_REG, 1);
	OUT_RING(ring, A3XX_HLSQ_FS_CONTROL_REG_CONSTLENGTH(csconstlen) |
			A3XX_HLSQ_FS_CONTROL_REG_CONSTSTARTOFFSET(0) |
			A3XX_HLSQ_FS_CONTROL_REG_INSTRLENGTH(instrlen(cs)));

	OUT_PKT0(ring, REG_A3XX_HLSQ_CONST_VSPRESV_RANGE_REG, 1);
	OUT_RING(ring, A3XX_HLSQ_CONST_VSPRESV_RANGE_REG_STARTENTRY(0) |
			A3XX_HLSQ_CONST_VSPRESV_RANGE_REG_ENDENTRY(0));

	OUT_PKT0(ring, REG_A3XX_HLSQ_CONST_FSPRESV_RANGE_REG, 1);
	OUT_RING(ring, A3XX_HLSQ_CONST_FSPRESV_RANGE_REG_STARTENTRY(0) |
			A3XX_HLSQ_CONST_FSPRESV_RANGE_REG_ENDENTRY(0));

	OUT_PKT0(ring, REG_A3XX_SP_SP_CTRL_REG, 1);
	OUT_RING(ring, A3XX_SP_SP_CTRL_REG_CONSTMODE(0) |
			A3XX_SP_SP_CTRL_REG_SLEEPMODE(1) |
			A3XX_SP_SP_CTRL_REG_L0MODE(0));

	OUT_PKT0(ring, REG_A3XX_SP_FS_LENGTH_REG, 1);
	OUT_RING(ring, A3XX_SP_FS_LENGTH_REG_SHADERLENGTH(instrlen(cs)));

	OUT_PKT0(ring, REG_A3XX_SP_FS_CTRL_REG0, 2);
	OUT_RING(ring, A3XX_SP_FS_CTRL_REG0_THREADMODE(MULTI) |
			A3XX_SP_FS_CTRL_REG0_INSTRBUFFERMODE(BUFFER) |
			A3XX_SP_FS_CTRL_REG0_HALFREGFOOTPRINT(csi->max_half_reg + 1) |
			A3XX_SP_FS_CTRL_REG0_FULLREGFOOTPRINT(csi->max_reg + 1) |
			A3XX_SP_FS_CTRL_REG0_INOUTREGOVERLAP(0) |
			A3XX_SP_FS_CTRL_REG0_CACHEINVALID |
			A3XX_SP_FS_CTRL_REG0_THREADSIZE(TWO_QUADS) |
			A3XX_SP_FS_CTRL_REG0_COMPUTEMODE |
			COND(cs->ir->samplers_count > 0, A3XX_SP_FS_CTRL_REG0_PIXLODENABLE) |
			A3XX_SP_FS_CTRL_REG0_LENGTH(instrlen(cs)));
	OUT_RING(ring, A3XX_SP_FS_CTRL_REG1_CONSTLENGTH(csconstlen) |
			A3XX_SP_FS_CTRL_REG1_INITIALOUTSTANDING(3) |
			A3XX_SP_FS_CTRL_REG1_CONSTFOOTPRINT(max(csi->max_const, 0)) |
			A3XX_SP_FS_CTRL_REG1_HALFPRECVAROFFSET(0));

	OUT_PKT0(ring, REG_A3XX_SP_FS_OBJ_OFFSET_REG, 2);
	OUT_RING(ring, A3XX_SP_FS_OBJ_OFFSET_REG_CONSTOBJECTOFFSET(0) |
			A3XX_SP_FS_OBJ_OFFSET_REG_SHADEROBJOFFSET(0));
	OUT_RELOC(ring, cs->bo, 0, 0);     /* SP_FS_OBJ_START_REG */

	OUT_PKT0(ring, REG_A3XX_SP_FS_OUTPUT_REG, 1);
	OUT_RING(ring, 0x00000000);        /* SP_FS_OUTPUT_REG */

	emit_shader(ring, cs, SB_FRAG_SHADER);

	OUT_PKT0(ring, REG_A3XX_VFD_PERFCOUNTER0_SELECT, 1);
	OUT_RING(ring, 0x00000000);        /* VFD_PERFCOUNTER0_SELECT */

	/* we have this sometimes, not others.. perhaps we could be clever
	 * and figure out actually when we need to invalidate cache:
	 */
	OUT_PKT0(ring, REG_A3XX_UCHE_CACHE_INVALIDATE0_REG, 2);
	OUT_RING(ring, A3XX_UCHE_CACHE_INVALIDATE0_REG_ADDR(0));
	OUT_RING(ring, A3XX_UCHE_CACHE_INVALIDATE1_REG_ADDR(0) |
			A3XX_UCHE_CACHE_INVALIDATE1_REG_OPCODE(INVALIDATE) |
			A3XX_UCHE_CACHE_INVALIDATE1_REG_ENTIRE_CACHE);

	emit_uniconst(ring, cs, uniforms, bufs, SB_FRAG_SHADER);
	emit_global_mem(ring, cs, bufs);
}
