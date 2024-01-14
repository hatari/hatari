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
#include "blitter.h"
#include "debug.h"
#include "savestate.h"

#define WRITE_LOG_BUF_SIZE 4096

/* TODO: move custom.c stuff declarations to custom.h? */

#ifdef WINUAE_FOR_HATARI
/* declared in newcpu.c */
extern struct regstruct mmu_backup_regs;
/* declared in events.h */
evt_t currcycle;
/* declared in savestate.h */
int savestate_state = 0;
TCHAR savestate_fname[MAX_DPATH];
/* declared in custom.h */
uae_u32 hsync_counter = 0, vsync_counter = 0;
#endif


uae_u16 dmacon;

uae_u32 extra_cycle;

#ifdef CPUEMU_13

#ifndef WINUAE_FOR_HATARI
static void sync_cycles(void)
{
	if (extra_cycle) {
		do_cycles(extra_cycle);
		extra_cycle = 0;
	}
	evt_t c = get_cycles();
	int extra = c & (CYCLE_UNIT - 1);
	if (extra) {
		extra = CYCLE_UNIT - extra;
		do_cycles(extra);
	}
}
#endif

uae_u32 wait_cpu_cycle_read (uaecptr addr, int mode)
{
	uae_u32 v = 0;
#ifndef WINUAE_FOR_HATARI
	int hpos;
	int ipl = regs.ipl[0];
	evt_t now = get_cycles();

	sync_cycles();

	x_do_cycles_pre(CYCLE_UNIT);

	hpos = dma_cycle(addr, 0xffffffff, &mode, &ipl);

#ifdef DEBUGGER
	if (debug_dma) {
		int reg = 0x1000;
		if (mode == -3) {
			reg |= 2;
			v = regs.chipset_latch_rw;
		} else if (mode < 0) {
			reg |= 4;
		} else if (mode > 0) {
			reg |= 2;
		} else {
			reg |= 1;
		}
		record_dma_read(reg, addr, hpos, vpos, DMARECORD_CPU, mode == -2 || mode == 2 ? 0 : 1);
	}
	peekdma_data.mask = 0;
#endif

	switch(mode)
	{
		case -1:
		v = get_long(addr);
		break;
		case -2:
		v = get_longi(addr);
		break;
		case 1:
		v = get_word(addr);
		break;
		case 2:
		v = get_wordi(addr);
		break;
		case 0:
		v = get_byte(addr);
		break;
	}

#ifdef DEBUGGER
	if (debug_dma) {
		record_dma_read_value(v);
	}
#endif

	regs.chipset_latch_rw = regs.chipset_latch_read = v;
		
	x_do_cycles_post(CYCLE_UNIT, v);

	// if IPL fetch was pending and CPU had wait states
	// Use ipl_pin value from previous cycle
	if (now == regs.ipl_evt) {
		regs.ipl[0] = ipl;
	}

#else						/* WINUAE_FOR_HATARI */
	int ipl = regs.ipl[0];
	evt_t now = get_cycles();

//	fprintf ( stderr , "mem read ce %x %d %lu %lu\n" , addr , mode ,currcycle / cpucycleunit , currcycle );
	if ( ( ( CyclesGlobalClockCounter + currcycle*2/CYCLE_UNIT ) & 3 ) == 2 )
	{
//		fprintf ( stderr , "mem wait read %x %d %lu %lu\n" , addr , mode , currcycle / cpucycleunit , currcycle );
		x_do_cycles (2*cpucycleunit);
//		fprintf ( stderr , "mem wait read after %x %d %lu %lu\n" , addr , mode , currcycle / cpucycleunit , currcycle );
	}

	switch(mode)
	{
		case -1:
		v = get_long(addr);
		break;
		case -2:
		v = get_longi(addr);
		break;
		case 1:
		v = get_word(addr);
		break;
		case 2:
		v = get_wordi(addr);
		break;
		case 0:
		v = get_byte(addr);
		break;
	}

	x_do_cycles_post (2*CYCLE_UNIT, v);

	// if IPL fetch was pending and CPU had wait states
	// Use ipl_pin value from previous cycle
	if (now == regs.ipl_evt && regs.ipl_pin_change_evt > now + cpuipldelay2) {
		regs.ipl[0] = ipl;
	}
#endif						/* WINUAE_FOR_HATARI */

	return v;
}

