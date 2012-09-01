/*
  Hatari - low640x8.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Screen conversion function, Low Res to 640x8Bit
*/

static void Line_ConvertLowRes_640x8Bit(Uint32 *edi, Uint32 *ebp, Uint32 *esi, Uint32 eax)
{
	Uint32 edx;
	Uint32 ebx, ecx, ebpp;
	int x, update, Screen4BytesPerLine;

	x = STScreenWidthBytes>>3; /* Amount to draw across in 16-pixels (8 bytes) */
	Screen4BytesPerLine = PCScreenBytesPerLine/4;
	update = ScrUpdateFlag & PALETTEMASK_UPDATEMASK;

	do  /* x-loop */
	{
		/* Do 16 pixels at one time */
		ebx = *edi;
		ecx = *(edi+1);

		if (update || ebx!=*ebp || ecx!=*(ebp+1))   /* Does differ? */
		{
			/* copy word */

			bScreenContentsChanged = true;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
			/* Plot in 'right-order' on big endian systems */
			if (!bScrDoubleY)                   /* Double on Y? */
			{
				/* Plot pixels */
				LOW_BUILD_PIXELS_0 ;            /* Generate 'ecx' as pixels [12,13,14,15] */
				PLOT_LOW_640_8BIT(6) ;
				LOW_BUILD_PIXELS_1 ;            /* Generate 'ecx' as pixels [4,5,6,7] */
				PLOT_LOW_640_8BIT(2) ;
				LOW_BUILD_PIXELS_2 ;            /* Generate 'ecx' as pixels [8,9,10,11] */
				PLOT_LOW_640_8BIT(4) ;
				LOW_BUILD_PIXELS_3 ;            /* Generate 'ecx' as pixels [0,1,2,3]] */
				PLOT_LOW_640_8BIT(0) ;
			}
			else
			{
				/* Plot pixels */
				LOW_BUILD_PIXELS_0 ;            /* Generate 'ecx' as pixels [12,13,14,15] */
				PLOT_LOW_640_8BIT_DOUBLE_Y(6) ;
				LOW_BUILD_PIXELS_1 ;            /* Generate 'ecx' as pixels [4,5,6,7] */
				PLOT_LOW_640_8BIT_DOUBLE_Y(2) ;
				LOW_BUILD_PIXELS_2 ;            /* Generate 'ecx' as pixels [8,9,10,11] */
				PLOT_LOW_640_8BIT_DOUBLE_Y(4) ;
				LOW_BUILD_PIXELS_3 ;            /* Generate 'ecx' as pixels [0,1,2,3]] */
				PLOT_LOW_640_8BIT_DOUBLE_Y(0)
			}
#else
			/* Plot in 'wrong-order', as ebx is 68000 endian */
			if (!bScrDoubleY)                   /* Double on Y? */
			{
				/* Plot pixels */
				LOW_BUILD_PIXELS_0 ;            /* Generate 'ecx' as pixels [4,5,6,7] */
				PLOT_LOW_640_8BIT(2) ;
				LOW_BUILD_PIXELS_1 ;            /* Generate 'ecx' as pixels [12,13,14,15] */
				PLOT_LOW_640_8BIT(6) ;
				LOW_BUILD_PIXELS_2 ;            /* Generate 'ecx' as pixels [0,1,2,3] */
				PLOT_LOW_640_8BIT(0) ;
				LOW_BUILD_PIXELS_3 ;            /* Generate 'ecx' as pixels [8,9,10,11] */
				PLOT_LOW_640_8BIT(4) ;
			}
			else
			{
				/* Plot pixels */
				LOW_BUILD_PIXELS_0 ;            /* Generate 'ecx' as pixels [4,5,6,7] */
				PLOT_LOW_640_8BIT_DOUBLE_Y(2) ;
				LOW_BUILD_PIXELS_1 ;            /* Generate 'ecx' as pixels [12,13,14,15] */
				PLOT_LOW_640_8BIT_DOUBLE_Y(6) ;
				LOW_BUILD_PIXELS_2 ;            /* Generate 'ecx' as pixels [0,1,2,3] */
				PLOT_LOW_640_8BIT_DOUBLE_Y(0) ;
				LOW_BUILD_PIXELS_3 ;            /* Generate 'ecx' as pixels [8,9,10,11] */
				PLOT_LOW_640_8BIT_DOUBLE_Y(4)
			}
#endif

		}

		esi += 8;                       /* Next PC pixels */
		edi += 2;                       /* Next ST pixels */
		ebp += 2;                       /* Next ST copy pixels */
	}
	while (--x);                        /* Loop on X */
}


static void ConvertLowRes_640x8Bit(void)
{
	Uint32 *edi, *ebp;
	Uint32 *esi;
	Uint32 eax;
	int y;

	Convert_StartFrame();           /* Start frame, track palettes */

	for (y = STScreenStartHorizLine; y < STScreenEndHorizLine; y++)
	{

		/* Get screen addresses */
		eax = STScreenLineOffset[y] + STScreenLeftSkipBytes;  /* Offset for this line + Amount to skip on left hand side */
		edi = (Uint32 *)((Uint8 *)pSTScreen + eax);       /* ST format screen 4-plane 16 colors */
		ebp = (Uint32 *)((Uint8 *)pSTScreenCopy + eax);   /* Previous ST format screen */
		esi = (Uint32 *)pPCScreenDest;                    /* PC format screen */

		if (AdjustLinePaletteRemap(y) & 0x00030000)       /* Change palette table */
			Line_ConvertMediumRes_640x8Bit(edi, ebp, esi, eax);
		else
			Line_ConvertLowRes_640x8Bit(edi, ebp, esi, eax);

		pPCScreenDest = (((Uint8 *)pPCScreenDest)+PCScreenBytesPerLine*2);  /* Offset to next line */
	}
}
