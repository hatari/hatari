/*
  Hatari - evaluate.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_EVALUATE_H
#define HATARI_EVALUATE_H

typedef enum {
	NUM_TYPE_CPU,
	NUM_TYPE_DSP,
	NUM_TYPE_NORMAL // neither CPU nor DSP memory address, just number
} num_type_t;

extern bool Eval_Number(const char *value, uint32_t *number, num_type_t type);
extern int Eval_Range(char *str, uint32_t *lower, uint32_t *upper, bool bForDsp);
extern const char* Eval_Expression(const char *expression, uint32_t *result, int *offset, bool bForDsp);

#endif
