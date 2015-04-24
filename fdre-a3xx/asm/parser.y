/*
 * Copyright (c) 2013 Rob Clark <robclark@freedesktop.org>
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

%{
#define YYDEBUG 0

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ir-a3xx.h"
#include "instr-a3xx.h"

static struct ir3_shader      *shader;  /* current shader program */
static struct ir3_instruction *instr;   /* current instruction */

static struct {
	unsigned flags;
	unsigned repeat;
} iflags;

static struct {
	unsigned flags;
	unsigned wrmask;
} rflags;

int asm_yyget_lineno(void);

static struct ir3_instruction * new_instr(int cat, opc_t opc)
{
	instr = ir3_instr_create(shader, cat, opc);
	instr->flags = iflags.flags;
	instr->repeat = iflags.repeat;
	instr->line = asm_yyget_lineno();
	iflags.flags = iflags.repeat = 0;
	return instr;
}

static type_t parse_type(const char **type)
{
	if (!strncmp("f16", *type, 3)) {
		*type += 3;
		return TYPE_F16;
	} else if (!strncmp("f32", *type, 3)) {
		*type += 3;
		return TYPE_F32;
	} else if (!strncmp("u16", *type, 3)) {
		*type += 3;
		return TYPE_U16;
	} else if (!strncmp("u32", *type, 3)) {
		*type += 3;
		return TYPE_U32;
	} else if (!strncmp("s16", *type, 3)) {
		*type += 3;
		return TYPE_S16;
	} else if (!strncmp("s32", *type, 3)) {
		*type += 3;
		return TYPE_S32;
	} else if (!strncmp("u8", *type, 2)) {
		*type += 2;
		return TYPE_U8;
	} else if (!strncmp("s8", *type, 2)) {
		*type += 2;
		return TYPE_S8;
	} else {
		assert(0);  /* shouldn't get here */
		return ~0;
	}
}

static struct ir3_instruction * parse_type_type(struct ir3_instruction *instr,
		const char *type_type)
{
	instr->cat1.src_type = parse_type(&type_type);
	instr->cat1.dst_type = parse_type(&type_type);
	return instr;
}

static struct ir3_register * new_reg(int num, unsigned flags)
{
	struct ir3_register *reg;
	flags |= rflags.flags;
	if (num & 0x1)
		flags |= IR3_REG_HALF;
	reg = ir3_reg_create(instr, num>>1, flags);
	reg->wrmask = rflags.wrmask;
	rflags.flags = rflags.wrmask = 0;
	return reg;
}

#ifdef YYDEBUG
int yydebug;
#endif

extern int yylex(void);
typedef void *YY_BUFFER_STATE;
extern YY_BUFFER_STATE asm_yy_scan_string(const char *);
extern void asm_yy_delete_buffer(YY_BUFFER_STATE);

int yyparse(void);

void yyerror(const char *error)
{
	fprintf(stderr, "error at line %d: %s\n", asm_yyget_lineno(), error);
}

struct ir3_shader * fd_asm_parse(const char *src)
{
	YY_BUFFER_STATE buffer = asm_yy_scan_string(src);
#ifdef YYDEBUG
	yydebug = 1;
#endif
	if (yyparse()) {
		ir3_shader_destroy(shader);
		shader = NULL;
	}
	asm_yy_delete_buffer(buffer);
	return shader;
}
%}

%union {
	int tok;
	int num;
	uint32_t unum;
	double flt;
	const char *str;
	struct ir3_register *reg;
	struct {
		int start;
		int num;
	} range;
	type_t type;
}

%{
static void print_token(FILE *file, int type, YYSTYPE value)
{
	fprintf(file, "\ntype: %d\n", type);
}

#define YYPRINT(file, type, value) print_token(file, type, value)
%}

%token <num> T_INT
%token <unum> T_HEX
%token <flt> T_FLOAT
%token <str> T_IDENTIFIER
%token <num> T_REGISTER
%token <num> T_CONSTANT

/* @ headers (@const/@sampler/@uniform/@varying) */
%token <tok> T_A_ATTRIBUTE
%token <tok> T_A_CONST
%token <tok> T_A_SAMPLER
%token <tok> T_A_UNIFORM
%token <tok> T_A_VARYING
%token <tok> T_A_BUF
%token <tok> T_A_OUT

/* src register flags */
%token <tok> T_ABSNEG
%token <tok> T_NEG
%token <tok> T_ABS
%token <tok> T_R

/* dst register flags */
%token <tok> T_EVEN
%token <tok> T_POS_INFINITY
%token <tok> T_EI
%token <num> T_WRMASK

/* instruction flags */
%token <tok> T_SY
%token <tok> T_SS
%token <tok> T_JP
%token <num> T_RPT
%token <tok> T_UL

