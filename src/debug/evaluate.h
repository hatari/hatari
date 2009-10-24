/*
  Hatari - calculate.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_CALCULATE_H
#define HATARI_CALCULATE_H

extern bool Eval_Number(const char *value, Uint32 *number);
extern int Eval_Range(char *str, Uint32 *lower, Uint32 *upper);
extern const char* Eval_Expression(const char *expression, Uint32 *result, int *offset);

#endif
