/*
* UAE - The Un*x Amiga Emulator
*
* Custom chip emulation
*
* Copyright 1995-2002 Bernd Schmidt
* Copyright 1995 Alessandro Bissacco
* Copyright 2000-2014 Toni Wilen
*/

#include "sysconfig.h"
#include "sysdeps.h"
#include "compat.h"
#include "hatari-glue.h"
#include "options_cpu.h"
#include "events.h"
#include "custom.h"
#include "newcpu.h"
#include "main.h"
#include "cpummu.h"
#include "m68000.h"
#include "debugui.h"
#include "debugcpu.h"
#ifdef WINUAE_FOR_HATARI
#include "debug.h"
#endif

#define WRITE_LOG_BUF_SIZE 4096

/* TODO: move custom.c stuff declarations to custom.h? */

#ifdef WINUAE_FOR_HATARI
/* declared in newcpu.c */
extern struct regstruct mmu_backup_regs;
/* declared in events.h */
unsigned long currcycle;
/* declared in savestate.h */
int savestate_state = 0;
#endif


uae_u16 dmacon;

static int extra_cycle;

#if 0
typedef struct _LARGE_INTEGER
{
     union
     {
          struct
          {
               unsigned long LowPart;
               long HighPart;
          };
          int64_t QuadPart;
     };
} LARGE_INTEGER, *PLARGE_INTEGER;
#endif


#ifdef CPUEMU_13

uae_u8 cycle_line[256 + 1];

static void sync_ce020 (void)
{
	unsigned long c;
	int extra;

	c = get_cycles ();
	extra = c & (CYCLE_UNIT - 1);
	if (extra) {
		extra = CYCLE_UNIT - extra;
		do_cycles (extra);
	}
}

#ifndef WINUAE_FOR_HATARI
#define SETIFCHIP \
	if (addr < 0xd80000) \
		last_custom_value1 = v;
#endif		/* WINUAE_FOR_HATARI */

uae_u32 wait_cpu_cycle_read (uaecptr addr, int mode)
{
	uae_u32 v = 0;
#ifndef WINUAE_FOR_HATARI
	int hpos;

	hpos = dma_cycle ();
	x_do_cycles_pre (CYCLE_UNIT);

#ifdef DEBUGGER
	struct dma_rec *dr = NULL;
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

	x_do_cycles_post (CYCLE_UNIT, v);

	regs.chipset_latch_rw = regs.chipset_latch_read = v;
	SETIFCHIP

#else						/* WINUAE_FOR_HATARI */
//	fprintf ( stderr , "mem read ce %x %d %lu %lu\n" , addr , mode ,currcycle / cpucycleunit , currcycle );
	if ( ( ( CyclesGlobalClockCounter + currcycle*2/CYCLE_UNIT ) & 3 ) == 2 )
	{
//		fprintf ( stderr , "mem wait read %x %d %lu %lu\n" , addr , mode , currcycle / cpucycleunit , currcycle );
		x_do_cycles (2*cpucycleunit);
//		fprintf ( stderr , "mem wait read after %x %d %lu %lu\n" , addr , mode , currcycle / cpucycleunit , currcycle );
	}

	if (mode < 0)
		v = get_long (addr);
	else if (mode > 0)
		v = get_word (addr);
	else if (mode == 0)
		v = get_byte (addr);

	x_do_cycles_post (2*CYCLE_UNIT, v);
#endif						/* WINUAE_FOR_HATARI */

	return v;
}

uae_u32 wait_cpu_cycle_read_ce020 (uaecptr addr, int mode)
{
	uae_u32 v = 0;
#ifndef WINUAE_FOR_HATARI
	int hpos;

	sync_ce020 ();
	hpos = dma_cycle ();
	x_do_cycles_pre (CYCLE_UNIT);

#ifdef DEBUGGER
	struct dma_rec *dr = NULL;
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
	if (currprefs.cpu_model == 68020)
		x_do_cycles_post (CYCLE_UNIT / 2, v);

	regs.chipset_latch_rw = regs.chipset_latch_read = v;
	SETIFCHIP

#else						/* WINUAE_FOR_HATARI */
	sync_ce020 ();
	x_do_cycles_pre (CYCLE_UNIT);

	if (mode < 0)
		v = get_long (addr);
	else if (mode > 0)
		v = get_word (addr);
	else if (mode == 0)
		v = get_byte (addr);

	if (currprefs.cpu_model == 68020)
		x_do_cycles_post (CYCLE_UNIT / 2, v);
#endif						/* WINUAE_FOR_HATARI */

	return v;
}

