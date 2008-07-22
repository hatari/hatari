/*
 * dsp.c - Atari DSP56001 emulation code
 *
 * Copyright (c) 2001-2004 Petr Stehlik of ARAnyM dev team
 * Adaption to Hatari (C) 2006 by Thomas Huth
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ARAnyM; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "main.h"
#include "sysdeps.h"
#include "ioMem.h"
#include "dsp.h"
#include "dsp_cpu.h"

#define DSP_EMULATION 1
#define DEBUG 1

#if DEBUG
#define D(x) x
#else
#define D(x)
#endif

#include <math.h>

#include <SDL.h>
#include <SDL_thread.h>

#ifndef M_PI
#define M_PI	3.141592653589793238462643383279502
#endif


/* The structure with all public DSP variables */
struct DSP dsp;

/* For bootstrap routine */
static uint16	bootstrap_pos;
static uint32	bootstrap_accum;

static SDL_Thread	*dsp56k_thread;



#if DSP_EMULATION

/* More disasm infos, if wanted */
#define DSP_DISASM_HOSTREAD 0	/* Dsp->Host transfer */
#define DSP_DISASM_HOSTWRITE 0	/* Host->Dsp transfer */
#define DSP_DISASM_STATE 0		/* State changes */

/* Execute DSP instructions till the DSP waits for a read/write */
#define DSP_HOST_FORCEEXEC 0


static inline Uint32 getHWoffset(void)
{
	return 0xFFA200;
}


/* Constructor and  destructor for DSP class */
void DSP_Init(void)
{
	int i;

	memset(dsp.ram, 0,sizeof(dsp.ram));

	/* Initialize Y:rom[0x0100-0x01ff] with a sin table */
	{
		float src;
		int32 dest;

		for (i=0;i<256;i++) {
			src = (((float) i)*M_PI)/128.0;
			dest = (int32) (sin(src) * 8388608.0); /* 1<<23 */
			if (dest>8388607) {
				dest = 8388607;
			} else if (dest<-8388608) {
				dest = -8388608;
			}
			dsp.rom[DSP_SPACE_Y][0x100+i]=dest & 0x00ffffff;
		}
	}

	/* Initialize X:rom[0x0100-0x017f] with a mu-law table */
	{
		const uint16 mulaw_base[8]={
			0x7d7c, 0x3e7c, 0x1efc, 0x0f3c, 0x075c, 0x036c, 0x0174, 0x0078
		};

		uint32 value, offset, position;
		int j;

		position = 0x0100;
		offset = 0x040000;
		for(i=0;i<8;i++) {
			value = mulaw_base[i]<<8;

			for (j=0;j<16;j++) {
				dsp.rom[DSP_SPACE_X][position++]=value;
				value -= offset;
			}

			offset >>= 1;
		}
	}

	/* Initialize X:rom[0x0180-0x01ff] with a a-law table */
	{
		const int32 multiply_base[8]={
			0x1580, 0x0ac0, 0x5600, 0x2b00,
			0x1580, 0x0058, 0x0560, 0x02b0
		};
		const int32 multiply_col[4]={0x10, 0x01, 0x04, 0x02};
		const int32 multiply_line[4]={0x40, 0x04, 0x10, 0x08};
		const int32 base_values[4]={0, -1, 2, 1};
		uint32 pos=0x0180;
		
		for (i=0;i<8;i++) {
			int32 alawbase, j;

			alawbase = multiply_base[i]<<8;
			for (j=0;j<4;j++) {
				int32 alawbase1, k;
				
				alawbase1 = alawbase + ((base_values[j]*multiply_line[i & 3])<<12);

				for (k=0;k<4;k++) {
					int32 alawbase2;

					alawbase2 = alawbase1 + ((base_values[k]*multiply_col[i & 3])<<12);

					dsp.rom[DSP_SPACE_X][pos++]=alawbase2;
				}
			}
		}
	}
	
	D(bug("Dsp: power-on done"));

	dsp56k_thread = NULL;
	dsp.dsp56k_sem = NULL;

	dsp.state = DSP_HALT;
#if DSP_DISASM_STATE
	D(bug("Dsp: state = HALT"));
#endif
}

void DSP_UnInit(void)
{
	DSP_shutdown();
}

