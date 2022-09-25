/*
 * Hatari - profile.h
 * 
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 */

#ifndef HATARI_PROFILE_H
#define HATARI_PROFILE_H

/* caller types */
#define CALL_UNDEFINED	0	/* = call type information not supported */
typedef enum {
	CALL_UNKNOWN	= 1,
	CALL_NEXT	= 2,
	CALL_BRANCH	= 4,
	CALL_SUBROUTINE	= 8,
	CALL_SUBRETURN	= 16,
	CALL_EXCEPTION	= 32,
	CALL_EXCRETURN	= 64,
	CALL_INTERRUPT	= 128
} calltype_t;

/* profile command parsing */
extern const char Profile_Description[];
extern char *Profile_Match(const char *text, int state);
extern int Profile_Command(int nArgc, char *psArgs[], bool bForDsp);

/* CPU profile control */
extern void Profile_CpuFree(void);
extern bool Profile_CpuStart(void);
extern void Profile_CpuUpdate(void);
extern void Profile_CpuStop(void);

/* CPU profile results */
extern bool Profile_CpuAddr_HasData(uint32_t addr);
extern int Profile_CpuAddr_DataStr(char *buffer, int maxlen, uint32_t addr);

/* DSP profile control */
extern void Profile_DspFree(void);
extern bool Profile_DspStart(void);
extern void Profile_DspUpdate(void);
extern void Profile_DspStop(void);

/* DSP profile results */
extern bool Profile_DspAddressData(uint16_t addr, float *percentage, uint64_t *count,
                                   uint64_t *cycles, uint16_t *cycle_diff);

#endif
