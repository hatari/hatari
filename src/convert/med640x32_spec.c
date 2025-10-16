/*
  Hatari - med640x32_spec.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Screen Conversion, Medium Res Spec512 to 640x32Bit
*/

static void ConvertMediumRes_640x32Bit_Spec(void)
{
	uint32_t *PCScreen = (uint32_t *)pPCScreenDest;
	uint32_t *edi, *ebp;
	uint32_t *esi;
	uint32_t eax;
	int y;

	Spec512_StartFrame();            /* Start frame, track palettes */

	for (y = STScreenStartHorizLine; y < STScreenEndHorizLine; y++)
	{
		eax = STScreenLineOffset[y] + STScreenLeftSkipBytes;  /* Offset for this line + Amount to skip on left hand side */
		edi = (uint32_t *)((uint8_t *)pSTScreen + eax);       /* ST format screen 4-plane 16 colors */
		ebp = (uint32_t *)((uint8_t *)pSTScreenCopy + eax);   /* Previous ST format screen */
		esi = PCScreen;                                    /* PC format screen */

		if (HBLPaletteMasks[y] & 0x00030000)               /* Test resolution */
			Line_ConvertMediumRes_640x32Bit_Spec(edi, ebp, esi, eax);	/* med res line */
		else
			Line_ConvertLowRes_640x32Bit_Spec(edi, ebp, esi, eax);		/* low res line (double on X) */

		PCScreen = Double_ScreenLine32(PCScreen, PCScreenBytesPerLine);
	}

        bScreenContentsChanged = true;
}


static void Line_ConvertMediumRes_640x32Bit_Spec(uint32_t *edi, uint32_t *ebp, uint32_t *esi, uint32_t eax)
{
	int x;
	uint32_t ebx, ecx;
	uint32_t pixelspace[5]; /* Workspace to store pixels to so can print in right order for Spec512 */

	/* on x86, unaligned access macro touches also
	 * next byte, zero it for code checkers
	 */
	pixelspace[4] = 0;

	Spec512_StartScanLine();        /* Build up palettes for every 4 pixels, store in 'ScanLinePalettes' */

	x = STScreenWidthBytes >> 2;   /* Amount to draw across in 16-pixels (4 bytes) */

	do  /* x-loop */
	{
		/* Do 16 pixels at one time */
		ebx = *edi;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		/* Plot in 'right-order' on big endian systems */
		MED_BUILD_PIXELS_0 ;              /* Generate 'ecx' as pixels [12,13,14,15] */
		pixelspace[3] = ecx;
		MED_BUILD_PIXELS_1 ;              /* Generate 'ecx' as pixels [4,5,6,7] */
		pixelspace[1] = ecx;
		MED_BUILD_PIXELS_2 ;              /* Generate 'ecx' as pixels [8,9,10,11] */
		pixelspace[2] = ecx;
		MED_BUILD_PIXELS_3 ;              /* Generate 'ecx' as pixels [0,1,2,3] */
		pixelspace[0] = ecx;
#else
		/* Plot in 'wrong-order', as ebx is 68000 endian */
		MED_BUILD_PIXELS_0 ;              /* Generate 'ecx' as pixels [4,5,6,7] */
		pixelspace[1] = ecx;
		MED_BUILD_PIXELS_1 ;              /* Generate 'ecx' as pixels [12,13,14,15] */
		pixelspace[3] = ecx;
		MED_BUILD_PIXELS_2 ;              /* Generate 'ecx' as pixels [0,1,2,3] */
		pixelspace[0] = ecx;
		MED_BUILD_PIXELS_3 ;              /* Generate 'ecx' as pixels [8,9,10,11] */
		pixelspace[2] = ecx;
#endif
		/* And plot, the Spec512 is offset by 1 pixel and works on 'chunks' of 4 pixels */
		/* So, we plot 1_4_4_4_3 to give 16 pixels, changing palette between */
		/* (last one is used for first of next 16-pixels) */
		/* NOTE : In med res, we display 16 pixels in 8 cycles, so palette should be */
		/* updated every 8 pixels, not every 4 pixels (as in low res) */
		ecx = pixelspace[0];
		PLOT_SPEC512_LEFT_MED_640_32BIT(0);
//		Spec512_UpdatePaletteSpan();

		ecx = GET_SPEC512_OFFSET_PIXELS(pixelspace, 1);
		PLOT_SPEC512_MID_MED_640_32BIT(1);
		Spec512_UpdatePaletteSpan();

		ecx = GET_SPEC512_OFFSET_PIXELS(pixelspace, 5);
		PLOT_SPEC512_MID_MED_640_32BIT(5);
//		Spec512_UpdatePaletteSpan();

		ecx = GET_SPEC512_OFFSET_PIXELS(pixelspace, 9);
		PLOT_SPEC512_MID_MED_640_32BIT(9);
		Spec512_UpdatePaletteSpan();

		ecx = GET_SPEC512_OFFSET_FINAL_PIXELS(pixelspace);
		PLOT_SPEC512_END_MED_640_32BIT(13);

		esi += 16;                      /* Next PC pixels */
		edi += 1;                       /* Next ST pixels */
		ebp += 1;                       /* Next ST copy pixels */
	}
	while (--x);                        /* Loop on X */

	Spec512_EndScanLine();
}
