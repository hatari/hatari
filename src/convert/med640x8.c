// Screen Conversion, Medium Res to 640x8Bit

void ConvertMediumRes_640x8Bit_YLoop(void);
void Line_ConvertMediumRes_640x8Bit(void);

void ConvertMediumRes_640x8Bit(void)
{
fprintf(stderr,"FIXME: Screen Conversion, Medium Res to 640x8Bit\n");
/* FIXME */
/*
  __asm {
    push  ebp
    push  edi
    push  esi
    push  ebx

    xor    edx,edx                  // Clear index for loop

    call  Convert_StartFrame            // Start frame, track palettes
    mov    eax,[STScreenStartHorizLine]
    mov    [ScrY],eax                // Starting line in ST screen
    jmp    ConvertMediumRes_640x8Bit_YLoop
  }
*/
}
/*
NAKED void ConvertMediumRes_640x8Bit_YLoop(void)
{
  __asm {
    // Get screen addresses, 'edi'-ST screen, 'ebp'-Previous ST screen, 'esi'-PC screen
    mov    eax,[ScrY]
    mov    eax,STScreenLineOffset[eax*4]      // Offset for this line
    add    eax,[STScreenLeftSkipBytes]        // Amount to skip on left hand side
    mov    edi,[pSTScreen]              // ST format screen 4-plane 16 colours
    add    edi,eax
    mov    ebp,[pSTScreenCopy]            // Previous ST format screen
    add    ebp,eax
    mov    esi,[pPCScreenDest]            // PC format screen, byte per pixel 256 colours

    call  AdjustLinePaletteRemap          // Change palette table, DO NOT corrupt edx,edi,esi or ebp!
    test  eax,0x00030000
    je    Line_ConvertLowRes_640x8Bit        // resolution change(MUST be first)
    jmp    Line_ConvertMediumRes_640x8Bit      // 0 palette same, can do check tests
  }
}
NAKED void Line_ConvertMediumRes_640x8Bit(void)
{
  __asm {
    mov    eax,[STScreenWidthBytes]        // Amount to draw across
    shr    eax,2                  // in 16-pixels(4 bytes)
    mov    [ScrX],eax
x_loop:
#ifdef TEST_SCREEN_UPDATE
    mov    BYTE PTR [esi+0],0
    mov    BYTE PTR [esi+2],0
    mov    BYTE PTR [esi+4],0
    mov    BYTE PTR [esi+6],0
    mov    BYTE PTR [esi+8],0
    mov    BYTE PTR [esi+10],0
    mov    BYTE PTR [esi+12],0
    mov    BYTE PTR [esi+14],0
#endif  //TEST_SCREEN_UPDATE

    // Do 16 pixels at one time
    mov    ebx,[edi]
    // Full update? or just test changes?
    test  [ScrUpdateFlag],0xe0000000
    jne    copy_word                // Force
    // Does differ?
    cmp    ebx,[ebp]
    je    next_word                // Pixels are same as last frame, so ignore

copy_word:
    mov    [bScreenContentsChanged],TRUE

    // Plot in 'wrong-order', as ebx is 68000 endian
    push  ebp
    cmp    [bScrDoubleY],TRUE            // Double on Y?
    je    double_y

    MED_BUILD_PIXELS_0                // Generate 'ecx' as pixels [4,5,6,7]
    PLOT_MED_640_8BIT(4)
    MED_BUILD_PIXELS_1                // Generate 'ecx' as pixels [12,13,14,15]
    PLOT_MED_640_8BIT(12)
    MED_BUILD_PIXELS_2                // Generate 'ecx' as pixels [0,1,2,3]
    PLOT_MED_640_8BIT(0)
    MED_BUILD_PIXELS_3                // Generate 'ecx' as pixels [8,9,10,11]
    PLOT_MED_640_8BIT(8)

    jmp    done_word

double_y:
    push  ebp
    mov    ebp,[PCScreenBytesPerLine]
    MED_BUILD_PIXELS_0                // Generate 'ecx' as pixels [4,5,6,7]
    PLOT_MED_640_8BIT_DOUBLE_Y(4)
    MED_BUILD_PIXELS_1                // Generate 'ecx' as pixels [12,13,14,15]
    PLOT_MED_640_8BIT_DOUBLE_Y(12)
    MED_BUILD_PIXELS_2                // Generate 'ecx' as pixels [0,1,2,3]
    PLOT_MED_640_8BIT_DOUBLE_Y(0)
    MED_BUILD_PIXELS_3                // Generate 'ecx' as pixels [8,9,10,11]
    PLOT_MED_640_8BIT_DOUBLE_Y(8)
    pop    ebp
done_word:

    pop    ebp

next_word:
    add    esi,16                  // Next PC pixels
    add    edi,4                  // Next ST pixels
    add    ebp,4                  // Next ST copy pixels

    dec    [ScrX]
    jne    x_loop                  // Loop on X

    mov    eax,[pPCScreenDest]
    add    eax,[PCScreenBytesPerLine]        // Offset to next line
    add    eax,[PCScreenBytesPerLine]
    mov    [pPCScreenDest],eax

    inc    [ScrY]
    mov    eax,[STScreenEndHorizLine]
    cmp    [ScrY],eax
    jne    ConvertMediumRes_640x8Bit_YLoop      // And on Y

    pop    ebx
    pop    esi
    pop    edi
    pop    ebp

    ret
  }
}
*/
