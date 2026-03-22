/*
  Hatari - routines.h
  
  Definitions for the screen conversion routines

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_CONVERTROUTINES_H
#define HATARI_CONVERTROUTINES_H

static void ConvertLowRes_320x32Bit(void);
static void ConvertLowRes_640x32Bit(void);
static void ConvertLowRes_320x32Bit_Spec(void);
static void Line_ConvertLowRes_640x32Bit_Spec(uint32_t *edi, uint32_t *ebp, uint32_t *esi, uint32_t eax);
static void ConvertLowRes_640x32Bit_Spec(void);
static void Line_ConvertMediumRes_640x32Bit(uint32_t *edi, uint32_t *ebp, uint32_t *esi, uint32_t eax);
static void ConvertMediumRes_640x32Bit(void);
static void Line_ConvertMediumRes_640x32Bit_Spec(uint32_t *edi, uint32_t *ebp, uint32_t *esi, uint32_t eax);
static void ConvertMediumRes_640x32Bit_Spec(void);

#endif /* HATARI_CONVERTROUTINES_H */
