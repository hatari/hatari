/*
  Hatari - ioMemTables.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_IOMEMTABLES_H
#define HATARI_IOMEMTABLES_H

/* Hardware address details */
typedef struct
{
	Uint32 Address;              /* ST hardware address */
	int SpanInBytes;             /* E.g. SIZE_BYTE, SIZE_WORD or SIZE_LONG */
	void *ReadFunc;              /* Read function */
	void *WriteFunc;             /* Write function */
} INTERCEPT_ACCESS_FUNC;

extern INTERCEPT_ACCESS_FUNC IoMemTable_ST[];
extern INTERCEPT_ACCESS_FUNC IoMemTable_STE[];
extern INTERCEPT_ACCESS_FUNC IoMemTable_TT[];

#endif
