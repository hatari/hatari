/*
  Hatari - cart.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Cartridge program

  To load programs into memory, through TOS, we need to intercept GEMDOS so we
  can relocate/execute programs via GEMDOS call $4B (Pexec).
  We have some 68000 assembler, located at 0xFA0000 (cartridge memory), which is
  used as our new GEMDOS handler. This checks if we need to intercept the call.

  The assembler routine can be found in 'cart_asm.s', and has been converted to
  a byte array and stored in 'Cart_data[]' (see cartData.c).
*/
const char Cart_fileid[] = "Hatari cart.c : " __DATE__ " " __TIME__;

/* 2007/12/09	[NP]	Change the function associated to opcodes $8, $a and $c only if hard drive	*/
/*			emulation is ON. Else, these opcodes should give illegal instructions (also	*/
/*			see uae-cpu/newcpu.c).								*/


#include "main.h"
#include "cart.h"
#include "configuration.h"
#include "file.h"
#include "log.h"
#include "m68000.h"
#include "stMemory.h"
#include "tos.h"
#include "vdi.h"
#include "hatari-glue.h"
#include "newcpu.h"

#include "cartData.c"


/* Possible cartridge file extensions to scan for */
static const char * const psCartNameExts[] =
{
	".img",
	".rom",
	".stc",
	NULL
};

static int PatchIllegal = false;


/*-----------------------------------------------------------------------*/
/**
 * Load an external cartridge image file.
 */
static void Cart_LoadImage(void)
{
	Uint8 *pCartData;
	long nCartSize;
	char *pCartFileName = ConfigureParams.Rom.szCartridgeImageFileName;

	/* Try to load the image file: */
	pCartData = File_Read(pCartFileName, &nCartSize, psCartNameExts);
	if (!pCartData)
	{
		Log_Printf(LOG_ERROR, "Failed to load '%s'.\n", pCartFileName);
		return;
	}

	if (nCartSize < 40 || (nCartSize > 0x20000 && nCartSize != 0x20004))
	{
		Log_Printf(LOG_ERROR, "Cartridge file '%s' has illegal size.\n", pCartFileName);
		free(pCartData);
		return;
	}

	/* There are two type of cartridge images, normal 1:1 images which are
	 * always smaller than or equal to 0x20000 bytes, and the .STC images,
	 * which are always 0x20004 bytes (the first 4 bytes are a dummy header).
	 * So if size is 0x20004 bytes we have to skip the first 4 bytes */
	if (nCartSize == 0x20004)
	{
		memcpy(&RomMem[0xfa0000], pCartData+4, 0x20000);
	}
	else
	{
		memcpy(&RomMem[0xfa0000], pCartData, nCartSize);
	}

	free(pCartData);
}


/*-----------------------------------------------------------------------*/
/**
 * Copy ST GEMDOS intercept program image into cartridge memory space
 * or load an external cartridge file.
 * The intercept program is part of Hatari and used as an interface to the host
 * file system through GemDOS. It is also needed for Line-A-Init when using
 * extended VDI resolutions.
 */
void Cart_ResetImage(void)
{
	/* "Clear" cartridge ROM space */
	memset(&RomMem[0xfa0000], 0xff, 0x20000);

	/* Print a warning if user tries to use an external cartridge file
	 * together with GEMDOS HD emulation or extended VDI resolution: */
	if (strlen(ConfigureParams.Rom.szCartridgeImageFileName) > 0)
	{
		if (bUseVDIRes)
			Log_Printf(LOG_WARN, "Cartridge can't be used together with extended VDI resolution!\n");
		if (ConfigureParams.HardDisk.bUseHardDiskDirectories)
			Log_Printf(LOG_WARN, "Cartridge can't be used together with GEMDOS hard disk emulation!\n");
		if (LogTraceFlags & (TRACE_OS_GEMDOS | TRACE_OS_BASE | TRACE_OS_VDI | TRACE_OS_AES))
			Log_Printf(LOG_WARN, "Cartridge can't be used together with GEMDOS/VDI/AES tracing!\n");
	}

	/* Use internal cartridge trampoline code when user wants extended VDI
	 * resolution, GEMDOS HD emulation or to trace GEMDOS, VDI or AES.
	 * (OS_BASE does subset of GEMDOS tracing)
	 * But don't use it on TOS 0.00, it does not work there. */
	PatchIllegal = false;				/* By default, don't patch opcodes */
	if ((bUseVDIRes || ConfigureParams.HardDisk.bUseHardDiskDirectories ||
	    LogTraceFlags & (TRACE_OS_GEMDOS | TRACE_OS_BASE | TRACE_OS_VDI | TRACE_OS_AES))
	    && TosVersion >= 0x100)
	{
		/* Copy built-in cartridge data into the cartridge memory of the ST */
		memcpy(&RomMem[0xfa0000], Cart_data, sizeof(Cart_data));
		PatchIllegal = true;
	}
	else if (strlen(ConfigureParams.Rom.szCartridgeImageFileName) > 0)
	{
		/* Load external image file: */
		Cart_LoadImage();
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Patch the cpu tables to intercept some opcodes used for Gemdos HD
 * emulation or for NatFeats.
 * We need to split this from Cart_ResetImage(), as the patches should
 * be applied after building the cpu opcodes tables.
 */
void Cart_Patch(void)
{
	if (PatchIllegal == true)
	{
		//fprintf ( stderr ," Cart_ResetImage patch\n" );
		/* Hatari's specific illegal opcodes for HD emulation */
		cpufunctbl[GEMDOS_OPCODE] = OpCode_GemDos;	/* 0x0008 */
		cpufunctbl[SYSINIT_OPCODE] = OpCode_SysInit;	/* 0x000a */
		cpufunctbl[VDI_OPCODE] = OpCode_VDI;		/* 0x000c */
	}
	else
	{
		//fprintf ( stderr ," Cart_ResetImage no patch\n" );
		/* No built-in cartridge loaded : set same handler as 0x4afc (illegal) */
		cpufunctbl[GEMDOS_OPCODE] = cpufunctbl[ 0x4afc ];	/* 0x0008 */
		cpufunctbl[SYSINIT_OPCODE] = cpufunctbl[ 0x4afc ];	/* 0x000a */
		cpufunctbl[VDI_OPCODE] = cpufunctbl[ 0x4afc ];		/* 0x000c */
	}

	/* although these don't need cartridge code, it's better
	 * to configure all illegal opcodes in same place...
	 */
	if (ConfigureParams.Log.bNatFeats)
	{
		/* illegal opcodes for emulators Native Features */
		cpufunctbl[NATFEAT_ID_OPCODE] = OpCode_NatFeat_ID;	/* 0x7300 */
		cpufunctbl[NATFEAT_CALL_OPCODE] = OpCode_NatFeat_Call;	/* 0x7301 */
	}
	else
	{
		/* No Native Features : set same handler as 0x4afc (illegal) */
		cpufunctbl[NATFEAT_ID_OPCODE] = cpufunctbl[ 0x4afc ];	/* 0x7300 */
		cpufunctbl[NATFEAT_CALL_OPCODE] = cpufunctbl[ 0x4afc ];	/* 0x7300 */
	}
}
