 /*
  * UAE - The Un*x Amiga Emulator
  *
  * custom chip support
  *
  * (c) 1995 Bernd Schmidt
  */

#ifndef UAE_CUSTOM_H
#define UAE_CUSTOM_H

#include "uae/types.h"
#include "machdep/rpt.h"

/* These are the masks that are ORed together in the chipset_mask option.
 * If CSMASK_AGA is set, the ECS bits are guaranteed to be set as well.  */
#define CSMASK_ECS_AGNUS 1
#define CSMASK_ECS_DENISE 2
#define CSMASK_AGA 4
#define CSMASK_MASK (CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE | CSMASK_AGA)

uae_u32 get_copper_address (int copno);

extern int custom_init (void);
extern void custom_reset (bool hardreset, bool keyboardreset);
extern int intlev (void);
extern void dumpcustom (void);
extern void uae_reset (int hardreset);

extern void do_copper (void);

extern void notice_new_xcolors (void);
extern void notice_screen_contents_lost(int monid);
extern void init_row_map (void);
extern void init_hz_normal (void);
extern void init_custom (void);

extern void set_picasso_hack_rate(int hz);

/* Set to 1 to leave out the current frame in average frame time calculation.
* Useful if the debugger was active.  */
extern int bogusframe;
extern uae_u32 hsync_counter, vsync_counter;

extern uae_u16 dmacon;
extern uae_u16 intena, intreq, intreqr;

extern int current_hpos (void);
extern int vpos;

extern int n_frames;

STATIC_INLINE int dmaen(unsigned int dmamask)
{
	return (dmamask & dmacon) && (dmacon & 0x200);
}

extern uae_u16 adkcon;

extern unsigned int joy0dir, joy1dir;
extern int joy0button, joy1button;

extern void INTREQ(uae_u16);
extern bool INTREQ_0(uae_u16);
extern void INTREQ_f(uae_u16);
extern void INTREQ_INT(int num, int delay);
extern void rethink_uae_int(void);
extern uae_u16 INTREQR(void);


#define DMA_AUD0      0x0001
#define DMA_AUD1      0x0002
#define DMA_AUD2      0x0004
#define DMA_AUD3      0x0008
#define DMA_DISK      0x0010
#define DMA_SPRITE    0x0020
#define DMA_BLITTER   0x0040
#define DMA_COPPER    0x0080
#define DMA_BITPLANE  0x0100
#define DMA_MASTER    0x0200
#define DMA_BLITPRI   0x0400

#define CYCLE_BITPLANE  1
#define CYCLE_REFRESH	2
#define CYCLE_STROBE	3
#define CYCLE_MISC		4
#define CYCLE_SPRITE	5
#define CYCLE_COPPER	6
#define CYCLE_BLITTER	7
#define CYCLE_CPU		8
#define CYCLE_CPUNASTY	9
#define CYCLE_COPPER_SPECIAL 0x10

#define CYCLE_MASK 0x0f

#ifdef AGA
/* AGA mode color lookup tables */
extern unsigned int xredcolors[256], xgreencolors[256], xbluecolors[256];
#endif
extern int xredcolor_s, xredcolor_b, xredcolor_m;
extern int xgreencolor_s, xgreencolor_b, xgreencolor_m;
extern int xbluecolor_s, xbluecolor_b, xbluecolor_m;

#define RES_LORES 0
#define RES_HIRES 1
#define RES_SUPERHIRES 2
#define RES_MAX 2
#define VRES_NONDOUBLE 0
#define VRES_DOUBLE 1
#define VRES_QUAD 2
#define VRES_MAX 1

/* calculate shift depending on resolution (replaced "decided_hires ? 4 : 8") */
#define RES_SHIFT(res) ((res) == RES_LORES ? 8 : (res) == RES_HIRES ? 4 : 2)

/* get resolution from bplcon0 */
#if AMIGA_ONLY
STATIC_INLINE int GET_RES_DENISE (uae_u16 con0)
{
	if (!(currprefs.chipset_mask & CSMASK_ECS_DENISE))
		con0 &= ~0x40; // SUPERHIRES
	return ((con0) & 0x40) ? RES_SUPERHIRES : ((con0) & 0x8000) ? RES_HIRES : RES_LORES;
}
STATIC_INLINE int GET_RES_AGNUS (uae_u16 con0)
{
	if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		con0 &= ~0x40; // SUPERHIRES
	return ((con0) & 0x40) ? RES_SUPERHIRES : ((con0) & 0x8000) ? RES_HIRES : RES_LORES;
}
#endif // AMIGA_ONLY

/* get sprite width from FMODE */
#define GET_SPRITEWIDTH(FMODE) ((((FMODE) >> 2) & 3) == 3 ? 64 : (((FMODE) >> 2) & 3) == 0 ? 16 : 32)
/* Compute the number of bitplanes from a value written to BPLCON0  */
STATIC_INLINE int GET_PLANES(uae_u16 bplcon0)
{
	if ((bplcon0 & 0x0010) && (bplcon0 & 0x7000))
		return 0; // >8 planes = 0 planes
	if (bplcon0 & 0x0010)
		return 8; // AGA 8-planes bit
	return (bplcon0 >> 12) & 7; // normal planes bits
}

extern void fpscounter_reset(void);
extern frame_time_t idletime;
extern int lightpen_x[2], lightpen_y[2];
extern int lightpen_cx[2], lightpen_cy[2], lightpen_active, lightpen_enabled, lightpen_enabled2;

struct customhack {
	uae_u16 v;
	int vpos, hpos;
};
void customhack_put (struct customhack *ch, uae_u16 v, int hpos);
uae_u16 customhack_get (struct customhack *ch, int hpos);
extern void alloc_cycle_ext (int, int);
extern bool alloc_cycle_blitter(int hpos, uaecptr *ptr, int, int);
extern uaecptr alloc_cycle_blitter_conflict_or(int, int, bool*);
extern bool ispal (int *line);
extern bool isvga(void);
extern int current_maxvpos(void);
extern int inprec_open(char *fname, int record);
extern void sleep_millis (int ms);

/* referred by prefetch.h */
extern uae_u32 wait_cpu_cycle_read (uaecptr addr, int mode);
extern void wait_cpu_cycle_write (uaecptr addr, int mode, uae_u32 v);
extern uae_u32 wait_cpu_cycle_read_ce020 (uaecptr addr, int mode);
extern void wait_cpu_cycle_write_ce020 (uaecptr addr, int mode, uae_u32 v);

#endif /* UAE_CUSTOM_H */
