/*
  Hatari - options_cpu.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef OPTIONS_CPU_H
#define OPTIONS_CPU_H

struct uae_prefs {
    int cpu_level;
    int cpu_compatible;
    int address_space_24;
    int cpu_cycle_exact;
};

extern struct uae_prefs currprefs, changed_prefs;


extern void check_prefs_changed_cpu (void);

#endif /* OPTIONS_CPU_H */
