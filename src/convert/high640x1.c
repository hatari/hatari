// Screen Conversion, High Res to 640x1Bit

void ConvertHighRes_640x1Bit(void)
{
fprintf(stderr,"Screen Conversion, High Res to 640x1Bit\n");
  // Copy palette to bitmap (2 colours)
/* FIXME */
/*  if (HBLPalettes[0]==0x777) {
    ScreenBMP.Colours[0].rgbRed = ScreenBMP.Colours[0].rgbGreen = ScreenBMP.Colours[0].rgbBlue = 0xff;
    ScreenBMP.Colours[1].rgbRed = ScreenBMP.Colours[1].rgbGreen = ScreenBMP.Colours[1].rgbBlue = 0x00;
  }
  else {
    ScreenBMP.Colours[0].rgbRed = ScreenBMP.Colours[0].rgbGreen = ScreenBMP.Colours[0].rgbBlue = 0x00;
    ScreenBMP.Colours[1].rgbRed = ScreenBMP.Colours[1].rgbGreen = ScreenBMP.Colours[1].rgbBlue = 0xff;
  }
*/
  // Simply copy ST screen, as same format!
  memcpy(pPCScreenDest,pSTScreen,(640/8)*400);

  bScreenContentsChanged = TRUE;
}
