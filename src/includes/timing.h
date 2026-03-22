/*
  Hatari - timing.h

  SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef HATARI_TIMING_H
#define HATARI_TIMING_H

int64_t Timing_GetTicks(void);
void Timing_PrintSpeed(void);
uint32_t Timing_SetRunVBLs(uint32_t vbls);
const char* Timing_SetVBLSlowdown(int factor);
uint32_t Timing_SetRunVBLs(uint32_t vbls);
const char* Timing_SetVBLSlowdown(int factor);
void Timing_WaitOnVbl(void);
void Timing_CheckForAccurateDelays(void);

#endif
