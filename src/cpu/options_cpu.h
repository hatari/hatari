/*
  Hatari - options_cpu.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef OPTIONS_CPU_H
#define OPTIONS_CPU_H

struct uae_prefs {
	/* Laurent */
    int cpu_level;  // CAUTION : not in current amiga emulator, should probably be replaced by cpu_model in all hatari code
    int cpu_compatible;
    int address_space_24;
   	int cpu_idle;
   	int cpu_cycle_exact;
	int cpu_clock_multiplier;
	int cpu_frequency;
	int blitter_cycle_exact;
	int m68k_speed;
	int cpu_model;
	int mmu_model;
	int cpu060_revision;
	int fpu_model;
	int fpu_revision;
	int cachesize;
	int compfpu;
	
	/* Laurent */
	/* Next variables are probably not useful for hatari, but they're useful to let newcpu.c compile*/
	/* Let's see later if they should be kept or removed */
	TCHAR inprecfile[MAX_DPATH];
	int inprecmode;
	int ntscmode;
	uae_u32 chipmem_size;	
	int cs_mbdmac;
	uae_u32 mbresmem_low_size;
	int produce_sound;

};

extern struct uae_prefs currprefs, changed_prefs;
extern void fixup_cpu (struct uae_prefs *prefs);


extern void check_prefs_changed_cpu (void);

#endif /* OPTIONS_CPU_H */
