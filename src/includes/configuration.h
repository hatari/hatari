/*
  Hatari
*/

extern BOOL bFirstTimeInstall;

extern void Configuration_SetDefault(void);
extern void Configuration_Init(void);
extern void Configuration_UnInit(void);
extern BOOL Configuration_OpenFileToWrite(void);
extern BOOL Configuration_OpenFileToRead(void);
extern void Configuration_CloseFile(void);
extern void Configuration_WriteToFile(void *pData,int nBytes);
extern void Configuration_ReadFromFile(void *pData,int nBytes);
