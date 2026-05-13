/*
  Hatari - buserror.c
  Copyright (C) 2026 by manni07
  Created: 2026-05-13

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#include "main.h"
#include "configuration.h"
#include "log.h"
#include "tos.h"

/*
 * Bus error addresses TOS probes to detect optional hardware.
 * In debug logging these are intentionally still visible, but at
 * normal log levels they should not be treated as compatibility bugs.
 */
bool M68000_IsVerboseBusError(uint32_t pc, uint32_t addr);

bool M68000_IsVerboseBusError(uint32_t pc, uint32_t addr)
{
	const uint32_t nTosProbeAddrs[] =
	{
		0xf00039, 0xff8900, 0xff8a00, 0xff8c83,
		0xff8e0d, 0xff8e09, 0xfffa40
	};
	const uint32_t nEmuTosProbeAddrs[] =
	{
		0xf0001d, 0xf0005d, 0xf0009d, 0xf000dd, 0xff8006, 0xff8282,
		0xff8400, 0xff8701, 0xff8901, 0xff8943, 0xff8961, 0xff8c80,
		0xff8a3c, 0xff9201, 0xfffa81, 0xfffe00
	};
	int idx;

	if (ConfigureParams.Log.nTextLogLevel == LOG_DEBUG)
		return true;

	if (ConfigureParams.System.bAddressSpace24
	    || (addr & 0xff000000) == 0xff000000)
	{
		addr &= 0xffffff;
	}

	/* Program just probing for FPU? A lot of C startup code is always
	 * doing this, so reporting bus errors here would be annoying */
	if (addr == 0xfffa42)
		return false;

	/* Always report other bus errors from normal programs */
	if (pc < TosAddress || pc > TosAddress + TosSize)
		return true;

	for (idx = 0; idx < ARRAY_SIZE(nTosProbeAddrs); idx++)
	{
		if (nTosProbeAddrs[idx] == addr)
			return false;
	}

	if (bIsEmuTOS)
	{
		for (idx = 0; idx < ARRAY_SIZE(nEmuTosProbeAddrs); idx++)
		{
			if (nEmuTosProbeAddrs[idx] == addr)
				return false;
		}
	}

	return true;
}