/* Other functions to init/shutdown dsp emulation */
void DSP_Reset(void)
{
	int i;

	/* Kill existing thread and semaphore */
	DSP_shutdown();

	/* Pause thread */
	dsp.state = DSP_BOOTING;
#if DSP_DISASM_STATE
	D(bug("Dsp: state = BOOTING"));
#endif

	/* Memory */
	memset(dsp.periph, 0,sizeof(dsp.periph));
	memset(dsp.stack, 0,sizeof(dsp.stack));
	memset(dsp.registers, 0,sizeof(dsp.registers));

	bootstrap_pos = bootstrap_accum = 0;
	
	/* Registers */
	dsp.pc = 0x0000;
	dsp.registers[DSP_REG_OMR]=0x02;
	for (i=0;i<8;i++) {
		dsp.registers[DSP_REG_M0+i]=0x00ffff;
	}

	/* host port init, dsp side */
	dsp.periph[DSP_SPACE_X][DSP_HOST_HSR]=(1<<DSP_HOST_HSR_HTDE);

	/* host port init, cpu side */
	dsp.hostport[CPU_HOST_CVR]=0x12;
	dsp.hostport[CPU_HOST_ISR]=(1<<CPU_HOST_ISR_TRDY)|(1<<CPU_HOST_ISR_TXDE);
	dsp.hostport[CPU_HOST_IVR]=0x0f;

	/* Other hardware registers */
	dsp.periph[DSP_SPACE_X][DSP_IPR]=0;
	dsp.periph[DSP_SPACE_X][DSP_BCR]=0xffff;

	/* Misc */
	dsp.loop_rep = 0;
	dsp.last_loop_inst = 0;
	dsp.first_host_write = 1;

	D(bug("Dsp: reset done"));

	/* Create thread and semaphore if needed */
	if (dsp.dsp56k_sem == NULL) {
		dsp.dsp56k_sem = SDL_CreateSemaphore(0);
	}

	if (dsp56k_thread == NULL) {
		dsp56k_thread = SDL_CreateThread(dsp56k_do_execute, NULL);
	}
}

void DSP_shutdown(void)
{
	if (dsp56k_thread != NULL) {

		/* Stop thread */
		dsp.state = DSP_STOPTHREAD;
#if DSP_DISASM_STATE
		D(bug("Dsp: state = STOPTHREAD"));
#endif

		/* Release semaphore, if thread waiting for it */
		if (SDL_SemValue(dsp.dsp56k_sem)==0) {
			SDL_SemPost(dsp.dsp56k_sem);
		}

		/* Wait for the thread to finish */
		while (dsp.state != DSP_STOPPEDTHREAD) {
			SDL_Delay(1);
		}

		dsp56k_thread = NULL;
	}

	/* Destroy the semaphore */
	if (dsp.dsp56k_sem != NULL) {
		SDL_DestroySemaphore(dsp.dsp56k_sem);
		dsp.dsp56k_sem = NULL;
	}
}

/**********************************
 *	Force execution of DSP, till something
 *  to read from/write to host port
 **********************************/

static inline void DSP_force_exec(void)
{
#if DSP_HOST_FORCEEXEC
	Uint32 startticks;

	startticks = SDL_GetTicks();
	while (dsp.state == DSP_RUNNING) {
		if (SDL_GetTicks() != startticks) {
			SDL_Delay(1);
			startticks = SDL_GetTicks();
		}
	}
#endif
}

#endif /* DSP_EMULATION */

/**********************************
 *	Hardware address read/write by CPU
 **********************************/

