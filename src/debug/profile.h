/*
 * Hatari - profile.h
 * 
 * This file is distributed under the GNU Public License, version 2 or at
 * your option any later version. Read the file gpl.txt for details.
 */

#ifndef HATARI_PROFILE_H
#define HATARI_PROFILE_H

/* profile command parsing */
extern const char Profile_Description[];
extern char *Profile_Match(const char *text, int state);
extern bool Profile_Command(int nArgc, char *psArgs[], bool bForDsp);

/* CPU profile control */
extern bool Profile_CpuStart(void);
extern void Profile_CpuUpdate(void);
extern void Profile_CpuStop(void);
/* CPU profile results */
extern void Profile_CpuShowStats(void);
extern void Profile_CpuShowCycles(unsigned int show);
extern void Profile_CpuShowCounts(unsigned int show, bool only_symbols);
extern bool Profile_CpuAddressData(Uint32 addr, Uint32 *count, Uint32 *cycles);

/* DSP profile control */
extern bool Profile_DspStart(void);
extern void Profile_DspUpdate(void);
extern void Profile_DspStop(void);
/* DSP profile results */
extern void Profile_DspShowStats(void);
extern void Profile_DspShowCycles(unsigned int show);
extern void Profile_DspShowCounts(unsigned int show, bool only_symbols);
extern bool Profile_DspAddressData(Uint16 addr, Uint32 *count, Uint32 *cycles);

#endif
