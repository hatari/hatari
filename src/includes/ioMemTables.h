/*
  Hatari - ioMemTables.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_IOMEMTABLES_H
#define HATARI_IOMEMTABLES_H

/* Hardware address details */
typedef struct
{
	const Uint32 Address;     /* ST hardware address */
	const int SpanInBytes;    /* E.g. SIZE_BYTE, SIZE_WORD or SIZE_LONG */
	void (*ReadFunc)(void);   /* Read function */
	void (*WriteFunc)(void);  /* Write function */
} INTERCEPT_ACCESS_FUNC;

extern const INTERCEPT_ACCESS_FUNC IoMemTable_ST[];
extern const INTERCEPT_ACCESS_FUNC IoMemTable_STE[];
extern const INTERCEPT_ACCESS_FUNC IoMemTable_TT[];
extern const INTERCEPT_ACCESS_FUNC IoMemTable_Falcon[];

extern void IoMemTabFalcon_DSPnone(void (**readtab)(void), void (**writetab)(void));
extern void IoMemTabFalcon_DSPdummy(void (**readtab)(void), void (**writetab)(void));
#if ENABLE_DSP_EMU
extern void IoMemTabFalcon_DSPemulation(void (**readtab)(void), void (**writetab)(void));
#endif

extern void IoMemTabMegaSTE_CacheCpuCtrl_WriteByte(void);
#endif
