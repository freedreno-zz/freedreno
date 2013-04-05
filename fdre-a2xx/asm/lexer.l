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
#include "util.h"

#define TOKEN(t) (asm_yylval.tok = t)
#define FORMAT(f) asm_yylval.fmt = f; return T_ ## f
extern YYSTYPE asm_yylval;
%}

%option noyywrap
%option prefix="asm_yy"

%%
[ \t\n]                           ; /* ignore whitespace */
";"[^\n]*"\n"                     ; /* ignore comments */
[0-9]+"."[0-9]+                   asm_yylval.flt = strtod(yytext, NULL);       return T_FLOAT;
[0-9]*                            asm_yylval.num = strtol(yytext, NULL, 0);    return T_INT;
"0x"[0-9a-fA-F]*                  asm_yylval.num = strtol(yytext, NULL, 0);    return T_HEX;
"."[_w-z01][_w-z01]?[_w-z01]?[_w-z01]? asm_yylval.str = yytext + 1;            return T_SWIZZLE;
"@attribute"                      return TOKEN(T_A_ATTRIBUTE);
"@const"                          return TOKEN(T_A_CONST);
"@sampler"                        return TOKEN(T_A_SAMPLER);
"@uniform"                        return TOKEN(T_A_UNIFORM);
"@varying"                        return TOKEN(T_A_VARYING);
"NOP"                             return TOKEN(T_NOP);
"EXEC"                            return TOKEN(T_EXEC);
"EXEC_END"                        return TOKEN(T_EXEC_END);
"ALLOC"                           return TOKEN(T_ALLOC);
"POSITION"                        return TOKEN(T_POSITION);
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
"SETEv"                           return TOKEN(T_SETEv);
"SETGTv"                          return TOKEN(T_SETGTv);
"SETGTEv"                         return TOKEN(T_SETGTEv);
"SETNEv"                          return TOKEN(T_SETNEv);
"FRACv"                           return TOKEN(T_FRACv);
"TRUNCv"                          return TOKEN(T_TRUNCv);
"FLOORv"                          return TOKEN(T_FLOORv);
"MULADDv"                         return TOKEN(T_MULADDv);
"CNDEv"                           return TOKEN(T_CNDEv);
"CNDGTEv"                         return TOKEN(T_CNDGTEv);
"CNDGTv"                          return TOKEN(T_CNDGTv);
"DOT4v"                           return TOKEN(T_DOT4v);
"DOT3v"                           return TOKEN(T_DOT3v);
"DOT2ADDv"                        return TOKEN(T_DOT2ADDv);
"CUBEv"                           return TOKEN(T_CUBEv);
"MAX4v"                           return TOKEN(T_MAX4v);
"PRED_SETE_PUSHv"                 return TOKEN(T_PRED_SETE_PUSHv);
"PRED_SETNE_PUSHv"                return TOKEN(T_PRED_SETNE_PUSHv);
"PRED_SETGT_PUSHv"                return TOKEN(T_PRED_SETGT_PUSHv);
"PRED_SETGTE_PUSHv"               return TOKEN(T_PRED_SETGTE_PUSHv);
"KILLEv"                          return TOKEN(T_KILLEv);
"KILLGTv"                         return TOKEN(T_KILLGTv);
"KILLGTEv"                        return TOKEN(T_KILLGTEv);
"KILLNEv"                         return TOKEN(T_KILLNEv);
"DSTv"                            return TOKEN(T_DSTv);
"MOVAv"                           return TOKEN(T_MOVAv);
"ADDs"                            return TOKEN(T_ADDs);
"ADD_PREVs"                       return TOKEN(T_ADD_PREVs);
"MULs"                            return TOKEN(T_MULs);
"MUL_PREVs"                       return TOKEN(T_MUL_PREVs);
"MUL_PREV2s"                      return TOKEN(T_MUL_PREV2s);
"MAXs"                            return TOKEN(T_MAXs);
"MINs"                            return TOKEN(T_MINs);
"SETEs"                           return TOKEN(T_SETEs);
"SETGTs"                          return TOKEN(T_SETGTs);
"SETGTEs"                         return TOKEN(T_SETGTEs);
"SETNEs"                          return TOKEN(T_SETNEs);
"FRACs"                           return TOKEN(T_FRACs);
"TRUNCs"                          return TOKEN(T_TRUNCs);
"FLOORs"                          return TOKEN(T_FLOORs);
"EXP_IEEE"                        return TOKEN(T_EXP_IEEE);
"LOG_CLAMP"                       return TOKEN(T_LOG_CLAMP);
"LOG_IEEE"                        return TOKEN(T_LOG_IEEE);
"RECIP_CLAMP"                     return TOKEN(T_RECIP_CLAMP);
"RECIP_FF"                        return TOKEN(T_RECIP_FF);
"RECIP_IEEE"                      return TOKEN(T_RECIP_IEEE);
"RECIPSQ_CLAMP"                   return TOKEN(T_RECIPSQ_CLAMP);
"RECIPSQ_FF"                      return TOKEN(T_RECIPSQ_FF);
"RECIPSQ_IEEE"                    return TOKEN(T_RECIPSQ_IEEE);
"MOVAs"                           return TOKEN(T_MOVAs);
"MOVA_FLOORs"                     return TOKEN(T_MOVA_FLOORs);
"SUBs"                            return TOKEN(T_SUBs);
"SUB_PREVs"                       return TOKEN(T_SUB_PREVs);
"PRED_SETEs"                      return TOKEN(T_PRED_SETEs);
"PRED_SETNEs"                     return TOKEN(T_PRED_SETNEs);
"PRED_SETGTs"                     return TOKEN(T_PRED_SETGTs);
"PRED_SETGTEs"                    return TOKEN(T_PRED_SETGTEs);
"PRED_SET_INVs"                   return TOKEN(T_PRED_SET_INVs);
"PRED_SET_POPs"                   return TOKEN(T_PRED_SET_POPs);
"PRED_SET_CLRs"                   return TOKEN(T_PRED_SET_CLRs);
"PRED_SET_RESTOREs"               return TOKEN(T_PRED_SET_RESTOREs);
"KILLEs"                          return TOKEN(T_KILLEs);
"KILLGTs"                         return TOKEN(T_KILLGTs);
"KILLGTEs"                        return TOKEN(T_KILLGTEs);
"KILLNEs"                         return TOKEN(T_KILLNEs);
"KILLONEs"                        return TOKEN(T_KILLONEs);
"SQRT_IEEE"                       return TOKEN(T_SQRT_IEEE);
"MUL_CONST_0"                     return TOKEN(T_MUL_CONST_0);
"MUL_CONST_1"                     return TOKEN(T_MUL_CONST_1);
"ADD_CONST_0"                     return TOKEN(T_ADD_CONST_0);
"ADD_CONST_1"                     return TOKEN(T_ADD_CONST_1);
"SUB_CONST_0"                     return TOKEN(T_SUB_CONST_0);
"SUB_CONST_1"                     return TOKEN(T_SUB_CONST_1);
"SIN"                             return TOKEN(T_SIN);
"COS"                             return TOKEN(T_COS);
"RETAIN_PREV"                     return TOKEN(T_RETAIN_PREV);
"FMT_1_REVERSE"                   FORMAT(FMT_1_REVERSE);
"FMT_32_FLOAT"                    FORMAT(FMT_32_FLOAT);
"FMT_32_32_FLOAT"                 FORMAT(FMT_32_32_FLOAT);
"FMT_32_32_32_FLOAT"              FORMAT(FMT_32_32_32_FLOAT);
"FMT_32_32_32_32_FLOAT"           FORMAT(FMT_32_32_32_32_FLOAT);
"FMT_16"                          FORMAT(FMT_16);
"FMT_16_16"                       FORMAT(FMT_16_16);
"FMT_16_16_16_16"                 FORMAT(FMT_16_16_16_16);
"FMT_8"                           FORMAT(FMT_8);
"FMT_8_8"                         FORMAT(FMT_8_8);
"FMT_8_8_8_8"                     FORMAT(FMT_8_8_8_8);
"FMT_32"                          FORMAT(FMT_32);
"FMT_32_32"                       FORMAT(FMT_32_32);
"FMT_32_32_32_32"                 FORMAT(FMT_32_32_32_32);
"SIGNED"                          return TOKEN(T_SIGNED);
"UNSIGNED"                        return TOKEN(T_UNSIGNED);
"="                               return '=';
"("                               return '(';
")"                               return ')';
","                               return ',';
"-"                               return '-';
"|"                               return '|';
[a-zA-Z_][a-zA-Z_0-9]*            asm_yylval.str = yytext;                     return T_IDENTIFIER;
.                                 printf("Unknown token: %s\n", yytext); yyterminate();
%%
