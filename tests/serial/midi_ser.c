/*
 * Hatari MIDI port test
 *
 * Based on the file midi.c from EmuTOS:
 * Copyright (C) 2001-2016 Martin Doering
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


/* constants for the ACIA registers */

/* baudrate selection and reset (Baudrate = clock/factor) */
#define ACIA_DIV1  0
#define ACIA_DIV16 1
#define ACIA_DIV64 2
#define ACIA_RESET 3

/* character format */
#define ACIA_D7E2S (0<<2)       /* 7 data, even parity, 2 stop */
#define ACIA_D7O2S (1<<2)       /* 7 data, odd parity, 2 stop */
#define ACIA_D7E1S (2<<2)       /* 7 data, even parity, 1 stop */
#define ACIA_D7O1S (3<<2)       /* 7 data, odd parity, 1 stop */
#define ACIA_D8N2S (4<<2)       /* 8 data, no parity, 2 stop */
#define ACIA_D8N1S (5<<2)       /* 8 data, no parity, 1 stop */
#define ACIA_D8E1S (6<<2)       /* 8 data, even parity, 1 stop */
#define ACIA_D8O1S (7<<2)       /* 8 data, odd parity, 1 stop */

/* transmit control */
#define ACIA_RLTID (0<<5)       /* RTS low, TxINT disabled */
#define ACIA_RLTIE (1<<5)       /* RTS low, TxINT enabled */
#define ACIA_RHTID (2<<5)       /* RTS high, TxINT disabled */
#define ACIA_RLTIDSB (3<<5)     /* RTS low, TxINT disabled, send break */

/* receive control */
#define ACIA_RID (0<<7)         /* RxINT disabled */
#define ACIA_RIE (1<<7)         /* RxINT enabled */

/* status fields of the ACIA */
#define ACIA_RDRF 1             /* Receive Data Register Full */
#define ACIA_TDRE (1<<1)        /* Transmit Data Register Empty */
#define ACIA_DCD  (1<<2)        /* Data Carrier Detect */
#define ACIA_CTS  (1<<3)        /* Clear To Send */
#define ACIA_FE   (1<<4)        /* Framing Error */
#define ACIA_OVRN (1<<5)        /* Receiver Overrun */
#define ACIA_PE   (1<<6)        /* Parity Error */
#define ACIA_IRQ  (1<<7)        /* Interrupt Request */

struct ACIA
{
	unsigned char  ctrl;
	unsigned char  dummy1;
	unsigned char  data;
	unsigned char  dummy2;
};

#define ACIA_MIDI_BASE (0xfffffc04L)
#define midi_acia (*(volatile struct ACIA*)ACIA_MIDI_BASE)

/* can we send a byte to the MIDI ACIA ? */
long bcostat3(void)
{
	if (midi_acia.ctrl & ACIA_TDRE)
	{
		return -1;  /* OK */
	}
	else
	{
		/* Data register not empty */
		return 0;   /* not OK */
	}
}

/* send a byte to the MIDI ACIA */
long bconout3(char c)
{
	while(!bcostat3())
		;

	midi_acia.data = c;
	return 1L;
}

/*==== midi_init - initialize the MIDI acia ==================*/
/*
 *  Enable receive interrupts, set the clock for 31.25 kbaud
 */
void midi_init(void)
{
	/* initialize midi ACIA */
	midi_acia.ctrl = ACIA_RESET;    /* master reset */

	midi_acia.ctrl = ACIA_RIE|      /* enable RxINT */
	                 ACIA_RLTID|    /* RTS low, TxINT disabled */
	                 ACIA_DIV16|    /* clock/16 */
	                 ACIA_D8N1S;    /* 8 bit, 1 stop, no parity */
}

int main(int argc, char *argv[])
{
	char text[] = "The quick brown fox\njumps over the lazy dog\n";
	int i;
	void *sp = (void*)Super(0);

	midi_init();

	for (i = 0; text[i] != 0; i++)
		bconout3(text[i]);

	// wait a few VBL's to be sure all the bytes were transfered/received
	sleep_vbl(5);

	Super(sp);

	return 0;
}
