/*
  Hatari - low640x16.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Screen Conversion, Low Res to 640x16Bit
*/

static void Line_ConvertLowRes_640x16Bit(Uint32 *edi, Uint32 *ebp, Uint32 *esi, Uint32 eax)
{
	Uint32 edx;
	Uint32 ebx, ecx;
	int x, update;

	x = STScreenWidthBytes>>3;   /* Amount to draw across in 16-pixels(8 bytes) */
	update = ScrUpdateFlag & PALETTEMASK_UPDATEMASK;

	do    /* x-loop */
	{
		/* Do 16 pixels at one time */
		ebx = *edi;
		ecx = *(edi+1);

		if (update || ebx != *ebp || ecx != *(ebp+1))    /* Does differ? */
		{
			/* copy word */

			bScreenContentsChanged = true;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			/* Plot pixels in 'right-order' on big endian systems */
			LOW_BUILD_PIXELS_0 ;              /* Generate 'ecx' as pixels [12,13,14,15] */
			PLOT_LOW_640_16BIT(12) ;
			LOW_BUILD_PIXELS_1 ;              /* Generate 'ecx' as pixels [4,5,6,7] */
			PLOT_LOW_640_16BIT(4) ;
			LOW_BUILD_PIXELS_2 ;              /* Generate 'ecx' as pixels [8,9,10,11] */
			PLOT_LOW_640_16BIT(8) ;
			LOW_BUILD_PIXELS_3 ;              /* Generate 'ecx' as pixels [0,1,2,3] */
			PLOT_LOW_640_16BIT(0) ;
#else
			/* Plot pixels in 'wrong-order', as ebx is 68000 endian */
			LOW_BUILD_PIXELS_0 ;              /* Generate 'ecx' as pixels [4,5,6,7] */
			PLOT_LOW_640_16BIT(4) ;
			LOW_BUILD_PIXELS_1 ;              /* Generate 'ecx' as pixels [12,13,14,15] */
			PLOT_LOW_640_16BIT(12) ;
			LOW_BUILD_PIXELS_2 ;              /* Generate 'ecx' as pixels [0,1,2,3] */
			PLOT_LOW_640_16BIT(0) ;
			LOW_BUILD_PIXELS_3 ;              /* Generate 'ecx' as pixels [8,9,10,11] */
			PLOT_LOW_640_16BIT(8) ;
#endif

		}

		esi += 16;                      /* Next PC pixels */
		edi += 2;                       /* Next ST pixels */
		ebp += 2;                       /* Next ST copy pixels */
	}
	while (--x);                        /* Loop on X */

}

static void ConvertLowRes_640x16Bit(void)
{
	Uint16 *PCScreen = (Uint16 *)pPCScreenDest;
	Uint32 *edi, *ebp;
	Uint32 *esi;
	Uint32 eax;
	int y;

	Convert_StartFrame();            /* Start frame, track palettes */

	for (y = STScreenStartHorizLine; y < STScreenEndHorizLine; y++)
	{
		/* Get screen addresses */
		eax = STScreenLineOffset[y] + STScreenLeftSkipBytes;  /* Offset for this line + Amount to skip on left hand side */
		edi = (Uint32 *)((Uint8 *)pSTScreen + eax);        /* ST format screen 4-plane 16 colors */
		ebp = (Uint32 *)((Uint8 *)pSTScreenCopy + eax);    /* Previous ST format screen */
		esi = (Uint32 *)PCScreen;                          /* PC format screen */

		if (AdjustLinePaletteRemap(y) & 0x00030000)        /* Change palette table */
			Line_ConvertMediumRes_640x16Bit(edi, ebp, (Uint16 *)esi, eax);
		else
			Line_ConvertLowRes_640x16Bit(edi, ebp, esi, eax);

		PCScreen = Double_ScreenLine16(PCScreen, PCScreenBytesPerLine);
	}
}
