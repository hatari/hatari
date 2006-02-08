/*
  Hatari - cart.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Cartridge program

  To load programs into memory, through TOS, we need to intercept GEMDOS so we
  can relocate/execute programs via GEMDOS call $4B (Pexec).
  We have some 68000 assembler, located at 0xFA0000 (cartridge memory), which is
  used as our new GEMDOS handler. This checks if we need to intercept the call.

  The assembler routine can be found in 'cart_asm.s', and has been converted to
  a byte array and stored in 'Cart_data[]' (see cartData.c).
*/
const char Cart_rcsid[] = "Hatari $Id: cart.c,v 1.11 2006-02-08 22:49:27 eerot Exp $";

#include "main.h"
#include "cart.h"
#include "configuration.h"
#include "file.h"
#include "log.h"
#include "stMemory.h"
#include "vdi.h"

#include "cartData.c"


/* Possible cartridge file extensions to scan for */
static const char *psCartNameExts[] =
{
	".img",
	".rom",
	".stc",
	NULL
};


/*-----------------------------------------------------------------------*/
/*
  Load an external cartridge image file.
*/
static void Cart_LoadImage(void)
{
	Uint8 *pCartData;
	long nCartSize;
	char *pCartFileName = ConfigureParams.Rom.szCartridgeImageFileName;

	/* Try to load the image file: */
	pCartData = File_Read(pCartFileName, NULL, &nCartSize, psCartNameExts);
	if (!pCartData)
	{
		Log_Printf(LOG_ERROR, "Failed to load '%s'.\n", pCartFileName);
		return;
	}

	if (nCartSize > 0x20000 && nCartSize != 0x20004)
	{
		Log_Printf(LOG_ERROR, "Cartridge file '%s' is too big.\n", pCartFileName);
		return;
	}

	/* There are two type of cartridge images, normal 1:1 images which are
	 * always smaller than 0x20000 bytes, and the .STC images, which are
	 * always 0x20004 bytes (the first 4 bytes are a dummy header).
	 * So if size is 0x20004 bytes we have to skip the first 4 bytes */
	if (nCartSize == 0x20004)
	{
		memcpy(&STRam[0xfa0000], pCartData+4, 0x20000);
	}
	else
	{
		memcpy(&STRam[0xfa0000], pCartData, nCartSize);
	}

	free(pCartData);
}


/*-----------------------------------------------------------------------*/
/*
  Copy ST GEMDOS intercept program image into cartridge memory space
  or load an external cartridge file.
  The intercept program is part of Hatari and used as an interface to the host
  file system through GemDOS. It is also needed for Line-A-Init when using
  extended VDI resolutions.
*/
void Cart_ResetImage(void)
{
	/* "Clear" cartridge ROM space */
	memset(&STRam[0xfa0000], 0xff, 0x20000);

	/* Print a warning if user tries to use an external cartridge file
	 * together with GEMDOS HD emulation or extended VDI resolution: */
	if (strlen(ConfigureParams.Rom.szCartridgeImageFileName) > 0)
	{
		if (bUseVDIRes)
			Log_Printf(LOG_WARN, "Cartridge can't be used together with extended VDI resolution!\n");
		if (ConfigureParams.HardDisk.bUseHardDiskDirectories)
			Log_Printf(LOG_WARN, "Cartridge can't be used together with GEMDOS hard disk emulation!\n");
	}

	/* Use internal cartridge when user wants extended VDI resolution or GEMDOS HD. */
	if (bUseVDIRes || ConfigureParams.HardDisk.bUseHardDiskDirectories)
	{
		/* Copy built-in cartrige data into the cartridge memory of the ST */
		memcpy(&STRam[0xfa0000], Cart_data, sizeof(Cart_data));
	}
	else if (strlen(ConfigureParams.Rom.szCartridgeImageFileName) > 0)
	{
		/* Load external image file: */
		Cart_LoadImage();
	}
}
