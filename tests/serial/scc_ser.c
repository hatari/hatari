/*
 * Hatari SCC serial port test
 *
 * Partly based on the file serport.c and mfp.h from EmuTOS:
 * Copyright (C) 2013-2018 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include <tos.h>

volatile int delay_cnt;

static void delay_loop(int loops)
{
	int i;

	for (i = 0; i < loops; i++)
		delay_cnt++;
}

void sleep_vbl ( int count )
{
	volatile long	vbl_nb;
	vbl_nb = *(long *)0x462;

	while ( *(long volatile *)0x462 < vbl_nb+count )
		;
}


/*
 * defines
 */
#define RESET_RECOVERY_DELAY    delay_loop(8)
#define RECOVERY_DELAY          delay_loop(4)

#define SCC_BASE                0xffff8c80UL

#define HIBYTE(x) ((unsigned char)((unsigned short)(x) >> 8))
#define LOBYTE(x) ((unsigned char)(unsigned short)(x))

/*
 * structures
 */
typedef struct {
    unsigned char dum1;
    volatile unsigned char ctl;
    unsigned char dum2;
    volatile unsigned char data;
} PORT;
typedef struct {
    PORT portA;
    PORT portB;
} SCC;

/*
 * SCC port B i/o routines
 */
static long bcostatB(void)
{
	SCC *scc = (SCC *)SCC_BASE;
	long rc;

	rc = (scc->portB.ctl & 0x04) ? -1L : 0L;
	RECOVERY_DELAY;

	return rc;
}

long bconoutB(unsigned char b)
{
	SCC *scc = (SCC *)SCC_BASE;

	while(!bcostatB())
		;
	scc->portB.data = b;
	RECOVERY_DELAY;

	return 1L;
}

static void write_scc(PORT *port, unsigned char reg, unsigned char data)
{
	port->ctl = reg;
	RECOVERY_DELAY;
	port->ctl = data;
	RECOVERY_DELAY;
}

static const short SCC_init_string[] = {
	0x0444,     /* x16 clock mode, 1 stop bit, no parity */
	0x0104,     /* 'parity is special condition' */
	0x0260,     /* interrupt vector #s start at 0x60 (lowmem 0x180) */
	0x03c0,     /* Rx 8 bits/char, disabled */
	0x05e2,     /* Tx 8 bits/char, disabled, DTR, RTS */
	0x0600,     /* SDLC (n/a) */
	0x0700,     /* SDLC (n/a) */
	0x0901,     /* status low, vector includes status */
	0x0a00,     /* misc flags */
	0x0b50,     /* Rx/Tx clocks from baudrate generator output */
	0x0c18,     /* time const low = 24 | so rate = (24+2)*2/BR clock period */
	0x0d00,     /* time const hi = 0   | = 52/(8053976/16) => 9680 bps      */
	0x0e02,     /* baudrate generator source = PCLK (8MHz) */
	0x0e03,     /* ditto + enable baudrate generator */
	0x03c1,     /* Rx 8 bits/char, enabled */
	0x05ea,     /* Tx 8 bits/char, enabled, DTR, RTS */
	0x0f20,     /* CTS interrupt enable */
	0x0010,     /* reset external/status interrupts */
	0x0010,     /* reset again (necessary, see manual) */
	0x0117,     /* interrupts for Rx, Tx, special condition; parity is special */
	0x0901,     /* status low, master interrupt disable */
	            /* NOTE: change above to 0x0909 to enable interrupts! */
	0xffff      /* end of table marker */
};

/*
 * initialise the SCC
 */
static void init_scc(void)
{
	SCC *scc = (SCC *)SCC_BASE;
	const short *p;

	/* issue hardware reset */
	scc->portA.ctl = 0x09;
	RECOVERY_DELAY;
	scc->portA.ctl = 0xC0;
	RESET_RECOVERY_DELAY;

	/* initialise channel A */
	for (p = SCC_init_string; *p >= 0; p++)
		write_scc(&scc->portA,HIBYTE(*p),LOBYTE(*p));

	/* initialise channel B */
	for (p = SCC_init_string; *p >= 0; p++)
		write_scc(&scc->portB,HIBYTE(*p),LOBYTE(*p));

	/*
	 * Enable routing of the SCC interrupt through the SCU like TOS does.
	 * Even though interrupts are not used here, other programs might
	 * install their own interrupt vectors and expect the interrupt
	 * to be available to them.
	 */
	 //if (HAS_VME)
	 //   *(volatile BYTE *)VME_INT_MASK |= VME_INT_SCC;
}

int main(int argc, char *argv[])
{
	char text[] = "The quick brown fox\njumps over the lazy dog\n";
	int i;
	void *sp = (void*)Super(0);

	init_scc();

	for (i = 0; text[i] != 0; i++)
		bconoutB(text[i]);

	// wait a few VBL's to be sure all the bytes were transfered/received
	sleep_vbl(5);

	Super(sp);

	return 0;
}
