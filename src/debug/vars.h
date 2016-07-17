/*
  Hatari - vars.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_VARS_H
#define HATARI_VARS_H

typedef enum {
	/* plain number */
	VALUE_TYPE_NUMBER     = 0,

	/* functions to call to get value */
	VALUE_TYPE_FUNCTION32 = 2,

	/* internal Hatari value variables */
	VALUE_TYPE_VAR32      = 4,

	/* size must match register size used in BreakCond_ParseRegister() */
	VALUE_TYPE_REG16      = 16,
	VALUE_TYPE_REG32      = 32
} value_t;


/* Hatari variable name & address array items */
typedef struct {
	const char *name;
	Uint32 *addr;
	value_t vtype;
	size_t bits;
	const char *info;
} var_addr_t;


/* variables + CPU symbols readline callback for breakpoints & evaluate */
extern char *Vars_MatchCpuVariable(const char *text, int state);

/* pointer to variable struct with given variable name */
extern const var_addr_t *Vars_ParseVariable(const char *name);

/* get variable value for given variable name */
bool Vars_GetVariableValue(const char *name, Uint32 *value);

/* get variable value for given variable struct */
extern Uint32 Vars_GetValue(const var_addr_t *hvar);

/* list variables & their names */
extern int Vars_List(int nArgc, char *psArgv[]);

#endif
