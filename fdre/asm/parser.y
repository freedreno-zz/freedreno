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

%{
//#define YYDEBUG 1

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ir.h"

static struct ir_shader      *shader;  /* current shader program */
static struct ir_cf          *cf;      /* current CF block */
static struct ir_instruction *instr;   /* current ALU/FETCH instruction */

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
	fprintf(stderr, "%s\n", error);
}

/* TODO return IR data structure */
struct ir_shader * fd_asm_parse(const char *src)
{
	YY_BUFFER_STATE buffer = asm_yy_scan_string(src);
#ifdef YYDEBUG
	yydebug = 1;
#endif
	if (yyparse()) {
		ir_shader_destroy(shader);
		shader = NULL;
	}
	asm_yy_delete_buffer(buffer);
	return shader;
}
%}

%union {
	int tok;
	int num;
	double flt;
	int fmt;
	const char *str;
	struct ir_register *reg;
	struct {
		int start;
		int num;
	} range;
}

%{
static void print_token(FILE *file, int type, YYSTYPE value)
{
//	fprintf(file, "\ntype: %d\n", type);
}

#define YYPRINT(file, type, value) print_token(file, type, value)
%}

%token <num> T_INT
%token <num> T_HEX
%token <flt> T_FLOAT
%token <str> T_SWIZZLE
%token <str> T_IDENTIFIER
%token <tok> T_NOP
%token <tok> T_EXEC
%token <tok> T_EXEC_END
%token <tok> T_ALLOC
%token <tok> T_POSITION
%token <tok> T_PARAM_PIXEL
%token <tok> T_ADDR
%token <tok> T_CNT
%token <tok> T_SIZE
%token <tok> T_STRIDE
%token <tok> T_CONST
%token <num> T_REGISTER
%token <num> T_CONSTANT
%token <num> T_EXPORT
%token <tok> T_SYNC
%token <tok> T_FETCH
%token <tok> T_SAMPLE
%token <tok> T_VERTEX
%token <tok> T_ALU

/* @ headers (@const/@sampler/@uniform/@varying) */
%token <tok> T_A_ATTRIBUTE
%token <tok> T_A_CONST
%token <tok> T_A_SAMPLER
%token <tok> T_A_UNIFORM
%token <tok> T_A_VARYING

/* vector instructions: */
%token <tok> T_ADDv
%token <tok> T_MULv
%token <tok> T_MAXv
%token <tok> T_MINv
%token <tok> T_SETEv
%token <tok> T_SETGTv
%token <tok> T_SETGTEv
%token <tok> T_SETNEv
%token <tok> T_FRACv
%token <tok> T_TRUNCv
%token <tok> T_FLOORv
%token <tok> T_MULADDv
%token <tok> T_CNDEv
%token <tok> T_CNDGTEv
%token <tok> T_CNDGTv
%token <tok> T_DOT4v
%token <tok> T_DOT3v
%token <tok> T_DOT2ADDv
%token <tok> T_CUBEv
%token <tok> T_MAX4v
%token <tok> T_PRED_SETE_PUSHv
%token <tok> T_PRED_SETNE_PUSHv
%token <tok> T_PRED_SETGT_PUSHv
%token <tok> T_PRED_SETGTE_PUSHv
%token <tok> T_KILLEv
%token <tok> T_KILLGTv
%token <tok> T_KILLGTEv
%token <tok> T_KILLNEv
%token <tok> T_DSTv
%token <tok> T_MOVAv

