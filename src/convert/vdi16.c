/*
  Hatari - vdi16.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Screen Conversion, VDI resolution with 16 colors
*/

static void ConvertVDIRes_16Colour(void)
{
	Uint32 *edi, *ebp;
	Uint32 *esi;
	Uint32 eax, edx;	/* set & used by macros */
	Uint32 ebx, ecx;
	int y, x, update;

	/* Get screen addresses, 'edi'-ST screen, 'ebp'-Previous ST screen,
	 * 'esi'-PC screen */

	edi = (Uint32 *)pSTScreen;        /* ST format screen 4-plane 16 colors */
	ebp = (Uint32 *)pSTScreenCopy;    /* Previous ST format screen */
	update = ScrUpdateFlag & PALETTEMASK_UPDATEMASK;

	for (y = 0; y < VDIHeight; y++)
	{

		esi = (Uint32 *)pPCScreenDest;  /* PC format screen, byte per pixel 256 colors */

		x = VDIWidth >> 4;              /* Amount to draw across - in 16-pixels (8 bytes) */

		do  /* x-loop */
		{
			/* Do 16 pixels at one time */
			ebx = *edi;
			ecx = *(edi+1);

			/* Full update? or just test changes? */
			if (update || ebx != *ebp || ecx != *(ebp+1))   /* Does differ? */
			{
				bScreenContentsChanged = true;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
				/* Plot pixels */
				LOW_BUILD_PIXELS_0 ;    /* Generate 'ecx' as pixels [12,13,14,15] */
				PLOT_LOW_320_8BIT(3) ;
				LOW_BUILD_PIXELS_1 ;    /* Generate 'ecx' as pixels [4,5,6,7] */
				PLOT_LOW_320_8BIT(1) ;
				LOW_BUILD_PIXELS_2 ;    /* Generate 'ecx' as pixels [8,9,10,11] */
				PLOT_LOW_320_8BIT(2) ;
				LOW_BUILD_PIXELS_3 ;    /* Generate 'ecx' as pixels [0,1,2,3] */
				PLOT_LOW_320_8BIT(0) ;
#else
				/* Plot pixels */
				LOW_BUILD_PIXELS_0 ;    /* Generate 'ecx' as pixels [4,5,6,7] */
				PLOT_LOW_320_8BIT(1) ;
				LOW_BUILD_PIXELS_1 ;    /* Generate 'ecx' as pixels [12,13,14,15] */
				PLOT_LOW_320_8BIT(3) ;
				LOW_BUILD_PIXELS_2 ;    /* Generate 'ecx' as pixels [0,1,2,3] */
				PLOT_LOW_320_8BIT(0) ;
				LOW_BUILD_PIXELS_3 ;    /* Generate 'ecx' as pixels [8,9,10,11] */
				PLOT_LOW_320_8BIT(2) ;
#endif
			}

			esi += 4;                   /* Next PC pixels */
			edi += 2;                   /* Next ST pixels */
			ebp += 2;                   /* Next ST copy pixels */
		}
		while (--x);                    /* Loop on X */

		/* Offset to next line */
		pPCScreenDest = (((Uint8 *)pPCScreenDest) + PCScreenBytesPerLine);
	}
}
