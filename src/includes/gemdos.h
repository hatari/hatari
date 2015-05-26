/*
  Hatari - gemdos.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_GEMDOS_H
#define HATARI_GEMDOS_H


typedef struct {
  char hd_emulation_dir[FILENAME_MAX];     /* hd emulation directory (Host OS) */
  char fs_currpath[FILENAME_MAX];          /* current path (Host OS) */
  int drive_number;                        /* drive number (C: = 2, D: = 3...) */
} EMULATEDDRIVE;

extern EMULATEDDRIVE **emudrives;
#define  GEMDOS_EMU_ON  (emudrives != NULL)

extern bool bInitGemDOS;

extern void GemDOS_Init(void);
extern void GemDOS_Reset(void);
extern void GemDOS_InitDrives(void);
extern void GemDOS_UnInitDrives(void);
extern void GemDOS_MemorySnapShot_Capture(bool bSave);
extern void GemDOS_CreateHardDriveFileName(int Drive, const char *pszFileName, char *pszDestName, int nDestNameLen);
extern bool GemDOS_IsDriveEmulated(int drive);
extern void GemDOS_Info(FILE *fp, Uint32 bShowOpcodes);
extern void GemDOS_OpCode(void);
extern void GemDOS_Boot(void);

#endif /* HATARI_GEMDOS_H */