/* category 0: */
%token <tok> T_OP_NOP
%token <tok> T_OP_BR
%token <tok> T_OP_JUMP
%token <tok> T_OP_CALL
%token <tok> T_OP_RET
%token <tok> T_OP_KILL
%token <tok> T_OP_END
%token <tok> T_OP_EMIT
%token <tok> T_OP_CUT
%token <tok> T_OP_CHMASK
%token <tok> T_OP_CHSH
%token <tok> T_OP_FLOW_REV

/* category 1: */
%token <tok> T_OP_MOVA
%token <tok> T_OP_MOV
%token <tok> T_OP_COV

/* category 2: */
%token <tok> T_OP_ADD_F
%token <tok> T_OP_MIN_F
%token <tok> T_OP_MAX_F
%token <tok> T_OP_MUL_F
%token <tok> T_OP_SIGN_F
%token <tok> T_OP_CMPS_F
%token <tok> T_OP_ABSNEG_F
%token <tok> T_OP_CMPV_F
%token <tok> T_OP_FLOOR_F
%token <tok> T_OP_CEIL_F
%token <tok> T_OP_RNDNE_F
%token <tok> T_OP_RNDAZ_F
%token <tok> T_OP_TRUNC_F
%token <tok> T_OP_ADD_U
%token <tok> T_OP_ADD_S
%token <tok> T_OP_SUB_U
%token <tok> T_OP_SUB_S
%token <tok> T_OP_CMPS_U
%token <tok> T_OP_CMPS_S
%token <tok> T_OP_MIN_U
%token <tok> T_OP_MIN_S
%token <tok> T_OP_MAX_U
%token <tok> T_OP_MAX_S
%token <tok> T_OP_ABSNEG_S
%token <tok> T_OP_AND_B
%token <tok> T_OP_OR_B
%token <tok> T_OP_NOT_B
%token <tok> T_OP_XOR_B
%token <tok> T_OP_CMPV_U
%token <tok> T_OP_CMPV_S
%token <tok> T_OP_MUL_U
%token <tok> T_OP_MUL_S
%token <tok> T_OP_MULL_U
%token <tok> T_OP_BFREV_B
%token <tok> T_OP_CLZ_S
%token <tok> T_OP_CLZ_B
%token <tok> T_OP_SHL_B
%token <tok> T_OP_SHR_B
%token <tok> T_OP_ASHR_B
%token <tok> T_OP_BARY_F
%token <tok> T_OP_MGEN_B
%token <tok> T_OP_GETBIT_B
%token <tok> T_OP_SETRM
%token <tok> T_OP_CBITS_B
%token <tok> T_OP_SHB
%token <tok> T_OP_MSAD

/* category 3: */
%token <tok> T_OP_MAD_U16
%token <tok> T_OP_MADSH_U16
%token <tok> T_OP_MAD_S16
%token <tok> T_OP_MADSH_M16
%token <tok> T_OP_MAD_U24
%token <tok> T_OP_MAD_S24
%token <tok> T_OP_MAD_F16
%token <tok> T_OP_MAD_F32
%token <tok> T_OP_SEL_B16
%token <tok> T_OP_SEL_B32
%token <tok> T_OP_SEL_S16
%token <tok> T_OP_SEL_S32
%token <tok> T_OP_SEL_F16
%token <tok> T_OP_SEL_F32
%token <tok> T_OP_SAD_S16
%token <tok> T_OP_SAD_S32

/* category 4: */
%token <tok> T_OP_RCP
%token <tok> T_OP_RSQ
%token <tok> T_OP_LOG2
%token <tok> T_OP_EXP2
%token <tok> T_OP_SIN
%token <tok> T_OP_COS
%token <tok> T_OP_SQRT

/* category 5: */
%token <tok> T_OP_ISAM
%token <tok> T_OP_ISAML
%token <tok> T_OP_ISAMM
%token <tok> T_OP_SAM
%token <tok> T_OP_SAMB
%token <tok> T_OP_SAML
%token <tok> T_OP_SAMGQ
%token <tok> T_OP_GETLOD
%token <tok> T_OP_CONV
%token <tok> T_OP_CONVM
%token <tok> T_OP_GETSIZE
%token <tok> T_OP_GETBUF
%token <tok> T_OP_GETPOS
%token <tok> T_OP_GETINFO
%token <tok> T_OP_DSX
%token <tok> T_OP_DSY
%token <tok> T_OP_GATHER4R
%token <tok> T_OP_GATHER4G
%token <tok> T_OP_GATHER4B
%token <tok> T_OP_GATHER4A
%token <tok> T_OP_SAMGP0
%token <tok> T_OP_SAMGP1
%token <tok> T_OP_SAMGP2
%token <tok> T_OP_SAMGP3
%token <tok> T_OP_DSXPP_1
%token <tok> T_OP_DSYPP_1
%token <tok> T_OP_RGETPOS
%token <tok> T_OP_RGETINFO

