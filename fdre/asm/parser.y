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

extern int yydebug;

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
	const char *str;
	struct ir_register *reg;
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
%token <str> T_SWIZZLE
%token <tok> T_NOP
%token <tok> T_EXEC
%token <tok> T_EXEC_END
%token <tok> T_ALLOC
%token <tok> T_COORD
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

/* vector instructions: */
%token <tok> T_ADDv
%token <tok> T_MULv
%token <tok> T_MAXv
%token <tok> T_MINv
%token <tok> T_FLOORv
%token <tok> T_MULADDv
%token <tok> T_DOT4v
%token <tok> T_DOT3v 

/* scalar instructions: */
%token <tok> T_MOV
%token <tok> T_EXP2
%token <tok> T_LOG2
%token <tok> T_RCP
%token <tok> T_RSQ
%token <tok> T_PSETE
%token <tok> T_SQRT
%token <tok> T_MUL
%token <tok> T_ADD

/* vertex fetch attributes: */
%token <tok> T_GL_FLOAT
%token <tok> T_GL_SHORT
%token <tok> T_GL_BYTE
%token <tok> T_GL_FIXED
%token <tok> T_SIGNED
%token <tok> T_UNSIGNED

%type <num> number
%type <reg> reg alu_src_reg reg_or_const reg_or_export
%type <tok> cf_alloc_type type signedness alu_vec alu_vec_3src_op alu_vec_2src_op alu_scalar alu_scalar_op

%error-verbose

%start shader

%%

shader:            { shader = ir_shader_create(); } cfs

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

cf_alloc_type:     T_COORD
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
                       instr->fetch.constant = $7;
}

fetch_vertex:      T_VERTEX reg '=' reg type signedness T_STRIDE '(' number ')' T_CONST '(' number ')' {
                       instr->fetch.opc = $1;
                       instr->fetch.type = $5;
                       instr->fetch.sign = $6;
                       instr->fetch.stride = $9;
                       instr->fetch.constant = $13;
}

type:              T_GL_FLOAT
|                  T_GL_SHORT
|                  T_GL_BYTE
|                  T_GL_FIXED

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

/* TODO how do ADD/MUL scalar work.. which should take 2 src ops */
alu_scalar:        alu_scalar_op   reg_or_export '=' alu_src_reg

alu_vec_3src_op:   T_MULADDv

alu_vec_2src_op:   T_ADDv
|                  T_MULv
|                  T_MAXv
|                  T_MINv
|                  T_FLOORv
|                  T_DOT4v
|                  T_DOT3v

/* MUL/ADD should take 2 srcs */
alu_scalar_op:     T_MOV
|                  T_EXP2
|                  T_LOG2
|                  T_RCP
|                  T_RSQ
|                  T_PSETE
|                  T_SQRT
|                  T_MUL
|                  T_ADD

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
