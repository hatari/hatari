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
const char Cart_fileid[] = "Hatari cart.c";

/* 2007/12/09	[NP]	Change the function associated to opcodes $8, $a and $c only if hard drive	*/
/*			emulation is ON. Else, these opcodes should give illegal instructions (also	*/
/*			see cpu/newcpu.c).								*/


#include "main.h"
#include "cart.h"
#include "configuration.h"
#include "file.h"
#include "log.h"
#include "m68000.h"
#include "stMemory.h"
#include "inffile.h"
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


/**
 * Load an external cartridge image file.
 */
static void Cart_LoadImage(void)
{
	uint8_t *pCartData;
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


/**
 * Check whether we want to use internal cartridge code, i.e. when user wants
 * extended VDI resolution, use Autostarting, or to trace GEMDOS, VDI or AES
 * (OS_BASE does subset of GEMDOS tracing).
 * But don't use it on TOS 0.00, it does not work there.
 */
bool Cart_UseBuiltinCartridge(void)
{
#define NEEDS_CART (TRACE_OS_GEMDOS | TRACE_OS_BASE | TRACE_OS_VDI | TRACE_OS_AES)
	return (bUseVDIRes || INF_Overriding(AUTOSTART_INTERCEPT) ||
	        ConfigureParams.HardDisk.bUseHardDiskDirectories ||
	        LOG_TRACE_LEVEL(NEEDS_CART))
	       && (TosVersion >= 0x100 || !bUseTos);
}


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
	 * together with something else requiring cartridge code:
	 * - GEMDOS hard disk emulation
	 * - extended VDI resolution
	 * - GEMDOS/AES/VDI tracing
	 */
	if (strlen(ConfigureParams.Rom.szCartridgeImageFileName) > 0 &&
	    (bUseVDIRes || ConfigureParams.HardDisk.bUseHardDiskDirectories ||
	     (LogTraceFlags & (TRACE_OS_GEMDOS | TRACE_OS_BASE | TRACE_OS_VDI | TRACE_OS_AES))))
	{
		Log_AlertDlg(LOG_ERROR, "Cartridge disabled! It can't be used with VDI mode, GEMDOS HD emulation nor their tracing.");
	}

	if (Cart_UseBuiltinCartridge())
	{
		/* Copy built-in cartridge data into the cartridge memory of the ST */
		memcpy(&RomMem[0xfa0000], Cart_data, sizeof(Cart_data));
	}
	else if (strlen(ConfigureParams.Rom.szCartridgeImageFileName) > 0)
	{
		/* Load external image file: */
		Cart_LoadImage();
	}
}
