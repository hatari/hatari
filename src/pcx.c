/*
  Hatari

  PCX Screen Shot File Output
*/

#include "main.h"
#include "file.h"
#include "memAlloc.h"
#include "misc.h"
#include "screen.h"

// PCX File Header
typedef struct {
  char Manufact;
  char Version;
  char Encoded;
  char BitsPerPixel;
  short int XMin,YMin;
  short int XMax,YMax;
  short int HRes,VRes;
  char Colours[16*3];
  char Reserved;
  char NumPlanes;
  short int BytesPerLine;
  short int PaletteInfo;
  char Filler[58];
} PCXHEADER;

//-----------------------------------------------------------------------
/*
  Compress PCX planes using RLE compression
*/
unsigned char *PCX_CompressPlanes(unsigned char *pSingleRGBLine, unsigned char *pCompressedData, int nBytesPerLine)
{
  unsigned char *pRGBLine;
  BOOL bDoingRun;
  int nPixels=0,nRunLength=0;
  unsigned char Pixel;
  int x;

  // Compress planes
  pRGBLine = (unsigned char *)pSingleRGBLine;
  bDoingRun = FALSE;
  for(x=0; x<nBytesPerLine; x++) {
    Pixel = *pRGBLine++;
    if (bDoingRun) {
      if ( (Pixel==nPixels) && (nRunLength<63) ) {  // Continue run
        nRunLength++;
      }
      else {
        // Store run, if pixel differs or runlength is 63
        *pCompressedData++ = 0xC0 | nRunLength;
        *pCompressedData++ = nPixels;
        bDoingRun = FALSE;              // Force next code to output
      }
    }

    if (!bDoingRun) {
      if ( (Pixel==*pRGBLine) || (Pixel>=0xC0) ) {  // Start run
        nRunLength = 1;
        nPixels = Pixel;
        bDoingRun = TRUE;
      }
      else {
        // Just store pixel
        *pCompressedData++ = Pixel;
      }
    }
  }

  // Complete run
  if (bDoingRun) {
    *pCompressedData++ = 0xC0 | nRunLength;
    *pCompressedData++ = nPixels;
  }

  return(pCompressedData);
}

//-----------------------------------------------------------------------
/*
  Save screen shot as .PCX
*/
void PCX_SaveScreenShot(char *pszFileName)
{
/* FIXME */
/*
  PCXHEADER *pPCXHeader;
  unsigned short int *pSrcImage;
  unsigned char *pSingleRGBLine, *pWorkRGBLine_Red, *pWorkRGBLine_Green, *pWorkRGBLine_Blue;
  unsigned char *pCompressedPCX, *pCompressedData;
  unsigned short int Pixel;
  unsigned short int Red,Green,Blue;
  int x,y,nBytesPerLine;

  // Allocate workspace for compression, over-estimate for compressed data
  pSingleRGBLine = (unsigned char *)Memory_Alloc(1024*3);
  pCompressedData = pCompressedPCX = (unsigned char *)Memory_Alloc((ScreenBMP.InfoHeader.biWidth*abs(ScreenBMP.InfoHeader.biHeight)*3)*2);

  // Create our PCX header
  pPCXHeader = (PCXHEADER *)pCompressedData;
  pCompressedData += sizeof(PCXHEADER);
  Memory_Clear(pPCXHeader,sizeof(PCXHEADER));
  pPCXHeader->Manufact = 10;                // ZSoft PCX
  pPCXHeader->Version = 5;
  pPCXHeader->Encoded = 1;                // RLE encoding
  pPCXHeader->BitsPerPixel = 8;
  pPCXHeader->XMin = 0;
  pPCXHeader->YMin = 0;
  pPCXHeader->XMax = ScreenBMP.InfoHeader.biWidth-1;
  pPCXHeader->YMax = abs(-ScreenBMP.InfoHeader.biHeight)-1;
  pPCXHeader->NumPlanes = 3;                // 24-bit
  pPCXHeader->BytesPerLine = nBytesPerLine = (ScreenBMP.InfoHeader.biWidth+1)&0xfffffffe;
  pPCXHeader->PaletteInfo = 1;

  // Compress picture
  for(y=0; y<abs(ScreenBMP.InfoHeader.biHeight); y++) {
    // Get pointer into our 16-bit screen
    pSrcImage = (unsigned short int *)pScreenBitmap;
    pSrcImage += ScreenBMP.InfoHeader.biWidth*y;

    // Convert to 24-bit RGB
    pWorkRGBLine_Red = pSingleRGBLine;
    pWorkRGBLine_Green = pWorkRGBLine_Red+nBytesPerLine;
    pWorkRGBLine_Blue = pWorkRGBLine_Green+nBytesPerLine;
    for(x=0; x<ScreenBMP.InfoHeader.biWidth; x++) {
      // Read 16-bit pixel, as RGB 0x1555
      Pixel = *pSrcImage++;
      // Split into Red,Green,Blue(range 0...255)
      Red = ((Pixel>>10)&0x1f)<<3;
      Green = ((Pixel>>5)&0x1f)<<3;
      Blue = (Pixel&0x1f)<<3;
      // And store as Red Plane, Green Plane and Blue Plane
      *pWorkRGBLine_Red++ = Red;
      *pWorkRGBLine_Green++ = Green;
      *pWorkRGBLine_Blue++ = Blue;
    }

    // Compress each of the 3 planes
    pCompressedData = PCX_CompressPlanes(pSingleRGBLine,pCompressedData,nBytesPerLine*3);
  }

  // And save
  File_Save(NULL,pszFileName,pCompressedPCX,pCompressedData-pCompressedPCX,FALSE);

  // Free workspace
  Memory_Free(pCompressedPCX);
  Memory_Free(pSingleRGBLine);
*/

}