/* category 6: */
%token <tok> T_OP_LDG
%token <tok> T_OP_LDL
%token <tok> T_OP_LDP
%token <tok> T_OP_STG
%token <tok> T_OP_STL
%token <tok> T_OP_STP
%token <tok> T_OP_STI
%token <tok> T_OP_G2L
%token <tok> T_OP_L2G
%token <tok> T_OP_PREFETCH
%token <tok> T_OP_LDLW
%token <tok> T_OP_STLW
%token <tok> T_OP_RESFMT
%token <tok> T_OP_RESINF
%token <tok> T_OP_ATOMIC_ADD
%token <tok> T_OP_ATOMIC_SUB
%token <tok> T_OP_ATOMIC_XCHG
%token <tok> T_OP_ATOMIC_INC
%token <tok> T_OP_ATOMIC_DEC
%token <tok> T_OP_ATOMIC_CMPXCHG
%token <tok> T_OP_ATOMIC_MIN
%token <tok> T_OP_ATOMIC_MAX
%token <tok> T_OP_ATOMIC_AND
%token <tok> T_OP_ATOMIC_OR
%token <tok> T_OP_ATOMIC_XOR
%token <tok> T_OP_LDGB_TYPED_4D
%token <tok> T_OP_STGB_4D_4
%token <tok> T_OP_STIB
%token <tok> T_OP_LDC_4
%token <tok> T_OP_LDLV

/* type qualifiers: */
%token <tok> T_TYPE_F16
%token <tok> T_TYPE_F32
%token <tok> T_TYPE_U16
%token <tok> T_TYPE_U32
%token <tok> T_TYPE_S16
%token <tok> T_TYPE_S32
%token <tok> T_TYPE_U8
%token <tok> T_TYPE_S8

/* condition qualifiers: */
%token <tok> T_LT
%token <tok> T_LE
%token <tok> T_GT
%token <tok> T_GE
%token <tok> T_EQ
%token <tok> T_NE

%token <tok> T_3D
%token <tok> T_S2EN
%token <tok> T_SAMP
%token <tok> T_TEX

%token <tok> T_NAN
%token <tok> T_INF
%token <num> T_A0
%token <num> T_P0
%token <str> T_CAT1_TYPE_TYPE

%type <num> integer offset
%type <flt> float
%type <reg> reg const
%type <tok> cat1_opc
%type <tok> cat2_opc_1src cat2_opc_2src_cnd cat2_opc_2src
%type <tok> cat3_opc
%type <tok> cat4_opc
%type <tok> cat5_opc cat5_samp cat5_tex cat5_type
%type <range> reg_range const_range
%type <type> type
%type <unum> const_val

%error-verbose

%start shader

%%

shader:            { shader = ir3_shader_create(); } headers instrs

headers:           
|                  header headers

header:            attribute_header
|                  const_header
|                  sampler_header
|                  uniform_header
|                  varying_header
|                  buf_header
|                  out_header

attribute_header:  T_A_ATTRIBUTE '(' reg_range ')' T_IDENTIFIER {
                       ir3_attribute_create(shader, $3.start, $3.num, $5);
}

const_val:         T_FLOAT   { $$ = fui($1); printf("%08x\n", $$); }
|                  T_INT     { $$ = $1;      printf("%08x\n", $$); }
|                  '-' T_INT { $$ = -$2;     printf("%08x\n", $$); }
|                  T_HEX     { $$ = $1;      printf("%08x\n", $$); }

const_header:      T_A_CONST '(' T_CONSTANT ')' const_val ',' const_val ',' const_val ',' const_val {
                       ir3_const_create(shader, $3, $5, $7, $9, $11);
}

sampler_header:    T_A_SAMPLER '(' integer ')' T_IDENTIFIER {
                       ir3_sampler_create(shader, $3, $5);
}

uniform_header:    T_A_UNIFORM '(' const_range ')' T_IDENTIFIER {
                       ir3_uniform_create(shader, $3.start, $3.num, $5);
}

varying_header:    T_A_VARYING '(' reg_range ')' T_IDENTIFIER {
                       ir3_varying_create(shader, $3.start, $3.num, $5);
}

out_header:        T_A_OUT '(' reg_range ')' T_IDENTIFIER {
                       ir3_out_create(shader, $3.start, $3.num, $5);
}

buf_header:        T_A_BUF '(' T_CONSTANT ')' T_IDENTIFIER {
                       ir3_buf_create(shader, $3, $5);
}

                   /* NOTE: if just single register is specified (rather than a range) assume vec4 */
reg_range:         T_REGISTER                { $$.start = $1; $$.num = 4; }
|                  T_REGISTER '-' T_REGISTER { $$.start = $1; $$.num = 1 + ($3 >> 1) - ($1 >> 1); }

const_range:       T_CONSTANT                { $$.start = $1; $$.num = 4; }
|                  T_CONSTANT '-' T_CONSTANT { $$.start = $1; $$.num = 1 + ($3 >> 1) - ($1 >> 1); }

