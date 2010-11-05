/*
* UAE - The Un*x Amiga Emulator
*
* Custom chip emulation
*
* Copyright 1995-2002 Bernd Schmidt
* Copyright 1995 Alessandro Bissacco
* Copyright 2000-2010 Toni Wilen
*/

#include "sysconfig.h"
#include "sysdeps.h"
#include "compat.h"
#include "hatari-glue.h"
#include "options_cpu.h"
#include "custom.h"
#include "newcpu.h"
#include "cpummu.h"
#include "cpu_prefetch.h"
#include "m68000.h"
#include "debugui.h"
#include "debugcpu.h"

#define WRITE_LOG_BUF_SIZE 4096

void do_cycles_ce (long cycles);
unsigned long int event_cycles, nextevent, is_lastline, currcycle;
uae_u32 wait_cpu_cycle_read (uaecptr addr, int mode);
void wait_cpu_cycle_write (uaecptr addr, int mode, uae_u32 v);
void wait_cpu_cycle_write_ce020 (uaecptr addr, int mode, uae_u32 v);
uae_u32 wait_cpu_cycle_read_ce020 (uaecptr addr, int mode);
TCHAR* buf_out (TCHAR *buffer, int *bufsize, const TCHAR *format, ...);


struct ev eventtab[ev_max];

void do_cycles_ce (long cycles)
{
	static int extra_cycle;

	cycles += extra_cycle;
/*
	while (cycles >= CYCLE_UNIT) {
		int hpos = current_hpos () + 1;
		sync_copper (hpos);
		decide_line (hpos);
		decide_fetch_ce (hpos);
		if (bltstate != BLT_done)
			decide_blitter (hpos);
		do_cycles (1 * CYCLE_UNIT);
		cycles -= CYCLE_UNIT;
	}
*/
	extra_cycle = cycles;
}

void wait_cpu_cycle_write_ce020 (uaecptr addr, int mode, uae_u32 v)
{
	int hpos;

/*
	hpos = dma_cycle ();
	do_cycles_ce (CYCLE_UNIT);
*/
#ifdef DEBUGGER
	if (debug_dma) {
		int reg = 0x1100;
		if (mode < 0)
			reg |= 4;
		else if (mode > 0)
			reg |= 2;
		else
			reg |= 1;
		record_dma (reg, v, addr, hpos, vpos, DMARECORD_CPU);
		checknasty (hpos, vpos);
	}
#endif

	if (mode < 0)
		put_long (addr, v);
	else if (mode > 0)
		put_word (addr, v);
	else if (mode == 0)
		put_byte (addr, v);

	regs.ce020memcycles -= CYCLE_UNIT;
}

uae_u32 wait_cpu_cycle_read_ce020 (uaecptr addr, int mode)
{
	uae_u32 v = 0;
	int hpos;
	struct dma_rec *dr;

/*
	hpos = dma_cycle ();
	do_cycles_ce (CYCLE_UNIT);
*/

#ifdef DEBUGGER
	if (debug_dma) {
		int reg = 0x1000;
		if (mode < 0)
			reg |= 4;
		else if (mode > 0)
			reg |= 2;
		else
			reg |= 1;
		dr = record_dma (reg, v, addr, hpos, vpos, DMARECORD_CPU);
		checknasty (hpos, vpos);
	}
#endif
	if (mode < 0)
		v = get_long (addr);
	else if (mode > 0)
		v = get_word (addr);
	else if (mode == 0)
		v = get_byte (addr);

#ifdef DEBUGGER
	if (debug_dma)
		dr->dat = v;
#endif

	regs.ce020memcycles -= CYCLE_UNIT;
	return v;
}