//-----------------------------------------------------------------------
/*
  Save screen shot as .PCX using monochrome 1-bit(2 colours)
*/
void PCX_SaveScreenShot_Mono(char *pszFileName)
{
/* FIXME */
/*
  PCXHEADER *pPCXHeader;
  unsigned char *pSrcImage;
  unsigned char *pSingleLine;
  unsigned char *pCompressedPCX, *pCompressedData;
  int x,y,nBytesPerLine;

  // Allocate workspace for compression, over-estimate for compressed data
  pSingleLine = (unsigned char *)Memory_Alloc(1024*2);
  pCompressedData = pCompressedPCX = (unsigned char *)Memory_Alloc((ScreenBMP.InfoHeader.biWidth/3*abs(ScreenBMP.InfoHeader.biHeight))*2);

  // Create our PCX header
  pPCXHeader = (PCXHEADER *)pCompressedData;
  pCompressedData += sizeof(PCXHEADER);
  Memory_Clear(pPCXHeader,sizeof(PCXHEADER));
  pPCXHeader->Manufact = 10;                // ZSoft PCX
  pPCXHeader->Version = 0;
  pPCXHeader->Encoded = 1;                // RLE encoding
  pPCXHeader->BitsPerPixel = 1;
  pPCXHeader->XMin = 0;
  pPCXHeader->YMin = 0;
  pPCXHeader->XMax = ScreenBMP.InfoHeader.biWidth-1;
  pPCXHeader->YMax = abs(-ScreenBMP.InfoHeader.biHeight)-1;
  pPCXHeader->NumPlanes = 1;                // 1-bit
  pPCXHeader->BytesPerLine = nBytesPerLine = ScreenBMP.InfoHeader.biWidth/8;
  pPCXHeader->PaletteInfo = 0;

  // Compress picture
  for(y=0; y<abs(ScreenBMP.InfoHeader.biHeight); y++) {
    // Get pointer into our 1-bit screen
    pSrcImage = (unsigned char *)pScreenBitmap;
    pSrcImage += (ScreenBMP.InfoHeader.biWidth/8)*y;

    // Copy to line buffer and NOT to swap black/white
    for(x=0; x<ScreenBMP.InfoHeader.biWidth/8; x++) {
      pSingleLine[x] = ~(*pSrcImage++);
    }

    // Compress it
    pCompressedData = PCX_CompressPlanes(pSingleLine,pCompressedData,nBytesPerLine);
  }

  // And save
  File_Save(NULL,pszFileName,pCompressedPCX,pCompressedData-pCompressedPCX,FALSE);

  // Free workspace
  Memory_Free(pCompressedPCX);
  Memory_Free(pSingleLine);
*/
}