iflag:             T_SY   { iflags.flags |= IR3_INSTR_SY; }
|                  T_SS   { iflags.flags |= IR3_INSTR_SS; }
|                  T_JP   { iflags.flags |= IR3_INSTR_JP; }
|                  T_RPT  { iflags.repeat = $1; }
|                  T_UL   { iflags.flags |= IR3_INSTR_UL; }

iflags:
|                  iflag iflags

instrs:            instr instrs
|                  instr

instr:             iflags cat0_instr
|                  iflags cat1_instr
|                  iflags cat2_instr
|                  iflags cat3_instr
|                  iflags cat4_instr
|                  iflags cat5_instr
|                  iflags cat6_instr

cat0_src:          '!' T_P0        { instr->cat0.inv = true; instr->cat0.comp = $2 >> 1; }
|                  T_P0            { instr->cat0.comp = $1 >> 1; }

cat0_immed:        '#' integer     { instr->cat0.immed = $2; }

cat0_instr:        T_OP_NOP        { new_instr(0, OPC_NOP); }
|                  T_OP_BR         { new_instr(0, OPC_BR); }    cat0_src ',' cat0_immed
|                  T_OP_JUMP       { new_instr(0, OPC_JUMP); }  cat0_immed
|                  T_OP_CALL       { new_instr(0, OPC_CALL); }  cat0_immed
|                  T_OP_RET        { new_instr(0, OPC_RET); }
|                  T_OP_KILL       { new_instr(0, OPC_KILL); }  cat0_src
|                  T_OP_END        { new_instr(0, OPC_END); }
|                  T_OP_EMIT       { new_instr(0, OPC_EMIT); }
|                  T_OP_CUT        { new_instr(0, OPC_CUT); }
|                  T_OP_CHMASK     { new_instr(0, OPC_CHMASK); }
|                  T_OP_CHSH       { new_instr(0, OPC_CHSH); }
|                  T_OP_FLOW_REV   { new_instr(0, OPC_FLOW_REV); }

cat1_opc:          T_OP_MOVA {
                       new_instr(1, 0);
                       instr->cat1.src_type = TYPE_S16;
                       instr->cat1.dst_type = TYPE_S16;
}
|                  T_OP_MOV '.' T_CAT1_TYPE_TYPE {
                       parse_type_type(new_instr(1, 0), $3);
}
|                  T_OP_COV '.' T_CAT1_TYPE_TYPE {
                       parse_type_type(new_instr(1, 0), $3);
}

cat1_instr:        cat1_opc dst_reg ',' src_reg_or_const_or_rel_or_imm

cat2_opc_1src:     T_OP_ABSNEG_F  { new_instr(2, OPC_ABSNEG_F); }
|                  T_OP_ABSNEG_S  { new_instr(2, OPC_ABSNEG_S); }
|                  T_OP_CLZ_B     { new_instr(2, OPC_CLZ_B); }
|                  T_OP_CLZ_S     { new_instr(2, OPC_CLZ_S); }
|                  T_OP_SIGN_F    { new_instr(2, OPC_SIGN_F); }
|                  T_OP_FLOOR_F   { new_instr(2, OPC_FLOOR_F); }
|                  T_OP_CEIL_F    { new_instr(2, OPC_CEIL_F); }
|                  T_OP_RNDNE_F   { new_instr(2, OPC_RNDNE_F); }
|                  T_OP_RNDAZ_F   { new_instr(2, OPC_RNDAZ_F); }
|                  T_OP_TRUNC_F   { new_instr(2, OPC_TRUNC_F); }
|                  T_OP_NOT_B     { new_instr(2, OPC_NOT_B); }
|                  T_OP_BFREV_B   { new_instr(2, OPC_BFREV_B); }
|                  T_OP_SETRM     { new_instr(2, OPC_SETRM); }
|                  T_OP_CBITS_B   { new_instr(2, OPC_CBITS_B); }

cat2_opc_2src_cnd: T_OP_CMPS_F    { new_instr(2, OPC_CMPS_F); }
|                  T_OP_CMPS_U    { new_instr(2, OPC_CMPS_U); }
|                  T_OP_CMPS_S    { new_instr(2, OPC_CMPS_S); }
|                  T_OP_CMPV_F    { new_instr(2, OPC_CMPV_F); }
|                  T_OP_CMPV_U    { new_instr(2, OPC_CMPV_U); }
|                  T_OP_CMPV_S    { new_instr(2, OPC_CMPV_S); }

