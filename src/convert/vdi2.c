// Screen Conversion, VDI Res to 2Colour(1-Bit and 8-Bit)

void ConvertVDIRes_2Colour_1Bit(void)
{
fprintf(stderr,"FIXME: Screen Conversion, VDI Res to 2Colour(1-Bit and 8-Bit)\n");
  // Copy palette to bitmap (2 colours)
/* FIXME */
/*
  if (HBLPalettes[0]==0x777) {
    ScreenBMP.Colours[0].rgbRed = ScreenBMP.Colours[0].rgbGreen = ScreenBMP.Colours[0].rgbBlue = 0xff;
    ScreenBMP.Colours[1].rgbRed = ScreenBMP.Colours[1].rgbGreen = ScreenBMP.Colours[1].rgbBlue = 0x00;
  }
  else {
    ScreenBMP.Colours[0].rgbRed = ScreenBMP.Colours[0].rgbGreen = ScreenBMP.Colours[0].rgbBlue = 0x00;
    ScreenBMP.Colours[1].rgbRed = ScreenBMP.Colours[1].rgbGreen = ScreenBMP.Colours[1].rgbBlue = 0xff;
  }
*/
  // Simply copy ST screen, as same format!
  memcpy(pPCScreenDest,pSTScreen,(VDIWidth/8)*VDIHeight);

  bScreenContentsChanged = TRUE;
}

void ConvertVDIRes_2Colour_YLoop(void);
void Line_ConvertVDIRes_2Colour(void);

void ConvertVDIRes_2Colour(void)
{
fprintf(stderr,"FIXME: Screen Conversion, VDI Res to 2Colour(1-Bit and 8-Bit)\n");/* FIXME */
/*
  __asm {
    push  ebp
    push  edi
    push  esi
    push  ebx

    xor    edx,edx                  // Clear index for loop

    mov    eax,0
    mov    [ScrY],eax                // Starting line in ST screen

    // Get screen addresses, 'edi'-ST screen, 'ebp'-Previous ST screen, 'esi'-PC screen
    mov    edi,[pSTScreen]              // ST format screen 4-plane 16 colours
    mov    ebp,[pSTScreenCopy]            // Previous ST format screen
    mov    esi,[pPCScreenDest]            // PC format screen, byte per pixel 256 colours

    jmp    ConvertVDIRes_2Colour_YLoop
  }
*/
}
/*
NAKED void ConvertVDIRes_2Colour_YLoop(void)
{
  __asm {
    // Get screen addresses, 'edi'-ST screen, 'ebp'-Previous ST screen, 'esi'-PC screen
    mov    esi,[pPCScreenDest]            // PC format screen, byte per pixel 256 colours

    jmp    Line_ConvertVDIRes_2Colour      // 0 palette same, can do check tests
  }
}
NAKED void Line_ConvertVDIRes_2Colour(void)
{
  __asm {
    mov    eax,[VDIWidth]              // Amount to draw across
    shr    eax,4                  // in 16-pixels(4 bytes)
    mov    [ScrX],eax
x_loop:
    // Do 16 pixels at one time
    mov    bx,[edi]
    // Full update? or just test changes?
    test  [ScrUpdateFlag],0xe0000000
    jne    copy_word                // Force
    // Does differ?
    cmp    bx,[ebp]
    je    next_word                // Pixels are same as last frame, so ignore

copy_word:
    mov    [bScreenContentsChanged],TRUE

    // Plot in 'wrong-order', as ebx is 68000 endian
    HIGH_BUILD_PIXELS_0                // Generate 'ecx' as pixels [4,5,6,7]
    PLOT_HIGH_640_8BIT(4)
    HIGH_BUILD_PIXELS_1                // Generate 'ecx' as pixels [0,1,2,3]
    PLOT_HIGH_640_8BIT(0)
    HIGH_BUILD_PIXELS_2                // Generate 'ecx' as pixels [12,13,14,15]
    PLOT_HIGH_640_8BIT(12)
    HIGH_BUILD_PIXELS_3                // Generate 'ecx' as pixels [8,9,10,11]
    PLOT_HIGH_640_8BIT(8)

next_word:
    add    esi,16                  // Next PC pixels
    add    edi,2                  // Next ST pixels
    add    ebp,2                  // Next ST copy pixels

    dec    [ScrX]
    jne    x_loop                  // Loop on X

    mov    eax,[pPCScreenDest]
    add    eax,[PCScreenBytesPerLine]        // Offset to next line
    mov    [pPCScreenDest],eax
    
    inc    [ScrY]
    mov    eax,[VDIHeight]
    cmp    [ScrY],eax
    jne    ConvertVDIRes_2Colour_YLoop      // And on Y

    pop    ebx
    pop    esi
    pop    edi
    pop    ebp

    ret
  }
}
*/
