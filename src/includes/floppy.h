/*
  Hatari
*/

/* Structure for each drive connected as emulation */
typedef struct {
  unsigned char *pBuffer;
  char szFileName[MAX_FILENAME_LENGTH];
  int nImageBytes;
  BOOL bDiscInserted;
  BOOL bMediaChanged;
  BOOL bContentsChanged;
  BOOL bOKToSave;
} EMULATION_DRIVE;

#define NUM_EMULATION_DRIVES  2            /* A:, B: */
#define DRIVE_BUFFER_BYTES    (1536*1024)  /* 1.5Mb area for each drive */
#define NUMBYTESPERSECTOR    512           /* All discs are 512 bytes per sector */

extern EMULATION_DRIVE EmulationDrives[NUM_EMULATION_DRIVES];
extern int nBootDrive;
extern BOOL bFloppyChanged;
extern char *pszDiscImageNameExts[];

extern void Floppy_Init(void);
extern void Floppy_UnInit(void);
extern void Floppy_MemorySnapShot_Capture(BOOL bSave);
extern void Floppy_GetBootDrive(void);
extern BOOL Floppy_InsertDiscIntoDrive(int Drive, char *pszFileName);
extern BOOL Floppy_ZipInsertDiscIntoDrive(int Drive, char *pszFileName, char *pszZipPath);
extern void Floppy_EjectDiscFromDrive(int Drive,BOOL bInformUser);
extern void Floppy_EjectBothDrives(void);
extern void Floppy_FindDiscDetails(unsigned char *pBuffer,int nImageBytes,unsigned short int *pnSectorsPerTrack,unsigned short int *pnSides);
extern BOOL Floppy_ReadSectors(int Drive,char *pBuffer,unsigned short int Sector,unsigned short int Track,unsigned short int Side, short int Count, int *pnSectorsPerTrack);
extern BOOL Floppy_WriteSectors(int Drive,char *pBuffer,unsigned short int Sector,unsigned short int Track,unsigned short int Side, short int Count, int *pnSectorsPerTrack);
extern int Floppy_GetPhysicalSectorsPerTrack(int Drive);
extern BOOL Floppy_ReadPhysicalSector(int Drive,char *pBuffer,unsigned short int Sector,unsigned short int Track,unsigned short int Side,unsigned short int Count);