cat2_opc_2src:     T_OP_ADD_F     { new_instr(2, OPC_ADD_F); }
|                  T_OP_MIN_F     { new_instr(2, OPC_MIN_F); }
|                  T_OP_MAX_F     { new_instr(2, OPC_MAX_F); }
|                  T_OP_MUL_F     { new_instr(2, OPC_MUL_F); }
|                  T_OP_ADD_U     { new_instr(2, OPC_ADD_U); }
|                  T_OP_ADD_S     { new_instr(2, OPC_ADD_S); }
|                  T_OP_SUB_U     { new_instr(2, OPC_SUB_U); }
|                  T_OP_SUB_S     { new_instr(2, OPC_SUB_S); }
|                  T_OP_MIN_U     { new_instr(2, OPC_MIN_U); }
|                  T_OP_MIN_S     { new_instr(2, OPC_MIN_S); }
|                  T_OP_MAX_U     { new_instr(2, OPC_MAX_U); }
|                  T_OP_MAX_S     { new_instr(2, OPC_MAX_S); }
|                  T_OP_AND_B     { new_instr(2, OPC_AND_B); }
|                  T_OP_OR_B      { new_instr(2, OPC_OR_B); }
|                  T_OP_XOR_B     { new_instr(2, OPC_XOR_B); }
|                  T_OP_MUL_U     { new_instr(2, OPC_MUL_U); }
|                  T_OP_MUL_S     { new_instr(2, OPC_MUL_S); }
|                  T_OP_MULL_U    { new_instr(2, OPC_MULL_U); }
|                  T_OP_SHL_B     { new_instr(2, OPC_SHL_B); }
|                  T_OP_SHR_B     { new_instr(2, OPC_SHR_B); }
|                  T_OP_ASHR_B    { new_instr(2, OPC_ASHR_B); }
|                  T_OP_BARY_F    { new_instr(2, OPC_BARY_F); }
|                  T_OP_MGEN_B    { new_instr(2, OPC_MGEN_B); }
|                  T_OP_GETBIT_B  { new_instr(2, OPC_GETBIT_B); }
|                  T_OP_SHB       { new_instr(2, OPC_SHB); }
|                  T_OP_MSAD      { new_instr(2, OPC_MSAD); }

cond:              T_LT           { instr->cat2.condition = IR3_COND_LT; }
|                  T_LE           { instr->cat2.condition = IR3_COND_LE; }
|                  T_GT           { instr->cat2.condition = IR3_COND_GT; }
|                  T_GE           { instr->cat2.condition = IR3_COND_GE; }
|                  T_EQ           { instr->cat2.condition = IR3_COND_EQ; }
|                  T_NE           { instr->cat2.condition = IR3_COND_NE; }

cat2_instr:        cat2_opc_1src dst_reg ',' src_reg_or_const_or_rel_or_imm
|                  cat2_opc_2src_cnd '.' cond dst_reg ',' src_reg_or_const_or_rel_or_imm ',' src_reg_or_const_or_rel_or_imm
|                  cat2_opc_2src dst_reg ',' src_reg_or_const_or_rel_or_imm ',' src_reg_or_const_or_rel_or_imm

cat3_opc:          T_OP_MAD_U16   { new_instr(3, OPC_MAD_U16); }
|                  T_OP_MADSH_U16 { new_instr(3, OPC_MADSH_U16); }
|                  T_OP_MAD_S16   { new_instr(3, OPC_MAD_S16); }
|                  T_OP_MADSH_M16 { new_instr(3, OPC_MADSH_M16); }
|                  T_OP_MAD_U24   { new_instr(3, OPC_MAD_U24); }
|                  T_OP_MAD_S24   { new_instr(3, OPC_MAD_S24); }
|                  T_OP_MAD_F16   { new_instr(3, OPC_MAD_F16); }
|                  T_OP_MAD_F32   { new_instr(3, OPC_MAD_F32); }
|                  T_OP_SEL_B16   { new_instr(3, OPC_SEL_B16); }
|                  T_OP_SEL_B32   { new_instr(3, OPC_SEL_B32); }
|                  T_OP_SEL_S16   { new_instr(3, OPC_SEL_S16); }
|                  T_OP_SEL_S32   { new_instr(3, OPC_SEL_S32); }
|                  T_OP_SEL_F16   { new_instr(3, OPC_SEL_F16); }
|                  T_OP_SEL_F32   { new_instr(3, OPC_SEL_F32); }
|                  T_OP_SAD_S16   { new_instr(3, OPC_SAD_S16); }
|                  T_OP_SAD_S32   { new_instr(3, OPC_SAD_S32); }

cat3_instr:        cat3_opc dst_reg ',' src_reg_or_const_or_rel ',' src_reg_or_const ',' src_reg_or_const_or_rel

cat4_opc:          T_OP_RCP       { new_instr(4, OPC_RCP); }
|                  T_OP_RSQ       { new_instr(4, OPC_RSQ); }
|                  T_OP_LOG2      { new_instr(4, OPC_LOG2); }
|                  T_OP_EXP2      { new_instr(4, OPC_EXP2); }
|                  T_OP_SIN       { new_instr(4, OPC_SIN); }
|                  T_OP_COS       { new_instr(4, OPC_COS); }
|                  T_OP_SQRT      { new_instr(4, OPC_SQRT); }

