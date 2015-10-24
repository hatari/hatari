/*
  Hatari - routines.h
  
  Definitions for the screen conversion routines

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_CONVERTROUTINES_H
#define HATARI_CONVERTROUTINES_H

static void ConvertLowRes_320x8Bit(void);
static void ConvertLowRes_640x8Bit(void);
static void Line_ConvertMediumRes_640x8Bit(Uint32 *edi, Uint32 *ebp, Uint32 *esi, Uint32 eax);
static void ConvertMediumRes_640x8Bit(void);

static void ConvertLowRes_320x16Bit(void);
static void ConvertLowRes_640x16Bit(void);
static void ConvertLowRes_320x16Bit_Spec(void);
static void Line_ConvertLowRes_640x16Bit_Spec(Uint32 *edi, Uint32 *ebp, Uint32 *esi, Uint32 eax);
static void ConvertLowRes_640x16Bit_Spec(void);
static void Line_ConvertMediumRes_640x16Bit(Uint32 *edi, Uint32 *ebp, Uint16 *esi, Uint32 eax);
static void ConvertMediumRes_640x16Bit(void);
static void Line_ConvertMediumRes_640x16Bit_Spec(Uint32 *edi, Uint32 *ebp, Uint16 *esi, Uint32 eax);
static void ConvertMediumRes_640x16Bit_Spec(void);

static void ConvertLowRes_320x32Bit(void);
static void ConvertLowRes_640x32Bit(void);
static void ConvertLowRes_320x32Bit_Spec(void);
static void Line_ConvertLowRes_640x32Bit_Spec(Uint32 *edi, Uint32 *ebp, Uint32 *esi, Uint32 eax);
static void ConvertLowRes_640x32Bit_Spec(void);
static void Line_ConvertMediumRes_640x32Bit(Uint32 *edi, Uint32 *ebp, Uint32 *esi, Uint32 eax);
static void ConvertMediumRes_640x32Bit(void);
static void Line_ConvertMediumRes_640x32Bit_Spec(Uint32 *edi, Uint32 *ebp, Uint32 *esi, Uint32 eax);
static void ConvertMediumRes_640x32Bit_Spec(void);

static void ConvertVDIRes_16Colour(void);
static void ConvertVDIRes_4Colour(void);
static void ConvertVDIRes_2Colour(void);

#endif /* HATARI_CONVERTROUTINES_H */
