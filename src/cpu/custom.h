 /*
  * UAE - The Un*x Amiga Emulator
  *
  * custom chip support
  *
  * (c) 1995 Bernd Schmidt
  */

//#include "machdep/rpt.h"
#include "rpt.h"


/* Set to 1 to leave out the current frame in average frame time calculation.
 * Useful if the debugger was active.  */
extern int bogusframe;
extern unsigned long int hsync_counter;

extern uae_u16 dmacon;
extern uae_u16 intena, intreq, intreqr;

//extern int current_hpos (void);
extern int vpos;

extern int n_frames;

#ifdef JIT
#define SPCFLAG_END_COMPILE 16384
#endif


extern uae_u16 adkcon;


extern void INTREQ (uae_u16);
extern void INTREQ_0 (uae_u16);
extern void INTREQ_f (uae_u16);
extern void send_interrupt (int num, int delay);
extern uae_u16 INTREQR (void);

/* maximums for statically allocated tables */
#ifdef UAE_MINI
/* absolute minimums for basic A500/A1200-emulation */
#define MAXHPOS 227
#define MAXVPOS 312
#else
#define MAXHPOS 256
#define MAXVPOS 592
#endif



#define CYCLE_REFRESH	0x01
#define CYCLE_STROBE	0x02
#define CYCLE_MISC	0x04
#define CYCLE_SPRITE	0x08
#define CYCLE_COPPER	0x10
#define CYCLE_BLITTER	0x20
#define CYCLE_CPU	0x40
#define CYCLE_CPUNASTY	0x80

extern unsigned long frametime, timeframes;
extern int plfstrt, plfstop, plffirstline, plflastline;
extern uae_u16 htotal, vtotal, beamcon0;


extern void alloc_cycle_ext (int, int);
extern bool ispal (void);
