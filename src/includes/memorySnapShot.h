/*
  Hatari
*/

extern BOOL bSaveMemoryState, bRestoreMemoryState;

extern void MemorySnapShot_CheckSaveRestore(void);
extern void MemorySnapShot_Store(void *pData, int Size);
extern void MemorySnapShot_Capture(char *pszFileName);
extern void MemorySnapShot_Restore(char *pszFileName);
