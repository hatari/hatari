// Screen Conversion, High Res to 640x8Bit

void ConvertHighRes_640x8Bit_YLoop(void);
void Line_ConvertHighRes_640x8Bit(void);

void ConvertHighRes_640x8Bit(void)
{
//fprintf(stderr,"FIXME: Screen Conversion, High Res to 640x8Bit\n");
 unsigned char *src=pSTScreen;
 unsigned char *dst=pPCScreenDest;
 int i;
 for(i=0; i<640*400/8; i++, src++)
  {
   if( *src & 128 ) *dst++ = 0;  else  *dst++ = 1;
   if( *src & 64 )  *dst++ = 0;  else  *dst++ = 1;
   if( *src & 32 )  *dst++ = 0;  else  *dst++ = 1;
   if( *src & 16 )  *dst++ = 0;  else  *dst++ = 1;
   if( *src & 8 )   *dst++ = 0;  else  *dst++ = 1;
   if( *src & 4 )   *dst++ = 0;  else  *dst++ = 1;
   if( *src & 2 )   *dst++ = 0;  else  *dst++ = 1;
   if( *src & 1 )   *dst++ = 0;  else  *dst++ = 1;
  }
 bScreenContentsChanged = TRUE;
/* FIXME */
/*
  __asm {
    push  ebp
    push  edi
    push  esi
    push  ebx

    mov    edi,[pSTScreen]      // ST format screen 4-plane 16 colours
    mov    ebp,[pSTScreenCopy]    // Previous ST format screen
    mov    esi,[pPCScreenDest]    // PC format screen, byte per pixel 256 colours
    xor    edx,edx        // Clear index for loop

    mov    eax,[STScreenStartHorizLine]
    mov    [ScrY],eax      // 200 lines
    jmp    ConvertHighRes_640x8Bit_YLoop
  }
*/
}

/*
NAKED void ConvertHighRes_640x8Bit_YLoop(void)
{
  __asm {
    // NOTE 'ScrUpdateFlag' is already set(to full update or check, no palettes)
    jmp    Line_ConvertHighRes_640x8Bit      // 0 palette same, can do check tests
  }
}
NAKED void Line_ConvertHighRes_640x8Bit(void)
{
  __asm {
    mov    [ScrX],40
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

    sub    esi,(40*16)                // Back to start of line
    add    esi,[PCScreenBytesPerLine]        // Offset to next line

    inc    [ScrY]
    mov    eax,[STScreenEndHorizLine]
    cmp    [ScrY],eax
    jne    ConvertHighRes_640x8Bit_YLoop      // And on Y

    pop    ebx
    pop    esi
    pop    edi
    pop    ebp

    ret
  }
}
*/
