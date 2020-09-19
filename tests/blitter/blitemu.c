/*
 * Blitter test program for different combinations of src inc, dest inc, fxsr, nfsr
 * It requires an accurate emulation of the complex handling of nfsr
 *
 * Program by Christian Zietz, slightly modified to work with Hatari's test suite
 *
 */

#include <stdio.h>
#include <string.h>
#include <tos.h>
#include <ext.h>

typedef unsigned char BYTE;
typedef unsigned int  WORD;
typedef unsigned long LONG;

#define HALFTONE(x) (*((volatile WORD *)0xFF8A00L+(x<<1)))
#define OP 			(*((volatile BYTE *)0xFF8A3BL))
#define ENDMASK1	(*((volatile WORD *)0xFF8A28L))
#define ENDMASK2	(*((volatile WORD *)0xFF8A2AL))
#define ENDMASK3	(*((volatile WORD *)0xFF8A2CL))
#define X_COUNT		(*((volatile WORD *)0xFF8A36L))
#define Y_COUNT		(*((volatile WORD *)0xFF8A38L))
#define DST_XINC	(*((volatile WORD *)0xFF8A2EL))
#define DST_YINC	(*((volatile WORD *)0xFF8A30L))
#define DST_ADDR	(*((volatile LONG *)0xFF8A32L))
#define SRC_XINC	(*((volatile WORD *)0xFF8A20L))
#define SRC_YINC	(*((volatile WORD *)0xFF8A22L))
#define SRC_ADDR	(*((volatile LONG *)0xFF8A24L))
#define HOP			(*((volatile BYTE *)0xFF8A3AL))
#define LINE_NUM	(*((volatile BYTE *)0xFF8A3CL))
#define SKEW		(*((volatile BYTE *)0xFF8A3DL))

#define SRCLEN 64
#define DSTLEN 16

WORD srcbuf[SRCLEN];
WORD dstbuf[DSTLEN];

long do_copy(int stride, int fxsr, int nfsr, int op) {
	OP = op; /* D = S and D */

	ENDMASK1 = ENDMASK2 = ENDMASK3 = 0xFFFF;

	X_COUNT = 1;
	Y_COUNT = 10;

	DST_XINC = stride;
	DST_YINC = stride;
	if (stride < 0) {
		DST_ADDR = (LONG)(&dstbuf[DSTLEN-1]);
	} else {
		DST_ADDR = (LONG)(&dstbuf[0]);
	}

	SRC_XINC = stride;
	SRC_YINC = stride;
	if (stride < 0) {
		SRC_ADDR = (LONG)(&srcbuf[SRCLEN-1]);
	} else {
		SRC_ADDR = (LONG)(&srcbuf[0]);
	}

	HOP = 2; /* use source data */

	LINE_NUM = 0;
	SKEW = ((fxsr&1)<<7) + ((nfsr&1)<<6); /* FXSR NFSR */

	LINE_NUM = 0x80; /* set busy bit: start blitter: no HOG mode */
	while (LINE_NUM & 0x80)
		continue;

	return 0;
}


int main(void) {
	unsigned int i;
	int s,f,n,o;
	long oldsuper;
	int fh;
	char txt[128];

	for (i=0; i<SRCLEN; i++) {
		srcbuf[i] = i;
	}

	fh = Fcreate("BLITEMU.TXT", 0);
	if (fh < 0) {
		Cconws("Fcreate failed!\r\n");
		return 1;
	}

	oldsuper = Super(0);

	for (o=1; o<=3; o+=2) {
		sprintf(txt, ">>>>> OP = %d <<<<<", o);
		Fwrite(fh, strlen(txt), txt);
		for (s=2; s>=-2; s-=4)
		 for (f=0; f<=1; f++)
		  for (n=0; n<=1; n++) {
			sprintf(txt, "\r\nSRC_INC = DST_INC = %+d, FXSR = %d, NFSR = %d\r\n", s, f, n);
			Fwrite(fh, strlen(txt), txt);
			memset(dstbuf, -1, sizeof(dstbuf));
			do_copy(s,f,n,o);

			for (i=0; i<DSTLEN; i++) {
				sprintf(txt, "%d ", dstbuf[i]);
				Fwrite(fh, strlen(txt), txt);
			}
			Fwrite(fh, 2, "\r\n");
		  }

	}

	Super((void *)oldsuper);

	Fclose(fh);

	return 0;
}