void wait_cpu_cycle_write (uaecptr addr, int mode, uae_u32 v)
{
#ifndef WINUAE_FOR_HATARI
	int hpos;
	int ipl = regs.ipl[0];
	evt_t now = get_cycles();

	sync_cycles();

	x_do_cycles_pre(CYCLE_UNIT);

	hpos = dma_cycle(addr, v, &mode, &ipl);

#ifdef DEBUGGER
	if (debug_dma) {
		int reg = 0x1100;
		if (mode == -3) {
			reg |= 2;
		} else if (mode < 0) {
			reg |= 4;
		} else if (mode > 0) {
			reg |= 2;
		} else {
			reg |= 1;
		}
		record_dma_write(reg, v, addr, hpos, vpos, DMARECORD_CPU, 1);
	}
	peekdma_data.mask = 0;
#endif

	if (mode > -2) {
		if (mode < 0) {
			put_long(addr, v);
		} else if (mode > 0) {
			put_word(addr, v);
		} else if (mode == 0) {
			put_byte(addr, v);
		}
	}

	regs.chipset_latch_rw = regs.chipset_latch_write = v;

	x_do_cycles_post(CYCLE_UNIT, v);

	// if IPL fetch was pending and CPU had wait states:
	// Use ipl_pin value from previous cycle
	if (now == regs.ipl_evt) {
		regs.ipl[0] = ipl;
	}

#else						/* WINUAE_FOR_HATARI */
	int ipl = regs.ipl[0];
	evt_t now = get_cycles();

//	fprintf ( stderr , "mem write ce %x %d %lu %lu\n" , addr , mode ,currcycle / cpucycleunit , currcycle );
	if ( ( ( CyclesGlobalClockCounter + currcycle*2/CYCLE_UNIT ) & 3 ) == 2 )
	{
//		fprintf ( stderr , "mem wait write %x %d %lu %lu\n" , addr , mode , currcycle / cpucycleunit , currcycle );
		x_do_cycles (2*cpucycleunit);
//		fprintf ( stderr , "mem wait write after %x %d %lu %lu\n" , addr , mode , currcycle / cpucycleunit , currcycle );
	}

	if (mode > -2) {
		if (mode < 0) {
			put_long(addr, v);
		} else if (mode > 0) {
			put_word(addr, v);
		} else if (mode == 0) {
			put_byte(addr, v);
		}
	}

	x_do_cycles_post (2*CYCLE_UNIT, v);

	// if IPL fetch was pending and CPU had wait states:
	// Use ipl_pin value from previous cycle
	if (now == regs.ipl_evt) {
		regs.ipl[0] = ipl;
	}
#endif						/* WINUAE_FOR_HATARI */
}


uae_u32 wait_cpu_cycle_read_ce020 (uaecptr addr, int mode)
{
	uae_u32 v = 0;
#ifndef WINUAE_FOR_HATARI
	int hpos, ipl;

	sync_cycles();

	x_do_cycles_pre(CYCLE_UNIT);

	hpos = dma_cycle(0xffffffff, 0xffff, NULL, &ipl);

#ifdef DEBUGGER
	if (debug_dma) {
		int reg = 0x1000;
		if (mode < 0) {
			reg |= 4;
		} else if (mode > 0) {
			reg |= 2;
		} else {
			reg |= 1;
		}
		record_dma_read(reg, addr, hpos, vpos, DMARECORD_CPU, mode == -2 || mode == 2 ? 0 : 1);
	}
	peekdma_data.mask = 0;
#endif

	switch (mode) {
		case -1:
		v = get_long(addr);
		break;
		case -2:
		v = get_longi(addr);
		break;
		case 1:
		v = get_word(addr);
		break;
		case 2:
		v = get_wordi(addr);
		break;
		case 0:
		v = get_byte(addr);
		break;
	}

#ifdef DEBUGGER
	if (debug_dma) {
		record_dma_read_value(v);
	}
#endif

	regs.chipset_latch_rw = regs.chipset_latch_read = v;
		
	x_do_cycles_post(CYCLE_UNIT, v);

#else						/* WINUAE_FOR_HATARI */

//fprintf ( stderr , "wait read ce020 glob %lu\n" , CyclesGlobalClockCounter );
//fprintf ( stderr , "wait read ce020 %lu %lu\n" , currcycle / cpucycleunit , currcycle );
	int bus_pos = ( CyclesGlobalClockCounter + currcycle*2/CYCLE_UNIT ) & 3;
	if ( ( bus_pos & 2 ) == 2 )
//	if ( bus_pos )
	{
//		fprintf ( stderr , "mem wait read %x %d %lu %lu\n" , addr , mode , currcycle / cpucycleunit , currcycle );
		x_do_cycles ((4-bus_pos)*cpucycleunit);
//		fprintf ( stderr , "mem wait read after %x %d %lu %lu\n" , addr , mode , currcycle / cpucycleunit , currcycle );
	}

//fprintf ( stderr , "wait read2 ce020 %lu %lu\n" , currcycle / cpucycleunit , currcycle );

	switch (mode) {
		case -1:
		v = get_long(addr);
		break;
		case -2:
		v = get_longi(addr);
		break;
		case 1:
		v = get_word(addr);
		break;
		case 2:
		v = get_wordi(addr);
		break;
		case 0:
		v = get_byte(addr);
		break;
	}

//fprintf ( stderr , "wait read3 ce020 %lu %lu\n" , currcycle / cpucycleunit , currcycle );
	x_do_cycles_post (3*cpucycleunit, v);
//fprintf ( stderr , "wait read4 ce020 %lu %lu\n" , currcycle / cpucycleunit , currcycle );

#endif						/* WINUAE_FOR_HATARI */

	return v;
}

