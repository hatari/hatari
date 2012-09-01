/*
  Hatari - vdi4.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Screen Conversion, VDI resolution with 4 colors
*/


static void ConvertVDIRes_4Colour(void)
{
	Uint32 *edi, *ebp;
	Uint32 *esi;
	Uint32 eax, ebx, ecx;
	int y, x, update;

	/* Get screen addresses, 'edi'-ST screen, 'ebp'-Previous ST screen, 'esi'-PC screen */
	edi = (Uint32 *)pSTScreen;          /* ST format screen 2-plane 4 colors */
	ebp = (Uint32 *)pSTScreenCopy;      /* Previous ST format screen */
	update = ScrUpdateFlag & PALETTEMASK_UPDATEMASK;

	for (y = 0; y < VDIHeight; y++)
	{

		esi = (Uint32 *)pPCScreenDest;  /* PC format screen, byte per pixel 256 colors */

		x = VDIWidth >> 4;              /* Amount to draw across - in 16-pixels (4 bytes) */

		do  /* x-loop */
		{
			/* Do 16 pixels at one time */
			ebx = *edi;

			if (update || ebx != *ebp)  /* Update? */
			{
				bScreenContentsChanged = true;

				/* Plot pixels */
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
				MED_BUILD_PIXELS_0 ;    /* Generate 'ecx' as pixels [12,13,14,15] */
				PLOT_MED_640_8BIT(3) ;
				MED_BUILD_PIXELS_1 ;    /* Generate 'ecx' as pixels [4,5,6,7] */
				PLOT_MED_640_8BIT(1) ;
				MED_BUILD_PIXELS_2 ;    /* Generate 'ecx' as pixels [8,9,10,11] */
				PLOT_MED_640_8BIT(2) ;
				MED_BUILD_PIXELS_3 ;    /* Generate 'ecx' as pixels [0,1,2,3] */
				PLOT_MED_640_8BIT(0) ;
#else
				MED_BUILD_PIXELS_0 ;    /* Generate 'ecx' as pixels [4,5,6,7] */
				PLOT_MED_640_8BIT(1) ;
				MED_BUILD_PIXELS_1 ;    /* Generate 'ecx' as pixels [12,13,14,15] */
				PLOT_MED_640_8BIT(3) ;
				MED_BUILD_PIXELS_2 ;    /* Generate 'ecx' as pixels [0,1,2,3] */
				PLOT_MED_640_8BIT(0) ;
				MED_BUILD_PIXELS_3 ;    /* Generate 'ecx' as pixels [8,9,10,11] */
				PLOT_MED_640_8BIT(2) ;
#endif
			}

			esi += 4;                   /* Next PC pixels */
			edi += 1;                   /* Next ST pixels */
			ebp += 1;                   /* Next ST copy pixels */
		}
		while (--x);                    /* Loop on X */

		/* Offset to next line */
		pPCScreenDest = (((Uint8 *)pPCScreenDest) + PCScreenBytesPerLine);
	}
}