cat4_instr:        cat4_opc dst_reg ',' src_reg_or_const_or_rel_or_imm

cat5_opc_dsxypp:   T_OP_DSXPP_1   { new_instr(5, OPC_DSXPP_1); }
|                  T_OP_DSYPP_1   { new_instr(5, OPC_DSYPP_1); }

cat5_opc:          T_OP_ISAM      { new_instr(5, OPC_ISAM); }
|                  T_OP_ISAML     { new_instr(5, OPC_ISAML); }
|                  T_OP_ISAMM     { new_instr(5, OPC_ISAMM); }
|                  T_OP_SAM       { new_instr(5, OPC_SAM); }
|                  T_OP_SAMB      { new_instr(5, OPC_SAMB); }
|                  T_OP_SAML      { new_instr(5, OPC_SAML); }
|                  T_OP_SAMGQ     { new_instr(5, OPC_SAMGQ); }
|                  T_OP_GETLOD    { new_instr(5, OPC_GETLOD); }
|                  T_OP_CONV      { new_instr(5, OPC_CONV); }
|                  T_OP_CONVM     { new_instr(5, OPC_CONVM); }
|                  T_OP_GETSIZE   { new_instr(5, OPC_GETSIZE); }
|                  T_OP_GETBUF    { new_instr(5, OPC_GETBUF); }
|                  T_OP_GETPOS    { new_instr(5, OPC_GETPOS); }
|                  T_OP_GETINFO   { new_instr(5, OPC_GETINFO); }
|                  T_OP_DSX       { new_instr(5, OPC_DSX); }
|                  T_OP_DSY       { new_instr(5, OPC_DSY); }
|                  T_OP_GATHER4R  { new_instr(5, OPC_GATHER4R); }
|                  T_OP_GATHER4G  { new_instr(5, OPC_GATHER4G); }
|                  T_OP_GATHER4B  { new_instr(5, OPC_GATHER4B); }
|                  T_OP_GATHER4A  { new_instr(5, OPC_GATHER4A); }
|                  T_OP_SAMGP0    { new_instr(5, OPC_SAMGP0); }
|                  T_OP_SAMGP1    { new_instr(5, OPC_SAMGP1); }
|                  T_OP_SAMGP2    { new_instr(5, OPC_SAMGP2); }
|                  T_OP_SAMGP3    { new_instr(5, OPC_SAMGP3); }
|                  T_OP_RGETPOS   { new_instr(5, OPC_RGETPOS); }
|                  T_OP_RGETINFO  { new_instr(5, OPC_RGETINFO); }

cat5_flag:         '.' T_3D       { instr->flags |= IR3_INSTR_3D; }
|                  '.' 'a'        { instr->flags |= IR3_INSTR_A; }
|                  '.' 'o'        { instr->flags |= IR3_INSTR_O; }
|                  '.' 'p'        { instr->flags |= IR3_INSTR_P; }
|                  '.' 's'        { instr->flags |= IR3_INSTR_S; }
|                  '.' T_S2EN     { instr->flags |= IR3_INSTR_S2EN; }
cat5_flags:
|                  cat5_flag cat5_flags

cat5_samp:         T_SAMP         { instr->cat5.samp = $1; }
cat5_tex:          T_TEX          { instr->cat5.tex = $1; }
cat5_type:         '(' type ')'   { instr->cat5.type = $2; }

cat5_instr:        cat5_opc_dsxypp cat5_flags dst_reg ',' src_reg
|                  cat5_opc cat5_flags cat5_type dst_reg ',' src_reg ',' src_reg ',' cat5_samp ',' cat5_tex
|                  cat5_opc cat5_flags cat5_type dst_reg ',' src_reg ',' src_reg ',' cat5_samp
|                  cat5_opc cat5_flags cat5_type dst_reg ',' src_reg ',' src_reg ',' cat5_tex
|                  cat5_opc cat5_flags cat5_type dst_reg ',' src_reg ',' src_reg
|                  cat5_opc cat5_flags cat5_type dst_reg ',' src_reg ',' cat5_samp ',' cat5_tex
|                  cat5_opc cat5_flags cat5_type dst_reg ',' src_reg ',' cat5_samp
|                  cat5_opc cat5_flags cat5_type dst_reg ',' src_reg ',' cat5_tex
|                  cat5_opc cat5_flags cat5_type dst_reg ',' src_reg
|                  cat5_opc cat5_flags cat5_type dst_reg ',' cat5_samp ',' cat5_tex
|                  cat5_opc cat5_flags cat5_type dst_reg ',' cat5_samp
|                  cat5_opc cat5_flags cat5_type dst_reg ',' cat5_tex
|                  cat5_opc cat5_flags cat5_type dst_reg

