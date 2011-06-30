/*
  Hatari - evaluate.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_EVALUATE_H
#define HATARI_EVALUATE_H

extern bool Eval_Number(const char *value, Uint32 *number);
extern int Eval_Range(char *str, Uint32 *lower, Uint32 *upper, bool bForDsp);
extern const char* Eval_Expression(const char *expression, Uint32 *result, int *offset, bool bForDsp);

#endif
