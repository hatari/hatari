/*
  Hatari - test-buserror.c
  Copyright (C) 2026 by manni07
  Created: 2026-05-13

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#include <assert.h>
#include <string.h>

#include "main.h"
#include "configuration.h"
#include "log.h"
#include "tos.h"

bool M68000_IsVerboseBusError(uint32_t pc, uint32_t addr);

CNF_PARAMS ConfigureParams;
bool bIsEmuTOS;
uint32_t TosAddress, TosSize;

static void reset_state(void)
{
	memset(&ConfigureParams, 0, sizeof(ConfigureParams));
	ConfigureParams.System.bAddressSpace24 = true;
	ConfigureParams.Log.nTextLogLevel = LOG_INFO;
	TosAddress = 0xfc0000;
	TosSize = 0x40000;
	bIsEmuTOS = false;
}

static void test_tos_hardware_probe_is_suppressed(void)
{
	reset_state();
	assert(!M68000_IsVerboseBusError(0xfc0ee2, 0xffff8a00));
}

static void test_debug_logging_keeps_probe_visible(void)
{
	reset_state();
	ConfigureParams.Log.nTextLogLevel = LOG_DEBUG;
	assert(M68000_IsVerboseBusError(0xfc0ee2, 0xffff8a00));
}

static void test_non_tos_pc_is_reported(void)
{
	reset_state();
	assert(M68000_IsVerboseBusError(0x1000, 0xffff8a00));
}

static void test_fpu_probe_is_suppressed(void)
{
	reset_state();
	assert(!M68000_IsVerboseBusError(0xfc1000, 0xfffffa42));
}

static void test_emutos_probe_is_suppressed(void)
{
	reset_state();
	bIsEmuTOS = true;
	assert(!M68000_IsVerboseBusError(0xfc2000, 0xffff8901));
}

int main(void)
{
	test_tos_hardware_probe_is_suppressed();
	test_debug_logging_keeps_probe_visible();
	test_non_tos_pc_is_reported();
	test_fpu_probe_is_suppressed();
	test_emutos_probe_is_suppressed();
	return 0;
}