cat6_type:         '.' type  { instr->cat6.type = $2; }
cat6_offset:       offset    { instr->cat6.src_offset = $1; }
cat6_immed:        integer   { instr->cat6.iim_val = $1; }

cat6_load:         T_OP_LDG  { new_instr(6, OPC_LDG); }  cat6_type dst_reg ',' 'g' '[' reg cat6_offset ']' ',' cat6_immed
|                  T_OP_LDP  { new_instr(6, OPC_LDP); }  cat6_type dst_reg ',' 'p' '[' reg cat6_offset ']' ',' cat6_immed
|                  T_OP_LDL  { new_instr(6, OPC_LDL); }  cat6_type dst_reg ',' 'l' '[' reg cat6_offset ']' ',' cat6_immed
|                  T_OP_LDLW { new_instr(6, OPC_LDLW); } cat6_type dst_reg ',' 'l' '[' reg cat6_offset ']' ',' cat6_immed
|                  T_OP_LDLV { new_instr(6, OPC_LDLV); } cat6_type dst_reg ',' 'l' '[' reg cat6_offset ']' ',' cat6_immed

cat6_store:        T_OP_STG  { new_instr(6, OPC_STG); }  cat6_type 'g' '[' dst_reg cat6_offset ']' ',' reg ',' cat6_immed
|                  T_OP_STP  { new_instr(6, OPC_STP); }  cat6_type 'p' '[' dst_reg cat6_offset ']' ',' reg ',' cat6_immed
|                  T_OP_STL  { new_instr(6, OPC_STL); }  cat6_type 'l' '[' dst_reg cat6_offset ']' ',' reg ',' cat6_immed
|                  T_OP_STLW { new_instr(6, OPC_STLW); } cat6_type 'l' '[' dst_reg cat6_offset ']' ',' reg ',' cat6_immed

cat6_storei:       T_OP_STI  { new_instr(6, OPC_STI); }  cat6_type dst_reg cat6_offset ',' reg ',' cat6_immed

cat6_storeib:      T_OP_STIB { new_instr(6, OPC_STIB); } cat6_type 'g' '[' dst_reg ']' ',' reg cat6_offset ',' cat6_immed

cat6_prefetch:     T_OP_PREFETCH { new_instr(6, OPC_PREFETCH); new_reg(0,0); /* dummy dst */ } 'g' '[' reg cat6_offset ']' ',' cat6_immed

cat6_atomic_l_g:   '.' 'g'  { instr->flags |= IR3_INSTR_G; }
|                  '.' 'l'  {  }

cat6_atomic:       T_OP_ATOMIC_ADD     { new_instr(6, OPC_ATOMIC_ADD); }    cat6_atomic_l_g cat6_type dst_reg ',' 'l' '[' reg cat6_offset ']' ',' cat6_immed
|                  T_OP_ATOMIC_SUB     { new_instr(6, OPC_ATOMIC_SUB); }    cat6_atomic_l_g cat6_type dst_reg ',' 'l' '[' reg cat6_offset ']' ',' cat6_immed
|                  T_OP_ATOMIC_XCHG    { new_instr(6, OPC_ATOMIC_XCHG); }   cat6_atomic_l_g cat6_type dst_reg ',' 'l' '[' reg cat6_offset ']' ',' cat6_immed
|                  T_OP_ATOMIC_INC     { new_instr(6, OPC_ATOMIC_INC); }    cat6_atomic_l_g cat6_type dst_reg ',' 'l' '[' reg cat6_offset ']' ',' cat6_immed
|                  T_OP_ATOMIC_DEC     { new_instr(6, OPC_ATOMIC_DEC); }    cat6_atomic_l_g cat6_type dst_reg ',' 'l' '[' reg cat6_offset ']' ',' cat6_immed
|                  T_OP_ATOMIC_CMPXCHG { new_instr(6, OPC_ATOMIC_CMPXCHG); }cat6_atomic_l_g cat6_type dst_reg ',' 'l' '[' reg cat6_offset ']' ',' cat6_immed
|                  T_OP_ATOMIC_MIN     { new_instr(6, OPC_ATOMIC_MIN); }    cat6_atomic_l_g cat6_type dst_reg ',' 'l' '[' reg cat6_offset ']' ',' cat6_immed
|                  T_OP_ATOMIC_MAX     { new_instr(6, OPC_ATOMIC_MAX); }    cat6_atomic_l_g cat6_type dst_reg ',' 'l' '[' reg cat6_offset ']' ',' cat6_immed
|                  T_OP_ATOMIC_AND     { new_instr(6, OPC_ATOMIC_AND); }    cat6_atomic_l_g cat6_type dst_reg ',' 'l' '[' reg cat6_offset ']' ',' cat6_immed
|                  T_OP_ATOMIC_OR      { new_instr(6, OPC_ATOMIC_OR); }     cat6_atomic_l_g cat6_type dst_reg ',' 'l' '[' reg cat6_offset ']' ',' cat6_immed
|                  T_OP_ATOMIC_XOR     { new_instr(6, OPC_ATOMIC_XOR); }    cat6_atomic_l_g cat6_type dst_reg ',' 'l' '[' reg cat6_offset ']' ',' cat6_immed

