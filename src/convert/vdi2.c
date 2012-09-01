/*
  Hatari - vdi2.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Screen Conversion, monochrome VDI resolution
*/


static void ConvertVDIRes_2Colour(void)
{
	Uint16 *edi, *ebp;
	Uint32 *esi;
	Uint16 eax, ebx;
	int y, x, update;

	edi = (Uint16 *)pSTScreen;            /* ST format screen */
	ebp = (Uint16 *)pSTScreenCopy;        /* Previous ST format screen */
	update = ScrUpdateFlag & PALETTEMASK_UPDATEMASK;

	for (y = 0; y < VDIHeight; y++)
	{

		esi = (Uint32 *)pPCScreenDest;  /* PC format screen, byte per pixel 256 colors */

		x = VDIWidth >> 4;              /* Amount to draw across in 16-pixels (4 bytes) */

		do  /* x-loop */
		{
			/* Do 16 pixels at one time */
			ebx = *edi;

			if (update || ebx != *ebp)  /* Does differ? */
			{
				bScreenContentsChanged = true;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
				/* Plot in 'right-order' on big endian systems */
				HIGH_BUILD_PIXELS_0 ;   /* Generate pixels [12,13,14,15] */
				PLOT_HIGH_640_8BIT(3) ;
				HIGH_BUILD_PIXELS_1 ;   /* Generate pixels [8,9,10,11] */
				PLOT_HIGH_640_8BIT(2) ;
				HIGH_BUILD_PIXELS_2 ;   /* Generate pixels [4,5,6,7] */
				PLOT_HIGH_640_8BIT(1) ;
				HIGH_BUILD_PIXELS_3 ;   /* Generate pixels [0,1,2,3] */
				PLOT_HIGH_640_8BIT(0) ;
#else
				/* Plot in 'wrong-order', as ebx is 68000 endian */
				HIGH_BUILD_PIXELS_0 ;   /* Generate pixels [4,5,6,7] */
				PLOT_HIGH_640_8BIT(1) ;
				HIGH_BUILD_PIXELS_1 ;   /* Generate pixels [0,1,2,3] */
				PLOT_HIGH_640_8BIT(0) ;
				HIGH_BUILD_PIXELS_2 ;   /* Generate pixels [12,13,14,15] */
				PLOT_HIGH_640_8BIT(3) ;
				HIGH_BUILD_PIXELS_3 ;   /* Generate pixels [8,9,10,11] */
				PLOT_HIGH_640_8BIT(2) ;
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
