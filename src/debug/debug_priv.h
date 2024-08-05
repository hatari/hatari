/*
  Hatari - debug.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Internal header used by debugger files.
*/
#ifndef HATARI_DEBUG_PRIV_H
#define HATARI_DEBUG_PRIV_H

/* internal defines for checks */
#define TTRAM_START	0x01000000
#define CART_START	0xFA0000
#define CART_END	0xFC0000

/* DebugUI command structure */
typedef struct
{
	int (*pFunction)(int argc, char *argv[]);
	char* (*pMatch)(const char *, int);
	const char *sLongName;
	const char *sShortName;
	const char *sShortDesc;
	const char *sUsage;
	bool bNoParsing;
} dbgcommand_t;

/* Output file debugger output */
extern FILE *debugOutput;

extern int DebugUI_PrintCmdHelp(const char *psCmd);
extern int DebugUI_GetPageLines(int config, int defvalue);
extern void DebugUI_PrintBinary(FILE *fp, int minwidth, uint32_t value);
extern char *DebugUI_MatchHelper(const char **strings, int items, const char *text, int state);
extern bool DebugUI_ParseFile(const char *path, bool reinit, bool verbose);
extern bool DebugUI_DoQuitQuery(const char *info);

extern int DebugCpu_Init(const dbgcommand_t **table);
extern void DebugCpu_InitSession(void);

#ifdef ENABLE_DSP_EMU
extern int DebugDsp_Init(const dbgcommand_t **table);
extern void DebugDsp_InitSession(void);
#else /* !ENABLE_DSP_EMU */
static inline int DebugDsp_Init(const dbgcommand_t **t) { *t = NULL; return 0; }
#define DebugDsp_InitSession()
#endif /* !ENABLE_DSP_EMU */

#endif /* HATARI_DEBUG_PRIV_H */
