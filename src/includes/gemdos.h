/*
  Hatari - gemdos.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_GEMDOS_H
#define HATARI_GEMDOS_H

/*
  GEMDOS error codes, See 'The Atari Compendium' D.3
*/
#define GEMDOS_EOK      0    // OK
#define GEMDOS_ERROR   -1    // Generic error
#define GEMDOS_EDRVNR  -2    // Drive not ready
#define GEMDOS_EUNCMD  -3    // Unknown command
#define GEMDOS_E_CRC   -4    // CRC error
#define GEMDOS_EBADRQ  -5    // Bad request
#define GEMDOS_E_SEEK  -6    // Seek error
#define GEMDOS_EMEDIA  -7    // Unknown media
#define GEMDOS_ESECNF  -8    // Sector not found
#define GEMDOS_EPAPER  -9    // Out of paper
#define GEMDOS_EWRITF  -10   // Write fault
#define GEMDOS_EREADF  -11   // Read fault
#define GEMDOS_EWRPRO  -12   // Device is write protected
#define GEMDOS_E_CHNG  -14   // Media change detected
#define GEMDOS_EUNDEV  -15   // Unknown device
#define GEMDOS_EINVFN  -32   // Invalid function
#define GEMDOS_EFILNF  -33   // File not found
#define GEMDOS_EPTHNF  -34   // Path not found
#define GEMDOS_ENHNDL  -35   // No more handles
#define GEMDOS_EACCDN  -36   // Access denied
#define GEMDOS_EIHNDL  -37   // Invalid handle
#define GEMDOS_ENSMEM  -39   // Insufficient memory
#define GEMDOS_EIMBA   -40   // Invalid memory block address
#define GEMDOS_EDRIVE  -46   // Invalid drive specification
#define GEMDOS_ENSAME  -48   // Cross device rename
#define GEMDOS_ENMFIL  -49   // No more files
#define GEMDOS_ELOCKED -58   // Record is already locked
#define GEMDOS_ENSLOCK -59   // Invalid lock removal request
#define GEMDOS_ERANGE  -64   // Range error
#define GEMDOS_EINTRN  -65   // Internal error
#define GEMDOS_EPLFMT  -66   // Invalid program load format
#define GEMDOS_EGSBF   -67   // Memory block growth failure
#define GEMDOS_ELOOP   -80   // Too many symbolic links
#define GEMDOS_EMOUNT  -200  // Mount point crossed (indicator)

/*
  GemDOS file attributes
*/
#define GEMDOS_FILE_ATTRIB_READONLY      0x01
#define GEMDOS_FILE_ATTRIB_HIDDEN        0x02
#define GEMDOS_FILE_ATTRIB_SYSTEM_FILE   0x04
#define GEMDOS_FILE_ATTRIB_VOLUME_LABEL  0x08
#define GEMDOS_FILE_ATTRIB_SUBDIRECTORY  0x10
#define GEMDOS_FILE_ATTRIB_WRITECLOSE    0x20

/*
  Disk Tranfer Address (DTA)
*/
#define TOS_NAMELEN  14

typedef struct {
  unsigned char index[2];
  unsigned char magic[4];
  char dta_pat[TOS_NAMELEN];
  char dta_sattrib;
  char dta_attrib;
  unsigned char dta_time[2];
  unsigned char dta_date[2];
  unsigned char dta_size[4];
  char dta_name[TOS_NAMELEN];
} DTA;

#define DTA_MAGIC_NUMBER  0x12983476
#define MAX_DTAS_FILES    256      /* Must be ^2 */
#define CALL_PEXEC_ROUTINE 3       /* Call our cartridge pexec routine */

#define  BASE_FILEHANDLE     64    /* Our emulation handles - MUST not be valid TOS ones, but MUST be <256 */
#define  MAX_FILE_HANDLES    32    /* We can allow 32 files open at once */

/* 
   DateTime structure used by TOS call $57 f_dattime 
   Changed to fix potential problem with alignment.
*/
typedef struct {
  unsigned short word1;
  unsigned short word2;
} DATETIME;


#ifndef MAX_PATH
#define MAX_PATH 256
#endif

typedef struct {
  char hd_emulation_dir[MAX_PATH];         /* hd emulation directory */
  char fs_currpath[MAX_PATH];              /* current path */
  int hd_letter;                           /* drive letter */
} EMULATEDDRIVE;

extern EMULATEDDRIVE **emudrives;

#define  ISHARDDRIVE(Drive)  (Drive!=-1)
#define  GEMDOS_EMU_ON  (emudrives != NULL)

extern BOOL bInitGemDOS;
extern unsigned short int CurrentDrive;

extern void GemDOS_Init(void);
extern void GemDOS_Reset(void);
extern void GemDOS_InitDrives(void);
extern void GemDOS_UnInitDrives(void);
extern void GemDOS_MemorySnapShot_Capture(BOOL bSave);
extern void GemDOS_CreateHardDriveFileName(int Drive, const char *pszFileName, char *pszDestName);
extern BOOL GemDOS(void);
extern void GemDOS_OpCode(void);
extern void GemDOS_RunOldOpCode(void);
extern void GemDOS_Boot(void);

#endif /* HATARI_GEMDOS_H */