/* scalar instructions: */
%token <tok> T_ADDs
%token <tok> T_ADD_PREVs
%token <tok> T_MULs
%token <tok> T_MUL_PREVs
%token <tok> T_MUL_PREV2s
%token <tok> T_MAXs
%token <tok> T_MINs
%token <tok> T_SETEs
%token <tok> T_SETGTs
%token <tok> T_SETGTEs
%token <tok> T_SETNEs
%token <tok> T_FRACs
%token <tok> T_TRUNCs
%token <tok> T_FLOORs
%token <tok> T_EXP_IEEE
%token <tok> T_LOG_CLAMP
%token <tok> T_LOG_IEEE
%token <tok> T_RECIP_CLAMP
%token <tok> T_RECIP_FF
%token <tok> T_RECIP_IEEE
%token <tok> T_RECIPSQ_CLAMP
%token <tok> T_RECIPSQ_FF
%token <tok> T_RECIPSQ_IEEE
%token <tok> T_MOVAs
%token <tok> T_MOVA_FLOORs
%token <tok> T_SUBs
%token <tok> T_SUB_PREVs
%token <tok> T_PRED_SETEs
%token <tok> T_PRED_SETNEs
%token <tok> T_PRED_SETGTs
%token <tok> T_PRED_SETGTEs
%token <tok> T_PRED_SET_INVs
%token <tok> T_PRED_SET_POPs
%token <tok> T_PRED_SET_CLRs
%token <tok> T_PRED_SET_RESTOREs
%token <tok> T_KILLEs
%token <tok> T_KILLGTs
%token <tok> T_KILLGTEs
%token <tok> T_KILLNEs
%token <tok> T_KILLONEs
%token <tok> T_SQRT_IEEE
%token <tok> T_MUL_CONST_0
%token <tok> T_MUL_CONST_1
%token <tok> T_ADD_CONST_0
%token <tok> T_ADD_CONST_1
%token <tok> T_SUB_CONST_0
%token <tok> T_SUB_CONST_1
%token <tok> T_SIN
%token <tok> T_COS
%token <tok> T_RETAIN_PREV

/* vertex fetch formats: */
%token <fmt> T_FMT_1_REVERSE
%token <fmt> T_GL_FLOAT
%token <fmt> T_FMT_32_FLOAT
%token <fmt> T_FMT_32_32_FLOAT
%token <fmt> T_FMT_32_32_32_FLOAT
%token <fmt> T_FMT_32_32_32_32_FLOAT
%token <fmt> T_FMT_16
%token <fmt> T_FMT_16_16
%token <fmt> T_FMT_16_16_16_16
%token <fmt> T_FMT_8
%token <fmt> T_FMT_8_8
%token <fmt> T_FMT_8_8_8_8
%token <fmt> T_FMT_32
%token <fmt> T_FMT_32_32
%token <fmt> T_FMT_32_32_32_32

/* vertex fetch attributes: */
%token <tok> T_UNSIGNED
%token <tok> T_SIGNED

%type <num> number
%type <reg> reg alu_src_reg reg_or_const reg_or_export
%type <tok> cf_alloc_type signedness alu_vec alu_vec_3src_op alu_vec_2src_op alu_vec_1src_op alu_scalar alu_scalar_op
%type <fmt> format
%type <range> reg_range const_range

%error-verbose

%start shader

%%

shader:            { shader = ir_shader_create(); } headers cfs

headers:           
|                  header headers

header:            attribute_header
|                  const_header
|                  sampler_header
|                  uniform_header
|                  varying_header

attribute_header:  T_A_ATTRIBUTE '(' reg_range ')' T_IDENTIFIER {
                       ir_attribute_create(shader, $3.start, $3.num, $5);
}

const_header:      T_A_CONST '(' T_CONSTANT ')' T_FLOAT ',' T_FLOAT ',' T_FLOAT ',' T_FLOAT {
                       ir_const_create(shader, $3, $5, $7, $9, $11);
}

sampler_header:    T_A_SAMPLER '(' number ')' T_IDENTIFIER {
                       ir_sampler_create(shader, $3, $5);
}

uniform_header:    T_A_UNIFORM '(' const_range ')' T_IDENTIFIER {
                       ir_uniform_create(shader, $3.start, $3.num, $5);
}

varying_header:    T_A_VARYING '(' reg_range ')' T_IDENTIFIER {
                       ir_varying_create(shader, $3.start, $3.num, $5);
}

