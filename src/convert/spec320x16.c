// Screen Conversion, Spec512 to 320x16Bit

void ConvertSpec512_320x16Bit_YLoop(void);
void Line_ConvertSpec512_320x16Bit(void);

void ConvertSpec512_320x16Bit(void)
{
fprintf(stderr,"FIXME: Screen Conversion, Spec512 to 320x16Bit\n");
/* FIXME */
/*
  __asm {
    push  ebp
    push  edi
    push  esi
    push  ebx

    call  Spec512_StartFrame            // Start frame, track palettes

    xor    edx,edx                  // Clear index for loop

    mov    eax,[STScreenStartHorizLine]
    mov    [ScrY],eax                // Starting line in ST screen
    jmp    ConvertSpec512_320x16Bit_YLoop
  }
*/
}
/*
NAKED void ConvertSpec512_320x16Bit_YLoop(void)
{
  __asm {
    call  Spec512_StartScanLine          // Build up palettes for every 4 pixels, store in 'ScanLinePalettes'
    xor    edx,edx                  // Clear index for loop

    // Get screen addresses, 'edi'-ST screen, 'ebp'-Previous ST screen, 'esi'-PC screen
    mov    eax,[ScrY]
    mov    eax,STScreenLineOffset[eax*4]      // Offset for this line
    add    eax,[STScreenLeftSkipBytes]        // Amount to skip on left hand side
    mov    edi,[pSTScreen]              // ST format screen 4-plane 16 colours
    add    edi,eax
    mov    ebp,[pSTScreenCopy]            // Previous ST format screen
    add    ebp,eax
    mov    esi,[pPCScreenDest]            // PC format screen, byte per pixel 256 colours

    jmp    Line_ConvertSpec512_320x16Bit
  }
}

NAKED void Line_ConvertSpec512_320x16Bit(void)
{
  __asm {
    mov    eax,[STScreenWidthBytes]        // Amount to draw across
    shr    eax,3                  // in 16-pixels(8 bytes)
    mov    [ScrX],eax
x_loop:
    // Do 16 pixels at one time
    mov    ebx,[edi]
    mov    ecx,4[edi]

    // Convert planes to byte indices - as works in wrong order store to workspace so can read back in order!
    LOW_BUILD_PIXELS_0                // Generate 'ecx' as pixels [4,5,6,7]
    mov    [PixelWorkspace+4],ecx
    LOW_BUILD_PIXELS_1                // Generate 'ecx' as pixels [12,13,14,15]
    mov    [PixelWorkspace+12],ecx
    LOW_BUILD_PIXELS_2                // Generate 'ecx' as pixels [0,1,2,3]
    mov    [PixelWorkspace],ecx
    LOW_BUILD_PIXELS_3                // Generate 'ecx' as pixels [8,9,10,11]
    mov    [PixelWorkspace+8],ecx

    // And plot, the Spec512 is offset by 1 pixel and works on 'chunks' of 4 pixels
    // So, we plot 1_4_4_3 to give 16 pixels, changing palette between(last one is used for first of next 16-pixels)
    mov    ecx,[PixelWorkspace]
    PLOT_SPEC512_LEFT_LOW_320_16BIT(0)
    call  Spec512_UpdatePaletteSpan
    mov    ecx,[PixelWorkspace+1]
    PLOT_SPEC512_MID_320_16BIT(2)
    call  Spec512_UpdatePaletteSpan
    mov    ecx,[PixelWorkspace+5]
    PLOT_SPEC512_MID_320_16BIT(10)
    call  Spec512_UpdatePaletteSpan
    mov    ecx,[PixelWorkspace+9]
    PLOT_SPEC512_MID_320_16BIT(18)
    call  Spec512_UpdatePaletteSpan
    mov    ecx,[PixelWorkspace+13]
    PLOT_SPEC512_END_LOW_320_16BIT(26)

    add    esi,16*2                // Next PC pixels
    add    edi,8                  // Next ST pixels
    add    ebp,8                  // Next ST copy pixels

    dec    [ScrX]
    jne    x_loop                  // Loop on X

    call  Spec512_EndScanLine

    mov    eax,[pPCScreenDest]
    add    eax,[PCScreenBytesPerLine]        // Offset to next line
    mov    [pPCScreenDest],eax
    
    inc    [ScrY]
    mov    eax,[STScreenEndHorizLine]
    cmp    [ScrY],eax
    jne    ConvertSpec512_320x16Bit_YLoop      // And on Y

    mov    [bScreenContentsChanged],TRUE

    pop    ebx
    pop    esi
    pop    edi
    pop    ebp

    ret
  }
}
*/
