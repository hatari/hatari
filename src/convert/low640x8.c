/*
  Hatari - low640x8.c
 
  This file is distributed under the GNU Public License, version 2 or at your
  option any later version. Read the file gpl.txt for details.

  Screen conversion function, Low Res to 640x8Bit
*/

void ConvertLowRes_640x8Bit(void)
{
  Uint32 *edi, *ebp;
  Uint32 *esi;
  Uint32 eax;

  Convert_StartFrame();           /* Start frame, track palettes */
  ScrY = STScreenStartHorizLine;  /* Starting line in ST screen */

  do      /* y-loop */
  {
    /* Get screen addresses */
    eax = STScreenLineOffset[ScrY] + STScreenLeftSkipBytes;  /* Offset for this line + Amount to skip on left hand side */
    edi = (Uint32 *)((Uint8 *)pSTScreen + eax);       /* ST format screen 4-plane 16 colours */
    ebp = (Uint32 *)((Uint8 *)pSTScreenCopy + eax);   /* Previous ST format screen */
    esi = (Uint32 *)pPCScreenDest;                    /* PC format screen */

    AdjustLinePaletteRemap();

    if((AdjustLinePaletteRemap()&0x00030000) == 0)    /* Change palette table */
      Line_ConvertLowRes_640x8Bit(edi, ebp, esi, eax);
    else
      Line_ConvertMediumRes_640x8Bit(edi, ebp, esi, eax);

    pPCScreenDest = (void *)(((Uint8 *)pPCScreenDest)+PCScreenBytesPerLine*2);  /* Offset to next line */
    ScrY += 1;
  }
  while(ScrY < STScreenEndHorizLine);                 /* Loop on Y */
}


void Line_ConvertLowRes_640x8Bit(Uint32 *edi, Uint32 *ebp, Uint32 *esi, Uint32 eax)
{
  Uint32 edx;
  register Uint32 ebx, ecx, ebpp;

  ScrX = STScreenWidthBytes>>3;         /* Amount to draw across in 16-pixels (8 bytes) */

  do    /* x-loop */
  {
    /* Do 16 pixels at one time */
    ebx = *edi;
    ecx = *(edi+1);

    if((ScrUpdateFlag&0xe0000000) || ebx!=*ebp || ecx!=*(ebp+1))  /* Does differ? */
    { /* copy word */

      bScreenContentsChanged = TRUE;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
      /* Plot in 'right-order' on big endian systems */
      if(!bScrDoubleY)                  /* Double on Y? */
      {
        /* Plot pixels */
        LOW_BUILD_PIXELS_0 ;            /* Generate 'ecx' as pixels [4,5,6,7] */
        PLOT_LOW_640_8BIT(6) ;
        LOW_BUILD_PIXELS_1 ;            /* Generate 'ecx' as pixels [12,13,14,15] */
        PLOT_LOW_640_8BIT(2) ;
        LOW_BUILD_PIXELS_2 ;            /* Generate 'ecx' as pixels [0,1,2,3] */
        PLOT_LOW_640_8BIT(4) ;
        LOW_BUILD_PIXELS_3 ;            /* Generate 'ecx' as pixels [8,9,10,11] */
        PLOT_LOW_640_8BIT(0) ;
      }
      else
      {
        /* Plot pixels */
        LOW_BUILD_PIXELS_0 ;            /* Generate 'ecx' as pixels [4,5,6,7] */
        PLOT_LOW_640_8BIT_DOUBLE_Y(6) ;
        LOW_BUILD_PIXELS_1 ;            /* Generate 'ecx' as pixels [12,13,14,15] */
        PLOT_LOW_640_8BIT_DOUBLE_Y(2) ;
        LOW_BUILD_PIXELS_2 ;            /* Generate 'ecx' as pixels [0,1,2,3] */
        PLOT_LOW_640_8BIT_DOUBLE_Y(4) ;
        LOW_BUILD_PIXELS_3 ;            /* Generate 'ecx' as pixels [8,9,10,11] */
        PLOT_LOW_640_8BIT_DOUBLE_Y(0)
      }
#else
      /* Plot in 'wrong-order', as ebx is 68000 endian */
      if(!bScrDoubleY)                  /* Double on Y? */
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

    esi += 8;                           /* Next PC pixels */
    edi += 2;                           /* Next ST pixels */
    ebp += 2;                           /* Next ST copy pixels */
  }
  while(--ScrX);                        /* Loop on X */
}

