/*
  Hatari

  Memory Functions
*/

#include "main.h"
#include "memAlloc.h"


/*-----------------------------------------------------------------------*/
/*
  Allocate memory from Windows
*/
void *Memory_Alloc(int nBytes)
{
  void *pAlloc;

  /* Allocate our memory */
  pAlloc = malloc(nBytes);
  if (pAlloc==NULL) {
    Main_SysError("Out of Memory!\n\nPlease close all running applications and\ncheck you are not running low on disc space.\n",PROG_NAME);
    exit(0);
  }

  return(pAlloc);
}


/*-----------------------------------------------------------------------*/
/*
  Free memory back to Windows
*/
void Memory_Free(void *pAlloc)
{
  /* Free our memory */
  free(pAlloc);
}


/*-----------------------------------------------------------------------*/
/*
  Set memory block to byte value
*/
void *Memory_Set(void *pAlloc, int c, size_t count)
{
  /* Set memory region */
  return(memset(pAlloc,c,count));
}


/*-----------------------------------------------------------------------*/
/*
  Set memory block to zero
*/
void *Memory_Clear(void *pAlloc, size_t count)
{
  /* Clear out memory region */
  return(memset(pAlloc,0x0,count));
}