void wait_cpu_cycle_write (uaecptr addr, int mode, uae_u32 v)
{
#ifndef WINUAE_FOR_HATARI
	int hpos;

	hpos = dma_cycle ();
	x_do_cycles_pre (CYCLE_UNIT);

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

	x_do_cycles_post (CYCLE_UNIT, v);

	regs.chipset_latch_rw = regs.chipset_latch_write = v;
	SETIFCHIP

#else						/* WINUAE_FOR_HATARI */
//	fprintf ( stderr , "mem write ce %x %d %lu %lu\n" , addr , mode ,currcycle / cpucycleunit , currcycle );
	if ( ( ( CyclesGlobalClockCounter + currcycle*2/CYCLE_UNIT ) & 3 ) == 2 )
	{
//		fprintf ( stderr , "mem wait write %x %d %lu %lu\n" , addr , mode , currcycle / cpucycleunit , currcycle );
		x_do_cycles (2*cpucycleunit);
//		fprintf ( stderr , "mem wait write after %x %d %lu %lu\n" , addr , mode , currcycle / cpucycleunit , currcycle );
	}

	if (mode < 0)
		put_long (addr, v);
	else if (mode > 0)
		put_word (addr, v);
	else if (mode == 0)
		put_byte (addr, v);

	x_do_cycles_post (2*CYCLE_UNIT, v);
#endif						/* WINUAE_FOR_HATARI */
}

void wait_cpu_cycle_write_ce020 (uaecptr addr, int mode, uae_u32 v)
{
#ifndef WINUAE_FOR_HATARI
	int hpos;

	sync_ce020 ();
	hpos = dma_cycle ();
	x_do_cycles_pre (CYCLE_UNIT);

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

	if (currprefs.cpu_model == 68020)
		x_do_cycles_post (CYCLE_UNIT / 2, v);

	regs.chipset_latch_rw = regs.chipset_latch_write = v;
	SETIFCHIP

#else						/* WINUAE_FOR_HATARI */
	sync_ce020 ();
	x_do_cycles_pre (CYCLE_UNIT);

	if (mode < 0)
		put_long (addr, v);
	else if (mode > 0)
		put_word (addr, v);
	else if (mode == 0)
		put_byte (addr, v);

	if (currprefs.cpu_model == 68020)
		x_do_cycles_post (CYCLE_UNIT / 2, v);
#endif						/* WINUAE_FOR_HATARI */
}

void do_cycles_ce (unsigned long cycles)
{
	cycles += extra_cycle;
	while (cycles >= CYCLE_UNIT) {
#ifndef WINUAE_FOR_HATARI
		int hpos = current_hpos () + 1;
		decide_line (hpos);
		sync_copper (hpos);
		decide_fetch_ce (hpos);
		if (bltstate != BLT_done)
			decide_blitter (hpos);
#endif						/* WINUAE_FOR_HATARI */
		do_cycles (1 * CYCLE_UNIT);
		cycles -= CYCLE_UNIT;
	}
	extra_cycle = cycles;
}


#ifndef WINUAE_FOR_HATARI
void do_cycles_ce020 (unsigned long cycles)
#else
/* [NP] : confusing, because same function name as in cpu_prefetch.h with do_cycles_ce020( int ), */
/* but here unsigned long parameter is already multiplied by cpucycleunit. */
/* Requires C++, so we rename to do_cycles_ce020_long() to keep C compatibility */
void do_cycles_ce020_long (unsigned long cycles)
#endif
{
	unsigned long c;
#ifndef WINUAE_FOR_HATARI
	int extra;
#else
	unsigned long extra;			/* remove warning "comparison between signed/unsigned" */
#endif

	if (!cycles)
		return;
	c = get_cycles ();
	extra = c & (CYCLE_UNIT - 1);
//fprintf ( stderr , "do_cycles_ce020_long %d %d %d\n" , cycles , c , extra );
	if (extra) {
		extra = CYCLE_UNIT - extra;
		if (extra >= cycles) {
			do_cycles (cycles);
			return;
		}
		do_cycles (extra);
		cycles -= extra;
	}
	c = cycles;
	while (c) {
#ifndef WINUAE_FOR_HATARI
		int hpos = current_hpos () + 1;
		decide_line (hpos);
		sync_copper (hpos);
		decide_fetch_ce (hpos);
		if (bltstate != BLT_done)
			decide_blitter (hpos);
#endif						/* WINUAE_FOR_HATARI */
		if (c < CYCLE_UNIT)
			break;
		do_cycles (1 * CYCLE_UNIT);
		c -= CYCLE_UNIT;
	}
	if (c > 0)
		do_cycles (c);
}

