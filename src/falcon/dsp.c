/*
	DSP M56001 emulation
	Dummy emulation, Hatari glue

	(C) 2001-2008 ARAnyM developer team
	Adaption to Hatari (C) 2008 by Thomas Huth

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "araglue.h"
#include "main.h"
#include "sysdeps.h"
#include "newcpu.h"
#include "ioMem.h"
#include "dsp.h"

#define DEBUG 0
#include "araglue.h"

#if DSP_EMULATION
static dsp_core_t dsp_core;
#endif

#define DSP_HW_OFFSET  0xFFA200


void DSP_Init(void)
{
#if DSP_EMULATION
	dsp_core_init(&dsp_core);
#endif
}

void DSP_UnInit(void)
{
#if DSP_EMULATION
	dsp_core_shutdown(&dsp_core);
#endif
}

/* Other functions to init/shutdown dsp emulation */
void DSP_Reset(void)
{
#if DSP_EMULATION
	dsp_core_reset(&dsp_core);
#endif
}

/**********************************
 *	Hardware address read/write by CPU
 **********************************/

static Uint8 DSP_handleRead(Uint32 addr)
{
	Uint8 value;
#if DSP_EMULATION
	value = dsp_core_read_host(&dsp_core, addr-DSP_HW_OFFSET);
#else
	/* this value prevents TOS from hanging in the DSP init code */
	value = 0xff;
#endif

	D(bug("HWget_b(0x%08x)=0x%02x at 0x%08x", addr, value, showPC()));
	return value;
}

void DSP_HandleReadAccess(void)
{
	Uint32 a;
	Uint8 v;
	for (a = IoAccessBaseAddress; a < IoAccessBaseAddress+nIoMemAccessSize; a++)
	{
		v = DSP_handleRead(a);
		IoMem_WriteByte(a, v);
	}
}


static void DSP_handleWrite(Uint32 addr, Uint8 value)
{
	D(bug("HWput_b(0x%08x,0x%02x) at 0x%08x", addr, value, showPC()));
#if DSP_EMULATION
	dsp_core_write_host(&dsp_core, addr-DSP_HW_OFFSET, value);
#endif
}

void DSP_HandleWriteAccess(void)
{
	Uint32 a;
	Uint8 v;
	for (a = IoAccessBaseAddress; a < IoAccessBaseAddress+nIoMemAccessSize; a++)
	{
		v = IoMem_ReadByte(a);
		DSP_handleWrite(a,v);
	}
}

/*
vim:ts=4:sw=4:
*/
