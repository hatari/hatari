/*
  Hatari - med640x32.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Screen Conversion, Medium Res to 640x32Bit
*/

static void ConvertMediumRes_640x32Bit(void)
{
	Uint32 *edi, *ebp;
	Uint32 *esi;
	Uint32 eax;
	int y;

	Convert_StartFrame();            /* Start frame, track palettes */

	for (y = STScreenStartHorizLine; y < STScreenEndHorizLine; y++)
	{

		eax = STScreenLineOffset[y] + STScreenLeftSkipBytes;  /* Offset for this line + Amount to skip on left hand side */
		edi = (Uint32 *)((Uint8 *)pSTScreen + eax);        /* ST format screen 4-plane 16 colors */
		ebp = (Uint32 *)((Uint8 *)pSTScreenCopy + eax);    /* Previous ST format screen */
		esi = (Uint32 *)pPCScreenDest;                     /* PC format screen */

		if (AdjustLinePaletteRemap(y) & 0x00030000)        /* Change palette table */
			Line_ConvertMediumRes_640x32Bit(edi, ebp, esi, eax);
		else
			Line_ConvertLowRes_640x32Bit(edi, ebp, esi, eax);

		/* Offset to next line */
		pPCScreenDest = (((Uint8 *)pPCScreenDest) + PCScreenBytesPerLine * 2);
	}
}


static void Line_ConvertMediumRes_640x32Bit(Uint32 *edi, Uint32 *ebp, Uint32 *esi, Uint32 eax)
{
	Uint32 ebx, ecx;
	int x, update, Screen4BytesPerLine;

	x = STScreenWidthBytes >> 2;   /* Amount to draw across in 16-pixels (4 bytes) */
	Screen4BytesPerLine = PCScreenBytesPerLine/4;
	update = ScrUpdateFlag & PALETTEMASK_UPDATEMASK;

	do  /* x-loop */
	{

		/* Do 16 pixels at one time */
		ebx = *edi;

		if (update || ebx != *ebp)      /* Does differ? */
		{
			/* copy word */

			bScreenContentsChanged = true;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			/* Plot in 'right-order' on big endian systems */
			if (!bScrDoubleY)                     /* Double on Y? */
			{
				MED_BUILD_PIXELS_0 ;              /* Generate 'ecx' as pixels [12,13,14,15] */
				PLOT_MED_640_32BIT(12) ;
				MED_BUILD_PIXELS_1 ;              /* Generate 'ecx' as pixels [4,5,6,7] */
				PLOT_MED_640_32BIT(4) ;
				MED_BUILD_PIXELS_2 ;              /* Generate 'ecx' as pixels [8,9,10,11] */
				PLOT_MED_640_32BIT(8) ;
				MED_BUILD_PIXELS_3 ;              /* Generate 'ecx' as pixels [0,1,2,3] */
				PLOT_MED_640_32BIT(0) ;
			}
			else
			{
				MED_BUILD_PIXELS_0 ;              /* Generate 'ecx' as pixels [12,13,14,15] */
				PLOT_MED_640_32BIT_DOUBLE_Y(12) ;
				MED_BUILD_PIXELS_1 ;              /* Generate 'ecx' as pixels [4,5,6,7] */
				PLOT_MED_640_32BIT_DOUBLE_Y(4) ;
				MED_BUILD_PIXELS_2 ;              /* Generate 'ecx' as pixels [8,9,10,11] */
				PLOT_MED_640_32BIT_DOUBLE_Y(8) ;
				MED_BUILD_PIXELS_3 ;              /* Generate 'ecx' as pixels [0,1,2,3] */
				PLOT_MED_640_32BIT_DOUBLE_Y(0) ;
			}
#else
			/* Plot in 'wrong-order', as ebx is 68000 endian */
			if (!bScrDoubleY)                     /* Double on Y? */
			{
				MED_BUILD_PIXELS_0 ;              /* Generate 'ecx' as pixels [4,5,6,7] */
				PLOT_MED_640_32BIT(4) ;
				MED_BUILD_PIXELS_1 ;              /* Generate 'ecx' as pixels [12,13,14,15] */
				PLOT_MED_640_32BIT(12) ;
				MED_BUILD_PIXELS_2 ;              /* Generate 'ecx' as pixels [0,1,2,3] */
				PLOT_MED_640_32BIT(0) ;
				MED_BUILD_PIXELS_3 ;              /* Generate 'ecx' as pixels [8,9,10,11] */
				PLOT_MED_640_32BIT(8) ;
			}
			else
			{
				MED_BUILD_PIXELS_0 ;              /* Generate 'ecx' as pixels [4,5,6,7] */
				PLOT_MED_640_32BIT_DOUBLE_Y(4) ;
				MED_BUILD_PIXELS_1 ;              /* Generate 'ecx' as pixels [12,13,14,15] */
				PLOT_MED_640_32BIT_DOUBLE_Y(12) ;
				MED_BUILD_PIXELS_2 ;              /* Generate 'ecx' as pixels [0,1,2,3] */
				PLOT_MED_640_32BIT_DOUBLE_Y(0) ;
				MED_BUILD_PIXELS_3 ;              /* Generate 'ecx' as pixels [8,9,10,11] */
				PLOT_MED_640_32BIT_DOUBLE_Y(8) ;
			}
#endif
		}

		esi += 16;                      /* Next PC pixels */
		edi += 1;                       /* Next ST pixels */
		ebp += 1;                       /* Next ST copy pixels */
	}
	while (--x);                        /* Loop on X */

}
