/*
  Hatari - cart.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Cartridge program

  To load programs into memory, through TOS, we need to intercept GEMDOS so we can
  relocate/execute programs via GEMDOS call $4B (Pexec).
  We have some 68000 assembler, located at 0xFA0000 (cartridge memory), which is
  used as our new GEMDOS handler. This checks if we need to intercept the call.

  The assembler routine can be found in 'cart_asm.s', and has been converted to a byte
  array and stored in 'Cart_data[]' (see cartdata.c).
*/
char Cart_rcsid[] = "Hatari $Id: cart.c,v 1.8 2005-09-13 01:10:09 thothy Exp $";

#include "main.h"
#include "cart.h"
#include "configuration.h"
#include "file.h"
#include "stMemory.h"
#include "vdi.h"

#include "cartData.c"


/*-----------------------------------------------------------------------*/
/*
  Load ST GEMDOS intercept program image into cartridge memory space.
  This is used as an interface to the host file system and for GemDOS.
*/
void Cart_LoadImage(void)
{
	char *pCartFileName = ConfigureParams.Rom.szCartridgeImageFileName;

	/* "Clear" cartridge ROM space */
	memset(&STRam[0xfa0000], 0xff, 0x20000);

	if (bUseVDIRes || ConfigureParams.HardDisk.bUseHardDiskDirectories)
	{
		/* Copy cartrige data into ST's cartridge memory */
		memcpy(&STRam[0xfa0000], Cart_data, sizeof(Cart_data));
	}
	else if (strlen(pCartFileName) > 0)
	{
		/* Check if we can load an external cartridge file: */
		if (bUseVDIRes)
			fprintf(stderr, "Warning: Cartridge can't be used together with extended VDI resolution!\n");
		else if (ConfigureParams.HardDisk.bUseHardDiskDirectories)
			fprintf(stderr, "Warning: Cartridge can't be used together with GEMDOS hard disk emulation!\n");
		else if (!File_Exists(pCartFileName))
			fprintf(stderr, "Cartridge file not found: %s\n", pCartFileName);
		else if (File_Length(pCartFileName) > 0x20000)
			fprintf(stderr, "Cartridge file %s is too big.\n", pCartFileName);
		else
		{
			/* Now we can load it: */
			File_Read(pCartFileName, &STRam[0xfa0000], NULL, NULL);
		}
	}
}
