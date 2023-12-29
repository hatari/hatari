/*
  Hatari - tos.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_TOS_H
#define HATARI_TOS_H

/*
 * List of language and keyboard layout country codes supported
 * by currently available TOS versions.  Subset of:
 * https://freemint.github.io/tos.hyp/en/bios_cookiejar.html#Cookie_2C_20_AKP
 */
enum {
      TOS_LANG_US = 0,
      TOS_LANG_DE = 1,
      TOS_LANG_FR = 2,
      TOS_LANG_UK = 3,
      TOS_LANG_ES = 4,
      TOS_LANG_IT = 5,
      TOS_LANG_SE = 6,
      TOS_LANG_CH_FR = 7,
      TOS_LANG_CH_DE = 8,
      TOS_LANG_TR = 9,
      TOS_LANG_FI = 10,
      TOS_LANG_NO = 11,
      TOS_LANG_DK = 12,
      TOS_LANG_SA = 13,
      TOS_LANG_NL = 14,
      TOS_LANG_CS = 15,
      TOS_LANG_HU = 16,
      TOS_LANG_PL = 17,
      TOS_LANG_RU = 19,
      TOS_LANG_RO = 24,
      TOS_LANG_GR = 31,
      TOS_LANG_ALL = 127,
      TOS_LANG_UNKNOWN = -1
};

extern bool bIsEmuTOS;
extern uint32_t EmuTosVersion;
extern uint16_t TosVersion;
extern uint32_t TosAddress, TosSize;
extern bool bTosImageLoaded;
extern bool bRamTosImage;
extern bool bUseTos;
extern unsigned int ConnectedDriveMask;
extern int nNumDrives;

extern void TOS_MemorySnapShot_Capture(bool bSave);
extern int TOS_InitImage(void);
extern void TOS_SetTestPrgName(const char *testprg);

extern int TOS_DefaultLanguage(void);
extern int TOS_ParseCountryCode(const char *code);
extern void TOS_ShowCountryCodes(void);
extern const char *TOS_LanguageName(int code);

#endif
