/*
  Hatari - options_cpu.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef OPTIONS_CPU_H
#define OPTIONS_CPU_H

struct uae_prefs {
    int cpu_level;  // lolo : not in current amiga emulator
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
};

extern struct uae_prefs currprefs, changed_prefs;


extern void check_prefs_changed_cpu (void);

#endif /* OPTIONS_CPU_H */