void wait_cpu_cycle_write_ce020 (uaecptr addr, int mode, uae_u32 v)
{
#ifndef WINUAE_FOR_HATARI
	int hpos, ipl;

	sync_cycles();

 	x_do_cycles_pre(CYCLE_UNIT);

	hpos = dma_cycle(0xffffffff, 0xffff, NULL, &ipl);

#ifdef DEBUGGER
	if (debug_dma) {
		int reg = 0x1100;
		if (mode < 0)
			reg |= 4;
		else if (mode > 0)
			reg |= 2;
		else
			reg |= 1;
		record_dma_write(reg, v, addr, hpos, vpos, DMARECORD_CPU, 1);
	}
	peekdma_data.mask = 0;
#endif

	if (mode < 0) {
		put_long(addr, v);
	} else if (mode > 0) {
		put_word(addr, v);
	} else if (mode == 0) {
		put_byte(addr, v);
	}

	regs.chipset_latch_rw = regs.chipset_latch_write = v;
		
	x_do_cycles_post(CYCLE_UNIT, v);

#else						/* WINUAE_FOR_HATARI */

//fprintf ( stderr , "wait read ce020 %lu %lu\n" , currcycle / cpucycleunit , currcycle );
	int bus_pos = ( CyclesGlobalClockCounter + currcycle*2/CYCLE_UNIT ) & 3;
	if ( ( bus_pos & 2 ) == 2 )
//	if ( bus_pos )
	{
//		fprintf ( stderr , "mem wait read %x %d %lu %lu\n" , addr , mode , currcycle / cpucycleunit , currcycle );
		x_do_cycles ((4-bus_pos)*cpucycleunit);
//		fprintf ( stderr , "mem wait read after %x %d %lu %lu\n" , addr , mode , currcycle / cpucycleunit , currcycle );
	}

//fprintf ( stderr , "wait read2 ce020 %lu %lu\n" , currcycle / cpucycleunit , currcycle );

	if (mode < 0)
		put_long (addr, v);
	else if (mode > 0)
		put_word (addr, v);
	else if (mode == 0)
		put_byte (addr, v);

//fprintf ( stderr , "wait read3 ce020 %lu %lu\n" , currcycle / cpucycleunit , currcycle );
	x_do_cycles_post (3*cpucycleunit, v);
//fprintf ( stderr , "wait read4 ce020 %lu %lu\n" , currcycle / cpucycleunit , currcycle );

#endif						/* WINUAE_FOR_HATARI */
}

#ifndef WINUAE_FOR_HATARI
void do_cycles_ce (uae_u32 cycles)
{
	cycles += extra_cycle;
	while (cycles >= CYCLE_UNIT) {
		int hpos = current_hpos () + 1;
		decide_line (hpos);
		sync_copper (hpos);
		decide_fetch_ce (hpos);
		if (bltstate != BLT_done)
			decide_blitter (hpos);
		do_cycles (1 * CYCLE_UNIT);
		cycles -= CYCLE_UNIT;
	}
	extra_cycle = cycles;
}
#else
/* [NP] Unlike Amiga, for Hatari in 68000 CE mode, we don't need to update other components */
/* on every sub cycle, so we can do all cycles in one single call to speed up */
/* emulation (this gains approx 7%) */
void do_cycles_ce (int cycles)
{
	cycles += extra_cycle;
	extra_cycle = cycles & ( CYCLE_UNIT-1 );
	do_cycles ( cycles - extra_cycle );
}
#endif

#ifdef WINUAE_FOR_HATARI
/* Same as do_cycles_ce() with cycle exact blitter support */
void do_cycles_ce_hatari_blitter (int cycles)
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
		if ( Blitter_Check_Simultaneous_CPU() == 0 )
			do_cycles (1 * CYCLE_UNIT);
		Blitter_HOG_CPU_do_cycles_after ( 2 );

		cycles -= CYCLE_UNIT;
	}
	extra_cycle = cycles;
}
#endif


void do_cycles_ce020 (int cycles)
{
	int c;
	evt_t cc;
	int extra;

	if (!cycles)
		return;
	cc = get_cycles ();
	extra = cc & (CYCLE_UNIT - 1);
//fprintf ( stderr , "do_cycles_ce020 %d %d %d\n" , cycles , c , extra );
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
	if (c)
		do_cycles (c);
}

bool is_cycle_ce(uaecptr addr)
{
#ifndef WINUAE_FOR_HATARI
	addrbank *ab = get_mem_bank_real(addr);
	if (!ab || (ab->flags & ABFLAG_CHIPRAM) || ab == &custom_bank) {
		int hpos = current_hpos();
		return (cycle_line[hpos] & CYCLE_MASK) != 0;
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

	if (uae_quit_program == 0) {
		uae_quit_program = -UAE_RESET;
		if (keyboardreset)
			uae_quit_program = -UAE_RESET_KEYBOARD;
		if (hardreset)
			uae_quit_program = -UAE_RESET_HARD;
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