static uint8 DSP_handleRead(Uint32 addr)
{
#if DSP_EMULATION
	uint8 value=0;

	addr -= getHWoffset();

/*	D(bug("HWget_b(0x%08x)=0x%02x at 0x%08x", addr+HW_DSP, value, showPC()));*/

	/* Whenever the host want to read something on host port, we test if a
	   transfer is needed */
	DSP_dsp2host();

	switch(addr) {
		case CPU_HOST_ICR:
			value = dsp.hostport[CPU_HOST_ICR];
			break;
		case CPU_HOST_CVR:
			value = dsp.hostport[CPU_HOST_CVR];
			break;
		case CPU_HOST_ISR:
			value = dsp.hostport[CPU_HOST_ISR];
			break;
		case CPU_HOST_IVR:
			value = dsp.hostport[CPU_HOST_IVR];
			break;
		case CPU_HOST_RX0:
			DSP_force_exec();
			value = 0;
			break;
		case CPU_HOST_RXH:
			DSP_force_exec();
			value = dsp.hostport[CPU_HOST_RXH];
			break;
		case CPU_HOST_RXM:
			DSP_force_exec();
			value = dsp.hostport[CPU_HOST_RXM];
			break;
		case CPU_HOST_RXL:
			DSP_force_exec();
			value = dsp.hostport[CPU_HOST_RXL];

			if (dsp.state!=DSP_BOOTING) {
				/* Clear RXDF bit to say that CPU has read */
				dsp.hostport[CPU_HOST_ISR] &= 0xff-(1<<CPU_HOST_ISR_RXDF);
#if DSP_DISASM_HOSTWRITE
				D(bug("Dsp: (D->H): Host RXDF cleared"));
#endif
			}

			/* Wake up DSP if it was waiting our read */
			if (dsp.state==DSP_WAITHOSTREAD) {
#if DSP_DISASM_STATE
				D(bug("Dsp: state = DSP_RUNNING"));
#endif
				dsp.state = DSP_RUNNING;

				if (SDL_SemValue(dsp.dsp56k_sem)==0) {
					SDL_SemPost(dsp.dsp56k_sem);
				}
			}

			break;
	}

	return value;
#else
	return 0xff;	// this value prevents TOS from hanging in the DSP init code */
#endif	/* DSP_EMULATION */
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

static void DSP_handleWrite(Uint32 addr, uint8 value)
{
#if DSP_EMULATION
	addr -= getHWoffset();

/*	D(bug("HWput_b(0x%08x,0x%02x) at 0x%08x", addr+HW_DSP, value, showPC()));*/

	switch(addr) {
		case CPU_HOST_ICR:
			dsp.hostport[CPU_HOST_ICR]=value & 0xfb;
			/* Set HF1 and HF0 accordingly on the host side */
			dsp.periph[DSP_SPACE_X][DSP_HOST_HSR] &=
					0xff-((1<<DSP_HOST_HSR_HF1)|(1<<DSP_HOST_HSR_HF0));
			dsp.periph[DSP_SPACE_X][DSP_HOST_HSR] |=
					dsp.hostport[CPU_HOST_ICR] & ((1<<DSP_HOST_HSR_HF1)|(1<<DSP_HOST_HSR_HF0));
			break;
		case CPU_HOST_CVR:
			dsp.hostport[CPU_HOST_CVR]=value & 0x9f;
			/* if bit 7=1, host command */
			if (value & (1<<7)) {
				dsp.periph[DSP_SPACE_X][DSP_HOST_HSR] |= 1<<DSP_HOST_HSR_HCP;
			}
			break;
		case CPU_HOST_ISR:
			/* Read only */
			break;
		case CPU_HOST_IVR:
			dsp.hostport[CPU_HOST_IVR]=value;
			break;
		case CPU_HOST_TX0:
			DSP_force_exec();

			if (dsp.first_host_write) {
				dsp.first_host_write = 0;
				bootstrap_accum = 0;
			}
			break;
		case CPU_HOST_TXH:
			DSP_force_exec();

			if (dsp.first_host_write) {
				dsp.first_host_write = 0;
				bootstrap_accum = 0;
			}
			dsp.hostport[CPU_HOST_TXH]=value;
			bootstrap_accum |= value<<16;
			break;
		case CPU_HOST_TXM:
			DSP_force_exec();

			if (dsp.first_host_write) {
				dsp.first_host_write = 0;
				dsp.hostport[CPU_HOST_TXH]=value;	/* FIXME: is it correct ? */
				bootstrap_accum = 0;
			}
			dsp.hostport[CPU_HOST_TXM]=value;
			bootstrap_accum |= value<<8;
			break;
		case CPU_HOST_TXL:
			DSP_force_exec();

			if (dsp.first_host_write) {
				dsp.first_host_write = 0;
				dsp.hostport[CPU_HOST_TXH]=value;	/* FIXME: is it correct ? */
				dsp.hostport[CPU_HOST_TXM]=value;	/* FIXME: is it correct ? */
				bootstrap_accum = 0;
			}
			dsp.hostport[CPU_HOST_TXL]=value;
			bootstrap_accum |= value;

			dsp.first_host_write = 1;

			if (dsp.state != DSP_BOOTING) {
				/* Clear TXDE to say that host has written */
				dsp.hostport[CPU_HOST_ISR] &= 0xff-(1<<CPU_HOST_ISR_TXDE);
#if DSP_DISASM_HOSTREAD
				D(bug("Dsp: (H->D): Host TXDE cleared"));
#endif

				DSP_host2dsp();
			}

			switch(dsp.state) {
				case DSP_BOOTING:
					dsp.ramint[DSP_SPACE_P][bootstrap_pos] = bootstrap_accum;
/*					D(bug("Dsp: bootstrap: p:0x%04x: 0x%06x written", bootstrap_pos, bootstrap_accum));*/
					bootstrap_pos++;
					if (bootstrap_pos == 0x200) {
#if DSP_DISASM_STATE
						D(bug("Dsp: bootstrap done"));
#endif
						dsp.state = DSP_RUNNING;

						SDL_SemPost(dsp.dsp56k_sem);
					}		
					bootstrap_accum = 0;
					break;
				case DSP_WAITHOSTWRITE:
					/* Wake up DSP if it was waiting our write */
#if DSP_DISASM_STATE
					D(bug("Dsp: state = DSP_RUNNING"));
#endif
					dsp.state = DSP_RUNNING;

					if (SDL_SemValue(dsp.dsp56k_sem)==0) {
						SDL_SemPost(dsp.dsp56k_sem);
					}
					break;
			}

			break;
	}
#endif	/* DSP_EMULATION */
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


#if DSP_EMULATION

/**********************************
 *	Host transfer
 **********************************/

void DSP_host2dsp(void)
{
	int trdy;

	/* Host port transfer ? (host->dsp) */
	if (
		((dsp.hostport[CPU_HOST_ISR] & (1<<CPU_HOST_ISR_TXDE))==0) &&
		((dsp.periph[DSP_SPACE_X][DSP_HOST_HSR] & (1<<DSP_HOST_HSR_HRDF))==0)
		) {

		dsp.periph[DSP_SPACE_X][DSP_HOST_HRX] = dsp.hostport[CPU_HOST_TXL];
		dsp.periph[DSP_SPACE_X][DSP_HOST_HRX] |= dsp.hostport[CPU_HOST_TXM]<<8;
		dsp.periph[DSP_SPACE_X][DSP_HOST_HRX] |= dsp.hostport[CPU_HOST_TXH]<<16;

#if DSP_DISASM_HOSTREAD
		D(bug("Dsp: (H->D): Transfer 0x%06x",dsp.periph[DSP_SPACE_X][DSP_HOST_HRX]));
#endif

		/* Set HRDF bit to say that DSP can read */
		dsp.periph[DSP_SPACE_X][DSP_HOST_HSR] |= 1<<DSP_HOST_HSR_HRDF;
#if DSP_DISASM_HOSTREAD
		D(bug("Dsp: (H->D): Dsp HRDF set"));
#endif

		/* Set TXDE bit to say that host can write */
		dsp.hostport[CPU_HOST_ISR] |= 1<<CPU_HOST_ISR_TXDE;
#if DSP_DISASM_HOSTREAD
		D(bug("Dsp: (H->D): Host TXDE set"));
#endif

		/* Clear/set TRDY bit */
		dsp.hostport[CPU_HOST_ISR] &= 0xff-(1<<CPU_HOST_ISR_TRDY);
		trdy = (dsp.hostport[CPU_HOST_ISR]>>CPU_HOST_ISR_TXDE) & 1;
		trdy &= !((dsp.periph[DSP_SPACE_X][DSP_HOST_HSR]>>DSP_HOST_HSR_HRDF) & 1);
		dsp.hostport[CPU_HOST_ISR] |= (trdy & 1)<< CPU_HOST_ISR_TRDY;
	}
}

void DSP_dsp2host(void)
{
	/* Host port transfer ? (dsp->host) */
	if (
		((dsp.hostport[CPU_HOST_ISR] & (1<<CPU_HOST_ISR_RXDF))==0) &&
		((dsp.periph[DSP_SPACE_X][DSP_HOST_HSR] & (1<<DSP_HOST_HSR_HTDE))==0)
		) {

		dsp.hostport[CPU_HOST_RXL] = dsp.periph[DSP_SPACE_X][DSP_HOST_HTX];
		dsp.hostport[CPU_HOST_RXM] = dsp.periph[DSP_SPACE_X][DSP_HOST_HTX]>>8;
		dsp.hostport[CPU_HOST_RXH] = dsp.periph[DSP_SPACE_X][DSP_HOST_HTX]>>16;

#if DSP_DISASM_HOSTWRITE
		D(bug("Dsp: (D->H): Transfer 0x%06x",dsp.periph[DSP_SPACE_X][DSP_HOST_HTX]));
#endif

		/* Set HTDE bit to say that DSP can write */
		dsp.periph[DSP_SPACE_X][DSP_HOST_HSR] |= 1<<DSP_HOST_HSR_HTDE;
#if DSP_DISASM_HOSTWRITE
		D(bug("Dsp: (D->H): Dsp HTDE set"));
#endif

		/* Set RXDF bit to say that host can read */
		dsp.hostport[CPU_HOST_ISR] |= 1<<CPU_HOST_ISR_RXDF;
#if DSP_DISASM_HOSTWRITE
		D(bug("Dsp: (D->H): Host RXDF set"));
#endif
	}
}

#endif	/* DSP_EMULATION */

