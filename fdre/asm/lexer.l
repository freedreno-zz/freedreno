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
#include <stdlib.h>
#include "parser.h"

#define TOKEN(t) (asm_yylval.tok = t)
extern YYSTYPE asm_yylval;
%}

%option noyywrap
%option prefix="asm_yy"

%%
[ \t\n]                           ; /* ignore whitespace */
";"[^\n]*"\n"                     ; /* ignore comments */
[1-9][0-9]*                       asm_yylval.num = strtol(yytext, NULL, 0);    return T_INT;
"0x"[0-9a-fA-F]*                  asm_yylval.num = strtol(yytext, NULL, 0);    return T_HEX;
"."[_w-z01][_w-z01]?[_w-z01]?[_w-z01]? asm_yylval.str = yytext + 1;            return T_SWIZZLE;
"NOP"                             return TOKEN(T_NOP);
"EXEC"                            return TOKEN(T_EXEC);
"EXEC_END"                        return TOKEN(T_EXEC_END);
"ALLOC"                           return TOKEN(T_ALLOC);
"COORD"                           return TOKEN(T_COORD);
"PARAM/PIXEL"                     return TOKEN(T_PARAM_PIXEL);
"ADDR"                            return TOKEN(T_ADDR);
"CNT"                             return TOKEN(T_CNT);
"SIZE"                            return TOKEN(T_SIZE);
"CONST"                           return TOKEN(T_CONST);
"STRIDE"                          return TOKEN(T_STRIDE);
"R"[0-9]+                         asm_yylval.num = strtol(yytext+1, NULL, 10); return T_REGISTER;
"C"[0-9]+                         asm_yylval.num = strtol(yytext+1, NULL, 10); return T_CONSTANT;
"export"[0-9]+                    asm_yylval.num = strtol(yytext+6, NULL, 10); return T_EXPORT;
"(S)"                             return TOKEN(T_SYNC);
"FETCH:"                          return TOKEN(T_FETCH);
"SAMPLE"                          return TOKEN(T_SAMPLE);
"VERTEX"                          return TOKEN(T_VERTEX);
"ALU:"                            return TOKEN(T_ALU);
"ADDv"                            return TOKEN(T_ADDv);
"MULv"                            return TOKEN(T_MULv);
"MAXv"                            return TOKEN(T_MAXv);
"MINv"                            return TOKEN(T_MINv);
"FLOORv"                          return TOKEN(T_FLOORv);
"MULADDv"                         return TOKEN(T_MULADDv);
"DOT4v"                           return TOKEN(T_DOT4v);
"DOT3v"                           return TOKEN(T_DOT3v);
"MOV"                             return TOKEN(T_MOV);
"EXP2"                            return TOKEN(T_EXP2);
"LOG2"                            return TOKEN(T_LOG2);
"RCP"                             return TOKEN(T_RCP);
"RSQ"                             return TOKEN(T_RSQ);
"PSETE"                           return TOKEN(T_PSETE);
"SQRT"                            return TOKEN(T_SQRT);
"MUL"                             return TOKEN(T_MUL);
"ADD"                             return TOKEN(T_ADD);
"GL_FLOAT"                        return TOKEN(T_GL_FLOAT);
"GL_SHORT"                        return TOKEN(T_GL_SHORT);
"GL_BYTE"                         return TOKEN(T_GL_BYTE);
"GL_FIXED"                        return TOKEN(T_GL_FIXED);
"SIGNED"                          return TOKEN(T_SIGNED);
"UNSIGNED"                        return TOKEN(T_UNSIGNED);
"="                               return '=';
"("                               return '(';
")"                               return ')';
","                               return ',';
"-"                               return '-';
"|"                               return '|';
.                                 printf("Unknown token: %s\n", yytext); yyterminate();
%%
