/*
  Hatari - high640x8.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Screen Conversion, High Res to 640x8Bit
*/


static void ConvertHighRes_640x8Bit(void)
{
	Uint16 *edi, *ebp;
	Uint32 *esi;
	Uint16 eax, ebx;
	int y, x, update;

	edi = (Uint16 *)pSTScreen;        /* ST format screen */
	ebp = (Uint16 *)pSTScreenCopy;    /* Previous ST format screen */
	esi = (Uint32 *)pPCScreenDest;    /* PC format screen */

	/* NOTE 'ScrUpdateFlag' is already set (to full update or check, no palettes) */
	update = ScrUpdateFlag & PALETTEMASK_UPDATEMASK;

	for (y = STScreenStartHorizLine; y < STScreenEndHorizLine; y++)
	{

		for (x = 0; x < 40; x++)
		{

			/* Do 16 pixels at one time */
			ebx = *edi;

			if (update || ebx != *ebp)  /* Does differ? */
			{
				bScreenContentsChanged = true;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
				/* Plot in 'right-order' on big endian systems */
				HIGH_BUILD_PIXELS_0 ;               /* Generate pixels [12,13,14,15] */
				PLOT_HIGH_640_8BIT(3) ;
				HIGH_BUILD_PIXELS_1 ;               /* Generate pixels [8,9,10,11] */
				PLOT_HIGH_640_8BIT(2) ;
				HIGH_BUILD_PIXELS_2 ;               /* Generate pixels [4,5,6,7] */
				PLOT_HIGH_640_8BIT(1) ;
				HIGH_BUILD_PIXELS_3 ;               /* Generate pixels [0,1,2,3] */
				PLOT_HIGH_640_8BIT(0) ;
#else
				/* Plot in 'wrong-order', as ebx is 68000 endian */
				HIGH_BUILD_PIXELS_0 ;               /* Generate pixels [4,5,6,7] */
				PLOT_HIGH_640_8BIT(1) ;
				HIGH_BUILD_PIXELS_1 ;               /* Generate pixels [0,1,2,3] */
				PLOT_HIGH_640_8BIT(0) ;
				HIGH_BUILD_PIXELS_2 ;               /* Generate pixels [12,13,14,15] */
				PLOT_HIGH_640_8BIT(3) ;
				HIGH_BUILD_PIXELS_3 ;               /* Generate pixels [8,9,10,11] */
				PLOT_HIGH_640_8BIT(2) ;
#endif
			}

			esi += 4;                               /* Next PC pixels */
			edi += 1;                               /* Next ST pixels */
			ebp += 1;                               /* Next ST copy pixels */
		}

		esi += PCScreenBytesPerLine/4 - 40*4;           /* advance to start of next line */
	}
}
