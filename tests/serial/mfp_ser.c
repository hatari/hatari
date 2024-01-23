/*
 * Hatari serial port test
 *
 * Partly based on the file serport.c and mfp.h from EmuTOS:
 * Copyright (C) 2013-2018 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include <tos.h>

void sleep_vbl ( int count )
{
	volatile long	vbl_nb;
	vbl_nb = *(long *)0x462;

	while ( *(long volatile *)0x462 < vbl_nb+count )
		;
}


typedef unsigned char UBYTE;

/*==== MFP memory mapping =================================================*/
typedef struct
{
	UBYTE   dum1;
	volatile UBYTE  gpip;   /* general purpose .. register */
	UBYTE   dum2;
	volatile UBYTE  aer;    /* active edge register              */
	UBYTE   dum3;
	volatile UBYTE  ddr;    /* data direction register           */
	UBYTE   dum4;
	volatile UBYTE  iera;   /* interrupt enable register A       */
	UBYTE   dum5;
	volatile UBYTE  ierb;   /* interrupt enable register B       */
	UBYTE   dum6;
	volatile UBYTE  ipra;   /* interrupt pending register A      */
	UBYTE   dum7;
	volatile UBYTE  iprb;   /* interrupt pending register B      */
	UBYTE   dum8;
	volatile UBYTE  isra;   /* interrupt in-service register A   */
	UBYTE   dum9;
	volatile UBYTE  isrb;   /* interrupt in-service register B   */
	UBYTE   dum10;
	volatile UBYTE  imra;   /* interrupt mask register A         */
	UBYTE   dum11;
	volatile UBYTE  imrb;   /* interrupt mask register B         */
	UBYTE   dum12;
	volatile UBYTE  vr;     /* vector register                   */
	UBYTE   dum13;
	volatile UBYTE  tacr;   /* timer A control register          */
	UBYTE   dum14;
	volatile UBYTE  tbcr;   /* timer B control register          */
	UBYTE   dum15;
	volatile UBYTE  tcdcr;  /* timer C + D control register      */
	UBYTE   dum16;
	volatile UBYTE  tadr;   /* timer A data register             */
	UBYTE   dum17;
	volatile UBYTE  tbdr;   /* timer B data register             */
	UBYTE   dum18;
	volatile UBYTE  tcdr;   /* timer C data register             */
	UBYTE   dum19;
	volatile UBYTE  tddr;   /* timer D data register             */
	UBYTE   dum20;
	volatile UBYTE  scr;    /* synchronous character register     */
	UBYTE   dum21;
	volatile UBYTE  ucr;    /* USART control register            */
	UBYTE   dum22;
	volatile UBYTE  rsr;    /* receiver status register          */
	UBYTE   dum23;
	volatile UBYTE  tsr;    /* transmitter status register       */
	UBYTE   dum24;
	volatile UBYTE  udr;    /* USART data register               */
} MFP;

#define MFP_BASE  ((MFP *)(0xfffffa00L))

/*
 * MFP serial port i/o routines
 */
static long costat(void)
{
	if (MFP_BASE->tsr & 0x80)
		return -1;
	else
		return 0;
}

static void conout(char b)
{
	/* Wait for transmit buffer to become empty */
	while(!costat())
		;

	/* Output to RS232 interface */
	MFP_BASE->udr = (char)b;
}

int main(int argc, char *argv[])
{
	char text[] = "The quick brown fox\njumps over the lazy dog\n";
	int i;
	void *sp = (void*)Super(0);

	MFP_BASE->scr = 0x00;
	MFP_BASE->ucr = 0x88;
	MFP_BASE->rsr = 0x01;
	MFP_BASE->tsr = 0x01;

	for (i = 0; text[i] != 0; i++)
		conout(text[i]);

	// wait a few VBL's to be sure all the bytes were transfered/received
	sleep_vbl(5);

	Super(sp);

	return 0;
}