cat6_todo:         T_OP_G2L                 { new_instr(6, OPC_G2L); }
|                  T_OP_L2G                 { new_instr(6, OPC_L2G); }
|                  T_OP_RESFMT              { new_instr(6, OPC_RESFMT); }
|                  T_OP_RESINF              { new_instr(6, OPC_RESINFO); }
|                  T_OP_LDGB_TYPED_4D       { new_instr(6, OPC_LDGB_TYPED_4D); }
|                  T_OP_STGB_4D_4           { new_instr(6, OPC_STGB_4D_4); }
|                  T_OP_LDC_4               { new_instr(6, OPC_LDC_4); }

cat6_instr:        cat6_load
|                  cat6_store
|                  cat6_storei
|                  cat6_storeib
|                  cat6_prefetch
|                  cat6_atomic
|                  cat6_todo

reg:               T_REGISTER     { $$ = new_reg($1, 0); }
|                  T_A0           { $$ = new_reg((61 << 3) + $1, IR3_REG_HALF); }
|                  T_P0           { $$ = new_reg((62 << 3) + $1, 0); }

const:             T_CONSTANT     { $$ = new_reg($1, IR3_REG_CONST); }

dst_reg_flag:      T_EVEN         { rflags.flags |= IR3_REG_EVEN; }
|                  T_POS_INFINITY { rflags.flags |= IR3_REG_POS_INF; }
|                  T_EI           { rflags.flags |= IR3_REG_EI; }
|                  T_WRMASK       { rflags.wrmask = $1; }

dst_reg_flags:     dst_reg_flag
|                  dst_reg_flag dst_reg_flags

                   /* note: destination registers are always incremented in repeat */
dst_reg:           reg                 { $1->flags |= IR3_REG_R; }
|                  dst_reg_flags reg   { $2->flags |= IR3_REG_R; }

src_reg_flag:      T_ABSNEG       { rflags.flags |= IR3_REG_ABS|IR3_REG_NEGATE; }
|                  T_NEG          { rflags.flags |= IR3_REG_NEGATE; }
|                  T_ABS          { rflags.flags |= IR3_REG_ABS; }
|                  T_R            { rflags.flags |= IR3_REG_R; }

src_reg_flags:     src_reg_flag
|                  src_reg_flag src_reg_flags

src_reg:           reg
|                  src_reg_flags reg

src_const:         const
|                  src_reg_flags const

src_reg_or_const:  src_reg
|                  src_const

src_reg_or_const_or_rel: src_reg_or_const
|                  relative

src_reg_or_const_or_rel_or_imm: src_reg_or_const_or_rel
|                  src_reg_flags immediate
|                  immediate

offset:            { $$ = 0; }
|                  '+' integer { $$ = $2; }
|                  '-' integer { $$ = -$2; }

relative:          'r' '<' T_A0 offset '>'  { new_reg(0, IR3_REG_RELATIV)->offset = $4; }
|                  'c' '<' T_A0 offset '>'  { new_reg(0, IR3_REG_RELATIV | IR3_REG_CONST)->offset = $4; }

immediate:         integer             { new_reg(0, IR3_REG_IMMED)->iim_val = $1; }
|                  '(' integer ')'     { new_reg(0, IR3_REG_IMMED)->fim_val = $2; }
|                  '(' float ')'       { new_reg(0, IR3_REG_IMMED)->fim_val = $2; }
|                  '(' T_NAN ')'       { new_reg(0, IR3_REG_IMMED)->fim_val = NAN; }
|                  '(' T_INF ')'       { new_reg(0, IR3_REG_IMMED)->fim_val = INFINITY; }

integer:           T_INT       { $$ = $1; }
|                  '-' T_INT   { $$ = -$2; }
|                  T_HEX       { $$ = $1; }
|                  '-' T_HEX   { $$ = -$2; }

float:             T_FLOAT     { $$ = $1; }
|                  '-' T_FLOAT { $$ = -$2; }

type:              T_TYPE_F16  { $$ = TYPE_F16; }
|                  T_TYPE_F32  { $$ = TYPE_F32; }
|                  T_TYPE_U16  { $$ = TYPE_U16; }
|                  T_TYPE_U32  { $$ = TYPE_U32; }
|                  T_TYPE_S16  { $$ = TYPE_S16; }
|                  T_TYPE_S32  { $$ = TYPE_S32; }
|                  T_TYPE_U8   { $$ = TYPE_U8;  }
|                  T_TYPE_S8   { $$ = TYPE_S8;  }
