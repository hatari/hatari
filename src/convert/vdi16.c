// Screen Conversion, VDI Res to 16Colour

void ConvertVDIRes_16Colour_YLoop(void);
void Line_ConvertVDIRes_16Colour(void);

void ConvertVDIRes_16Colour(void)
{
fprintf(stderr,"FIXME: Screen Conversion, VDI Res to 16Colour\n");
/* FIXME */
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

    jmp    ConvertVDIRes_16Colour_YLoop
  }
*/
}
/*
NAKED void ConvertVDIRes_16Colour_YLoop(void)
{
  __asm {
    // Get screen addresses, 'edi'-ST screen, 'ebp'-Previous ST screen, 'esi'-PC screen
    mov    esi,[pPCScreenDest]            // PC format screen, byte per pixel 256 colours

    jmp    Line_ConvertVDIRes_16Colour
  }
}
NAKED void Line_ConvertVDIRes_16Colour(void)
{
  __asm {
    mov    eax,[VDIWidth]              // Amount to draw across
    shr    eax,4                  // in 16-pixels(8 bytes)
    mov    [ScrX],eax
x_loop:
#ifdef TEST_SCREEN_UPDATE
    mov    WORD PTR [esi+0],0
    mov    WORD PTR [esi+4],0
    mov    WORD PTR [esi+8],0
    mov    WORD PTR [esi+12],0
    mov    WORD PTR [esi+16],0
    mov    WORD PTR [esi+20],0
    mov    WORD PTR [esi+24],0
    mov    WORD PTR [esi+28],0
#endif  //TEST_SCREEN_UPDATE

    // Do 16 pixels at one time
    mov    ebx,[edi]
    mov    ecx,4[edi]
    // Full update? or just test changes?
    test  [ScrUpdateFlag],0xe0000000
    jne    copy_word                // Force
    // Does differ?
    cmp    ebx,[ebp]
    jne    copy_word
    cmp    ecx,4[ebp]
    je    next_word                // Pixels are same as last frame, so ignore

copy_word:
    mov    [bScreenContentsChanged],TRUE

    // Plot pixels
    LOW_BUILD_PIXELS_0                // Generate 'ecx' as pixels [4,5,6,7]
    PLOT_LOW_320_8BIT(4)
    LOW_BUILD_PIXELS_1                // Generate 'ecx' as pixels [12,13,14,15]
    PLOT_LOW_320_8BIT(12)
    LOW_BUILD_PIXELS_2                // Generate 'ecx' as pixels [0,1,2,3]
    PLOT_LOW_320_8BIT(0)
    LOW_BUILD_PIXELS_3                // Generate 'ecx' as pixels [8,9,10,11]
    PLOT_LOW_320_8BIT(8)

next_word:
    add    esi,16                  // Next PC pixels
    add    edi,8                  // Next ST pixels
    add    ebp,8                  // Next ST copy pixels

    dec    [ScrX]
    jne    x_loop                  // Loop on X

    mov    eax,[pPCScreenDest]
    add    eax,[PCScreenBytesPerLine]        // Offset to next line
    mov    [pPCScreenDest],eax
    
    inc    [ScrY]
    mov    eax,[VDIHeight]
    cmp    [ScrY],eax
    jne    ConvertVDIRes_16Colour_YLoop    // And on Y

    pop    ebx
    pop    esi
    pop    edi
    pop    ebp

    ret
  }
}
*/