uae_u32 wait_cpu_cycle_read (uaecptr addr, int mode)
{
	uae_u32 v = 0;
	int hpos;
	struct dma_rec *dr;

/*
	hpos = dma_cycle ();
	do_cycles_ce (CYCLE_UNIT);
*/

#ifdef DEBUGGER
	if (debug_dma) {
		int reg = 0x1000;
		if (mode < 0)
			reg |= 4;
		else if (mode > 0)
			reg |= 2;
		else
			reg |= 1;
		dr = record_dma (reg, v, addr, hpos, vpos, DMARECORD_CPU);
		checknasty (hpos, vpos);
	}
#endif
	if (mode < 0)
		v = get_long (addr);
	else if (mode > 0)
		v = get_word (addr);
	else if (mode == 0)
		v = get_byte (addr);

#ifdef DEBUGGER
	if (debug_dma)
		dr->dat = v;
#endif

	do_cycles_ce (CYCLE_UNIT);
	return v;
}

void wait_cpu_cycle_write (uaecptr addr, int mode, uae_u32 v)
{
	int hpos;

/*
	hpos = dma_cycle ();
	do_cycles_ce (CYCLE_UNIT);
*/

#ifdef DEBUGGER
	if (debug_dma) {
		int reg = 0x1100;
		if (mode < 0)
			reg |= 4;
		else if (mode > 0)
			reg |= 2;
		else
			reg |= 1;
		record_dma (reg, v, addr, hpos, vpos, DMARECORD_CPU);
		checknasty (hpos, vpos);
	}
#endif

	if (mode < 0)
		put_long (addr, v);
	else if (mode > 0)
		put_word (addr, v);
	else if (mode == 0)
		put_byte (addr, v);
	do_cycles_ce (CYCLE_UNIT);

}

/* Taken from writelog.cpp */
TCHAR* buf_out (TCHAR *buffer, int *bufsize, const TCHAR *format, ...)
{
	int count;
	va_list parms;
	va_start (parms, format);

	if (buffer == NULL)
		return 0;
	count = _vsntprintf (buffer, (*bufsize) - 1, format, parms);
	va_end (parms);
	*bufsize -= _tcslen (buffer);
	return buffer + _tcslen (buffer);
}

/* Taken from writelog.cpp */
void f_out (void *f, const TCHAR *format, ...)
{
/*
	int count;
	TCHAR buffer[WRITE_LOG_BUF_SIZE];
	va_list parms;
	va_start (parms, format);

	if (f == NULL)
		return;
	count = _vsntprintf (buffer, WRITE_LOG_BUF_SIZE - 1, format, parms);
	openconsole ();
	writeconsole (buffer);
	va_end (parms);
*/
}

/* Code taken from main.cpp */
void fixup_cpu (struct uae_prefs *p)
{
	if (p->cpu_frequency == 1000000)
		p->cpu_frequency = 0;
	switch (p->cpu_model)
	{
	case 68000:
		p->address_space_24 = 1;
		if (p->cpu_compatible || p->cpu_cycle_exact)
			p->fpu_model = 0;
		break;
	case 68010:
		p->address_space_24 = 1;
		if (p->cpu_compatible || p->cpu_cycle_exact)
			p->fpu_model = 0;
		break;
	case 68020:
		break;
	case 68030:
		p->address_space_24 = 0;
		break;
	case 68040:
		p->address_space_24 = 0;
		if (p->fpu_model)
			p->fpu_model = 68040;
		break;
	case 68060:
		p->address_space_24 = 0;
		if (p->fpu_model)
			p->fpu_model = 68060;
		break;
	}
	if (p->cpu_model != 68040)
		p->mmu_model = 0;
}

/* Code taken from main.cpp*/
void uae_reset (int hardreset)
{
	currprefs.quitstatefile[0] = changed_prefs.quitstatefile[0] = 0;

	if (quit_program == 0) {
		quit_program = -2;
		if (hardreset)
			quit_program = -3;
	}

}

int current_hpos (void)
{
    return (get_cycles () - eventtab[ev_hsync].oldcycles) / CYCLE_UNIT;
}