reg_range:         T_REGISTER                { $$.start = $1; $$.num = 1; }
|                  T_REGISTER '-' T_REGISTER { $$.start = $1; $$.num = 1 + $3 - $1; }

const_range:       T_CONSTANT                { $$.start = $1; $$.num = 1; }
|                  T_CONSTANT '-' T_CONSTANT { $$.start = $1; $$.num = 1 + $3 - $1; }

cfs:               cf
|                  cf cfs

cf:                { cf = ir_cf_create(shader, T_NOP); }      T_NOP
|                  { cf = ir_cf_create(shader, T_ALLOC); }    cf_alloc
|                  { cf = ir_cf_create(shader, T_EXEC); }     cf_exec
|                  { cf = ir_cf_create(shader, T_EXEC_END); } cf_exec_end

cf_alloc:          T_ALLOC cf_alloc_type T_SIZE '(' number ')' { 
                       cf->alloc.type = $2;
                       cf->alloc.size = $5;
}

cf_alloc_type:     T_POSITION
|                  T_PARAM_PIXEL

cf_exec:           T_EXEC cf_exec_addr_cnt instrs
|                  T_EXEC instrs

cf_exec_end:       T_EXEC_END cf_exec_addr_cnt instrs
|                  T_EXEC_END instrs
|                  T_EXEC_END cf_exec_addr_cnt
|                  T_EXEC_END

cf_exec_addr_cnt:  T_ADDR '(' number ')' T_CNT '(' number ')' { 
                       cf->exec.addr = $3;
                       cf->exec.cnt = $7;
}

instrs:            instr
|                  instr instrs

instr:             fetch_or_alu
|                  T_SYNC fetch_or_alu { instr->sync = 1; }

fetch_or_alu:      { instr = ir_instr_create(cf, T_FETCH); } T_FETCH fetch
|                  { instr = ir_instr_create(cf, T_ALU); }   T_ALU   alu

fetch:             fetch_sample
|                  fetch_vertex

/* I'm assuming vertex and sample fetch have some different parameters, 
 * so keeping them separate for now.. if it turns out to be same, then
 * combine the grammar nodes later.
 */
fetch_sample:      T_SAMPLE reg '=' reg T_CONST '(' number ')' {
                       instr->fetch.opc = $1;
                       instr->fetch.const_idx = $7;
}

fetch_vertex:      T_VERTEX reg '=' reg format signedness T_STRIDE '(' number ')' T_CONST '(' number ',' number ')' {
                       instr->fetch.opc = $1;
                       instr->fetch.fmt = $5;
                       instr->fetch.sign = $6;
                       instr->fetch.stride = $9;
                       instr->fetch.const_idx = $13;
                       instr->fetch.const_idx_sel = $15;
}

format:            T_FMT_1_REVERSE
|                  T_FMT_32_FLOAT
|                  T_FMT_32_32_FLOAT
|                  T_FMT_32_32_32_FLOAT
|                  T_FMT_32_32_32_32_FLOAT
|                  T_FMT_16
|                  T_FMT_16_16
|                  T_FMT_16_16_16_16
|                  T_FMT_8
|                  T_FMT_8_8
|                  T_FMT_8_8_8_8
|                  T_FMT_32
|                  T_FMT_32_32
|                  T_FMT_32_32_32_32

signedness:        T_SIGNED
|                  T_UNSIGNED

/* TODO can we combine a 3src vec op w/ a scalar?? */
alu:               alu_vec {
                       instr->alu.vector_opc = $1;
}
|                  alu_vec alu_scalar {
                       instr->alu.vector_opc = $1;
                       instr->alu.scalar_opc = $2;
}

alu_vec:           alu_vec_3src_op reg_or_export '=' alu_src_reg ',' alu_src_reg ',' alu_src_reg
|                  alu_vec_2src_op reg_or_export '=' alu_src_reg ',' alu_src_reg
|                  alu_vec_1src_op reg_or_export '=' alu_src_reg

