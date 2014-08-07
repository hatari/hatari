/*
  Hatari - low320x8.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Screen conversion function, Low Res to 320x8Bit
*/

static void ConvertLowRes_320x8Bit(void)
{
	Uint32 *edi, *ebp;
	Uint32 *esi;
	Uint32 eax, edx;	/* set & used by macros */
	Uint32 ebx, ecx;
	int y, x, update;

	Convert_StartFrame();             /* Start frame, track palettes */

	for (y = STScreenStartHorizLine; y < STScreenEndHorizLine; y++)
	{

		/* Get screen addresses, 'edi'-ST screen, 'ebp'-Previous ST screen, 'esi'-PC screen */
		eax = STScreenLineOffset[y] + STScreenLeftSkipBytes;  /* Offset for this line + Amount to skip on left hand side */
		edi = (Uint32 *)((Uint8 *)pSTScreen + eax);       /* ST format screen 4-plane 16 colors */
		ebp = (Uint32 *)((Uint8 *)pSTScreenCopy + eax);   /* Previous ST format screen */
		esi = (Uint32 *)pPCScreenDest;                    /* PC format screen, byte per pixel 256 colors */

		update = AdjustLinePaletteRemap(y) & PALETTEMASK_UPDATEMASK;

		x = STScreenWidthBytes>>3;   /* Amount to draw across in 16-pixels(8 bytes) */

		do    /* x-loop */
		{
			/* Do 16 pixels at one time */
			ebx = *edi;
			ecx = *(edi+1);

			if (update || ebx!=*ebp || ecx!=*(ebp+1)) /* Does differ? */
			{
				/* copy word */

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

			esi += 4;                 /* Next PC pixels */
			edi += 2;                 /* Next ST pixels */
			ebp += 2;                 /* Next ST copy pixels */
		}
		while (--x);             /* Loop on X */

		pPCScreenDest = (((Uint8 *)pPCScreenDest)+PCScreenBytesPerLine);  /* Offset to next line */
	}
}
