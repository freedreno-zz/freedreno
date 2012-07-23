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
void fd_asm_parse(const char *src)
{
	YY_BUFFER_STATE buffer = asm_yy_scan_string(src);
#ifdef YYDEBUG
	yydebug = 1;
#endif
	yyparse();
	asm_yy_delete_buffer(buffer);
}
%}

%union {
	int tok;
	int num;
	const char *str;
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

%error-verbose

%start shader

%%

shader:            cfs

cfs:               cf
|                  cf cfs

cf:                T_NOP
|                  cf_alloc
|                  cf_exec
|                  cf_exec_end

cf_alloc:          T_ALLOC cf_alloc_type T_SIZE '(' number ')'

cf_alloc_type:     T_COORD
|                  T_PARAM_PIXEL

cf_exec:           T_EXEC cf_exec_addr_cnt instrs
|                  T_EXEC instrs

cf_exec_end:       T_EXEC_END cf_exec_addr_cnt instrs
|                  T_EXEC_END instrs
|                  T_EXEC_END cf_exec_addr_cnt
|                  T_EXEC_END

cf_exec_addr_cnt:  T_ADDR '(' number ')' T_CNT '(' number ')'

instrs:            instr
|                  instr instrs

instr:             fetch
|                  alu
|                  T_SYNC fetch
|                  T_SYNC alu

fetch:             T_FETCH fetch_sample
|                  T_FETCH fetch_vertex

fetch_sample:      T_SAMPLE reg '=' reg T_CONST '(' number ')'

fetch_vertex:      T_VERTEX reg '=' reg T_CONST '(' number ')'

/* TODO can we combine a 3src vec op w/ a scalar?? */
alu:               T_ALU alu_vec
|                  T_ALU alu_vec alu_scalar

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
|                  '-' reg_or_const
|                  '|' reg_or_const

reg:               T_REGISTER
|                  T_REGISTER T_SWIZZLE

reg_or_const:      T_REGISTER
|                  T_REGISTER T_SWIZZLE
|                  T_CONSTANT
|                  T_CONSTANT T_SWIZZLE

reg_or_export:     T_REGISTER
|                  T_REGISTER T_SWIZZLE
|                  T_EXPORT
|                  T_EXPORT   T_SWIZZLE

number:            T_INT
|                  T_HEX