/* TODO how do ADD/MUL scalar work.. which should take 2 src ops */
alu_scalar:        alu_scalar_op   reg_or_export '=' alu_src_reg

alu_vec_3src_op:   T_MULADDv
|                  T_DOT2ADDv

alu_vec_2src_op:   T_ADDv
|                  T_MULv
|                  T_MAXv
|                  T_MINv
|                  T_SETEv
|                  T_SETGTv
|                  T_SETGTEv
|                  T_SETNEv
|                  T_CNDEv
|                  T_CNDGTEv
|                  T_CNDGTv
|                  T_DOT4v
|                  T_DOT3v
|                  T_CUBEv
|                  T_PRED_SETE_PUSHv
|                  T_PRED_SETNE_PUSHv
|                  T_PRED_SETGT_PUSHv
|                  T_PRED_SETGTE_PUSHv
|                  T_KILLEv
|                  T_KILLGTv
|                  T_KILLGTEv
|                  T_KILLNEv
|                  T_DSTv

alu_vec_1src_op:   T_FRACv
|                  T_TRUNCv
|                  T_FLOORv
|                  T_MAX4v
|                  T_MOVAv

/* MUL/ADD should take 2 srcs */
alu_scalar_op:     T_ADDs
|                  T_ADD_PREVs
|                  T_MULs
|                  T_MUL_PREVs
|                  T_MUL_PREV2s
|                  T_MAXs
|                  T_MINs
|                  T_SETEs
|                  T_SETGTs
|                  T_SETGTEs
|                  T_SETNEs
|                  T_FRACs
|                  T_TRUNCs
|                  T_FLOORs
|                  T_EXP_IEEE
|                  T_LOG_CLAMP
|                  T_LOG_IEEE
|                  T_RECIP_CLAMP
|                  T_RECIP_FF
|                  T_RECIP_IEEE
|                  T_RECIPSQ_CLAMP
|                  T_RECIPSQ_FF
|                  T_RECIPSQ_IEEE
|                  T_MOVAs
|                  T_MOVA_FLOORs
|                  T_SUBs
|                  T_SUB_PREVs
|                  T_PRED_SETEs
|                  T_PRED_SETNEs
|                  T_PRED_SETGTs
|                  T_PRED_SETGTEs
|                  T_PRED_SET_INVs
|                  T_PRED_SET_POPs
|                  T_PRED_SET_CLRs
|                  T_PRED_SET_RESTOREs
|                  T_KILLEs
|                  T_KILLGTs
|                  T_KILLGTEs
|                  T_KILLNEs
|                  T_KILLONEs
|                  T_SQRT_IEEE
|                  T_MUL_CONST_0
|                  T_MUL_CONST_1
|                  T_ADD_CONST_0
|                  T_ADD_CONST_1
|                  T_SUB_CONST_0
|                  T_SUB_CONST_1
|                  T_SIN
|                  T_COS
|                  T_RETAIN_PREV

alu_src_reg:       reg_or_const
|                  '|' reg_or_const      { $2->flags |= IR_REG_ABS; }
|                  '-' alu_src_reg       { $2->flags |= IR_REG_NEGATE; }

reg:               T_REGISTER {
                       $$ = ir_reg_create(instr, $1, NULL, 0);
}
|                  T_REGISTER T_SWIZZLE {
                       $$ = ir_reg_create(instr, $1, $2, 0);
}

reg_or_const:      reg
|                  T_CONSTANT {
                       $$ = ir_reg_create(instr, $1, NULL, IR_REG_CONST);
}
|                  T_CONSTANT T_SWIZZLE {
                       $$ = ir_reg_create(instr, $1, $2, IR_REG_CONST);
}

reg_or_export:     reg
|                  T_EXPORT {
                       $$ = ir_reg_create(instr, $1, NULL, IR_REG_EXPORT);
}
|                  T_EXPORT T_SWIZZLE {
                       $$ = ir_reg_create(instr, $1, $2, IR_REG_EXPORT);
}

number:            T_INT
|                  T_HEX
