/*
 * Hatari - Fix compilation with Visual Studio
 *
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 */
#ifndef VS_FIX_H
#define VS_FIX_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h> /* For getting SDK version */

// Stop Visual Studio complaining
#pragma warning (disable:4018)	/* signed / unsigned mismatch */
#pragma warning (disable:4049)	/* compiler limit, end of line numbering */
#pragma warning (disable:4101)	/* unreferenced local variable */
#pragma warning (disable:4102)	/* ignore unused label warning */
#pragma warning (disable:4146)	/* unary minus operator applied to unsigned type */
#pragma warning (disable:4244)	/* conversion with potential data loss */
#pragma warning (disable:4761)	/* integral size mismatch in argument */
#pragma warning (disable:4800)	/* Performance Warning on Conversion of bool to int */
#pragma warning (disable:4996)	/* Unsafe functions */

#ifndef NTDDI_WIN10_19H1 /* this makes compilation error in newer SDK's and obviously is not needed anymore (at least from SDK NTDDI_WIN10_19H1, but maybe even from older - not tested) */
#undef _DEBUG	/* Visual Studio is doing some macro redefinition otherwise */
#endif

typedef unsigned short mode_t;

#define	strncasecmp	_strnicmp
#define	strcasecmp	_stricmp

#endif
