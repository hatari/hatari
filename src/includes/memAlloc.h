/*
  Hatari
*/

extern void *Memory_Alloc(int nBytes);
extern void Memory_Free(void *pAlloc);
extern void *Memory_Set(void *pAlloc, int c, size_t count);
extern void *Memory_Clear(void *pAlloc, size_t count);