int is_cycle_ce (void)
{
#ifndef WINUAE_FOR_HATARI
	int hpos = current_hpos ();
	return cycle_line[hpos] & CYCLE_MASK;

#else						/* WINUAE_FOR_HATARI */
	return 0;
#endif						/* WINUAE_FOR_HATARI */
}

#endif



void reset_frame_rate_hack (void)
{
#ifndef WINUAE_FOR_HATARI
        jitcount = 0;
        if (currprefs.m68k_speed >= 0)
                return;

        rpt_did_reset = 1;
        is_syncline = 0;
        vsyncmintime = read_processor_time () + vsynctimebase;
        write_log (_T("Resetting frame rate hack\n"));
#endif						/* WINUAE_FOR_HATARI */
}

/* Code taken from main.cpp */
void fixup_cpu (struct uae_prefs *p)
{
	if (p->cpu_frequency == 1000000)
		p->cpu_frequency = 0;

#ifndef WINUAE_FOR_HATARI
	if (p->cpu_model >= 68030 && p->address_space_24) {
		error_log (_T("24-bit address space is not supported in 68030/040/060 configurations."));
		p->address_space_24 = 0;
	}
#else
        /* Hatari : don't force address_space_24=0 for 68030, as the Falcon has a 68030 EC with only 24 bits */
#endif
	if (p->cpu_model < 68020 && p->fpu_model && (p->cpu_compatible || p->cpu_cycle_exact)) {
		error_log (_T("FPU is not supported in 68000/010 configurations."));
		p->fpu_model = 0;
	}

	switch (p->cpu_model)
	{
	case 68000:
		p->address_space_24 = 1;
		break;
	case 68010:
		p->address_space_24 = 1;
		break;
	case 68020:
		break;
	case 68030:
		break;
	case 68040:
		if (p->fpu_model)
			p->fpu_model = 68040;
		break;
	case 68060:
		if (p->fpu_model)
			p->fpu_model = 68060;
		break;
	}

	if (p->cpu_model < 68020 && p->cachesize) {
		p->cachesize = 0;
		error_log (_T("JIT requires 68020 or better CPU."));
	}

	if (p->cpu_model >= 68040 && p->cachesize && p->cpu_compatible)
		p->cpu_compatible = false;

	if ((p->cpu_model < 68030 || p->cachesize) && p->mmu_model) {
		error_log (_T("MMU emulation requires 68030/040/060 and it is not JIT compatible."));
		p->mmu_model = 0;
	}

	if (p->cachesize && p->cpu_cycle_exact) {
		error_log (_T("JIT and cycle-exact can't be enabled simultaneously."));
		p->cachesize = 0;
	}
	if (p->cachesize && (p->fpu_no_unimplemented || p->int_no_unimplemented)) {
		error_log (_T("JIT is not compatible with unimplemented CPU/FPU instruction emulation."));
		p->fpu_no_unimplemented = p->int_no_unimplemented = false;
	}

#ifndef WINUAE_FOR_HATARI
	/* [NP] In Hatari, don't change m68k_speed in CE mode */
	if (p->cpu_cycle_exact && p->m68k_speed < 0)
		p->m68k_speed = 0;
#endif

#ifndef WINUAE_FOR_HATARI
	if (p->immediate_blits && p->blitter_cycle_exact) {
		error_log (_T("Cycle-exact and immediate blitter can't be enabled simultaneously.\n"));
		p->immediate_blits = false;
	}
	if (p->immediate_blits && p->waiting_blits) {
		error_log (_T("Immediate blitter and waiting blits can't be enabled simultaneously.\n"));
		p->waiting_blits = 0;
	}
#endif
	if (p->cpu_cycle_exact)
		p->cpu_compatible = true;
}


void custom_reset (bool hardreset, bool keyboardreset)
{
}


// TODO NP remove ?
#ifndef WINUAE_FOR_HATARI
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
#endif


/* Code taken from win32.cpp*/
void fpux_restore (int *v)
{
/*#ifndef _WIN64
	if (v)
		_controlfp (*v, _MCW_IC | _MCW_RC | _MCW_PC);
	else
		_controlfp (fpucontrol, _MCW_IC | _MCW_RC | _MCW_PC);
#endif
*/
}

// TODO NP remove ?
/* Code taken from win32.cpp*/
void sleep_millis (int ms)
{
/* Laurent : may be coded later (DSL-Delay ?) */
}


/* Code just here to let newcpu.c link (original function is in inprec.cpp) */
int inprec_open(char *fname, int record)
{
	return 0;
}

// TODO NP remove ?
#ifndef WINUAE_FOR_HATARI
int current_hpos (void)
{
    return (get_cycles () - eventtab[ev_hsync].oldcycles) / CYCLE_UNIT;
}
#endif


